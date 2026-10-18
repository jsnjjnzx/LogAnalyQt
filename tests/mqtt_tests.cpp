#include "core/mqtt/MqttController.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <functional>
#include <stdexcept>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>

namespace {
void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
bool waitFor(const std::function<bool()> &condition, int timeout = 6000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents();
    return condition();
}
int freePort() {
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0), "allocate test port");
    return server.serverPort();
}
QVariantMap findMessage(MqttSession *session, const QString &topic, bool received, const QString &format = "Hex") {
    for (int row = session->messageCount() - 1; row >= 0; --row) {
        const auto message = session->message(session->idAt(row), format);
        if (message.value("topic").toString() == topic && message.value("received").toBool() == received) return message;
    }
    return {};
}
bool subscribed(MqttSession *session) {
    const auto subscriptions = session->subscriptions();
    return !subscriptions.isEmpty() && subscriptions.first().toMap().value("state").toString().startsWith(QStringLiteral("已订阅"));
}
bool subscribedTo(MqttSession *session, const QString &topic) {
    for (const auto &entry : session->subscriptions()) {
        const auto sub = entry.toMap();
        if (sub.value("topic").toString() == topic && sub.value("state").toString().startsWith(QStringLiteral("已订阅"))) return true;
    }
    return false;
}
void formatsAndModel() {
    QByteArray bytes;
    require(Mqtt::decodePayload("00 68\nFF 00", "Hex", &bytes).isEmpty() && bytes == QByteArray::fromHex("0068ff00"), "binary Hex encoding must preserve NUL");
    require(!Mqtt::decodePayload("ABC", "Hex", &bytes).isEmpty(), "reject odd Hex");
    require(!Mqtt::decodePayload("ZZ", "Hex", &bytes).isEmpty(), "reject invalid Hex");
    require(Mqtt::decodePayload("", "Text", &bytes).isEmpty() && bytes.isEmpty(), "empty MQTT payload is valid");
    require(Mqtt::decodePayload("123", "JSON", &bytes).isEmpty() && bytes == "123", "accept scalar JSON");
    require(!Mqtt::decodePayload("{} {}", "JSON", &bytes).isEmpty(), "reject multiple JSON documents");
    require(Mqtt::decodePayload(" { \"value\" : 1 } ", "JSON", &bytes).isEmpty() && bytes == " { \"value\" : 1 } ", "JSON validation must not change bytes");
    require(!Mqtt::decodePayload(QString(Mqtt::MaxPayloadBytes + 1, 'a'), "Text", &bytes).isEmpty(), "payload size limit");
    require(Mqtt::validateSubscription("devices/+/status", 2).isEmpty(), "valid subscription wildcard");
    require(!Mqtt::validateSubscription("devices/#/status", 0).isEmpty(), "invalid wildcard");
    require(!Mqtt::publishError("devices/+", 0).isEmpty(), "publishing wildcard is invalid");
    auto config = Mqtt::connectionDefaults();
    config["port"] = 0;
    require(!Mqtt::validateConnection(config).isEmpty(), "invalid port");
    config["port"] = 1883.5;
    require(!Mqtt::validateConnection(config).isEmpty(), "fractional port");
    config["port"] = 1883;
    config["host"] = "mqtt://localhost";
    require(!Mqtt::validateConnection(config).isEmpty(), "host is not a URL");

    MqttMessageModel model(nullptr, 3, 20);
    for (int i = 0; i < 5; ++i) {
        Mqtt::Message message;
        message.id = i + 1; message.topic = "a"; message.payload = QByteArray::number(i); message.time = QDateTime::currentDateTime();
        model.append({message});
    }
    require(model.rowCount() == 3 && model.messageAt(0)->id == 3 && model.evictedCount() == 2, "evict oldest rows at limit");
    Mqtt::Message large; large.id = 6; large.topic = "a"; large.payload = QByteArray(18, 'x');
    model.append({large});
    require(model.rowCount() == 1 && model.payloadBytes() == 19, "byte limit bounds memory, not just row count");
    model.clear();
    require(model.rowCount() == 0 && model.payloadBytes() == 0, "clear bounded cache");
}

