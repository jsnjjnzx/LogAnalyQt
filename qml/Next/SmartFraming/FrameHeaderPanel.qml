import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt

Item {
    id: root
    property var theme
    property string defaultAddress: ""
    property bool expanded: false
    property bool serverResponseMode: false
    implicitHeight: header.height + (root.expanded ? body.implicitHeight + 8 : 0)

    onServerResponseModeChanged: applyDirectionDefaults()

    function applyDirectionDefaults() {
        dirCheck.checked = root.serverResponseMode
        prmCheck.checked = !root.serverResponseMode
        funcCode.value = 3
    }

    function getOptions() {
        return {
            dir: dirCheck.checked,
            prm: prmCheck.checked,
            split: splitCheck.checked,
            scramble: scrambleCheck.checked,
            funcCode: Number(funcCode.value),
            saType: saTypeCombo.currentIndex,
            logicAddress: Number(logicAddress.value),
            ca: Number(caSpin.value),
            physicalAddressHex: addrField.text.replace(/\s/g, "")
        }
    }

    Rectangle {
        id: header
        width: parent.width
        height: 30
        color: root.theme ? root.theme.controlBackground : "#ffffff"
        border.color: root.theme ? root.theme.controlBorder : "#cccccc"
        radius: 8

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            Text {
                text: root.expanded ? "▼" : "▶"
                color: root.theme ? root.theme.subText : "#666"
                font.pixelSize: 12
            }
            Label {
                text: "完整帧封装选项"
                color: root.theme ? root.theme.text : "#333"
                font.bold: true
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Label {
                text: root.expanded ? "已展开" : "使用默认帧头"
                color: root.theme ? root.theme.subText : "#666"
                font.pixelSize: 11
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.expanded = !root.expanded
        }
    }

    ColumnLayout {
        id: body
        anchors.top: header.bottom
        anchors.topMargin: 8
        width: parent.width
        visible: root.expanded
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: "服务器地址"; color: root.theme ? root.theme.subText : "#666"; Layout.preferredWidth: 76 }
            Next.GlassTextField {
                id: addrField
                Layout.fillWidth: true
                text: root.defaultAddress
                placeholderText: "物理地址 hex；为空则使用 000000000000"
                color: root.theme ? root.theme.text : "#333"
                placeholderTextColor: root.theme ? root.theme.placeholderText : "#888"
                font.family: Next.PlatformFonts.monospaceFamily
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            rowSpacing: 8
            columnSpacing: 10

            Next.GlassCheckBox { id: dirCheck; text: "DIR 服务器→客户机"; checked: false; palette.windowText: root.theme ? root.theme.text : "#333" }
            Next.GlassCheckBox { id: prmCheck; text: "PRM 主动"; checked: true; palette.windowText: root.theme ? root.theme.text : "#333" }
            Next.GlassCheckBox { id: splitCheck; text: "分帧"; checked: false; palette.windowText: root.theme ? root.theme.text : "#333" }
            Next.GlassCheckBox { id: scrambleCheck; text: "扰码"; checked: false; palette.windowText: root.theme ? root.theme.text : "#333" }

            Label { text: "功能码"; color: root.theme ? root.theme.subText : "#666" }
            Next.CompactSpinBox { id: funcCode; from: 0; to: 7; value: 3; editable: true; Layout.fillWidth: true }
            Label { text: "地址类型"; color: root.theme ? root.theme.subText : "#666" }
            Next.GlassComboBox { id: saTypeCombo; model: ["单地址", "通配地址", "组地址", "广播地址"]; Layout.fillWidth: true }

            Label { text: "逻辑地址"; color: root.theme ? root.theme.subText : "#666" }
            Next.CompactSpinBox { id: logicAddress; from: 0; to: 1; value: 0; editable: true; Layout.fillWidth: true }
            Label { text: "CA"; color: root.theme ? root.theme.subText : "#666" }
            Next.CompactSpinBox { id: caSpin; from: 0; to: 255; value: 0; editable: true; Layout.fillWidth: true }
        }
    }
}
