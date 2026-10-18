#include "MqttMessageModel.h"
#include <mosquitto.h>

MqttMessageModel::MqttMessageModel(QObject *parent, int maxRows, qsizetype maxBytes)
    : QAbstractListModel(parent), m_maxRows(maxRows), m_maxBytes(maxBytes) {}

int MqttMessageModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_messages.size());
}

QVariant MqttMessageModel::data(const QModelIndex &index, int role) const {
    const auto *m = messageAt(index.row());
    if (!index.isValid() || !m) return {};
    switch (role) {
    case IdRole: return QVariant::fromValue(m->id);
    case TimeRole: return m->time.toString("HH:mm:ss.zzz");
    case TopicRole: return m->topic;
    case PreviewRole: return QString::fromUtf8(m->payload.left(160)).replace('\n', ' ').replace('\r', ' ').replace(QChar::Null, QChar(0x2400));
    case QosRole: return m->qos;
    case RetainedRole: return m->retained;
    case ReceivedRole: return m->received;
    case DeliveryRole: return m->delivery;
    case SizeRole: return m->payload.size();
    default: return {};
    }
}

QHash<int, QByteArray> MqttMessageModel::roleNames() const {
    return {{IdRole, "messageId"}, {TimeRole, "messageTime"}, {TopicRole, "topic"},
        {PreviewRole, "preview"}, {QosRole, "qos"}, {RetainedRole, "retained"},
        {ReceivedRole, "received"}, {DeliveryRole, "delivery"}, {SizeRole, "byteCount"}, {TopicColorRole, "topicColor"}};
}

void MqttMessageModel::append(const Mqtt::Messages &messages) {
    Mqtt::Messages accepted;
    qsizetype bytes = 0;
    for (const auto &message : messages) {
        if (message.payload.size() > m_maxBytes) { ++m_evicted; continue; }
        accepted.append(message);
        bytes += message.payload.size() + message.topic.toUtf8().size();
    }
    while (!accepted.isEmpty() && (accepted.size() > m_maxRows || bytes > m_maxBytes)) {
        const auto &first = accepted.first();
        bytes -= first.payload.size() + first.topic.toUtf8().size();
        accepted.removeFirst();
        ++m_evicted;
    }
    int remove = 0;
    qsizetype oldBytes = m_bytes;
    while (remove < rowCount() && (rowCount() - remove + accepted.size() > m_maxRows || oldBytes + bytes > m_maxBytes)) {
        const auto &first = m_messages[static_cast<size_t>(remove++)];
        oldBytes -= first.payload.size() + first.topic.toUtf8().size();
    }
    if (remove) {
        beginRemoveRows({}, 0, remove - 1);
        for (int i = 0; i < remove; ++i) m_messages.pop_front();
        m_bytes = oldBytes;
        m_evicted += remove;
        endRemoveRows();
    }
    if (!accepted.isEmpty()) {
        beginInsertRows({}, rowCount(), rowCount() + accepted.size() - 1);
        for (const auto &message : accepted) m_messages.push_back(message);
        m_bytes += bytes;
        endInsertRows();
    }
    emit changed();
}

void MqttMessageModel::clear() {
    if (!m_messages.empty()) {
        beginRemoveRows({}, 0, rowCount() - 1);
        m_messages.clear();
        endRemoveRows();
    }
    m_bytes = 0;
    m_evicted = 0;
    emit changed();
}

const Mqtt::Message *MqttMessageModel::messageAt(int row) const {
    return row >= 0 && row < rowCount() ? &m_messages[static_cast<size_t>(row)] : nullptr;
}

const Mqtt::Message *MqttMessageModel::find(quint64 id) const {
    for (const auto &message : m_messages) if (message.id == id) return &message;
    return nullptr;
}

