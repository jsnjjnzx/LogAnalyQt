#include "SshTerminal.h"
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSet>
#include "../common/AppSettings.h"
#include "SshHostKeyStore.h"
#include <QHostInfo>
#include <QHostAddress>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QUuid>
#include <QtEndian>
#include <QJsonArray>
#include <cstring>
#include <cerrno>


// libssh2 和 socket 头文件
#include <libssh2.h>
#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

static bool validSshIdentity(const QString &host, int port, const QString &user) {
  auto hasControl = [](const QString &value) {
    for (const QChar c : value) if (c.category() == QChar::Other_Control) return true;
    return false;
  };
  return !host.trimmed().isEmpty() && !user.isEmpty() && port > 0 && port <= 65535 &&
         !hasControl(host) && !hasControl(user);
}

static QString sshCredentialProfile(const QString &host, int port,
                                    const QString &user) {
  const QByteArray identity = QJsonDocument(QJsonArray{
      host.trimmed().toLower(), port, user}).toJson(QJsonDocument::Compact);
  return QString::fromLatin1(QCryptographicHash::hash(
      identity, QCryptographicHash::Sha256).toHex());
}

class SshWorker : public QObject {
  Q_OBJECT
public:
  SshWorker(std::shared_ptr<SshSharedBuffer> buffer,
            std::shared_ptr<SecureCredentialStore> credentials,
            const QString &dataRoot)
      : m_buffer(std::move(buffer)), m_credentials(std::move(credentials)),
        m_dataRoot(dataRoot),
        m_trust(QDir(dataRoot).filePath(QStringLiteral("ssh-known-hosts.json"))) {}
  ~SshWorker() override { cleanup(); }

  void initializeCredentials(quint64 revision, const QString &host, int port,
                             const QString &user, int operation = 0,
                             const QString &password = {}) {
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
    Q_UNUSED(host); Q_UNUSED(port); Q_UNUSED(user); Q_UNUSED(operation); Q_UNUSED(password);
    emit credentialResult(revision, false, QString());
    return;
#endif
    if (operation == 0 && revision != m_buffer->credentialRevision.load()) {
      emit credentialResult(revision, false, QString());
      return;
    }
    const QString profile = sshCredentialProfile(host, port, user);
    QString error;
    bool exists = false;
    if (operation == 1) {
      if (password.isEmpty()) error = QStringLiteral("密码不能为空");
      else if (!m_credentials->writeVerified(profile, password.toUtf8(), &error) && error.isEmpty())
        error = QStringLiteral("SSH 密码保存失败");
    } else if (operation == 2) {
      if (!m_credentials->remove(profile, &error) && error.isEmpty())
        error = QStringLiteral("SSH 密码清除失败");
    }
    QString statusError;
    if (!m_credentials->contains(profile, &exists, &statusError)) {
      exists = false;
      if (statusError.isEmpty()) statusError = QStringLiteral("无法确认 SSH 已保存密码状态");
    }
    if (error.isEmpty()) error = statusError;
    emit credentialResult(revision, exists, error);
  }

public slots:
  void shutdown() { cleanup(); }
  void doConnect(quint64 generation, const QString &host, int port,
                 const QString &user, const QString &password) {
    cleanup();
    if (generation != m_buffer->activeGeneration.load()) return;
    m_generation = generation;
    m_host = host;
    m_port = port;
    m_user = user;
    m_password = password.toUtf8();
    m_overflowNotified = false;
#ifdef Q_OS_WIN
    static const bool socketsReady = [] {
      WSADATA data;
      return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!socketsReady) { fail(QStringLiteral("初始化 Windows socket 失败")); return; }
#endif
    m_phase = Resolving;
    m_deadline.start();
    if (!m_timer) {
      m_timer = new QTimer(this);
      m_timer->setInterval(10);
      connect(m_timer, &QTimer::timeout, this, &SshWorker::advance);
    }
    m_timer->start();
    m_lookupId = QHostInfo::lookupHost(host, this,
        [this, generation](const QHostInfo &info) {
          if (generation != m_generation || m_phase != Resolving) return;
          m_lookupId = -1;
          if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
            fail(QStringLiteral("无法解析 SSH 主机名"));
            return;
          }
          m_addresses = info.addresses();
          tryNextAddress();
        });
  }
  void doDisconnect(quint64 generation) {
    if (generation != m_generation) return;
    cleanup();
    emit disconnected(generation);
  }
  void confirmHostKey(quint64 generation, const QString &requestId, bool accept) {
    if (generation != m_generation || generation != m_buffer->activeGeneration.load() ||
        m_phase != AwaitingTrust || requestId != m_requestId) return;
    if (!accept) {
      fail(QStringLiteral("已拒绝 SSH 主机密钥，未发送密码"), true);
      return;
    }
    QString error;
    if (!m_trust.trust(m_host, m_port, m_hostKey, &error)) {
      fail(error, true);
      return;
    }
    m_requestId.clear();
    m_phase = Authenticating;
    m_deadline.restart();
  }
  void doSendCommand(const QString &cmd, bool appendNewline,
                     const QString &encoding) {
    if (m_phase != Running) return;
    QByteArray data = encoding.toUpper().contains("GB") ? cmd.toLocal8Bit() : cmd.toUtf8();
    if (appendNewline && !data.endsWith('\n')) data.append('\n');
    if (m_outgoing.size() + data.size() > 1024 * 1024) {
      fail(QStringLiteral("SSH 待发送数据超过上限"));
      return;
    }
    m_outgoing.append(data);
  }

signals:
  void connected(quint64 generation);
  void errorOccurred(quint64 generation, const QString &error, bool securityFailure);
  void disconnected(quint64 generation);
  void bufferOverflowed();
  void hostKeyRequired(quint64 generation, const QString &host, int port,
                       const QString &algorithm, const QString &fingerprint,
                       const QString &requestId);
  void credentialResult(quint64 revision, bool exists, const QString &error);
  void credentialReadState(quint64 generation, bool busy);