void settings(const QString &helper) {
    QTemporaryDir directory;
    QString id;
    {
        MqttController controller(directory.path(), nullptr, helper);
        auto config = controller.remote()->configuration();
        id = config.value("id").toString();
        config["username"] = "internal_test_user";
        config["password"] = "plain_test_password";
        require(controller.saveConnection(config).value("ok").toBool(), "save connection");
        require(controller.remote()->saveSubscription("test/#", 1).value("ok").toBool(), "save subscription");
        require(controller.remote()->subscriptions().first().toMap().value("visible").toBool(), "new subscriptions are visible by default");
        require(controller.remote()->setSubscriptionAppearance("test/#", "#AABBCC", false).value("ok").toBool(), "save color and display choice");
        require(controller.remote()->saveSubscription("renamed/#", 2, false, "test/#").value("ok").toBool(), "edit topic, QoS and enabled state");
        const auto renamed = controller.remote()->subscriptions().first().toMap();
        require(renamed.value("topic").toString() == "renamed/#" && renamed.value("color").toString() == "#aabbcc" && !renamed.value("visible").toBool(), "subscription edits retain color and display choice");
        require(controller.remote()->saveSubscription("test/#", 1, true, "renamed/#").value("ok").toBool(), "restore edited topic");
        controller.remote()->updateDraft({{"topic", "test/request"}, {"payload", "unfinished {"}, {"format", "JSON"}, {"qos", 1}, {"retain", false}});
        controller.setSplitRatio(0.62);
        require(controller.saveNow().value("ok").toBool(), "save workspace");
        QFile file(controller.configPath());
        require(file.open(QIODevice::ReadOnly) && file.readAll().contains("plain_test_password"), "user-authorized plaintext password storage");
    }
    {
        MqttController controller(directory.path(), nullptr, helper);
        require(controller.storageError().isEmpty(), "restore settings without errors");
        require(controller.currentProfileId() == id, "profile identity persisted");
        require(controller.remote()->configuration().value("password").toString() == "plain_test_password", "restore plaintext password");
        require(controller.remote()->draft().value("payload").toString() == "unfinished {", "unfinished draft survives restart");
        require(controller.remote()->subscriptions().size() == 1 && qAbs(controller.splitRatio() - 0.62) < 0.001, "subscription and layout persist");
        require(controller.remote()->subscriptions().first().toMap().value("color").toString() == "#aabbcc"
            && !controller.remote()->subscriptions().first().toMap().value("visible").toBool(), "subscription colors and visibility persist");
        require(!controller.remote()->active() && !controller.broker()->active(), "restart must not open network connections");
        require(controller.copyConnection(id).value("ok").toBool() && controller.profiles().size() == 2 && controller.currentProfileId() != id, "duplicate uses a distinct profile/client ID");
        require(controller.selectConnection(id).value("ok").toBool(), "select saved profile");
    }
    const auto path = directory.filePath("mqtt/connections.json");
    { QFile file(path); require(file.open(QIODevice::WriteOnly), "open corrupt fixture"); file.write("broken-config"); }
    {
        MqttController controller(directory.path(), nullptr, helper);
        require(!controller.storageError().isEmpty(), "corrupt config is reported");
        require(!controller.saveNow().value("ok").toBool(), "corrupt config is not silently overwritten");
    }
    QFile preserved(path); require(preserved.open(QIODevice::ReadOnly) && preserved.readAll() == "broken-config", "preserve corrupt original");
}

