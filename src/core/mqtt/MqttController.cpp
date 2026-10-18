#include "MqttController.h"
#include "common/AppPaths.h"
#include "common/JsonSettings.h"
#include "../config/CredentialAccess.h"
#include <QCoreApplication>
#include <QDir>
#include <QDesktopServices>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>
#include <cmath>
#include <QScopedValueRollback>

MqttController::MqttController(QObject *parent)
    : MqttController(AppPaths::writableDataRoot(), parent) {}

MqttController::MqttController(const QString &dataRoot, QObject *parent, const QString &helperOverride, std::shared_ptr<SecureCredentialStore> credentials)
    : QObject(parent), m_remote(this), m_local(this), m_broker(dataRoot, this, helperOverride),
      m_configPath(QDir(dataRoot).filePath("settings.json")),
      m_commonPath(QDir(dataRoot).filePath("settings.json")) {
    m_credentials = credentials ? std::move(credentials) : std::make_shared<SecureCredentialStore>(SecureCredentialStore::Domain::Mqtt);
    m_saveTimer.setInterval(500);
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { saveNow(); });
    load();
    reloadCommonSubscriptions();
    m_loading = false;
    connect(&m_remote, &MqttSession::subscriptionSaved, this, &MqttController::rememberCommonSubscription);
    connect(&m_local, &MqttSession::subscriptionSaved, this, &MqttController::rememberCommonSubscription);
    connect(&m_remote, &MqttSession::configurationChanged, this, [this] {
        if (m_loading) return;
        for (auto &profile : m_profiles) {
            if (profile.toMap().value("id").toString() == currentProfileId()) profile = m_remote.configuration();
        }
        scheduleSave();
    });
    connect(&m_local, &MqttSession::configurationChanged, this, &MqttController::scheduleSave);
    connect(&m_broker, &MqttBroker::stateChanged, this, [this] {
        if (m_broker.running() && m_broker.configuration().value("connectLocal").toBool() && !m_local.active())
            connectLocalClient();
        else if (!m_broker.active() || m_broker.state() == "stopping") {
            if (m_local.active() && m_local.state() != "disconnecting") m_local.disconnectFromBroker();
        }
    });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
        if (m_dirty) saveNow();
        m_remote.disconnectFromBroker();
        m_local.disconnectFromBroker();
        m_broker.stop();
    });
}

MqttController::~MqttController() {
    m_saveTimer.stop();
    if (m_dirty) saveNow();
}

QVariantMap MqttController::normalizedConnection(const QVariantMap &input) {
    auto config = Mqtt::connectionDefaults(input.value("id").toString());
    for (auto it = config.begin(); it != config.end(); ++it)
        if (input.contains(it.key())) it.value() = input.value(it.key());
    config.insert("name", config.value("name").toString().trimmed());
    config.insert("host", config.value("host").toString().trimmed());
    return config;
}

QString MqttController::validateWorkspace(const QVariantMap &config) {
    if (auto error = Mqtt::validateConnection(config); !error.isEmpty()) return error;
    const auto subscriptions = config.value("subscriptions").toList();
    if (subscriptions.size() > 100) return QStringLiteral("订阅配置超过 100 条。");
    QSet<QString> topics;
    const auto cancelled = config.value("cancelledSubscriptions").toStringList();
    if (cancelled.size() > 1024) return QStringLiteral("待取消的订阅过多。");
    for (const auto &topic : cancelled)
        if (auto error = Mqtt::validateSubscription(topic, 0); !error.isEmpty()) return error;
    for (const auto &entry : subscriptions) {
        const auto sub = entry.toMap();
        const auto topic = sub.value("topic").toString();
        bool validQos = false;
        const auto qos = sub.value("qos").toDouble(&validQos);
        if (!validQos || qos != std::floor(qos) || qos < 0 || qos > 2) return QStringLiteral("订阅 QoS 无效。");
        if (auto error = Mqtt::validateSubscription(topic, sub.value("qos").toInt()); !error.isEmpty()) return error;
        if (auto error = Mqtt::validateSubscriptionAppearance(sub); !error.isEmpty()) return error;
        if (topics.contains(topic)) return QStringLiteral("存在重复订阅主题。");
        topics.insert(topic);
    }
    if (config.value("draft").toMap().value("payload").toString().size() > Mqtt::MaxPayloadBytes * 3)
        return QStringLiteral("发送草稿超过大小限制。");
    const auto history = config.value("history").toList();
    if (history.size() > Mqtt::MaxHistory) return QStringLiteral("发送历史超过大小限制。");
    for (const auto &entry : history)
        if (entry.toMap().value("payload").toString().toUtf8().size() > 64 * 1024) return QStringLiteral("发送历史载荷过大。");
    return {};
}

