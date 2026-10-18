#pragma once

#include "MqttTypes.h"
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>

struct mosquitto;

// One instance and all its libmosquitto calls live on one dedicated Qt thread.
class MqttWorker : public QObject {
    Q_OBJECT
public:
    explicit MqttWorker(QObject *parent = nullptr);
    ~MqttWorker() override;
    static void initializeLibrary();
    void start(quint64 generation, const QVariantMap &config);
    void stop(quint64 generation);
    void publish(quint64 generation, const Mqtt::Message &message);
    void subscriptions(quint64 generation, const QVariantList &subscriptions, const QStringList &cancelled);
    void acknowledgeBatch(quint64 generation);
signals:
    void connectionChanged(quint64 generation, const QString &state, const QString &detail);
    void messagesReady(quint64 generation, const Mqtt::Messages &messages, quint64 dropped);
    void publishFinished(quint64 generation, quint64 id, const QString &delivery);
    void subscriptionChanged(quint64 generation, const QString &topic, const QString &state);
private:
    void attempt();
    void connectAddress(const QString &address);
    void poll();
    void flush();
    void destroyClient();
    void failed(QString detail, bool retry);
    void reconcileSubscriptions();
    void receive(const char *topic, const void *payload, int size, int qos, bool retained);
    mosquitto *m_client = nullptr;
    QVariantMap m_config;
    QVariantList m_subscriptions;
    QStringList m_cancelled;
    QHash<QString, int> m_requested;
    QHash<int, quint64> m_publishIds;
    QHash<int, QString> m_subscribeIds;
    QHash<int, QString> m_unsubscribeIds;
    QTimer m_pollTimer;
    QTimer m_flushTimer;
    QElapsedTimer m_clock;
    Mqtt::Messages m_pending;
    qsizetype m_pendingBytes = 0;
    quint64 m_dropped = 0;
    quint64 m_generation = 0;
    quint64 m_activity = 0;
    qint64 m_deadline = 0;
    qint64 m_retryAt = 0;
    bool m_wanted = false;
    bool m_connected = false;
    bool m_awaitingBatch = false;
    bool m_retrying = false;
    QString m_callbackError;
    bool m_retryError = true;
    bool m_resolving = false;
    int m_lookupId = -1;
    quint64 m_lookupGeneration = 0;
    QString m_certificateError;
};
