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
#include <QSocketNotifier>
#include <algorithm>


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
#include <netinet/tcp.h>
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
                 const QString &user, const QString &password, const QString &encoding, bool useSystemCredentials) {
    cleanup();
    if (generation != m_buffer->activeGeneration.load()) return;
    m_generation = generation;
    m_encoding = encoding;
    m_streamRevision = m_buffer->contentRevision.load();
    m_stream.reset(m_encoding);
    if (!m_stream.encodingAvailable()) { fail(QStringLiteral("当前系统不支持所选 SSH 字符编码"), true); return; }
    m_host = host;
    m_port = port;
    m_user = user;
    m_password = password.toUtf8();
    m_useSystemCredentials = useSystemCredentials;
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
    m_timer->setSingleShot(false);
    m_timer->setInterval(10);
    m_timer->start();
    QHostAddress address;
    if (address.setAddress(host)) {
      // lookupHost performs reverse DNS for IP literals; connecting does not need their host name.
      m_addresses = {address};
      tryNextAddress();
      return;
    }
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
    if (m_phase != Running || m_buffer->receptionPauseRequested.load()) return;
    QByteArray data = SshLogStream::encode(cmd, encoding);
    if (!cmd.isEmpty() && data.isEmpty()) { fail(QStringLiteral("SSH 命令字符编码失败")); return; }
    if (appendNewline && !data.endsWith('\n')) data.append('\n');
    if (m_outgoing.size() + m_writeInFlight.size() + data.size() > 1024 * 1024) {
      fail(QStringLiteral("SSH 待发送数据超过上限"));
      return;
    }
    m_outgoing.append(data);
    resumeRead();
  }
  void resetStream(quint64 revision, const QString &encoding, bool discardPartial) {
    if (revision != m_buffer->contentRevision.load()) return;
    m_streamRevision = revision; m_encoding = encoding;
    m_replay.clear(); m_replayOffset = 0;
    m_stream.reset(encoding, discardPartial);
    if (!m_stream.encodingAvailable()) { fail(QStringLiteral("当前系统不支持所选 SSH 字符编码"), true); return; }
    resumeRead();
  }
  void beginTabCapture(quint64 generation) {
    if (m_phase != Running || generation != m_generation || generation != m_buffer->activeGeneration.load()) return;
    QMutexLocker lock(&m_buffer->mutex);
    m_buffer->tabCapture = true;
    m_buffer->tabData.clear(); m_buffer->tabOverflow = false;
  }
  void endTabCapture(quint64 generation) {
    if (generation != m_generation || generation != m_buffer->activeGeneration.load()) return;
    QByteArray late;
    {
      QMutexLocker lock(&m_buffer->mutex);
      m_buffer->tabCapture = false;
      late.swap(m_buffer->tabData);
    }
    if (!late.isEmpty() && m_phase == Running) m_replay.append(late);
    resumeRead();
  }
  void resumeRead() {
    if (m_phase != Running || !m_timer) return;
    if (m_buffer->receptionPauseRequested.load()) {
      m_timer->stop();
      if (m_readNotifier) m_readNotifier->setEnabled(false);
      if (m_writeNotifier) m_writeNotifier->setEnabled(false);
    } else if (!m_timer->isActive()) m_timer->start(0);
  }
  void applyReceptionState(quint64 revision, bool paused) {
    resumeRead();
    emit receptionStateApplied(revision, paused);
  }

