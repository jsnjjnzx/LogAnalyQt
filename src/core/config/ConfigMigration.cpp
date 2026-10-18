#include "ConfigMigration.h"
#include "CredentialAccess.h"
#include "PresetCodec.h"
#include "../mqtt/MqttController.h"
#include "../mqtt/MqttTypes.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>
#include <QUrl>

namespace {
bool readFile(const QString &path, QByteArray *bytes, QString *error) {
  QFile file(path);
  if (QFileInfo(path).isSymbolicLink() || !QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > ConfigJson::MaxBytes)
    return ConfigJson::fail(error, QStringLiteral("旧文件不可读、过大或不是普通文件：") + path);
  *bytes = file.readAll(); return file.error() == QFile::NoError || ConfigJson::fail(error, QStringLiteral("旧文件读取失败：") + path);
}
bool protectedPath(const QString &path) {
  const auto parts = QDir::cleanPath(path).split('/');
  for (const auto &part : parts)
    if (part == "build" || part.startsWith("build-") || part == "release_package" || part.contains("backup", Qt::CaseInsensitive) || part == ".git") return true;
  return false;
}
void merge(QJsonObject &target, const QJsonObject &source, const QString &prefix, QStringList *conflicts) {
  for (auto it = source.begin(); it != source.end(); ++it) {
    const auto path = prefix.isEmpty() ? it.key() : prefix + '/' + it.key();
    if (target.contains(it.key()) && target.value(it.key()).isObject() && it.value().isObject()) {
      auto child = target.value(it.key()).toObject(); merge(child, it.value().toObject(), path, conflicts); target.insert(it.key(), child);
    } else if (target.contains(it.key()) && target.value(it.key()) != it.value()) {
      if (!conflicts->contains(path)) conflicts->append(path);
    } else target.insert(it.key(), it.value());
  }
}
bool iniKnown(const QString &key) {
  static const QRegularExpression pattern(QStringLiteral(
    "^(protocolType|ssh/(host|port|username|password|encoding|maxLines|autoReconnectEnabled|reconnectMaxAttempts|reconnectIntervalMs)|"
    "aiFraming/(baseUrl|model)|(?:protocolSync|promptSync)/(sharePath|username|password|credentialProfile)|"
    "ui/(?:next/window/(?:x|y|width|height)|window/(?:x|y|width|height)|next/appearance/(?:reducedTransparency|dark|followSystem)|"
    "globalPreview/colWidth(?:Time|Prot|Addr|Dir|Seq)|encoding/current|pane[12]/(?:colsR1|colsR2|autoFitLocked|presetProtocolIndex)|"
    "next/multiParse/(?:workspace|recovery)|liveLog/customCommands)|security/(?:sealedStateFormat|sealedStateBlobV1|lastVerifiedUtc|lastVerifiedProtocolSyncAt))$"));
  return pattern.match(key).hasMatch();
}
bool convertIni(const QString &path, ConfigMigration::Plan &plan, QJsonObject *converted) {
  QSettings ini(path, QSettings::IniFormat);
  for (const auto &key : ini.allKeys()) {
    if (!iniKnown(key)) return ConfigJson::fail(&plan.error, QStringLiteral("发现未识别字段，停止迁移并保留原件：") + key);
    if (key.startsWith("security/")) { plan.deviceState.insert(key, ini.value(key)); continue; }
    if (key.endsWith("/password")) continue;
    if (key == "ui/next/appearance/dark" || key == "ui/next/appearance/followSystem" || key.startsWith("ui/window/")) {
      plan.notes.append(QStringLiteral("保留在回退快照中的旧版界面字段：") + key);
      ConfigJson::put(*converted, {"retired", key}, QJsonValue::fromVariant(ini.value(key))); continue;
    }
    auto value = QJsonValue::fromVariant(ini.value(key));
    if (key.endsWith("autoFitLocked") || key.endsWith("reducedTransparency") || key == "ssh/autoReconnectEnabled") value = ini.value(key).toBool();
    if (key == "protocolType" || key.startsWith("ssh/") && QStringList{"port", "maxLines", "reconnectMaxAttempts", "reconnectIntervalMs"}.contains(key.mid(4)) || key.endsWith("presetProtocolIndex")) {
      bool ok = false; const auto n = ini.value(key).toInt(&ok);
      if (!ok) return ConfigJson::fail(&plan.error, QStringLiteral("旧整数字段无效：") + key);
      value = n;
    }
    if (key.startsWith("ui/") && (key.contains("/window/") || key.contains("/colWidth") || key.endsWith("colsR1") || key.endsWith("colsR2"))) {
      bool ok = false; const auto n = ini.value(key).toDouble(&ok);
      if (!ok || !std::isfinite(n)) return ConfigJson::fail(&plan.error, QStringLiteral("旧数值字段无效：") + key);
      value = n;
    }
    if (key == "ui/next/multiParse/workspace" || key == "ui/liveLog/customCommands") {
      QJsonParseError parse; auto doc = QJsonDocument::fromJson(ini.value(key).toString().toUtf8(), &parse);
      if (parse.error != QJsonParseError::NoError || (key.endsWith("workspace") ? !doc.isObject() : !doc.isArray()))
        return ConfigJson::fail(&plan.error, QStringLiteral("旧工作区或快捷命令损坏，原件保留"));
      value = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
    }
    ConfigJson::put(*converted, ConfigJson::canonicalKey(key).split('/'), value);
  }
  const auto password = ini.value("ssh/password").toString().toUtf8();
  if (!password.isEmpty()) {
    const auto host = ini.value("ssh/host").toString(), user = ini.value("ssh/username").toString();
    bool valid = false; int port = ini.value("ssh/port").toInt(&valid);
    if (host.trimmed().isEmpty() || user.isEmpty() || !valid || port < 1 || port > 65535) return ConfigJson::fail(&plan.error, QStringLiteral("旧 SSH 密码缺少有效连接身份，原件保留"));
    plan.secrets.append({SecureCredentialStore::Domain::Ssh, CredentialAccess::sshProfile(host, port, user), password});
  }
  for (const auto &prefix : {QString("protocolSync/"), QString("promptSync/")}) {
    const auto password = ini.value(prefix + "password").toString();
    if (password.isEmpty()) continue;
    const auto username = ini.value(prefix + "username").toString();
    const auto canonical = QFileInfo(path).canonicalFilePath();
    const auto profile = CredentialAccess::profile(((prefix.startsWith("prompt") ? "legacy-prompts-unbound\n" : "legacy-unbound\n") + canonical).toUtf8());
    plan.secrets.append({SecureCredentialStore::Domain::ProtocolSync, profile,
      QJsonDocument(QJsonObject{{"username", username}, {"password", password}}).toJson(QJsonDocument::Compact)});
    ConfigJson::put(*converted, ConfigJson::canonicalKey(prefix + "credentialProfile").split('/'), profile);
  }
  return ini.status() == QSettings::NoError || ConfigJson::fail(&plan.error, QStringLiteral("旧 INI 解析失败"));
}
void extractMqtt(QJsonObject &entry, ConfigMigration::Plan &plan, const QString &fallback) {
  Q_UNUSED(fallback);
  const auto bytes = entry.value("password").toString().toUtf8();
  if (!bytes.isEmpty()) {
    const auto profile = CredentialAccess::mqttProfile(entry.toVariantMap());
    plan.secrets.append({SecureCredentialStore::Domain::Mqtt, profile, bytes}); entry.insert("credentialProfile", profile);
  }
  entry.remove("password");
}
}

