#ifndef PROTOCOLSYNCMANAGER_H
#define PROTOCOLSYNCMANAGER_H

#include <QAbstractListModel>
#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include "SecureCredentialStore.h"
#include "SmbShare.h"
#include <atomic>
#include <functional>
#include <memory>

struct SyncStatusItem {
  QString fileName;
  QDateTime localTime;
  QDateTime remoteTime;
  bool localExists = false;
  bool remoteExists = false;
  QString localVersion;
  QString localDate;
  QString remoteVersion;
  QString remoteDate;
  QString action;
  QByteArray localDigest;
  QByteArray remoteDigest;
};

class SyncStatusModel : public QAbstractListModel {
  Q_OBJECT
public:
  enum Roles {
    FileNameRole = Qt::UserRole + 1,
    LocalTimeRole,
    RemoteTimeRole,
    LocalVersionRole,
    LocalDateRole,
    RemoteVersionRole,
    RemoteDateRole,
    ActionRole,
    LocalExistsRole,
    RemoteExistsRole
  };

  explicit SyncStatusModel(QObject *parent = nullptr);
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setItems(const QList<SyncStatusItem> &items);
  QList<SyncStatusItem> items() const;

private:
  QList<SyncStatusItem> m_items;
};

class ProtocolSyncManager : public QObject {
  Q_OBJECT
  Q_PROPERTY(SyncStatusModel *statusModel READ statusModel CONSTANT)
  Q_PROPERTY(QString sharePath READ sharePath WRITE setSharePath NOTIFY
                 sharePathChanged)
  Q_PROPERTY(
      QString username READ username WRITE setUsername NOTIFY usernameChanged)
  Q_PROPERTY(
      QString password READ password WRITE setPassword NOTIFY passwordChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
  Q_PROPERTY(bool connecting READ connecting NOTIFY connectingChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(bool usesSystemSmb READ usesSystemSmb CONSTANT)
  Q_PROPERTY(bool credentialBusy READ credentialBusy NOTIFY credentialStateChanged)
  Q_PROPERTY(QString credentialError READ credentialError NOTIFY credentialStateChanged)

public:
  explicit ProtocolSyncManager(QObject *parent = nullptr);
  struct Dependencies {
    std::shared_ptr<SecureCredentialStore> credentials;
    QString settingsFile;
    std::function<QList<SmbShare::Mount>()> mounts;
    std::function<bool(const QUrl &)> openShare;
    bool promptFiles = false;
    int connectionTimeoutMs = 60000;
  };
  ProtocolSyncManager(const Dependencies &dependencies, QObject *parent = nullptr);
  ~ProtocolSyncManager() override;
  static QString legacyCredentialProfile(const QString &settingsFile, bool promptFiles = false);

  SyncStatusModel *statusModel() const;
  QString sharePath() const;
  void setSharePath(const QString &path);
  QString username() const;
  void setUsername(const QString &name);
  QString password() const;
  void setPassword(const QString &pwd);
  QString lastError() const;
  bool connecting() const;
  bool busy() const { return m_busy; }
  bool usesSystemSmb() const {
#ifdef Q_OS_MACOS
    return true;
#else
    return false;
#endif
  }
  bool credentialBusy() const { return m_credentialBusy; }
  QString credentialError() const { return m_credentialError; }
  Q_INVOKABLE void cancelConnection();
  Q_INVOKABLE void cancelPendingOperations();

  Q_INVOKABLE void connectToShare();
  Q_INVOKABLE void clearStatus();
  Q_INVOKABLE bool fetchSyncStatus();
  Q_INVOKABLE QVariantMap syncAll();
  Q_INVOKABLE QVariantMap syncFile(const QString &fileName);
  Q_INVOKABLE QString localProtocolDir() const;
  Q_INVOKABLE void saveCredentials();
  Q_INVOKABLE void loadCredentials();
  Q_INVOKABLE QVariantList getXmlDiff(const QString &fileName) const;

signals:
  void sharePathChanged();
  void usernameChanged();
  void passwordChanged();
  void lastErrorChanged();
  void statusUpdated();
  void connectingChanged();
  void busyChanged();
  void credentialStateChanged();
  void statusFinished(bool success, const QString &message);
  void syncFinished(const QVariantMap &result);
  void diffFinished(const QString &fileName, const QVariantList &diff);
  void protocolFileDownloaded(const QString &fileName);
  // 连接完成信号：success 表示是否成功，message 为提示信息
  void connectionFinished(bool success, const QString &message);

private slots:
  // 连接进程完成回调
  void onConnectProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
  // 连接超时回调
  void onConnectTimeout();

private:
  // 清理连接相关资源
  void cleanupConnectProcess();
#ifdef Q_OS_MACOS
  void startConnectionProbe(const SmbShare::Target &target, bool openIfUnavailable);
#endif

  QString localDir() const;
  QString normalizePath(const QString &path) const;
  QString filePath(const QString &base, const QString &fileName) const;
  QString expectedRootForFile(const QString &fileName) const;
  QStringList protocolFiles() const;
  bool ensureLocalDir();
  bool promptSnapshotUnchanged(const SyncStatusItem &item, QString *error) const;

  bool validateXml(const QString &filePath, QString *errorMessage, int *line,
                   int *column, QString *rootName) const;
  bool readVersionInfo(const QString &filePath, QString *version,
                       QString *date) const;
  bool validateProtocolStructure(const QString &fileName,
                                 const QString &rootName,
                                 QString *errorMessage) const;
  bool validateProtocolSemantics(const QString &fileName,
                                 const QString &filePath,
                                 QString *errorMessage) const;
  int compareByMetadata(const SyncStatusItem &item) const;
  int compareVersion(const QString &localVersion,
                     const QString &remoteVersion) const;
  bool uploadFile(const SyncStatusItem &item, QString *errorMessage);
  bool downloadFile(const SyncStatusItem &item, QString *errorMessage);
  void setLastError(const QString &message);
  bool fetchSyncStatusNow();
  QVariantMap syncAllNow();
  QVariantMap syncFileNow(const QString &fileName);
  QVariantList getXmlDiffNow(const QString &fileName, QString *error = nullptr) const;
  void startOperation(const QString &kind, const QString &fileName = {});
  QString remoteDir() const;
  struct WorkerTag {};
  ProtocolSyncManager(WorkerTag);

  // XML 差异比较辅助方法
  QVariantList buildLcsDiff(const QStringList &localLines,
                            const QStringList &remoteLines) const;

  SyncStatusModel *m_statusModel;
  QString m_sharePath;
  QString m_username;
  QString m_password;
  QString m_lastError;

  // 异步连接相关
  QProcess *m_connectProcess = nullptr;
  QTimer *m_connectTimer = nullptr;
  bool m_connecting = false;
  bool m_busy = false;
  bool m_isWorker = false;
  bool m_connectionProbeRunning = false;
  bool m_credentialBusy = false;
  QString m_credentialError;
  Dependencies m_dependencies;
  QString m_resolvedSharePath;
  quint64 m_generation = 0;
  std::shared_ptr<std::atomic_bool> m_connectionCancelled;
  std::shared_ptr<std::atomic_bool> m_operationCancelled;
};

#endif // PROTOCOLSYNCMANAGER_H
