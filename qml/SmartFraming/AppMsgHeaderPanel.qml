import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt

Item {
    id: root
    property var theme
    property string kind: "request"
    property int iid: 0
    property int iop: 0
    property bool compactLayout: width < 620
    implicitHeight: body.implicitHeight

    onKindChanged: applyKindDefaults()

    function applyKindDefaults() {
        prmCheck.checked = root.kind !== "response"
    }

    function hex4(value) {
        return "0x" + Number(value).toString(16).toUpperCase().padStart(4, "0")
    }

    function getOptions() {
        return {
            prm: prmCheck.checked,
            priority: Number(prioritySpin.value),
            index: Number(indexSpin.value),
            label: Number(labelSpin.value),
            source: sourceField.text,
            destination: destinationField.text
        }
    }

    Component.onCompleted: applyKindDefaults()

    ColumnLayout {
        id: body
        width: parent.width
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label { text: "APP 消息头"; color: root.theme ? root.theme.text : "#333"; font.bold: true }
            Item { Layout.fillWidth: true }
            Label { text: "IID " + root.hex4(root.iid) + " / IOP " + root.hex4(root.iop); color: root.theme ? root.theme.subText : "#666"; font.family: PlatformFonts.monospaceFamily; font.pixelSize: 12 }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.compactLayout ? 2 : 4
            rowSpacing: 8
            columnSpacing: 10

            CheckBox {
                id: prmCheck
                text: "PRM 启动站"
                checked: true
                palette.windowText: root.theme ? root.theme.text : "#333"
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Label { text: "优先级"; color: root.theme ? root.theme.subText : "#666" }
                SpinBox { id: prioritySpin; from: 0; to: 127; value: 0; editable: true; Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Label { text: "Index"; color: root.theme ? root.theme.subText : "#666" }
                SpinBox { id: indexSpin; from: 0; to: 65535; value: 0; editable: true; Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Label { text: "Label"; color: root.theme ? root.theme.subText : "#666" }
                SpinBox { id: labelSpin; from: 0; to: 65535; value: 0; editable: true; Layout.fillWidth: true }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.compactLayout ? 2 : 4
            rowSpacing: 8
            columnSpacing: 8

            Label {
                text: "Source"
                color: root.theme ? root.theme.subText : "#666"
                Layout.preferredWidth: 64
            }
            TextField {
                id: sourceField
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.kind === "response" ? "server" : "client"
                color: root.theme ? root.theme.text : "#333"
                background: Rectangle { color: root.theme ? root.theme.controlBackground : "#fff"; border.color: root.theme ? root.theme.controlBorder : "#ccc"; radius: 4 }
            }

            Label {
                text: "Destination"
                color: root.theme ? root.theme.subText : "#666"
                Layout.preferredWidth: 84
            }
            TextField {
                id: destinationField
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.kind === "response" ? "client" : "server"
                color: root.theme ? root.theme.text : "#333"
                background: Rectangle { color: root.theme ? root.theme.controlBackground : "#fff"; border.color: root.theme ? root.theme.controlBorder : "#ccc"; radius: 4 }
            }
        }
    }
}
