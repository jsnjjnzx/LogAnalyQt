#pragma once

#include "AiModelSettings.h"
#include <QObject>
#include <memory>

// One controller's credential transactions run serially on its worker thread.
class AiCredentialWorker : public QObject {
  Q_OBJECT
public:
  AiCredentialWorker(std::shared_ptr<AiCredentialStore> credentials,
                     const QString &settingsFile);
  void run(const QString &operationId, const QString &kind,
           const QVariantMap &arguments);
signals:
  void completed(QString operationId, QString kind, QVariantMap reply,
                 QVariantMap snapshot, QByteArray key);
private:
  QVariantMap snapshot() const;
  std::shared_ptr<AiCredentialStore> m_credentials;
  AiModelSettings m_settings;
};