void subscriptionDisplay(const QString &helper) {
    MqttMessageModel model;
    MqttMessageFilter filter;
    filter.setSourceModel(&model);
    QVariantList rules{
        QVariantMap{{"topic", "devices/#"}, {"color", "#60a5fa"}, {"visible", true}},
        QVariantMap{{"topic", "devices/+/status"}, {"color", "#34d399"}, {"visible", false}},
        QVariantMap{{"topic", "devices/1/status"}, {"color", "#fbbf24"}, {"visible", false}}};
    Mqtt::Message sample; sample.id = 1; sample.topic = "devices/1/status"; sample.payload = "value";
    model.append({sample}); filter.setSubscriptions(rules);
    require(filter.rowCount() == 1 && filter.topicColor(sample.topic) == "#fbbf24", "visible overlap uses most specific color even if that rule is hidden");
    rules.removeLast(); filter.setSubscriptions(rules);
    require(filter.topicColor(sample.topic) == "#34d399", "single-level wildcard outranks broad wildcard");
    filter.setSubscriptions({QVariantMap{{"topic", "devices/+/status"}, {"color", "#60a5fa"}}, QVariantMap{{"topic", "devices/1/+"}, {"color", "#34d399"}}});
    require(filter.topicColor(sample.topic) == "#60a5fa", "equally specific colors follow list order");
    filter.setSubscriptions({QVariantMap{{"topic", "#"}, {"color", "#60a5fa"}}});
    sample.topic = "$SYS/status";
    require(!filter.acceptsMessage(sample) && filter.topicColor(sample.topic).isEmpty(), "MQTT system topics respect wildcard rules");

    QTemporaryDir directory;
    MqttController controller(directory.path(), nullptr, helper);
    auto broker = Mqtt::brokerDefaults(); broker["host"] = "127.0.0.1"; broker["port"] = freePort();
    require(controller.saveBroker(broker).value("ok").toBool() && controller.broker()->start().value("ok").toBool(), "start topic display fixture");
    require(waitFor([&] { return controller.localClient()->connected(); }), "topic display publisher ready");
    auto *receiver = controller.remote();
    auto config = receiver->configuration(); config["port"] = broker["port"];
    config["subscriptions"] = QVariantList{QVariantMap{{"topic", "color/a"}, {"qos", 1}, {"enabled", true}}};
    require(controller.saveConnection(config).value("ok").toBool(), "load legacy subscription without appearance fields");
    require(receiver->subscriptions().first().toMap().value("visible").toBool() && !receiver->subscriptions().first().toMap().value("color").toString().isEmpty(), "legacy subscriptions default to shown with a color");
    receiver->saveSubscription("color/b", 1, true, {}, "#34d399");
    receiver->saveSubscription("color/c", 1, true, {}, "#fbbf24");
    require(receiver->connectToBroker().value("ok").toBool() && waitFor([&] { return subscribedTo(receiver, "color/a") && subscribedTo(receiver, "color/b") && subscribedTo(receiver, "color/c"); }), "three concurrent subscriptions");
    receiver->setFilter("received", "", "");
    for (const auto &topic : {"color/a", "color/b", "color/c"}) controller.localClient()->publishMessage(topic, "initial", "Text", 1, false);
    require(waitFor([&] { return receiver->receivedCount() == 3 && receiver->messageCount() == 3; }), "all three topics initially visible");
    const auto before = receiver->configuration();
    require(!receiver->setSubscriptionAppearance("color/a", "red", false).value("ok").toBool() && receiver->configuration() == before, "invalid color leaves settings unchanged");
    receiver->setSubscriptionAppearance("color/b", "#34d399", false);
    require(receiver->messageCount() == 2, "show two subscribed topics");
    receiver->setSubscriptionAppearance("color/c", "#fbbf24", false);
    require(receiver->messageCount() == 1, "show one subscribed topic");
    for (const auto &topic : {"color/b", "color/c"}) controller.localClient()->publishMessage(topic, "hidden", "Text", 1, false);
    require(waitFor([&] { return receiver->receivedCount() == 5; }) && receiver->messageCount() == 1, "hidden topics stay subscribed and keep receiving");
    receiver->setSubscriptionAppearance("color/b", "#f472b6", true);
    require(receiver->messageCount() == 3 && findMessage(receiver, "color/b", true).value("topicColor").toString() == "#f472b6", "re-show cached messages with updated color");
    const auto exportPath = directory.filePath("selected-topics.jsonl");
    require(receiver->exportMessages(QUrl::fromLocalFile(exportPath)).value("ok").toBool() && waitFor([&] { return !receiver->exportBusy(); }), "export selected topics");
    QFile exported(exportPath); require(exported.open(QIODevice::ReadOnly), "open selected export");
    int exportedRows = 0;
    while (!exported.atEnd()) {
        const auto entry = QJsonDocument::fromJson(exported.readLine()).object();
        require(entry.value("topic").toString() != "color/c", "display-only export excludes hidden topic");
        ++exportedRows;
    }
    require(exportedRows == 3, "export includes selected-topic history only");
    receiver->setAllSubscriptionsVisible(false);
    require(receiver->messageCount() == 0, "unchecking every topic hides all received messages");
    receiver->setAllSubscriptionsVisible(true);
    require(receiver->messageCount() == 5, "select all restores hidden history without resubscribing");
    require(subscribedTo(receiver, "color/a") && subscribedTo(receiver, "color/b") && subscribedTo(receiver, "color/c"), "display choices preserve protocol subscription states");
    receiver->disconnectFromBroker(); controller.localClient()->disconnectFromBroker(); controller.broker()->stop();
    require(waitFor([&] { return !receiver->active() && !controller.localClient()->active() && !controller.broker()->active(); }), "topic display fixture cleanup");
}

