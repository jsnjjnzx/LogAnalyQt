#include "MqttWorker.h"
#include <QFile>
#include <QHostInfo>
#include <QUrl>
#include <mosquitto.h>
#include <openssl/ssl.h>

extern "C" int loganalyqt_mqtt_connect_async_v5(mosquitto *, const char *, const char *, int, int, const mosquitto_property *);

namespace {
QString errorText(int error) { return QString::fromUtf8(mosquitto_strerror(error)); }
QString reasonText(int reason) { return QString::fromUtf8(mosquitto_reason_string(reason)); }
QByteArray certificateFileName(const QString &path) {
#ifdef Q_OS_WIN
    return path.toUtf8(); // OpenSSL opens UTF-8 paths with the Windows wide API.
#else
    return QFile::encodeName(path);
#endif
}
}

void MqttWorker::initializeLibrary() {
    struct Library {
        Library() { mosquitto_lib_init(); }
        ~Library() { mosquitto_lib_cleanup(); }
    };
    static Library library;
    Q_UNUSED(library)
}

MqttWorker::MqttWorker(QObject *parent) : QObject(parent) {
    m_pollTimer.setParent(this);
    m_pollTimer.setInterval(10);
    m_flushTimer.setParent(this);
    m_flushTimer.setInterval(50);
    connect(&m_pollTimer, &QTimer::timeout, this, &MqttWorker::poll);
    connect(&m_flushTimer, &QTimer::timeout, this, &MqttWorker::flush);
    m_clock.start();
}

MqttWorker::~MqttWorker() { destroyClient(); }

void MqttWorker::start(quint64 generation, const QVariantMap &config) {
    stop(generation);
    m_generation = generation;
    m_config = config;
    m_subscriptions = config.value("subscriptions").toList();
    m_cancelled = config.value("cancelledSubscriptions").toStringList();
    m_wanted = true;
    m_retryAt = 0;
    m_pollTimer.start();
    m_flushTimer.start();
    attempt();
}

void MqttWorker::stop(quint64 generation) {
    m_generation = generation;
    m_wanted = false;
    m_retrying = false;
    m_pollTimer.stop();
    m_flushTimer.stop();
    if (m_client && m_connected) {
        mosquitto_disconnect(m_client);
        mosquitto_loop_write(m_client, 1);
    }
    destroyClient();
    m_pending.clear();
    m_pendingBytes = 0;
    m_dropped = 0;
    m_awaitingBatch = false;
    emit connectionChanged(m_generation, "idle", QStringLiteral("未连接"));
}

void MqttWorker::destroyClient() {
    ++m_lookupGeneration;
    if (m_lookupId >= 0) QHostInfo::abortHostLookup(m_lookupId);
    m_lookupId = -1;
    m_resolving = false;
    if (m_client) { mosquitto_destroy(m_client); m_client = nullptr; }
    m_connected = false;
    m_callbackError.clear();
    m_certificateError.clear();
    m_requested.clear();
    m_subscribeIds.clear();
    m_unsubscribeIds.clear();
    for (const auto id : std::as_const(m_publishIds))
        emit publishFinished(m_generation, id, QStringLiteral("连接已断开，结果未知"));
    m_publishIds.clear();
}

void MqttWorker::failed(QString detail, bool retry) {
    if (!m_certificateError.isEmpty()) { detail = m_certificateError; retry = false; }
    if (retry && m_wanted && m_config.value("autoReconnect").toBool()) {
        ++m_lookupGeneration;
        if (m_lookupId >= 0) QHostInfo::abortHostLookup(m_lookupId);
        m_lookupId = -1;
        m_resolving = false;
        m_connected = false;
        m_callbackError.clear();
        m_requested.clear();
        m_subscribeIds.clear();
        m_unsubscribeIds.clear();
        // Preserve the library's in-flight QoS state; never enqueue new offline sends.
        for (const auto id : std::as_const(m_publishIds))
            emit publishFinished(m_generation, id, QStringLiteral("重连中，等待协议确认"));
        m_retrying = true;
        m_retryAt = m_clock.elapsed() + m_config.value("reconnectSeconds").toInt() * 1000;
        emit connectionChanged(m_generation, "reconnecting", detail + QStringLiteral("；等待重连"));
    } else {
        destroyClient();
        m_retrying = false;
        m_wanted = false;
        m_pollTimer.stop();
        emit connectionChanged(m_generation, "error", detail);
    }
}

