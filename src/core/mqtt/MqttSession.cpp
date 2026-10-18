#include "MqttSession.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>

MqttSession::MqttSession(QObject *parent)
    : QObject(parent), m_model(this), m_filter(this), m_worker(new MqttWorker), m_config(Mqtt::connectionDefaults()) {
    MqttWorker::initializeLibrary();
    qRegisterMetaType<Mqtt::Messages>();
    m_filter.setSourceModel(&m_model);
    connect(&m_model, &MqttMessageModel::changed, this, &MqttSession::messagesChanged);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &MqttWorker::connectionChanged, this, [this](quint64 generation, const QString &state, const QString &detail) {
        if (generation != m_generation) return;
        m_state = state;
        m_detail = detail;
        if (state != "connected") {
            if (state == "idle" || state == "error" || state == "disconnecting") {
                m_model.failPending();
                for (auto &message : m_pending)
                    if (!message.received && (message.delivery == QStringLiteral("等待确认") || message.delivery == QStringLiteral("重连中，等待协议确认")))
                        message.delivery = QStringLiteral("连接已断开，结果未知");
            }
            m_subscriptionStates.clear();
            emit subscriptionsChanged();
        }
        emit stateChanged();
    });
    connect(m_worker, &MqttWorker::messagesReady, this, [this](quint64 generation, Mqtt::Messages messages, quint64 dropped) {
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, generation] { worker->acknowledgeBatch(generation); });
        if (generation != m_generation) return;
        m_dropped += dropped;
        m_received += messages.size();
        acceptMessages(std::move(messages));
    });
    connect(m_worker, &MqttWorker::publishFinished, this, [this](quint64 generation, quint64 id, const QString &delivery) {
        if (generation == m_generation) setDelivery(id, delivery);
    });
    connect(m_worker, &MqttWorker::subscriptionChanged, this, [this](quint64 generation, const QString &topic, const QString &state) {
        if (generation != m_generation) return;
        m_subscriptionStates.insert(topic, state);
        if (state == QStringLiteral("已取消订阅")) {
            auto cancelled = m_config.value("cancelledSubscriptions").toStringList();
            if (cancelled.removeAll(topic)) {
                m_config.insert("cancelledSubscriptions", cancelled);
                emit configurationChanged();
            }
        }
        if (state.contains(QStringLiteral("失败"))) report(topic + QStringLiteral("：") + state);
        emit subscriptionsChanged();
    });
    connect(&m_exportWatcher, &QFutureWatcher<QString>::finished, this, [this] {
        const auto error = m_exportWatcher.result();
        m_notice = error.isEmpty() ? QStringLiteral("消息记录已导出（载荷以 Base64 原样保存）。") : error;
        emit noticeChanged();
        emit exportBusyChanged();
    });
    m_thread.start();
}

MqttSession::~MqttSession() {
    const auto generation = ++m_generation;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, generation] { worker->stop(generation); }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

QString MqttSession::configure(const QVariantMap &config) {
    if (active()) return QStringLiteral("请先断开连接，再修改或切换连接配置。");
    if (auto error = Mqtt::validateConnection(config); !error.isEmpty()) return error;
    auto subscriptions = config.value("subscriptions").toList();
    for (int i = 0; i < subscriptions.size(); ++i) {
        auto subscription = subscriptions[i].toMap();
        if (auto error = Mqtt::validateSubscriptionAppearance(subscription); !error.isEmpty()) return error;
        if (!subscription.contains("color")) subscription.insert("color", Mqtt::defaultSubscriptionColor(i));
        if (!subscription.contains("visible")) subscription.insert("visible", true);
        subscriptions[i] = subscription;
    }
    if (m_config.value("id") != config.value("id")) clearMessages();
    m_config = config;
    m_config.insert("subscriptions", subscriptions);
    m_subscriptionStates.clear();
    updateSubscriptionDisplay();
    return {};
}

QVariantMap MqttSession::report(const QString &error) {
    m_notice = error;
    emit noticeChanged();
    return Mqtt::result(error);
}