QString ConfigMigration::Plan::description() const {
  QString text = QStringLiteral("以下旧配置将写入 settings.json；配置和密码均验证成功后，才删除标注的原文件。\n\n");
  for (const auto &source : sources) text += (source.deletable ? QStringLiteral("迁移后删除：") : QStringLiteral("仅导入，原件保留：")) + source.path + '\n';
  text += QStringLiteral("\n密码迁移项：%1；冲突字段：%2\n协议 XML、提示词、证书、主机信任记录不删除。历史备份和构建目录不自动处理。").arg(secrets.size()).arg(conflicts.size());
  if (!notes.isEmpty()) text += "\n" + notes.join("\n");
  if (!error.isEmpty()) text += QStringLiteral("\n\n尚不能迁移：") + error;
  return text;
}

ConfigMigration::Plan ConfigMigration::inspect(const QString &root, const QStringList &roots, bool allowDelete) {
  Plan plan; plan.root = QDir(root).absolutePath();
  if (!ConfigJson::read(QDir(root).filePath("settings.json"), &plan.original, &plan.error)) return plan;
  QStringList locations = roots.isEmpty() ? QStringList{root} : roots;
  for (const auto &location : locations) {
    if (allowDelete && protectedPath(location)) continue;
    if (QFileInfo(location).isSymbolicLink()) { plan.error = QStringLiteral("旧配置根目录是符号链接，未处理"); return plan; }
    QStringList relative{"LogAnalyQt.ini", "mqtt/connections.json", "mqtt/common-subscriptions.json", "protocol_xml/preset_messages.xml", "preset/preset_messages.xml"};
    QDir templates(QDir(location).filePath("preset/smart_framing"));
    for (const auto &name : templates.entryList({"*.json"}, QDir::Files)) relative.append("preset/smart_framing/" + name);
    for (const auto &name : relative) {
      const auto path = QDir(location).absoluteFilePath(name);
      if (!QFileInfo::exists(path) && !QFileInfo(path).isSymbolicLink()) continue;
      // Reject symlink ancestors as well as symlink files before reading or cleanup.
      for (auto parent = QFileInfo(path).dir(); parent.absolutePath() != QDir(location).absolutePath(); parent.cdUp()) {
        if (QFileInfo(parent.absolutePath()).isSymbolicLink()) { plan.error = QStringLiteral("旧配置路径含符号链接"); return plan; }
      }
      QByteArray bytes; if (!readFile(path, &bytes, &plan.error)) return plan;
      QJsonObject converted;
      if (name.endsWith(".ini")) {
        if (!convertIni(path, plan, &converted)) return plan;
      } else if (name.endsWith(".xml")) {
        QJsonObject messages; if (!PresetCodec::decode(bytes, &messages, &plan.error)) return plan;
        converted.insert("messages", messages);
      } else {
        QJsonParseError parse; const auto doc = QJsonDocument::fromJson(bytes, &parse);
        if (parse.error != QJsonParseError::NoError || !doc.isObject()) { plan.error = QStringLiteral("旧 JSON 格式无效：") + path; return plan; }
        auto json = doc.object();
        if (name == "mqtt/connections.json") {
          if (json.value("schema") != 1) { plan.error = QStringLiteral("旧 MQTT 格式版本不受支持"); return plan; }
          json.remove("schema");
          if (json.contains("splitRatio")) { json.insert("layout", QJsonObject{{"splitRatio", json.take("splitRatio")}}); }
          QJsonArray entries;
          for (const auto &entry : json.value("connections").toArray()) { auto row = entry.toObject(); extractMqtt(row, plan, "remote"); entries.append(row); }
          json.insert("connections", entries);
          for (const auto &section : {"broker", "localClient"}) { auto row = json.value(section).toObject(); extractMqtt(row, plan, section); json.insert(section, row); }
          converted.insert("mqtt", json);
        } else if (name == "mqtt/common-subscriptions.json") {
          if (json.value("schema") != 1 || !json.value("subscriptions").isArray()) { plan.error = QStringLiteral("旧常用订阅格式无效"); return plan; }
          converted.insert("mqtt", QJsonObject{{"commonSubscriptions", json.value("subscriptions")}});
        } else {
          auto templates = QJsonObject{{QFileInfo(name).completeBaseName(), json}}; converted.insert("framingTemplates", templates);
        }
      }
      QStringList sourceConflicts;
      merge(plan.incoming, converted, {}, &sourceConflicts);
      if (!sourceConflicts.isEmpty()) { plan.error = QStringLiteral("多份旧来源互相冲突，请在管理入口分次导入，原件保留：") + sourceConflicts.join(", "); return plan; }
      plan.sources.append({path, digest(bytes), converted, allowDelete && !protectedPath(path)});
    }
  }
  auto merged = plan.original; merge(merged, plan.incoming, {}, &plan.conflicts);
  if (plan.conflicts.isEmpty() && !validate(merged, &plan.error)) return plan;
  return plan;
}