void integration(const QString &helper) {
    QTemporaryDir directory;
    MqttController controller(directory.path(), nullptr, helper);
    const int port = freePort();
    auto broker = Mqtt::brokerDefaults();
    broker["host"] = "127.0.0.1"; broker["port"] = port;
    require(controller.saveBroker(broker).value("ok").toBool(), "configure loopback broker");
    require(controller.broker()->start().value("ok").toBool(), "start broker request");
    require(waitFor([&] { return controller.broker()->running(); }), qPrintable(controller.broker()->detail()));
    auto *local = controller.localClient();
    auto *remote = controller.remote();
    const bool localReady = waitFor([&] { return local->connected() && subscribed(local); });
    if (!localReady) QTextStream(stderr) << "Broker events:\n" << controller.broker()->events().join('\n')
        << "\nLocal endpoint: " << local->configuration().value("host").toString() << ':' << local->configuration().value("port").toInt() << '\n';
    require(localReady, qPrintable(local->detail()));
    require(waitFor([&] { return controller.broker()->clients().size() == 1; }), "local client connection event");
    auto config = remote->configuration();
    config["host"] = "127.0.0.1"; config["port"] = port;
    config["reconnectSeconds"] = 1; config["timeoutSeconds"] = 2;
    require(controller.saveConnection(config).value("ok").toBool(), "configure remote test client");
    require(remote->saveSubscription("test/+/data", 2).value("ok").toBool(), "set remote wildcard subscription");
    require(remote->connectToBroker().value("ok").toBool(), "connect remote client");
    require(waitFor([&] { return remote->connected() && subscribed(remote); }), qPrintable(remote->detail()));
    require(!controller.selectConnection(controller.currentProfileId()).value("ok").toBool(), "do not replace live connection config");

    for (int qos = 0; qos <= 2; ++qos) {
        const QString topic = QStringLiteral("test/qos%1/data").arg(qos);
        require(remote->publishMessage(topic, "00 68 FF 00", "Hex", qos, false).value("ok").toBool(), "publish binary payload");
        require(waitFor([&] { return !findMessage(local, topic, true).isEmpty(); }), "local test client receives remote publish");
        require(findMessage(local, topic, true).value("payload").toString() == "00 68 FF 00", "binary payload exact match");
        require(waitFor([&] { return findMessage(remote, topic, false).value("delivery").toString().contains(qos ? QStringLiteral("Broker 已确认") : QStringLiteral("无确认")); }), "QoS-specific delivery state");
    }
    require(waitFor([&] { return controller.broker()->clients().size() == 2; }), "connected client list");
    require(!remote->publishMessage("bad/+", "{}", "JSON", 0, false).value("ok").toBool(), "invalid publication never sent");
    require(!remote->publishMessage("bad/payload", "{", "JSON", 0, false).value("ok").toBool(), "invalid JSON never sent");
    require(remote->setFilter("received", "test/+/data", "68 ff").value("ok").toBool(), "topic and Hex content filtering");
    require(waitFor([&] { return remote->messageCount() == 3; }), "filter only matching received rows");
    require(!remote->setFilter("all", "bad/#/topic", "").value("ok").toBool() && remote->messageCount() == 3, "invalid filter preserves last valid view");
    remote->setFilter("all", "", "");

    local->setPaused(true);
    const int visible = local->messageCount();
    const auto before = local->receivedCount();
    require(remote->publishMessage("test/pause/data", "paused", "Text", 1, false).value("ok").toBool(), "publish while display paused");
    require(waitFor([&] { return local->receivedCount() > before; }), "pause does not unsubscribe or stop reception");
    require(local->messageCount() == visible && local->pendingCount() > 0, "paused view remains stable");
    local->setPaused(false);
    require(!findMessage(local, "test/pause/data", true).isEmpty(), "resume appends buffered data");

    const auto burstBefore = local->receivedCount();
    QElapsedTimer responsiveness;
    responsiveness.start();
    qint64 lastTick = 0, maxTickGap = 0;
    QTimer heartbeat;
    heartbeat.setInterval(20);
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] {
        const auto now = responsiveness.elapsed();
        maxTickGap = qMax(maxTickGap, now - lastTick); lastTick = now;
    });
    heartbeat.start();
    for (int i = 0; i < 1000; ++i)
        require(remote->publishMessage("test/burst/data", QString::number(i), "Text", 0, false).value("ok").toBool(), "burst submission");
    require(waitFor([&] { return local->receivedCount() >= burstBefore + 1000; }, 10000), "continuous local receive");
    heartbeat.stop();
    require(lastTick > 0 && maxTickGap < 750, "message processing blocked the main event loop");

    require(remote->publishMessage("retained/value", "retained-payload", "Text", 1, true).value("ok").toBool(), "publish retained value");
    require(waitFor([&] { return findMessage(remote, "retained/value", false).value("delivery").toString().contains(QStringLiteral("已确认")); }), "retain ack");
    remote->clearMessages();
    MqttSession fresh;
    auto freshConfig = Mqtt::connectionDefaults();
    freshConfig["port"] = port; freshConfig["autoReconnect"] = false;
    require(fresh.configure(freshConfig).isEmpty(), "configure independent receiver");
    fresh.saveSubscription("retained/value", 1);
    fresh.connectToBroker();
    require(waitFor([&] { return !findMessage(&fresh, "retained/value", true, "Text").isEmpty(); }), "clear UI does not delete retained broker data");
    const auto retained = findMessage(&fresh, "retained/value", true, "Text");
    require(retained.value("retained").toBool() && retained.value("payload").toString() == "retained-payload", "retained flag and payload");
    fresh.disconnectFromBroker();
    require(waitFor([&] { return !fresh.active(); }), "disconnect independent receiver");

    // MQTT 3.1.1 CONNECT from an independent raw socket, then an abrupt close.
    const auto mqttString = [](const QByteArray &value) { return QByteArray(1, static_cast<char>(value.size() >> 8)) + QByteArray(1, static_cast<char>(value.size())) + value; };
    QByteArray body = QByteArray::fromHex("00044d515454040e003c") + mqttString("raw-will-client") + mqttString("will/test") + mqttString("offline");
    QTcpSocket raw;
    raw.connectToHost(QHostAddress::LocalHost, port);
    require(raw.waitForConnected(2000), "raw test client connects");
    raw.write(QByteArray(1, 0x10) + QByteArray(1, static_cast<char>(body.size())) + body);
    require(raw.waitForBytesWritten(1000) && raw.waitForReadyRead(2000), "raw MQTT CONNACK");
    require(raw.readAll().startsWith(QByteArray::fromHex("20020000")), "raw client accepted");
    raw.abort();
    require(waitFor([&] { return findMessage(local, "will/test", true, "Text").value("payload").toString() == "offline"; }), "abrupt disconnect triggers will");

    const auto exportPath = directory.filePath("messages.jsonl");
    require(local->exportMessages(QUrl::fromLocalFile(exportPath), false).value("ok").toBool(), "begin export");
    require(waitFor([&] { return !local->exportBusy(); }), "finish export");
    QFile exported(exportPath);
    require(exported.open(QIODevice::ReadOnly), "export exists");
    bool binaryExported = false;
    while (!exported.atEnd()) {
        const auto row = QJsonDocument::fromJson(exported.readLine()).object();
        if (row.value("topic").toString() == "test/qos1/data")
            binaryExported = QByteArray::fromBase64(row.value("payloadBase64").toString().toLatin1()) == QByteArray::fromHex("0068ff00");
    }
    require(binaryExported, "export preserves binary payload");

    remote->disconnectFromBroker();
    require(waitFor([&] { return !remote->active(); }), "remote disconnect");
    config = remote->configuration(); config["protocol"] = 5; config["cleanSession"] = false; config["sessionExpiry"] = 30;
    require(controller.saveConnection(config).value("ok").toBool(), "configure MQTT 5 session");
    remote->connectToBroker();
    require(waitFor([&] { return remote->connected() && subscribed(remote); }), "MQTT 5 connection and subscribe");
    require(remote->publishMessage("test/v5/data", "{\"version\":5}", "JSON", 2, false).value("ok").toBool(), "MQTT 5 QoS 2 publication");
    require(waitFor([&] { return findMessage(local, "test/v5/data", true, "Text").value("payload").toString() == "{\"version\":5}"; }), "MQTT 5 payload");

    remote->saveSubscription("persist/remove", 1);
    require(waitFor([&] { return subscribedTo(remote, "persist/remove"); }), "persistent subscription established");
    remote->disconnectFromBroker();
    require(waitFor([&] { return !remote->active(); }), "disconnect persistent session");
    require(remote->removeSubscription("persist/remove").value("ok").toBool(), "remove subscription while offline");
    remote->connectToBroker();
    require(waitFor([&] { return remote->connected() && remote->configuration().value("cancelledSubscriptions").toStringList().isEmpty(); }), "offline cancellation synchronizes on reconnect");
    remote->saveSubscription("after/barrier", 0);
    require(waitFor([&] { return subscribedTo(remote, "after/barrier"); }), "subscription barrier");
    const auto beforeCancelledPublish = remote->receivedCount();
    local->publishMessage("persist/remove", "must not arrive", "Text", 1, false);
    waitFor([&] { return remote->receivedCount() > beforeCancelledPublish; }, 400);
    require(remote->receivedCount() == beforeCancelledPublish, "cancelled persistent subscription stays cancelled in the background");

    controller.broker()->stop();
    require(waitFor([&] { return !controller.broker()->active() && remote->state() == "reconnecting"; }), "network interruption enters reconnect state");
    require(!remote->publishMessage("test/offline/data", "do not replay", "Text", 1, false).value("ok").toBool(), "offline publish is rejected, not queued");
    controller.broker()->start();
    require(waitFor([&] { return remote->connected() && subscribed(remote) && local->connected(); }), "restart restores desired subscriptions");
    require(findMessage(local, "test/offline/data", true).isEmpty(), "offline command was not replayed");
    remote->disconnectFromBroker();
    local->disconnectFromBroker();
    require(waitFor([&] { return !remote->active() && !local->active(); }), "stop both test clients");
    controller.broker()->stop();
    require(waitFor([&] { return !controller.broker()->active(); }), "stop managed broker");
    QTcpServer portCheck;
    require(portCheck.listen(QHostAddress::LocalHost, port), "stop releases listener port");
    portCheck.close();

    // Port conflicts must not attach to or stop an unrelated listener.
    QTcpServer occupied;
    require(occupied.listen(QHostAddress::LocalHost, port), "occupy test port");
    controller.broker()->start();
    require(waitFor([&] { return controller.broker()->state() == "error"; }), "report occupied port");
    require(occupied.isListening(), "leave unrelated listener untouched");
}

