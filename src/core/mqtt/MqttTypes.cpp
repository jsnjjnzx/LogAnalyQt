#include "MqttTypes.h"

#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>
#include <mosquitto.h>

namespace {
bool boundedInteger(const QVariantMap &c, const char *key, int low, int high) {
    bool ok = false;
    const double value = c.value(QLatin1String(key)).toDouble(&ok);
    return ok && std::isfinite(value) && value == std::floor(value) && value >= low && value <= high;
}
bool validString(const QString &text, int limit = 65535) {
    return !text.contains(QChar::Null) && text.toUtf8().size() <= limit;
}
QString certificates(const QVariantMap &c, bool broker, bool checkFiles) {
    if (!c.value("tls").toBool()) return {};
    const auto cert = c.value("certFile").toString();
    const auto key = c.value("keyFile").toString();
    if (cert.isEmpty() != key.isEmpty() || (broker && cert.isEmpty()))
        return QStringLiteral("证书与私钥必须同时配置。");
    if (!broker && c.value("caFile").toString().isEmpty())
        return QStringLiteral("TLS 连接请配置 CA 证书文件。");
    for (const auto *name : {"caFile", "certFile", "keyFile"}) {
        const auto path = c.value(QLatin1String(name)).toString();
        if (!validString(path, 4096) || path.contains('\n') || path.contains('\r'))
            return QStringLiteral("证书路径包含无效字符。");
        if (checkFiles && !path.isEmpty()) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("无法读取证书文件：%1").arg(path);
        }
    }
    return {};
}
}

QVariantMap Mqtt::connectionDefaults(const QString &id) {
    const auto uuid = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
    return {{"id", uuid}, {"name", QStringLiteral("新建 MQTT 连接")},
        {"host", "127.0.0.1"}, {"port", 1883},
        {"clientId", "loganalyqt_" + uuid.left(8)}, {"username", ""}, {"password", ""},
        {"protocol", 4}, {"tls", false}, {"caFile", ""}, {"certFile", ""}, {"keyFile", ""},
        {"keepAlive", 60}, {"timeoutSeconds", 10}, {"autoReconnect", true}, {"reconnectSeconds", 3},
        {"cleanSession", true}, {"sessionExpiry", 0}, {"willEnabled", false},
        {"willTopic", ""}, {"willPayload", ""}, {"willFormat", "Text"},
        {"willQos", 0}, {"willRetain", false}, {"subscriptions", QVariantList{}}, {"cancelledSubscriptions", QStringList{}},
        {"draft", QVariantMap{{"topic", ""}, {"payload", ""}, {"format", "Text"}, {"qos", 0}, {"retain", false}}},
        {"history", QVariantList{}}};
}

QVariantMap Mqtt::brokerDefaults() {
    return {{"host", "0.0.0.0"}, {"port", 1883}, {"anonymous", true},
        {"username", ""}, {"password", ""}, {"tls", false}, {"caFile", ""},
        {"certFile", ""}, {"keyFile", ""}, {"connectLocal", true}};
}

QString Mqtt::validateConnection(const QVariantMap &c, bool checkFiles) {
    const QString host = c.value("host").toString();
    const QString name = c.value("name").toString();
    const QString clientId = c.value("clientId").toString();
    if (name.trimmed().isEmpty() || !validString(name, 256)) return QStringLiteral("请填写连接名称（不超过 256 字节）。");
    static const QRegularExpression hostPattern(QStringLiteral("^[^\\s/\\\\?#@]+$"));
    if (host.isEmpty() || !validString(host, 253) || !hostPattern.match(host).hasMatch()
        || (host.contains(':') && QHostAddress(host).isNull()))
        return QStringLiteral("请填写主机名或 IP 地址，协议与端口在各自选项中设置。");
    if (!boundedInteger(c, "port", 1, 65535)) return QStringLiteral("端口必须为 1–65535 的整数。");
    if (clientId.isEmpty() || !validString(clientId, 256)) return QStringLiteral("请填写有效的 Client ID（不超过 256 字节）。");
    if (!validString(c.value("username").toString()) || !validString(c.value("password").toString()))
        return QStringLiteral("用户名或密码过长，或包含空字符。");
    if (!boundedInteger(c, "protocol", 4, 5))
        return QStringLiteral("协议版本请选择 MQTT 3.1.1 或 5.0。");
    if (!boundedInteger(c, "keepAlive", 5, 65535)) return QStringLiteral("Keep Alive 必须为 5–65535 秒。");
    if (!boundedInteger(c, "timeoutSeconds", 1, 120)) return QStringLiteral("连接超时必须为 1–120 秒。");
    if (!boundedInteger(c, "reconnectSeconds", 1, 300)) return QStringLiteral("重连间隔必须为 1–300 秒。");
    if (!boundedInteger(c, "sessionExpiry", 0, 86400)) return QStringLiteral("会话有效期必须为 0–86400 秒。");
    if (c.value("willEnabled").toBool()) {
        if (!boundedInteger(c, "willQos", 0, 2)) return QStringLiteral("遗嘱 QoS 必须为 0、1 或 2。");
        if (auto error = publishError(c.value("willTopic").toString(), c.value("willQos").toInt()); !error.isEmpty()) return error;
        QByteArray bytes;
        if (auto error = decodePayload(c.value("willPayload").toString(), c.value("willFormat").toString(), &bytes); !error.isEmpty()) return error;
    }
    return certificates(c, false, checkFiles);
}

