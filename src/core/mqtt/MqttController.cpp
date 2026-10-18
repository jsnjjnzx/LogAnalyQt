#include "MqttController.h"
#include "common/AppPaths.h"
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

MqttController::MqttController(QObject *parent)
    : MqttController(AppPaths::writableDataRoot(), parent) {}

MqttController::MqttController(const QString &dataRoot, QObject *parent, const QString &helperOverride)
    : QObject(parent), m_remote(this), m_local(this), m_broker(dataRoot, this, helperOverride),
      m_configPath(QDir(dataRoot).filePath("mqtt/connections.json")),
      m_commonPath(QDir(dataRoot).filePath("mqtt/common-subscriptions.json")) {
    m_saveTimer.setInterval(500);
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { saveNow(); });
    load();
    reloadCommonSubscriptions();
    if (!QFile::exists(m_commonPath) && QFile::exists(m_configPath) && !m_badFile) {
        changeCommonSubscriptions([this](QVariantList &subscriptions) {
            QSet<QString> topics;
            for (const auto &entry : subscriptions) topics.insert(entry.toMap().value("topic").toString());
            auto profiles = m_profiles;
            profiles.append(m_local.configuration());
            for (const auto &profile : profiles) {
                for (const auto &entry : profile.toMap().value("subscriptions").toList()) {
                    const auto saved = entry.toMap();
                    const auto topic = saved.value("topic").toString();
                    if (topics.contains(topic)) continue;
                    QVariantMap item{{"topic", topic}, {"qos", saved.value("qos", 0)},
                        {"color", saved.value("color", Mqtt::defaultSubscriptionColor(subscriptions.size()))}};
                    subscriptions.append(item); topics.insert(topic);
                }
            }
            return QString();
        });
    }
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
    QFile file(m_configPath);
    if (!file.exists()) return;
    auto invalid = [this](const QString &error) {
        m_badFile = true;
        m_storageError = QStringLiteral("原 MQTT 配置已保留，未自动覆盖：") + error;
    };
    if (!file.open(QIODevice::ReadOnly)) { invalid(file.errorString()); return; }
    if (file.size() > Mqtt::MaxConfigBytes) { invalid(QStringLiteral("配置文件超过 8 MiB")); return; }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject() || doc.object().value("schema").toInt() != 1) {
        invalid(QStringLiteral("文件格式或版本无效")); return;
    }
    const auto json = doc.object();
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
    const auto ratio = json.value("splitRatio").toDouble(0.56);
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
    const QJsonObject json{{"schema", 1}, {"connections", QJsonArray::fromVariantList(m_profiles)},
        {"selectedConnectionId", currentProfileId()}, {"broker", QJsonObject::fromVariantMap(m_broker.configuration())},
        {"localClient", QJsonObject::fromVariantMap(m_local.configuration())}, {"splitRatio", m_splitRatio}};
    const auto bytes = QJsonDocument(json).toJson(QJsonDocument::Indented);
    if (bytes.size() > Mqtt::MaxConfigBytes) return storageResult(QStringLiteral("配置超过 8 MiB，未保存；请减少大型草稿或连接数量。"));
    if (!QDir().mkpath(QFileInfo(m_configPath).absolutePath())) return storageResult(QStringLiteral("无法创建 MQTT 配置目录。"));
    QSaveFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return storageResult(QStringLiteral("MQTT 配置未保存：") + file.errorString());
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
    QFile file(m_commonPath);
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) return file.errorString();
    if (file.size() > Mqtt::MaxConfigBytes) return QStringLiteral("文件超过 8 MiB。");
    *bytes = file.readAll();
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(*bytes, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject() || json.object().value("schema").toInt() != 1
        || !json.object().value("subscriptions").isArray()) return QStringLiteral("JSON 格式或版本无效，原文件已保留；修复后点击重新加载。");
    const auto entries = json.object().value("subscriptions").toArray();
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
    if (!QDir().mkpath(QFileInfo(m_commonPath).absolutePath())) return commonResult(QStringLiteral("无法创建本地目录。"));
    QLockFile lock(m_commonPath + ".lock");
    if (!lock.tryLock(0)) return commonResult(lock.error() == QLockFile::LockFailedError
        ? QStringLiteral("文件正在被其他实例修改，请稍后重试。") : QStringLiteral("无法创建文件锁，请检查本地目录的写入权限。"));
    const bool existed = QFile::exists(m_commonPath);
    QVariantList saved;
    QByteArray before;
    if (auto error = readCommonSubscriptions(&saved, &before); !error.isEmpty()) return commonResult(error);
    auto updated = saved;
    if (auto error = change(updated); !error.isEmpty()) return commonResult(error);
    if (updated.size() > 1000) return commonResult(QStringLiteral("最多保存 1000 个常用订阅。"));
    if (!existed || updated != saved) {
        const auto bytes = QJsonDocument(QJsonObject{{"schema", 1}, {"subscriptions", QJsonArray::fromVariantList(updated)}}).toJson(QJsonDocument::Indented);
        if (bytes.size() > Mqtt::MaxConfigBytes) return commonResult(QStringLiteral("文件超过 8 MiB，未保存。"));
        // Preserve manual edits made since this operation read the file.
        QFile current(m_commonPath);
        if (current.exists() != existed || (existed && (!current.open(QIODevice::ReadOnly) || current.readAll() != before)))
            return commonResult(QStringLiteral("文件已被外部修改，请重新加载后重试。"));
        current.close();
        QSaveFile file(m_commonPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return commonResult(file.errorString());
    }
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
