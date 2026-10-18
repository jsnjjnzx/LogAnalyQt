#pragma once
#include "../SecureCredentialStore.h"
#include "../../common/JsonSettings.h"
#include <QCryptographicHash>
#include <memory>

struct ConfigMigration {
  struct Source { QString path; QByteArray digest; QJsonObject converted; bool deletable = false; };
  struct Secret { SecureCredentialStore::Domain domain; QString profile; QByteArray bytes; };
  struct Plan {
    QString root, error;
    QJsonObject original, incoming;
    QList<Source> sources;
    QList<Secret> secrets;
    QVariantMap deviceState;
    QStringList conflicts, notes;
    bool pending() const { return !sources.isEmpty(); }
    QString description() const;
  };
  using Stores = std::function<std::shared_ptr<SecureCredentialStore>(SecureCredentialStore::Domain)>;
  static Plan inspect(const QString &root, const QStringList &sources = {}, bool allowDelete = true);
  static bool execute(const Plan &plan, const QVariantMap &choices, QString *error, const Stores &stores = {});
  static bool validate(const QJsonObject &root, QString *error);
  static QByteArray digest(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
};
