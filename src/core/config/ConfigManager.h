#pragma once
#include "ConfigMigration.h"
#include <QObject>
#include <QUrl>

class ConfigManager : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString summary READ summary NOTIFY changed)
  Q_PROPERTY(QString migrationDescription READ migrationDescription NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QStringList conflicts READ conflicts NOTIFY changed)
  Q_PROPERTY(QVariantList conflictDetails READ conflictDetails NOTIFY changed)
  Q_PROPERTY(bool pending READ pending NOTIFY changed)
  Q_PROPERTY(bool canMigrate READ canMigrate NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
  explicit ConfigManager(const QString &root, QObject *parent = nullptr);
  QString summary() const;
  QString migrationDescription() const { return m_plan.description(); }
  QString error() const { return m_error; }
  QStringList conflicts() const { return m_plan.conflicts; }
  QVariantList conflictDetails() const;
  bool pending() const { return m_plan.pending(); }
  bool canMigrate() const { return m_plan.pending() && m_plan.error.isEmpty(); }
  bool busy() const { return m_busy; }
  void inspectStartup(const QStringList &roots);
  Q_INVOKABLE void migrate(const QVariantMap &choices);
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void openDirectory();
  Q_INVOKABLE QVariantMap cancelPending();
  Q_INVOKABLE QVariantMap exportBackup(const QUrl &url, bool resources, bool workspace, bool trust);
  Q_INVOKABLE QVariantMap previewRestore(const QUrl &url);
  Q_INVOKABLE QVariantMap stageRestore(const QStringList &groups, bool resources, bool trust);
  Q_INVOKABLE QVariantMap previewLegacy(const QUrl &url);
  Q_INVOKABLE QVariantMap stageLegacy(const QVariantMap &choices);
  bool applyPending(QString *error);
  static bool decodeBackup(const QByteArray &bytes, QJsonObject *package, QString *error);
signals:
  void changed();
  void migrationFinished(bool success);
private:
  QVariantMap result(bool ok, const QString &error = {}) const;
  QString m_root, m_error, m_legacyRoot;
  ConfigMigration::Plan m_plan;
  bool m_busy = false;
  QJsonObject m_restore;
  QByteArray m_previewConfigDigest;
};
