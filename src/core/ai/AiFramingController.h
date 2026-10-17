#pragma once

#include "AiModelSettings.h"
#include "DeepSeekClient.h"
#include "698_prot/Ai698Tools.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QObject>
#include <functional>

class AiCredentialWorker;
class QThread;

class AiFramingController : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString promptFilePath READ promptFilePath CONSTANT)
  Q_PROPERTY(QUrl promptDirectoryUrl READ promptDirectoryUrl CONSTANT)
  Q_PROPERTY(QString baseUrl READ baseUrl NOTIFY settingsChanged)
  Q_PROPERTY(QString model READ model NOTIFY settingsChanged)
  Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY settingsChanged)
  Q_PROPERTY(bool credentialAvailable READ credentialAvailable NOTIFY settingsChanged)
  Q_PROPERTY(bool settingsBusy READ settingsBusy NOTIFY settingsChanged)
  Q_PROPERTY(QString credentialStatus READ credentialStatus NOTIFY settingsChanged)
  Q_PROPERTY(QString credentialMessage READ credentialMessage NOTIFY settingsChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
  Q_PROPERTY(QString state READ state NOTIFY stateChanged)
  Q_PROPERTY(QString requestId READ requestId NOTIFY stateChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
  Q_PROPERTY(QString errorCode READ errorCode NOTIFY stateChanged)
  Q_PROPERTY(QString clarification READ clarification NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap result READ result NOTIFY stateChanged)
  Q_PROPERTY(bool connectionBusy READ connectionBusy NOTIFY connectionChanged)
  Q_PROPERTY(QString connectionRequestId READ connectionRequestId NOTIFY connectionChanged)
  Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionChanged)
  Q_PROPERTY(QString connectionMessage READ connectionMessage NOTIFY connectionChanged)

public:
  static constexpr int HttpLimit = 10;
  static constexpr int ToolLimit = 30;
  static constexpr int ClarificationLimit = 4;
  static constexpr int RuntimeLimitMs = 180000;
  static constexpr int CredentialWaitMs = 30000;

  // nextPiid is owned by LogBackend; test dependencies are C++ only.
  explicit AiFramingController(std::function<int()> nextPiid,
                                QObject *parent = nullptr,
                                QNetworkAccessManager *manager = nullptr,
                                std::shared_ptr<AiCredentialStore> credentials = {},
                                const QString &settingsFile = {});
  ~AiFramingController() override;

  QString promptFilePath() const { return m_promptPath; }
  QUrl promptDirectoryUrl() const;
  QString baseUrl() const { return m_baseUrl; }
  QString model() const { return m_model; }
  bool hasApiKey() const { return m_hasApiKey; }
  bool credentialAvailable() const { return m_credentialAvailable; }
  bool settingsBusy() const { return m_settingsBusy; }
  QString credentialStatus() const { return m_credentialStatus; }
  QString credentialMessage() const { return m_credentialMessage; }
  bool busy() const;
  QString state() const { return m_state; }
  QString requestId() const { return m_context.requestId; }
  QString statusText() const { return m_statusText; }
  QString errorCode() const { return m_errorCode; }
  QString clarification() const { return m_clarification; }
  QVariantMap result() const { return m_result; }
  bool connectionBusy() const { return m_connectionBusy; }
  QString connectionRequestId() const { return m_connectionId; }
  QString connectionStatus() const { return m_connectionStatus; }
  QString connectionMessage() const { return m_connectionMessage; }

  Q_INVOKABLE QVariantMap saveConfiguration(const QString &baseUrl,
                                           const QString &model,
                                           const QString &newApiKey);
  Q_INVOKABLE QVariantMap clearApiKey();
  Q_INVOKABLE QString testConnection(const QString &baseUrl,
                                     const QString &model,
                                     const QString &temporaryApiKey);
  Q_INVOKABLE void cancelConnectionTest();
  Q_INVOKABLE QString submit(const QString &text, const QVariantMap &terminal);
  Q_INVOKABLE bool answerClarification(const QString &requestId,
                                       const QString &answer);
  Q_INVOKABLE void cancel();

signals:
  void settingsChanged();
  void settingsOperationFinished(QString operationId, QVariantMap reply);
  void stateChanged();
  void connectionChanged();

private:
  bool active() const;
  bool checkContext();
  void setState(const QString &state, const QString &message,
                const QString &error = {});
  void failGeneration(const QString &code, const QString &message);
  void clearConversation();
  void pauseRuntime();
  void resumeRuntime();
  void sendRound();
  void beginGeneration(const QByteArray &key);
  QString startCredentialOperation(const QString &kind, const QVariantMap &arguments,
                                  const QString &logicalId = {});
  void credentialWaitExpired();
  void credentialCompleted(const QString &operationId, const QString &kind,
                           const QVariantMap &reply, const QVariantMap &snapshot,
                           const QByteArray &key);
  void handleCompleted(const QString &id, const QJsonObject &message,
                       const QString &finishReason);
  void handleFailed(const QString &id, const QString &code,
                    const QString &message);
  bool validateTerminal(const QVariantMap &terminal, QJsonObject *snapshot) const;

  QPointer<AiCredentialWorker> m_credentialWorker;
  QPointer<QThread> m_credentialThread;
  QString m_baseUrl;
  QString m_model;
  bool m_hasApiKey = false;
  bool m_credentialAvailable = false;
  bool m_settingsBusy = false;
  QString m_credentialStatus = QStringLiteral("checking");
  QString m_credentialMessage;
  QString m_credentialOperationId;
  QString m_credentialKind;
  QString m_credentialLogicalId;
  QString m_pendingText;
  QString m_promptPath;
  QString m_promptSnapshot;
  DeepSeekClient m_client;
  Ai698Tools m_tools;
  std::function<int()> m_nextPiid;
  AiFrameContext m_context;
  QUrl m_activeUrl;
  QString m_activeModel;
  QByteArray m_activeKey;
  QJsonArray m_messages;
  QJsonArray m_toolDefinitions;
  QSet<QString> m_toolNames;
  QString m_state = QStringLiteral("idle");
  QString m_statusText;
  QString m_errorCode;
  QString m_clarification;
  QVariantMap m_result;
  int m_httpCount = 0;
  int m_toolCount = 0;
  int m_clarificationCount = 0;
  QString m_lastToolSummary;
  int m_remainingMs = RuntimeLimitMs;
  QTimer m_runtimeTimer;
  QTimer m_credentialTimer;
  QElapsedTimer m_runningTime;
  bool m_connectionBusy = false;
  QString m_connectionId;
  QString m_connectionStatus = QStringLiteral("idle");
  QString m_connectionMessage;
};
