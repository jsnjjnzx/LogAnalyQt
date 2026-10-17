#include "AiCredentialWorker.h"

AiCredentialWorker::AiCredentialWorker(
    std::shared_ptr<AiCredentialStore> credentials, const QString &settingsFile)
    : m_credentials(credentials ? std::move(credentials)
                                : std::make_shared<AiCredentialStore>()),
      m_settings(m_credentials.get(), settingsFile) {}

QVariantMap AiCredentialWorker::snapshot() const {
  bool exists = false;
  const auto checked = m_settings.queryCredentialPresence(&exists);
  return {{"baseUrl", m_settings.baseUrl()}, {"model", m_settings.model()},
          {"credentialAvailable", m_settings.credentialAvailable()},
          {"credentialKnown", checked.ok}, {"hasApiKey", checked.ok && exists},
          {"credentialMessage", checked.message}};
}

void AiCredentialWorker::run(const QString &id, const QString &kind,
                              const QVariantMap &arguments) {
  AiOperationResult result{true, {}, {}};
  QByteArray key;
  if (kind == "save")
    result = m_settings.saveConfiguration(arguments["baseUrl"].toString(),
        arguments["model"].toString(), arguments["key"].toString());
  else if (kind == "clear")
    result = m_settings.clearApiKey();
  else if (kind == "read_generate" || kind == "read_probe")
    result = m_settings.keyForRequest(arguments["baseUrl"].toString(),
        arguments["key"].toString(), &key);
  else if (kind != "refresh")
    result = {false, QStringLiteral("invalid_input"), QStringLiteral("未知凭据操作")};
  const auto current = snapshot();
  if (kind == "refresh" && !current["credentialKnown"].toBool())
    result = {false, QStringLiteral("credential_unavailable"), current["credentialMessage"].toString()};
  emit completed(id, kind, result.toVariantMap(), current, key);
}
