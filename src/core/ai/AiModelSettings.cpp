#include "AiModelSettings.h"

#include "../../common/AppPaths.h"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSettings>

namespace {
const QString kUrlKey = QStringLiteral("aiFraming/baseUrl");
const QString kModelKey = QStringLiteral("aiFraming/model");
AiOperationResult success() { return {true, {}, {}}; }
AiOperationResult failure(const char *code, const QString &message) {
  return {false, QLatin1String(code), message};
}
} // namespace

AiModelSettings::AiModelSettings(AiCredentialStore *credentials,
                                 const QString &settingsFile)
    : m_settingsFile(settingsFile.isEmpty()
                         ? QDir(AppPaths::writableDataRoot())
                               .filePath(QStringLiteral("LogAnalyQt.ini"))
                         : settingsFile),
      m_ownedCredentials(credentials ? nullptr : new AiCredentialStore),
      m_credentials(credentials ? credentials : m_ownedCredentials.get()) {}

QString AiModelSettings::baseUrl() const {
  QSettings settings(m_settingsFile, QSettings::IniFormat);
  return settings.value(kUrlKey, QStringLiteral("https://api.deepseek.com"))
      .toString();
}

QString AiModelSettings::model() const {
  QSettings settings(m_settingsFile, QSettings::IniFormat);
  return settings.value(kModelKey, QStringLiteral("deepseek-flash")).toString();
}

bool AiModelSettings::credentialAvailable() const {
  return m_credentials->isAvailable();
}

bool AiModelSettings::hasApiKey() const {
  bool exists = false;
  return queryCredentialPresence(&exists).ok && exists;
}

AiOperationResult AiModelSettings::queryCredentialPresence(bool *exists) const {
  if (!exists)
    return failure("invalid_input", QStringLiteral("凭据状态输出无效"));
  *exists = false;
  QUrl url;
  if (!credentialAvailable() || !normalizeBaseUrl(baseUrl(), &url).ok)
    return failure("credential_unavailable", QStringLiteral("无法查询系统安全凭据状态"));
  if (!m_credentials->contains(profileId(url), exists))
    return failure("credential_unavailable", QStringLiteral("系统安全凭据状态查询失败，请检查系统凭据权限后重试"));
  return success();
}

AiOperationResult AiModelSettings::normalizeBaseUrl(const QString &text,
                                                    QUrl *output) {
  if (!output)
    return failure("invalid_input", QStringLiteral("接入地址输出无效"));
  const QString input = text.trimmed();
  static const QRegularExpression whitespace(QStringLiteral("[\\s\\x00-\\x1f\\x7f]"));
  QUrl url = QUrl::fromEncoded(input.toUtf8(), QUrl::StrictMode);
  if (input.isEmpty() || input.size() > 2048 ||
      whitespace.match(input).hasMatch() || !url.isValid() ||
      url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0 ||
      url.host().isEmpty() || url.authority().contains(QLatin1Char('@')) || url.hasQuery() ||
      url.hasFragment() || url.port(443) <= 0) {
    return failure("invalid_input", QStringLiteral("请输入不含凭据、查询或片段的有效 HTTPS 接入根地址"));
  }
  QString path = url.path();
  while (path.endsWith(QLatin1Char('/')))
    path.chop(1);
  if (path.endsWith(QStringLiteral("/chat/completions"), Qt::CaseInsensitive))
    return failure("invalid_input", QStringLiteral("请输入服务根地址，不要包含 /chat/completions"));
  url.setPath(path);
  url.setScheme(QStringLiteral("https"));
  url.setHost(url.host().toLower());
  if (url.port() == 443)
    url.setPort(-1);
  *output = url;
  return success();
}

AiOperationResult AiModelSettings::validateModel(const QString &model) {
  static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:/-]{0,127}$"));
  return pattern.match(model).hasMatch()
             ? success()
             : failure("invalid_input", QStringLiteral("模型 ID 为空或格式无效"));
}

AiOperationResult AiModelSettings::validateApiKey(const QByteArray &key) {
  if (key.isEmpty() || key.size() > 2048)
    return failure("configuration_missing", QStringLiteral("请输入有效的 API Key"));
  for (const char character : key) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 0x21 || byte > 0x7e)
      return failure("invalid_input", QStringLiteral("API Key 不能包含空白或控制字符"));
  }
  return success();
}

QString AiModelSettings::profileId(const QUrl &normalizedBaseUrl) {
  return QString::fromLatin1(QCryptographicHash::hash(
                                 normalizedBaseUrl.toEncoded(),
                                 QCryptographicHash::Sha256)
                                 .toHex());
}

