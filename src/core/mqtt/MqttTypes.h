#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QVariantMap>
#include <QVector>

namespace Mqtt {
constexpr qsizetype MaxPayloadBytes = 1024 * 1024;
constexpr qsizetype MaxConfigBytes = 8 * 1024 * 1024;
constexpr qsizetype MaxQueueBytes = 8 * 1024 * 1024;
constexpr int MaxQueueMessages = 2000;
constexpr int MaxHistory = 20;

QVariantMap connectionDefaults(const QString &id = {});
QVariantMap brokerDefaults();
QString validateConnection(const QVariantMap &config, bool checkFiles = false);
QString validateBroker(const QVariantMap &config, bool checkFiles = false);
QString validateSubscription(const QString &topic, int qos);
QString decodePayload(const QString &text, const QString &format, QByteArray *bytes);
QString formatPayload(const QByteArray &bytes, const QString &format);
QString publishError(const QString &topic, int qos);
QVariantMap result(const QString &error = {});

struct Message {
    quint64 id = 0;
    QDateTime time;
    QString topic;
    QByteArray payload;
    int qos = 0;
    bool retained = false;
    bool received = true;
    QString delivery;
};
using Messages = QVector<Message>;
}
Q_DECLARE_METATYPE(Mqtt::Message)
Q_DECLARE_METATYPE(Mqtt::Messages)