bool ConfigMigration::validate(const QJsonObject &root, QString *error) {
  if (root.contains("security")) return ConfigJson::fail(error, QStringLiteral("设备安全状态不能放入个人配置"));
  if (root.value("schemaVersion") != 1) return ConfigJson::fail(error, QStringLiteral("不支持的配置版本"));
  for (const auto &key : {"ui", "parser", "mqtt", "ssh", "ai", "sync", "messages", "workspace", "resources", "framingTemplates"})
    if (root.contains(key) && !root.value(key).isObject()) return ConfigJson::fail(error, QStringLiteral("配置分组必须是对象：") + key);
  const auto ssh = root.value("ssh").toObject();
  if (ssh.contains("password") || ssh.value("connection").toObject().contains("password") || root.value("ai").toObject().contains("apiKey")) return ConfigJson::fail(error, QStringLiteral("个人配置不得包含专用密码字段"));
  for (const auto &key : {"protocol", "prompt"}) if (root.value("sync").toObject().value(key).toObject().contains("password")) return ConfigJson::fail(error, QStringLiteral("共享目录密码必须保存在系统凭据中"));
  const auto ai = root.value("ai").toObject();
  if (ai.contains("baseUrl")) {
    const auto text = ai.value("baseUrl").toString(); const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() || text.contains(QRegularExpression("[\\s\\x00-\\x1f]"))) return ConfigJson::fail(error, QStringLiteral("AI 服务地址无效"));
  }
  if (ai.contains("model") && !QRegularExpression("^[A-Za-z0-9][A-Za-z0-9._:/-]{0,127}$").match(ai.value("model").toString()).hasMatch()) return ConfigJson::fail(error, QStringLiteral("AI 模型标识无效"));
  const auto connection = ssh.value("connection").toObject();
  if (connection.contains("port")) {
    const auto port = connection.value("port").toDouble(-1);
    if (port < 1 || port > 65535 || port != int(port)) return ConfigJson::fail(error, QStringLiteral("SSH 端口无效"));
  }
  const auto parser = root.value("parser").toObject();
  if (parser.contains("protocolType")) {
    const auto protocol = parser.value("protocolType").toDouble(-1);
    if (protocol < 0 || protocol > 8 || protocol != int(protocol)) return ConfigJson::fail(error, QStringLiteral("解析协议选择无效"));
  }
  if (root.contains("mqtt")) {
    const auto mqtt = root.value("mqtt").toObject();
    const auto connections = mqtt.value("connections");
    if (!connections.isUndefined() && (!connections.isArray() || connections.toArray().isEmpty() || connections.toArray().size() > 64)) return ConfigJson::fail(error, QStringLiteral("MQTT 连接列表无效"));
    QSet<QString> ids;
    for (const auto &entry : connections.toArray()) {
      if (!entry.isObject()) return ConfigJson::fail(error, QStringLiteral("MQTT 连接格式无效"));
      const auto row = entry.toObject();
      if (row.contains("password")) return ConfigJson::fail(error, QStringLiteral("MQTT 密码必须保存在系统凭据中"));
      if (QUuid::fromString(row.value("id").toString()).isNull() || ids.contains(row.value("id").toString())) return ConfigJson::fail(error, QStringLiteral("MQTT 连接标识无效或重复"));
      ids.insert(row.value("id").toString());
      auto normalized = Mqtt::connectionDefaults(row.value("id").toString());
      for (const auto &key : row.keys()) if (!normalized.contains(key)) return ConfigJson::fail(error, QStringLiteral("MQTT 连接含未识别字段，停止迁移：") + key);
      for (auto it = row.begin(); it != row.end(); ++it) normalized.insert(it.key(), it.value().toVariant());
      const auto problem = MqttController::validateWorkspace(normalized);
      if (!problem.isEmpty()) return ConfigJson::fail(error, problem);
    }
    if (mqtt.value("localClient").toObject().contains("password") || mqtt.value("broker").toObject().contains("password")) return ConfigJson::fail(error, QStringLiteral("MQTT 密码必须保存在系统凭据中"));
    if (mqtt.contains("localClient")) {
      auto row = Mqtt::connectionDefaults("local"); const auto json = mqtt.value("localClient").toObject();
      for (auto it = json.begin(); it != json.end(); ++it) row.insert(it.key(), it.value().toVariant());
      const auto problem = MqttController::validateWorkspace(row); if (!problem.isEmpty()) return ConfigJson::fail(error, problem);
    }
    if (mqtt.contains("broker")) {
      auto row = Mqtt::brokerDefaults(); const auto json = mqtt.value("broker").toObject();
      for (auto it = json.begin(); it != json.end(); ++it) row.insert(it.key(), it.value().toVariant());
      const auto problem = Mqtt::validateBroker(row); if (!problem.isEmpty()) return ConfigJson::fail(error, problem);
    }
    if (mqtt.contains("commonSubscriptions") && !mqtt.value("commonSubscriptions").isArray()) return ConfigJson::fail(error, QStringLiteral("常用订阅必须是数组"));
    const auto common = mqtt.value("commonSubscriptions").toArray();
    if (common.size() > 1000) return ConfigJson::fail(error, QStringLiteral("常用订阅过多"));
    QSet<QString> topics;
    for (const auto &entry : common) {
      const auto row = entry.toObject(); const auto topic = row.value("topic").toString();
      const auto qos = row.value("qos").toDouble(-1);
      const auto problem = Mqtt::validateSubscription(topic, int(qos));
      if (!entry.isObject() || qos != int(qos) || topics.contains(topic) || !problem.isEmpty() || !Mqtt::validateSubscriptionAppearance(row.toVariantMap()).isEmpty())
        return ConfigJson::fail(error, QStringLiteral("常用订阅无效或重复"));
      topics.insert(topic);
    }
  }
  const auto workspace = root.value("workspace").toObject().value("parse");
  if (!workspace.isUndefined()) {
    const auto value = workspace.toObject();
    if (!workspace.isObject() || value.value("version") != 1 || !value.value("tabs").isArray() || !value.value("currentIndex").isDouble() || !value.value("open").isBool())
      return ConfigJson::fail(error, QStringLiteral("解析工作区格式无效"));
    for (const auto &tab : value.value("tabs").toArray()) {
      const auto t = tab.toObject();
      if (!t.value("text").isString() || !t.value("parsedText").isString() || !t.value("locked").isBool()) return ConfigJson::fail(error, QStringLiteral("解析标签内容无效"));
      for (const auto &key : {"protocol", "parsedProtocol", "encoding", "parsedEncoding"}) {
        const auto n = t.value(key).toDouble(-1); const auto maximum = QString(key).contains("rotocol") ? 8 : 3;
        if (n < 0 || n > maximum || n != int(n)) return ConfigJson::fail(error, QStringLiteral("解析标签参数无效"));
      }
      const auto a = t.value("r1").toDouble(), b = t.value("r2").toDouble();
      if (a <= 0 || b <= 0 || a+b >= 1) return ConfigJson::fail(error, QStringLiteral("解析标签列宽无效"));
    }
  }
  const auto commands = root.value("ssh").toObject().value("customCommands");
  if (!commands.isUndefined() && !commands.isArray()) return ConfigJson::fail(error, QStringLiteral("快捷命令格式无效"));
  for (const auto &command : commands.toArray()) if (command.toObject().value("name").toString().trimmed().isEmpty() || command.toObject().value("command").toString().trimmed().isEmpty()) return ConfigJson::fail(error, QStringLiteral("快捷命令名称或内容为空"));
  if (root.contains("messages")) {
    QJsonObject roundtrip;
    if (!PresetCodec::decode(PresetCodec::encode(root.value("messages").toObject()), &roundtrip, error)) return false;
    if (roundtrip.value("presets") != root.value("messages").toObject().value("presets")) return ConfigJson::fail(error, QStringLiteral("预置报文格式无效"));
  }
  return true;
}