AiOperationResult AiModelSettings::keyForRequest(
    const QString &requestedBaseUrl, const QString &temporaryApiKey,
    QByteArray *key) const {
  if (!key)
    return failure("invalid_input", QStringLiteral("凭据输出无效"));
  key->clear();
  QUrl requested;
  auto checked = normalizeBaseUrl(requestedBaseUrl, &requested);
  if (!checked.ok)
    return checked;
  if (!temporaryApiKey.isEmpty()) {
    const QByteArray candidate = temporaryApiKey.toUtf8();
    checked = validateApiKey(candidate);
    if (checked.ok)
      *key = candidate;
    return checked;
  }
  QUrl saved;
  if (!normalizeBaseUrl(baseUrl(), &saved).ok || requested != saved)
    return failure("configuration_missing", QStringLiteral("新接入地址需要重新输入 API Key"));
  if (!credentialAvailable() ||
      !m_credentials->read(profileId(saved), key)) {
    key->clear();
    return failure("credential_unavailable", QStringLiteral("无法读取系统安全凭据，请检查系统存储权限"));
  }
  checked = validateApiKey(*key);
  if (!checked.ok)
    key->clear();
  return checked;
}

AiOperationResult AiModelSettings::saveConfiguration(
    const QString &newBaseUrl, const QString &newModel, const QString &newApiKey) {
  QUrl target;
  auto checked = normalizeBaseUrl(newBaseUrl, &target);
  if (!checked.ok)
    return checked;
  const QString normalizedModel = newModel.trimmed();
  checked = validateModel(normalizedModel);
  if (!checked.ok)
    return checked;
  if (!credentialAvailable())
    return failure("credential_unavailable", QStringLiteral("当前平台没有可用的安全凭据存储"));
  QSettings settings(m_settingsFile, QSettings::IniFormat);
  const QVariant oldUrl = settings.value(kUrlKey);
  const QVariant oldModel = settings.value(kModelKey);
  QUrl previous;
  // A displayed fallback URL does not prove this INI owns its credential.
  const bool hadPrevious = oldUrl.isValid() &&
                           normalizeBaseUrl(oldUrl.toString(), &previous).ok;
  const bool endpointChanged = !hadPrevious || previous != target;
  if (newApiKey.isEmpty() && endpointChanged)
    return failure("configuration_missing", QStringLiteral("新接入地址需要重新输入 API Key"));
  const QString targetProfile = profileId(target);
  QByteArray priorKey;
  if (!m_credentials->read(targetProfile, &priorKey))
    return failure("credential_unavailable", QStringLiteral("无法读取系统安全凭据"));
  QByteArray nextKey = newApiKey.isEmpty() ? priorKey : newApiKey.toUtf8();
  checked = validateApiKey(nextKey);
  if (!checked.ok)
    return checked;
  const bool replacingKey = !newApiKey.isEmpty();
  if (replacingKey && !m_credentials->write(targetProfile, nextKey))
    return failure("credential_write_failed", QStringLiteral("API Key 保存失败，配置未更新"));

  auto restoreSettings = [&]() {
    for (const auto &entry : {
             qMakePair(kUrlKey, oldUrl), qMakePair(kModelKey, oldModel)}) {
      if (entry.second.isValid())
        settings.setValue(entry.first, entry.second);
      else
        settings.remove(entry.first);
    }
    settings.sync();
    return settings.status() == QSettings::NoError;
  };
  auto restoreKey = [&]() {
    return !replacingKey ||
           (priorKey.isEmpty() ? m_credentials->remove(targetProfile)
                               : m_credentials->write(targetProfile, priorKey));
  };
  settings.setValue(kUrlKey, QString::fromUtf8(target.toEncoded()));
  settings.setValue(kModelKey, normalizedModel);
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    restoreSettings();
    const bool restored = restoreKey();
    return failure("settings_write_failed",
                   restored ? QStringLiteral("接入配置保存失败，请检查配置目录写入权限")
                            : QStringLiteral("接入配置保存失败，凭据恢复也失败，请重新保存或清除凭据"));
  }
  if (endpointChanged && hadPrevious &&
      !m_credentials->remove(profileId(previous))) {
    const bool settingsRestored = restoreSettings();
    const bool restored = restoreKey() && settingsRestored;
    return failure("credential_cleanup_failed",
                   restored ? QStringLiteral("旧接入凭据清理失败，已恢复原配置")
                            : QStringLiteral("旧接入凭据清理失败，原配置恢复未完成，请重新保存并检查系统凭据存储"));
  }
  return success();
}

AiOperationResult AiModelSettings::clearApiKey() {
  QUrl current;
  auto checked = normalizeBaseUrl(baseUrl(), &current);
  if (!checked.ok)
    return checked;
  if (!credentialAvailable())
    return failure("credential_unavailable", QStringLiteral("当前平台没有可用的安全凭据存储"));
  if (!m_credentials->remove(profileId(current)))
    return failure("credential_write_failed", QStringLiteral("API Key 清除失败，请检查系统凭据存储"));
  return success();
}
