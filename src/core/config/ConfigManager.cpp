#include "ConfigManager.h"
#include "../../common/AppPaths.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDirIterator>
#include <QThread>
#include <QDateTime>
#include <QStringDecoder>
#include <QDomDocument>
#include <QRegularExpression>
#include <QUuid>

namespace {
QString hash(const QByteArray &bytes) { return QString::fromLatin1(ConfigMigration::digest(bytes).toHex()); }
bool atomicBytes(const QString &path, const QByteArray &bytes, QString *error) {
  if (QFileInfo(path).isSymbolicLink() || !QDir().mkpath(QFileInfo(path).absolutePath())) return ConfigJson::fail(error, QStringLiteral("目标路径不可写或为符号链接"));
  QSaveFile output(path);
  if (!output.open(QIODevice::WriteOnly) || !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) || output.write(bytes) != bytes.size() || !output.commit()) return ConfigJson::fail(error, QStringLiteral("备份或资源写入失败"));
  QFile check(path); return (check.open(QIODevice::ReadOnly) && check.readAll() == bytes) || ConfigJson::fail(error, QStringLiteral("文件回读验证失败"));
}
bool resourceName(const QString &name) {
  if (name == "ssh-known-hosts.json") return true;
  return QRegularExpression(QStringLiteral("^(protocol_xml/[^/\\\\]+\\.xml|prompts/[^/\\\\]+\\.md)$")).match(name).hasMatch() && !name.contains("..");
}
QJsonObject portable(QJsonObject root, bool workspace) {
  root.remove("retired"); root.remove("migration"); root.remove("security");
  if (!workspace) root.remove("workspace");
  auto mqtt = root.value("mqtt").toObject();
  auto clean = [workspace](QJsonObject entry) {
    entry.remove("password");
    if (!workspace) { entry.remove("draft"); entry.remove("history"); }
    return entry;
  };
  if (mqtt.contains("connections")) {
    QJsonArray list; for (const auto &entry : mqtt.value("connections").toArray()) list.append(clean(entry.toObject()));
    mqtt.insert("connections", list);
  }
  for (const auto &key : {"broker", "localClient"}) if (mqtt.contains(key)) mqtt.insert(key, clean(mqtt.value(key).toObject()));
  if (!mqtt.isEmpty()) root.insert("mqtt", mqtt);
  auto ssh = root.value("ssh").toObject(); ssh.remove("password"); if (!ssh.isEmpty()) root.insert("ssh", ssh);
  auto ai = root.value("ai").toObject(); ai.remove("apiKey"); if (!ai.isEmpty()) root.insert("ai", ai);
  auto sync = root.value("sync").toObject();
  for (const auto &key : {"protocol", "prompt"}) {
    auto entry = sync.value(key).toObject(); entry.remove("password"); if (!entry.isEmpty()) sync.insert(key, entry);
  }
  if (!sync.isEmpty()) root.insert("sync", sync);
  return root;
}
bool readBytes(const QString &path, QByteArray *bytes, QString *error) {
  QFile file(path);
  if (!QFileInfo(path).isFile() || QFileInfo(path).isSymbolicLink() || !file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) return ConfigJson::fail(error, QStringLiteral("文件不可读、过大或不是普通文件：") + path);
  *bytes = file.readAll();
  return file.error() == QFile::NoError || ConfigJson::fail(error, QStringLiteral("文件读取失败：") + path);
}
QByteArray bytesOf(const QString &path) { QByteArray bytes; readBytes(path, &bytes, nullptr); return bytes; }
bool rollbackRestore(const QString &root, QJsonObject state, QString *error) {
  const QString snapshot = state.value("snapshot").toString();
  const auto allowed = QDir(root).absoluteFilePath("restore-backups") + '/';
  if (!QDir::cleanPath(snapshot).startsWith(allowed) || QFileInfo(snapshot).isSymbolicLink() || !QFile::exists(snapshot + "/settings.json")) return ConfigJson::fail(error, QStringLiteral("恢复回退目录无效"));
  QJsonObject original, current;
  if (!ConfigJson::read(snapshot + "/settings.json", &original, error) || !ConfigMigration::validate(original, error) || !ConfigJson::read(QDir(root).filePath("settings.json"), &current, error)) return false;
  if (current != original && hash(QJsonDocument(current).toJson(QJsonDocument::Compact)) != state.value("targetSha256").toString()) return ConfigJson::fail(error, QStringLiteral("配置已被其他程序修改，停止自动回退"));
  const auto absent = state.value("absent").toArray();
  for (const auto &entry : state.value("files").toArray()) {
    const auto row = entry.toObject(); const auto name = row.value("path").toString();
    if (!resourceName(name)) return ConfigJson::fail(error, QStringLiteral("恢复日志路径无效"));
    const auto target = QDir(root).filePath(name);
    if (QFileInfo(QFileInfo(target).absolutePath()).isSymbolicLink()) return ConfigJson::fail(error, QStringLiteral("资源目录已变为符号链接，停止回退"));
    QByteArray installed;
    if (QFile::exists(target) && !readBytes(target, &installed, error)) return false;
    const auto intended = QByteArray::fromBase64(row.value("data").toString().toLatin1());
    if (absent.contains(name)) {
      if (!QFile::exists(target)) continue;
      if (installed != intended) return ConfigJson::fail(error, QStringLiteral("新资源已被外部修改，停止自动回退：") + name);
      if (!QFile::remove(target)) return ConfigJson::fail(error, QStringLiteral("无法回退新建资源"));
    } else {
      QByteArray previous;
      if (!readBytes(snapshot + '/' + name, &previous, error) || hash(previous) != state.value("previousSha256").toObject().value(name).toString()) return ConfigJson::fail(error, QStringLiteral("资源回退快照校验失败"));
      if (installed == previous) continue;
      if (!QFile::exists(target) || installed != intended) return ConfigJson::fail(error, QStringLiteral("资源已被外部修改，停止自动回退：") + name);
      if (!atomicBytes(target, previous, error)) return false;
    }
  }
  if (!ConfigJson::write(QDir(root).filePath("settings.json"), original, error)) return false;
  state.insert("phase", "recovered");
  return ConfigJson::write(QDir(root).filePath("restore-state.json"), state, error);
}
}
ConfigManager::ConfigManager(const QString &root, QObject *parent) : QObject(parent), m_root(root) {}
QVariantMap ConfigManager::result(bool ok, const QString &error) const { return {{"ok", ok}, {"message", error}}; }
QString ConfigManager::summary() const {
  QJsonObject root; QString error;
  if (!ConfigJson::read(QDir(m_root).filePath("settings.json"), &root, &error)) return error;
  const auto mqtt = root.value("mqtt").toObject();
  QString text = QStringLiteral("配置：%1\n格式版本：1\nMQTT 连接：%2；常用订阅：%3\nSSH 自定义命令：%4；预置报文：%5；组帧模板：%6\n解析标签：%7\n密码保存在系统凭据存储，不包含在备份中。\n协议、提示词和证书仍是独立资源；证书和私钥需另行保管。")
    .arg(QDir(m_root).filePath("settings.json")).arg(mqtt.value("connections").toArray().size()).arg(mqtt.value("commonSubscriptions").toArray().size())
    .arg(root.value("ssh").toObject().value("customCommands").toArray().size()).arg(root.value("messages").toObject().value("presets").toArray().size())
    .arg(root.value("framingTemplates").toObject().size()).arg(root.value("workspace").toObject().value("parse").toObject().value("tabs").toArray().size());
  const auto statePath = QDir(m_root).filePath("migration-state.json");
  if (QFile::exists(statePath)) {
    QJsonObject state; if (ConfigJson::read(statePath, &state)) text += QStringLiteral("\n迁移状态：%1\n迁移回退快照：%2").arg(state.value("phase").toString(), state.value("snapshot").toString());
  }
  for (const auto &entry : mqtt.value("connections").toArray()) {
    const auto row = entry.toObject();
    for (const auto &key : {"caFile", "certFile", "keyFile"}) {
      const auto path = row.value(key).toString();
      if (!path.isEmpty() && !QFileInfo::exists(path)) text += QStringLiteral("\n缺少证书资源（%1）：%2").arg(row.value("name").toString(), path);
    }
  }
  return text;
}
QVariantList ConfigManager::conflictDetails() const {
  QVariantList rows;
  auto preview = [](const QJsonValue &value) {
    auto bytes = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    auto text = QString::fromUtf8(bytes.mid(1, bytes.size() - 2));
    return text.size() > 320 ? text.left(320) + QStringLiteral("…") : text;
  };
  for (const auto &key : m_plan.conflicts) rows.append(QVariantMap{{"key", key},
    {"current", preview(ConfigJson::get(m_plan.original, key.split('/')))},
    {"legacy", preview(ConfigJson::get(m_plan.incoming, key.split('/')))}});
  return rows;
}
QVariantMap ConfigManager::cancelPending() {
  for (const auto &name : {"import-pending.json", "restore-pending.json"}) {
    const auto path = QDir(m_root).filePath(name);
    if (QFile::exists(path) && !QFile::remove(path)) return result(false, QStringLiteral("无法取消待应用操作：") + name);
  }
  return result(true, QStringLiteral("待应用操作已取消，当前配置未改变。"));
}
void ConfigManager::inspectStartup(const QStringList &roots) { m_plan = ConfigMigration::inspect(m_root, roots); m_error = m_plan.error; emit changed(); }
void ConfigManager::migrate(const QVariantMap &choices) {
  if (m_busy) return;
  m_busy = true; m_error.clear(); emit changed();
  const auto plan = m_plan; auto detail = std::make_shared<QString>(); auto ok = std::make_shared<bool>(false);
  auto *thread = QThread::create([plan, choices, detail, ok] { *ok = ConfigMigration::execute(plan, choices, detail.get()); });
  connect(thread, &QThread::finished, this, [this, detail, ok] {
    m_busy = false; m_error = *detail;
    if (*ok) { for (auto &secret : m_plan.secrets) secret.bytes.fill(0); m_plan.secrets.clear(); }
    emit changed(); emit migrationFinished(*ok);
  });
  connect(thread, &QThread::finished, thread, &QObject::deleteLater); thread->start();
}
void ConfigManager::refresh() { emit changed(); }
void ConfigManager::openDirectory() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_root)); }
QVariantMap ConfigManager::exportBackup(const QUrl &url, bool resources, bool workspace, bool trust) {
  if (!url.isLocalFile()) return result(false, QStringLiteral("请选择本地备份文件"));
  const auto destination = QDir::cleanPath(QFileInfo(url.toLocalFile()).absoluteFilePath());
  const auto relative = QDir(m_root).relativeFilePath(destination);
  if (QStringList{"settings.json", "settings.json.previous", "device-state.ini", "ssh-known-hosts.json", "migration-state.json", "restore-state.json", "restore-pending.json", "import-pending.json"}.contains(relative) || relative.startsWith("protocol_xml/") || relative.startsWith("prompts/"))
    return result(false, QStringLiteral("备份不能覆盖当前配置、安全状态或资源文件，请选择其他名称或目录"));
  QString error; QJsonObject root;
  QLockFile lock(QDir(m_root).filePath("settings.json.lock"));
  if (!lock.tryLock(0) || !ConfigJson::read(QDir(m_root).filePath("settings.json"), &root, &error) || !ConfigMigration::validate(root, &error)) return result(false, error.isEmpty() ? QStringLiteral("配置正在保存，请重试") : error);
  root = portable(root, workspace);
  if (!resources) return result(atomicBytes(url.toLocalFile(), QJsonDocument(root).toJson(QJsonDocument::Indented), &error), error);
  QJsonArray files;
  for (const auto &directory : {QString("protocol_xml"), QString("prompts")}) {
    if (QFileInfo(QDir(m_root).filePath(directory)).isSymbolicLink()) return result(false, QStringLiteral("资源目录是符号链接，未导出"));
    QDirIterator it(QDir(m_root).filePath(directory), QDir::Files | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
      const auto path = it.next(); const auto name = QDir(m_root).relativeFilePath(path);
      if (name == "protocol_xml/preset_messages.xml") continue;
      if (!resourceName(name) || QFileInfo(path).isSymbolicLink()) return result(false, QStringLiteral("资源路径无效或是符号链接"));
      QByteArray bytes; if (!readBytes(path, &bytes, &error) || bytes.isEmpty()) return result(false, error.isEmpty() ? QStringLiteral("资源为空：") + name : error);
      files.append(QJsonObject{{"path", name}, {"sha256", hash(bytes)}, {"data", QString::fromLatin1(bytes.toBase64())}});
    }
  }
  if (trust && QFile::exists(QDir(m_root).filePath("ssh-known-hosts.json"))) {
    QByteArray bytes; if (!readBytes(QDir(m_root).filePath("ssh-known-hosts.json"), &bytes, &error)) return result(false, error);
    files.append(QJsonObject{{"path", "ssh-known-hosts.json"}, {"sha256", hash(bytes)}, {"data", QString::fromLatin1(bytes.toBase64())}});
  }
  QJsonObject check;
  if (!ConfigJson::read(QDir(m_root).filePath("settings.json"), &check, &error) || portable(check, workspace) != root) return result(false, QStringLiteral("备份期间配置已变化，请重试"));
  for (const auto &entry : files) {
    const auto row = entry.toObject();
    if (hash(bytesOf(QDir(m_root).filePath(row.value("path").toString()))) != row.value("sha256").toString()) return result(false, QStringLiteral("备份期间资源已变化，请重试"));
  }
  const auto configBytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
  QJsonObject package{{"backupVersion", 1}, {"createdAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {"appVersion", QStringLiteral(APP_VERSION)},
    {"config", root}, {"configSha256", hash(configBytes)}, {"files", files}};
  const auto bytes = QJsonDocument(package).toJson(QJsonDocument::Indented);
  if (bytes.size() > 64 * 1024 * 1024) return result(false, QStringLiteral("备份超过 64 MiB"));
  return result(atomicBytes(url.toLocalFile(), bytes, &error), error);
}
bool ConfigManager::decodeBackup(const QByteArray &bytes, QJsonObject *package, QString *error) {
  if (bytes.size() > 64 * 1024 * 1024) return ConfigJson::fail(error, QStringLiteral("备份超过 64 MiB"));
  QJsonParseError parse; const auto doc = QJsonDocument::fromJson(bytes, &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject()) return ConfigJson::fail(error, QStringLiteral("备份格式无效"));
  auto root = doc.object();
  if (root.contains("schemaVersion")) root = {{"backupVersion", 1}, {"config", root}, {"files", QJsonArray()}};
  if (root.value("backupVersion") != 1 || !root.value("config").isObject() || !root.value("files").isArray()) return ConfigJson::fail(error, QStringLiteral("不支持的备份版本"));
  const auto config = root.value("config").toObject();
  if (!root.value("configSha256").isUndefined() && root.value("configSha256").toString() != hash(QJsonDocument(config).toJson(QJsonDocument::Compact))) return ConfigJson::fail(error, QStringLiteral("配置校验值不符"));
  if (!ConfigMigration::validate(config, error) || portable(config, true) != config) return ConfigJson::fail(error, QStringLiteral("备份配置无效或包含专用密码字段"));
  QSet<QString> names;
  for (const auto &entry : root.value("files").toArray()) {
    const auto file = entry.toObject(); const auto name = file.value("path").toString(); const auto data = QByteArray::fromBase64(file.value("data").toString().toLatin1());
    if (!resourceName(name) || name == "protocol_xml/preset_messages.xml" || names.contains(name) || hash(data) != file.value("sha256").toString()) return ConfigJson::fail(error, QStringLiteral("资源路径、重复项或校验值无效"));
    if (name == "ssh-known-hosts.json" && !QJsonDocument::fromJson(data).isObject()) return ConfigJson::fail(error, QStringLiteral("SSH 信任记录格式无效"));
    if (name.endsWith(".xml") && !QDomDocument().setContent(data)) return ConfigJson::fail(error, QStringLiteral("协议 XML 损坏"));
    if (name.endsWith(".md")) { QStringDecoder decoder(QStringDecoder::Utf8); decoder(data); if (data.isEmpty() || data.size() > 65536 || decoder.hasError()) return ConfigJson::fail(error, QStringLiteral("提示词格式无效")); }
    names.insert(name);
  }
  *package = root; return true;
}
QVariantMap ConfigManager::previewRestore(const QUrl &url) {
  QString error;
  if (!url.isLocalFile() || !decodeBackup(bytesOf(url.toLocalFile()), &m_restore, &error)) { m_restore = {}; return result(false, error); }
  QJsonObject current; if (!ConfigJson::read(QDir(m_root).filePath("settings.json"), &current, &error)) return result(false, error);
  m_previewConfigDigest = ConfigMigration::digest(QJsonDocument(current).toJson(QJsonDocument::Compact));
  const auto incoming = m_restore.value("config").toObject(); QStringList changes;
  for (const auto &group : incoming.keys()) if (group != "schemaVersion") changes.append(group + (current.value(group) == incoming.value(group) ? QStringLiteral("：一致") : current.contains(group) ? QStringLiteral("：替换当前分组") : QStringLiteral("：新增")));
  auto reply = result(true, QStringLiteral("恢复在下次启动、业务模块加载前生效。密码不导入，恢复后需核对证书路径。\n") + changes.join('\n'));
  reply.insert("groups", incoming.keys()); reply.insert("resourceCount", m_restore.value("files").toArray().size()); return reply;
}
QVariantMap ConfigManager::stageRestore(const QStringList &groups, bool resources, bool trust) {
  if (m_restore.isEmpty()) return result(false, QStringLiteral("请先选择并预览备份"));
  QString error; QJsonObject current;
  if (!ConfigJson::read(QDir(m_root).filePath("settings.json"), &current, &error)) return result(false, error);
  auto incoming = m_restore.value("config").toObject();
  if (ConfigMigration::digest(QJsonDocument(current).toJson(QJsonDocument::Compact)) != m_previewConfigDigest) return result(false, QStringLiteral("配置已变化，请重新预览"));
  QJsonArray files;
  if (resources) for (const auto &entry : m_restore.value("files").toArray()) if (entry.toObject().value("path") != "ssh-known-hosts.json" || trust) files.append(entry);
  QJsonArray selected;
  for (const auto &group : groups) if (incoming.contains(group) && group != "schemaVersion") selected.append(group);
  QJsonObject request{{"schemaVersion", 1}, {"config", incoming}, {"groups", selected}, {"files", files}};
  if (!ConfigJson::write(QDir(m_root).filePath("restore-pending.json"), request, &error)) return result(false, error);
  return result(true, QStringLiteral("恢复已安排。请关闭软件，下一次启动时应用；当前配置会先自动备份。"));
}
QVariantMap ConfigManager::previewLegacy(const QUrl &url) {
  if (!url.isLocalFile()) return result(false, QStringLiteral("请选择本地旧配置目录"));
  const QFileInfo chosen(url.toLocalFile());
  if (!chosen.isDir() && chosen.fileName() != "LogAnalyQt.ini") return result(false, QStringLiteral("请选择旧配置目录或 LogAnalyQt.ini；目录可以没有 INI 文件"));
  m_legacyRoot = chosen.isDir() ? chosen.absoluteFilePath() : chosen.absolutePath();
  m_plan = ConfigMigration::inspect(m_root, {m_legacyRoot}, false);
  m_error = m_plan.error; emit changed(); return result(m_error.isEmpty() && m_plan.pending(), m_plan.description());
}
QVariantMap ConfigManager::stageLegacy(const QVariantMap &choices) {
  if (!m_plan.error.isEmpty() || !m_plan.pending()) return result(false, m_plan.error);
  for (const auto &conflict : m_plan.conflicts) if (choices.value(conflict) != "current" && choices.value(conflict) != "legacy") return result(false, QStringLiteral("请先选择冲突字段：") + conflict);
  QJsonArray sources;
  for (const auto &source : m_plan.sources) sources.append(QJsonObject{{"path", source.path}, {"sha256", QString::fromLatin1(source.digest.toHex())}});
  QJsonObject request{{"schemaVersion", 1}, {"sources", sources}, {"roots", QJsonArray{m_legacyRoot}}, {"choices", QJsonObject::fromVariantMap(choices)}};
  QString error;
  if (!ConfigJson::write(QDir(m_root).filePath("import-pending.json"), request, &error)) return result(false, error);
  return result(true, QStringLiteral("旧配置导入已安排。请关闭软件，下次启动时执行；所选目录中的原件全部保留。"));
}
bool ConfigManager::applyPending(QString *error) {
  const auto restoreStatePath = QDir(m_root).filePath("restore-state.json");
  if (QFile::exists(restoreStatePath)) {
    QJsonObject state;
    if (!ConfigJson::read(restoreStatePath, &state, error)) return false;
    const auto pendingPath = QDir(m_root).filePath("restore-pending.json");
    if (state.value("phase") == "completed" && QFile::exists(pendingPath) && state.value("requestSha256").toString() == hash(bytesOf(pendingPath))) {
      if (!QFile::remove(pendingPath)) return ConfigJson::fail(error, QStringLiteral("恢复已完成，但无法清理任务"));
    } else if (state.value("phase") == "applying") {
      QLockFile recoveryLock(QDir(m_root).filePath("settings.json.lock"));
      if (!recoveryLock.tryLock(0)) return ConfigJson::fail(error, QStringLiteral("其他实例正在使用配置，不能回退恢复"));
      if (!rollbackRestore(m_root, state, error)) return false;
    }
  }
  const auto importPath = QDir(m_root).filePath("import-pending.json");
  if (QFile::exists(importPath)) {
    QJsonObject request; if (!ConfigJson::read(importPath, &request, error)) return false;
    QStringList roots;
    for (const auto &entry : request.value("roots").toArray()) roots.append(entry.toString());
    if (roots.isEmpty()) return ConfigJson::fail(error, QStringLiteral("待导入任务缺少已确认的来源目录"));
    for (const auto &entry : request.value("sources").toArray()) {
      const auto source = entry.toObject(); const auto path = source.value("path").toString();
      if (hash(bytesOf(path)) != source.value("sha256").toString()) return ConfigJson::fail(error, QStringLiteral("确认后的旧配置发生变化，请取消待导入任务后重新选择"));
    }
    auto plan = ConfigMigration::inspect(m_root, roots, false);
    const auto approved = request.value("sources").toArray();
    if (plan.sources.size() != approved.size()) return ConfigJson::fail(error, QStringLiteral("旧目录文件清单已变化，请重新预览确认"));
    for (const auto &source : plan.sources) {
      bool matched = false;
      for (const auto &entry : approved) matched |= entry.toObject().value("path") == source.path && entry.toObject().value("sha256") == QString::fromLatin1(source.digest.toHex());
      if (!matched) return ConfigJson::fail(error, QStringLiteral("旧目录文件清单已变化，请重新预览确认"));
    }
    if (!ConfigMigration::execute(plan, request.value("choices").toObject().toVariantMap(), error)) return false;
    if (!QFile::remove(importPath)) return ConfigJson::fail(error, QStringLiteral("导入成功但任务清理失败"));
  }
  const auto requestPath = QDir(m_root).filePath("restore-pending.json");
  if (!QFile::exists(requestPath)) return true;
  QJsonObject request; if (!ConfigJson::read(requestPath, &request, error)) return false;
  QJsonObject package{{"backupVersion", 1}, {"config", request.value("config")}, {"files", request.value("files")}}, validated;
  if (!decodeBackup(QJsonDocument(package).toJson(), &validated, error)) return false;
  QLockFile lock(QDir(m_root).filePath("settings.json.lock")); if (!lock.tryLock(0)) return ConfigJson::fail(error, QStringLiteral("其他实例正在使用配置"));
  QJsonObject current; if (!ConfigJson::read(QDir(m_root).filePath("settings.json"), &current, error)) return false;
  const auto backupRoot = QDir(m_root).filePath("restore-backups/" + QUuid::createUuid().toString(QUuid::WithoutBraces));
  if (!ConfigJson::write(backupRoot + "/settings.json", current, error)) return false;
  const auto incoming = request.value("config").toObject(); auto target = current;
  for (const auto &group : request.value("groups").toArray()) {
    const auto name = group.toString(); if (!incoming.contains(name) || name == "schemaVersion") return ConfigJson::fail(error, QStringLiteral("恢复分组无效"));
    target.insert(name, incoming.value(name));
  }
  if (!ConfigMigration::validate(target, error)) return false;
  QMap<QString, QByteArray> previous;
  QStringList absent;
  for (const auto &entry : request.value("files").toArray()) {
    const auto row = entry.toObject(); const auto name = row.value("path").toString(), path = QDir(m_root).filePath(name);
    if (QFileInfo(path).isSymbolicLink() || QFileInfo(QFileInfo(path).absolutePath()).isSymbolicLink()) return ConfigJson::fail(error, QStringLiteral("资源目标包含符号链接"));
    if (QFile::exists(path)) {
      QByteArray bytes; if (!readBytes(path, &bytes, error)) return false;
      previous.insert(name, bytes);
      if (!atomicBytes(backupRoot + '/' + name, previous.value(name), error)) return false;
      if (name == "ssh-known-hosts.json") {
        const auto before = QJsonDocument::fromJson(previous.value(name)).object(), after = QJsonDocument::fromJson(QByteArray::fromBase64(row.value("data").toString().toLatin1())).object();
        for (auto it = before.begin(); it != before.end(); ++it) if (after.contains(it.key()) && after.value(it.key()) != it.value()) return ConfigJson::fail(error, QStringLiteral("SSH 主机指纹冲突，停止恢复"));
      }
    } else absent.append(name);
  }
  QJsonArray restoreFiles;
  QJsonObject previousHashes;
  for (auto it = previous.begin(); it != previous.end(); ++it) previousHashes.insert(it.key(), hash(it.value()));
  for (const auto &entry : request.value("files").toArray()) {
    auto row = entry.toObject();
    if (row.value("path") == "ssh-known-hosts.json" && previous.contains("ssh-known-hosts.json")) {
      auto merged = QJsonDocument::fromJson(previous.value("ssh-known-hosts.json")).object();
      const auto imported = QJsonDocument::fromJson(QByteArray::fromBase64(row.value("data").toString().toLatin1())).object();
      for (auto it = imported.begin(); it != imported.end(); ++it) merged.insert(it.key(), it.value());
      const auto bytes = QJsonDocument(merged).toJson(QJsonDocument::Compact);
      row.insert("data", QString::fromLatin1(bytes.toBase64())); row.insert("sha256", hash(bytes));
    }
    restoreFiles.append(row);
  }
  // Persist the rollback recipe before touching the first resource; next startup retries safely.
  QJsonArray absentJson = QJsonArray::fromStringList(absent);
  QJsonObject journal{{"schemaVersion", 1}, {"requestSha256", hash(bytesOf(requestPath))}, {"snapshot", backupRoot}, {"absent", absentJson}, {"files", restoreFiles}, {"previousSha256", previousHashes}, {"targetSha256", hash(QJsonDocument(target).toJson(QJsonDocument::Compact))}, {"phase", "applying"}};
  if (!ConfigJson::write(QDir(m_root).filePath("restore-state.json"), journal, error)) return false;
  QString failure;
  for (const auto &entry : restoreFiles) {
    const auto row = entry.toObject(); const auto name = row.value("path").toString(), path = QDir(m_root).filePath(name);
    QByteArray latest;
    if (previous.contains(name)) {
      if (!readBytes(path, &latest, &failure) || latest != previous.value(name)) { failure = QStringLiteral("资源在恢复前已变化：") + name; break; }
    } else if (QFile::exists(path)) { failure = QStringLiteral("资源在恢复前被其他程序创建：") + name; break; }
    if (!atomicBytes(path, QByteArray::fromBase64(row.value("data").toString().toLatin1()), &failure)) break;
  }
  if (failure.isEmpty()) {
    QJsonObject latest;
    if (!ConfigJson::read(QDir(m_root).filePath("settings.json"), &latest, &failure) || latest != current) failure = QStringLiteral("配置在恢复前已变化");
    else ConfigJson::write(QDir(m_root).filePath("settings.json"), target, &failure);
  }
  if (!failure.isEmpty()) {
    QString rollbackError;
    const bool recovered = rollbackRestore(m_root, journal, &rollbackError);
    return ConfigJson::fail(error, failure + (recovered ? QStringLiteral("；原数据已恢复") : QStringLiteral("；回退未完成：") + rollbackError));
  }
  journal.insert("phase", "completed");
  if (!ConfigJson::write(QDir(m_root).filePath("restore-state.json"), journal, error)) return false;
  if (!QFile::remove(requestPath)) return ConfigJson::fail(error, QStringLiteral("恢复完成但待恢复任务无法清理"));
  return true;
}