signals:
  void connected(quint64 generation);
  void errorOccurred(quint64 generation, const QString &error, bool securityFailure);
  void disconnected(quint64 generation);
  void dataReady(quint64 generation, quint64 revision);
  void receptionStateApplied(quint64 revision, bool paused);
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
      const int noDelay = 1;
      if (setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char *>(&noDelay), sizeof(noDelay)) != 0) {
        qWarning() << "SSH TCP_NODELAY unavailable; retaining default TCP buffering";
      }
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
  void prepareMonitor() {
    QVariantList rules;
    quint64 revision;
    {
      QMutexLocker lock(&m_buffer->mutex);
      revision = m_buffer->monitorRevision;
      if (revision == m_monitorRevision) return;
      rules = m_buffer->monitorRules;
    }
    m_monitor.configure(rules, revision, m_stream.logicalId(), m_stream.partialLine(), m_stream.logicalOffset());
    m_monitorRevision = revision;
  }
  void publish(std::vector<SshLogLine> lines) {
    bool notify = false;
    SshLogLine partial{m_stream.partialLine(), m_stream.logicalId(), m_stream.continuation(), m_stream.hasPendingInput()};
    m_monitor.feed(lines, partial);
    auto hit = m_monitor.takeHit();
    {
      QMutexLocker lock(&m_buffer->mutex);
      if (m_generation != m_buffer->activeGeneration.load() ||
          m_streamRevision != m_buffer->contentRevision.load()) return;
      if (!hit.data.isEmpty() && hit.revision == m_buffer->monitorRevision
          && (m_buffer->monitorHit.data.isEmpty() || !m_buffer->monitorHit.data.value("pauseOnMatch", true).toBool()))
        m_buffer->monitorHit = std::move(hit);
      for (auto &line : lines) {
        m_buffer->bytes += line.storageBytes();
        m_buffer->lines.push_back(std::move(line));
      }
      const bool liveChanged = m_buffer->partial.text != m_stream.partialLine() || m_buffer->cursorColumn != m_stream.cursorColumn();
      m_buffer->partial = std::move(partial);
      m_buffer->cursorColumn = m_stream.cursorColumn();
      m_buffer->cursorPosition = m_stream.cursorPosition(); m_buffer->cursorPadding = m_stream.cursorPadding();
      if ((!m_buffer->lines.empty() || liveChanged || !m_buffer->tabData.isEmpty() || m_buffer->tabOverflow) &&
          !m_buffer->notificationPending) {
        m_buffer->notificationPending = true; notify = true;
      }
    }
    if (notify) emit dataReady(m_generation, m_streamRevision);
  }
  void appendData(const char *data, int size) {
    prepareMonitor();
    QByteArray replay;
    bool capturing = false;
    {
      QMutexLocker lock(&m_buffer->mutex);
      if (m_generation != m_buffer->activeGeneration.load() ||
          m_streamRevision != m_buffer->contentRevision.load()) return;
      if (m_buffer->tabCapture) {
        if (m_buffer->tabData.size() + size <= 256 * 1024) {
          m_buffer->tabData.append(data, size); capturing = true;
        } else {
          // A noisy peer must not make completion an unbounded side channel or lose its output.
          replay.swap(m_buffer->tabData);
          m_buffer->tabCapture = false; m_buffer->tabOverflow = true;
        }
      }
    }
    if (!replay.isEmpty()) {
      m_replay = std::move(replay); m_replayOffset = 0;
      m_replay.append(data, size);
      publish({});
    } else if (capturing) publish({});
    else publish(m_stream.feed(QByteArrayView(data, size)));
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
        if (m_useSystemCredentials && m_password.isEmpty()) {
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
        if (m_password.isEmpty()) { fail(QStringLiteral("请输入 SSH 密码"), true); return; }
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
      if (rc == 0) {
        m_phase = Running;
        m_timer->stop(); m_timer->setSingleShot(true);
        m_readNotifier = new QSocketNotifier(qintptr(m_sock), QSocketNotifier::Read, this);
        m_writeNotifier = new QSocketNotifier(qintptr(m_sock), QSocketNotifier::Write, this);
        m_writeNotifier->setEnabled(false);
        connect(m_readNotifier, &QSocketNotifier::activated, this, [this] { advance(); });
        connect(m_writeNotifier, &QSocketNotifier::activated, this, [this] { advance(); });
        emit connected(m_generation); resumeRead(); return;
      }
      break;
    case Running: {
      m_timer->stop();
      m_readNotifier->setEnabled(false); m_writeNotifier->setEnabled(false);
      if (m_buffer->receptionPauseRequested.load()) return;
      if (m_streamRevision != m_buffer->contentRevision.load()) return;
      int waitDirections = 0;
      bool writeProgress = false;
      if (m_writeInFlight.isEmpty()) m_writeInFlight.swap(m_outgoing);
      if (!m_writeInFlight.isEmpty()) {
        const auto sent = libssh2_channel_write(m_channel, m_writeInFlight.constData(), m_writeInFlight.size());
        if (sent > 0) { m_writeInFlight.remove(0, sent); writeProgress = true; }
        else if (sent != LIBSSH2_ERROR_EAGAIN) { fail(QStringLiteral("发送 SSH 数据失败")); return; }
        else waitDirections |= libssh2_session_block_directions(m_session);
      }
      QElapsedTimer budget; budget.start();
      qsizetype bytesRead = 0;
      bool paused = false, drained = false;
      char buffer[65536];
      while (bytesRead < 256 * 1024 && budget.nsecsElapsed() < 2 * 1000 * 1000) {
        bool progress = false;
        for (int stream = 0; stream < 2; ++stream) {
          if (m_buffer->receptionPauseRequested.load()) return;
          if (m_generation != m_buffer->activeGeneration.load() ||
              m_streamRevision != m_buffer->contentRevision.load()) return;
          qsizetype allowance;
          {
            QMutexLocker lock(&m_buffer->mutex);
            // Account for worst-case one-character rows and a final unterminated segment.
            allowance = (SshLogStream::PendingBytes - m_buffer->bytes -
                         4 * SshLogStream::SegmentCharacters) / 128;
          }
          if (allowance <= 0) { paused = true; break; }
          if (m_replayOffset < m_replay.size()) {
            // Completion recovery uses the same budget as socket input, including newline-heavy output.
            const auto take = qMin(allowance, m_replay.size() - m_replayOffset);
            prepareMonitor();
            publish(m_stream.feed(QByteArrayView(m_replay).sliced(m_replayOffset, take)));
            m_replayOffset += take; bytesRead += take; progress = true;
            if (m_replayOffset == m_replay.size()) { m_replay.clear(); m_replayOffset = 0; }
            break;
          }
          const auto size = libssh2_channel_read_ex(m_channel, stream, buffer,
                                                   size_t(qMin(qsizetype(sizeof(buffer)), allowance)));
          if (size > 0) { appendData(buffer, int(size)); bytesRead += size; progress = true; }
          else if (size == LIBSSH2_ERROR_EAGAIN) waitDirections |= libssh2_session_block_directions(m_session);
          else if (size < 0) { fail(QStringLiteral("读取 SSH 数据失败")); return; }
        }
        if (paused) break;
        if (!progress) { drained = true; break; }
      }
      if (m_buffer->receptionPauseRequested.load()) return;
      if (drained && libssh2_channel_eof(m_channel)) {
        prepareMonitor();
        publish(m_stream.finish());
        const auto generation = m_generation;
        cleanup(); emit disconnected(generation);
        return;
      }
      if ((!paused && !drained) || (writeProgress && !m_writeInFlight.isEmpty()) ||
          (!m_outgoing.isEmpty() && m_writeInFlight.isEmpty())) {
        // Yield to commands/cancellation, then drain libssh2's already-buffered bytes as well as the socket.
        m_timer->start(0);
      } else {
        if (!paused || (!m_writeInFlight.isEmpty() && (waitDirections & LIBSSH2_SESSION_BLOCK_INBOUND)))
          m_readNotifier->setEnabled(true);
        if (waitDirections & LIBSSH2_SESSION_BLOCK_OUTBOUND) m_writeNotifier->setEnabled(true);
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
    for (auto **notifier : {&m_readNotifier, &m_writeNotifier}) {
      if (*notifier) { (*notifier)->setEnabled(false); (*notifier)->deleteLater(); *notifier = nullptr; }
    }
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
    m_outgoing.clear(); m_writeInFlight.clear(); m_replay.clear(); m_replayOffset = 0;
    m_hostKey.clear(); m_requestId.clear();
    m_addresses.clear();
  }
  std::shared_ptr<SshSharedBuffer> m_buffer;
  std::shared_ptr<SecureCredentialStore> m_credentials;
  QString m_dataRoot;
  SshHostKeyStore m_trust;
  QTimer *m_timer = nullptr;
  QSocketNotifier *m_readNotifier = nullptr;
  QSocketNotifier *m_writeNotifier = nullptr;
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
  QByteArray m_password, m_hostKey, m_outgoing, m_writeInFlight, m_replay;
  qsizetype m_replayOffset = 0;
  bool m_credentialsLoaded = false;
  bool m_useSystemCredentials = false;
  QString m_encoding = QStringLiteral("UTF-8");
  SshLogStream m_stream;
  SshMonitorMatcher m_monitor;
  quint64 m_monitorRevision = ~quint64(0);
  quint64 m_streamRevision = 0;
};

// --- TerminalLineModel 实现 ---

TerminalLineModel::TerminalLineModel(int maxLines, QObject *parent)
    : QAbstractListModel(parent), m_maxLines(qBound(100, maxLines, 200000)) {}

int TerminalLineModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return int(m_lines.size());
}

QVariant TerminalLineModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= int(m_lines.size()))
    return {};
  const auto &line = m_lines[size_t(index.row())];
  if (role == LineTextRole || role == Qt::DisplayRole)
    return line.text;
  if (role == LineIdRole) return lineId(index.row());
  if (role == ContinuationRole) return line.continuation;
  if (role == MonitorColorRole) return line.monitorColor;
  if (role == MonitorRuleNameRole) return line.monitorRuleName;
  if (role == ContinuesRole) return line.continues;
  return {};
}

