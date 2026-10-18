#pragma once

#include "MqttTypes.h"
#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <deque>

class MqttMessageModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { IdRole = Qt::UserRole + 1, TimeRole, TopicRole, PreviewRole,
                QosRole, RetainedRole, ReceivedRole, DeliveryRole, SizeRole };
    explicit MqttMessageModel(QObject *parent = nullptr, int maxRows = 10000,
                              qsizetype maxBytes = 64 * 1024 * 1024);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void append(const Mqtt::Messages &messages);
    void clear();
    void setDelivery(quint64 id, const QString &delivery);
    void failPending();
    const Mqtt::Message *messageAt(int row) const;
    const Mqtt::Message *find(quint64 id) const;
    quint64 evictedCount() const { return m_evicted; }
    qsizetype payloadBytes() const { return m_bytes; }
signals:
    void changed();
private:
    std::deque<Mqtt::Message> m_messages;
    int m_maxRows;
    qsizetype m_maxBytes;
    qsizetype m_bytes = 0;
    quint64 m_evicted = 0;
};

class MqttMessageFilter : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit MqttMessageFilter(QObject *parent = nullptr);
    QString setCriteria(const QString &direction, const QString &topic, const QString &content);
protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
private:
    QString m_direction = "all";
    QByteArray m_topic;
    QString m_content;
};