void authentication(const QString &helper) {
    QTemporaryDir directory;
    MqttController controller(directory.path(), nullptr, helper);
    auto broker = Mqtt::brokerDefaults();
    broker["host"] = "127.0.0.1"; broker["port"] = freePort();
    broker["anonymous"] = false; broker["username"] = "debug"; broker["password"] = "test-password";
    require(controller.saveBroker(broker).value("ok").toBool(), "save broker credentials");
    controller.broker()->start();
    require(waitFor([&] { return controller.localClient()->connected(); }), "local test client uses configured credentials");
    auto config = controller.remote()->configuration();
    config["port"] = broker.value("port"); config["username"] = "debug"; config["password"] = "wrong-password";
    controller.saveConnection(config);
    controller.remote()->connectToBroker();
    require(waitFor([&] { return controller.remote()->state() == "error"; }), "bad credentials reported without endless reconnect");
    require(controller.broker()->running(), "bad client cannot stop broker");
    controller.broker()->stop();
    require(waitFor([&] { return !controller.broker()->active(); }), "authenticated broker stops");
}

QPair<QString, QString> makeCertificate(const QString &directory, const QString &prefix, bool encrypted = false) {
    auto *context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    require(context && EVP_PKEY_keygen_init(context) > 0 && EVP_PKEY_CTX_set_rsa_keygen_bits(context, 2048) > 0, "create test key context");
    EVP_PKEY *key = nullptr;
    require(EVP_PKEY_keygen(context, &key) > 0, "generate test key");
    EVP_PKEY_CTX_free(context);
    auto *cert = X509_new();
    require(cert, "create test certificate");
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert), -3600);
    X509_gmtime_adj(X509_getm_notAfter(cert), 86400);
    X509_set_pubkey(cert, key);
    auto *name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>("MQTT test localhost"), -1, -1, 0);
    X509_set_issuer_name(cert, name);
    X509V3_CTX extensions{};
    X509V3_set_ctx(&extensions, cert, cert, nullptr, nullptr, 0);
    for (const auto &entry : {qMakePair(NID_basic_constraints, "critical,CA:TRUE"), qMakePair(NID_subject_alt_name, "IP:127.0.0.1,DNS:localhost")}) {
        auto *extension = X509V3_EXT_conf_nid(nullptr, &extensions, entry.first, const_cast<char *>(entry.second));
        require(extension, "create certificate extension");
        X509_add_ext(cert, extension, -1); X509_EXTENSION_free(extension);
    }
    require(X509_sign(cert, key, EVP_sha256()) > 0, "sign test certificate");
    auto *certBio = BIO_new(BIO_s_mem());
    auto *keyBio = BIO_new(BIO_s_mem());
    unsigned char password[] = "test-key";
    require(PEM_write_bio_X509(certBio, cert) && PEM_write_bio_PrivateKey(keyBio, key, encrypted ? EVP_aes_256_cbc() : nullptr,
        encrypted ? password : nullptr, encrypted ? 8 : 0, nullptr, nullptr), "encode certificate");
    const auto certPath = QDir(directory).filePath(prefix + ".pem");
    const auto keyPath = QDir(directory).filePath(prefix + ".key");
    for (const auto &entry : {qMakePair(certPath, certBio), qMakePair(keyPath, keyBio)}) {
        char *data = nullptr;
        const auto size = BIO_get_mem_data(entry.second, &data);
        QFile file(entry.first);
        require(file.open(QIODevice::WriteOnly) && file.write(data, size) == size, "write test certificate");
    }
    BIO_free(certBio); BIO_free(keyBio); X509_free(cert); EVP_PKEY_free(key);
    return {certPath, keyPath};
}