QVariantMap MqttSession::connectToBroker() {
    if (active()) return report(QStringLiteral("当前连接正在运行，请先断开。"));
    if (auto error = Mqtt::validateConnection(m_config, true); !error.isEmpty()) return report(error);
    const auto generation = ++m_generation;
    m_state = "connecting";
    m_detail = QStringLiteral("正在连接");
    emit stateChanged();
    const auto config = m_config;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, generation, config] { worker->start(generation, config); });
    return report({});
}

void MqttSession::disconnectFromBroker() {
    const auto generation = ++m_generation;
    m_state = "disconnecting";
    m_detail = QStringLiteral("正在断开");
    m_model.failPending();
    emit stateChanged();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, generation] { worker->stop(generation); });
}

void MqttSession::acceptMessages(Mqtt::Messages messages) {
    for (auto &message : messages) if (!message.id) message.id = m_nextId++;
    if (!m_paused) m_model.append(messages);
    else {
        for (auto &message : messages) {
            const qsizetype bytes = message.payload.size() + message.topic.toUtf8().size();
            while (!m_pending.empty() && (m_pending.size() >= Mqtt::MaxQueueMessages || m_pendingBytes + bytes > Mqtt::MaxQueueBytes)) {
                m_pendingBytes -= m_pending.front().payload.size() + m_pending.front().topic.toUtf8().size();
                m_pending.pop_front();
                ++m_dropped;
            }
            m_pendingBytes += bytes;
            m_pending.push_back(std::move(message));
        }
    }
    emit messagesChanged();
}

void MqttSession::setPaused(bool paused) {
    if (m_paused == paused) return;
    m_paused = paused;
    if (!paused) {
        Mqtt::Messages pending;
        pending.reserve(m_pending.size());
        for (auto &message : m_pending) pending.append(std::move(message));
        m_pending.clear();
        m_pendingBytes = 0;
        m_model.append(pending);
    }
    emit pausedChanged();
    emit messagesChanged();
}

void MqttSession::clearMessages() {
    m_model.clear();
    m_pending.clear();
    m_pendingBytes = 0;
    m_received = 0;
    m_sent = 0;
    m_dropped = 0;
    emit messagesChanged();
}

void MqttSession::setDelivery(quint64 id, const QString &delivery) {
    m_model.setDelivery(id, delivery);
    for (auto &message : m_pending) if (message.id == id) message.delivery = delivery;
    emit messagesChanged();
}

QVariantMap MqttSession::publishMessage(const QString &topic, const QString &text, const QString &format, int qos, bool retain) {
    if (!connected()) return report(QStringLiteral("未连接，消息未发送。"));
    if (auto error = Mqtt::publishError(topic, qos); !error.isEmpty()) return report(error);
    Mqtt::Message message;
    if (auto error = Mqtt::decodePayload(text, format, &message.payload); !error.isEmpty()) return report(error);
    message.id = m_nextId++;
    message.time = QDateTime::currentDateTime();
    message.topic = topic;
    message.qos = qos;
    message.retained = retain;
    message.received = false;
    message.delivery = QStringLiteral("等待确认");
    ++m_sent;
    acceptMessages({message});
    const auto generation = m_generation;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, generation, message] { worker->publish(generation, message); });
    QVariantMap draft{{"topic", topic}, {"payload", text}, {"format", format}, {"qos", qos}, {"retain", retain}};
    m_config.insert("draft", draft);
    auto history = m_config.value("history").toList();
    // Large payloads remain in the draft/message cache, not duplicated in history.
    if (text.toUtf8().size() <= 64 * 1024) {
        history.removeAll(draft);
        history.prepend(draft);
        while (history.size() > Mqtt::MaxHistory) history.removeLast();
        m_config.insert("history", history);
    }
    emit configurationChanged();
    return report({});
}

QVariantList MqttSession::subscriptions() const {
    auto subscriptions = m_config.value("subscriptions").toList();
    for (auto &entry : subscriptions) {
        auto subscription = entry.toMap();
        subscription.insert("enabled", subscription.value("enabled", true).toBool());
        subscription.insert("state", m_subscriptionStates.value(subscription.value("topic").toString(),
            subscription.value("enabled", true).toBool() ? QStringLiteral("待连接") : QStringLiteral("已停用")));
        entry = subscription;
    }
    return subscriptions;
}