void MqttWorker::attempt() {
    m_retrying = false;
    if (auto error = Mqtt::validateConnection(m_config, true); !error.isEmpty()) { failed(error, false); return; }
    emit connectionChanged(m_generation, "connecting", QStringLiteral("正在连接 %1:%2").arg(m_config.value("host").toString()).arg(m_config.value("port").toInt()));
    m_deadline = m_clock.elapsed() + m_config.value("timeoutSeconds").toInt() * 1000;
    const auto host = m_config.value("host").toString();
    if (!QHostAddress(host).isNull()) { connectAddress(host); return; }
    m_resolving = true;
    const auto lookup = ++m_lookupGeneration;
    m_lookupId = QHostInfo::lookupHost(host, this, [this, lookup](const QHostInfo &info) {
        if (!m_wanted || lookup != m_lookupGeneration) return;
        m_lookupId = -1;
        m_resolving = false;
        if (m_clock.elapsed() >= m_deadline) { failed(QStringLiteral("主机名解析超时"), true); return; }
        if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
            failed(QStringLiteral("主机名解析失败：") + info.errorString(), true);
            return;
        }
        auto address = info.addresses().first();
        for (const auto &candidate : info.addresses())
            if (candidate.protocol() == QAbstractSocket::IPv4Protocol) { address = candidate; break; }
        connectAddress(address.toString());
    });
}