QString Mqtt::validateBroker(const QVariantMap &c, bool checkFiles) {
    if (QHostAddress(c.value("host").toString()).isNull()) return QStringLiteral("请选择有效的监听 IP 地址。");
    if (!boundedInteger(c, "port", 1, 65535)) return QStringLiteral("监听端口必须为 1–65535 的整数。");
    const auto user = c.value("username").toString();
    const auto pass = c.value("password").toString();
    if (!c.value("anonymous").toBool() && (user.isEmpty() || pass.isEmpty()))
        return QStringLiteral("关闭匿名连接时，请填写用户名和密码。");
    if (!validString(user, 256) || user.contains(':') || user.contains('\n') || user.contains('\r') || !validString(pass))
        return QStringLiteral("用户名含无效字符或凭据过长。");
    return certificates(c, true, checkFiles);
}

QString Mqtt::publishError(const QString &topic, int qos) {
    if (topic.isEmpty() || !validString(topic) || mosquitto_pub_topic_check(topic.toUtf8().constData()) != MOSQ_ERR_SUCCESS)
        return QStringLiteral("发布主题不能为空，也不能包含 +、# 通配符。");
    if (qos < 0 || qos > 2) return QStringLiteral("QoS 必须为 0、1 或 2。");
    return {};
}

QString Mqtt::defaultSubscriptionColor(int index) {
    static const QStringList colors{"#60a5fa", "#34d399", "#fbbf24", "#f472b6", "#a78bfa", "#22d3ee", "#fb923c", "#f87171", "#a3e635", "#2dd4bf", "#818cf8", "#fb7185"};
    return colors.at(index % colors.size());
}

QString Mqtt::validateSubscriptionAppearance(const QVariantMap &subscription) {
    static const QRegularExpression colorPattern(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    if (subscription.contains("color") && !colorPattern.match(subscription.value("color").toString()).hasMatch())
        return QStringLiteral("主题颜色请使用 #RRGGBB 格式。");
    if (subscription.contains("visible") && subscription.value("visible").metaType().id() != QMetaType::Bool)
        return QStringLiteral("主题显示选项无效。");
    return {};
}

QString Mqtt::validateSubscription(const QString &topic, int qos) {
    if (topic.isEmpty() || !validString(topic) || mosquitto_sub_topic_check(topic.toUtf8().constData()) != MOSQ_ERR_SUCCESS)
        return QStringLiteral("订阅主题无效：+ 必须独占一层，# 必须位于最后一层。");
    if (qos < 0 || qos > 2) return QStringLiteral("QoS 必须为 0、1 或 2。");
    return {};
}

QString Mqtt::decodePayload(const QString &text, const QString &format, QByteArray *bytes) {
    if (!bytes) return QStringLiteral("缺少载荷输出。");
    if (text.size() > MaxPayloadBytes * 3) return QStringLiteral("单条消息不能超过 1 MiB。");
    if (format == "Hex") {
        QString hex = text;
        hex.remove(QRegularExpression(QStringLiteral("\\s")));
        if (hex.size() % 2 || hex.contains(QRegularExpression(QStringLiteral("[^0-9a-fA-F]"))))
            return QStringLiteral("Hex 必须为完整字节，例如 68 01 FF；允许空白分隔。");
        *bytes = QByteArray::fromHex(hex.toLatin1());
    } else if (format == "Text" || format == "JSON") {
        *bytes = text.toUtf8();
        if (format == "JSON") {
            // Wrapping also accepts valid scalar JSON values without rewriting bytes.
            QJsonParseError error;
            const auto json = QJsonDocument::fromJson('[' + *bytes + ']', &error);
            if (error.error != QJsonParseError::NoError || json.array().size() != 1)
                return QStringLiteral("JSON 格式无效，请检查后再发送。");
        }
    } else return QStringLiteral("消息格式请选择文本、JSON 或 Hex。");
    if (bytes->size() > MaxPayloadBytes) return QStringLiteral("单条消息不能超过 1 MiB。");
    return {};
}

QString Mqtt::formatPayload(const QByteArray &bytes, const QString &format) {
    if (format == "Hex") return QString::fromLatin1(bytes.toHex(' ').toUpper());
    if (format == "JSON") {
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(bytes, &error);
        if (error.error == QJsonParseError::NoError) return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }
    return QString::fromUtf8(bytes);
}

QVariantMap Mqtt::result(const QString &error) {
    return {{"ok", error.isEmpty()}, {"message", error}};
}
