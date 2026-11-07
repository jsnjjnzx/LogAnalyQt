#include "SshFileTransfer.h"

#include "SshHostKeyStore.h"
#include "SshLibrary.h"
#include "../common/AppPaths.h"
#include <libssh2_sftp.h>

#include <QDir>
#include <QDateTime>
#include <QDirIterator>
#include <QProcess>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QUuid>
#include <QUrl>
#include <QtConcurrentRun>
#include <algorithm>

namespace {
constexpr int MaxListing = 8 * 1024 * 1024;
constexpr int MaxEntries = 20000;

[[noreturn]] void fail(const QString &error) { throw error; }
void checkCancelled(const std::atomic_bool &cancelled) {
  if (cancelled.load()) fail(QStringLiteral("已取消"));
}

bool validText(const QString &text) {
  return std::none_of(text.cbegin(), text.cend(), [](QChar c) { return c.category() == QChar::Other_Control; });
}
bool validName(const QString &name) {
  return !name.isEmpty() && name != "." && name != ".." && validText(name)
      && !name.contains('/') && !name.contains('\\');
}
bool validLocalName(const QString &name) {
  if (!validName(name)) return false;
#ifdef Q_OS_WIN
  static const QRegularExpression invalid(
      QStringLiteral(R"([<>:"|?*]|[. ]$|^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³]) *(\.|$))"),
      QRegularExpression::CaseInsensitiveOption);
  if (invalid.match(name).hasMatch()) return false;
#endif
  return true;
}
QString remoteChild(const QString &path, const QString &name) {
  return path + (path.endsWith('/') ? QString{} : QStringLiteral("/")) + name;
}
void appendEntry(QVariantList &rows, const QString &name, bool directory, qint64 size, bool regular = true, qint64 modified = 0) {
  if (!validName(name)) return;
  if (rows.size() >= MaxEntries) fail(QStringLiteral("目录项目超过 20,000，无法完整列出"));
  rows.append(QVariantMap{{"name", name}, {"directory", directory}, {"file", !directory && regular},
                          {"size", size}, {"modified", modified}});
}
QVariantMap listing(const QString &path, QVariantList rows) {
  std::sort(rows.begin(), rows.end(), [](const QVariant &left, const QVariant &right) {
    const auto a = left.toMap(), b = right.toMap();
    const bool aDirectory = a.value("directory").toBool(), bDirectory = b.value("directory").toBool();
    if (aDirectory != bDirectory) return aDirectory;
    return QString::compare(a.value("name").toString(), b.value("name").toString(), Qt::CaseInsensitive) < 0;
  });
  return {{"path", path}, {"entries", std::move(rows)}};
}

