#include "SshFileTransfer.h"
#include "config/CredentialAccess.h"

#include "SshHostKeyStore.h"
#include "SshLibrary.h"
#include "../common/AppPaths.h"
#include <libssh2_sftp.h>

#include <QDir>
#include <QDateTime>
#include <QMutex>
#include <QDirIterator>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QUuid>
#include <QtConcurrentRun>
#include <algorithm>

namespace {
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
                  const std::atomic_bool &cancelled, SecureCredentialStore *storedCredential) {
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
    const auto login = user.toUtf8();
    auto secret = password.toUtf8();
    if (secret.isEmpty() && storedCredential && storedCredential->isAvailable()) {
      static QMutex systemCredentialReads;
      QMutexLocker lock(&systemCredentialReads);
      checkCancelled(cancelled);
      if (!storedCredential->read(CredentialAccess::sshProfile(host, port, user), &secret, &error)) {
        secret.fill(0);
        fail(error.isEmpty() ? QStringLiteral("无法读取系统 SSH 凭据") : error);
      }
    }
    if (cancelled.load() || secret.isEmpty()) {
      secret.fill(0);
      checkCancelled(cancelled);
      fail(QStringLiteral("未找到该连接的 SSH 密码，请手动输入 SFTP 密码"));
    }
    const auto auth = libssh2_userauth_password_ex(session, login.constData(), unsigned(login.size()),
                                                   secret.constData(), unsigned(secret.size()), nullptr);
    secret.fill(0);
    if (auth != 0)
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

} // namespace

SshFileTransfer::SshFileTransfer(QObject *parent) : SshFileTransfer(AppPaths::writableDataRoot(), parent) {}
SshFileTransfer::SshFileTransfer(QString dataRoot, QObject *parent, std::shared_ptr<SecureCredentialStore> credentials)
    : QObject(parent), m_dataRoot(std::move(dataRoot)),
      m_credentials(credentials ? std::move(credentials) : std::make_shared<SecureCredentialStore>(SecureCredentialStore::Domain::Ssh)) {
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
         connection.value("port").toInt(), connection.value("useStoredSshPassword").toBool()});
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
  m_watcher.setFuture(QtConcurrent::run([job = std::move(job), root = m_dataRoot, state = m_state, credentials = m_credentials] {
    return execute(job, root, *state, *credentials);
  }));
  emit busyChanged();
}

QVariantMap SshFileTransfer::execute(const Job &job, const QString &dataRoot, State &state,
                                    SecureCredentialStore &credentials) {
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
    if (job.password.isEmpty() && !job.useStoredSshPassword) fail(QStringLiteral("请输入 SFTP 密码"));
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
    if (upload && !source.open(QIODevice::ReadOnly)) fail(source.errorString());
    QTemporaryFile target(local.filePath(QStringLiteral(".loganalyqt-XXXXXX.part")));
    if (download && !target.open()) fail(target.errorString());
    SshHostKeyStore trust(QDir(dataRoot).filePath(QStringLiteral("ssh-known-hosts.json")));
    SftpConnection sftp;
    const auto fingerprint = sftp.connect(job.host, job.port, job.user, job.password, trust,
                                          job.acceptedFingerprint, cancelled,
                                          job.useStoredSshPassword ? &credentials : nullptr);
    if (!fingerprint.isEmpty()) return {{"fingerprint", fingerprint}};
    const auto encodedPath = sftp.realPath(job.remotePath.isEmpty() ? QStringLiteral(".") : job.remotePath);
    const auto path = QString::fromUtf8(encodedPath);
    if (!path.startsWith('/') || path.toUtf8() != encodedPath || !validText(path) || path.contains('\\'))
      fail(QStringLiteral("远程目录路径无法安全识别"));
    if (job.action == "list") return listing(path, sftp.list(encodedPath, cancelled));
    checkCancelled(cancelled);
    const auto remote = remoteChild(path, job.name).toUtf8();
    if (upload) sftp.upload(remote, remoteChild(path, temporaryName()).toUtf8(), source, cancelled, state.transferred);
    else if (download) sftp.download(remote, target, cancelled, state.transferred);
    else if (mkdir) sftp.mkdir(remote);
    else sftp.rename(remote, remoteChild(path, job.newName).toUtf8());
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
