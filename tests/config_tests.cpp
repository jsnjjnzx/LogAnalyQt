#include "core/config/ConfigManager.h"
#include "core/config/CredentialAccess.h"
#include "common/AppSettings.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <QDirIterator>
#include "core/mqtt/MqttTypes.h"
#include <stdexcept>

namespace {
int assertions = 0;
void check(bool ok, const char *message) { ++assertions; if (!ok) throw std::runtime_error(message); }
void write(const QString &path, const QByteArray &bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
  check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write fixture");
}
QByteArray read(const QString &path) { QFile file(path); check(file.open(QIODevice::ReadOnly), "read fixture"); return file.readAll(); }
class Credentials : public SecureCredentialStore {
public:
  Credentials() : SecureCredentialStore(Domain::Ssh) {}
  bool available = true, denyWrite = false, corrupt = false;
  int reads = 0, denyReadAt = -1;
  QMap<QString, QByteArray> values;
  std::function<void()> onWrite;
  bool isAvailable() const override { return available; }
  bool read(const QString &id, QByteArray *bytes, QString *error) const override {
    auto *self = const_cast<Credentials *>(this); ++self->reads;
    if (reads == denyReadAt) return ConfigJson::fail(error, "synthetic read failure");
    *bytes = values.value(id); return true;
  }
  bool write(const QString &id, const QByteArray &bytes, QString *error) override {
    if (denyWrite) return ConfigJson::fail(error, "synthetic write failure");
    values[id] = corrupt ? QByteArray("corrupt") : bytes; if (onWrite) onWrite(); return true;
  }
  bool remove(const QString &id, QString *) override { values.remove(id); return true; }
};
QByteArray iniSecret() { return "[ssh]\nhost=old.invalid\nport=2222\nusername=user\npassword=synthetic-secret\n"; }
void storesAndMissing() {
  QTemporaryDir dir;
  JsonSettings a(dir.filePath("settings.json")), b(dir.filePath("settings.json"));
  a.setValue("ui/encoding/current", "GBK"); b.setValue("aiFraming/model", "test"); a.sync(); b.sync();
  check(a.value("aiFraming/model").toString() == "test" && b.value("ui/encoding/current").toString() == "GBK", "independent writers preserve other modules");
  check(ConfigMigration::inspect(dir.path()).sources.isEmpty(), "no old files means no migration");
  QTemporaryDir empty;
  check(!ConfigMigration::inspect(empty.path()).pending() && !QFile::exists(empty.filePath("settings.json")), "inspection is read-only on fresh installation");
  write(dir.filePath("LogAnalyQt.ini"), "[General]\nprotocolType=3\n[ui]\nnext\\appearance\\reducedTransparency=false\n");
  const auto before = read(dir.filePath("LogAnalyQt.ini"));
  auto plan = ConfigMigration::inspect(dir.path());
  check(plan.error.isEmpty() && plan.sources.size() == 1, "only existing INI migrates");
  check(read(dir.filePath("LogAnalyQt.ini")) == before && QFile::exists(dir.filePath("settings.json")), "inspection and cancellation do not alter data");
  QString error; check(ConfigMigration::execute(plan, {}, &error), "partial old version INI migration succeeds");
  check(!QFile::exists(dir.filePath("LogAnalyQt.ini")), "verified old file removed");
  JsonSettings saved(dir.filePath("settings.json"));
  check(saved.value("protocolType").toInt() == 3 && saved.value("ui/next/appearance/reducedTransparency").typeId() == QMetaType::Bool && !saved.value("ui/next/appearance/reducedTransparency").toBool(), "types and false boolean preserved");
  check(!ConfigMigration::inspect(dir.path()).pending(), "restart never repeats migration");
  write(dir.filePath("settings.json"), "broken");
  const auto corrupt = read(dir.filePath("settings.json"));
  JsonSettings invalid(dir.filePath("settings.json")); invalid.setValue("ui/test", true); invalid.sync();
  check(invalid.status() != QSettings::NoError && read(dir.filePath("settings.json")) == corrupt, "broken JSON cannot be overwritten with defaults");
}
void credentialGates() {
  for (int mode = 0; mode < 8; ++mode) {
    QTemporaryDir dir; const auto path = dir.filePath("LogAnalyQt.ini"); write(path, iniSecret());
    auto plan = ConfigMigration::inspect(dir.path()); check(plan.error.isEmpty(), "secret fixture parsed");
    auto store = std::make_shared<Credentials>();
    if (mode == 1) store->denyWrite = true;
    if (mode == 2) store->values[plan.secrets.first().profile] = "existing-other-secret";
    if (mode == 3) store->corrupt = true;
    if (mode == 4) store->onWrite = [&] { write(path, iniSecret() + "encoding=GBK\n"); };
    if (mode == 5) store->denyReadAt = 4;
    if (mode == 6) store->onWrite = [&] { QDir().mkdir(dir.filePath("settings.json")); };
    if (mode == 7) store->onWrite = [&] { ConfigJson::write(dir.filePath("settings.json"), {{"schemaVersion", 1}, {"parser", QJsonObject{{"protocolType", 3}}}}); };
    QString error;
    const bool success = ConfigMigration::execute(plan, {}, &error, [store](auto) { return store; });
    check(success == (mode == 0), "credential failure or source change prevents completion");
    check(QFile::exists(path) == (mode != 0), "only verified configuration AND credentials allow deletion");
    if (mode == 0) {
      check(store->values.value(plan.secrets.first().profile) == "synthetic-secret", "correct identity credential saved");
      check(!read(dir.filePath("settings.json")).contains("synthetic-secret"), "JSON has no plaintext secret");
      QDirIterator it(dir.path(), QDir::Files, QDirIterator::Subdirectories);
      while (it.hasNext()) check(!read(it.next()).contains("synthetic-secret"), "migration snapshots and journals omit dedicated secret");
    }
    if (mode == 2) check(store->values.value(plan.secrets.first().profile) == "existing-other-secret", "existing credential never overwritten");
    if (mode == 7) check(JsonSettings(dir.filePath("settings.json")).value("protocolType").toInt() == 3, "configuration edited during native-like credential wait is preserved");
  }
  QTemporaryDir missing;
  write(missing.filePath("LogAnalyQt.ini"), "[ssh]\npassword=synthetic-secret\n");
  auto plan = ConfigMigration::inspect(missing.path());
  check(!plan.error.isEmpty() && QFile::exists(missing.filePath("LogAnalyQt.ini")), "missing credential identity is never guessed");
}
void protectionAndConflicts() {
  QTemporaryDir dir;
  const auto protectedRoot = dir.filePath("build/old");
  write(protectedRoot + "/LogAnalyQt.ini", "[General]\nprotocolType=4\n");
  auto automatic = ConfigMigration::inspect(dir.path(), {protectedRoot});
  check(!automatic.pending(), "automatic scan excludes build directories");
  auto manual = ConfigMigration::inspect(dir.path(), {protectedRoot}, false);
  QString error; check(ConfigMigration::execute(manual, {}, &error), "explicit read-only import from build fixture succeeds");
  check(QFile::exists(protectedRoot + "/LogAnalyQt.ini"), "manual import also preserves protected source");
  write(dir.filePath("LogAnalyQt.ini"), "[General]\nprotocolType=2\n");
  auto conflict = ConfigMigration::inspect(dir.path()); check(!conflict.conflicts.isEmpty(), "different current and old values are conflicts");
  check(!ConfigMigration::execute(conflict, {}, &error) && QFile::exists(dir.filePath("LogAnalyQt.ini")), "unresolved conflict does not delete");
  check(ConfigMigration::execute(conflict, {{"parser/protocolType", "current"}}, &error), "explicit current selection allowed");
  check(JsonSettings(dir.filePath("settings.json")).value("protocolType").toInt() == 4, "current choice respected");
  QTemporaryDir changed;
  write(changed.filePath("LogAnalyQt.ini"), "[General]\nprotocolType=1\n");
  auto plan = ConfigMigration::inspect(changed.path());
  write(changed.filePath("LogAnalyQt.ini"), "[General]\nprotocolType=2\n");
  check(!ConfigMigration::execute(plan, {}, &error) && !QFile::exists(changed.filePath("settings.json")), "changed source requires new confirmation before writing");
  QTemporaryDir unknown;
  write(unknown.filePath("LogAnalyQt.ini"), "[unknown]\ncustom=data\n");
  check(!ConfigMigration::inspect(unknown.path()).error.isEmpty(), "unknown fields prevent destructive cleanup");
  // QFile::link creates a .lnk shortcut on Windows, not the Unix symlink this fixture exercises.
#ifdef Q_OS_UNIX
  QTemporaryDir links;
  write(links.filePath("outside.ini"), "[General]\nprotocolType=1\n");
  check(QFile::link(links.filePath("outside.ini"), links.filePath("LogAnalyQt.ini")), "symlink fixture");
  check(!ConfigMigration::inspect(links.path()).error.isEmpty(), "symlink input refused");
#endif
}
void mqttAndPresets() {
  QTemporaryDir dir;
  QJsonObject remote = QJsonObject::fromVariantMap(Mqtt::connectionDefaults());
  remote.insert("password", "mqtt-synthetic"); remote.insert("subscriptions", QJsonArray{QJsonObject{{"topic", "test/#"}, {"qos", 1}, {"enabled", true}, {"visible", false}, {"color", "#60a5fa"}}});
  write(dir.filePath("mqtt/connections.json"), QJsonDocument(QJsonObject{{"schema", 1}, {"connections", QJsonArray{remote}}}).toJson());
  write(dir.filePath("protocol_xml/preset_messages.xml"), "<presets version=\"v1.2.3\" date=\"2026-10-18\"><message protocol=\"698\" summary=\"test\"><content>6816</content></message></presets>");
  auto plan = ConfigMigration::inspect(dir.path()); check(plan.error.isEmpty() && plan.sources.size() == 2, "MQTT and presets migrate without INI or common file");
  auto credentials = std::make_shared<Credentials>(); QString error;
  const bool migrated = ConfigMigration::execute(plan, {}, &error, [credentials](auto) { return credentials; });
  if (!migrated) QTextStream(stderr) << error << "\n";
  check(migrated, "mixed available subset migrates");
  QJsonObject saved; check(ConfigJson::read(dir.filePath("settings.json"), &saved), "read unified fixture");
  check(saved.value("messages").toObject().value("presets").toArray().size() == 1, "presets preserved");
  check(saved.value("mqtt").toObject().value("connections").toArray().first().toObject().value("subscriptions").toArray().first().toObject().value("visible") == false, "subscription appearance preserved");
  check(!saved.value("mqtt").toObject().contains("commonSubscriptions"), "missing common library is not populated by guessing");
  check(!read(dir.filePath("settings.json")).contains("mqtt-synthetic"), "MQTT dedicated password absent from JSON");
  auto profile = saved.value("mqtt").toObject().value("connections").toArray().first().toObject().toVariantMap();
  const auto bound = profile.value("credentialProfile").toString(); profile.insert("host", "other.invalid");
  check(!CredentialAccess::loadMqtt(&profile, &error, credentials), "changed MQTT target cannot read old target credential");
  auto changed = QJsonObject::fromVariantMap(profile);
  check(CredentialAccess::sealMqtt(&changed, &error, credentials) && !changed.contains("credentialProfile") && credentials->values.contains(bound), "saving a changed target removes only its stale reference and retains old credential");
  QTemporaryDir templates;
  write(templates.filePath("preset/smart_framing/example.json"), "{\"protocol\":\"698\",\"tree\":{}}");
  plan = ConfigMigration::inspect(templates.path());
  check(ConfigMigration::execute(plan, {}, &error), "template-only old installation migrates");
  check(JsonSettings(templates.filePath("settings.json")).value("framingTemplates/example").toMap().value("protocol") == "698", "template restored structurally");
  QTemporaryDir oldFolder; QTemporaryDir target;
  QDir().mkpath(oldFolder.filePath("mqtt"));
  write(oldFolder.filePath("mqtt/common-subscriptions.json"), "{\"schema\":1,\"subscriptions\":[]}");
  ConfigManager folderImport(target.path());
  check(folderImport.previewLegacy(QUrl::fromLocalFile(oldFolder.path())).value("ok").toBool(), "folder import accepts MQTT-only directory without INI");
  check(folderImport.stageLegacy({}).value("ok").toBool() && folderImport.applyPending(&error), "confirmed subset folder import applies on restart");
  check(QFile::exists(oldFolder.filePath("mqtt/common-subscriptions.json")), "manual folder original remains untouched");
}
void backups() {
  QTemporaryDir dir; QString error;
  ConfigJson::write(dir.filePath("settings.json"), {{"schemaVersion", 1}, {"parser", QJsonObject{{"protocolType", 2}}}}, &error);
  write(dir.filePath("prompts/ai_698_framing.md"), "synthetic prompt\n");
  ConfigManager manager(dir.path());
  const auto url = QUrl::fromLocalFile(dir.filePath("backup.laqbackup"));
  check(!manager.exportBackup(QUrl::fromLocalFile(dir.filePath("settings.json")), false, false, false).value("ok").toBool(), "export cannot overwrite active configuration");
  check(manager.exportBackup(url, true, true, false).value("ok").toBool(), "complete backup exported");
  QJsonObject package; check(ConfigManager::decodeBackup(read(url.toLocalFile()), &package, &error), "complete backup validated");
  auto corrupt = package; corrupt.insert("configSha256", "wrong");
  check(!ConfigManager::decodeBackup(QJsonDocument(corrupt).toJson(), &package, &error), "checksum failure refused");
  QJsonObject bad{{"path", "../outside"}, {"sha256", "wrong"}, {"data", ""}};
  corrupt.insert("files", QJsonArray{bad});
  check(!ConfigManager::decodeBackup(QJsonDocument(corrupt).toJson(), &package, &error), "path traversal refused");
  JsonSettings settings(dir.filePath("settings.json")); settings.setValue("protocolType", 5); settings.sync();
  check(manager.previewRestore(url).value("ok").toBool(), "restore preview");
  check(manager.stageRestore({"parser"}, true, false).value("ok").toBool(), "confirmed restore staged");
  check(settings.value("protocolType").toInt() == 5, "stage does not change running configuration");
  check(manager.applyPending(&error), "next startup applies staged restore");
  check(settings.value("protocolType").toInt() == 2 && !QFile::exists(dir.filePath("restore-pending.json")), "restore applied and request removed");
  check(read(dir.filePath("prompts/ai_698_framing.md")) == "synthetic prompt\n", "resource content restored");
}
void interruptedRestore() {
  for (bool externalEdit : {false, true}) {
    QTemporaryDir dir; QString error;
    const QJsonObject baseline{{"schemaVersion", 1}, {"parser", QJsonObject{{"protocolType", 5}}}};
    const QJsonObject desired{{"schemaVersion", 1}, {"parser", QJsonObject{{"protocolType", 2}}}};
    const auto snapshot = dir.filePath("restore-backups/test-run");
    ConfigJson::write(dir.filePath("settings.json"), baseline, &error);
    ConfigJson::write(snapshot + "/settings.json", baseline, &error);
    write(snapshot + "/prompts/ai_698_framing.md", "old prompt");
    const auto resource = QJsonObject{{"path", "prompts/ai_698_framing.md"}, {"sha256", QString::fromLatin1(ConfigMigration::digest("new prompt").toHex())}, {"data", QString::fromLatin1(QByteArray("new prompt").toBase64())}};
    QJsonObject request{{"schemaVersion", 1}, {"config", desired}, {"groups", QJsonArray{"parser"}}, {"files", QJsonArray{resource}}};
    check(ConfigJson::write(dir.filePath("restore-pending.json"), request, &error), "stage interrupted fixture");
    QJsonObject state{{"schemaVersion", 1}, {"phase", "applying"}, {"snapshot", snapshot}, {"absent", QJsonArray{}}, {"files", QJsonArray{resource}},
      {"previousSha256", QJsonObject{{"prompts/ai_698_framing.md", QString::fromLatin1(ConfigMigration::digest("old prompt").toHex())}}},
      {"targetSha256", QString::fromLatin1(ConfigMigration::digest(QJsonDocument(desired).toJson(QJsonDocument::Compact)).toHex())}};
    check(ConfigJson::write(dir.filePath("restore-state.json"), state, &error), "persist interrupted journal");
    write(dir.filePath("prompts/ai_698_framing.md"), externalEdit ? "external edit" : "new prompt");
    ConfigManager manager(dir.path());
    const bool ok = manager.applyPending(&error);
    check(ok != externalEdit, "interrupted restore resumes only if no outside edit would be overwritten");
    check(read(dir.filePath("prompts/ai_698_framing.md")) == (externalEdit ? "external edit" : "new prompt"), "external edit preserved and successful restart completes intended resource");
    check(JsonSettings(dir.filePath("settings.json")).value("protocolType").toInt() == (externalEdit ? 5 : 2), "config stays original on conflict and restores on retry");
  }
}

}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    storesAndMissing(); credentialGates(); protectionAndConflicts(); mqttAndPresets(); backups(); interruptedRestore();
    QTextStream(stdout) << "PASS: " << assertions << " configuration assertions\n"; return 0;
  } catch (const std::exception &e) { QTextStream(stderr) << "FAIL: " << e.what() << '\n'; return 1; }
}