QString temporaryName() {
  return QStringLiteral(".loganalyqt-%1.part").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

class SftpConnection {
public:
  ~SftpConnection() {
    if (!session) return;
    libssh2_session_set_timeout(session, 1000);
    if (sftp) libssh2_sftp_shutdown(sftp);
    libssh2_session_free(session);
  }

  QString connect(const QString &host, int port, const QString &user, const QString &password,
                  SshHostKeyStore &trust, const QString &acceptedFingerprint,
                  const std::atomic_bool &cancelled) {
    socket.connectToHost(host, quint16(port));
    if (!socket.waitForConnected(10000)) fail(QStringLiteral("SFTP 连接失败：") + socket.errorString());
    checkCancelled(cancelled);
    if (!initializeSshLibrary() || !(session = libssh2_session_init()))
      fail(QStringLiteral("无法初始化 SFTP"));
    libssh2_session_set_timeout(session, 8000);
    if (libssh2_session_handshake(session, libssh2_socket_t(socket.socketDescriptor())) != 0)
      sshError(QStringLiteral("SFTP 握手失败"));
    checkCancelled(cancelled);
    size_t keyLength = 0;
    const char *keyData = libssh2_session_hostkey(session, &keyLength, nullptr);
    if (!keyData || !keyLength) fail(QStringLiteral("无法读取 SFTP 主机密钥"));
    const QByteArray key(keyData, qsizetype(keyLength));
    const auto fingerprint = SshHostKeyStore::fingerprint(key);
    QString error;
    if (!acceptedFingerprint.isEmpty() && fingerprint != acceptedFingerprint)
      fail(QStringLiteral("SFTP 主机指纹在确认后发生变化"));
    switch (trust.check(host, port, key, &error)) {
    case SshHostKeyStore::Changed: fail(QStringLiteral("SFTP 主机密钥已变化，已阻止连接"));
    case SshHostKeyStore::Error: fail(error);
    case SshHostKeyStore::Unknown:
      if (acceptedFingerprint.isEmpty()) return fingerprint;
      if (!trust.trust(host, port, key, &error)) fail(error);
      break;
    case SshHostKeyStore::Trusted: break;
    }
    checkCancelled(cancelled);
    const auto login = user.toUtf8(), secret = password.toUtf8();
    if (libssh2_userauth_password_ex(session, login.constData(), unsigned(login.size()),
                                    secret.constData(), unsigned(secret.size()), nullptr) != 0)
      sshError(QStringLiteral("SFTP 密码认证失败"));
    if (!(sftp = libssh2_sftp_init(session))) sshError(QStringLiteral("服务器未能打开 SFTP 子系统"));
    return {};
  }

  QByteArray realPath(const QString &path) {
    char buffer[8192];
    const auto input = path.toUtf8();
    const int count = libssh2_sftp_realpath(sftp, input.constData(), buffer, sizeof(buffer));
    if (count <= 0) sftpError(QStringLiteral("无法定位远程目录"));
    return QByteArray(buffer, count);
  }

  QVariantList list(const QByteArray &path, const std::atomic_bool &cancelled) {
    Handle handle(libssh2_sftp_opendir(sftp, path.constData()), libssh2_sftp_close_handle);
    if (!handle) sftpError(QStringLiteral("无法打开远程目录"));
    QVariantList rows;
    for (;;) {
      checkCancelled(cancelled);
      char name[4096];
      LIBSSH2_SFTP_ATTRIBUTES attr{};
      const int size = libssh2_sftp_readdir(handle.get(), name, sizeof(name), &attr);
      if (size == 0) return rows;
      if (size < 0) sftpError(QStringLiteral("无法读取远程目录"));
      const auto decoded = QString::fromUtf8(name, size);
      if (decoded.toUtf8() != QByteArrayView(name, size)) continue;
      const bool directory = LIBSSH2_SFTP_S_ISDIR(attr.permissions);
      if (directory || LIBSSH2_SFTP_S_ISREG(attr.permissions))
        appendEntry(rows, decoded, directory, attr.flags & LIBSSH2_SFTP_ATTR_SIZE ? qint64(attr.filesize) : -1,
                    true, attr.flags & LIBSSH2_SFTP_ATTR_ACMODTIME ? qint64(attr.mtime) * 1000 : 0);
    }
  }

  void mkdir(const QByteArray &path) {
    ensureMissing(path);
    if (libssh2_sftp_mkdir(sftp, path.constData(), 0755) != 0)
      sftpError(QStringLiteral("无法新建远程文件夹"));
  }

  void rename(const QByteArray &oldPath, const QByteArray &newPath) {
    LIBSSH2_SFTP_ATTRIBUTES attr{};
    if (libssh2_sftp_lstat(sftp, oldPath.constData(), &attr) != 0 ||
        (!LIBSSH2_SFTP_S_ISDIR(attr.permissions) && !LIBSSH2_SFTP_S_ISREG(attr.permissions)))
      fail(QStringLiteral("远程项目不存在或类型不支持"));
    ensureMissing(newPath);
    renameNoReplace(oldPath, newPath, QStringLiteral("无法重命名远程项目"));
  }

  void download(const QByteArray &path, QFileDevice &target, const std::atomic_bool &cancelled,
                std::atomic<qint64> &progress) {
    LIBSSH2_SFTP_ATTRIBUTES attr{};
    if (libssh2_sftp_lstat(sftp, path.constData(), &attr) != 0 || !LIBSSH2_SFTP_S_ISREG(attr.permissions))
      sftpError(QStringLiteral("远程文件不存在或不是常规文件"));
    Handle handle(libssh2_sftp_open(sftp, path.constData(), LIBSSH2_FXF_READ, 0), libssh2_sftp_close_handle);
    if (!handle) sftpError(QStringLiteral("无法打开远程文件"));
    char buffer[65536];
    for (;;) {
      checkCancelled(cancelled);
      const auto count = libssh2_sftp_read(handle.get(), buffer, sizeof(buffer));
      if (count < 0) sftpError(QStringLiteral("下载失败"));
      if (!count) break;
      if (target.write(buffer, count) != count) fail(target.errorString());
      progress.fetch_add(count, std::memory_order_relaxed);
    }
    if (libssh2_sftp_close(handle.release()) != 0) sftpError(QStringLiteral("无法完成下载"));
  }

  void upload(const QByteArray &path, const QByteArray &temporary, QFileDevice &source,
              const std::atomic_bool &cancelled, std::atomic<qint64> &progress) {
    ensureMissing(path);
    Handle handle(libssh2_sftp_open(sftp, temporary.constData(),
          LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL, 0644), libssh2_sftp_close_handle);
    if (!handle) sftpError(QStringLiteral("无法创建远程暂存文件"));
    try {
      char buffer[65536];
      for (;;) {
        checkCancelled(cancelled);
        const auto count = source.read(buffer, sizeof(buffer));
        if (count < 0) fail(source.errorString());
        if (!count) break;
        for (qint64 offset = 0; offset < count;) {
          checkCancelled(cancelled);
          const auto written = libssh2_sftp_write(handle.get(), buffer + offset, size_t(count - offset));
          if (written <= 0) sftpError(QStringLiteral("上传失败"));
          offset += written;
          progress.fetch_add(written, std::memory_order_relaxed);
        }
      }
      if (libssh2_sftp_close(handle.release()) != 0) sftpError(QStringLiteral("无法完成上传"));
      checkCancelled(cancelled);
      renameNoReplace(temporary, path, QStringLiteral("无法交付上传文件（目标可能已存在）"));
    } catch (const QString &error) {
      handle.reset();
      if (libssh2_sftp_unlink(sftp, temporary.constData()) != 0)
        fail(error + QStringLiteral("；无法清理远程暂存文件：") + QString::fromUtf8(temporary));
      throw;
    }
  }

private:
  using Handle = std::unique_ptr<LIBSSH2_SFTP_HANDLE, decltype(&libssh2_sftp_close_handle)>;
  void ensureMissing(const QByteArray &path) {
    LIBSSH2_SFTP_ATTRIBUTES attr{};
    if (libssh2_sftp_lstat(sftp, path.constData(), &attr) == 0)
      fail(QStringLiteral("远程已存在同名项目"));
    if (libssh2_sftp_last_error(sftp) != LIBSSH2_FX_NO_SUCH_FILE)
      sftpError(QStringLiteral("无法检查远程目标"));
  }
  void renameNoReplace(const QByteArray &oldPath, const QByteArray &newPath, const QString &error) {
    if (libssh2_sftp_rename_ex(sftp, oldPath.constData(), unsigned(oldPath.size()),
          newPath.constData(), unsigned(newPath.size()), 0) != 0) sftpError(error);
  }
  [[noreturn]] void sshError(const QString &message) {
    char *detail = nullptr;
    int length = 0;
    libssh2_session_last_error(session, &detail, &length, 0);
    fail(detail && length > 0 ? message + QStringLiteral("：") + QString::fromUtf8(detail, length) : message);
  }
  [[noreturn]] void sftpError(const QString &message) {
    fail(message + QStringLiteral("（SFTP 状态 %1）").arg(libssh2_sftp_last_error(sftp)));
  }
  QTcpSocket socket;
  LIBSSH2_SESSION *session = nullptr;
  LIBSSH2_SFTP *sftp = nullptr;
};

QString curlProgram() {
#ifdef Q_OS_WIN
  const QString systemRoot = qEnvironmentVariable("SystemRoot");
  return systemRoot.isEmpty() ? QString{} : QDir(systemRoot).filePath(QStringLiteral("System32/curl.exe"));
#else
  return QStringLiteral("/usr/bin/curl");
#endif
}

struct CurlResult { int code; QByteArray output; QString error; };

class FtpConnection {
public:
  FtpConnection(const QString &host, int port, const QString &user, const QString &password,
                const std::atomic_bool &cancelled, std::atomic<qint64> &progress)
      : cancelled(cancelled), progress(progress) {
    QUrl base;
    base.setScheme(QStringLiteral("ftp"));
    base.setHost(host);
    base.setPort(port);
    if (!base.isValid() || base.host().isEmpty() || host.contains('%'))
      fail(QStringLiteral("FTP 主机地址无效"));
    endpoint = base.toEncoded();
    if (user.contains(':')) fail(QStringLiteral("FTP 用户名不能包含冒号"));
    auto login = (user + ':' + password).toUtf8();
    login.replace("\\", "\\\\").replace("\"", "\\\"");
    config = "user = \"" + login + "\"\n";
  }

  QVariantList list(const QString &path) const {
    const auto result = request({"--request", "MLSD"}, path, true);
    if (result.code == 0) return parseMlsd(result.output);
    if (result.code != 19 && result.code != 21 && result.code != 78) verify(result);
    const auto legacy = request({}, path, true);
    verify(legacy);
    return parseList(legacy.output);
  }

  void download(const QString &directory, const QString &name, QFileDevice &target) const {
    const auto rows = list(directory);
    for (const auto &entry : rows) {
      const auto row = entry.toMap();
      if (row.value("name").toString() == name && row.value("file").toBool()) {
        verify(request({}, remoteChild(directory, name), false, &target));
        return;
      }
    }
    fail(QStringLiteral("远程文件不存在或不是常规文件"));
  }

  void mkdir(const QString &directory, const QString &name) const {
    ensureMissing(list(directory), name);
    checkCancelled(cancelled);
    verify(request({"--head", "--quote", "MKD " + remoteChild(directory, name)}, directory, true, nullptr, false));
  }

  void rename(const QString &directory, const QString &oldName, const QString &newName) const {
    const auto rows = list(directory);
    ensureMissing(directory, rows, newName);
    for (const auto &entry : rows) {
      const auto row = entry.toMap();
      if (row.value("name").toString() == oldName && (row.value("directory").toBool() || row.value("file").toBool())) {
        renamePath(directory, remoteChild(directory, oldName), remoteChild(directory, newName));
        return;
      }
    }
    fail(QStringLiteral("远程项目不存在或类型不支持"));
  }

  void upload(const QString &directory, const QString &name, const QString &localFile) const {
    ensureMissing(directory, list(directory), name);
    const auto target = remoteChild(directory, name), temporary = remoteChild(directory, temporaryName());
    try {
      verify(request({"--upload-file", localFile}, temporary, false));
      ensureMissing(directory, list(directory), name);
      renamePath(directory, temporary, target);
    } catch (const QString &error) {
      const auto cleanup = request({"--head", "--quote", "DELE " + temporary}, directory, true, nullptr, false);
      if (cleanup.code != 0)
        fail(error + QStringLiteral("；如有远程暂存文件，请手动检查：") + temporary);
      throw;
    }
  }

private:
  void ensureMissing(const QString &directory, const QVariantList &rows, const QString &name) const {
    ensureMissing(rows, name);
    if (!name.startsWith('.')) return;
    // A server can omit dotfiles from its listing; RNFR alone probes the exact name without renaming it.
    const auto probe = request({"--head", "--quote", "RNFR " + remoteChild(directory, name),
                                "--write-out", "%{response_code}"}, directory, true);
    if (probe.code == 0) fail(QStringLiteral("远程已存在同名项目"));
    if (probe.code != 21 || probe.output.trimmed() != "550") verify(probe);
  }
  static void ensureMissing(const QVariantList &rows, const QString &name) {
    for (const auto &row : rows)
      if (row.toMap().value("name").toString().compare(name, Qt::CaseInsensitive) == 0)
        fail(QStringLiteral("远程已存在同名项目"));
  }
  void renamePath(const QString &directory, const QString &from, const QString &to) const {
    checkCancelled(cancelled);
    // shortcut: FTP RNTO has no no-replace flag; concurrent writers need SFTP.
    verify(request({"--head", "--quote", "RNFR " + from, "--quote", "RNTO " + to},
                   directory, true, nullptr, false));
  }
  static void verify(const CurlResult &result) {
    if (result.code) fail(QStringLiteral("FTP 操作失败（%1）：%2").arg(result.code).arg(result.error));
  }

  CurlResult request(const QStringList &args, const QString &path, bool directory,
                     QFileDevice *target = nullptr, bool canCancel = true) const {
    if (canCancel) checkCancelled(cancelled);
    const auto executable = curlProgram();
    if (executable.isEmpty() || !QFileInfo(executable).isExecutable())
      fail(QStringLiteral("未找到系统 curl，无法使用 FTP"));
    QString url = QString::fromLatin1(endpoint + '/' + QUrl::toPercentEncoding(path, "/"));
    if (directory && !url.endsWith('/')) url += '/';
    QProcess process;
    process.start(executable,
        QStringList{"-q", "--config", "-", "--silent", "--show-error", "--globoff", "--proto", "=ftp",
          "--noproxy", "*", "--connect-timeout", canCancel ? "10" : "4", "--speed-limit", "1",
          "--speed-time", canCancel ? "12" : "4", "--ftp-skip-pasv-ip"} +
        (canCancel ? QStringList{} : QStringList{"--max-time", "6"}) + args + QStringList{"--url", url});
    if (!process.waitForStarted(3000)) fail(QStringLiteral("无法启动系统 curl：") + process.errorString());
    process.write(config);
    process.closeWriteChannel();

    QByteArray output, error;
    do {
      process.waitForReadyRead(100);
      const auto part = process.readAllStandardOutput();
      error += process.readAllStandardError().left(qMax(0, 4096 - int(error.size())));
      if (canCancel && cancelled.load()) {
        process.kill(); process.waitForFinished(1000);
        checkCancelled(cancelled);
      }
      if (target) {
        if (target->write(part) != part.size()) {
          process.kill(); process.waitForFinished(1000);
          fail(target->errorString());
        }
        progress.fetch_add(part.size(), std::memory_order_relaxed);
      } else {
        if (output.size() + part.size() > MaxListing) {
          process.kill(); process.waitForFinished(1000);
          fail(QStringLiteral("FTP 目录响应过大"));
        }
        output += part;
      }
    } while (process.state() != QProcess::NotRunning);
    return {process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1,
            std::move(output), QString::fromUtf8(error).trimmed()};
  }

  static QVariantList parseMlsd(const QByteArray &data) {
    QVariantList rows;
    for (const auto &raw : data.split('\n')) {
      const auto line = raw.endsWith('\r') ? raw.chopped(1) : raw;
      if (line.isEmpty()) continue;
      const auto separator = line.indexOf(' ');
      if (separator <= 0) fail(QStringLiteral("FTP MLSD 目录格式无效"));
      const auto name = QString::fromUtf8(line.mid(separator + 1));
      if (name.toUtf8() != line.mid(separator + 1))
        fail(QStringLiteral("FTP 文件名不是 UTF-8，无法安全识别"));
      QByteArray type;
      qint64 size = -1, modified = 0;
      for (const auto &fact : line.left(separator).split(';')) {
        const auto key = fact.left(fact.indexOf('=')).toLower();
        if (key == "type") type = fact.mid(5).toLower();
        else if (key == "size") {
          bool ok = false;
          const auto value = fact.mid(5).toLongLong(&ok);
          if (ok && value >= 0) size = value;
        } else if (key == "modify") {
          const auto timestamp = QDateTime::fromString(QString::fromLatin1(fact.mid(7, 14)) + 'Z',
                                                       "yyyyMMddHHmmsst");
          if (timestamp.isValid()) modified = timestamp.toMSecsSinceEpoch();
        }
      }
      if (type == "dir" || type == "file") appendEntry(rows, name, type == "dir", size, true, modified);
      else if (type != "cdir" && type != "pdir" && validName(name)) appendEntry(rows, name, false, -1, false);
    }
    return rows;
  }

  static QVariantList parseList(const QByteArray &data) {
    static const QRegularExpression unixListing(
        R"(^([-dlbcps])\S*\s+\d+\s+\S+\s+\S+\s+(\d+)\s+\S+\s+\d+\s+\S+\s(.*)$)");
    static const QRegularExpression windowsListing(
        R"(^\d{2}-\d{2}-\d{2,4}\s+\d{1,2}:\d{2}(?:AM|PM)?\s+(<DIR>|\d+)\s+(.*)$)",
        QRegularExpression::CaseInsensitiveOption);
    QVariantList rows;
    for (const auto &raw : data.split('\n')) {
      const auto bytes = raw.endsWith('\r') ? raw.chopped(1) : raw;
      const auto line = QString::fromUtf8(bytes);
      if (line.toUtf8() != bytes) fail(QStringLiteral("FTP 文件名不是 UTF-8，无法安全识别"));
      if (line.trimmed().isEmpty() || line.startsWith("total ")) continue;
      auto match = unixListing.match(line);
      if (match.hasMatch()) {
        auto name = match.captured(3);
        const auto type = match.captured(1);
        if (type == "l") name = name.section(QStringLiteral(" -> "), 0, 0);
        appendEntry(rows, name, type == "d", type == "-" ? match.captured(2).toLongLong() : -1, type == "-");
        continue;
      }
      match = windowsListing.match(line);
      if (!match.hasMatch()) fail(QStringLiteral("服务器不支持 MLSD，且 LIST 目录格式无法识别"));
      appendEntry(rows, match.captured(2), match.captured(1).startsWith('<'), match.captured(1).toLongLong());
    }
    return rows;
  }

  const std::atomic_bool &cancelled;
  std::atomic<qint64> &progress;
  QByteArray endpoint, config;
};
} // namespace

