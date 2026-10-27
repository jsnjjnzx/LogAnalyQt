#ifndef SSHTERMINAL_H
#define SSHTERMINAL_H

#include <QAbstractListModel>
#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantList>
#include <QUrl>
#include "../common/JsonSettings.h"
#include <memory>
#include <atomic>
#include "SecureCredentialStore.h"
#include "SshLogStream.h"
#include "SshMonitorMatcher.h"
#include <deque>

// TerminalLineModel：给 QML ListView 使用的终端行模型
// History is bounded by both rows and allocated text bytes; row identities survive eviction.
class TerminalLineModel : public QAbstractListModel {
  Q_OBJECT
public:
  enum Roles { LineTextRole = Qt::UserRole + 1, LineIdRole, ContinuationRole, ContinuesRole, MonitorColorRole, MonitorRuleNameRole, MonitorWholeLineRole, MonitorRangesRole };

  explicit TerminalLineModel(int maxLines, QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  void appendLines(const QStringList &lines);
  void appendRecords(std::vector<SshLogLine> lines);
  void applyMonitorMark(const SshLogLine &source, int before = -1);
  void setMaxLines(int maxLines);
  void clear();
  QString lineAt(int index) const;
  QStringList lines() const;
  QString bufferText() const;
  QString textRange(qint64 firstId, int firstOffset, qint64 lastId, int lastOffset,
                    const QString &tail = {}) const;
  QString logicalLineAt(int index) const;
  bool completeLineAt(int index) const;
  int logicalStart(int index) const;
  int logicalEnd(int index) const;
  int segmentOffset(int index) const;
  qint64 idForLogicalOffset(quint64 logicalId, qsizetype offset) const;
  qint64 firstLineId() const { return m_firstLineId; }
  qint64 lineId(int index) const;
  int indexForId(qint64 id) const;
  qsizetype byteSize() const { return m_bytes; }
  int maxLines() const { return m_maxLines; }

private:
  std::deque<SshLogLine> m_lines;
  int m_maxLines;
  qsizetype m_bytes = 0;
  qint64 m_firstLineId = 0;
  qint64 m_nextLineId = 0;
  quint64 m_syntheticId = ~quint64(0);
};

struct SshSharedBuffer {
  QMutex mutex;
  std::deque<SshLogLine> lines;
  qsizetype bytes = 0;
  SshLogLine partial;
  int cursorColumn = 0;
  int cursorPosition = 0;
  int cursorPadding = 0;
  QByteArray tabData;
  bool tabCapture = false;
  bool tabOverflow = false;
  bool notificationPending = false;
  QVariantList monitorRules;
  quint64 monitorRevision = 0;
  SshMonitorHit monitorHit;
  std::atomic<bool> receptionPauseRequested{false};
  std::atomic<quint64> activeGeneration{0};
  std::atomic<quint64> credentialRevision{0};
  std::atomic<quint64> contentRevision{0};
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
  Q_PROPERTY(bool usesSecureCredentials READ usesSecureCredentials NOTIFY credentialStateChanged)
  Q_PROPERTY(bool useSystemCredentials READ useSystemCredentials WRITE setUseSystemCredentials NOTIFY credentialStateChanged)
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
  Q_PROPERTY(qint64 firstLineId READ firstLineId NOTIFY lineCountChanged)
  Q_PROPERTY(bool presentationActive READ presentationActive WRITE setPresentationActive NOTIFY presentationActiveChanged)
  Q_PROPERTY(bool receptionPaused READ receptionPaused WRITE setReceptionPaused NOTIFY receptionPausedChanged)
  Q_PROPERTY(bool persistSettings MEMBER m_persistSettings)
  Q_PROPERTY(QString liveLine READ liveLine NOTIFY liveLineChanged)
  Q_PROPERTY(QString liveMonitorColor READ liveMonitorColor NOTIFY liveLineChanged)
  Q_PROPERTY(bool liveMonitorWholeLine READ liveMonitorWholeLine NOTIFY liveLineChanged)
  Q_PROPERTY(QVariantList liveMonitorRanges READ liveMonitorRanges NOTIFY liveLineChanged)
  Q_PROPERTY(int cursorColumn READ cursorColumn NOTIFY liveLineChanged)
  Q_PROPERTY(int cursorPosition READ cursorPosition NOTIFY liveLineChanged)
  Q_PROPERTY(int cursorPadding READ cursorPadding NOTIFY liveLineChanged)
  // QML ListView 的数据模型，替代 TextArea 实现内存有界的虚拟化渲染
  Q_PROPERTY(TerminalLineModel *lineModel READ lineModel CONSTANT)
  Q_PROPERTY(bool logging READ logging NOTIFY loggingChanged)
  Q_PROPERTY(bool logBusy READ logBusy NOTIFY loggingChanged)
  Q_PROPERTY(QString logFilePath READ logFilePath NOTIFY loggingChanged)
  Q_PROPERTY(QString logError READ logError NOTIFY loggingChanged)

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
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    return m_useSystemCredentials;
#else
    return false;
#endif
  }
  bool useSystemCredentials() const { return m_useSystemCredentials; }
  void setUseSystemCredentials(bool enabled);
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
  qint64 firstLineId() const { return lineModel()->firstLineId(); }
  bool presentationActive() const { return m_presentationActive; }
  bool receptionPaused() const { return m_receptionPaused; }
  void setReceptionPaused(bool paused);
  QString liveLine() const { return m_committedPartial; }
  QString liveMonitorColor() const { return m_committedMonitorColor; }
  bool liveMonitorWholeLine() const { return m_committedMonitorWholeLine; }
  QVariantList liveMonitorRanges() const { return m_committedMonitorRanges; }
  int cursorColumn() const { return m_cursorColumn; }
  int cursorPosition() const { return m_cursorPosition; }
  int cursorPadding() const { return m_cursorPadding; }
  void setPresentationActive(bool active);

