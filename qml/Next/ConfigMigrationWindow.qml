import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 760; height: 620; minimumWidth: 580; minimumHeight: 450
    visible: true; title: "通联工坊 · 旧配置迁移确认"
    color: "#1d2025"
    property var choices: ({})
    property bool accepted: false
    signal finished(bool accepted)
    onClosing: function(event) { if (configManager.busy) event.accepted = false; else finished(false) }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 16
        Label { text: "将个人配置归拢到 JSON"; font.pixelSize: 22; color: "#eceef2" }
        ScrollView {
            id: migrationScroll
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentWidth: availableWidth
            ColumnLayout {
                width: migrationScroll.availableWidth; spacing: 12
                Label { text: configManager.migrationDescription; textFormat: Text.PlainText; color: "#eceef2"; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                Repeater {
                    model: configManager.conflictDetails
                    delegate: RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        Label { text: modelData.key + "\n当前：" + modelData.current + "\n旧配置：" + modelData.legacy; textFormat: Text.PlainText; color: "#e8b76a"; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        ComboBox {
                            model: ["请选择来源", "保留当前配置", "使用旧配置"]
                            onActivated: { var next = Object.assign({}, root.choices); next[modelData.key] = currentIndex === 1 ? "current" : currentIndex === 2 ? "legacy" : ""; root.choices = next }
                        }
                    }
                }
            }
        }
        Label { text: configManager.error; textFormat: Text.PlainText; color: "#f79a93"; wrapMode: Text.Wrap; Layout.fillWidth: true; visible: text.length > 0 }
        Label { text: "不存在的旧文件直接跳过。取消不会改动配置。迁移快照不含专用密码字段。"; color: "#a7adb7"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            BusyIndicator { running: configManager.busy; visible: running; implicitWidth: 30; implicitHeight: 30 }
            Button { objectName: "migration-cancel"; text: "取消并退出"; enabled: !configManager.busy; onClicked: root.finished(false) }
            Button { objectName: "migration-confirm"; text: configManager.busy ? "迁移并验证中…" : "迁移并继续"; enabled: !configManager.busy && configManager.canMigrate; onClicked: configManager.migrate(root.choices) }
        }
    }
    Connections { target: configManager; function onMigrationFinished(success) { if (success) { root.accepted = true; root.finished(true) } } }
}
