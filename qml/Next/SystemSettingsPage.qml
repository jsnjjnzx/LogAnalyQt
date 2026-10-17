import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    property var backend: null
    property var controller: backend && backend.aiFraming ? backend.aiFraming : null
    property int section: 0
    property string notice: ""
    property bool noticeError: false
    property string ownedConnectionId: ""
    property string ownedSettingsOperationId: ""
    property string settingsOperationKind: ""
    property bool disposed: false
    readonly property bool settingsPending: ownedSettingsOperationId.length > 0
    function beginSettingsOperation(kind) {
        if (!controller || locked) return
        const reply = kind === "save"
            ? controller.saveConfiguration(endpoint.text.trim(), modelName.text.trim(), apiKey.text)
            : controller.clearApiKey()
        if (reply && reply.ok && reply.pending === true && typeof reply.operationId === "string" && reply.operationId) {
            ownedSettingsOperationId = reply.operationId
            settingsOperationKind = kind
            noticeError = false
            notice = reply.message || "正在等待系统安全凭据操作完成…"
        } else {
            noticeError = true
            notice = reply && reply.message ? reply.message : "未能受理设置操作，请重试。"
        }
    }
    function finishSettingsOperation(operationId, reply) {
        if (disposed || !ownedSettingsOperationId || operationId !== ownedSettingsOperationId || (reply && reply.pending === true)) return
        const completedKind = settingsOperationKind
        ownedSettingsOperationId = ""
        settingsOperationKind = ""
        noticeError = !reply || reply.ok !== true
        notice = reply && reply.message ? reply.message : noticeError ? "设置操作失败。" : "设置操作完成。"
        if (!noticeError) {
            apiKey.text = ""
            if (completedKind === "save") loadConfiguration()
        }
    }
    function startConnectionTest() {
        notice = ""
        if (controller) ownedConnectionId = controller.testConnection(endpoint.text.trim(), modelName.text.trim(), apiKey.text)
    }
    function cancelOwnedConnection() {
        if (controller && ownedConnectionId && ownedConnectionId === controller.connectionRequestId && controller.connectionBusy)
            controller.cancelConnectionTest()
        ownedConnectionId = ""
    }
    Component.onDestruction: {
        disposed = true
        ownedSettingsOperationId = ""
        cancelOwnedConnection()
    }
    readonly property bool locked: settingsPending || !!(controller && (controller.settingsBusy || controller.busy || controller.connectionBusy || controller.state === "clarification"))
    function showModelSettings() { section = 1 }
    function loadConfiguration() {
        endpoint.text = controller ? controller.baseUrl : ""
        modelName.text = controller ? controller.model : ""
    }
    function persistAppearance(key, value) {
        if (backend && typeof backend.setUiSetting === "function") backend.setUiSetting(key, value)
    }
    Component.onCompleted: loadConfiguration()
    onControllerChanged: {
        ownedSettingsOperationId = ""; settingsOperationKind = ""
        if (apiKey) apiKey.text = ""
        loadConfiguration()
    }
    Connections {
        target: root.controller
        enabled: !root.disposed
        ignoreUnknownSignals: true
        function onSettingsOperationFinished(operationId, reply) { root.finishSettingsOperation(operationId, reply) }
    }
    component SectionButton: Button {
        property bool selected: false
        Layout.fillWidth: true
        implicitHeight: 42
        Accessible.name: text
        palette.buttonText: root.theme.text
        background: Rectangle { radius: 7; color: parent.selected ? root.theme.tableHighlight : parent.hovered ? root.theme.surfaceHover : "transparent"; border.color: parent.activeFocus ? root.theme.accent : "transparent" }
    }
    component SettingField: TextField {
        implicitHeight: 40
        Layout.fillWidth: true
        color: root.theme.text
        placeholderTextColor: root.theme.placeholderText
        selectByMouse: true
        background: Rectangle { radius: 6; color: root.theme.controlBackground; border.color: parent.activeFocus ? root.theme.accent : root.theme.controlBorder }
    }
    RowLayout {
        anchors.fill: parent
        spacing: 14
        GlassPage {
            theme: root.theme
            Layout.preferredWidth: root.width < 850 ? 146 : 180
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 10
                Label { text: "系统设置"; color: root.theme.text; font.pixelSize: 20; font.bold: true; Layout.fillWidth: true }
                SectionButton { objectName: "settings-appearance-tab"; text: "外观与快捷键"; selected: root.section === 0; onClicked: root.section = 0 }
                SectionButton { objectName: "settings-model-tab"; text: "大模型接入"; selected: root.section === 1; onClicked: root.section = 1 }
                Item { Layout.fillHeight: true }
            }
        }
        GlassPage {
            theme: root.theme
            Layout.fillWidth: true; Layout.fillHeight: true
            ScrollView {
                anchors.fill: parent; anchors.margins: 24
                contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: parent.width; spacing: 18
                    ColumnLayout {
                        visible: root.section === 0
                        Layout.fillWidth: true; spacing: 20
                        Label { text: "界面外观"; color: root.theme.text; font.pixelSize: 23; font.bold: true }
                        Label { text: "选择适合当前工作环境的显示方式"; color: root.theme.subText; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Switch { objectName: "settings-dark"; text: "深色外观"; checked: root.theme.isDark; palette.windowText: root.theme.text
                            onToggled: { root.theme.followSystem = false; root.theme.isDark = checked; root.persistAppearance("next/appearance/dark", checked); root.persistAppearance("next/appearance/followSystem", false) } }
                        Switch { objectName: "settings-follow-system"; text: "跟随系统外观"; checked: root.theme.followSystem; palette.windowText: root.theme.text
                            onToggled: { root.theme.followSystem = checked; if (checked) root.theme.isDark = Qt.styleHints.colorScheme === Qt.ColorScheme.Dark; root.persistAppearance("next/appearance/followSystem", checked) } }
                        Switch { objectName: "settings-reduced-transparency"; text: "减少透明度"; checked: root.theme.reducedTransparency; palette.windowText: root.theme.text
                            onToggled: { root.theme.reducedTransparency = checked; root.persistAppearance("next/appearance/reducedTransparency", checked) } }
                        Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.controlBorder }
                        Label { text: "快捷键"; color: root.theme.text; font.pixelSize: 19; font.bold: true }
                        Label { text: "Ctrl / ⌘ + O  打开日志\nCtrl / ⌘ + F  搜索日志\nF3 / Shift + F3  下一处 / 上一处\nCtrl + Shift + F  智能组帧\nCtrl + Shift + S  协议同步"; color: root.theme.subText; lineHeight: 1.7; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    }
                    ColumnLayout {
                        visible: root.section === 1
                        Layout.fillWidth: true; spacing: 14
                        Label { text: "大模型接入"; color: root.theme.text; font.pixelSize: 23; font.bold: true }
                        Label { text: "填写 DeepSeek 或兼容服务的接入信息。模型配置保存在本机，API Key 使用系统安全凭据存储。"; color: root.theme.subText; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Label { visible: !root.controller; text: "静态预览：尚未接入设置控制器，不能保存或测试连接。"; color: root.theme.warning; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Label { objectName: "settings-credential-unavailable"; visible: !!root.controller && (root.controller.credentialStatus === "ready" || root.controller.credentialStatus === "failed") && !root.controller.credentialAvailable; text: "当前系统安全凭据存储不可用，不能保存密钥或生成报文。"; color: root.theme.warning; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Label { text: "服务地址（HTTPS）"; color: root.theme.text }
                        SettingField { id: endpoint; objectName: "settings-endpoint"; placeholderText: "https://api.deepseek.com"; enabled: !!root.controller && !root.locked; Accessible.name: "大模型服务地址" }
                        Label { text: "填写服务根地址或 /v1 根地址，不包含 /chat/completions。"; color: root.theme.subText; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Label { text: "模型 ID"; color: root.theme.text }
                        SettingField { id: modelName; objectName: "settings-model"; placeholderText: "填写服务支持的模型 ID"; enabled: !!root.controller && !root.locked; Accessible.name: "模型 ID" }
                        Label { text: "API Key"; color: root.theme.text }
                        SettingField { id: apiKey; objectName: "settings-api-key"; echoMode: TextInput.Password; placeholderText: root.controller && root.controller.hasApiKey ? "已保存；同一服务地址下留空可保留" : "输入 API Key"; enabled: !!root.controller && root.controller.credentialAvailable && !root.locked; Accessible.name: "API Key"; inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText }
                        Label { objectName: "settings-key-status"; textFormat: Text.PlainText; text: !root.controller ? "尚未接入设置控制器" : root.controller.credentialStatus !== "ready" ? (root.controller.credentialMessage || "安全凭据状态尚未确认") : root.controller.hasApiKey ? "•••••••• · 已保存于 " + root.controller.baseUrl : "未保存密钥"; color: root.theme.subText; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Flow {
                            Layout.fillWidth: true; spacing: 10
                            Button { objectName: "settings-save"; text: root.settingsPending && root.settingsOperationKind === "save" ? "保存处理中…" : "保存设置"; enabled: !!root.controller && root.controller.credentialAvailable && !root.locked && !!endpoint.text.trim() && !!modelName.text.trim(); onClicked: root.beginSettingsOperation("save") }
                            Button { objectName: "settings-clear-key"; text: root.settingsPending && root.settingsOperationKind === "clear" ? "清除处理中…" : "清除已保存密钥"; enabled: !!root.controller && root.controller.hasApiKey && !root.locked; onClicked: root.beginSettingsOperation("clear") }
                            Button { objectName: "settings-test"; text: "测试连接"; enabled: !!root.controller && root.controller.credentialAvailable && !root.locked && !!endpoint.text.trim() && !!modelName.text.trim(); onClicked: root.startConnectionTest() }
                            Button { objectName: "settings-cancel-test"; text: "取消连接测试"; visible: !!root.controller && root.controller.connectionBusy && root.ownedConnectionId === root.controller.connectionRequestId; onClicked: root.cancelOwnedConnection() }
                        }
                        Label { text: "连接测试会发送一次小型真实工具请求，可能产生少量计费；测试不保存当前输入。"; color: root.theme.subText; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Label { objectName: "settings-connection-status"; textFormat: Text.PlainText; text: root.controller ? root.controller.connectionMessage : "尚未测试连接"; color: root.controller && root.controller.connectionStatus === "failed" ? root.theme.error : root.theme.subText; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.controlBorder }
                        Label { text: "AI 组帧提示词"; color: root.theme.text; font.bold: true }
                        Label {
                            objectName: "settings-prompt-path"
                            textFormat: Text.PlainText
                            text: root.controller && root.controller.promptFilePath ? root.controller.promptFilePath : "尚未接入提示词文件"
                            color: root.theme.subText; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                        }
                        Label {
                            text: "使用 UTF-8 文本编辑器修改；保存后从下一次组帧任务生效，当前澄清对话保持原版本。共享同步请在协议同步页面选择“AI 提示词”。"
                            color: root.theme.subText; Layout.fillWidth: true; wrapMode: Text.Wrap
                        }
                        Button {
                            objectName: "settings-open-prompts"
                            text: "打开提示词文件夹"
                            enabled: !!(root.controller && root.controller.promptDirectoryUrl)
                            onClicked: {
                                if (!Qt.openUrlExternally(root.controller.promptDirectoryUrl)) {
                                    root.noticeError = true; root.notice = "无法打开提示词目录，请检查路径是否可访问。"
                                }
                            }
                        }
                        Label { objectName: "settings-operation-status"; textFormat: Text.PlainText; visible: root.notice.length > 0; text: root.settingsPending && root.controller && root.controller.credentialStatus === "waiting" ? root.controller.credentialMessage : root.notice; color: root.settingsPending ? root.theme.subText : root.noticeError ? root.theme.error : root.theme.success; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    }
                }
            }
        }
    }
}