private:
  enum Phase { Stopped, Resolving, TcpConnecting, Handshaking, AwaitingTrust,
               Authenticating, OpeningChannel, RequestingPty, RequestingShell, Running };
  void tryNextAddress() {
    if (m_sock != INVALID_SOCKET) { closesocket(m_sock); m_sock = INVALID_SOCKET; }
    while (!m_addresses.isEmpty()) {
      const QHostAddress address = m_addresses.takeFirst();
      sockaddr_storage storage{};
      socklen_t length;
      if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        auto *addr = reinterpret_cast<sockaddr_in *>(&storage);
        addr->sin_family = AF_INET;
        addr->sin_port = htons(m_port);
        addr->sin_addr.s_addr = htonl(address.toIPv4Address());
        length = sizeof(*addr);
      } else if (address.protocol() == QAbstractSocket::IPv6Protocol) {
        auto *addr = reinterpret_cast<sockaddr_in6 *>(&storage);
        addr->sin6_family = AF_INET6;
        addr->sin6_port = htons(m_port);
        const auto ipv6 = address.toIPv6Address();
        memcpy(&addr->sin6_addr, ipv6.c, 16);
        length = sizeof(*addr);
      } else continue;
      m_sock = socket(storage.ss_family, SOCK_STREAM, 0);
      if (m_sock == INVALID_SOCKET) continue;
#ifdef Q_OS_WIN
      u_long mode = 1;
      if (ioctlsocket(m_sock, FIONBIO, &mode) != 0) continue;
#else
      if (fcntl(m_sock, F_SETFL, fcntl(m_sock, F_GETFL) | O_NONBLOCK) < 0) continue;
      int noSigPipe = 1;
#ifdef SO_NOSIGPIPE
      setsockopt(m_sock, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
#endif
      const int result = ::connect(m_sock, reinterpret_cast<sockaddr *>(&storage), length);
#ifdef Q_OS_WIN
      const bool pending = WSAGetLastError() == WSAEWOULDBLOCK;
#else
      const bool pending = errno == EINPROGRESS;
#endif
      if (result == 0 || pending) { m_phase = TcpConnecting; return; }
      closesocket(m_sock);
      m_sock = INVALID_SOCKET;
    }
    fail(QStringLiteral("连接 SSH 服务器失败，请检查网络和端口"));
  }
  void fail(const QString &error, bool securityFailure = false) {
    const auto generation = m_generation;
    cleanup();
    emit errorOccurred(generation, error, securityFailure);
    emit disconnected(generation);
  }
  void appendData(const char *data, int size) {
    QMutexLocker lock(&m_buffer->mutex);
    constexpr int limit = 4 * 1024 * 1024;
    const int overflow = m_buffer->data.size() + size - limit;
    if (overflow > 0) {
      m_buffer->data.remove(0, overflow);
      if (!m_overflowNotified) { m_overflowNotified = true; emit bufferOverflowed(); }
    }
    m_buffer->data.append(data, size);
  }
  void advance() {
    if (m_phase == Stopped) return;
    if (m_generation != m_buffer->activeGeneration.load()) { cleanup(); return; }
    if (m_phase != Running && m_deadline.elapsed() >
        (m_phase == AwaitingTrust ? 300000 : 15000)) {
      fail(QStringLiteral("SSH 连接或确认超时"), m_phase == AwaitingTrust);
      return;
    }
    int rc = LIBSSH2_ERROR_EAGAIN;
    switch (m_phase) {
    case TcpConnecting: {
      fd_set writes, errors;
      FD_ZERO(&writes); FD_ZERO(&errors);
      FD_SET(m_sock, &writes); FD_SET(m_sock, &errors);
      timeval timeout{};
      if (select(static_cast<int>(m_sock) + 1, nullptr, &writes, &errors, &timeout) <= 0) return;
      int error = 0;
      socklen_t size = sizeof(error);
      if (getsockopt(m_sock, SOL_SOCKET, SO_ERROR,
                     reinterpret_cast<char *>(&error), &size) != 0 || error) {
        tryNextAddress(); return;
      }
      static const int initialized = libssh2_init(0);
      if (initialized != 0 || !(m_session = libssh2_session_init())) {
        fail(QStringLiteral("初始化 SSH 会话失败")); return;
      }
      libssh2_session_set_blocking(m_session, 0);
      m_phase = Handshaking;
      return;
    }
    case Handshaking:
      rc = libssh2_session_handshake(m_session, m_sock);
      if (rc == 0) {
        size_t length = 0; int type = 0;
        const char *key = libssh2_session_hostkey(m_session, &length, &type);
        if (!key || length == 0) { fail(QStringLiteral("SSH 未提供有效主机密钥"), true); return; }
        m_hostKey = QByteArray(key, static_cast<int>(length));
#ifndef Q_OS_MACOS
        m_phase = Authenticating;
        return;
#endif
        if (length < 4) { fail(QStringLiteral("SSH 主机密钥格式无效"), true); return; }
        const quint32 algorithmLength = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(key));
        if (algorithmLength == 0 || algorithmLength > length - 4) { fail(QStringLiteral("SSH 主机密钥格式无效"), true); return; }
        const QString algorithm = QString::fromLatin1(key + 4, static_cast<int>(algorithmLength));
        const QStringList algorithms{QStringLiteral("ssh-ed25519"), QStringLiteral("ssh-rsa"),
          QStringLiteral("ecdsa-sha2-nistp256"), QStringLiteral("ecdsa-sha2-nistp384"),
          QStringLiteral("ecdsa-sha2-nistp521")};
        if (!algorithms.contains(algorithm)) { fail(QStringLiteral("不支持的 SSH 主机密钥算法"), true); return; }
        QString error;
        const auto result = m_trust.check(m_host, m_port, m_hostKey, &error);
        if (result == SshHostKeyStore::Error) { fail(error, true); return; }
        if (result == SshHostKeyStore::Changed) {
          fail(QStringLiteral("SSH 主机密钥已变化：已停止自动重连。请通过独立渠道核实服务器，再人工处理 ssh-known-hosts.json。"), true);
          return;
        }
        if (result == SshHostKeyStore::Trusted) { m_phase = Authenticating; return; }
        m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_phase = AwaitingTrust;
        m_deadline.restart();
        emit hostKeyRequired(m_generation, m_host, m_port, algorithm,
                            SshHostKeyStore::fingerprint(m_hostKey), m_requestId);
        return;
      }
      break;
    case Authenticating: {
      if (!m_credentialsLoaded) {
        m_credentialsLoaded = true;
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        if (m_password.isEmpty()) {
          QString error;
          emit credentialReadState(m_generation, true);
          const bool read = m_credentials->read(sshCredentialProfile(m_host, m_port, m_user),
                                                &m_password, &error);
          emit credentialReadState(m_generation, false);
          if (m_generation != m_buffer->activeGeneration.load()) { cleanup(); return; }
          if (m_deadline.elapsed() > 15000) { fail(QStringLiteral("SSH 凭据读取超时，未发送密码"), true); return; }
          if (!read) { fail(error, true); return; }
        }
#endif
        // Keychain may have waited for native UI while the owner cancelled.
        if (m_generation != m_buffer->activeGeneration.load()) { cleanup(); return; }
        if (m_password.isEmpty()) { fail(QStringLiteral("请输入 SSH 密码或保存到钥匙串"), true); return; }
      }
      const QByteArray user = m_user.toUtf8();
      rc = libssh2_userauth_password_ex(m_session, user.constData(), user.size(),
              m_password.constData(), m_password.size(), nullptr);
      if (rc == 0) { m_password.fill(0); m_password.clear(); m_phase = OpeningChannel; return; }
      if (rc != LIBSSH2_ERROR_EAGAIN) { fail(QStringLiteral("SSH 密码认证失败，请检查用户名或密码"), true); return; }
      break;
    }
    case OpeningChannel:
      m_channel = libssh2_channel_open_session(m_session);
      if (m_channel) { m_phase = RequestingPty; return; }
      rc = libssh2_session_last_errno(m_session);
      break;
    case RequestingPty:
      rc = libssh2_channel_request_pty(m_channel, "xterm");
      if (rc == 0) { m_phase = RequestingShell; return; }
      break;
    case RequestingShell:
      rc = libssh2_channel_shell(m_channel);
      if (rc == 0) { m_phase = Running; emit connected(m_generation); return; }
      break;
    case Running: {
      if (!m_outgoing.isEmpty()) {
        const auto sent = libssh2_channel_write(m_channel, m_outgoing.constData(), m_outgoing.size());
        if (sent > 0) m_outgoing.remove(0, sent);
        else if (sent != LIBSSH2_ERROR_EAGAIN) { fail(QStringLiteral("发送 SSH 数据失败")); return; }
      }
      char buffer[16384];
      for (int stream = 0; stream < 2; ++stream) {
        const auto size = libssh2_channel_read_ex(m_channel, stream, buffer, sizeof(buffer));
        if (size > 0) appendData(buffer, size);
        else if (size < 0 && size != LIBSSH2_ERROR_EAGAIN) { fail(QStringLiteral("读取 SSH 数据失败")); return; }
      }
      if (libssh2_channel_eof(m_channel)) {
        const auto generation = m_generation;
        cleanup(); emit disconnected(generation);
      }
      return;
    }
    default: return;
    }
    if (rc != LIBSSH2_ERROR_EAGAIN) fail(QStringLiteral("SSH 握手或通道建立失败"));
  }
  void cleanup() {
    m_phase = Stopped;
    if (m_timer) m_timer->stop();
    if (m_lookupId >= 0) { QHostInfo::abortHostLookup(m_lookupId); m_lookupId = -1; }
    // Shut down before freeing, but keep the descriptor allocated until libssh2
    // is done so another thread cannot reuse it during cleanup.
    if (m_sock != INVALID_SOCKET) {
#ifdef Q_OS_WIN
      ::shutdown(m_sock, SD_BOTH);
#else
      ::shutdown(m_sock, SHUT_RDWR);
#endif
    }
    if (m_session) { libssh2_session_free(m_session); m_session = nullptr; }
    if (m_sock != INVALID_SOCKET) { closesocket(m_sock); m_sock = INVALID_SOCKET; }
    m_channel = nullptr;
    m_password.fill(0); m_password.clear();
    m_credentialsLoaded = false;
    m_outgoing.clear(); m_hostKey.clear(); m_requestId.clear();
    m_addresses.clear();
  }
  std::shared_ptr<SshSharedBuffer> m_buffer;
  std::shared_ptr<SecureCredentialStore> m_credentials;
  QString m_dataRoot;
  SshHostKeyStore m_trust;
  QTimer *m_timer = nullptr;
  QElapsedTimer m_deadline;
  QList<QHostAddress> m_addresses;
  int m_lookupId = -1;
  SOCKET m_sock = INVALID_SOCKET;
  LIBSSH2_SESSION *m_session = nullptr;
  LIBSSH2_CHANNEL *m_channel = nullptr;
  Phase m_phase = Stopped;
  quint64 m_generation = 0;
  QString m_host, m_user, m_requestId;
  int m_port = 22;
  QByteArray m_password, m_hostKey, m_outgoing;
  bool m_credentialsLoaded = false;
  bool m_overflowNotified = false;
};