QHash<int, QByteArray> TerminalLineModel::roleNames() const {
  return {{LineTextRole, "lineText"}, {LineIdRole, "lineId"},
          {ContinuationRole, "continuation"}, {ContinuesRole, "continues"}, {MonitorColorRole, "monitorColor"}, {MonitorRuleNameRole, "monitorRuleName"}};
}

void TerminalLineModel::appendLines(const QStringList &lines) {
  std::vector<SshLogLine> records;
  for (const auto &line : lines) {
    const auto logicalId = m_syntheticId--;
    for (qsizetype pos = 0; pos < line.size() || pos == 0;) {
      auto length = qMin(SshLogStream::SegmentCharacters, line.size() - pos);
      if (pos + length < line.size() && line[pos + length - 1].isHighSurrogate() && line[pos + length].isLowSurrogate()) --length;
      const auto text = QStringView(line).mid(pos, length).toString();
      records.push_back({text, logicalId, pos != 0, pos + text.size() < line.size()});
      if (line.isEmpty()) break;
      pos += length;
    }
  }
  appendRecords(std::move(records));
}

void TerminalLineModel::appendRecords(std::vector<SshLogLine> lines) {
  if (lines.empty()) return;
  qsizetype incomingBytes = 0;
  for (const auto &line : lines) incomingBytes += line.storageBytes();
  size_t start = 0;
  while (start < lines.size() && (int(lines.size() - start) > m_maxLines ||
                                  incomingBytes > SshLogStream::HistoryBytes))
    incomingBytes -= lines[start++].storageBytes();
  const int incoming = int(lines.size() - start);
  // If even the incoming batch was trimmed, every existing row is older than discarded data.
  int removed = start ? int(m_lines.size()) : 0;
  qsizetype removedBytes = start ? m_bytes : 0;
  while (removed < int(m_lines.size()) &&
         (int(m_lines.size()) - removed + incoming > m_maxLines ||
          m_bytes - removedBytes + incomingBytes > SshLogStream::HistoryBytes))
    removedBytes += m_lines[size_t(removed++)].storageBytes();
  if (removed > 0 && removed == incoming && start == 0) {
    // A full rolling window keeps its slots; repaint visible data without rebuilding ListView geometry.
    for (int i = 0; i < removed; ++i) m_lines.pop_front();
    for (auto &line : lines) m_lines.push_back(std::move(line));
    m_firstLineId += removed; m_nextLineId += incoming;
    m_bytes += incomingBytes - removedBytes;
    emit dataChanged(index(0), index(rowCount() - 1));
    return;
  }
  if (removed) {
    beginRemoveRows({}, 0, removed - 1);
    for (int i = 0; i < removed; ++i) m_lines.pop_front();
    m_bytes -= removedBytes; m_firstLineId += removed;
    endRemoveRows();
  }
  m_nextLineId += qint64(start);
  if (m_lines.empty()) m_firstLineId = m_nextLineId;
  if (incoming) {
    const int first = int(m_lines.size());
    beginInsertRows({}, first, first + incoming - 1);
    for (; start < lines.size(); ++start) m_lines.push_back(std::move(lines[start]));
    m_nextLineId += incoming; m_bytes += incomingBytes;
    endInsertRows();
  }
}