void MqttWorker::connectAddress(const QString &address) {
    if (!m_client) {
        const auto clientId = m_config.value("clientId").toString().toUtf8();
        m_client = mosquitto_new(clientId.constData(), m_config.value("cleanSession").toBool(), this);
        if (!m_client) { failed(QStringLiteral("无法创建 MQTT 客户端。"), false); return; }
        // Defer writes to our loop so a nonblocking TCP connect can finish first.
        mosquitto_threaded_set(m_client, true);
        mosquitto_int_option(m_client, MOSQ_OPT_PROTOCOL_VERSION, m_config.value("protocol").toInt());
        mosquitto_int_option(m_client, MOSQ_OPT_TCP_NODELAY, 1);
        mosquitto_int_option(m_client, MOSQ_OPT_DISABLE_SOCKETPAIR, 1);
        mosquitto_log_callback_set(m_client, [](mosquitto *client, void *object, int level, const char *) {
            auto *self = static_cast<MqttWorker *>(object);
            if (!(level & MOSQ_LOG_ERR) || !self->m_config.value("tls").toBool()) return;
            auto *ssl = static_cast<SSL *>(mosquitto_ssl_get(client));
            if (ssl && SSL_get_verify_result(ssl) != X509_V_OK)
                self->m_certificateError = QStringLiteral("TLS 证书校验失败：") + QString::fromUtf8(X509_verify_cert_error_string(SSL_get_verify_result(ssl)));
        });
        mosquitto_connect_v5_callback_set(m_client, [](mosquitto *, void *object, int reason, int, const mosquitto_property *) {
            auto *self = static_cast<MqttWorker *>(object);
            ++self->m_activity;
            if (reason != 0) {
                self->m_callbackError = QStringLiteral("Broker 拒绝连接：") + (self->m_config.value("protocol").toInt() == 5
                    ? reasonText(reason) : QString::fromUtf8(mosquitto_connack_string(reason)));
                self->m_retryError = false;
                return;
            }
            self->m_connected = true;
            emit self->connectionChanged(self->m_generation, "connected", QStringLiteral("已连接"));
            self->reconcileSubscriptions();
        });
        mosquitto_disconnect_v5_callback_set(m_client, [](mosquitto *, void *object, int reason, const mosquitto_property *) {
            auto *self = static_cast<MqttWorker *>(object);
            ++self->m_activity;
            self->m_connected = false;
            if (self->m_wanted && self->m_callbackError.isEmpty()) {
                self->m_callbackError = QStringLiteral("连接中断：") + errorText(reason);
                self->m_retryError = reason != MOSQ_ERR_AUTH && reason != MOSQ_ERR_ACL_DENIED && reason != MOSQ_ERR_OVERSIZE_PACKET
                    && reason != MOSQ_ERR_TLS && reason != MOSQ_ERR_TLS_HANDSHAKE;
            }
        });
        mosquitto_message_v5_callback_set(m_client, [](mosquitto *, void *object, const mosquitto_message *m, const mosquitto_property *) {
            static_cast<MqttWorker *>(object)->receive(m->topic, m->payload, m->payloadlen, m->qos, m->retain);
        });
        mosquitto_publish_v5_callback_set(m_client, [](mosquitto *, void *object, int mid, int reason, const mosquitto_property *) {
            auto *self = static_cast<MqttWorker *>(object);
            ++self->m_activity;
            const auto id = self->m_publishIds.take(mid);
            if (id) emit self->publishFinished(self->m_generation, id,
                reason >= 128 ? QStringLiteral("发布失败：") + reasonText(reason) : QStringLiteral("Broker 已确认"));
        });
        mosquitto_subscribe_v5_callback_set(m_client, [](mosquitto *, void *object, int mid, int count, const int *reasons, const mosquitto_property *) {
            auto *self = static_cast<MqttWorker *>(object);
            ++self->m_activity;
            const auto topic = self->m_subscribeIds.take(mid);
            if (topic.isEmpty()) return;
            const bool ok = count == 1 && reasons && reasons[0] < 128;
            emit self->subscriptionChanged(self->m_generation, topic, ok ? QStringLiteral("已订阅 · QoS %1").arg(reasons[0])
                : QStringLiteral("订阅失败：") + (count && reasons ? reasonText(reasons[0]) : QStringLiteral("无确认")));
        });
        mosquitto_unsubscribe2_v5_callback_set(m_client, [](mosquitto *, void *object, int mid, int count, const int *reasons, const mosquitto_property *) {
            auto *self = static_cast<MqttWorker *>(object);
            ++self->m_activity;
            const auto topic = self->m_unsubscribeIds.take(mid);
            if (topic.isEmpty()) return;
            const bool ok = count == 0 || (reasons && reasons[0] < 128);
            if (ok) self->m_cancelled.removeAll(topic);
            emit self->subscriptionChanged(self->m_generation, topic, ok ? QStringLiteral("已取消订阅") : QStringLiteral("取消订阅失败：") + reasonText(reasons[0]));
        });

        int rc = MOSQ_ERR_SUCCESS;
        const auto user = m_config.value("username").toString().toUtf8();
        const auto password = m_config.value("password").toString().toUtf8();
        if (!user.isEmpty()) rc = mosquitto_username_pw_set(m_client, user.constData(), password.constData());
        if (!rc && m_config.value("tls").toBool()) {
            const auto ca = certificateFileName(m_config.value("caFile").toString());
            const auto cert = certificateFileName(m_config.value("certFile").toString());
            const auto key = certificateFileName(m_config.value("keyFile").toString());
            rc = mosquitto_tls_set(m_client, ca.constData(), nullptr, cert.isEmpty() ? nullptr : cert.constData(), key.isEmpty() ? nullptr : key.constData(),
                [](char *, int, int, void *) { return 0; }); // Do not block a GUI worker on a console password prompt.
        }
        if (!rc && m_config.value("willEnabled").toBool()) {
            QByteArray payload;
            Mqtt::decodePayload(m_config.value("willPayload").toString(), m_config.value("willFormat").toString(), &payload);
            rc = mosquitto_will_set(m_client, m_config.value("willTopic").toString().toUtf8().constData(), payload.size(), payload.constData(),
                m_config.value("willQos").toInt(), m_config.value("willRetain").toBool());
        }
        if (rc) { failed(errorText(rc), false); return; }
    }
    int rc = MOSQ_ERR_SUCCESS;
    mosquitto_property *properties = nullptr;
    if (m_config.value("protocol").toInt() == 5) {
        mosquitto_property_add_int32(&properties, MQTT_PROP_SESSION_EXPIRY_INTERVAL, m_config.value("sessionExpiry").toUInt());
        mosquitto_property_add_int32(&properties, MQTT_PROP_MAXIMUM_PACKET_SIZE, 2 * 1024 * 1024);
    }
    const auto host = m_config.value("host").toString();
    const auto serverName = QHostAddress(host).isNull() ? QUrl::toAce(host) : host.toUtf8();
    rc = loganalyqt_mqtt_connect_async_v5(m_client, serverName.constData(), address.toUtf8().constData(),
        m_config.value("port").toInt(), m_config.value("keepAlive").toInt(), properties);
    mosquitto_property_free_all(&properties);
    if (rc > 0) failed(QStringLiteral("连接失败：") + errorText(rc), rc != MOSQ_ERR_TLS);
}

void MqttWorker::poll() {
    if (!m_wanted) return;
    if (m_retrying) {
        if (m_clock.elapsed() >= m_retryAt) attempt();
        return;
    }
    if (m_resolving) {
        if (m_clock.elapsed() >= m_deadline) failed(QStringLiteral("主机名解析超时"), true);
        return;
    }
    if (!m_client) {
        if (m_clock.elapsed() >= m_retryAt) attempt();
        return;
    }
    if (!m_connected && m_clock.elapsed() >= m_deadline) { failed(QStringLiteral("连接超时"), true); return; }
    QElapsedTimer budget;
    budget.start();
    for (int i = 0; i < 64 && budget.elapsed() < 5; ++i) {
        const auto previous = m_activity;
        const int rc = mosquitto_loop(m_client, 0, 1);
        if (!m_callbackError.isEmpty()) { failed(m_callbackError, m_retryError); return; }
        if (rc > 0) { failed(QStringLiteral("连接中断：") + errorText(rc), rc != MOSQ_ERR_TLS && rc != MOSQ_ERR_OVERSIZE_PACKET); return; }
        if (previous == m_activity) break;
    }
}