void tlsConnections(const QString &helper) {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("MQTT 测试"));
    require(QDir().mkpath(path), "create non-ASCII runtime directory");
    const auto certificate = makeCertificate(path, "server");
    const auto wrongCertificate = makeCertificate(path, "unrelated");
    const auto encryptedCertificate = makeCertificate(path, "encrypted", true);
    MqttController controller(path, nullptr, helper);
    auto broker = Mqtt::brokerDefaults();
    broker["host"] = "127.0.0.1"; broker["port"] = freePort(); broker["tls"] = true;
    broker["certFile"] = certificate.first; broker["keyFile"] = certificate.second; broker["caFile"] = certificate.first;
    require(controller.saveBroker(broker).value("ok").toBool(), "configure TLS broker");
    require(controller.broker()->start().value("ok").toBool(), "start TLS broker");
    require(waitFor([&] { return controller.localClient()->connected() && subscribed(controller.localClient()); }), "TLS test client verifies configured CA");
    auto config = controller.remote()->configuration();
    config["port"] = broker.value("port"); config["tls"] = true; config["caFile"] = certificate.first;
    config["host"] = "localhost";
    controller.saveConnection(config);
    controller.remote()->connectToBroker();
    require(waitFor([&] { return controller.remote()->connected(); }), "TLS remote client connects");
    controller.remote()->publishMessage("tls/data", QStringLiteral("{\"数值\":1}"), "JSON", 1, false);
    require(waitFor([&] { return findMessage(controller.localClient(), "tls/data", true, "Text").value("payload").toString() == QStringLiteral("{\"数值\":1}"); }), "TLS preserves Unicode JSON bytes");
    controller.remote()->disconnectFromBroker();
    require(waitFor([&] { return !controller.remote()->active(); }), "TLS disconnect");
    config["caFile"] = wrongCertificate.first;
    controller.saveConnection(config);
    controller.remote()->connectToBroker();
    require(waitFor([&] { return controller.remote()->state() == "error"; }), "wrong CA is reported without endless reconnect");
    config["caFile"] = certificate.first;
    config["certFile"] = encryptedCertificate.first;
    config["keyFile"] = encryptedCertificate.second;
    controller.saveConnection(config);
    controller.remote()->connectToBroker();
    require(waitFor([&] { return controller.remote()->state() == "error"; }, 2000), "encrypted private key must not block on a console prompt");
    controller.broker()->stop();
    require(waitFor([&] { return !controller.broker()->active(); }), "TLS broker stops");
}

