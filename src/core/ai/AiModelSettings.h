#pragma once

#include "AiCredentialStore.h"
#include <QUrl>
#include <QVariantMap>
#include <memory>

struct AiOperationResult {
  bool ok = false;
  QString code;
  QString message;
  QVariantMap toVariantMap() const {
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("code"), code},
            {QStringLiteral("message"), message}};
  }
};

class AiModelSettings {
public:
  explicit AiModelSettings(AiCredentialStore *credentials = nullptr,
                            const QString &settingsFile = {});
  QString baseUrl() const;
  QString model() const;
  bool hasApiKey() const;
  bool credentialAvailable() const;
  AiOperationResult queryCredentialPresence(bool *exists) const;
  AiOperationResult saveConfiguration(const QString &baseUrl,
                                       const QString &model,
                                       const QString &newApiKey);
  AiOperationResult clearApiKey();
  // Temporary keys are never persisted. Saved keys are bound to one endpoint.
  AiOperationResult keyForRequest(const QString &baseUrl,
                                   const QString &temporaryApiKey,
                                   QByteArray *key) const;
  static AiOperationResult normalizeBaseUrl(const QString &text, QUrl *url);
  static AiOperationResult validateModel(const QString &model);
  static AiOperationResult validateApiKey(const QByteArray &key);
  static QString profileId(const QUrl &normalizedBaseUrl);

private:
  QString m_settingsFile;
  std::unique_ptr<AiCredentialStore> m_ownedCredentials;
  AiCredentialStore *m_credentials;
};