// --- TerminalLineModel 实现 ---

TerminalLineModel::TerminalLineModel(int maxLines, QObject *parent)
    : QAbstractListModel(parent), m_maxLines(maxLines) {}

int TerminalLineModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return m_lines.size();
}

QVariant TerminalLineModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_lines.size())
    return {};
  if (role == LineTextRole || role == Qt::DisplayRole)
    return m_lines.at(index.row());
  return {};
}

QHash<int, QByteArray> TerminalLineModel::roleNames() const {
  return {{LineTextRole, "lineText"}};
}

void TerminalLineModel::appendLines(const QStringList &lines) {
  if (lines.isEmpty())
    return;

  const int incoming = lines.size();

  // 若新行数本身就超出上限，只取最后 m_maxLines 行
  const QStringList *toAdd = &lines;
  QStringList trimmed;
  if (incoming > m_maxLines) {
    trimmed = lines.mid(incoming - m_maxLines);
    toAdd = &trimmed;
  }

  int overflow = m_lines.size() + toAdd->size() - m_maxLines;
  if (overflow > 0) {
    // 溢出时用 beginResetModel 一次信号替代 remove+insert 两次信号，
    // 避免 ListView 内部对 5000 行位置偏移做两轮 O(n) 更新。
    // 就地修改 m_lines，不触发任何中间信号。
    beginResetModel();
    m_lines.erase(m_lines.begin(), m_lines.begin() + overflow);
    m_lines.append(*toAdd);
    endResetModel();
  } else {
    // 未溢出：简单尾部插入
    const int first = m_lines.size();
    beginInsertRows(QModelIndex(), first, first + toAdd->size() - 1);
    m_lines.append(*toAdd);
    endInsertRows();
  }
}

void TerminalLineModel::clear() {
  if (m_lines.isEmpty())
    return;
  beginResetModel();
  m_lines.clear();
  endResetModel();
}

QString TerminalLineModel::lineAt(int index) const {
  if (index < 0 || index >= m_lines.size()) {
    return {};
  }
  return m_lines.at(index);
}

QStringList TerminalLineModel::lines() const { return m_lines; }

// --- SshTerminal 实现 ---

SshTerminal::SshTerminal(QObject *parent)
    : SshTerminal(std::make_shared<SecureCredentialStore>(SecureCredentialStore::Domain::Ssh),
                  AppPaths::writableDataRoot(), parent) {}