SshFileTransfer::SshFileTransfer(QObject *parent) : SshFileTransfer(AppPaths::writableDataRoot(), parent) {}
SshFileTransfer::SshFileTransfer(QString dataRoot, QObject *parent) : QObject(parent), m_dataRoot(std::move(dataRoot)) {
  m_progressTimer.setInterval(120);
  connect(&m_progressTimer, &QTimer::timeout, this, &SshFileTransfer::syncProgress);
  connect(&m_watcher, &QFutureWatcher<QVariantMap>::finished, this, [this] {
    m_progressTimer.stop();
    syncProgress();
    const auto result = m_watcher.result();
    const auto action = m_job.action;
    const auto fingerprint = result.value("fingerprint").toString();
    const bool ask = !m_state->cancelled.load() && !fingerprint.isEmpty();
    if (ask) m_job.acceptedFingerprint = fingerprint;
    else m_job = {};
    m_busy = false;
    emit busyChanged();
    if (ask) emit hostKeyRequired(SshHostKeyStore::endpoint(m_job.host, m_job.port), fingerprint);
    else if (!fingerprint.isEmpty()) emit finished(action, {{"error", QStringLiteral("已取消")}});
    else emit finished(action, result);
  });
}

SshFileTransfer::~SshFileTransfer() { cancel(); }