void MqttSession::sendSubscriptions() {
    const auto generation = m_generation;
    const auto subscriptions = m_config.value("subscriptions").toList();
    const auto cancelled = m_config.value("cancelledSubscriptions").toStringList();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, generation, subscriptions, cancelled] { worker->subscriptions(generation, subscriptions, cancelled); });
    updateSubscriptionDisplay();
}

void MqttSession::updateSubscriptionDisplay() {
    m_filter.setSubscriptions(m_config.value("subscriptions").toList());
    emit subscriptionsChanged();
    emit configurationChanged();
    emit messagesChanged();
}

QVariantMap MqttSession::setSubscriptionAppearance(const QString &topic, const QString &color, bool visible) {
    if (auto error = Mqtt::validateSubscriptionAppearance({{"color", color}, {"visible", visible}}); !error.isEmpty()) return report(error);
    auto subscriptions = m_config.value("subscriptions").toList();
    for (auto &entry : subscriptions) {
        auto subscription = entry.toMap();
        if (subscription.value("topic").toString() != topic) continue;
        subscription.insert("color", color.toLower());
        subscription.insert("visible", visible);
        entry = subscription;
        m_config.insert("subscriptions", subscriptions);
        updateSubscriptionDisplay();
        return report({});
    }
    return report(QStringLiteral("订阅主题不存在。"));
}

void MqttSession::setAllSubscriptionsVisible(bool visible) {
    auto subscriptions = m_config.value("subscriptions").toList();
    for (auto &entry : subscriptions) { auto subscription = entry.toMap(); subscription.insert("visible", visible); entry = subscription; }
    m_config.insert("subscriptions", subscriptions);
    updateSubscriptionDisplay();
}

QVariantMap MqttSession::saveSubscription(const QString &topic, int qos, bool enabled, const QString &previousTopic, const QString &color) {
    if (auto error = Mqtt::validateSubscription(topic, qos); !error.isEmpty()) return report(error);
    if (!color.isEmpty()) {
        if (auto error = Mqtt::validateSubscriptionAppearance({{"color", color}}); !error.isEmpty()) return report(error);
    }
    auto subscriptions = m_config.value("subscriptions").toList();
    QVariantMap subscription;
    int previousIndex = -1;
    for (int i = 0; i < subscriptions.size(); ++i) {
        const auto entry = subscriptions[i].toMap();
        if (!previousTopic.isEmpty() && previousTopic != topic && entry.value("topic").toString() == topic)
            return report(QStringLiteral("该订阅主题已存在，请使用其他名称。"));
        if (entry.value("topic").toString() == (previousTopic.isEmpty() ? topic : previousTopic)) { subscription = entry; previousIndex = i; }
    }
    auto cancelled = m_config.value("cancelledSubscriptions").toStringList();
    if (!previousTopic.isEmpty() && previousTopic != topic) {
        if (!cancelled.contains(previousTopic)) cancelled.append(previousTopic);
        if (cancelled.size() > 1024) return report(QStringLiteral("待取消的订阅过多，请先连接 Broker 同步。"));
        for (int i = subscriptions.size() - 1; i >= 0; --i)
            if (subscriptions[i].toMap().value("topic").toString() == previousTopic) subscriptions.removeAt(i);
    }
    cancelled.removeAll(topic);
    subscription.insert("topic", topic); subscription.insert("qos", qos); subscription.insert("enabled", enabled);
    if (!color.isEmpty()) subscription.insert("color", color.toLower());
    if (!subscription.contains("color")) subscription.insert("color", Mqtt::defaultSubscriptionColor(subscriptions.size()));
    if (!subscription.contains("visible")) subscription.insert("visible", true);
    bool found = false;
    for (auto &entry : subscriptions) {
        if (entry.toMap().value("topic").toString() == topic) { entry = subscription; found = true; break; }
    }
    if (!found) {
        if (subscriptions.size() >= 100) return report(QStringLiteral("每个连接最多保存 100 个订阅。"));
        if (previousIndex >= 0) subscriptions.insert(previousIndex, subscription);
        else subscriptions.append(subscription);
    }
    m_config.insert("subscriptions", subscriptions);
    m_config.insert("cancelledSubscriptions", cancelled);
    sendSubscriptions();
    if (!found) emit subscriptionSaved(subscription);
    return report({});
}