SshTerminal::SshTerminal(std::shared_ptr<SecureCredentialStore> credentials,
                         const QString &dataRoot, QObject *parent)
    : QObject(parent), m_workerThread(new QThread),
      m_worker(new SshWorker(m_buffer, std::move(credentials), dataRoot)),
      m_lineModel(new TerminalLineModel(5000, this)), m_dataRoot(dataRoot), m_port(22),
      m_connected(false), m_connectionState(ConnectionState::Idle),
      m_maxBufferLines(5000), m_flushTimer(new QTimer(this)) {

  auto settings = this->settings();
  m_host = settings.value("ssh/host", "").toString();
  m_port = settings.value("ssh/port", 22).toInt();
  m_username = settings.value("ssh/username", "root").toString();

  m_encoding = settings.value("ssh/encoding", "UTF-8").toString();
  m_maxBufferLines = settings.value("ssh/maxLines", 10000).toInt();
  m_autoReconnectEnabled =
      settings.value("ssh/autoReconnectEnabled", true).toBool();
  m_reconnectMaxAttempts =
      settings.value("ssh/reconnectMaxAttempts", 5).toInt();
  m_reconnectIntervalMs =
      settings.value("ssh/reconnectIntervalMs", 2000).toInt();
  m_reconnectMaxAttempts = qMax(0, m_reconnectMaxAttempts);
  m_reconnectIntervalMs = qMax(500, m_reconnectIntervalMs);

  m_worker->moveToThread(m_workerThread);

  connect(this, &SshTerminal::sigStartConnect, m_worker, &SshWorker::doConnect,
          Qt::QueuedConnection);
  connect(this, &SshTerminal::sigSendCommand, m_worker, &SshWorker::doSendCommand,
          Qt::QueuedConnection);
  connect(this, &SshTerminal::sigDisconnect, m_worker, &SshWorker::doDisconnect,
          Qt::QueuedConnection);
  connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
  connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);
  connect(m_worker, &SshWorker::connected, this, [this](quint64 generation) {
    if (generation == m_generation && !m_manualDisconnectRequested) onWorkerConnected();
  });
  connect(m_worker, &SshWorker::errorOccurred, this,
      [this](quint64 generation, const QString &error, bool securityFailure) {
        if (generation != m_generation || m_manualDisconnectRequested) return;
        m_securityFailure = securityFailure;
        clearHostKeyRequest();
        onWorkerError(error);
      });
  connect(m_worker, &SshWorker::disconnected, this, [this](quint64 generation) {
    if (generation == m_generation) { clearHostKeyRequest(); onWorkerDisconnected(); }
  });
  connect(m_worker, &SshWorker::bufferOverflowed, this, &SshTerminal::sharedBufferOverflowed);
  connect(m_worker, &SshWorker::hostKeyRequired, this,
      [this](quint64 generation, const QString &host, int port, const QString &algorithm,
             const QString &fingerprint, const QString &requestId) {
        if (generation != m_generation || m_manualDisconnectRequested) return;
        m_hostKeyRequestId = requestId;
        m_hostKeyFingerprint = fingerprint;
        setStatusMessage(QStringLiteral("请通过独立渠道核对 SSH 主机指纹，确认前不会发送密码"));
        emit hostKeyPendingChanged();
        emit hostKeyConfirmationRequested(host, port, algorithm, fingerprint, requestId);
      });
  connect(m_worker, &SshWorker::credentialReadState, this,
      [this](quint64 generation, bool busy) {
        if (busy) { m_authCredentialGeneration = generation; m_authCredentialBusy = true; }
        else if (generation == m_authCredentialGeneration) m_authCredentialBusy = false;
        emit credentialStateChanged();
      });
  connect(m_worker, &SshWorker::credentialResult, this,
      [this](quint64 revision, bool exists, const QString &error) {
        m_credentialInFlight = false;
        const QString operationId = m_passwordOperations.take(revision);
        if (revision != m_credentialGeneration) {
          if (m_refreshPending) dispatchCredentialRefresh();
          if (!operationId.isEmpty()) emit passwordOperationFinished(operationId, error.isEmpty(),
              error.isEmpty() ? QStringLiteral("操作已完成，结果属于之前的连接目标") : error);
          return;
        }
        m_credentialBusy = false;
        m_hasSavedPassword = exists;
        m_credentialError = error;
        emit credentialStateChanged();
        if (!operationId.isEmpty()) emit passwordOperationFinished(operationId, error.isEmpty(), error);
      });
  m_workerThread->start();
  refreshCredentials();

  m_flushTimer->setInterval(
      500); // 500ms：日志监控场景无需 10fps，减少场景图提交压力
  m_flushTimer->setSingleShot(false);
  connect(m_flushTimer, &QTimer::timeout, this, &SshTerminal::flushPendingData);

  m_reconnectTimer = new QTimer(this);
  m_reconnectTimer->setSingleShot(true);
  connect(m_reconnectTimer, &QTimer::timeout, this,
          &SshTerminal::triggerReconnect);

  // Tab 补全截获定时器：等待 SSH 响应后解析补全结果
  m_tabTimer = new QTimer(this);
  m_tabTimer->setSingleShot(true);
  m_tabTimer->setInterval(500); // 500ms 内收不到有效响应则放弃截获
  connect(m_tabTimer, &QTimer::timeout, this,
          &SshTerminal::processTabCompletion);
}

SshTerminal::~SshTerminal() {
  disconnectFromHost();
  // Native credential UI may still be waiting. The worker owns its buffers and
  // finishes itself once the current operation returns; the UI never joins it.
  QMetaObject::invokeMethod(m_worker, [worker = m_worker] {
    worker->shutdown();
    QThread::currentThread()->quit();
  });
}

JsonSettings SshTerminal::settings() const {
  return JsonSettings(QDir(m_dataRoot).filePath(QStringLiteral("settings.json")));
}

void SshTerminal::refreshCredentials() {
  m_hasSavedPassword = false;
  m_credentialBusy = true;
  m_credentialError.clear();
  ++m_credentialGeneration;
  m_buffer->credentialRevision.store(m_credentialGeneration);
  m_refreshPending = true;
  if (!m_credentialInFlight) dispatchCredentialRefresh();
  emit credentialStateChanged();
}

void SshTerminal::dispatchCredentialRefresh() {
  m_refreshPending = false;
  m_credentialInFlight = true;
  const auto revision = m_credentialGeneration;
  const auto host = m_host;
  const auto user = m_username;
  const auto port = m_port;
  QMetaObject::invokeMethod(m_worker, [worker = m_worker, revision, host, user, port] {
    worker->initializeCredentials(revision, host, port, user);
  });
}