QString SshFileTransfer::homePath() const { return QDir::homePath(); }
QString SshFileTransfer::parentPath(const QString &path) const {
  QDir directory(path);
  directory.cdUp();
  return directory.absolutePath();
}

void SshFileTransfer::browseLocal(const QString &path) {
  Job job;
  job.action = QStringLiteral("local");
  job.localPath = path;
  start(std::move(job));
}

void SshFileTransfer::run(const QString &action, const QVariantMap &connection, const QString &remotePath,
                          const QString &localPath, const QString &name, const QString &newName) {
  start({action, connection.value("host").toString().trimmed(), connection.value("user").toString(),
         connection.value("password").toString(), remotePath, localPath, name, newName, {},
         connection.value("port").toInt(), connection.value("ftp").toBool()});
}

void SshFileTransfer::confirmHost(bool accept) {
  if (m_busy || m_job.acceptedFingerprint.isEmpty()) return;
  auto job = std::exchange(m_job, {});
  if (accept) start(std::move(job));
  else emit finished(job.action, {{"error", QStringLiteral("未信任 SFTP 主机，未发送密码")}});
}

void SshFileTransfer::cancel() {
  if (m_state) m_state->cancelled.store(true);
  if (!m_busy) m_job = {};
}

void SshFileTransfer::syncProgress() {
  const auto bytes = m_state->transferred.load(std::memory_order_relaxed);
  if (m_transferredBytes == bytes) return;
  m_transferredBytes = bytes;
  emit transferredBytesChanged();
}