QVariantMap MqttSession::removeSubscription(const QString &topic) {
    if (auto error = Mqtt::validateSubscription(topic, 0); !error.isEmpty()) return report(error);
    auto subscriptions = m_config.value("subscriptions").toList();
    auto cancelled = m_config.value("cancelledSubscriptions").toStringList();
    if (!cancelled.contains(topic)) cancelled.append(topic);
    if (cancelled.size() > 1024) return report(QStringLiteral("待取消的订阅过多，请先连接 Broker 同步。"));
    for (int i = subscriptions.size() - 1; i >= 0; --i)
        if (subscriptions[i].toMap().value("topic").toString() == topic) subscriptions.removeAt(i);
    m_config.insert("subscriptions", subscriptions);
    m_config.insert("cancelledSubscriptions", cancelled);
    sendSubscriptions();
    return report({});
}

QVariantMap MqttSession::setFilter(const QString &direction, const QString &content) {
    const auto error = m_filter.setCriteria(direction, content);
    emit messagesChanged();
    return report(error);
}

qulonglong MqttSession::idAt(int row) const {
    return m_filter.data(m_filter.index(row, 0), MqttMessageModel::IdRole).toULongLong();
}

QVariantMap MqttSession::message(qulonglong id, const QString &format) const {
    const auto *message = m_model.find(id);
    if (!message) return {};
    constexpr qsizetype PreviewLimit = 64 * 1024;
    const bool truncated = message->payload.size() > PreviewLimit;
    return {{"id", QVariant::fromValue(id)}, {"topic", message->topic},
        {"topicColor", m_filter.topicColor(message->topic)}, {"visible", m_filter.acceptsMessage(*message)},
        {"time", message->time.toString("yyyy-MM-dd HH:mm:ss.zzz")}, {"qos", message->qos},
        {"retained", message->retained}, {"received", message->received}, {"delivery", message->delivery},
        {"byteCount", message->payload.size()}, {"truncated", truncated},
        {"payload", Mqtt::formatPayload(message->payload.left(PreviewLimit), format)}};
}

void MqttSession::copyMessage(qulonglong id, const QString &format) {
    const auto *message = m_model.find(id);
    if (!message) { report(QStringLiteral("记录已被清空或移出缓存。")); return; }
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        QGuiApplication::clipboard()->setText(Mqtt::formatPayload(message->payload, format));
        m_notice = QStringLiteral("已复制完整消息内容。");
        emit noticeChanged();
    }
}

QVariantMap MqttSession::updateDraft(const QVariantMap &draft) {
    if (draft.value("payload").toString().size() > Mqtt::MaxPayloadBytes * 3)
        return report(QStringLiteral("草稿超过大小限制，未保存。"));
    m_config.insert("draft", draft);
    emit configurationChanged();
    return Mqtt::result();
}

QVariantMap MqttSession::exportMessages(const QUrl &url, bool filtered) {
    if (exportBusy()) return report(QStringLiteral("上一份导出尚未完成。"));
    if (!url.isLocalFile()) return report(QStringLiteral("请选择本地导出文件。"));
    Mqtt::Messages snapshot;
    const int count = filtered ? m_filter.rowCount() : m_model.rowCount();
    snapshot.reserve(count);
    for (int i = 0; i < count; ++i) {
        const auto *message = m_model.messageAt(filtered ? m_filter.mapToSource(m_filter.index(i, 0)).row() : i);
        if (message) snapshot.append(*message);
    }
    m_exportWatcher.setFuture(QtConcurrent::run([snapshot, path = url.toLocalFile()]() -> QString {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return QStringLiteral("无法导出：") + file.errorString();
        for (const auto &message : snapshot) {
            QJsonObject row{{"schema", 1}, {"time", message.time.toUTC().toString(Qt::ISODateWithMs)},
                {"direction", message.received ? "received" : "sent"}, {"topic", message.topic},
                {"qos", message.qos}, {"retain", message.retained}, {"delivery", message.delivery},
                {"payloadBase64", QString::fromLatin1(message.payload.toBase64())}};
            const auto bytes = QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
            if (file.write(bytes) != bytes.size()) return QStringLiteral("导出写入失败：") + file.errorString();
        }
        return file.commit() ? QString() : QStringLiteral("导出保存失败：") + file.errorString();
    }));
    emit exportBusyChanged();
    return report({});
}