void TerminalLineModel::setMaxLines(int maxLines) {
  m_maxLines = qBound(100, maxLines, 200000);
  const int remove = qMax(0, int(m_lines.size()) - m_maxLines);
  if (!remove) return;
  beginRemoveRows({}, 0, remove - 1);
  for (int i = 0; i < remove; ++i) { m_bytes -= m_lines.front().storageBytes(); m_lines.pop_front(); }
  m_firstLineId += remove;
  endRemoveRows();
}

void TerminalLineModel::clear() {
  if (m_lines.empty())
    return;
  beginResetModel();
  m_lines.clear();
  m_bytes = 0; m_firstLineId = m_nextLineId;
  endResetModel();
}

QString TerminalLineModel::lineAt(int index) const {
  if (index < 0 || index >= int(m_lines.size())) {
    return {};
  }
  return m_lines[size_t(index)].text;
}

QStringList TerminalLineModel::lines() const {
  QStringList result;
  for (const auto &line : m_lines) result.append(line.text);
  return result;
}

QString TerminalLineModel::bufferText() const {
  QString result;
  for (const auto &line : m_lines) {
    result.append(line.text);
    if (!line.continues) result.append('\n');
  }
  return result;
}

QString TerminalLineModel::textRange(qint64 firstId, int firstOffset, qint64 lastId,
                                    int lastOffset, const QString &tail) const {
  if (firstId > lastId || (firstId == lastId && firstOffset > lastOffset)) {
    std::swap(firstId, lastId);
    std::swap(firstOffset, lastOffset);
  }
  const int first = firstId == m_nextLineId ? rowCount() : indexForId(firstId);
  const int last = lastId == m_nextLineId ? rowCount() : indexForId(lastId);
  if (first < 0 || last < first) return {};
  QString result;
  for (int row = first; row <= last; ++row) {
    const QString text = row == rowCount() ? tail : lineAt(row);
    const int length = int(text.size());
    const int from = row == first ? qBound(0, firstOffset, length) : 0;
    const int to = row == last ? qBound(from, lastOffset, length) : length;
    result.append(text.mid(from, to - from));
    if (row < last && !m_lines[size_t(row)].continues) result.append('\n');
  }
  return result;
}

qint64 TerminalLineModel::lineId(int index) const {
  return index >= 0 && index < int(m_lines.size()) ? m_firstLineId + index : -1;
}

int TerminalLineModel::indexForId(qint64 id) const {
  return id >= m_firstLineId && id < m_nextLineId ? int(id - m_firstLineId) : -1;
}

int TerminalLineModel::logicalStart(int index) const {
  if (index < 0 || index >= int(m_lines.size())) return -1;
  while (index > 0 && m_lines[size_t(index - 1)].logicalId == m_lines[size_t(index)].logicalId) --index;
  return index;
}

int TerminalLineModel::logicalEnd(int index) const {
  if (index < 0 || index >= int(m_lines.size())) return -1;
  while (index + 1 < int(m_lines.size()) && m_lines[size_t(index + 1)].logicalId == m_lines[size_t(index)].logicalId) ++index;
  return index;
}

QString TerminalLineModel::logicalLineAt(int index) const {
  const int start = logicalStart(index), end = logicalEnd(index);
  if (start < 0) return {};
  QString result;
  for (int i = start; i <= end; ++i) result.append(m_lines[size_t(i)].text);
  return result;
}

bool TerminalLineModel::completeLineAt(int index) const {
  const int start = logicalStart(index), end = logicalEnd(index);
  return start >= 0 && !m_lines[size_t(start)].continuation && !m_lines[size_t(end)].continues;
}

