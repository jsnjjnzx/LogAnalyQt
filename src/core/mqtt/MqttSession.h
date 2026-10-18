#pragma once

#include "MqttMessageModel.h"
#include "MqttWorker.h"
#include <QFutureWatcher>
#include <QThread>
#include <QUrl>
#include <deque>

class MqttSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap configuration READ configuration NOTIFY configurationChanged)
    Q_PROPERTY(QVariantList subscriptions READ subscriptions NOTIFY subscriptionsChanged)
    Q_PROPERTY(QVariantMap draft READ draft NOTIFY configurationChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY configurationChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString detail READ detail NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QAbstractItemModel *messages READ messages CONSTANT)
    Q_PROPERTY(int messageCount READ messageCount NOTIFY messagesChanged)
    Q_PROPERTY(qulonglong receivedCount READ receivedCount NOTIFY messagesChanged)
    Q_PROPERTY(qulonglong sentCount READ sentCount NOTIFY messagesChanged)
    Q_PROPERTY(qulonglong evictedCount READ evictedCount NOTIFY messagesChanged)
    Q_PROPERTY(qulonglong droppedCount READ droppedCount NOTIFY messagesChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY messagesChanged)
    Q_PROPERTY(bool exportBusy READ exportBusy NOTIFY exportBusyChanged)
public:
    explicit MqttSession(QObject *parent = nullptr);
    ~MqttSession() override;
    QVariantMap configuration() const { return m_config; }
    QString configure(const QVariantMap &config);
    QVariantList subscriptions() const;
    QVariantMap draft() const { return m_config.value("draft").toMap(); }
    QVariantList history() const { return m_config.value("history").toList(); }
    QString state() const { return m_state; }
    QString detail() const { return m_detail; }
    bool connected() const { return m_state == "connected"; }
    bool active() const { return m_state == "connecting" || m_state == "connected" || m_state == "reconnecting" || m_state == "disconnecting"; }
    QString notice() const { return m_notice; }
    QAbstractItemModel *messages() { return &m_filter; }
    int messageCount() const { return m_filter.rowCount(); }
    qulonglong receivedCount() const { return m_received; }
    qulonglong sentCount() const { return m_sent; }
    qulonglong evictedCount() const { return m_model.evictedCount(); }
    qulonglong droppedCount() const { return m_dropped; }
    bool paused() const { return m_paused; }
    int pendingCount() const { return static_cast<int>(m_pending.size()); }
    bool exportBusy() const { return m_exportWatcher.isRunning(); }
    Q_INVOKABLE QVariantMap connectToBroker();
    Q_INVOKABLE void disconnectFromBroker();
    Q_INVOKABLE QVariantMap publishMessage(const QString &topic, const QString &text, const QString &format, int qos, bool retain);
    Q_INVOKABLE QVariantMap saveSubscription(const QString &topic, int qos, bool enabled = true, const QString &previousTopic = {});
    Q_INVOKABLE QVariantMap removeSubscription(const QString &topic);
    Q_INVOKABLE QVariantMap setFilter(const QString &direction, const QString &topic, const QString &content);
    Q_INVOKABLE QVariantMap message(qulonglong id, const QString &format) const;
    Q_INVOKABLE qulonglong idAt(int row) const;
    Q_INVOKABLE void copyMessage(qulonglong id, const QString &format);
    Q_INVOKABLE QVariantMap updateDraft(const QVariantMap &draft);
    Q_INVOKABLE QVariantMap exportMessages(const QUrl &url, bool filtered = true);
    Q_INVOKABLE void clearMessages();
    void setPaused(bool paused);
signals:
    void configurationChanged();
    void subscriptionsChanged();
    void stateChanged();
    void messagesChanged();
    void noticeChanged();
    void pausedChanged();
    void exportBusyChanged();
private:
    QVariantMap report(const QString &error);
    void acceptMessages(Mqtt::Messages messages);
    void setDelivery(quint64 id, const QString &delivery);
    void sendSubscriptions();
    MqttMessageModel m_model;
    MqttMessageFilter m_filter;
    QThread m_thread;
    MqttWorker *m_worker;
    QVariantMap m_config;
    QHash<QString, QString> m_subscriptionStates;
    QString m_state = "idle";
    QString m_detail = QStringLiteral("未连接");
    QString m_notice;
    quint64 m_generation = 0;
    quint64 m_nextId = 1;
    quint64 m_received = 0;
    quint64 m_sent = 0;
    quint64 m_dropped = 0;
    bool m_paused = false;
    std::deque<Mqtt::Message> m_pending;
    qsizetype m_pendingBytes = 0;
    QFutureWatcher<QString> m_exportWatcher;
};