void SshFileTransfer::start(Job job) {
  if (m_busy) return;
  m_job = job;
  m_busy = true;
  m_state = std::make_shared<State>();
  syncProgress();
  if (job.action == "upload" || job.action == "download") m_progressTimer.start();
  m_watcher.setFuture(QtConcurrent::run([job = std::move(job), root = m_dataRoot, state = m_state] {
    return execute(job, root, *state);
  }));
  emit busyChanged();
}

QVariantMap SshFileTransfer::execute(const Job &job, const QString &dataRoot, State &state) {
  try {
    const auto &cancelled = state.cancelled;
    checkCancelled(cancelled);
    QDir local(job.localPath);
    const bool localMkdir = job.action == "localMkdir", localRename = job.action == "localRename";
    if (localMkdir || localRename) {
      const auto &newName = localRename ? job.newName : job.name;
      if (!QFileInfo(job.localPath).isAbsolute() || !local.exists() || !validLocalName(newName)
          || (localRename && !validLocalName(job.name)))
        fail(QStringLiteral("本地目录或名称无效"));
      const QFileInfo destination(local.filePath(newName));
      if (destination.exists() || destination.isSymLink()) fail(QStringLiteral("本地已存在同名项目"));
      if (localRename) {
        const QFileInfo source(local.filePath(job.name));
        if (source.isSymLink() || (!source.isFile() && !source.isDir()))
          fail(QStringLiteral("本地项目不存在或类型不支持"));
        if (!local.rename(job.name, newName)) fail(QStringLiteral("无法重命名本地项目"));
      } else if (!local.mkdir(newName)) fail(QStringLiteral("无法新建本地文件夹"));
      return {{"name", newName}};
    }
    if (job.action == "local") {
      if (!QFileInfo(job.localPath).isAbsolute() || !local.exists() || !QFileInfo(job.localPath).isReadable())
        fail(QStringLiteral("无法读取本地目录"));
      QVariantList rows;
      QDirIterator it(local.absolutePath(), QDir::Dirs | QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks);
      while (it.hasNext()) {
        checkCancelled(cancelled);
        it.next();
        const auto info = it.fileInfo();
        appendEntry(rows, info.fileName(), info.isDir(), info.size(), info.isFile(),
                    info.lastModified().toMSecsSinceEpoch());
      }
      return listing(local.absolutePath(), std::move(rows));
    }
    const bool upload = job.action == "upload", download = job.action == "download";
    const bool mkdir = job.action == "mkdir", rename = job.action == "rename";
    if (!upload && !download && !mkdir && !rename && job.action != "list") fail(QStringLiteral("不支持的文件操作"));
    if ((mkdir || rename) && (!validName(job.name) || (rename && !validName(job.newName))))
      fail(QStringLiteral("远程名称无效"));
    if (job.host.isEmpty() || job.host.size() > 1024 || job.port < 1 || job.port > 65535 ||
        job.user.isEmpty() || job.user.size() > 1024 || job.password.size() > 4096 ||
        !validText(job.host) || !validText(job.user) || !validText(job.password) ||
        !validText(job.remotePath) || job.remotePath.toUtf8().size() > 4096 || job.remotePath.contains('\\'))
      fail(QStringLiteral("请检查主机、端口、账号、密码或远程路径"));
    if (!job.ftp && job.password.isEmpty()) fail(QStringLiteral("请输入 SFTP 密码"));
    const auto localFile = local.filePath(job.name);
    if (upload || download) {
      if (!validLocalName(job.name) || !QFileInfo(job.localPath).isAbsolute() || !local.exists())
        fail(QStringLiteral("文件名称或本地目录无效"));
      const QFileInfo info(localFile);
      if (upload && (!info.isFile() || info.isSymLink()))
        fail(QStringLiteral("请选择本地常规文件"));
      if (download && (info.exists() || info.isSymLink()))
        fail(QStringLiteral("本地已存在同名项目"));
    }
    QFile source(localFile);
    if (upload && !job.ftp && !source.open(QIODevice::ReadOnly)) fail(source.errorString());
    QTemporaryFile target(local.filePath(QStringLiteral(".loganalyqt-XXXXXX.part")));
    if (download && !target.open()) fail(target.errorString());
    QString path;
    if (job.ftp) {
      path = QDir::cleanPath(job.remotePath.startsWith('/') ? job.remotePath : '/' + job.remotePath);
      FtpConnection ftp(job.host, job.port, job.user, job.password, cancelled, state.transferred);
      if (job.action == "list") return listing(path, ftp.list(path));
      if (upload) ftp.upload(path, job.name, QFileInfo(localFile).absoluteFilePath());
      else if (download) ftp.download(path, job.name, target);
      else if (mkdir) ftp.mkdir(path, job.name);
      else ftp.rename(path, job.name, job.newName);
    } else {
      SshHostKeyStore trust(QDir(dataRoot).filePath(QStringLiteral("ssh-known-hosts.json")));
      SftpConnection sftp;
      const auto fingerprint = sftp.connect(job.host, job.port, job.user, job.password, trust,
                                            job.acceptedFingerprint, cancelled);
      if (!fingerprint.isEmpty()) return {{"fingerprint", fingerprint}};
      const auto encodedPath = sftp.realPath(job.remotePath.isEmpty() ? QStringLiteral(".") : job.remotePath);
      path = QString::fromUtf8(encodedPath);
      if (!path.startsWith('/') || path.toUtf8() != encodedPath || !validText(path) || path.contains('\\'))
        fail(QStringLiteral("远程目录路径无法安全识别"));
      if (job.action == "list") return listing(path, sftp.list(encodedPath, cancelled));
      checkCancelled(cancelled);
      const auto remote = remoteChild(path, job.name).toUtf8();
      if (upload) sftp.upload(remote, remoteChild(path, temporaryName()).toUtf8(), source, cancelled, state.transferred);
      else if (download) sftp.download(remote, target, cancelled, state.transferred);
      else if (mkdir) sftp.mkdir(remote);
      else sftp.rename(remote, remoteChild(path, job.newName).toUtf8());
    }
    if (download) {
      checkCancelled(cancelled);
      if (!target.flush()) fail(target.errorString());
      target.close();
      if (target.error() != QFileDevice::NoError) fail(target.errorString());
      // QFile::rename refuses an existing destination, including a file created during the transfer.
      if (!target.rename(localFile)) fail(target.errorString());
      target.setAutoRemove(false);
    }
    return {{"name", rename ? job.newName : job.name}};
  } catch (const QString &error) {
    return {{"error", error}};
  }
}
