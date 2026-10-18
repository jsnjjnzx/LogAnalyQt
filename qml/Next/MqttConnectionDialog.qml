import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs

Next.GlassDialog {
    id: dialog
    objectName: "mqtt-connection-dialog"
    required property var theme
    required property var controller
    property var config: ({})
    property string errorText: ""
    property string fileKey: ""
    signal saved()
    parent: Overlay.overlay
    modal: true
    focus: true
    title: "MQTT 连接配置"
    width: 490
    height: parent ? parent.height - 24 : 740
    x: parent ? parent.width - width - 12 : 0
    y: 12
    closePolicy: Popup.CloseOnEscape
    palette.window: theme.popupBackground
    palette.windowText: theme.text
    palette.mid: theme.groupBorder
    function edit(configuration) {
        config = JSON.parse(JSON.stringify(configuration))
        errorText = ""
        tabs.currentIndex = 0
        open()
    }
    function field(key, value) { config[key] = value }
    component Setting: ColumnLayout {
        property string fieldKey
        property string caption
        property bool secret: false
        Layout.fillWidth: true
        spacing: 6
        Label { textFormat: Text.PlainText; text: parent.caption; color: dialog.theme.subText; font.pixelSize: 12 }
        Next.GlassTextField {
            objectName: "mqtt-config-" + parent.fieldKey
            Accessible.name: parent.caption
            Layout.fillWidth: true
            text: dialog.config[parent.fieldKey] === undefined ? "" : String(dialog.config[parent.fieldKey])
            echoMode: parent.secret && !showPassword.checked ? TextInput.Password : TextInput.Normal
            selectByMouse: true
            onTextEdited: dialog.field(parent.fieldKey, text)
        }
    }
    component Certificate: ColumnLayout {
        property string fieldKey
        property string caption
        Layout.fillWidth: true
        spacing: 6
        Label { textFormat: Text.PlainText; text: parent.caption; color: dialog.theme.subText; font.pixelSize: 12 }
        RowLayout {
            Layout.fillWidth: true
            Next.GlassTextField {
                Layout.fillWidth: true
                text: dialog.config[parent.parent.fieldKey] || ""
                selectByMouse: true
                onTextEdited: dialog.field(parent.parent.fieldKey, text)
            }
            Next.GlassButton {
                text: "选择…"
                onClicked: { dialog.fileKey = parent.parent.fieldKey; filePicker.open() }
            }
        }
    }
    Dialogs.FileDialog {
        id: filePicker
        title: "选择证书或私钥文件"
        onAccepted: {
            let updated = JSON.parse(JSON.stringify(dialog.config))
            updated[dialog.fileKey] = dialog.controller.filePath(selectedFile)
            dialog.config = updated
        }
    }
    contentItem: ColumnLayout {
        spacing: 12
        TabBar {
            id: tabs
            objectName: "mqtt-config-tabs"
            Layout.fillWidth: true
            TabButton { text: "基本配置" }
            TabButton { text: "TLS / 证书" }
            TabButton { text: "会话 / 遗嘱" }
        }
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: scroll.availableWidth
                spacing: 14
                Loader {
                    Layout.fillWidth: true
                    sourceComponent: tabs.currentIndex === 0 ? basic : tabs.currentIndex === 1 ? tls : advanced
                }
            }
        }
        Next.GlassCheckBox { id: showPassword; text: "显示密码"; visible: tabs.currentIndex === 0 }
        Label { textFormat: Text.PlainText;
            Layout.fillWidth: true
            text: errorText
            visible: text.length > 0
            color: dialog.theme.error
            wrapMode: Text.Wrap
        }
        Label { textFormat: Text.PlainText;
            Layout.fillWidth: true
            text: controller.remote.active ? "请先断开终端 Broker，再保存参数。" : "用户名和密码随连接配置保存在本机文件中。"
            color: dialog.theme.subText
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            Next.GlassButton { text: "取消"; onClicked: dialog.close() }
            Next.GlassButton {
                objectName: "mqtt-save-connection"
                text: "保存配置"
                highlighted: true
                enabled: !controller.remote.active
                onClicked: {
                    let result = controller.saveConnection(dialog.config)
                    if (result.ok) { dialog.saved(); dialog.close() }
                    else dialog.errorText = result.message
                }
            }
        }
    }
    Component {
        id: basic
        ColumnLayout {
            spacing: 14
            Setting { fieldKey: "name"; caption: "连接名称" }
            Setting { fieldKey: "host"; caption: "Broker 主机名或 IP（不含协议和端口）" }
            RowLayout {
                Layout.fillWidth: true
                Setting { fieldKey: "port"; caption: "端口" }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { textFormat: Text.PlainText; text: "MQTT 版本"; color: dialog.theme.subText; font.pixelSize: 12 }
                    Next.GlassComboBox {
                        Layout.fillWidth: true
                        model: ["3.1.1", "5.0"]
                        currentIndex: dialog.config.protocol === 5 ? 1 : 0
                        onActivated: dialog.field("protocol", currentIndex === 1 ? 5 : 4)
                    }
                }
            }
            Setting { fieldKey: "clientId"; caption: "Client ID" }
            Next.GlassButton {
                text: "生成新的 Client ID"
                onClicked: {
                    let updated = JSON.parse(JSON.stringify(dialog.config))
                    updated.clientId = dialog.controller.newConnection().clientId
                    dialog.config = updated
                }
            }
            Setting { fieldKey: "username"; caption: "用户名（可留空）" }
            Setting { fieldKey: "password"; caption: "密码"; secret: true }
            RowLayout {
                Setting { fieldKey: "keepAlive"; caption: "Keep Alive（秒）" }
                Setting { fieldKey: "timeoutSeconds"; caption: "连接超时（秒）" }
            }
            Next.GlassCheckBox {
                text: "连接中断后自动重连"
                checked: !!dialog.config.autoReconnect
                onToggled: dialog.field("autoReconnect", checked)
            }
            Setting { fieldKey: "reconnectSeconds"; caption: "重连间隔（秒）" }
        }
    }
    Component {
        id: tls
        ColumnLayout {
            spacing: 16
            Next.GlassCheckBox {
                id: tlsEnabled
                text: "使用 TLS 加密连接"
                checked: !!dialog.config.tls
                onToggled: dialog.field("tls", checked)
            }
            Label { textFormat: Text.PlainText;
                Layout.fillWidth: true
                text: "普通 TCP 通常使用 1883，TLS 通常使用 8883，请以终端配置为准。"
                color: dialog.theme.subText; wrapMode: Text.Wrap
            }
            Certificate { fieldKey: "caFile"; caption: "CA 证书"; enabled: tlsEnabled.checked }
            Certificate { fieldKey: "certFile"; caption: "客户端证书（双向认证时填写）"; enabled: tlsEnabled.checked }
            Certificate { fieldKey: "keyFile"; caption: "客户端私钥（与证书同时填写）"; enabled: tlsEnabled.checked }
            Label { textFormat: Text.PlainText; Layout.fillWidth: true; text: "TLS 连接会校验服务器证书与主机名。"; color: dialog.theme.subText; wrapMode: Text.Wrap }
        }
    }
    Component {
        id: advanced
        ColumnLayout {
            spacing: 14
            Next.GlassCheckBox {
                text: dialog.config.protocol === 5 ? "Clean Start · 建立新会话" : "Clean Session · 清理会话"
                checked: !!dialog.config.cleanSession
                onToggled: dialog.field("cleanSession", checked)
            }
            Setting { fieldKey: "sessionExpiry"; caption: "会话有效期（秒，0 表示断开即结束）"; visible: dialog.config.protocol === 5 }
            Rectangle { Layout.fillWidth: true; height: 1; color: dialog.theme.groupBorder }
            Next.GlassCheckBox {
                id: willEnabled
                text: "启用遗嘱消息（异常断线时由 Broker 发布）"
                checked: !!dialog.config.willEnabled
                onToggled: dialog.field("willEnabled", checked)
            }
            Setting { fieldKey: "willTopic"; caption: "遗嘱 Topic"; enabled: willEnabled.checked }
            RowLayout {
                enabled: willEnabled.checked
                Next.GlassComboBox {
                    model: ["Text", "JSON", "Hex"]
                    currentIndex: Math.max(0, model.indexOf(dialog.config.willFormat))
                    onActivated: dialog.field("willFormat", currentText)
                }
                Next.GlassComboBox {
                    model: ["QoS 0", "QoS 1", "QoS 2"]
                    currentIndex: Number(dialog.config.willQos || 0)
                    onActivated: dialog.field("willQos", currentIndex)
                }
                Next.GlassCheckBox { text: "Retain"; checked: !!dialog.config.willRetain; onToggled: dialog.field("willRetain", checked) }
            }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: 160
                enabled: willEnabled.checked
                TextArea {
                    text: dialog.config.willPayload || ""
                    color: dialog.theme.text
                    font.family: Next.PlatformFonts.monospaceFamily
                    placeholderText: "遗嘱消息内容"
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    onTextChanged: if (activeFocus) dialog.field("willPayload", text)
                    background: Rectangle { color: dialog.theme.controlBackground; border.color: dialog.theme.controlBorder; radius: 8 }
                }
            }
        }
    }
}
