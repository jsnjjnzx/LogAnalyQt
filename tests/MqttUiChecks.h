#pragma once

#include "core/mqtt/MqttController.h"
#include "common/AppPaths.h"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTcpServer>
#include <QThread>
#include <functional>

// Only invoked by --ui-smoke --mqtt-checks in a disposable app/data directory.
inline QString checkMqttUi(QQuickWindow *window, const QString &screenshot, bool brokerView) {
    auto require = [](bool ok, const QString &message) { if (!ok) throw message; };
    auto waitFor = [](const std::function<bool()> &condition, int timeout = 6000) {
        QElapsedTimer timer; timer.start();
        while (!condition() && timer.elapsed() < timeout) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(5);
        }
        QCoreApplication::processEvents();
        return condition();
    };
    auto *controller = window->findChild<MqttController *>("mqttController");
    try {
        require(controller, "MQTT controller missing");
        require(controller->configPath() == QDir(AppPaths::writableDataRoot()).filePath("mqtt/connections.json"), "MQTT settings must use the isolated app data root");
        require(QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, 15)), "MQTT navigation failed");
        auto *page = window->findChild<QQuickItem *>("page-15");
        require(page && page->isVisible(), "MQTT page not visible");
        std::function<QQuickItem *(QQuickItem *, const QString &)> findItem = [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
            if (parent->objectName() == name) return parent;
            for (auto *child : parent->childItems()) if (auto *found = findItem(child, name)) return found;
            return nullptr;
        };
        auto item = [&](const char *name) {
            QQuickItem *found = nullptr;
            require(waitFor([&] { found = findItem(window->contentItem(), QLatin1String(name)); return found; }), QStringLiteral("Missing MQTT control: ") + QLatin1String(name));
            return found;
        };
        auto settle = [&] {
            bool frame = false;
            QEventLoop loop;
            QObject::connect(window, &QQuickWindow::frameSwapped, &loop, [&] { frame = true; loop.quit(); }, Qt::QueuedConnection);
            QTimer::singleShot(3000, &loop, &QEventLoop::quit);
            window->update(); loop.exec();
            require(frame, "MQTT frame not rendered");
        };
        auto click = [&](QQuickItem *target) {
            require(target && target->isVisible() && target->isEnabled(), "MQTT click target unavailable");
            const QPointF point = target->mapToScene({target->width() / 2, target->height() / 2});
            QMouseEvent down(QEvent::MouseButtonPress, point, window->mapToGlobal(point.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent up(QEvent::MouseButtonRelease, point, window->mapToGlobal(point.toPoint()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &down); QCoreApplication::sendEvent(window, &up);
            settle();
        };
        QTcpServer available;
        require(available.listen(QHostAddress::LocalHost, 0), "Could not allocate MQTT UI test port");
        const int port = available.serverPort(); available.close();
        auto broker = Mqtt::brokerDefaults(); broker["host"] = "127.0.0.1"; broker["port"] = port;
        require(controller->saveBroker(broker).value("ok").toBool(), "UI broker configuration save");
        QMetaObject::invokeMethod(page, "changeMode", Q_ARG(QVariant, 1)); settle();
        click(item("mqtt-broker-action"));
        require(waitFor([&] { return controller->localClient()->connected(); }), "UI start button did not start/connect local broker");
        auto remote = controller->remote()->configuration();
        remote["name"] = QStringLiteral("本机 MQTT 验收"); remote["host"] = "127.0.0.1"; remote["port"] = port;
        require(controller->saveConnection(remote).value("ok").toBool(), "UI remote profile save");
        controller->remote()->saveSubscription("terminal/001/command", 1, true, {}, "#60a5fa");
        QMetaObject::invokeMethod(page, "changeMode", Q_ARG(QVariant, 0)); settle();
        for (const auto &subscription : QList<QPair<QString, QString>>{{"terminal/001/telemetry", "#34d399"}, {"terminal/001/status", "#fbbf24"}}) {
            click(item("mqtt-add-subscription"));
            item("mqtt-subscription-topic")->setProperty("text", subscription.first);
            item("mqtt-subscription-color")->setProperty("text", subscription.second);
            click(item("mqtt-save-subscription"));
        }
        require(controller->remote()->subscriptions().size() == 3, "UI must create three colored subscriptions");
        click(item("mqtt-connection-action"));
        require(waitFor([&] {
            if (!controller->remote()->connected()) return false;
            for (const auto &subscription : controller->remote()->subscriptions())
                if (!subscription.toMap().value("state").toString().startsWith(QStringLiteral("已订阅"))) return false;
            return true;
        }), "UI connect button/subscriptions");
        item("mqtt-send-topic")->setProperty("text", "terminal/001/command");
        const QString command = QStringLiteral("{\n  \"request_id\": \"local_test_001\",\n  \"command\": \"read_measurements\",\n  \"parameters\": {\n    \"items\": [\"voltage\", \"current\", \"frequency\"],\n    \"phases\": [\"A\", \"B\", \"C\"]\n  }\n}");
        item("mqtt-send-payload")->setProperty("text", command);
        const auto before = controller->localClient()->receivedCount();
        click(item("mqtt-send-button"));
        require(waitFor([&] { return controller->localClient()->receivedCount() > before; }), "UI send button did not publish");
        const auto last = controller->localClient()->message(controller->localClient()->idAt(controller->localClient()->messageCount() - 1), "Text");
        require(last.value("payload").toString() == command, "UI text editor changed transmitted bytes");
        const QString telemetry = QStringLiteral("{\n  \"device_id\": \"loopback_test_001\",\n  \"measurements\": {\n    \"voltage\": [229.6, 230.1, 229.8],\n    \"current\": [3.21, 3.18, 3.24],\n    \"active_power\": 2209.5,\n    \"frequency\": 50.02\n  },\n  \"status\": {\n    \"online\": true,\n    \"alarm\": false\n  }\n}");
        controller->localClient()->publishMessage("terminal/001/telemetry", telemetry, "JSON", 1, false);
        require(waitFor([&] { return item("mqtt-received-payload")->property("text").toString().contains("\"measurements\""); }), "received JSON not rendered in UI");
        require(item("mqtt-received-topic")->property("color").value<QColor>() == QColor("#34d399"), "received topic uses subscription color");
        controller->localClient()->publishMessage("terminal/001/status", "status-initial", "Text", 1, false);
        require(waitFor([&] { return item("mqtt-received-payload")->property("text").toString().contains("status-initial"); }), "third topic visible by default");
        QMetaObject::invokeMethod(item("mqtt-subscriptions-list"), "positionViewAtEnd"); settle();
        click(item("mqtt-topic-visible-2"));
        require(waitFor([&] { return item("mqtt-received-payload")->property("text").toString().contains("\"measurements\""); }), "hidden selected topic must leave the detail view");
        const auto hiddenBefore = controller->remote()->receivedCount();
        controller->localClient()->publishMessage("terminal/001/status", "status-while-hidden", "Text", 1, false);
        require(waitFor([&] { return controller->remote()->receivedCount() > hiddenBefore; }), "unchecked topic still receives messages");
        require(item("mqtt-received-payload")->property("text").toString().contains("\"measurements\""), "hidden topic must not replace the displayed payload");
        QMetaObject::invokeMethod(item("mqtt-subscriptions-list"), "positionViewAtEnd"); settle();
        click(item("mqtt-topic-visible-2"));
        require(waitFor([&] { return item("mqtt-received-payload")->property("text").toString().contains("status-while-hidden"); }), "checking topic restores cached messages");
        controller->localClient()->publishMessage("terminal/001/telemetry", telemetry, "JSON", 1, false);
        require(waitFor([&] { return item("mqtt-received-payload")->property("text").toString().contains("\"measurements\""); }), "return to telemetry detail");
        const auto appearance = controller->remote()->subscriptions().at(1);
        QMetaObject::invokeMethod(page, "editSubscription", Q_ARG(QVariant, appearance)); settle();
        item("mqtt-subscription-color")->setProperty("text", "#a78bfa");
        click(item("mqtt-save-subscription"));
        require(item("mqtt-received-topic")->property("color").value<QColor>() == QColor("#a78bfa"), "editing color refreshes existing detail immediately");
        settle();
        const auto sidebarWidth = item("mqtt-subscription-sidebar")->width();
        require(item("mqtt-payload-view")->width() > (page->width() - sidebarWidth) * 0.6 && item("mqtt-payload-view")->width() > 400, "message list took too much payload space");
        const auto payloadWidth = item("mqtt-payload-view")->width();
        const auto sendWidth = item("mqtt-send-editor")->width();
        click(item("mqtt-toggle-subscriptions"));
        require(!item("mqtt-subscription-sidebar")->isVisible() && item("mqtt-payload-view")->width() > payloadWidth + 100, "collapsing topics restores receive width");
        require(qAbs(item("mqtt-send-editor")->width() - sendWidth) < 1, "topic sidebar must not shrink the send editor");
        click(item("mqtt-toggle-subscriptions"));
        require(item("mqtt-send-editor")->height() > 120, "send editor too small");
        require(item("mqtt-payload-view")->height() > 150, "receive editor too small");
        click(item("mqtt-expand-send"));
        require(!item("mqtt-receive-pane")->isVisible() && item("mqtt-send-pane")->height() > page->height() * 0.7, "expand send view");
        click(item("mqtt-expand-send"));
        click(item("mqtt-expand-receive"));
        require(!item("mqtt-send-pane")->isVisible(), "expand receive view");
        click(item("mqtt-expand-receive"));
        require(item("mqtt-send-pane")->isVisible() && item("mqtt-receive-pane")->isVisible(), "restore split layout");
        {
            auto *handle = item("mqtt-split-handle");
            const auto start = handle->mapToScene({handle->width() / 2, handle->height() / 2});
            const auto end = start + QPointF(0, 45);
            QMouseEvent down(QEvent::MouseButtonPress, start, window->mapToGlobal(start.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent move(QEvent::MouseMove, end, window->mapToGlobal(end.toPoint()), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent up(QEvent::MouseButtonRelease, end, window->mapToGlobal(end.toPoint()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            const auto ratio = controller->splitRatio();
            QCoreApplication::sendEvent(window, &down); QCoreApplication::sendEvent(window, &move); QCoreApplication::sendEvent(window, &up);
            settle();
            require(qAbs(controller->splitRatio() - ratio) > 0.02, "split drag was not applied/persisted");
            controller->setSplitRatio(0.56);
            QMetaObject::invokeMethod(page, "updateSplit"); settle();
        }
        const auto received = controller->remote()->receivedCount();
        click(item("mqtt-pause"));
        require(controller->remote()->paused(), "pause button");
        controller->localClient()->publishMessage("terminal/001/telemetry", telemetry, "JSON", 1, false);
        require(waitFor([&] { return controller->remote()->receivedCount() > received; }), "paused UI stopped receiving");
        click(item("mqtt-pause"));
        require(!controller->remote()->paused(), "resume button");
        require(controller->thirdPartyNotices().contains("Apache License"), "third-party notices missing from packaged executable");
        QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, 0)); settle();
        require(controller->remote()->connected() && controller->broker()->running(), "page navigation stopped MQTT sessions");
        QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, 15)); settle();
        if (brokerView) {
            QMetaObject::invokeMethod(page, "changeMode", Q_ARG(QVariant, 1));
            item("mqtt-send-topic")->setProperty("text", "terminal/001/command");
            item("mqtt-send-payload")->setProperty("text", command);
            settle();
        }
        if (!screenshot.isEmpty()) require(window->grabWindow().save(screenshot), "MQTT screenshot save failed");
        controller->remote()->disconnectFromBroker();
        require(waitFor([&] { return !controller->remote()->active(); }), "disconnect before editing connection");
        QMetaObject::invokeMethod(page, "changeMode", Q_ARG(QVariant, 0)); settle();
        click(item("mqtt-open-settings"));
        auto *connectionDialog = window->findChild<QObject *>("mqtt-connection-dialog");
        require(connectionDialog && connectionDialog->property("visible").toBool(), "connection editor did not open");
        auto *hostField = item("mqtt-config-host");
        hostField->setProperty("text", "localhost");
        QMetaObject::invokeMethod(hostField, "textEdited");
        auto *tabs = item("mqtt-config-tabs");
        tabs->setProperty("currentIndex", 1); settle();
        tabs->setProperty("currentIndex", 2); settle();
        tabs->setProperty("currentIndex", 0); settle();
        click(item("mqtt-save-connection"));
        require(controller->remote()->configuration().value("host").toString() == "localhost" && !connectionDialog->property("visible").toBool(), "connection editor did not save entered value");
        controller->localClient()->disconnectFromBroker();
        controller->broker()->stop();
        require(waitFor([&] { return !controller->remote()->active() && !controller->localClient()->active() && !controller->broker()->active(); }), "MQTT UI cleanup incomplete");
        QMetaObject::invokeMethod(page, "changeMode", Q_ARG(QVariant, 1)); settle();
        click(item("mqtt-open-settings"));
        auto *brokerDialog = window->findChild<QObject *>("mqtt-broker-dialog");
        auto *portField = item("mqtt-broker-port");
        portField->setProperty("text", "0"); QMetaObject::invokeMethod(portField, "textEdited");
        click(item("mqtt-save-broker"));
        require(brokerDialog->property("visible").toBool() && !brokerDialog->property("errorText").toString().isEmpty(), "broker editor silently accepted port zero");
        portField->setProperty("text", QString::number(port)); QMetaObject::invokeMethod(portField, "textEdited");
        click(item("mqtt-save-broker"));
        require(!brokerDialog->property("visible").toBool() && !controller->broker()->active(), "saving broker config must not start it");
        qInfo("MQTT_UI_CHECK_PASSED");
        return {};
    } catch (const QString &error) {
        if (controller) {
            controller->remote()->disconnectFromBroker(); controller->localClient()->disconnectFromBroker(); controller->broker()->stop();
        }
        return error;
    }
}
