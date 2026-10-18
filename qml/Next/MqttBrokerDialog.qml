import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs

Next.GlassDialog {
    id: dialog
    objectName: "mqtt-broker-dialog"
    required property var theme
    required property var controller
    property var config: ({})
    property string errorText: ""
    property string fileKey: ""
    parent: Overlay.overlay
    modal: true
    focus: true
    title: "本机 Broker 配置"
    width: 490; height: parent ? parent.height - 24 : 740
    x: parent ? parent.width - width - 12 : 0; y: 12
    palette.window: theme.popupBackground
    palette.windowText: theme.text
    palette.mid: theme.groupBorder
    function edit() {
        controller.broker.refreshInterfaces()
        config = JSON.parse(JSON.stringify(controller.broker.configuration))
        errorText = ""
        open()
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
        ScrollView {
            id: scroll
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth; clip: true
            ColumnLayout {
                width: scroll.availableWidth
                spacing: 14
                Label { textFormat: Text.PlainText; text: "监听网卡 / IP"; color: dialog.theme.subText }
                Next.GlassComboBox {
                    Layout.fillWidth: true
                    model: dialog.controller.broker.interfaces
                    textRole: "label"; valueRole: "address"
                    currentIndex: Math.max(0, indexOfValue(dialog.config.host))
                    onActivated: dialog.config.host = currentValue
                }
                Label { textFormat: Text.PlainText; text: "监听端口"; color: dialog.theme.subText }
                Next.GlassTextField { objectName: "mqtt-broker-port"; Layout.fillWidth: true; text: String(dialog.config.port || 1883); onTextEdited: dialog.config.port = text }
                Next.GlassCheckBox { id: anonymous; text: "允许匿名连接"; checked: !!dialog.config.anonymous; onToggled: dialog.config.anonymous = checked }
                Label { textFormat: Text.PlainText; text: "用户名"; color: dialog.theme.subText; enabled: !anonymous.checked }
                Next.GlassTextField { Layout.fillWidth: true; text: dialog.config.username || ""; enabled: !anonymous.checked; onTextEdited: dialog.config.username = text }
                Label { textFormat: Text.PlainText; text: "密码"; color: dialog.theme.subText; enabled: !anonymous.checked }
                Next.GlassTextField { Layout.fillWidth: true; text: dialog.config.password || ""; enabled: !anonymous.checked; echoMode: TextInput.Password; onTextEdited: dialog.config.password = text }
                Rectangle { Layout.fillWidth: true; height: 1; color: dialog.theme.groupBorder }
                Next.GlassCheckBox { id: tls; text: "启用 TLS"; checked: !!dialog.config.tls; onToggled: dialog.config.tls = checked }
                Repeater {
                    model: [{key:"certFile",label:"服务端证书"}, {key:"keyFile",label:"服务端私钥"}, {key:"caFile",label:"CA 证书（供本机测试客户端验证）"}]
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true; spacing: 5; enabled: tls.checked
                        Label { textFormat: Text.PlainText; text: modelData.label; color: dialog.theme.subText; font.pixelSize: 12 }
                        RowLayout {
                            Next.GlassTextField { Layout.fillWidth: true; text: dialog.config[modelData.key] || ""; onTextEdited: dialog.config[modelData.key] = text }
                            Next.GlassButton { text: "选择…"; onClicked: { dialog.fileKey = modelData.key; filePicker.open() } }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: dialog.theme.groupBorder }
                Next.GlassCheckBox {
                    text: "服务启动后连接本机测试客户端"
                    checked: !!dialog.config.connectLocal
                    onToggled: dialog.config.connectLocal = checked
                }
                Label { textFormat: Text.PlainText;
                    Layout.fillWidth: true
                    text: "测试模式：不会随软件自动启动。停止服务会断开接入终端，并清除本次服务的会话与保留消息。"
                    wrapMode: Text.Wrap; color: dialog.theme.subText; font.pixelSize: 12
                }
            }
        }
        Label { textFormat: Text.PlainText; Layout.fillWidth: true; text: dialog.errorText; visible: text.length > 0; color: dialog.theme.error; wrapMode: Text.Wrap }
        Label { textFormat: Text.PlainText; Layout.fillWidth: true; text: dialog.controller.broker.active ? "请先停止服务，再修改配置。" : "服务参数保存在 JSON，密码保存在系统凭据存储；同一连接留空保留已保存密码。"; color: dialog.theme.subText; font.pixelSize: 11; wrapMode: Text.Wrap }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            Next.GlassButton { text: "取消"; onClicked: dialog.close() }
            Next.GlassButton {
                objectName: "mqtt-save-broker"
                text: "保存配置"; highlighted: true
                enabled: !dialog.controller.broker.active && !dialog.controller.localClient.active
                onClicked: {
                    let result = dialog.controller.saveBroker(dialog.config)
                    if (result.ok) dialog.close()
                    else dialog.errorText = result.message
                }
            }
        }
    }
}