  Q_INVOKABLE void connectToHost();
  Q_INVOKABLE void disconnectFromHost();
  Q_INVOKABLE void sendCommand(const QString &cmd);
  Q_INVOKABLE void sendRawBytes(const QString &text);
  // 发送 Tab 补全请求：截获响应并解析后通过 tabCompletionReady 信号通知 QML
  Q_INVOKABLE void sendTabCompletion(const QString &prefix);
  Q_INVOKABLE QString getBufferText() const;
  bool logging() const { return m_logging; }
  bool logBusy() const { return m_logBusy; }
  QString logFilePath() const { return m_logFilePath; }
  QString logError() const { return m_logError; }
  Q_INVOKABLE QUrl suggestedLogFile(const QString &sessionName) const;
  Q_INVOKABLE bool logFileExists(const QUrl &file) const;
  Q_INVOKABLE bool startLogging(const QUrl &file, bool append = false,
                               int flushIntervalMs = 1000, int bufferKiB = 256);
  Q_INVOKABLE bool stopLogging();
  Q_INVOKABLE bool exportLog(const QUrl &file);
  Q_INVOKABLE QString selectedText(qint64 firstId, int firstOffset, qint64 lastId, int lastOffset) const {
    return lineModel()->textRange(firstId, firstOffset, lastId, lastOffset, liveLine());
  }
  Q_INVOKABLE void clearBuffer();
  Q_INVOKABLE QVariantList findLineIndexes(const QString &keyword,
                                           bool caseSensitive = false) const;
  // 增量搜索：只在 [fromLine, toLine] 区间内检索，避免日志滚动时全量遍历
  Q_INVOKABLE QVariantList
  findLineIndexesInRange(const QString &keyword, int fromLine, int toLine,
                         bool caseSensitive = false) const;
  Q_INVOKABLE QString lineAt(int index) const;
  Q_INVOKABLE qint64 lineIdAt(int index) const { return lineModel()->lineId(index); }
  Q_INVOKABLE int indexForLineId(qint64 id) const { return lineModel()->indexForId(id); }
  Q_INVOKABLE bool lineIsComplete(int index) const { return lineModel()->completeLineAt(index); }
  Q_INVOKABLE int lineSegmentOffset(int index) const { return lineModel()->segmentOffset(index); }
  Q_INVOKABLE QVariantList findLineIds(const QString &keyword, qint64 fromId = -1) const;
  Q_INVOKABLE void copyLineToClipboard(int index) const;
  Q_INVOKABLE void copyBufferToClipboard() const;
  Q_INVOKABLE QString clipboardText() const;
  Q_INVOKABLE bool setMonitorRules(const QVariantList &rules, bool armed);

signals:
  void loggingChanged();
  void logOperationFinished(bool success, const QString &message);
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
  void linesAboutToChange();
  void presentationActiveChanged();
  void receptionPausedChanged();
  void liveLineChanged();
  void monitorMatched(const QVariantMap &hit);
  void connectionError(const QString &error);
  void sharedBufferOverflowed();
  // Tab 补全结果：completedText 非空表示有唯一补全，空表示无匹配；
  // candidates 为多候选列表（completedText 为空时有效）
  void tabCompletionReady(const QString &completedText,
                          const QStringList &candidates);

  // 发送给工作线程的信号
  void sigStartConnect(quint64 generation, const QString &host, int port,
                       const QString &user, const QString &pass, const QString &encoding, bool useSystemCredentials);
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
  friend struct SshTerminalTestAccess;
  bool beginLogOperation(const QUrl &file);
  JsonSettings settings() const;
  void refreshCredentials();
  void dispatchCredentialRefresh();
  void identityChanged();
  void clearHostKeyRequest();
  void startConnection();
  void prioritizeInput();
  void setStatusMessage(const QString &msg);
  void setConnected(bool connected);
  void setConnectionState(ConnectionState state);
  void setLastError(const QString &error);
  void scheduleReconnect(const QString &reason);
  void resetPendingData(bool discardPartial);
  void flushFinalPendingData();
  void finishTabCapture();
  void collectTabData();

  std::shared_ptr<SshSharedBuffer> m_buffer = std::make_shared<SshSharedBuffer>();

  QThread *m_workerThread;
  SshWorker *m_worker;
  TerminalLineModel *m_lineModel;

  QString m_dataRoot;
  bool m_logging = false;
  bool m_logBusy = false;
  QString m_logFilePath;
  QString m_logError;
  bool m_persistSettings = true;
  bool m_useSystemCredentials = false;
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
  bool m_presentationActive = true;
  bool m_receptionPaused = false;
  quint64 m_receptionRevision = 0;
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

  QTimer *m_flushTimer;
  QString m_committedPartial;
  QString m_committedMonitorColor;
  bool m_committedMonitorWholeLine = true;
  QVariantList m_committedMonitorRanges;
  int m_cursorColumn = 0;
  int m_cursorPosition = 0, m_cursorPadding = 0;
  QElapsedTimer m_lastInput;
  QTimer *m_reconnectTimer = nullptr;

  // Tab 补全截获模式
  bool m_tabBusy = false; // 防重入标志
  int m_tabPhase = 0; // 0=第一次Tab  1=第二次Tab候选收集  2=第二次Tab第二次等待
  QString m_tabPrefix;          // 按 Tab 时输入框的文本
  QByteArray m_tabRawBuffer;    // Tab 模式下收到的原始字节
  QTimer *m_tabTimer = nullptr; // 超时后触发 processTabCompletion
};

#endif // SSHTERMINAL_H
