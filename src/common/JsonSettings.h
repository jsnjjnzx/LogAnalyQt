#pragma once

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <functional>

// All writers reload under the same lock; a module never replaces another module's snapshot.
namespace ConfigJson {
inline constexpr qint64 MaxBytes = 32 * 1024 * 1024;
inline bool fail(QString *error, const QString &message) {
  if (error) *error = message;
  return false;
}
inline bool read(const QString &path, QJsonObject *root, QString *error = nullptr) {
  QFile file(path);
  if (!file.exists()) { *root = {{"schemaVersion", 1}}; return true; }
  if (QFileInfo(path).isSymbolicLink() || !file.open(QIODevice::ReadOnly) || file.size() > MaxBytes)
    return fail(error, QStringLiteral("配置无法读取、是符号链接或超过 32 MiB：") + path);
  QJsonParseError parse;
  auto document = QJsonDocument::fromJson(file.readAll(), &parse);
  if (parse.error != QJsonParseError::NoError || !document.isObject() || document.object().value("schemaVersion") != 1)
    return fail(error, QStringLiteral("配置格式损坏或版本不受支持，原文件未覆盖：") + path);
  *root = document.object();
  for (const auto &key : {"ui", "parser", "mqtt", "ssh", "ai", "sync", "messages", "workspace", "resources"})
    if (root->contains(key) && !root->value(key).isObject()) return fail(error, QStringLiteral("配置分组类型无效：") + key);
  if (root->contains("framingTemplates") && !root->value("framingTemplates").isObject())
    return fail(error, QStringLiteral("组帧模板必须是按名称索引的对象"));
  return true;
}
inline bool write(const QString &path, const QJsonObject &root, QString *error = nullptr) {
  if (QFileInfo(path).isSymbolicLink()) return fail(error, QStringLiteral("拒绝写入配置符号链接"));
  const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
  if (bytes.size() > MaxBytes || !QDir().mkpath(QFileInfo(path).absolutePath())) return fail(error, QStringLiteral("配置过大或目录不可写"));
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
      file.write(bytes) != bytes.size() || !file.commit()) return fail(error, QStringLiteral("配置保存失败：") + file.errorString());
  QJsonObject saved;
  return (read(path, &saved, error) && saved == root) || fail(error, QStringLiteral("配置回读验证失败，禁止清理旧文件"));
}
inline bool update(const QString &path, const std::function<bool(QJsonObject &, QString *)> &change, QString *error = nullptr) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) return fail(error, QStringLiteral("无法创建配置目录"));
  QLockFile lock(path + ".lock");
  if (!lock.tryLock(1000)) return fail(error, QStringLiteral("配置正在被其他实例修改，请重试"));
  QJsonObject root;
  if (!read(path, &root, error)) return false;
  const auto before = root;
  if (!change(root, error)) return false;
  QJsonObject latest;
  if (!read(path, &latest, error) || latest != before) return fail(error, QStringLiteral("配置已被外部修改，请重新加载"));
  if (root == before && QFile::exists(path)) return true;
  if (QFile::exists(path) && !write(path + ".previous", before, error)) return false;
  return write(path, root, error);
}
inline QJsonValue get(QJsonObject object, const QStringList &keys) {
  for (qsizetype i = 0; i < keys.size(); ++i) {
    const auto value = object.value(keys[i]);
    if (i == keys.size() - 1) return value;
    object = value.toObject();
  }
  return QJsonValue(QJsonValue::Undefined);
}
inline void put(QJsonObject &object, const QStringList &keys, const QJsonValue &value) {
  if (keys.isEmpty()) return;
  if (keys.size() == 1) { if (value.isUndefined()) object.remove(keys.first()); else object.insert(keys.first(), value); return; }
  auto child = object.value(keys.first()).toObject();
  put(child, keys.mid(1), value); object.insert(keys.first(), child);
}
inline QString canonicalKey(QString key) {
  if (QStringList{"ssh/host", "ssh/port", "ssh/username"}.contains(key)) return "ssh/connection/" + key.mid(4);
  if (QStringList{"ssh/encoding", "ssh/maxLines", "ssh/autoReconnectEnabled", "ssh/reconnectMaxAttempts", "ssh/reconnectIntervalMs"}.contains(key)) return "ssh/terminal/" + key.mid(4);
  if (key == "protocolType") return "parser/protocolType";
  if (key.startsWith("aiFraming/")) return "ai/" + key.mid(10);
  if (key.startsWith("protocolSync/")) return "sync/protocol/" + key.mid(13);
  if (key.startsWith("promptSync/")) return "sync/prompt/" + key.mid(11);
  if (key == "ui/liveLog/customCommands") return "ssh/customCommands";
  if (key == "ui/next/multiParse/workspace") return "workspace/parse";
  if (key == "ui/next/multiParse/recovery") return "workspace/recovery";
  return key;
}
}

class JsonSettings {
public:
  explicit JsonSettings(QString path) : m_path(std::move(path)) {}
  QString fileName() const { return m_path; }
  QVariant value(const QString &key, const QVariant &fallback = {}) const {
    QJsonObject root;
    if (!ConfigJson::read(m_path, &root, &m_error)) { m_status = QSettings::FormatError; return fallback; }
    const auto canonical = ConfigJson::canonicalKey(key);
    auto value = m_pending.contains(canonical) ? m_pending.value(canonical) : ConfigJson::get(root, canonical.split('/'));
    if (value.isUndefined()) return fallback;
    if (key == "ui/liveLog/customCommands" && value.isArray()) return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    if (key == "ui/next/multiParse/workspace" && value.isObject()) return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    return value.toVariant();
  }
  bool contains(const QString &key) const { return value(key).isValid(); }
  void setValue(const QString &key, const QVariant &value) {
    QJsonValue json = QJsonValue::fromVariant(value);
    if (key == "ui/liveLog/customCommands" || key == "ui/next/multiParse/workspace") {
      QJsonParseError parse; auto doc = QJsonDocument::fromJson(value.toString().toUtf8(), &parse);
      if (parse.error != QJsonParseError::NoError || (key.endsWith("customCommands") ? !doc.isArray() : !doc.isObject())) {
        m_status = QSettings::FormatError; m_error = QStringLiteral("工作区或快捷命令格式无效"); return;
      }
      json = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
    }
    m_pending.insert(ConfigJson::canonicalKey(key), json);
  }
  void remove(const QString &key) { m_pending.insert(ConfigJson::canonicalKey(key), QJsonValue(QJsonValue::Undefined)); }
  void sync() {
    if (m_status != QSettings::NoError || m_pending.isEmpty()) return;
    if (!ConfigJson::update(m_path, [this](QJsonObject &root, QString *) {
      for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) ConfigJson::put(root, it.key().split('/'), it.value());
      return true;
    }, &m_error)) { m_status = QSettings::AccessError; return; }
    m_pending.clear();
  }
  QSettings::Status status() const { return m_status; }
  QString errorString() const { return m_error; }
private:
  QString m_path;
  QMap<QString, QJsonValue> m_pending;
  mutable QSettings::Status m_status = QSettings::NoError;
  mutable QString m_error;
};