QString SshTerminal::savePassword(const QString &password) {
  if (credentialBusy() || password.isEmpty() || !validSshIdentity(m_host, m_port, m_username)) return {};
  const auto revision = ++m_credentialGeneration;
  m_buffer->credentialRevision.store(revision);
  const QString operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_passwordOperations.insert(revision, operationId);
  m_credentialInFlight = true;
  const auto host = m_host; const auto user = m_username; const auto port = m_port;
  m_credentialBusy = true;
  m_credentialError.clear();
  QMetaObject::invokeMethod(m_worker, [worker = m_worker, revision, host, user, port, password] {
    worker->initializeCredentials(revision, host, port, user, 1, password);
  });
  emit credentialStateChanged();
  return operationId;
}

QString SshTerminal::clearSavedPassword() {
  if (m_credentialBusy) return {};
  disconnectFromHost();
  const auto revision = ++m_credentialGeneration;
  m_buffer->credentialRevision.store(revision);
  const QString operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_passwordOperations.insert(revision, operationId);
  m_credentialInFlight = true;
  const auto host = m_host; const auto user = m_username; const auto port = m_port;
  m_credentialBusy = true;
  m_credentialError.clear();
  m_password.fill(QChar(0)); m_password.clear();
  QMetaObject::invokeMethod(m_worker, [worker = m_worker, revision, host, user, port] {
    worker->initializeCredentials(revision, host, port, user, 2);
  });
  emit credentialStateChanged();
  return operationId;
}

void SshTerminal::clearHostKeyRequest() {
  if (m_hostKeyRequestId.isEmpty()) return;
  m_hostKeyRequestId.clear(); m_hostKeyFingerprint.clear(); emit hostKeyPendingChanged();
}

void SshTerminal::confirmHostKey(const QString &requestId, const QString &fingerprint, bool accept) {
  if (requestId.isEmpty() || requestId != m_hostKeyRequestId ||
      fingerprint != m_hostKeyFingerprint || m_manualDisconnectRequested) return;
  clearHostKeyRequest();
  const auto generation = m_generation;
  QMetaObject::invokeMethod(m_worker, [worker = m_worker, generation, requestId, accept] {
    worker->confirmHostKey(generation, requestId, accept);
  });
}

void SshTerminal::identityChanged() {
  disconnectFromHost();
  m_password.fill(QChar(0)); m_password.clear();
  emit passwordChanged();
  refreshCredentials();
}

QString SshTerminal::host() const { return m_host; }
void SshTerminal::setHost(const QString &host) {
  if (m_host != host) {
    m_host = host.trimmed();
    identityChanged();
    emit hostChanged();
  }
}

int SshTerminal::port() const { return m_port; }
void SshTerminal::setPort(int port) {
  if (m_port != port) {
    m_port = port;
    identityChanged();
    emit portChanged();
  }
}

QString SshTerminal::username() const { return m_username; }
void SshTerminal::setUsername(const QString &username) {
  if (m_username != username) {
    m_username = username;
    identityChanged();
    emit usernameChanged();
  }
}

QString SshTerminal::password() const {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
  return {};
#else
  return m_password;
#endif
}

void SshTerminal::setPassword(const QString &password) {
  if (m_password != password) {
    m_password = password;
    emit passwordChanged();
  }
}

QString SshTerminal::encoding() const { return m_encoding; }

void SshTerminal::setEncoding(const QString &encoding) {
  if (m_encoding != encoding) {
    m_encoding = encoding;
    auto settings = this->settings();
    settings.setValue("ssh/encoding", m_encoding);
  settings.sync();
    emit encodingChanged();
  }
}

bool SshTerminal::isConnected() const { return m_connected; }
int SshTerminal::connectionState() const {
  return static_cast<int>(m_connectionState);
}
QString SshTerminal::statusMessage() const { return m_statusMessage; }
QString SshTerminal::lastError() const { return m_lastError; }

bool SshTerminal::autoReconnectEnabled() const {
  return m_autoReconnectEnabled;
}

void SshTerminal::setAutoReconnectEnabled(bool enabled) {
  if (m_autoReconnectEnabled == enabled) {
    return;
  }
  m_autoReconnectEnabled = enabled;
  auto settings = this->settings();
  settings.setValue("ssh/autoReconnectEnabled", m_autoReconnectEnabled);
  settings.sync();
  emit autoReconnectEnabledChanged();
}

int SshTerminal::reconnectMaxAttempts() const { return m_reconnectMaxAttempts; }

void SshTerminal::setReconnectMaxAttempts(int attempts) {
  attempts = qMax(0, attempts);
  if (m_reconnectMaxAttempts == attempts) {
    return;
  }
  m_reconnectMaxAttempts = attempts;
  auto settings = this->settings();
  settings.setValue("ssh/reconnectMaxAttempts", m_reconnectMaxAttempts);
  settings.sync();
  emit reconnectMaxAttemptsChanged();
}

int SshTerminal::reconnectIntervalMs() const { return m_reconnectIntervalMs; }

void SshTerminal::setReconnectIntervalMs(int intervalMs) {
  intervalMs = qMax(500, intervalMs);
  if (m_reconnectIntervalMs == intervalMs) {
    return;
  }
  m_reconnectIntervalMs = intervalMs;
  auto settings = this->settings();
  settings.setValue("ssh/reconnectIntervalMs", m_reconnectIntervalMs);
  settings.sync();
  emit reconnectIntervalMsChanged();
}

int SshTerminal::reconnectAttempt() const { return m_reconnectAttempt; }

int SshTerminal::maxBufferLines() const { return m_maxBufferLines; }
void SshTerminal::setMaxBufferLines(int maxLines) {
  if (m_maxBufferLines != maxLines) {
    m_maxBufferLines = qMax(100, maxLines);
    emit maxBufferLinesChanged();
  }
}

int SshTerminal::currentLineCount() const { return m_lineModel->rowCount(); }

TerminalLineModel *SshTerminal::lineModel() const { return m_lineModel; }

void SshTerminal::setConnected(bool connected) {
  if (m_connected != connected) {
    m_connected = connected;
    emit connectedChanged();
  }
}

void SshTerminal::setConnectionState(ConnectionState state) {
  if (m_connectionState == state) {
    return;
  }
  m_connectionState = state;
  emit connectionStateChanged();
}

void SshTerminal::setStatusMessage(const QString &msg) {
  if (m_statusMessage != msg) {
    m_statusMessage = msg;
    emit statusMessageChanged();
  }
}

void SshTerminal::setLastError(const QString &error) {
  if (m_lastError == error) {
    return;
  }
  m_lastError = error;
  emit lastErrorChanged();
}

