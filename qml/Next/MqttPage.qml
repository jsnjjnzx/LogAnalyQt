import LogAnalyQt.Next as Next
import LogAnalyQt
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs

Item {
    id: root
    required property var theme
    property alias controller: mqtt
    property int mode: 0
    readonly property var session: mode === 0 ? mqtt.remote : mqtt.localClient
    property var draftOwner: null
    property bool ready: false
    property bool loadingDraft: false
    property bool followLatest: true
    property real selectedMessageId: 0
    property var selectedMessage: ({})
    property string focusedPane: ""
    property string actionMessage: ""
    property string panel: "subscriptions"
    property string editingTopic: ""
    property int editingQos: 0
    property bool editingEnabled: true
    property bool editingVisible: true
    property bool subscriptionsOpen: true
    readonly property var topicColors: ["#60a5fa", "#34d399", "#fbbf24", "#f472b6", "#a78bfa", "#22d3ee", "#fb923c", "#f87171", "#a3e635", "#2dd4bf", "#818cf8", "#fb7185"]
    property string pendingDeleteId: ""
    property var exportOwner: null

    MqttController { id: mqtt; objectName: "mqttController" }
    function showResult(result) {
        actionMessage = result && !result.ok ? result.message : ""
        return result && result.ok
    }
    function saveDraft() {
        draftSave.stop()
        if (!ready || loadingDraft || !draftOwner) return true
        return showResult(draftOwner.updateDraft({topic: sendTopic.text, payload: sendPayload.text,
            format: sendFormat.currentText, qos: sendQos.currentIndex, retain: retain.checked}))
    }
    function loadDraft() {
        loadingDraft = true
        draftOwner = session
        const draft = session.draft
        sendTopic.text = draft.topic || ""
        sendPayload.text = draft.payload || ""
        sendFormat.currentIndex = Math.max(0, ["Text", "JSON", "Hex"].indexOf(draft.format || "Text"))
        sendQos.currentIndex = Number(draft.qos || 0)
        retain.checked = !!draft.retain
        selectedMessageId = session.idAt(session.messageCount - 1)
        followLatest = true
        loadingDraft = false
        refreshDetail()
    }
    function refreshDetail() {
        let message = session.message(selectedMessageId, receiveFormat.currentText)
        if (!message.id || !message.visible) {
            selectedMessageId = session.idAt(session.messageCount - 1)
            message = session.message(selectedMessageId, receiveFormat.currentText)
        }
        selectedMessage = message
    }
    function applyFilter() { showResult(session.setFilter(["all", "received", "sent"][direction.currentIndex], topicFilter.text, contentFilter.text)) }
    function changeMode(next) {
        if (next === mode) return
        saveDraft()
        mode = next
        topicFilter.text = ""
        contentFilter.text = ""
        direction.currentIndex = 0
        applyFilter()
        loadDraft()
    }
    function changeConnection(id) {
        saveDraft()
        if (showResult(mqtt.selectConnection(id))) loadDraft()
    }
    function openPanel(which) { saveDraft(); panel = which; sidePanel.open() }
    function editSubscription(subscription) {
        actionMessage = ""
        editingTopic = subscription ? subscription.topic : ""
        editingQos = subscription ? subscription.qos : 0
        editingEnabled = subscription ? subscription.enabled : true
        editingVisible = subscription ? subscription.visible : true
        subscriptionTopic.text = editingTopic
        subscriptionQos.currentIndex = editingQos
        subscriptionColor.text = subscription ? subscription.color : topicColors[session.subscriptions.length % topicColors.length]
        openPanel("subscriptions")
    }
    function send() {
        saveDraft()
        if (showResult(session.publishMessage(sendTopic.text, sendPayload.text, sendFormat.currentText, sendQos.currentIndex, retain.checked))) {
            followLatest = true
            updateMessages()
        }
    }
    function updateMessages() {
        if (followLatest && !session.paused && session.messageCount > 0)
            selectedMessageId = session.idAt(session.messageCount - 1)
        refreshDetail()
        if (autoScroll.checked && !session.paused) messageList.positionViewAtEnd()
    }
    function updateSplit() {
        if (!workspace.resizing && focusedPane === "") receivePane.SplitView.preferredHeight = (workspace.height - 9) * mqtt.splitRatio
    }
    onHeightChanged: if (ready) Qt.callLater(updateSplit)
    onFocusedPaneChanged: if (ready) Qt.callLater(updateSplit)
    Component.onCompleted: { ready = true; loadDraft(); Qt.callLater(updateSplit) }
    Component.onDestruction: saveDraft()
    Timer { id: draftSave; interval: 350; onTriggered: root.saveDraft() }
    Timer { id: filterTimer; interval: 250; onTriggered: root.applyFilter() }
    Connections { target: root.session; function onMessagesChanged() { Qt.callLater(root.updateMessages) } }
    Shortcut { sequences: ["Ctrl+Return", "Meta+Return"]; enabled: root.visible && root.session.connected && !sidePanel.visible && !connectionDialog.visible && !brokerDialog.visible && !exportDialog.visible && !licensesDialog.visible && !deleteDialog.visible; onActivated: root.send() }

    ColumnLayout {
        anchors.fill: parent; spacing: 8
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            RowLayout {
                spacing: 3
                Repeater {
                    model: ["连接终端", "本机 Broker"]
                    delegate: Next.GlassButton {
                        required property string modelData
                        required property int index
                        objectName: "mqtt-mode-" + index
                        text: modelData
                        implicitWidth: 100; implicitHeight: 34; font.pixelSize: 12
                        onClicked: root.changeMode(index)
                        background: Rectangle { color: root.mode === index ? root.theme.navigationSelected : "transparent"; border.color: root.theme.groupBorder; radius: 7 }
                    }
                }
            }
            Next.GlassButton {
                objectName: "mqtt-profile-picker"
                visible: root.mode === 0
                Layout.maximumWidth: 210
                text: (mqtt.remote.connected ? "● " : "○ ") + mqtt.remote.configuration.name + " ▾"
                onClicked: root.openPanel("connections")
            }
            Label { textFormat: Text.PlainText;
                visible: root.mode === 1
                text: mqtt.broker.running ? "● 服务运行中" : mqtt.broker.active ? "◌ " + mqtt.broker.detail : "○ 服务未启动"
                color: mqtt.broker.running ? root.theme.success : root.theme.subText
                font.pixelSize: 12
            }
            Label { textFormat: Text.PlainText;
                Layout.fillWidth: true; Layout.minimumWidth: 0
                text: root.mode === 0 ? mqtt.remote.configuration.host + ":" + mqtt.remote.configuration.port : mqtt.broker.connectionAddress
                font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 11
                color: root.theme.subText; elide: Text.ElideMiddle
                ToolTip.visible: addressHover.hovered; ToolTip.text: text
                HoverHandler { id: addressHover }
            }
            Next.GlassButton {
                objectName: "mqtt-open-settings"
                text: root.mode === 0 ? "连接配置" : "服务配置"
                onClicked: { root.saveDraft(); if (root.mode === 0) connectionDialog.edit(mqtt.remote.configuration); else brokerDialog.edit() }
            }
            Next.GlassButton {
                objectName: "mqtt-connection-action"
                visible: root.mode === 0
                text: mqtt.remote.active ? "断开" : "连接"
                highlighted: !mqtt.remote.active
                enabled: mqtt.remote.state !== "disconnecting"
                onClicked: { if (mqtt.remote.active) mqtt.remote.disconnectFromBroker(); else root.showResult(mqtt.remote.connectToBroker()) }
            }
            Next.GlassButton {
                visible: root.mode === 1
                text: "接入终端 " + mqtt.broker.clients.filter(function(c) { return !c.local }).length
                onClicked: root.openPanel("clients")
            }
            Next.GlassButton {
                objectName: "mqtt-broker-action"
                visible: root.mode === 1
                text: mqtt.broker.active ? "停止服务" : "启动服务"
                highlighted: !mqtt.broker.active
                enabled: mqtt.broker.state !== "stopping"
                onClicked: {
                    if (mqtt.broker.active) { mqtt.localClient.disconnectFromBroker(); mqtt.broker.stop() }
                    else root.showResult(mqtt.broker.start())
                }
            }
            Next.GlassButton { objectName: "mqtt-toggle-subscriptions"; text: (root.subscriptionsOpen ? "收起订阅 " : "显示订阅 ") + root.session.subscriptions.length; onClicked: root.subscriptionsOpen = !root.subscriptionsOpen }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.mode === 1
            Label { textFormat: Text.PlainText; text: "本机测试客户端：" + mqtt.localClient.detail; color: mqtt.localClient.connected ? root.theme.success : root.theme.subText; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
            Next.GlassButton {
                text: mqtt.localClient.active ? "断开测试客户端" : "连接测试客户端"
                implicitHeight: 28; font.pixelSize: 11
                enabled: mqtt.broker.running && mqtt.localClient.state !== "disconnecting"
                onClicked: { if (mqtt.localClient.active) mqtt.localClient.disconnectFromBroker(); else root.showResult(mqtt.connectLocalClient()) }
            }
        }
        Label { textFormat: Text.PlainText;
            Layout.fillWidth: true
            visible: text.length > 0
            text: mqtt.storageError || root.actionMessage || (root.session.state === "error" || root.session.state === "reconnecting" ? root.session.detail : "") || (root.mode === 1 && mqtt.broker.state === "error" ? mqtt.broker.detail : "")
            color: root.theme.warning; font.pixelSize: 12
            wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
        }
        SplitView {
            id: workspace
            objectName: "mqtt-workspace"
            Layout.fillWidth: true; Layout.fillHeight: true
            orientation: Qt.Vertical
            onResizingChanged: if (!resizing && root.focusedPane === "" && height > 0) mqtt.splitRatio = receivePane.height / Math.max(1, height - 9)
            handle: Rectangle {
                id: splitHandle
                objectName: "mqtt-split-handle"
                activeFocusOnTab: true
                Accessible.role: Accessible.Separator
                Accessible.name: "调整接收和发送区域高度"
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                        mqtt.splitRatio += event.key === Qt.Key_Up ? -0.03 : 0.03
                        root.updateSplit()
                        event.accepted = true
                    }
                }
                implicitHeight: 9
                color: "transparent"
                readonly property bool hovered: SplitHandle.hovered
                readonly property bool pressed: SplitHandle.pressed
                Rectangle { anchors.centerIn: parent; width: 42; height: 3; radius: 2; color: splitHandle.hovered || splitHandle.pressed ? root.theme.accent : root.theme.splitHandleHover }
            }
            Rectangle {
                id: receivePane
                objectName: "mqtt-receive-pane"
                visible: root.focusedPane !== "send"
                SplitView.minimumHeight: 180
                SplitView.preferredHeight: (workspace.height - 9) * mqtt.splitRatio
                SplitView.fillHeight: root.focusedPane === "receive"
                color: root.theme.surface; border.color: root.theme.groupBorder; radius: 9
                clip: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 1; spacing: 0
                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredHeight: 43
                        Layout.leftMargin: 12; Layout.rightMargin: 10
                        Label { textFormat: Text.PlainText; text: "接收 / 消息记录"; font.bold: true; color: root.theme.text }
                        Label { textFormat: Text.PlainText; text: "↓ " + root.session.receivedCount + "　↑ " + root.session.sentCount; color: root.theme.subText; font.pixelSize: 11 }
                        Item { Layout.fillWidth: true }
                        Next.GlassCheckBox { id: autoScroll; text: "自动滚动"; checked: true; font.pixelSize: 11 }
                        Next.GlassButton { objectName: "mqtt-pause"; text: root.session.paused ? "继续（" + root.session.pendingCount + "）" : "暂停显示"; implicitHeight: 28; font.pixelSize: 11; onClicked: root.session.paused = !root.session.paused }
                        Next.GlassButton { text: "清空"; implicitHeight: 28; font.pixelSize: 11; onClicked: { root.session.clearMessages(); root.selectedMessageId = 0; root.refreshDetail() } }
                        Next.GlassButton { text: root.session.exportBusy ? "导出中…" : "导出"; implicitHeight: 28; font.pixelSize: 11; enabled: !root.session.exportBusy; onClicked: { root.exportOwner = root.session; exportDialog.open() } }
                        Next.GlassButton { objectName: "mqtt-expand-receive"; text: root.focusedPane === "receive" ? "恢复布局" : "展开接收区"; implicitHeight: 28; font.pixelSize: 11; onClicked: root.focusedPane = root.focusedPane === "receive" ? "" : "receive" }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.groupBorder }
                    RowLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                        ColumnLayout {
                            objectName: "mqtt-subscription-sidebar"
                            visible: root.subscriptionsOpen
                            Layout.preferredWidth: 184; Layout.minimumWidth: 184; Layout.maximumWidth: 184
                            Layout.fillHeight: true; spacing: 0
                            RowLayout {
                                Layout.fillWidth: true; Layout.margins: 8
                                Label { text: "订阅主题"; color: root.theme.text; font.bold: true; Layout.fillWidth: true }
                                Next.GlassButton { objectName: "mqtt-add-subscription"; text: "＋ 新增"; implicitHeight: 28; implicitWidth: 65; font.pixelSize: 11; onClicked: root.editSubscription(null) }
                            }
                            RowLayout {
                                Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8
                                Next.GlassButton { objectName: "mqtt-show-all-topics"; text: "全选"; implicitHeight: 26; implicitWidth: 54; font.pixelSize: 11; onClicked: root.session.setAllSubscriptionsVisible(true) }
                                Next.GlassButton { objectName: "mqtt-hide-all-topics"; text: "全不选"; implicitHeight: 26; implicitWidth: 66; font.pixelSize: 11; onClicked: root.session.setAllSubscriptionsVisible(false) }
                                Item { Layout.fillWidth: true }
                            }
                            Label { Layout.fillWidth: true; Layout.margins: 8; text: "勾选控制接收显示"; font.pixelSize: 10; color: root.theme.subText }
                            ListView {
                                id: subscriptionsList
                                objectName: "mqtt-subscriptions-list"
                                Layout.fillWidth: true; Layout.fillHeight: true
                                model: root.session.subscriptions; clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar {}
                                delegate: Rectangle {
                                    id: subscriptionRow
                                    required property var modelData
                                    required property int index
                                    width: subscriptionsList.width; height: 64; color: "transparent"
                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: 6; spacing: 5
                                        Next.GlassCheckBox {
                                            objectName: "mqtt-topic-visible-" + subscriptionRow.index
                                            checked: subscriptionRow.modelData.visible
                                            Accessible.name: "显示接收：" + subscriptionRow.modelData.topic
                                            onClicked: root.showResult(root.session.setSubscriptionAppearance(subscriptionRow.modelData.topic, subscriptionRow.modelData.color, checked))
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: 4
                                            Label { textFormat: Text.PlainText; text: subscriptionRow.modelData.topic; color: subscriptionRow.modelData.color; Layout.fillWidth: true; font.pixelSize: 11; font.family: Next.PlatformFonts.monospaceFamily; wrapMode: Text.WrapAnywhere; maximumLineCount: 2; elide: Text.ElideRight }
                                            Label { textFormat: Text.PlainText; text: subscriptionRow.modelData.state.startsWith("已订阅") ? subscriptionRow.modelData.state : "QoS " + subscriptionRow.modelData.qos + " · " + subscriptionRow.modelData.state; Layout.fillWidth: true; elide: Text.ElideRight; color: root.theme.subText; font.pixelSize: 9 }
                                        }
                                        Next.GlassButton {
                                            text: "⋯"; implicitWidth: 26; implicitHeight: 28; padding: 0
                                            Accessible.name: "配置订阅：" + subscriptionRow.modelData.topic
                                            ToolTip.visible: hovered
                                            ToolTip.text: "编辑颜色、停用或删除"
                                            onClicked: subscriptionMenu.popup()
                                        }
                                    }
                                    Next.GlassMenu {
                                        id: subscriptionMenu
                                        MenuItem { text: "编辑主题 / 颜色"; onTriggered: root.editSubscription(subscriptionRow.modelData) }
                                        MenuItem { text: subscriptionRow.modelData.enabled ? "停用订阅" : "启用订阅"; onTriggered: root.showResult(root.session.saveSubscription(subscriptionRow.modelData.topic, subscriptionRow.modelData.qos, !subscriptionRow.modelData.enabled)) }
                                        MenuItem { text: "取消订阅并删除"; onTriggered: root.showResult(root.session.removeSubscription(subscriptionRow.modelData.topic)) }
                                    }
                                    Rectangle { width: parent.width; height: 1; anchors.bottom: parent.bottom; color: root.theme.separator }
                                }
                                Label { anchors.centerIn: parent; visible: subscriptionsList.count === 0; text: "点击新增订阅主题"; color: root.theme.subText; font.pixelSize: 11 }
                            }
                        }
                        Rectangle { visible: root.subscriptionsOpen; width: 1; Layout.fillHeight: true; color: root.theme.groupBorder }
                        ColumnLayout {
                            Layout.preferredWidth: root.subscriptionsOpen ? 220 : (root.width < 1150 ? 250 : 285)
                            Layout.minimumWidth: Layout.preferredWidth
                            Layout.maximumWidth: Layout.preferredWidth
                            Layout.fillHeight: true; spacing: 0
                            ColumnLayout {
                                Layout.fillWidth: true; Layout.margins: 9; spacing: 6
                                Next.GlassTextField { id: topicFilter; Layout.fillWidth: true; implicitHeight: 30; placeholderText: "主题过滤，例如 terminal/#"; font.pixelSize: 11; onTextEdited: filterTimer.restart() }
                                Next.GlassTextField { id: contentFilter; Layout.fillWidth: true; implicitHeight: 30; placeholderText: "搜索内容 / Hex"; font.pixelSize: 11; onTextEdited: filterTimer.restart() }
                                Next.GlassComboBox { id: direction; Layout.fillWidth: true; implicitHeight: 28; model: ["全部消息", "仅接收", "仅发送"]; font.pixelSize: 11; onActivated: root.applyFilter() }
                            }
                            ListView {
                                id: messageList
                                objectName: "mqtt-message-list"
                                Layout.fillWidth: true; Layout.fillHeight: true
                                model: root.session.messages
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar {}
                                delegate: Next.GlassButton {
                                    id: row
                                    required property var messageId
                                    required property string messageTime
                                    required property string topic
                                    required property string topicColor
                                    required property string preview
                                    required property int qos
                                    required property bool received
                                    required property string delivery
                                    width: messageList.width; height: 82; padding: 10
                                    Accessible.name: (received ? "接收 " : "发送 ") + topic + " " + messageTime
                                    onClicked: { root.selectedMessageId = messageId; root.followLatest = false; root.refreshDetail() }
                                    background: Rectangle {
                                        color: root.selectedMessageId === row.messageId ? root.theme.tableHighlight : row.hovered ? root.theme.surfaceHover : "transparent"
                                        Rectangle { width: 3; height: parent.height; color: row.topicColor || root.theme.accent; visible: root.selectedMessageId === row.messageId }
                                        Rectangle { width: parent.width; height: 1; anchors.bottom: parent.bottom; color: root.theme.separator }
                                    }
                                    contentItem: ColumnLayout {
                                        spacing: 5
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Label { textFormat: Text.PlainText; text: row.received ? "↓ 接收" : "↑ 发送"; color: row.received ? root.theme.success : root.theme.accent; font.pixelSize: 11 }
                                            Item { Layout.fillWidth: true }
                                            Label { textFormat: Text.PlainText; text: row.messageTime; color: root.theme.subText; font.pixelSize: 10; font.family: Next.PlatformFonts.monospaceFamily }
                                        }
                                        Label { textFormat: Text.PlainText; Layout.fillWidth: true; text: row.topic; elide: Text.ElideMiddle; font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 12; color: row.topicColor || root.theme.text }
                                        Label { textFormat: Text.PlainText; Layout.fillWidth: true; text: row.received ? row.preview : row.delivery; elide: Text.ElideRight; color: root.theme.subText; font.pixelSize: 11 }
                                    }
                                }
                                Label { textFormat: Text.PlainText; anchors.centerIn: parent; visible: messageList.count === 0; text: "暂无匹配消息"; color: root.theme.subText; font.pixelSize: 12 }
                            }
                        }
                        Rectangle { width: 1; Layout.fillHeight: true; color: root.theme.groupBorder }
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                            RowLayout {
                                Layout.fillWidth: true; Layout.preferredHeight: 40
                                Layout.leftMargin: 12; Layout.rightMargin: 10
                                Label { textFormat: Text.PlainText; text: root.selectedMessage.received === false ? "↑" : "↓"; color: root.selectedMessage.received === false ? root.theme.accent : root.theme.success }
                                Label { objectName: "mqtt-received-topic"; textFormat: Text.PlainText; Layout.fillWidth: true; text: root.selectedMessage.topic || "完整消息内容"; color: root.selectedMessage.topicColor || root.theme.text; elide: Text.ElideMiddle; font.pixelSize: 12; font.family: Next.PlatformFonts.monospaceFamily }
                                Next.GlassComboBox { id: receiveFormat; implicitWidth: 90; implicitHeight: 28; model: ["Text", "JSON", "Hex"]; currentIndex: 1; font.pixelSize: 11; onActivated: root.refreshDetail() }
                                Next.GlassButton { text: "复制"; implicitHeight: 28; implicitWidth: 56; font.pixelSize: 11; enabled: !!root.selectedMessage.id; onClicked: root.session.copyMessage(root.selectedMessageId, receiveFormat.currentText) }
                            }
                            ScrollView {
                                id: payloadScroll
                                objectName: "mqtt-payload-view"
                                Layout.fillWidth: true; Layout.fillHeight: true
                                clip: true
                                TextArea {
                                    objectName: "mqtt-received-payload"
                                    Accessible.name: "接收消息内容"
                                    text: root.selectedMessage.payload || ""
                                    readOnly: true; selectByMouse: true; textFormat: TextEdit.PlainText
                                    color: root.theme.text; padding: 14
                                    font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 13
                                    wrapMode: TextEdit.WrapAnywhere
                                    placeholderText: "连接 Broker 并订阅主题后，在左侧选择消息。\n\n这里展示完整的多行内容，接收区可拖动调整或单独展开。"
                                    placeholderTextColor: root.theme.placeholderText
                                    background: Rectangle { color: root.theme.controlBackground }
                                }
                            }
                            Label { textFormat: Text.PlainText;
                                Layout.fillWidth: true; Layout.margins: 8
                                text: root.selectedMessage.id ? "QoS " + root.selectedMessage.qos + "　·　Retain " + (root.selectedMessage.retained ? "是" : "否") + "　·　" + root.selectedMessage.byteCount + " 字节　·　" + root.selectedMessage.delivery + (root.selectedMessage.truncated ? "　·　预览前 64 KiB，复制 / 导出保留完整载荷" : "") : "显示格式不会改变原始载荷"
                                elide: Text.ElideRight; color: root.selectedMessage.truncated ? root.theme.warning : root.theme.subText; font.pixelSize: 10
                            }
                        }
                    }
                }
            }
            Rectangle {
                id: sendPane
                objectName: "mqtt-send-pane"
                visible: root.focusedPane !== "receive"
                SplitView.minimumHeight: 180
                SplitView.fillHeight: true
                color: root.theme.surface; border.color: root.theme.groupBorder; radius: 9; clip: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 1; spacing: 0
                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredHeight: 45
                        Layout.leftMargin: 12; Layout.rightMargin: 10; spacing: 8
                        Label { textFormat: Text.PlainText; text: "发送消息"; font.bold: true; color: root.theme.text }
                        Label { textFormat: Text.PlainText; text: "Topic"; font.pixelSize: 11; color: root.theme.subText }
                        Next.GlassTextField { id: sendTopic; objectName: "mqtt-send-topic"; Layout.fillWidth: true; implicitHeight: 30; placeholderText: "例如 terminal/001/command"; font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 12; onTextEdited: draftSave.restart() }
                        Next.GlassComboBox { id: sendQos; model: ["QoS 0", "QoS 1", "QoS 2"]; implicitWidth: 85; implicitHeight: 30; font.pixelSize: 11; onActivated: draftSave.restart() }
                        Next.GlassCheckBox { id: retain; text: "Retain"; font.pixelSize: 11; onToggled: draftSave.restart() }
                        Next.GlassButton { objectName: "mqtt-expand-send"; text: root.focusedPane === "send" ? "恢复布局" : "展开发送区"; implicitHeight: 28; font.pixelSize: 11; onClicked: root.focusedPane = root.focusedPane === "send" ? "" : "send" }
                    }
                    ScrollView {
                        objectName: "mqtt-send-editor"
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        TextArea {
                            id: sendPayload
                            objectName: "mqtt-send-payload"
                            Accessible.name: "发送内容"
                            selectByMouse: true; textFormat: TextEdit.PlainText
                            wrapMode: TextEdit.WrapAnywhere
                            color: root.theme.text; padding: 14
                            font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 13
                            placeholderText: "输入要发送的内容；Hex 模式输入实际字节，例如 68 01 FF。"
                            placeholderTextColor: root.theme.placeholderText
                            onTextChanged: if (root.ready && !root.loadingDraft) draftSave.restart()
                            background: Rectangle { color: root.theme.controlBackground; border.color: sendPayload.activeFocus ? root.theme.accent : "transparent" }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredHeight: 42; Layout.leftMargin: 10; Layout.rightMargin: 10
                        Next.GlassComboBox { id: sendFormat; model: ["Text", "JSON", "Hex"]; implicitWidth: 90; implicitHeight: 28; font.pixelSize: 11; onActivated: draftSave.restart() }
                        Next.GlassButton { text: "发送历史"; implicitHeight: 28; font.pixelSize: 11; onClicked: root.openPanel("history") }
                        Item { Layout.fillWidth: true }
                        Label { textFormat: Text.PlainText; text: root.session.connected ? "发布到当前连接 · ⌘ / Ctrl + Enter" : "连接后可发送"; color: root.theme.subText; font.pixelSize: 11 }
                        Next.GlassButton { objectName: "mqtt-send-button"; text: "↑ 发送"; highlighted: true; enabled: root.session.connected; implicitHeight: 30; implicitWidth: 90; onClicked: root.send() }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { textFormat: Text.PlainText; Layout.fillWidth: true; text: root.session.notice || root.session.detail; color: root.theme.subText; font.pixelSize: 10; elide: Text.ElideRight }
            Label { textFormat: Text.PlainText; text: "显示 " + root.session.messageCount + " 条 · 淘汰 " + root.session.evictedCount + " · 超限丢弃 " + root.session.droppedCount; color: root.session.droppedCount ? root.theme.warning : root.theme.subText; font.pixelSize: 10 }
        }
    }

    Next.MqttConnectionDialog { id: connectionDialog; theme: root.theme; controller: mqtt; onSaved: root.loadDraft() }
    Next.MqttBrokerDialog { id: brokerDialog; theme: root.theme; controller: mqtt }
    Dialogs.FileDialog {
        id: exportDialog
        title: "导出当前过滤后的消息记录"
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: ["MQTT 消息记录 (*.jsonl)"]
        defaultSuffix: "jsonl"
        onAccepted: if (root.exportOwner) root.showResult(root.exportOwner.exportMessages(selectedFile, true))
    }
    Next.GlassDialog {
        id: sidePanel
        focus: true
        parent: Overlay.overlay
        modal: true; width: 480; height: root.panel === "subscriptions" ? Math.min(400, parent ? parent.height - 24 : 400) : (parent ? parent.height - 24 : 740)
        x: parent ? parent.width - width - 12 : 0; y: root.panel === "subscriptions" && parent ? (parent.height - height) / 2 : 12
        title: ({connections:"连接管理", subscriptions:"主题订阅", clients:"接入终端与服务事件", history:"发送历史"})[root.panel] || "MQTT"
        palette.window: root.theme.popupBackground; palette.windowText: root.theme.text; palette.mid: root.theme.groupBorder
        contentItem: ColumnLayout {
            spacing: 12
            ScrollView {
                id: panelScroll
                Layout.fillWidth: true; Layout.fillHeight: true
                contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: panelScroll.availableWidth; spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true; visible: root.panel === "connections"
                        Next.GlassButton { text: "＋ 新建连接"; enabled: !mqtt.remote.active; onClicked: { sidePanel.close(); connectionDialog.edit(mqtt.newConnection()) } }
                        Repeater {
                            model: mqtt.profiles
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true; spacing: 6
                                Label { textFormat: Text.PlainText; text: (modelData.id === mqtt.currentProfileId ? "● " : "○ ") + modelData.name; color: modelData.id === mqtt.currentProfileId ? root.theme.accent : root.theme.text }
                                Label { textFormat: Text.PlainText; text: modelData.host + ":" + modelData.port; color: root.theme.subText; font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 11 }
                                RowLayout {
                                    enabled: !mqtt.remote.active
                                    Next.GlassButton { text: "选择"; onClicked: { root.changeConnection(modelData.id); sidePanel.close() } }
                                    Next.GlassButton { text: "复制"; onClicked: { if (root.showResult(mqtt.copyConnection(modelData.id))) { root.loadDraft(); sidePanel.close() } } }
                                    Next.GlassButton { text: "删除"; enabled: mqtt.profiles.length > 1; onClicked: { root.pendingDeleteId = modelData.id; deleteDialog.open() } }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.groupBorder }
                            }
                        }
                        Label { textFormat: Text.PlainText; Layout.fillWidth: true; text: mqtt.remote.active ? "断开当前连接后，可以选择或编辑其他配置。" : "当前显示的消息不写入配置文件；需要保留请先导出。"; wrapMode: Text.Wrap; font.pixelSize: 12; color: root.theme.subText }
                        Label { textFormat: Text.PlainText; Layout.fillWidth: true; text: "配置文件：\n" + mqtt.configPath; wrapMode: Text.WrapAnywhere; font.pixelSize: 11; color: root.theme.subText }
                        Next.GlassButton { text: "第三方许可"; onClicked: licensesDialog.open() }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; visible: root.panel === "subscriptions"
                        Label { textFormat: Text.PlainText; Layout.fillWidth: true; text: "订阅决定接收哪些主题；主界面过滤只改变显示。"; wrapMode: Text.Wrap; color: root.theme.subText; font.pixelSize: 12 }
                        Next.GlassTextField { id: subscriptionTopic; objectName: "mqtt-subscription-topic"; Layout.fillWidth: true; placeholderText: "主题，例如 terminal/+/status 或 #" }
                        Label { text: "主题颜色"; color: root.theme.text }
                        RowLayout {
                            Repeater {
                                model: root.topicColors
                                delegate: Next.GlassButton {
                                    required property string modelData
                                    implicitWidth: 30; implicitHeight: 28
                                    Accessible.name: "选择颜色 " + modelData
                                    background: Rectangle { color: modelData; radius: 5; border.width: subscriptionColor.text.toLowerCase() === modelData ? 2 : 0; border.color: root.theme.text }
                                    onClicked: subscriptionColor.text = modelData
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }
                        Next.GlassTextField { id: subscriptionColor; objectName: "mqtt-subscription-color"; Layout.fillWidth: true; placeholderText: "自定义颜色，例如 #60a5fa"; maximumLength: 7 }
                        RowLayout {
                            Next.GlassComboBox { id: subscriptionQos; objectName: "mqtt-subscription-qos"; model: ["QoS 0", "QoS 1", "QoS 2"] }
                            Next.GlassButton {
                                objectName: "mqtt-save-subscription"
                                text: root.editingTopic ? "保存修改" : "新增订阅"
                                onClicked: {
                                    const result = root.editingTopic && root.editingTopic === subscriptionTopic.text && root.editingQos === subscriptionQos.currentIndex
                                        ? root.session.setSubscriptionAppearance(root.editingTopic, subscriptionColor.text, root.editingVisible)
                                        : root.session.saveSubscription(subscriptionTopic.text, subscriptionQos.currentIndex, root.editingEnabled, root.editingTopic, subscriptionColor.text)
                                    if (root.showResult(result)) {
                                        root.editingTopic = ""; sidePanel.close()
                                    }
                                }
                            }
                            Next.GlassButton { text: "取消"; onClicked: sidePanel.close() }
                        }
                        Label { textFormat: Text.PlainText; text: root.actionMessage; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.theme.warning }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; visible: root.panel === "clients"
                        Label { textFormat: Text.PlainText; text: "终端接入地址：" + mqtt.broker.connectionAddress; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: root.theme.accent }
                        Repeater {
                            model: mqtt.broker.clients
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Label { textFormat: Text.PlainText; text: "● " + modelData.clientId + (modelData.local ? "（本机测试客户端）" : ""); color: root.theme.success; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                                Label { textFormat: Text.PlainText; text: modelData.address + ":" + modelData.port + " · " + modelData.connectedAt + " 接入"; color: root.theme.subText; font.pixelSize: 11 }
                            }
                        }
                        Label { textFormat: Text.PlainText; text: "尚无接入客户端"; visible: mqtt.broker.clients.length === 0; color: root.theme.subText }
                        Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.groupBorder }
                        Label { textFormat: Text.PlainText; text: "服务事件"; color: root.theme.text; font.bold: true }
                        TextArea { Layout.fillWidth: true; text: mqtt.broker.events.join("\n"); readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; font.pixelSize: 11; font.family: Next.PlatformFonts.monospaceFamily; color: root.theme.subText; background: null }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; visible: root.panel === "history"
                        Label { textFormat: Text.PlainText; Layout.fillWidth: true; text: "选择一条记录回填输入框，不会自动发送。较大的消息仅保留在草稿和消息记录中。"; wrapMode: Text.Wrap; color: root.theme.subText; font.pixelSize: 12 }
                        Repeater {
                            model: root.session.history
                            delegate: Next.GlassButton {
                                required property var modelData
                                Layout.fillWidth: true
                                text: modelData.topic + " · " + modelData.format + " · QoS " + modelData.qos
                                onClicked: { root.session.updateDraft(modelData); root.loadDraft(); sidePanel.close() }
                            }
                        }
                        Label { textFormat: Text.PlainText; text: "暂无发送历史"; visible: root.session.history.length === 0; color: root.theme.subText }
                    }
                }
            }
            Label { textFormat: Text.PlainText; Layout.fillWidth: true; visible: root.actionMessage.length > 0; text: root.actionMessage; color: root.theme.warning; wrapMode: Text.Wrap }
            Next.GlassButton { Layout.alignment: Qt.AlignRight; text: "返回数据区"; onClicked: sidePanel.close() }
        }
    }
    Next.GlassDialog {
        id: licensesDialog
        parent: Overlay.overlay; anchors.centerIn: parent; modal: true
        title: "MQTT 第三方许可"
        width: Math.min(780, parent ? parent.width - 40 : 780)
        height: Math.min(700, parent ? parent.height - 40 : 700)
        standardButtons: Dialog.Close
        contentItem: ScrollView {
            clip: true
            TextArea { text: mqtt.thirdPartyNotices(); readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: root.theme.text; font.pixelSize: 12 }
        }
    }
    Next.GlassDialog {
        id: deleteDialog
        parent: Overlay.overlay; anchors.centerIn: parent; modal: true
        title: "删除连接配置？"
        standardButtons: Dialog.Yes | Dialog.No
        Label { textFormat: Text.PlainText; text: "该连接的订阅、草稿和发送历史也会删除。"; color: root.theme.text }
        onAccepted: if (root.showResult(mqtt.removeConnection(root.pendingDeleteId))) root.loadDraft()
    }
}
