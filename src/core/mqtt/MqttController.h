#pragma once

#include "MqttBroker.h"
#include "MqttSession.h"
#include <functional>
#include <QJsonObject>
#include <memory>
#include "../SecureCredentialStore.h"

class MqttController : public QObject {
    Q_OBJECT
    Q_PROPERTY(MqttSession *remote READ remote CONSTANT)
    Q_PROPERTY(MqttSession *localClient READ localClient CONSTANT)
    Q_PROPERTY(MqttBroker *broker READ broker CONSTANT)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString currentProfileId READ currentProfileId NOTIFY profilesChanged)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)
    Q_PROPERTY(QString storageError READ storageError NOTIFY storageChanged)
    Q_PROPERTY(QVariantList commonSubscriptions READ commonSubscriptions NOTIFY commonSubscriptionsChanged)
    Q_PROPERTY(QString commonSubscriptionsPath READ commonSubscriptionsPath CONSTANT)
    Q_PROPERTY(QString commonSubscriptionsError READ commonSubscriptionsError NOTIFY commonSubscriptionsChanged)
    Q_PROPERTY(double splitRatio READ splitRatio WRITE setSplitRatio NOTIFY layoutChanged)
public:
    explicit MqttController(QObject *parent = nullptr);
    MqttController(const QString &dataRoot, QObject *parent, const QString &helperOverride = {}, std::shared_ptr<SecureCredentialStore> credentials = {});
    ~MqttController() override;
    MqttSession *remote() { return &m_remote; }
    MqttSession *localClient() { return &m_local; }
    MqttBroker *broker() { return &m_broker; }
    QVariantList profiles() const;
    QString currentProfileId() const { return m_remote.configuration().value("id").toString(); }
    QString configPath() const { return m_configPath; }
    QString storageError() const { return m_storageError; }
    QVariantList commonSubscriptions() const { return m_commonSubscriptions; }
    QString commonSubscriptionsPath() const { return m_commonPath; }
    QString commonSubscriptionsError() const { return m_commonError; }
    double splitRatio() const { return m_splitRatio; }
    void setSplitRatio(double ratio);
    static QString validateWorkspace(const QVariantMap &config);
    Q_INVOKABLE QVariantMap newConnection() const;
    Q_INVOKABLE QVariantMap saveConnection(const QVariantMap &config);
    Q_INVOKABLE QVariantMap selectConnection(const QString &id);
    Q_INVOKABLE QVariantMap copyConnection(const QString &id);
    Q_INVOKABLE QVariantMap removeConnection(const QString &id);
    Q_INVOKABLE QVariantMap saveBroker(const QVariantMap &config);
    Q_INVOKABLE QVariantMap connectLocalClient();
    Q_INVOKABLE QVariantMap saveNow();
    Q_INVOKABLE QVariantMap reloadCommonSubscriptions();
    Q_INVOKABLE QVariantMap saveCommonSubscription(const QVariantMap &subscription, const QString &previousTopic = {});
    Q_INVOKABLE QVariantMap removeCommonSubscription(const QString &topic);
    Q_INVOKABLE QVariantMap useCommonSubscription(const QString &topic, bool local);
    Q_INVOKABLE QVariantMap openCommonSubscriptionsFile();
    Q_INVOKABLE QString filePath(const QUrl &url) const { return url.isLocalFile() ? url.toLocalFile() : QString(); }
    Q_INVOKABLE QString thirdPartyNotices() const;
signals:
    void profilesChanged();
    void storageChanged();
    void layoutChanged();
    void commonSubscriptionsChanged();
private:
    void load();
    void scheduleSave();
    static QVariantMap normalizedConnection(const QVariantMap &input);
    QVariantMap storageResult(const QString &error);
    QString readCommonSubscriptions(QVariantList *subscriptions, QByteArray *bytes) const;
    QVariantMap changeCommonSubscriptions(const std::function<QString(QVariantList &)> &change);
    QVariantMap commonResult(const QString &error, bool listChanged = false);
    void rememberCommonSubscription(const QVariantMap &subscription);
    static QString normalizeCommonSubscription(QVariantMap *subscription, int index);
    MqttSession m_remote;
    MqttSession m_local;
    MqttBroker m_broker;
    QVariantList m_profiles;
    QJsonObject m_workspaceSnapshot;
    QString m_configPath;
    QString m_storageError;
    QString m_commonPath;
    QString m_commonError;
    QVariantList m_commonSubscriptions;
    QTimer m_saveTimer;
    std::shared_ptr<SecureCredentialStore> m_credentials;
    bool m_saving = false;
    bool m_loading = true;
    bool m_dirty = false;
    bool m_badFile = false;
    double m_splitRatio = 0.56;
};
