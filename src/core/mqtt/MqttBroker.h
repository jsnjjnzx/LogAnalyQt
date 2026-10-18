#pragma once

#include "MqttTypes.h"
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

class MqttBroker : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap configuration READ configuration NOTIFY configurationChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString detail READ detail NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(QVariantList interfaces READ interfaces NOTIFY interfacesChanged)
    Q_PROPERTY(QVariantList clients READ clients NOTIFY clientsChanged)
    Q_PROPERTY(QStringList events READ events NOTIFY eventsChanged)
    Q_PROPERTY(QString connectionAddress READ connectionAddress NOTIFY configurationChanged)
public:
    explicit MqttBroker(const QString &dataRoot, QObject *parent = nullptr, const QString &helperOverride = {});
    ~MqttBroker() override;
    QVariantMap configuration() const { return m_config; }
    QString configure(const QVariantMap &config);
    QString state() const { return m_state; }
    QString detail() const { return m_detail; }
    bool active() const { return m_state == "starting" || m_state == "running" || m_state == "stopping"; }
    bool running() const { return m_state == "running"; }
    QVariantList interfaces() const { return m_interfaces; }
    QVariantList clients() const;
    QStringList events() const { return m_events; }
    QString connectionAddress() const;
    QVariantMap localClientConfiguration(const QVariantMap &previous);
    qint64 processId() const { return m_process.processId(); }
    Q_INVOKABLE QVariantMap start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refreshInterfaces();
signals:
    void configurationChanged();
    void stateChanged();
    void interfacesChanged();
    void clientsChanged();
    void eventsChanged();
private:
    QString prepareRuntime();
    QString helperPath(QString *error);
    void readOutput();
    void processLine(const QString &line);
    void setState(const QString &state, const QString &detail);
    QString m_dataRoot;
    QString m_helperOverride;
    QString m_localClientId;
    QVariantMap m_config;
    QVariantList m_interfaces;
    QMap<QString, QVariantMap> m_clients;
    QStringList m_events;
    QString m_state = "stopped";
    QString m_detail = QStringLiteral("未启动");
    QProcess m_process;
    QTimer m_startTimer;
    QByteArray m_output;
    std::unique_ptr<QTemporaryDir> m_runtime;
    quint64 m_run = 0;
};