int TerminalLineModel::segmentOffset(int index) const {
  const int start = logicalStart(index);
  int offset = 0;
  for (int i = start; i >= 0 && i < index; ++i) offset += int(m_lines[size_t(i)].text.size());
  return offset;
}

qint64 TerminalLineModel::idForLogicalOffset(quint64 logicalId, qsizetype offset) const {
  qsizetype position = 0;
  bool seen = false;
  for (int i = 0; i < int(m_lines.size()); ++i) {
    const auto &line = m_lines[size_t(i)];
    if (line.logicalId != logicalId) continue;
    if (!seen && line.continuation) return -2;
    seen = true;
    if (position + line.text.size() > offset || !line.continues) return lineId(i);
    position += line.text.size();
  }
  return -1;
}

bool SshTerminal::setMonitorRules(const QVariantList &rules, bool armed) {
  if (!SshMonitorMatcher::validRules(rules)) return false;
  QMutexLocker lock(&m_buffer->mutex);
  const QVariantList selected = armed ? rules : QVariantList{};
  if (m_buffer->monitorRules == selected) return true;
  m_buffer->monitorRules = selected;
  ++m_buffer->monitorRevision;
  m_buffer->monitorHit = {};
  return true;
}

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
  m_maxBufferLines = qBound(100, m_maxBufferLines, 200000);
  m_lineModel->setMaxLines(m_maxBufferLines);
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
  connect(m_worker, &SshWorker::dataReady, this, [this](quint64 generation, quint64 revision) {
    if (generation != m_generation || revision != m_buffer->contentRevision.load()) return;
    if (m_buffer->receptionPauseRequested.load()) return;
    const int delay = m_presentationActive ? (m_lastInput.isValid() && m_lastInput.elapsed() < 1000 ? 16 : 33) : 500;
    if (!m_flushTimer->isActive() || m_flushTimer->remainingTime() > delay) m_flushTimer->start(delay);
  });
  connect(m_worker, &SshWorker::receptionStateApplied, this, [this](quint64 revision, bool paused) {
    if (revision != m_receptionRevision || paused != m_buffer->receptionPauseRequested.load()) return;
    if (paused) {
      // The worker has stopped reading. Commit only the data received before it stopped.
      for (;;) {
        flushPendingData();
        QMutexLocker lock(&m_buffer->mutex);
        if (m_buffer->lines.empty()) break;
      }
      m_flushTimer->stop();
    }
    if (m_receptionPaused == paused) return;
    m_receptionPaused = paused;
    emit receptionPausedChanged();
  });
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

  m_flushTimer->setInterval(33);
  m_flushTimer->setTimerType(Qt::PreciseTimer);
  m_flushTimer->setSingleShot(true);
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
  if (!usesSecureCredentials()) {
    m_credentialBusy = false;
    m_refreshPending = false;
    emit credentialStateChanged();
    return;
  }
  m_refreshPending = true;
  if (!m_credentialInFlight) dispatchCredentialRefresh();
  emit credentialStateChanged();
}

void SshTerminal::setUseSystemCredentials(bool enabled) {
  if (m_useSystemCredentials == enabled) return;
  m_useSystemCredentials = enabled;
  refreshCredentials();
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
  return usesSecureCredentials() ? QString() : m_password;
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
    ++m_buffer->contentRevision;
    flushFinalPendingData();
    m_encoding = encoding;
    resetPendingData(false);
    if (m_persistSettings) {
      auto settings = this->settings();
      settings.setValue("ssh/encoding", m_encoding);
      settings.sync();
    }
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
  if (m_persistSettings) {
    auto settings = this->settings();
    settings.setValue("ssh/autoReconnectEnabled", m_autoReconnectEnabled);
    settings.sync();
  }
  emit autoReconnectEnabledChanged();
}

int SshTerminal::reconnectMaxAttempts() const { return m_reconnectMaxAttempts; }

void SshTerminal::setReconnectMaxAttempts(int attempts) {
  attempts = qMax(0, attempts);
  if (m_reconnectMaxAttempts == attempts) {
    return;
  }
  m_reconnectMaxAttempts = attempts;
  if (m_persistSettings) {
    auto settings = this->settings();
    settings.setValue("ssh/reconnectMaxAttempts", m_reconnectMaxAttempts);
    settings.sync();
  }
  emit reconnectMaxAttemptsChanged();
}

int SshTerminal::reconnectIntervalMs() const { return m_reconnectIntervalMs; }

void SshTerminal::setReconnectIntervalMs(int intervalMs) {
  intervalMs = qMax(500, intervalMs);
  if (m_reconnectIntervalMs == intervalMs) {
    return;
  }
  m_reconnectIntervalMs = intervalMs;
  if (m_persistSettings) {
    auto settings = this->settings();
    settings.setValue("ssh/reconnectIntervalMs", m_reconnectIntervalMs);
    settings.sync();
  }
  emit reconnectIntervalMsChanged();
}

int SshTerminal::reconnectAttempt() const { return m_reconnectAttempt; }

