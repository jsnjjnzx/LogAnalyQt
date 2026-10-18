#include "MqttController.h"
#include "common/AppPaths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <cmath>

MqttController::MqttController(QObject *parent)
    : MqttController(AppPaths::writableDataRoot(), parent) {}

MqttController::MqttController(const QString &dataRoot, QObject *parent, const QString &helperOverride)
    : QObject(parent), m_remote(this), m_local(this), m_broker(dataRoot, this, helperOverride),
      m_configPath(QDir(dataRoot).filePath("mqtt/connections.json")) {
    m_saveTimer.setInterval(500);
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { saveNow(); });
    load();
    m_loading = false;
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