bool ConfigMigration::execute(const Plan &plan, const QVariantMap &choices, QString *error, const Stores &stores) {
  if (!plan.error.isEmpty()) return ConfigJson::fail(error, plan.error);
  if (!QDir().mkpath(plan.root)) return ConfigJson::fail(error, QStringLiteral("数据目录不可写"));
  const auto path = QDir(plan.root).filePath("settings.json");
  QLockFile lock(path + ".lock"); if (!lock.tryLock(0)) return ConfigJson::fail(error, QStringLiteral("配置正被使用，请关闭其他实例"));
  QJsonObject current; if (!ConfigJson::read(path, &current, error) || current != plan.original) return ConfigJson::fail(error, QStringLiteral("新配置在确认后已变化，请重新检查"));
  for (const auto &source : plan.sources) {
    QByteArray bytes; if (!readFile(source.path, &bytes, error) || digest(bytes) != source.digest) return ConfigJson::fail(error, QStringLiteral("旧配置在确认后已变化，未清理：") + source.path);
  }
  auto target = plan.original;
  QStringList ignored; merge(target, plan.incoming, {}, &ignored);
  for (const auto &conflict : plan.conflicts) {
    const auto choice = choices.value(conflict).toString();
    if (choice != "current" && choice != "legacy") return ConfigJson::fail(error, QStringLiteral("请先选择冲突字段的来源：") + conflict);
    if (choice == "legacy") ConfigJson::put(target, conflict.split('/'), ConfigJson::get(plan.incoming, conflict.split('/')));
  }
  if (!validate(target, error)) return false;
  const auto backup = QDir(plan.root).filePath("migration-backups/" + QUuid::createUuid().toString(QUuid::WithoutBraces));
  if (!ConfigJson::write(backup + "/before.json", plan.original, error)) return false;
  for (qsizetype i = 0; i < plan.sources.size(); ++i) {
    auto snapshot = plan.sources[i].converted; snapshot.insert("schemaVersion", 1);
    if (!ConfigJson::write(backup + "/source-" + QString::number(i) + ".json", snapshot, error)) return false;
  }
  for (const auto &secret : plan.secrets) {
    auto store = stores ? stores(secret.domain) : std::make_shared<SecureCredentialStore>(secret.domain);
    QByteArray previous;
    if (!store || !store->isAvailable() || !store->read(secret.profile, &previous, error)) return ConfigJson::fail(error, QStringLiteral("无法读取安全凭据，所有旧文件保留"));
    if (!previous.isEmpty() && previous != secret.bytes) return ConfigJson::fail(error, QStringLiteral("旧密码与系统凭据冲突，未覆盖或删除"));
    if (!store->writeVerified(secret.profile, secret.bytes, error)) return false;
  }
  if (!plan.deviceState.isEmpty()) {
    QSettings device(QDir(plan.root).filePath("device-state.ini"), QSettings::IniFormat);
    for (auto it = plan.deviceState.begin(); it != plan.deviceState.end(); ++it)
      if (device.contains(it.key()) && device.value(it.key()) != it.value()) return ConfigJson::fail(error, QStringLiteral("设备状态冲突，所有旧文件保留"));
    for (auto it = plan.deviceState.begin(); it != plan.deviceState.end(); ++it) device.setValue(it.key(), it.value());
    device.sync();
    if (device.status() != QSettings::NoError) return ConfigJson::fail(error, QStringLiteral("设备状态保存失败，旧 INI 保留"));
    QSettings verified(device.fileName(), QSettings::IniFormat);
    for (auto it = plan.deviceState.begin(); it != plan.deviceState.end(); ++it) if (verified.value(it.key()) != it.value()) return ConfigJson::fail(error, QStringLiteral("设备状态回读失败"));
  }
  QJsonObject beforeCommit;
  if (!ConfigJson::read(path, &beforeCommit, error) || beforeCommit != plan.original) return ConfigJson::fail(error, QStringLiteral("等待密码授权期间新配置已变化，停止提交并保留旧文件"));
  for (const auto &source : plan.sources) {
    QByteArray bytes;
    if (!readFile(source.path, &bytes, error) || digest(bytes) != source.digest) return ConfigJson::fail(error, QStringLiteral("等待密码授权期间旧配置已变化，停止提交并保留原件"));
  }
  QJsonArray manifest;
  for (const auto &source : plan.sources) manifest.append(QJsonObject{{"path", source.path}, {"sha256", QString::fromLatin1(source.digest.toHex())}, {"delete", source.deletable}});
  QJsonObject record{{"schemaVersion", 1}, {"sources", manifest}, {"snapshot", backup}, {"phase", "prepared"}};
  if (!ConfigJson::write(QDir(plan.root).filePath("migration-state.json"), record, error)) return false;
  if (!ConfigJson::write(path, target, error)) return false;
  // Recheck every credential and every source before allowing the first deletion.
  for (const auto &secret : plan.secrets) {
    auto store = stores ? stores(secret.domain) : std::make_shared<SecureCredentialStore>(secret.domain);
    QByteArray saved;
    if (!store->read(secret.profile, &saved, error) || saved != secret.bytes) return ConfigJson::fail(error, QStringLiteral("密码最终验证失败，所有旧文件保留"));
  }
  QJsonObject saved; if (!ConfigJson::read(path, &saved, error) || saved != target) return ConfigJson::fail(error, QStringLiteral("配置最终验证失败"));
  for (const auto &source : plan.sources) {
    QByteArray bytes; if (!readFile(source.path, &bytes, error) || digest(bytes) != source.digest) return ConfigJson::fail(error, QStringLiteral("旧配置发生变化，停止清理"));
  }
  QStringList retained;
  for (const auto &source : plan.sources) {
    if (!source.deletable) continue;
    QByteArray bytes;
    if (!readFile(source.path, &bytes, error) || digest(bytes) != source.digest || !QFile::remove(source.path)) retained.append(source.path);
  }
  record.insert("phase", retained.isEmpty() ? "completed" : "cleanupPending");
  record.insert("retained", QJsonArray::fromStringList(retained));
  if (!ConfigJson::write(QDir(plan.root).filePath("migration-state.json"), record, error)) return false;
  if (!retained.isEmpty()) return ConfigJson::fail(error, QStringLiteral("新配置及密码已验证；以下旧文件未能清理，已保留：\n") + retained.join('\n'));
  return true;
}
