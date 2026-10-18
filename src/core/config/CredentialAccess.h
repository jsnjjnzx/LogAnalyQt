#pragma once
#include "../SecureCredentialStore.h"
#include "../../common/JsonSettings.h"
#include <QCryptographicHash>
#include <QEventLoop>
#include <QCoreApplication>
#include <QJsonObject>
#include <QVariantMap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <functional>
#include <atomic>
#include <memory>

namespace CredentialAccess {
inline QString profile(const QByteArray &identity) {
  return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}
inline QString sshProfile(const QString &host, int port, const QString &user) {
  return profile(QJsonDocument(QJsonArray{host.trimmed().toLower(), port, user}).toJson(QJsonDocument::Compact));
}
// Native credential UI runs on a worker while the owning event loop stays responsive.
inline bool run(const std::function<bool(QString *)> &operation, QString *error) {
  if (QThread::currentThread() != qApp->thread()) return operation(error);
  struct State { std::atomic_bool finished{false}; bool result = false; QString detail; };
  auto state = std::make_shared<State>();
  QEventLoop loop;
  auto *thread = QThread::create([state, operation] {
    state->result = operation(&state->detail); state->finished.store(true, std::memory_order_release);
  });
  QObject::connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
  QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start(); loop.exec();
  if (!state->finished.load(std::memory_order_acquire)) return ConfigJson::fail(error, QStringLiteral("凭据操作尚未结束，配置未提交；请稍后重新保存"));
  if (error) *error = state->detail;
  return state->result;
}
inline QString mqttProfile(const QVariantMap &config) {
  return profile(QJsonDocument(QJsonArray{"mqtt", config.value("id", "broker").toString(), config.value("host").toString().trimmed().toLower(), config.value("port").toInt(), config.value("username").toString()}).toJson(QJsonDocument::Compact));
}
inline bool loadMqtt(QVariantMap *config, QString *error, std::shared_ptr<SecureCredentialStore> provided = {}) {
  const auto id = config->value("credentialProfile").toString();
  if (id.isEmpty() || !config->value("password").toString().isEmpty()) return true;
  if (id != mqttProfile(*config)) return ConfigJson::fail(error, QStringLiteral("MQTT 连接身份已变化，请重新填写并保存密码"));
  auto bytes = std::make_shared<QByteArray>();
  const bool ok = run([id, bytes, provided](QString *e) { SecureCredentialStore native(SecureCredentialStore::Domain::Mqtt); return (provided ? provided.get() : &native)->read(id, bytes.get(), e); }, error);
  if (ok && bytes->isEmpty()) return ConfigJson::fail(error, QStringLiteral("系统中未找到该 MQTT 密码，请重新填写并保存"));
  if (ok) config->insert("password", QString::fromUtf8(*bytes));
  if (ok) bytes->fill(0); return ok;
}
inline bool sealMqtt(QJsonObject *config, QString *error, std::shared_ptr<SecureCredentialStore> provided = {}) {
  const auto password = config->value("password").toString().toUtf8();
  if (!password.isEmpty()) {
    const auto id = mqttProfile(config->toVariantMap());
    if (!run([id, password, provided](QString *e) { SecureCredentialStore native(SecureCredentialStore::Domain::Mqtt); return (provided ? provided.get() : &native)->writeVerified(id, password, e); }, error)) return false;
    config->insert("credentialProfile", id);
  }
  if (password.isEmpty() && config->value("credentialProfile").toString() != mqttProfile(config->toVariantMap())) config->remove("credentialProfile");
  config->remove("password"); return true;
}
}