void SshTerminal::startConnection() {
  clearHostKeyRequest();
  const quint64 previous = m_generation++;
  m_buffer->activeGeneration.store(m_generation);
  emit sigDisconnect(previous);
  emit sigStartConnect(m_generation, m_host, m_port, m_username, m_password);
}

void SshTerminal::connectToHost() {
  if (!validSshIdentity(m_host, m_port, m_username)) {
    setConnectionState(ConnectionState::Failed);
    setStatusMessage(QStringLiteral("请输入有效的主机、端口和用户名"));
    return;
  }
  if (credentialBusy() || !m_credentialError.isEmpty()) {
    setStatusMessage(credentialBusy() ? QStringLiteral("正在处理 SSH 钥匙串，请稍候") : m_credentialError);
    return;
  }
  m_reconnectTimer->stop();
  m_manualDisconnectRequested = false;
  m_securityFailure = false;
  m_reconnectPending = false;
  if (m_reconnectAttempt != 0) { m_reconnectAttempt = 0; emit reconnectAttemptChanged(); }
  setLastError({});
  setConnected(false);
  setConnectionState(ConnectionState::Connecting);
  setStatusMessage(QStringLiteral("正在建立连接并验证 SSH 主机密钥..."));
  startConnection();
}

void SshTerminal::disconnectFromHost() {
  const auto generation = m_generation++;
  m_buffer->activeGeneration.store(m_generation);
  m_manualDisconnectRequested = true;
  m_reconnectPending = false;
  if (m_tabTimer) m_tabTimer->stop();
  m_tabMode = false; m_tabBusy = false; m_tabPhase = 0;
  m_tabPrefix.clear(); m_tabRawBuffer.clear();
  clearHostKeyRequest();
  if (m_reconnectTimer) m_reconnectTimer->stop();
  if (m_reconnectAttempt != 0) { m_reconnectAttempt = 0; emit reconnectAttemptChanged(); }
  setConnected(false);
  m_flushTimer->stop();
  setConnectionState(ConnectionState::Disconnected);
  setStatusMessage(QStringLiteral("已断开连接"));
  emit sigDisconnect(generation);
}

void SshTerminal::sendCommand(const QString &cmd) {
  if (m_connected) {
    // 先清空远端当前输入行，避免 Tab
    // 补全阶段残留内容（候选/前缀）与最终命令拼接 Ctrl+U: Unix shell
    // 行编辑“清空到行首”
    emit sigSendCommand("\x15", false, m_encoding);
    emit sigSendCommand(cmd, true, m_encoding);
  }
}

void SshTerminal::sendRawBytes(const QString &text) {
  if (m_connected) {
    emit sigSendCommand(text, false, m_encoding);
  }
}

void SshTerminal::sendTabCompletion(const QString &prefix) {
  if (!m_connected)
    return;
  // 防重入：Tab 序列结束前（包含 phase0→phase1 过渡）忽略所有新按键
  if (m_tabBusy)
    return;

  // 先将已积压的数据正常刷到终端，确保之后收到的全是补全响应
  flushPendingData();

  m_tabPrefix = prefix;
  m_tabRawBuffer.clear();
  m_tabBusy = true; // 整个序列期间锁定
  m_tabMode = true; // 截获 SSH 数据流
  m_tabPhase = 0;
  m_tabTimer->setInterval(600); // Phase0：600ms 等待首次响应
  m_tabTimer->start();

  // 记录触发日志
  do {
    QFile logFile("log/log.txt");
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
      QString ts = QDateTime::currentDateTime().toString(Qt::ISODate);
      logFile.write(("[" + ts + "] TAB REQ: " + prefix + "\n").toUtf8());
      logFile.close();
    }
  } while (0);

  // 发送「当前文本 + \t」触发远端 shell 补全
  emit sigSendCommand(prefix + "\t", false, m_encoding);
}

QString SshTerminal::getBufferText() {
  // 从 lineModel 拼接所有行（用于调试或复制全文）
  QStringList lines = m_lineModel->lines();
  QMutexLocker locker(&m_buffer->mutex);
  if (!m_partialLine.isEmpty())
    lines << m_partialLine;
  return lines.join("\n");
}

QVariantList SshTerminal::findLineIndexes(const QString &keyword,
                                          bool caseSensitive) const {
  QVariantList result;
  if (keyword.isEmpty()) {
    return result;
  }
  const Qt::CaseSensitivity sensitivity =
      caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
  const int total = m_lineModel->rowCount();
  for (int i = 0; i < total; ++i) {
    const QString line = m_lineModel->lineAt(i);
    if (line.contains(keyword, sensitivity)) {
      result << i;
    }
  }
  return result;
}

// 增量搜索：只对 [fromLine, toLine] 区间内的行进行检索，
// 用于 onLineCountChanged 中新增行的快速匹配，避免全量遍历。
QVariantList SshTerminal::findLineIndexesInRange(const QString &keyword,
                                                 int fromLine, int toLine,
                                                 bool caseSensitive) const {
  QVariantList result;
  if (keyword.isEmpty())
    return result;
  const Qt::CaseSensitivity sensitivity =
      caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
  const int total = m_lineModel->rowCount();
  // 修正范围边界，防止越界
  const int from = qMax(0, fromLine);
  const int to = qMin(total - 1, toLine);
  for (int i = from; i <= to; ++i) {
    if (m_lineModel->lineAt(i).contains(keyword, sensitivity))
      result << i;
  }
  return result;
}

QString SshTerminal::lineAt(int index) const {
  return m_lineModel->lineAt(index);
}

void SshTerminal::copyLineToClipboard(int index) const {
  const QString line = m_lineModel->lineAt(index);
  if (line.isEmpty()) {
    return;
  }
  if (QGuiApplication::clipboard()) {
    QGuiApplication::clipboard()->setText(line);
  }
}

void SshTerminal::copyBufferToClipboard() const {
  if (QGuiApplication::clipboard()) {
    QStringList lines = m_lineModel->lines();
    {
      QMutexLocker locker(&m_buffer->mutex);
      if (!m_partialLine.isEmpty()) {
        lines << m_partialLine;
      }
    }
    QGuiApplication::clipboard()->setText(lines.join("\n"));
  }
}

QString SshTerminal::clipboardText() const {
  if (!QGuiApplication::clipboard()) {
    return {};
  }
  return QGuiApplication::clipboard()->text();
}

void SshTerminal::clearBuffer() {
  {
    QMutexLocker locker(&m_buffer->mutex);
    m_partialLine.clear();
    m_buffer->data.clear();
  }
  m_lineModel->clear();
  emit lineCountChanged();
}