void MqttMessageModel::setDelivery(quint64 id, const QString &delivery) {
    for (int row = 0; row < rowCount(); ++row) {
        auto &message = m_messages[static_cast<size_t>(row)];
        if (message.id != id) continue;
        message.delivery = delivery;
        emit dataChanged(index(row), index(row), {DeliveryRole});
        emit changed();
        return;
    }
}

void MqttMessageModel::failPending() {
    for (int row = 0; row < rowCount(); ++row) {
        auto &message = m_messages[static_cast<size_t>(row)];
        if (!message.received && (message.delivery == QStringLiteral("等待确认") || message.delivery == QStringLiteral("重连中，等待协议确认"))) {
            message.delivery = QStringLiteral("连接已断开，结果未知");
            emit dataChanged(index(row), index(row), {DeliveryRole});
        }
    }
    emit changed();
}

MqttMessageFilter::MqttMessageFilter(QObject *parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
}

QString MqttMessageFilter::setCriteria(const QString &direction, const QString &content) {
    if (direction != "all" && direction != "received" && direction != "sent") return QStringLiteral("收发过滤条件无效。");
    m_direction = direction;
    m_content = content;
    invalidateFilter();
    return {};
}

void MqttMessageFilter::setSubscriptions(const QVariantList &subscriptions) {
    m_subscriptions.clear();
    for (const auto &entry : subscriptions) {
        const auto subscription = entry.toMap();
        const auto topic = subscription.value("topic").toString();
        const auto levels = topic.split('/');
        qint64 literals = 0;
        for (const auto &level : levels) if (level != "+" && level != "#") ++literals;
        // Prefer literal levels, then fixed depth, then a longer minimum path; ties keep list order.
        const qint64 specificity = (literals << 32) + (topic.contains('#') ? 0 : (qint64(1) << 31)) + levels.size();
        m_subscriptions.append({topic.toUtf8(), subscription.value("color", Mqtt::defaultSubscriptionColor(m_subscriptions.size())).toString(),
                                subscription.value("visible", true).toBool(), specificity});
    }
    invalidateFilter();
    if (rowCount()) emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {MqttMessageModel::TopicColorRole});
}

QString MqttMessageFilter::topicColor(const QString &topic) const {
    const auto bytes = topic.toUtf8();
    QString color;
    qint64 specificity = -1;
    for (const auto &rule : m_subscriptions) {
        bool matches = false;
        mosquitto_topic_matches_sub(rule.topic.constData(), bytes.constData(), &matches);
        if (matches && rule.specificity > specificity) { color = rule.color; specificity = rule.specificity; }
    }
    return color;
}

QVariant MqttMessageFilter::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) return {};
    if (role == MqttMessageModel::TopicColorRole)
        return topicColor(QSortFilterProxyModel::data(index, MqttMessageModel::TopicRole).toString());
    return QSortFilterProxyModel::data(index, role);
}

bool MqttMessageFilter::filterAcceptsRow(int row, const QModelIndex &) const {
    const auto *model = qobject_cast<const MqttMessageModel *>(sourceModel());
    const auto *message = model ? model->messageAt(row) : nullptr;
    if (!message) return false;
    return acceptsMessage(*message);
}

bool MqttMessageFilter::acceptsMessage(const Mqtt::Message &message) const {
    if (message.received && !m_subscriptions.isEmpty()) {
        bool visible = false;
        const auto topic = message.topic.toUtf8();
        for (const auto &rule : m_subscriptions) {
            bool matches = false;
            if (rule.visible) mosquitto_topic_matches_sub(rule.topic.constData(), topic.constData(), &matches);
            if (matches) { visible = true; break; }
        }
        if (!visible) return false;
    }
    if (m_direction == "received" && !message.received) return false;
    if (m_direction == "sent" && message.received) return false;
    if (!m_content.isEmpty()) {
        return QString::fromUtf8(message.payload).contains(m_content, Qt::CaseInsensitive)
            || QString::fromLatin1(message.payload.toHex(' ')).contains(m_content, Qt::CaseInsensitive);
    }
    return true;
}
