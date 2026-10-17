#pragma once

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

// App-owned trust records: an existing endpoint can never be overwritten by TOFU.
class SshHostKeyStore {
public:
  enum Result { Unknown, Trusted, Changed, Error };
  explicit SshHostKeyStore(QString path) : m_path(std::move(path)) {}
  static QString endpoint(const QString &host, int port) {
    return QStringLiteral("[%1]:%2").arg(host.trimmed().toLower()).arg(port);
  }
  static QString fingerprint(const QByteArray &key) {
    return QStringLiteral("SHA256:") + QString::fromLatin1(
        QCryptographicHash::hash(key, QCryptographicHash::Sha256)
            .toBase64(QByteArray::OmitTrailingEquals));
  }
  Result check(const QString &host, int port, const QByteArray &key,
               QString *error = nullptr) const {
    QJsonObject records;
    if (!read(&records, error)) return Error;
    const QString id = endpoint(host, port);
    if (!records.contains(id)) return Unknown;
    const auto value = records.value(id);
    if (!value.isString() || value.toString().isEmpty()) {
      if (error) *error = QStringLiteral("SSH 信任记录损坏，需人工检查");
      return Error;
    }
    return value.toString() == fingerprint(key) ? Trusted : Changed;
  }
  bool trust(const QString &host, int port, const QByteArray &key,
             QString *error = nullptr) {
    QLockFile lock(m_path + QStringLiteral(".lock"));
    if (!lock.tryLock(0)) {
      if (error) *error = lock.error() == QLockFile::LockFailedError
          ? QStringLiteral("SSH 信任记录正被使用，请重试")
          : QStringLiteral("无法创建 SSH 信任记录锁，请检查便携目录路径和写入权限：") + m_path;
      return false;
    }
    QJsonObject records;
    if (!read(&records, error)) return false;
    const QString id = endpoint(host, port);
    const QString fp = fingerprint(key);
    if (records.contains(id) && records.value(id) != fp) {
      if (error) *error = QStringLiteral("SSH 主机密钥已变化，禁止覆盖信任记录");
      return false;
    }
    records.insert(id, fp);
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
      if (error) *error = file.errorString();
      return false;
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
      if (error) *error = QStringLiteral("无法设置 SSH 信任文件权限：") + m_path;
      file.cancelWriting();
      return false;
    }
    const QByteArray json = QJsonDocument(records).toJson(QJsonDocument::Compact);
    if (file.write(json) != json.size() || !file.commit()) {
      if (error) *error = file.errorString();
      return false;
    }
    return true;
  }
private:
  bool read(QJsonObject *records, QString *error) const {
    QFile file(m_path);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly)) {
      if (error) *error = file.errorString();
      return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
      if (error) *error = QStringLiteral("SSH 信任记录损坏，需人工检查");
      return false;
    }
    *records = document.object();
    return true;
  }
  QString m_path;
};