void MqttWorker::receive(const char *topic, const void *payload, int size, int qos, bool retained) {
    ++m_activity;
    const qsizetype bytes = size + qstrlen(topic);
    if (size < 0 || size > Mqtt::MaxPayloadBytes || m_pending.size() >= Mqtt::MaxQueueMessages || m_pendingBytes + bytes > Mqtt::MaxQueueBytes) {
        ++m_dropped;
        return;
    }
    Mqtt::Message message;
    message.time = QDateTime::currentDateTime();
    message.topic = QString::fromUtf8(topic);
    message.payload = QByteArray(static_cast<const char *>(payload), size);
    message.qos = qos;
    message.retained = retained;
    message.delivery = QStringLiteral("已接收");
    m_pending.append(std::move(message));
    m_pendingBytes += bytes;
}

void MqttWorker::flush() {
    if (m_awaitingBatch || (m_pending.isEmpty() && !m_dropped)) return;
    Mqtt::Messages messages;
    messages.swap(m_pending);
    const auto dropped = m_dropped;
    m_pendingBytes = 0;
    m_dropped = 0;
    m_awaitingBatch = true;
    emit messagesReady(m_generation, messages, dropped);
}

void MqttWorker::acknowledgeBatch(quint64 generation) {
    if (generation == m_generation) m_awaitingBatch = false;
}

void MqttWorker::publish(quint64 generation, const Mqtt::Message &message) {
    if (generation != m_generation || !m_client || !m_connected) {
        emit publishFinished(generation, message.id, QStringLiteral("未发送：连接已断开"));
        return;
    }
    if (m_publishIds.size() >= 128) {
        emit publishFinished(generation, message.id, QStringLiteral("未发送：等待确认的消息过多"));
        return;
    }
    int mid = 0;
    const int rc = mosquitto_publish(m_client, &mid, message.topic.toUtf8().constData(), message.payload.size(), message.payload.constData(), message.qos, message.retained);
    if (rc) emit publishFinished(generation, message.id, QStringLiteral("发送失败：") + errorText(rc));
    else if (message.qos == 0) emit publishFinished(generation, message.id, QStringLiteral("已提交 · QoS 0 无确认"));
    else m_publishIds.insert(mid, message.id);
}

void MqttWorker::subscriptions(quint64 generation, const QVariantList &subscriptions, const QStringList &cancelled) {
    if (generation != m_generation) return;
    m_subscriptions = subscriptions;
    m_cancelled = cancelled;
    if (m_client && m_connected) reconcileSubscriptions();
}

void MqttWorker::reconcileSubscriptions() {
    QHash<QString, int> desired;
    QSet<QString> cancelled(m_cancelled.cbegin(), m_cancelled.cend());
    for (const auto &entry : std::as_const(m_subscriptions)) {
        const auto sub = entry.toMap();
        if (sub.value("enabled", true).toBool()) desired.insert(sub.value("topic").toString(), sub.value("qos").toInt());
        else cancelled.insert(sub.value("topic").toString());
    }
    for (auto it = m_requested.cbegin(); it != m_requested.cend(); ++it) {
        if (!desired.contains(it.key())) cancelled.insert(it.key());
    }
    for (const auto &topic : std::as_const(cancelled)) {
        if (desired.contains(topic) || m_unsubscribeIds.values().contains(topic)) continue;
        int mid = 0;
        const int rc = mosquitto_unsubscribe(m_client, &mid, topic.toUtf8().constData());
        if (!rc) m_unsubscribeIds.insert(mid, topic);
        emit subscriptionChanged(m_generation, topic, rc ? QStringLiteral("取消失败：") + errorText(rc) : QStringLiteral("取消中"));
    }
    for (auto it = desired.cbegin(); it != desired.cend(); ++it) {
        if (m_requested.contains(it.key()) && m_requested.value(it.key()) == it.value()) continue;
        int mid = 0;
        const int rc = mosquitto_subscribe(m_client, &mid, it.key().toUtf8().constData(), it.value());
        if (!rc) m_subscribeIds.insert(mid, it.key());
        emit subscriptionChanged(m_generation, it.key(), rc ? QStringLiteral("订阅失败：") + errorText(rc) : QStringLiteral("订阅中"));
    }
    m_requested = desired;
}