void oversizedIncomingPacket() {
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0), "oversize fixture listener");
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
            socket->readAll();
            if (socket->property("replied").toBool()) return;
            socket->setProperty("replied", true);
            QByteArray header(1, 0x30);
            unsigned int size = 3 * 1024 * 1024;
            do { const auto digit = size % 128; size /= 128; header.append(static_cast<char>(digit | (size ? 128 : 0))); } while (size);
            socket->write(QByteArray::fromHex("20020000") + header);
        });
    });
    MqttSession client;
    auto config = Mqtt::connectionDefaults(); config["port"] = server.serverPort();
    client.configure(config); client.connectToBroker();
    require(waitFor([&] { return client.state() == "error"; }), "reject oversized packet before waiting for its body");
    require(client.messageCount() == 0, "oversized packet is not buffered/displayed");
}

void interruptedQos(int qos) {
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0), "in-flight QoS fixture listener");
    int connections = 0;
    int originalMid = 0;
    bool resumed = false;
    bool invalidRetry = false;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto *socket = server.nextPendingConnection();
        const int connection = ++connections;
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, connection] {
            QByteArray buffer = socket->property("buffer").toByteArray() + socket->readAll();
            while (buffer.size() >= 2) {
                int offset = 1, length = 0, multiplier = 1;
                bool complete = false;
                while (offset < buffer.size() && offset <= 4) {
                    const auto digit = static_cast<unsigned char>(buffer[offset++]);
                    length += (digit & 127) * multiplier;
                    multiplier *= 128;
                    if (!(digit & 128)) { complete = true; break; }
                }
                if (!complete || buffer.size() < offset + length) break;
                const auto header = static_cast<unsigned char>(buffer[0]);
                const auto payload = buffer.mid(offset, length);
                buffer.remove(0, offset + length);
                if ((header & 0xf0) == 0x10) {
                    socket->write(connection == 1 ? QByteArray::fromHex("20020000") : QByteArray::fromHex("20020100"));
                } else if ((header & 0xf0) == 0x30 && payload.size() >= 4) {
                    const int topicSize = static_cast<unsigned char>(payload[0]) * 256 + static_cast<unsigned char>(payload[1]);
                    if (payload.size() < topicSize + 4) { invalidRetry = true; break; }
                    const auto midBytes = payload.mid(topicSize + 2, 2);
                    const int mid = static_cast<unsigned char>(midBytes[0]) * 256 + static_cast<unsigned char>(midBytes[1]);
                    if (connection == 1) {
                        originalMid = mid;
                        if (qos == 2) socket->write(QByteArray::fromHex("5002") + midBytes);
                        socket->disconnectFromHost();
                    } else {
                        invalidRetry = qos != 1 || mid != originalMid || !(header & 8);
                        if (qos == 1) socket->write(QByteArray::fromHex("4002") + midBytes);
                        resumed = qos == 1;
                    }
                } else if ((header & 0xf0) == 0x60 && payload.size() == 2 && connection > 1) {
                    const int mid = static_cast<unsigned char>(payload[0]) * 256 + static_cast<unsigned char>(payload[1]);
                    invalidRetry = qos != 2 || mid != originalMid;
                    socket->write(QByteArray::fromHex("7002") + payload);
                    resumed = true;
                }
            }
            socket->setProperty("buffer", buffer);
        });
    });
    MqttSession client;
    auto config = Mqtt::connectionDefaults();
    config["port"] = server.serverPort(); config["cleanSession"] = false; config["reconnectSeconds"] = 1;
    client.configure(config); client.connectToBroker();
    require(waitFor([&] { return client.connected(); }), "in-flight test connection");
    client.publishMessage("inflight/test", "one user operation", "Text", qos, false);
    require(waitFor([&] { return findMessage(&client, "inflight/test", false).value("delivery").toString() == QStringLiteral("Broker 已确认"); }), "complete in-flight QoS after reconnection");
    require(resumed && !invalidRetry && connections >= 2, "preserve packet ID and correct QoS retry phase");
    require(client.sentCount() == 1, "protocol retry is not a new user publication");
    client.disconnectFromBroker();
    require(waitFor([&] { return !client.active(); }), "in-flight fixture cleanup");
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) { QTextStream(stderr) << "usage: mqtt_tests <broker-helper>\n"; return 2; }
    try {
        formatsAndModel();
        QTextStream(stdout) << "PASS formats, validation and bounded cache\n";
        settings(QString::fromLocal8Bit(argv[1]));
        QTextStream(stdout) << "PASS plaintext settings, restart and corrupt-file preservation\n";
        subscriptionDisplay(QString::fromLocal8Bit(argv[1]));
        QTextStream(stdout) << "PASS subscription colors, overlap, display-only selection, hidden reception and history restoration\n";
        integration(QString::fromLocal8Bit(argv[1]));
        QTextStream(stdout) << "PASS broker, QoS 0/1/2, binary, filters, pause, retain, will, export, MQTT 5 and port ownership\n";
        authentication(QString::fromLocal8Bit(argv[1]));
        QTextStream(stdout) << "PASS broker authentication and failure states\n";
        tlsConnections(QString::fromLocal8Bit(argv[1]));
        QTextStream(stdout) << "PASS TLS, certificates and Unicode paths/payloads\n";
        oversizedIncomingPacket();
        QTextStream(stdout) << "PASS incoming packet allocation bound\n";
        interruptedQos(1);
        interruptedQos(2);
        QTextStream(stdout) << "PASS in-flight QoS recovery with stable packet IDs\n";
    } catch (const std::exception &error) {
        QTextStream(stderr) << "FAIL MQTT: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