void MqttController::load() {
    auto initial = Mqtt::connectionDefaults();
    initial.insert("name", QStringLiteral("MQTT 连接 1"));
    m_profiles = {initial};
    auto local = Mqtt::connectionDefaults("local");
    local.insert("name", QStringLiteral("本机测试客户端"));
    local.insert("clientId", "loganalyqt_local_" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    local.insert("subscriptions", QVariantList{QVariantMap{{"topic", "#"}, {"qos", 0}, {"enabled", true}}});
    m_remote.configure(initial);
    m_local.configure(local);
    auto invalid = [this](const QString &error) {
        m_badFile = true; m_storageError = QStringLiteral("原配置已保留：") + error;
    };
    QJsonObject root; QString error;
    if (!ConfigJson::read(m_configPath, &root, &error)) { invalid(error); return; }
    m_workspaceSnapshot = root.value("mqtt").toObject(); m_workspaceSnapshot.remove("commonSubscriptions");
    if (!root.contains("mqtt") || !root.value("mqtt").toObject().contains("connections")) return;
    const auto json = root.value("mqtt").toObject();
    m_workspaceSnapshot = json; m_workspaceSnapshot.remove("commonSubscriptions");
    const auto profiles = json.value("connections").toArray();
    if (profiles.isEmpty() || profiles.size() > 64) { invalid(QStringLiteral("连接列表无效")); return; }
    QVariantList restored;
    QSet<QString> ids;
    for (const auto &entry : profiles) {
        const auto raw = entry.toObject().toVariantMap();
        const auto id = raw.value("id").toString();
        if (QUuid::fromString(id).isNull() || ids.contains(id)) { invalid(QStringLiteral("连接标识无效或重复")); return; }
        auto config = normalizedConnection(raw);
        if (auto error = validateWorkspace(config); !error.isEmpty()) { invalid(error); return; }
        ids.insert(id);
        restored.append(config);
    }
    auto broker = Mqtt::brokerDefaults();
    const auto savedBroker = json.value("broker").toObject().toVariantMap();
    for (auto it = broker.begin(); it != broker.end(); ++it)
        if (savedBroker.contains(it.key())) it.value() = savedBroker.value(it.key());
    if (auto error = Mqtt::validateBroker(broker); !error.isEmpty()) { invalid(error); return; }
    const auto savedLocal = json.value("localClient").toObject().toVariantMap();
    for (auto it = local.begin(); it != local.end(); ++it)
        if (savedLocal.contains(it.key())) it.value() = savedLocal.value(it.key());
    local.insert("id", "local");
    if (auto error = validateWorkspace(local); !error.isEmpty()) { invalid(error); return; }
    m_profiles = restored;
    const auto selected = json.value("selectedConnectionId").toString();
    auto config = restored.first().toMap();
    for (const auto &entry : restored) if (entry.toMap().value("id").toString() == selected) config = entry.toMap();
    m_remote.configure(config);
    m_local.configure(local);
    m_broker.configure(broker);
    const auto ratio = json.value("layout").toObject().value("splitRatio").toDouble(0.56);
    m_splitRatio = std::isfinite(ratio) ? qBound(0.3, ratio, 0.7) : 0.56;
}

QVariantList MqttController::profiles() const {
    QVariantList profiles;
    for (const auto &entry : m_profiles) {
        const auto config = entry.toMap();
        profiles.append(QVariantMap{{"id", config.value("id")}, {"name", config.value("name")},
            {"host", config.value("host")}, {"port", config.value("port")}});
    }
    return profiles;
}

void MqttController::scheduleSave() {
    if (m_loading) return;
    m_dirty = true;
    if (!m_badFile) m_saveTimer.start();
}

void MqttController::setSplitRatio(double ratio) {
    if (!std::isfinite(ratio)) return;
    ratio = qBound(0.3, ratio, 0.7);
    if (qFuzzyCompare(ratio, m_splitRatio)) return;
    m_splitRatio = ratio;
    emit layoutChanged();
    scheduleSave();
}

QVariantMap MqttController::storageResult(const QString &error) {
    m_storageError = error;
    emit storageChanged();
    return Mqtt::result(error);
}

QVariantMap MqttController::saveNow() {
    m_saveTimer.stop();
    if (m_badFile) return storageResult(QStringLiteral("现有 MQTT 配置无法读取。请先移走或修复配置文件，再重启软件；当前未覆盖原文件。"));
    if (m_saving) return storageResult(QStringLiteral("配置保存处理中，请稍后重试。"));
    QScopedValueRollback<bool> saving(m_saving, true);
    QJsonArray connections;
    QString error;
    for (const auto &entry : m_profiles) {
        auto config = QJsonObject::fromVariantMap(entry.toMap());
        if (!CredentialAccess::sealMqtt(&config, &error, m_credentials)) return storageResult(error);
        connections.append(config);
    }
    auto broker = QJsonObject::fromVariantMap(m_broker.configuration());
    auto local = QJsonObject::fromVariantMap(m_local.configuration());
    if (!CredentialAccess::sealMqtt(&broker, &error, m_credentials) || !CredentialAccess::sealMqtt(&local, &error, m_credentials)) return storageResult(error);
    QJsonObject nextSnapshot;
    if (!ConfigJson::update(m_configPath, [&](QJsonObject &root, QString *e) {
        auto mqtt = root.value("mqtt").toObject();
        auto currentWorkspace = mqtt; currentWorkspace.remove("commonSubscriptions");
        if (currentWorkspace != m_workspaceSnapshot) return ConfigJson::fail(e, QStringLiteral("其他实例已修改 MQTT 配置，请重启后重试；未覆盖对方设置。"));
        mqtt.insert("connections", connections); mqtt.insert("selectedConnectionId", currentProfileId());
        mqtt.insert("broker", broker); mqtt.insert("localClient", local); mqtt.insert("layout", QJsonObject{{"splitRatio", m_splitRatio}});
        nextSnapshot = mqtt; nextSnapshot.remove("commonSubscriptions");
        root.insert("mqtt", mqtt); return true;
    }, &error)) return storageResult(error);
    m_workspaceSnapshot = nextSnapshot;
    m_dirty = false;
    return storageResult({});
}

QVariantMap MqttController::newConnection() const { return Mqtt::connectionDefaults(); }

QString MqttController::normalizeCommonSubscription(QVariantMap *subscription, int index) {
    if (subscription->value("topic").metaType().id() != QMetaType::QString) return QStringLiteral("主题必须是文本。");
    bool valid = false;
    const auto qos = subscription->value("qos", 0).toDouble(&valid);
    if (!valid || qos != std::floor(qos) || qos < 0 || qos > 2) return QStringLiteral("QoS 必须为 0、1 或 2。");
    if (auto error = Mqtt::validateSubscription(subscription->value("topic").toString(), static_cast<int>(qos)); !error.isEmpty()) return error;
    subscription->insert("qos", static_cast<int>(qos));
    if (!subscription->contains("color")) subscription->insert("color", Mqtt::defaultSubscriptionColor(index));
    if (auto error = Mqtt::validateSubscriptionAppearance(*subscription); !error.isEmpty()) return error;
    subscription->insert("color", subscription->value("color").toString().toLower());
    return {};
}

QString MqttController::readCommonSubscriptions(QVariantList *subscriptions, QByteArray *bytes) const {
    QJsonObject root; QString problem;
    if (!ConfigJson::read(m_commonPath, &root, &problem)) return problem;
    const auto mqtt = root.value("mqtt").toObject();
    if (!mqtt.contains("commonSubscriptions")) return {};
    if (!mqtt.value("commonSubscriptions").isArray()) return QStringLiteral("常用订阅必须是数组。");
    const auto entries = mqtt.value("commonSubscriptions").toArray();
    *bytes = QJsonDocument(entries).toJson(QJsonDocument::Compact);
    if (entries.size() > 1000) return QStringLiteral("最多保存 1000 个常用订阅。");
    QSet<QString> topics;
    for (const auto &entry : entries) {
        if (!entry.isObject()) return QStringLiteral("每个常用订阅必须是一个对象。");
        auto subscription = entry.toObject().toVariantMap();
        if (auto problem = normalizeCommonSubscription(&subscription, subscriptions->size()); !problem.isEmpty()) return problem;
        const auto topic = subscription.value("topic").toString();
        if (topics.contains(topic)) continue;
        topics.insert(topic); subscriptions->append(subscription);
    }
    return {};
}

QVariantMap MqttController::commonResult(const QString &error, bool listChanged) {
    const auto message = error.isEmpty() ? QString() : QStringLiteral("常用订阅：") + error;
    const bool changed = listChanged || m_commonError != message;
    m_commonError = message;
    if (changed) emit commonSubscriptionsChanged();
    return Mqtt::result(m_commonError);
}

QVariantMap MqttController::reloadCommonSubscriptions() {
    QVariantList subscriptions;
    QByteArray bytes;
    if (auto error = readCommonSubscriptions(&subscriptions, &bytes); !error.isEmpty()) return commonResult(error);
    const bool changed = m_commonSubscriptions != subscriptions;
    m_commonSubscriptions = subscriptions;
    return commonResult({}, changed);
}

QVariantMap MqttController::changeCommonSubscriptions(const std::function<QString(QVariantList &)> &change) {
    QVariantList updated; QString problem;
    if (!ConfigJson::update(m_commonPath, [&](QJsonObject &root, QString *error) {
        auto mqtt = root.value("mqtt").toObject();
        const auto entries = mqtt.value("commonSubscriptions");
        if (!entries.isUndefined() && !entries.isArray()) return ConfigJson::fail(error, QStringLiteral("常用订阅格式无效"));
        updated = entries.toArray().toVariantList();
        if (auto message = change(updated); !message.isEmpty()) return ConfigJson::fail(error, message);
        if (updated.size() > 1000) return ConfigJson::fail(error, QStringLiteral("最多保存 1000 个常用订阅"));
        mqtt.insert("commonSubscriptions", QJsonArray::fromVariantList(updated)); root.insert("mqtt", mqtt); return true;
    }, &problem)) return commonResult(problem);
    const bool changed = m_commonSubscriptions != updated;
    m_commonSubscriptions = updated;
    return commonResult({}, changed);
}

void MqttController::rememberCommonSubscription(const QVariantMap &subscription) {
    for (const auto &entry : m_commonSubscriptions)
        if (entry.toMap().value("topic") == subscription.value("topic")) return;
    saveCommonSubscription({{"topic", subscription.value("topic")}, {"qos", subscription.value("qos")}, {"color", subscription.value("color")}});
}

QVariantMap MqttController::saveCommonSubscription(const QVariantMap &input, const QString &previousTopic) {
    auto subscription = input;
    if (auto error = normalizeCommonSubscription(&subscription, m_commonSubscriptions.size()); !error.isEmpty()) return commonResult(error);
    return changeCommonSubscriptions([&](QVariantList &entries) {
        const auto topic = subscription.value("topic").toString();
        int previous = -1;
        for (int i = 0; i < entries.size(); ++i) {
            const auto existing = entries[i].toMap().value("topic").toString();
            if (existing == topic && previousTopic.isEmpty()) return QString();
            if (existing == topic && topic != previousTopic) return QStringLiteral("该常用主题已存在，未覆盖原记录。");
            if (existing == previousTopic) previous = i;
        }
        if (previousTopic.isEmpty()) entries.append(subscription);
        else {
            if (previous < 0) return QStringLiteral("待编辑的常用订阅已不存在，请重新加载。");
            auto updated = entries[previous].toMap();
            for (auto it = subscription.cbegin(); it != subscription.cend(); ++it) updated.insert(it.key(), it.value());
            entries[previous] = updated;
        }
        return QString();
    });
}

QVariantMap MqttController::removeCommonSubscription(const QString &topic) {
    return changeCommonSubscriptions([&](QVariantList &entries) {
        for (int i = entries.size() - 1; i >= 0; --i) if (entries[i].toMap().value("topic").toString() == topic) entries.removeAt(i);
        return QString();
    });
}

QVariantMap MqttController::useCommonSubscription(const QString &topic, bool local) {
    if (auto result = reloadCommonSubscriptions(); !result.value("ok").toBool()) return result;
    auto *session = local ? &m_local : &m_remote;
    for (const auto &entry : m_commonSubscriptions) {
        const auto subscription = entry.toMap();
        if (subscription.value("topic").toString() != topic) continue;
        for (const auto &current : session->subscriptions())
            if (current.toMap().value("topic").toString() == topic) return Mqtt::result();
        return session->saveSubscription(topic, subscription.value("qos").toInt(), true, {}, subscription.value("color").toString());
    }
    return Mqtt::result(QStringLiteral("常用订阅不存在，请重新加载。"));
}

QVariantMap MqttController::openCommonSubscriptionsFile() {
    if (!QFile::exists(m_commonPath)) {
        if (auto result = changeCommonSubscriptions([](QVariantList &) { return QString(); }); !result.value("ok").toBool()) return result;
    }
    return Mqtt::result(QDesktopServices::openUrl(QUrl::fromLocalFile(m_commonPath)) ? QString() : QStringLiteral("无法打开文件，请按显示的路径手动打开。"));
}

QString MqttController::thirdPartyNotices() const {
    QFile file(QStringLiteral(":/mqtt/MQTT-THIRD-PARTY.txt"));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QStringLiteral("第三方许可文件不可用。");
}

QVariantMap MqttController::saveConnection(const QVariantMap &input) {
    if (m_remote.active()) return Mqtt::result(QStringLiteral("请先断开终端 Broker，再修改连接配置。"));
    const auto config = normalizedConnection(input);
    if (QUuid::fromString(config.value("id").toString()).isNull()) return Mqtt::result(QStringLiteral("连接标识无效。"));
    if (auto error = validateWorkspace(config); !error.isEmpty()) return Mqtt::result(error);
    bool found = false;
    for (auto &entry : m_profiles) {
        if (entry.toMap().value("id") == config.value("id")) { entry = config; found = true; break; }
    }
    if (!found) {
        if (m_profiles.size() >= 64) return Mqtt::result(QStringLiteral("最多保存 64 个连接配置。"));
        m_profiles.append(config);
    }
    m_loading = true;
    m_remote.configure(config);
    m_loading = false;
    m_dirty = true;
    emit profilesChanged();
    return saveNow();
}

QVariantMap MqttController::selectConnection(const QString &id) {
    if (m_remote.active()) return Mqtt::result(QStringLiteral("请先断开当前终端 Broker，再切换配置。"));
    for (const auto &entry : m_profiles) {
        if (entry.toMap().value("id").toString() != id) continue;
        m_loading = true;
        const auto error = m_remote.configure(entry.toMap());
        m_loading = false;
        if (!error.isEmpty()) return Mqtt::result(error);
        m_dirty = true;
        emit profilesChanged();
        return saveNow();
    }
    return Mqtt::result(QStringLiteral("连接配置不存在。"));
}

QVariantMap MqttController::copyConnection(const QString &id) {
    for (const auto &entry : m_profiles) {
        auto config = entry.toMap();
        if (config.value("id").toString() != id) continue;
        QString credentialError;
        if (!CredentialAccess::loadMqtt(&config, &credentialError, m_credentials)) return storageResult(credentialError);
        const auto defaults = Mqtt::connectionDefaults();
        config.insert("id", defaults.value("id"));
        config.insert("clientId", defaults.value("clientId"));
        config.insert("name", config.value("name").toString() + QStringLiteral(" 副本"));
        config.insert("history", QVariantList{});
        return saveConnection(config);
    }
    return Mqtt::result(QStringLiteral("连接配置不存在。"));
}

QVariantMap MqttController::removeConnection(const QString &id) {
    if (m_remote.active()) return Mqtt::result(QStringLiteral("请先断开终端 Broker，再删除连接配置。"));
    if (m_profiles.size() == 1) return Mqtt::result(QStringLiteral("请至少保留一个连接配置，可以直接修改当前配置。"));
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].toMap().value("id").toString() != id) continue;
        m_profiles.removeAt(i);
        if (currentProfileId() == id) {
            m_loading = true;
            m_remote.configure(m_profiles.first().toMap());
            m_loading = false;
        }
        m_dirty = true;
        emit profilesChanged();
        return saveNow();
    }
    return Mqtt::result(QStringLiteral("连接配置不存在。"));
}

QVariantMap MqttController::saveBroker(const QVariantMap &input) {
    if (m_local.active()) return Mqtt::result(QStringLiteral("请先停止 Broker 并断开本机测试客户端。"));
    auto config = Mqtt::brokerDefaults();
    for (auto it = config.begin(); it != config.end(); ++it)
        if (input.contains(it.key())) it.value() = input.value(it.key());
    if (auto error = m_broker.configure(config); !error.isEmpty()) return Mqtt::result(error);
    m_dirty = true;
    return saveNow();
}

QVariantMap MqttController::connectLocalClient() {
    if (!m_broker.running()) return Mqtt::result(QStringLiteral("请先启动本机 Broker。"));
    const auto error = m_local.configure(m_broker.localClientConfiguration(m_local.configuration()));
    if (!error.isEmpty()) return Mqtt::result(error);
    return m_local.connectToBroker();
}