int SshTerminal::maxBufferLines() const { return m_maxBufferLines; }
void SshTerminal::setMaxBufferLines(int maxLines) {
  maxLines = qBound(100, maxLines, 200000);
  if (m_maxBufferLines != maxLines) {
    emit linesAboutToChange();
    m_maxBufferLines = maxLines;
    m_lineModel->setMaxLines(maxLines);
    emit maxBufferLinesChanged();
    emit lineCountChanged();
  }
}

int SshTerminal::currentLineCount() const { return lineModel()->rowCount(); }

TerminalLineModel *SshTerminal::lineModel() const { return m_lineModel; }

void SshTerminal::setReceptionPaused(bool paused) {
  if (m_buffer->receptionPauseRequested.exchange(paused) == paused) return;
  const auto revision = ++m_receptionRevision;
  if (paused) {
    m_flushTimer->stop();
    if (m_tabBusy) finishTabCapture();
  }
  QMetaObject::invokeMethod(m_worker, [worker = m_worker, revision, paused] { worker->applyReceptionState(revision, paused); });
}

void SshTerminal::setPresentationActive(bool active) {
  if (m_presentationActive == active) return;
  m_presentationActive = active;
  if (m_flushTimer->isActive()) m_flushTimer->start(active ? 0 : 500);
  emit presentationActiveChanged();
}

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
  resetPendingData(false);
  emit sigDisconnect(previous);
  emit sigStartConnect(m_generation, m_host, m_port, m_username, m_password, m_encoding, usesSecureCredentials());
}