void SshTerminal::onWorkerConnected() {
  auto settings = this->settings();
  settings.setValue("ssh/host", m_host);
  settings.setValue("ssh/port", m_port);
  settings.setValue("ssh/username", m_username);

  settings.setValue("ssh/maxLines", m_maxBufferLines);
  settings.sync();
  const bool configurationSaved = settings.status() == QSettings::NoError;

  m_manualDisconnectRequested = false;
  m_reconnectPending = false;
  if (m_reconnectAttempt != 0) {
    m_reconnectAttempt = 0;
    emit reconnectAttemptChanged();
  }
  setConnected(true);
  setConnectionState(ConnectionState::Connected);
  setLastError({});
  setStatusMessage(configurationSaved ? QStringLiteral("已连接")
      : QStringLiteral("已连接；连接配置保存失败，请检查便携目录写入权限：") + settings.fileName());
  m_flushTimer->start();
}

void SshTerminal::onWorkerError(const QString &errorStr) {
  // 错误消息写入共享缓冲（保持数据路径一致，不走事件队列）
  {
    QMutexLocker locker(&m_buffer->mutex);
    m_buffer->data.append((errorStr + "\r\n").toLocal8Bit());
  }
  setLastError(errorStr);
  setStatusMessage(errorStr);
  emit connectionError(errorStr);
  setConnected(false);
  scheduleReconnect(errorStr);
}

void SshTerminal::onWorkerDisconnected() {
  m_flushTimer->stop();
  flushPendingData(); // 将剩余缓冲内容最后刷一次到界面
  setConnected(false);

  if (m_manualDisconnectRequested) {
    m_manualDisconnectRequested = false;
    m_reconnectPending = false;
    if (m_reconnectTimer) {
      m_reconnectTimer->stop();
    }
    if (m_reconnectAttempt != 0) {
      m_reconnectAttempt = 0;
      emit reconnectAttemptChanged();
    }
    setConnectionState(ConnectionState::Disconnected);
    setStatusMessage("已断开连接");
    return;
  }

  if (m_reconnectPending) {
    return;
  }

  if (!m_securityFailure) scheduleReconnect("连接已断开");
}

void SshTerminal::scheduleReconnect(const QString &reason) {
  if (m_securityFailure) {
    setConnectionState(ConnectionState::Failed);
    setStatusMessage(reason.isEmpty() ? m_lastError : reason);
    return;
  }
  if (m_manualDisconnectRequested) {
    setConnectionState(ConnectionState::Disconnected);
    setStatusMessage("已断开连接");
    return;
  }

  if (!m_autoReconnectEnabled || m_reconnectMaxAttempts <= 0) {
    setConnectionState(ConnectionState::Failed);
    setStatusMessage(reason.isEmpty() ? "连接失败" : reason);
    return;
  }

  if (m_reconnectAttempt >= m_reconnectMaxAttempts) {
    setConnectionState(ConnectionState::Failed);
    setStatusMessage(
        QString("重连失败，已达到最大次数(%1)").arg(m_reconnectMaxAttempts));
    return;
  }

  m_reconnectAttempt += 1;
  emit reconnectAttemptChanged();
  m_reconnectPending = true;
  setConnectionState(ConnectionState::Reconnecting);
  setStatusMessage(QString("连接中断，%1秒后尝试重连 (%2/%3)")
                       .arg(m_reconnectIntervalMs / 1000.0, 0, 'f', 1)
                       .arg(m_reconnectAttempt)
                       .arg(m_reconnectMaxAttempts));
  if (m_reconnectTimer) {
    m_reconnectTimer->start(m_reconnectIntervalMs);
  }
}

void SshTerminal::triggerReconnect() {
  if (m_manualDisconnectRequested || m_connected || m_securityFailure) {
    return;
  }
  m_reconnectPending = false;
  setConnectionState(ConnectionState::Connecting);
  setStatusMessage(QString("正在重连 (%1/%2)...")
                       .arg(qMax(1, m_reconnectAttempt))
                       .arg(m_reconnectMaxAttempts));
  startConnection();
}

void SshTerminal::enforceBufferLimit() {
  // 行上限由 TerminalLineModel::appendLines 内部管理，此函数保留仅供兼容
}

void SshTerminal::flushPendingData() {
  // Tab 补全截获模式：将数据路由到独立缓冲区，不推入终端显示
  if (m_tabMode) {
    QMutexLocker locker(&m_buffer->mutex);
    if (!m_buffer->data.isEmpty()) {
      m_tabRawBuffer.append(m_buffer->data);
      m_buffer->data.clear();
    }
    return;
  }

  // 1. O(1) 取走共享缓冲区（持锁时间极短）
  QByteArray rawData;
  {
    QMutexLocker locker(&m_buffer->mutex);
    if (m_buffer->data.isEmpty())
      return;
    rawData.swap(m_buffer->data);
  }

  // 2. 解码（仅一次内存分配）
  QString newText;
  if (m_encoding.toUpper().contains("GB")) {
    newText = QString::fromLocal8Bit(rawData);
  } else {
    newText = QString::fromUtf8(rawData);
  }
  rawData.clear(); // 立即释放原始字节

  if (newText.isEmpty())
    return;

  // 3. ANSI 转义清理 + \r 处理
  // 先处理 \r\n（PTY 标准换行），再将孤立 \r（readline 行首覆写）转为 \n，
  // 不能直接删除 \r，否则 Tab 补全、进度条等输出会粘连到上一行不可见
  static const QRegularExpression reAnsi(
      "\x1B\\[[0-9;?]*[a-zA-Z]|\x1B\\].*?(?:\x07|\x1B\\\\)");
  newText.replace(reAnsi, "");
  newText.replace("\r\n", "\n"); // Windows / PTY 标准换行
  newText.replace("\r", "\n");   // 孤立 \r：行首覆写 → 换行显示

  // 3.5 newDataChunk 信号已废弃（TextArea 终端已移除），不再 emit，节省 QString
  // 复制开销

  // 4. 切分为行（拼接上次未完整的行尾）
  QStringList newLines;
  {
    QMutexLocker locker(&m_buffer->mutex);
    QString fullText = m_partialLine + newText;
    m_partialLine.clear();
    newText.clear();

    newLines = fullText.split('\n');
    if (!newLines.isEmpty()) {
      m_partialLine = newLines.takeLast(); // 末尾不完整行留待下次
    }
  }

  if (newLines.isEmpty())
    return;

  // 5. 追加到行模型。
  // 【核心】TerminalLineModel
  // 内部维护固定上限（默认5000行），超出时直接丢弃最老行。 ListView
  // 只渲染当前可见的约30行，内存占用 = 行数 × 平均行大小 ≈ 1MB，永远有界。
  m_lineModel->appendLines(newLines);
  emit lineCountChanged();
}

