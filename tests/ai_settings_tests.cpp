#include "core/ai/AiModelSettings.h"

#include <QCoreApplication>
#include <QFile>
#include <QMap>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

namespace {
int failures = 0;
void expect(bool condition, const QString &message) {
  if (!condition) {
    QTextStream(stderr) << "FAIL: " << message << '\n';
    ++failures;
  }
}

class MemoryCredentials : public AiCredentialStore {
public:
  bool available = true;
  bool readFails = false;
  bool writeFails = false;
  QString removeFailsFor;
  mutable int reads = 0;
  mutable int existenceChecks = 0;
  QMap<QString, QByteArray> values;
  QStringList removedProfiles;
  bool isAvailable() const override { return available; }
  bool contains(const QString &id, bool *exists, QString *) const override {
    ++existenceChecks;
    *exists = values.contains(id);
    return !readFails;
  }
  bool read(const QString &id, QByteArray *key, QString *) const override {
    ++reads;
    *key = readFails ? QByteArray() : values.value(id);
    return !readFails;
  }
  bool write(const QString &id, const QByteArray &key, QString *) override {
    if (writeFails)
      return false;
    values.insert(id, key);
    return true;
  }
  bool remove(const QString &id, QString *) override {
    removedProfiles.append(id);
    if (id == removeFailsFor)
      return false;
    values.remove(id);
    return true;
  }
};

QString profile(const QString &url) {
  QUrl normalized;
  expect(AiModelSettings::normalizeBaseUrl(url, &normalized).ok, QStringLiteral("测试地址合法"));
  return AiModelSettings::profileId(normalized);
}

void testValidation() {
  for (const QString &url : {
           QStringLiteral("http://api.deepseek.com"),
           QStringLiteral("https://user:secret@api.deepseek.com"),
           QStringLiteral("https://@api.deepseek.com"),
           QStringLiteral("https://api.deepseek.com?key=secret"),
           QStringLiteral("https://api.deepseek.com#fragment"),
           QStringLiteral("https://api.deepseek.com/chat/completions/"),
           QStringLiteral("https://api.deepseek.com/a b"),
           QStringLiteral("https://"), QStringLiteral("https://api.deepseek.com:0")}) {
    QUrl result;
    expect(!AiModelSettings::normalizeBaseUrl(url, &result).ok, QStringLiteral("拒绝不安全/错误接入地址: ") + url);
  }
  QUrl normalized;
  expect(AiModelSettings::normalizeBaseUrl(QStringLiteral(" https://API.DEEPSEEK.COM:443/v1/// "), &normalized).ok &&
             normalized.toString() == QStringLiteral("https://api.deepseek.com/v1"),
         QStringLiteral("地址规范化保持 v1 路径"));
  expect(!AiModelSettings::validateApiKey("fake\r\nheader").ok,
         QStringLiteral("拒绝 Key 请求头注入"));
  expect(!AiModelSettings::validateModel(QStringLiteral("model\nsecret")).ok,
         QStringLiteral("拒绝非法模型标识"));
}

void testStorageAndBinding() {
  QTemporaryDir directory;
  const QString path = directory.filePath(QStringLiteral("settings.ini"));
  MemoryCredentials credentials;
  AiModelSettings settings(&credentials, path);
  expect(!settings.hasApiKey() && credentials.reads == 0,
         QStringLiteral("读取设置状态不读取完整 Key"));
  auto saved = settings.saveConfiguration(QStringLiteral("https://API.DEEPSEEK.COM/"),
                                           QStringLiteral("deepseek-flash"),
                                           QStringLiteral("fake-key-one"));
  expect(saved.ok && settings.hasApiKey(), QStringLiteral("普通配置与安全凭据保存成功"));
  const int reads = credentials.reads;
  expect(settings.hasApiKey() && credentials.reads == reads,
         QStringLiteral("hasApiKey 仅做存在查询"));
  QFile ini(path);
  expect(ini.open(QIODevice::ReadOnly), QStringLiteral("读取隔离配置"));
  const QByteArray iniBytes = ini.readAll();
  ini.close();
  expect(!iniBytes.contains("fake-key-one") && !iniBytes.contains("apiKey"),
         QStringLiteral("INI 只写地址和模型，无 Key"));
  expect(settings.baseUrl() == QStringLiteral("https://api.deepseek.com"),
         QStringLiteral("保存规范化地址"));
  expect(settings.saveConfiguration(settings.baseUrl(), QStringLiteral("deepseek-v4-pro"), {}).ok,
         QStringLiteral("同接入地址允许保留 Key 切模型"));
  expect(!settings.saveConfiguration(QStringLiteral("https://other.example/v1"),
                                      QStringLiteral("deepseek-flash"), {}).ok,
         QStringLiteral("新 endpoint 必须重新提供 Key"));
  QByteArray key;
  expect(!settings.keyForRequest(QStringLiteral("https://other.example/v1"), {}, &key).ok && key.isEmpty(),
         QStringLiteral("禁止跨 endpoint 复用已保存 Key"));
  expect(settings.keyForRequest(QStringLiteral("https://other.example/v1"),
                                 QStringLiteral("temporary-key"), &key).ok && key == "temporary-key",
         QStringLiteral("临时测试 Key 只用于明确指定的 endpoint"));
  expect(credentials.values.size() == 1 && !credentials.values.values().contains("temporary-key"),
         QStringLiteral("临时 Key 不持久化"));
  AiModelSettings reloaded(&credentials, path);
  expect(reloaded.model() == QStringLiteral("deepseek-v4-pro") && reloaded.hasApiKey(),
         QStringLiteral("重建设置实例可以恢复普通配置与凭据状态"));
  expect(reloaded.saveConfiguration(QStringLiteral("https://other.example/v1"),
                                     QStringLiteral("deepseek-flash"),
                                     QStringLiteral("fake-key-two")).ok &&
             !credentials.values.contains(profile(QStringLiteral("https://api.deepseek.com"))) &&
             credentials.values.size() == 1,
         QStringLiteral("更换 endpoint 后清理旧 profile"));
  expect(reloaded.clearApiKey().ok && !reloaded.hasApiKey(), QStringLiteral("清除系统凭据"));
  expect(reloaded.clearApiKey().ok, QStringLiteral("重复清除保持成功"));
}

void testFailures() {
  QTemporaryDir directory;
  MemoryCredentials credentials;
  AiModelSettings settings(&credentials, directory.filePath(QStringLiteral("good.ini")));
  expect(settings.saveConfiguration(QStringLiteral("https://api.deepseek.com"),
                                      QStringLiteral("deepseek-flash"), QStringLiteral("original-fake-key")).ok,
         QStringLiteral("失败用例基线配置"));
  credentials.writeFails = true;
  expect(settings.saveConfiguration(settings.baseUrl(), QStringLiteral("other-model"),
                                      QStringLiteral("new-fake-key")).code == QStringLiteral("credential_write_failed") &&
             settings.model() == QStringLiteral("deepseek-flash"),
         QStringLiteral("凭据写失败不更新普通配置"));
  credentials.writeFails = false;
  const QString originalProfile = profile(settings.baseUrl());
  credentials.removeFailsFor = originalProfile;
  expect(settings.saveConfiguration(QStringLiteral("https://other.example"),
                                      QStringLiteral("other-model"), QStringLiteral("new-fake-key")).code ==
             QStringLiteral("credential_cleanup_failed") &&
             settings.baseUrl() == QStringLiteral("https://api.deepseek.com") &&
             credentials.values.size() == 1 && credentials.values.value(originalProfile) == "original-fake-key",
         QStringLiteral("旧凭据清理失败恢复原配置并撤回新凭据"));
  expect(!settings.clearApiKey().ok && settings.hasApiKey(),
         QStringLiteral("清除失败不伪装已删除"));
  credentials.removeFailsFor.clear();
  credentials.readFails = true;
  QByteArray key = "must-clear";
  expect(settings.keyForRequest(settings.baseUrl(), {}, &key).code ==
             QStringLiteral("credential_unavailable") && key.isEmpty(),
         QStringLiteral("读凭据失败时输出不留旧 Key"));
  credentials.readFails = false;
  credentials.available = false;
  expect(!settings.hasApiKey() &&
             settings.saveConfiguration(settings.baseUrl(), settings.model(), QStringLiteral("fake-key")).code ==
                 QStringLiteral("credential_unavailable"),
         QStringLiteral("不支持的平台不回退明文"));

  MemoryCredentials rollbackCredentials;
  QFile blocker(directory.filePath(QStringLiteral("not-a-directory")));
  expect(blocker.open(QIODevice::WriteOnly), QStringLiteral("创建不可写配置路径"));
  blocker.write("block");
  blocker.close();
  AiModelSettings broken(&rollbackCredentials, blocker.fileName() + QStringLiteral("/settings.ini"));
  expect(broken.saveConfiguration(QStringLiteral("https://api.deepseek.com"),
                                   QStringLiteral("deepseek-flash"), QStringLiteral("rollback-fake-key")).code ==
             QStringLiteral("settings_write_failed") && rollbackCredentials.values.isEmpty(),
         QStringLiteral("INI 保存失败回滚新系统凭据"));
  const auto target = profile(QStringLiteral("https://api.deepseek.com"));
  rollbackCredentials.values.insert(target, "previous-fake-key");
  expect(broken.saveConfiguration(QStringLiteral("https://api.deepseek.com"),
                                   QStringLiteral("deepseek-flash"), QStringLiteral("replacement-fake-key")).code ==
             QStringLiteral("settings_write_failed") &&
             rollbackCredentials.values.value(target) == "previous-fake-key",
         QStringLiteral("INI 保存失败恢复先前凭据而不是删除它"));
}

void testFirstSaveDoesNotOwnFallbackCredentials() {
  QTemporaryDir directory;
  const QString path = directory.filePath(QStringLiteral("first-save.ini"));
  {
    QSettings ini(path, QSettings::IniFormat);
    ini.setValue(QStringLiteral("ui/next/appearance/dark"), true);
    ini.sync();
  }
  MemoryCredentials credentials;
  const QString defaultProfile = profile(QStringLiteral("https://api.deepseek.com"));
  credentials.values.insert(defaultProfile, "other-installation-fake-key");
  AiModelSettings settings(&credentials, path);
  expect(settings.baseUrl() == QStringLiteral("https://api.deepseek.com"),
         QStringLiteral("首次配置仍显示默认地址"));
  expect(settings.saveConfiguration(QStringLiteral("https://isolated.example/v1"),
             QStringLiteral("test-model"), QStringLiteral("fresh-profile-fake-key")).ok,
         QStringLiteral("只有ui配置时首次保存非默认endpoint成功"));
  expect(credentials.values.value(defaultProfile) == "other-installation-fake-key" &&
             !credentials.removedProfiles.contains(defaultProfile) && credentials.values.size() == 2,
         QStringLiteral("首次保存不得把fallback地址的其他现有凭据当成旧配置删除"));

  AiModelSettings empty(&credentials, directory.filePath(QStringLiteral("empty.ini")));
  expect(empty.saveConfiguration(QStringLiteral("https://api.deepseek.com"),
             QStringLiteral("test-model"), {}).code == QStringLiteral("configuration_missing"),
         QStringLiteral("无接入记录的首次保存不能隐式借用已有默认profile Key"));
  expect(credentials.values.value(defaultProfile) == "other-installation-fake-key",
         QStringLiteral("首次保存拒绝后保留所有既存凭据"));
}
} // namespace

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  testValidation();
  testStorageAndBinding();
  testFailures();
  testFirstSaveDoesNotOwnFallbackCredentials();
  if (failures == 0)
    QTextStream(stdout) << "AI settings tests passed (fake credential store only)\n";
  return failures ? 1 : 0;
}