void SshTerminal::connectToHost() {
  if (!validSshIdentity(m_host, m_port, m_username)) {
    setConnectionState(ConnectionState::Failed);
    setStatusMessage(QStringLiteral("请输入有效的主机、端口和用户名"));
    return;
  }
  if (usesSecureCredentials() && (credentialBusy() || !m_credentialError.isEmpty())) {
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
  flushFinalPendingData();
  resetPendingData(false);
  m_manualDisconnectRequested = true;
  m_reconnectPending = false;
  if (m_tabTimer) m_tabTimer->stop();
  m_tabBusy = false; m_tabPhase = 0;
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
  if (m_connected && !m_buffer->receptionPauseRequested.load()) {
    prioritizeInput();
    // 先清空远端当前输入行，避免 Tab
    // 补全阶段残留内容（候选/前缀）与最终命令拼接 Ctrl+U: Unix shell
    // 行编辑“清空到行首”
    emit sigSendCommand(QString(QChar(0x15)) + cmd, true, m_encoding);
  }
}

void SshTerminal::sendRawBytes(const QString &text) {
  if (m_connected && !m_buffer->receptionPauseRequested.load()) {
    prioritizeInput();
    emit sigSendCommand(text, false, m_encoding);
  }
}

void SshTerminal::prioritizeInput() {
  m_lastInput.start();
  // A pending notification suppresses new dataReady signals, so advance its existing deadline here.
  if (m_presentationActive && m_flushTimer->isActive() && m_flushTimer->remainingTime() > 16)
    m_flushTimer->start(16);
}

void SshTerminal::sendTabCompletion(const QString &prefix) {
  if (!m_connected || m_buffer->receptionPauseRequested.load())
    return;
  // 防重入：Tab 序列结束前（包含 phase0→phase1 过渡）忽略所有新按键
  if (m_tabBusy)
    return;

  // 先将已积压的数据正常刷到终端，确保之后收到的全是补全响应
  flushPendingData();

  m_tabPrefix = prefix;
  m_tabRawBuffer.clear();
  m_tabBusy = true; // 整个序列期间锁定
  m_tabPhase = 0;
  m_tabTimer->setInterval(600); // Phase0：600ms 等待首次响应
  m_tabTimer->start();
  const auto generation = m_generation;
  QMetaObject::invokeMethod(m_worker, [worker = m_worker, generation] { worker->beginTabCapture(generation); });

  // 发送「当前文本 + \t」触发远端 shell 补全
  emit sigSendCommand(prefix + "\t", false, m_encoding);
}

QString SshTerminal::getBufferText() const {
  QString partial;
  {
    QMutexLocker locker(&m_buffer->mutex);
    partial = m_buffer->lines.empty() ? m_buffer->partial.text : m_committedPartial;
  }
  QString text = m_lineModel->bufferText();
  text.append(partial);
  if (partial.isEmpty() && text.endsWith('\n')) text.chop(1);
  return text;
}

QVariantList SshTerminal::findLineIndexes(const QString &keyword,
                                          bool caseSensitive) const {
  QVariantList result;
  if (keyword.isEmpty()) {
    return result;
  }
  const Qt::CaseSensitivity sensitivity =
      caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
  const int total = lineModel()->rowCount();
  for (int i = 0; i < total; ++i) {
    const QString line = lineModel()->lineAt(i);
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
  const int total = lineModel()->rowCount();
  // 修正范围边界，防止越界
  const int from = qMax(0, fromLine);
  const int to = qMin(total - 1, toLine);
  for (int i = from; i <= to; ++i) {
    if (lineModel()->lineAt(i).contains(keyword, sensitivity))
      result << i;
  }
  return result;
}

QString SshTerminal::lineAt(int index) const {
  return lineModel()->logicalLineAt(index);
}

QVariantList SshTerminal::findLineIds(const QString &keyword, qint64 fromId) const {
  QVariantList result;
  if (keyword.isEmpty()) return result;
  const auto startId = qMax(fromId, firstLineId());
  int row = lineModel()->indexForId(startId);
  if (row < 0) return result;
  row = lineModel()->logicalStart(row);
  while (row < lineModel()->rowCount()) {
    if (lineModel()->logicalLineAt(row).contains(keyword, Qt::CaseInsensitive))
      result.append(lineModel()->lineId(row));
    row = lineModel()->logicalEnd(row) + 1;
  }
  return result;
}

void SshTerminal::copyLineToClipboard(int index) const {
  QString line = lineModel()->logicalLineAt(index);
  if (line.isEmpty()) {
    return;
  }
  if (!lineModel()->completeLineAt(index)) line.prepend(QStringLiteral("[日志片段，内容尚未完整或较早部分已淘汰]\n"));
  if (QGuiApplication::clipboard()) {
    QGuiApplication::clipboard()->setText(line);
  }
}

void SshTerminal::copyBufferToClipboard() const {
  if (QGuiApplication::clipboard()) {
    QString text = getBufferText();
    if (lineModel()->rowCount() && !lineModel()->completeLineAt(0))
      text.prepend(QStringLiteral("[首条日志为片段，较早内容已淘汰或尚未完整]\n"));
    QGuiApplication::clipboard()->setText(text);
  }
}

QString SshTerminal::clipboardText() const {
  if (!QGuiApplication::clipboard()) {
    return {};
  }
  return QGuiApplication::clipboard()->text();
}

void SshTerminal::clearBuffer() {
  resetPendingData(true);
  emit linesAboutToChange();
  m_lineModel->clear();
  emit lineCountChanged();
}

void SshTerminal::onWorkerConnected() {
  auto settings = this->settings();
  if (m_persistSettings) {
    settings.setValue("ssh/host", m_host);
    settings.setValue("ssh/port", m_port);
    settings.setValue("ssh/username", m_username);
    settings.setValue("ssh/maxLines", m_maxBufferLines);
    settings.sync();
  }
  const bool configurationSaved = settings.status() == QSettings::NoError;

  m_manualDisconnectRequested = false;
  m_reconnectPending = false;
  if (m_reconnectAttempt != 0) {
    emit linesAboutToChange();
    m_lineModel->appendLines({QStringLiteral("[%1] SSH 已重新连接成功")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"))});
    emit lineCountChanged();
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
  flushFinalPendingData();
  emit linesAboutToChange();
  m_lineModel->appendLines({errorStr});
  emit lineCountChanged();
  setLastError(errorStr);
  setStatusMessage(errorStr);
  emit connectionError(errorStr);
  setConnected(false);
  scheduleReconnect(errorStr);
}

void SshTerminal::onWorkerDisconnected() {
  m_flushTimer->stop();
  flushFinalPendingData();
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

void SshTerminal::resetPendingData(bool discardPartial) {
  m_flushTimer->stop();
  m_committedPartial.clear(); m_committedMonitorColor.clear();
  m_cursorColumn = 0;
  m_cursorPosition = m_cursorPadding = 0;
  emit liveLineChanged();
  const auto revision = ++m_buffer->contentRevision;
  bool hadPartial = false;
  {
    QMutexLocker lock(&m_buffer->mutex);
    hadPartial = !m_buffer->partial.text.isEmpty() || m_buffer->partial.continuation || m_buffer->partial.continues;
    m_buffer->lines.clear(); m_buffer->bytes = 0; m_buffer->partial = {}; m_buffer->cursorColumn = 0;
    m_buffer->tabData.clear(); m_buffer->tabCapture = false; m_buffer->tabOverflow = false;
    m_buffer->notificationPending = false;
    m_buffer->monitorHit = {};
    ++m_buffer->monitorRevision;
  }
  if (m_tabBusy) finishTabCapture();
  const auto encoding = m_encoding;
  QMetaObject::invokeMethod(m_worker, [worker = m_worker, revision, encoding, discard = discardPartial && hadPartial] {
    worker->resetStream(revision, encoding, discard);
  });
}

void SshTerminal::flushFinalPendingData() {
  for (;;) {
    flushPendingData();
    QMutexLocker lock(&m_buffer->mutex);
    if (m_buffer->lines.empty()) break;
  }
  SshLogLine tail;
  {
    QMutexLocker lock(&m_buffer->mutex);
    tail = std::exchange(m_buffer->partial, {});
  }
  if (!tail.text.isEmpty()) {
    tail.continues = false;
    emit linesAboutToChange();
    m_lineModel->appendRecords({std::move(tail)});
    emit lineCountChanged();
  }
  m_flushTimer->stop();
  m_committedPartial.clear(); m_committedMonitorColor.clear(); m_cursorColumn = m_cursorPosition = m_cursorPadding = 0; emit liveLineChanged();
}

void SshTerminal::flushPendingData() {
  m_flushTimer->stop();
  std::vector<SshLogLine> lines;
  qsizetype bytes = 0;
  bool more, tabOverflow;
  QString live;
  QString liveColor;
  int column, position, padding;
  SshMonitorHit hit;
  quint64 partialLogicalId;
  {
    QMutexLocker lock(&m_buffer->mutex);
    while (!m_buffer->lines.empty() && lines.size() < 4096 && bytes < 2 * 1024 * 1024) {
      bytes += m_buffer->lines.front().storageBytes();
      lines.push_back(std::move(m_buffer->lines.front())); m_buffer->lines.pop_front();
    }
    m_buffer->bytes -= bytes;
    more = !m_buffer->lines.empty();
    live = more ? QString() : m_buffer->partial.text;
    liveColor = more ? QString() : m_buffer->partial.monitorColor;
    partialLogicalId = m_buffer->partial.logicalId;
    column = more ? 0 : m_buffer->cursorColumn;
    position = more ? 0 : m_buffer->cursorPosition; padding = more ? 0 : m_buffer->cursorPadding;
    m_buffer->notificationPending = more;
    const auto &pending = m_buffer->monitorHit;
    if (!pending.data.isEmpty() && pending.revision == m_buffer->monitorRevision
        && (!more || std::any_of(lines.begin(), lines.end(), [&](const SshLogLine &line) { return line.logicalId == pending.logicalId; })))
      hit = std::exchange(m_buffer->monitorHit, {});
    tabOverflow = m_buffer->tabOverflow; m_buffer->tabOverflow = false;
  }
  if (!lines.empty()) {
    emit linesAboutToChange();
    m_lineModel->appendRecords(std::move(lines));
  }
  if (!hit.data.isEmpty()) {
    auto data = hit.data;
    data["lineId"] = m_lineModel->idForLogicalOffset(hit.logicalId, hit.offset);
    if (data["lineId"].toLongLong() == -1 && !more && !live.isEmpty() && partialLogicalId == hit.logicalId)
      data["lineId"] = m_lineModel->firstLineId() + m_lineModel->rowCount();
    data["time"] = QDateTime::currentDateTime().toString("HH:mm:ss");
    data["receivedLineId"] = m_lineModel->firstLineId() + m_lineModel->rowCount();
    emit monitorMatched(data);
  }
  if (bytes) emit lineCountChanged();
  if (m_committedPartial != live || m_cursorColumn != column || m_committedMonitorColor != liveColor) {
    m_committedPartial = std::move(live); m_cursorColumn = column;
    m_committedMonitorColor = liveColor;
    m_cursorPosition = position; m_cursorPadding = padding;
    emit liveLineChanged();
  }
  if (tabOverflow) {
    finishTabCapture();
    setStatusMessage(QStringLiteral("Tab 响应过大，已停止补全并恢复普通日志接收"));
    emit tabCompletionReady({}, {});
  }
  QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->resumeRead(); });
  if (more) m_flushTimer->start(1);
}

void SshTerminal::finishTabCapture() {
  m_tabBusy = false;
  m_tabRawBuffer.clear();
  if (m_tabTimer) m_tabTimer->stop();
  const auto generation = m_generation;
  QMetaObject::invokeMethod(m_worker, [worker = m_worker, generation] { worker->endTabCapture(generation); });
}

void SshTerminal::collectTabData() {
  QMutexLocker lock(&m_buffer->mutex);
  m_tabRawBuffer.append(m_buffer->tabData);
  m_buffer->tabData.clear();
}

void SshTerminal::processTabCompletion() {
  if (!m_connected || !m_tabBusy) return;
  // Keep worker capture active until completion has been resolved.

  collectTabData();

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
      finishTabCapture();
      emit tabCompletionReady("", {});
    }
    return;
  }

  SshLogStream response(m_encoding);
  auto records = response.feed(m_tabRawBuffer);
  auto tail = response.finish();
  records.insert(records.end(), std::make_move_iterator(tail.begin()), std::make_move_iterator(tail.end()));
  QString raw;
  for (const auto &record : records) {
    raw.append(record.text);
    if (!record.continues) raw.append('\n');
  }

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
        // A column of candidates may itself contain the prefix (e.g. "lock/  log/").
        if (line.contains(m_tabPrefix) && !line.contains("  ") && !line.contains('\t'))
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
    finishTabCapture();
    emit tabCompletionReady(completedCmd, {});
  } else if (!candidates.isEmpty()) {
    // 多候选：把候选词拼成一行干净展示到终端
    finishTabCapture();
    emit linesAboutToChange();
    m_lineModel->appendLines({candidates.join("  ")});
    emit lineCountChanged();
    emit tabCompletionReady("", candidates);
  } else {
    // 无有效 token（如全是 ANSI 控制字符）——按阶段处理
    m_tabRawBuffer.clear();
    if (m_tabPhase == 0) {
      // 第一阶段无增量：自动进入第二阶段，保持截获状态
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
      finishTabCapture();
      emit tabCompletionReady("", {});
    }
  }
}

#include "SshTerminal.moc"