void SshTerminal::processTabCompletion() {
  if (!m_connected || !m_tabBusy) return;
  // 不在开头清 m_tabMode，避免窗口期间数据被当成普通终端输出

  // 将截获期间可能还残留在共享缓冲的数据一并取出
  {
    QMutexLocker locker(&m_buffer->mutex);
    if (!m_buffer->data.isEmpty()) {
      m_tabRawBuffer.append(m_buffer->data);
      m_buffer->data.clear();
    }
  }

  if (m_tabRawBuffer.isEmpty()) {
    // BEL 只（或真空）：根据阶段处理
    if (m_tabPhase == 0) {
      // 第一阶段无增量（bash 小鸣）：自动进入第二阶段收候选
      m_tabPhase = 1;
      m_tabRawBuffer.clear();
      m_tabTimer->setInterval(1200); // Phase1：1200ms 等待候选列表响应
      m_tabTimer->start();
      emit sigSendCommand("\t", false, m_encoding);
    } else if (m_tabPhase == 1) {
      // 第二阶段已过 1200ms 仍无数据：再多等 800ms （网络慢容错）
      m_tabPhase = 2;
      m_tabTimer->setInterval(800);
      m_tabTimer->start();
    } else {
      // 总计等了 2.6s 仍无内容：放弃
      m_tabMode = false;
      m_tabBusy = false;
      emit tabCompletionReady("", {});
    }
    return;
  }

  // 解码
  QString raw = m_encoding.toUpper().contains("GB")
                    ? QString::fromLocal8Bit(m_tabRawBuffer)
                    : QString::fromUtf8(m_tabRawBuffer);

  // 记录原始 Tab 响应到日志文件，便于调试与分析
  do {
    QFile logFile("log/log.txt");
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
      QString ts = QDateTime::currentDateTime().toString(Qt::ISODate);
      QByteArray out = ("[" + ts + "] TAB RAW:\n").toUtf8();
      out.append(raw.toUtf8());
      out.append('\n');
      logFile.write(out);
      logFile.close();
    }
  } while (0);

  // 清理 ANSI 转义、BEL、\r 等控制字符
  static const QRegularExpression reAnsi(
      "\x1B\\[[0-9;?]*[a-zA-Z]|\x1B\\].*?(?:\x07|\x1B\\\\)");
  raw.replace(reAnsi, "");
  raw.replace("\x07", ""); // BEL
  raw.replace("\r\n", "\n");
  raw.replace("\r", "\n");

  const QStringList lines = raw.split('\n', Qt::SkipEmptyParts);

  QString completedCmd;
  QStringList candidates;

  if (!m_tabPrefix.isEmpty()) {
    // 策略1：找到包含完整 prefix 且更长的行（bash 回显完整命令行时命中）
    for (const QString &line : lines) {
      int idx = line.indexOf(m_tabPrefix);
      if (idx >= 0) {
        QString fromPrefix = line.mid(idx).trimmed();
        if (fromPrefix.length() <= m_tabPrefix.length()) {
          continue;
        }

        // 仅当 prefix 后缀为“单 token 增量”时才视为唯一补全：
        // - 不能以空白开头（否则通常是候选列表拼接）
        // - 不能包含双空格/Tab（通常是多候选展示）
        // - 不能再出现空白（避免把 "lock/  log/" 这类候选串当成命令）
        const QString suffix = fromPrefix.mid(m_tabPrefix.length());
        if (suffix.isEmpty()) {
          continue;
        }
        if (suffix.startsWith(' ') || suffix.startsWith('\t')) {
          continue;
        }
        if (suffix.contains("  ") || suffix.contains('\t')) {
          continue;
        }
        if (suffix.contains(QRegularExpression("\\s"))) {
          continue;
        }

        completedCmd = fromPrefix;
        break;
      }
    }

    // 策略2：bash 只回显增量（如 tai→l）或多候选列表
    // 收集所有不属于 prefix 单词、不含完整 prefix 的有效 token
    if (completedCmd.isEmpty()) {
      // 把 prefix 按空格拆成单词集合，用于过滤 bash echo-back
      // 注意：必须先存到具名变量，再取
      // begin/end，否则两个临时对象迭代器不匹配会崩溃
      const QStringList prefixParts =
          m_tabPrefix.split(' ', Qt::SkipEmptyParts);
      const QSet<QString> prefixWords(prefixParts.begin(), prefixParts.end());

      QStringList allTokens;
      for (const QString &line : lines) {
        // 跳过包含完整 prefix 的行（bash 重绘提示符行）
        if (line.contains(m_tabPrefix))
          continue;
        const QStringList parts =
            line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        for (const QString &p : parts) {
          QString t = p.trimmed();
          // 过滤空串、prefix 本身、prefix 中的单词（bash echo-back 碎片）
          if (t.isEmpty() || t == m_tabPrefix || prefixWords.contains(t))
            continue;
          allTokens << t;
        }
      }

      if (allTokens.size() == 1) {
        completedCmd = m_tabPrefix + allTokens.first();
      } else if (allTokens.size() > 1) {
        candidates = allTokens;
      }
    }
  }

  if (!completedCmd.isEmpty()) {
    // 唯一补全：通知 QML 更新输入框
    m_tabMode = false;
    m_tabBusy = false;
    m_tabRawBuffer.clear();
    emit tabCompletionReady(completedCmd, {});
  } else if (!candidates.isEmpty()) {
    // 多候选：把候选词拼成一行干净展示到终端
    m_tabMode = false;
    m_tabBusy = false;
    m_tabRawBuffer.clear();
    m_lineModel->appendLines({candidates.join("  ")});
    emit lineCountChanged();
    emit tabCompletionReady("", candidates);
  } else {
    // 无有效 token（如全是 ANSI 控制字符）——按阶段处理
    m_tabRawBuffer.clear();
    if (m_tabPhase == 0) {
      // 第一阶段无增量：自动进入第二阶段， m_tabBusy/m_tabMode 保持 true
      m_tabPhase = 1;
      m_tabTimer->setInterval(1200);
      m_tabTimer->start();
      emit sigSendCommand("\t", false, m_encoding);
    } else if (m_tabPhase == 1) {
      m_tabPhase = 2;
      m_tabTimer->setInterval(800);
      m_tabTimer->start();
    } else {
      // 最终放弃
      m_tabMode = false;
      m_tabBusy = false;
      emit tabCompletionReady("", {});
    }
  }
}

#include "SshTerminal.moc"
