#ifndef SSHTERMINAL_H
#define SSHTERMINAL_H

#include <QAbstractListModel>
#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QSettings>
#include <memory>
#include <atomic>
#include "SecureCredentialStore.h"

// TerminalLineModel：给 QML ListView 使用的终端行模型
// 核心特性：固定行数上限（环形缓冲语义），内存永远有界
class TerminalLineModel : public QAbstractListModel {
  Q_OBJECT
public:
  enum Roles { LineTextRole = Qt::UserRole + 1 };

  explicit TerminalLineModel(int maxLines, QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  // 追加若干行，若超出上限则从头部移除旧行（O(1) amortized）
  void appendLines(const QStringList &lines);
  void clear();
  QString lineAt(int index) const;
  QStringList lines() const;
  int maxLines() const { return m_maxLines; }

private:
  QStringList m_lines;
  int m_maxLines;
};

struct SshSharedBuffer {
  QMutex mutex;
  QByteArray data;
  std::atomic<quint64> activeGeneration{0};
  std::atomic<quint64> credentialRevision{0};
};
class SshWorker; // 前向声明

class SshTerminal : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString host READ host WRITE setHost NOTIFY hostChanged)
  Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
  Q_PROPERTY(
      QString username READ username WRITE setUsername NOTIFY usernameChanged)
  Q_PROPERTY(
      QString password READ password WRITE setPassword NOTIFY passwordChanged)
  Q_PROPERTY(bool usesSecureCredentials READ usesSecureCredentials CONSTANT)
  Q_PROPERTY(
      QString encoding READ encoding WRITE setEncoding NOTIFY encodingChanged)
  Q_PROPERTY(bool hasSavedPassword READ hasSavedPassword NOTIFY credentialStateChanged)
  Q_PROPERTY(bool credentialBusy READ credentialBusy NOTIFY credentialStateChanged)
  Q_PROPERTY(QString credentialError READ credentialError NOTIFY credentialStateChanged)
  Q_PROPERTY(bool hostKeyPending READ hostKeyPending NOTIFY hostKeyPendingChanged)
  Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
  Q_PROPERTY(
      int connectionState READ connectionState NOTIFY connectionStateChanged)
  Q_PROPERTY(
      QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
  Q_PROPERTY(bool autoReconnectEnabled READ autoReconnectEnabled WRITE
                 setAutoReconnectEnabled NOTIFY autoReconnectEnabledChanged)
  Q_PROPERTY(int reconnectMaxAttempts READ reconnectMaxAttempts WRITE
                 setReconnectMaxAttempts NOTIFY reconnectMaxAttemptsChanged)
  Q_PROPERTY(int reconnectIntervalMs READ reconnectIntervalMs WRITE
                 setReconnectIntervalMs NOTIFY reconnectIntervalMsChanged)
  Q_PROPERTY(
      int reconnectAttempt READ reconnectAttempt NOTIFY reconnectAttemptChanged)
  Q_PROPERTY(int maxBufferLines READ maxBufferLines WRITE setMaxBufferLines
                 NOTIFY maxBufferLinesChanged)
  Q_PROPERTY(int currentLineCount READ currentLineCount NOTIFY lineCountChanged)
  // QML ListView 的数据模型，替代 TextArea 实现内存有界的虚拟化渲染
  Q_PROPERTY(TerminalLineModel *lineModel READ lineModel CONSTANT)

public:
  enum ConnectionState {
    Idle = 0,
    Connecting = 1,
    Connected = 2,
    Reconnecting = 3,
    Failed = 4,
    Disconnected = 5
  };
  Q_ENUM(ConnectionState)

  explicit SshTerminal(QObject *parent = nullptr);
  SshTerminal(std::shared_ptr<SecureCredentialStore> credentials,
              const QString &dataRoot, QObject *parent = nullptr);
  ~SshTerminal() override;

  QString host() const;
  void setHost(const QString &host);

  int port() const;
  void setPort(int port);

  QString username() const;
  void setUsername(const QString &username);

  QString password() const;
  bool usesSecureCredentials() const {
#ifdef Q_OS_MACOS
    return true;
#else
    return false;
#endif
  }
  void setPassword(const QString &password);

  bool hasSavedPassword() const { return m_hasSavedPassword; }
  bool credentialBusy() const { return m_credentialBusy || m_authCredentialBusy; }
  QString credentialError() const { return m_credentialError; }
  bool hostKeyPending() const { return !m_hostKeyRequestId.isEmpty(); }
  Q_INVOKABLE QString savePassword(const QString &password);
  Q_INVOKABLE QString clearSavedPassword();
  Q_INVOKABLE void confirmHostKey(const QString &requestId, const QString &fingerprint, bool accept);

  QString encoding() const;
  void setEncoding(const QString &encoding);

  bool isConnected() const;
  int connectionState() const;
  QString statusMessage() const;
  QString lastError() const;
  bool autoReconnectEnabled() const;
  void setAutoReconnectEnabled(bool enabled);
  int reconnectMaxAttempts() const;
  void setReconnectMaxAttempts(int attempts);
  int reconnectIntervalMs() const;
  void setReconnectIntervalMs(int intervalMs);
  int reconnectAttempt() const;

  int maxBufferLines() const;
  void setMaxBufferLines(int maxBufferLines);

  int currentLineCount() const;
  TerminalLineModel *lineModel() const;

  Q_INVOKABLE void connectToHost();
  Q_INVOKABLE void disconnectFromHost();
  Q_INVOKABLE void sendCommand(const QString &cmd);
  Q_INVOKABLE void sendRawBytes(const QString &text);
  // 发送 Tab 补全请求：截获响应并解析后通过 tabCompletionReady 信号通知 QML
  Q_INVOKABLE void sendTabCompletion(const QString &prefix);
  Q_INVOKABLE QString getBufferText();
  Q_INVOKABLE void clearBuffer();
  Q_INVOKABLE QVariantList findLineIndexes(const QString &keyword,
                                           bool caseSensitive = false) const;
  // 增量搜索：只在 [fromLine, toLine] 区间内检索，避免日志滚动时全量遍历
  Q_INVOKABLE QVariantList
  findLineIndexesInRange(const QString &keyword, int fromLine, int toLine,
                         bool caseSensitive = false) const;
  Q_INVOKABLE QString lineAt(int index) const;
  Q_INVOKABLE void copyLineToClipboard(int index) const;
  Q_INVOKABLE void copyBufferToClipboard() const;
  Q_INVOKABLE QString clipboardText() const;

signals:
  void credentialStateChanged();
  void passwordOperationFinished(const QString &operationId, bool success, const QString &error);
  void hostKeyPendingChanged();
  void hostKeyConfirmationRequested(const QString &host, int port,
      const QString &algorithm, const QString &fingerprint, const QString &requestId);
  void hostChanged();
  void portChanged();
  void usernameChanged();
  void passwordChanged();
  void encodingChanged();
  void connectedChanged();
  void connectionStateChanged();
  void statusMessageChanged();
  void lastErrorChanged();
  void autoReconnectEnabledChanged();
  void reconnectMaxAttemptsChanged();
  void reconnectIntervalMsChanged();
  void reconnectAttemptChanged();
  void maxBufferLinesChanged();
  void lineCountChanged(); // currentLineCount 变化（用于自动滚动）
  void connectionError(const QString &error);
  void sharedBufferOverflowed();
  // Tab 补全结果：completedText 非空表示有唯一补全，空表示无匹配；
  // candidates 为多候选列表（completedText 为空时有效）
  void tabCompletionReady(const QString &completedText,
                          const QStringList &candidates);

  // 发送给工作线程的信号
  void sigStartConnect(quint64 generation, const QString &host, int port,
                       const QString &user, const QString &pass);
  void sigSendCommand(const QString &cmd, bool appendNewline, const QString &encoding);
  void sigDisconnect(quint64 generation);

private slots:
  void onWorkerConnected();
  void onWorkerError(const QString &error);
  void onWorkerDisconnected();
  void flushPendingData();
  void processTabCompletion(); // Tab 补全定时器超时后解析响应
  void triggerReconnect();

private:
  QSettings settings() const;
  void refreshCredentials();
  void dispatchCredentialRefresh();
  void identityChanged();
  void clearHostKeyRequest();
  void startConnection();
  void setStatusMessage(const QString &msg);
  void setConnected(bool connected);
  void setConnectionState(ConnectionState state);
  void setLastError(const QString &error);
  void scheduleReconnect(const QString &reason);
  void enforceBufferLimit(); // 调用方需已持有 m_mutex

  std::shared_ptr<SshSharedBuffer> m_buffer = std::make_shared<SshSharedBuffer>();

  QThread *m_workerThread;
  SshWorker *m_worker;
  TerminalLineModel *m_lineModel;

  QString m_dataRoot;
  bool m_hasSavedPassword = false;
  bool m_credentialBusy = false;
  bool m_authCredentialBusy = false;
  quint64 m_authCredentialGeneration = 0;
  bool m_credentialInFlight = false;
  bool m_refreshPending = false;
  QString m_credentialError;
  quint64 m_credentialGeneration = 0;
  QHash<quint64, QString> m_passwordOperations;
  quint64 m_generation = 0;
  bool m_securityFailure = false;
  QString m_hostKeyRequestId;
  QString m_hostKeyFingerprint;
  QString m_host;
  int m_port = 22;
  QString m_username;
  QString m_password;
  QString m_encoding = "UTF-8";

  bool m_connected;
  ConnectionState m_connectionState;
  QString m_statusMessage;
  QString m_lastError;
  int m_maxBufferLines;
  bool m_autoReconnectEnabled = true;
  int m_reconnectMaxAttempts = 5;
  int m_reconnectIntervalMs = 2000;
  int m_reconnectAttempt = 0;
  bool m_manualDisconnectRequested = false;
  bool m_reconnectPending = false;

  // 共享原始缓冲区：工作线程直接写入，主线程 flushTimer 读取，消除事件队列积压
  QString m_partialLine; // 未完成的行尾

  QTimer *m_flushTimer;
  QTimer *m_reconnectTimer = nullptr;

  // Tab 补全截获模式
  bool m_tabMode = false; // 是否正在截获 SSH 数据流（整个序列期间不清除）
  bool m_tabBusy = false; // 防重入标志
  int m_tabPhase = 0; // 0=第一次Tab  1=第二次Tab候选收集  2=第二次Tab第二次等待
  QString m_tabPrefix;          // 按 Tab 时输入框的文本
  QByteArray m_tabRawBuffer;    // Tab 模式下收到的原始字节
  QTimer *m_tabTimer = nullptr; // 超时后触发 processTabCompletion
};

#endif // SSHTERMINAL_H
