import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control
    property int iconCode: 0
    property bool primary: false
    property bool quiet: false
    property bool trailingChevron: false
    property string hint: ""
    implicitHeight: 44
    implicitWidth: contentItem.implicitWidth + (text.length ? 32 : 24)
    hoverEnabled: true
    Accessible.name: text || hint
    ToolTip.visible: hovered && hint.length > 0
    ToolTip.text: hint
    ToolTip.delay: 650
    background: Rectangle {
        radius: 8
        color: control.primary ? (control.down ? "#0056c9" : control.hovered ? "#2587ff" : "#0877fa")
             : control.down ? "#435367" : control.hovered ? "#3b4859"
             : control.quiet ? "transparent" : "#70333d48"
        border.width: control.quiet && !control.activeFocus ? 0 : 1
        border.color: control.activeFocus ? "#82b8ff" : control.primary ? "#3e94ff" : "#554f6072"
        Behavior on color { ColorAnimation { duration: 100 } }
    }
    contentItem: RowLayout {
        spacing: 10
        Glyph {
            visible: control.iconCode !== 0
            code: control.iconCode
            font.pixelSize: 23
            color: control.primary ? "white" : control.quiet ? "#8ebfff" : "#d1dbea"
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: control.quiet ? 1 : 0
        }
        Text {
            visible: control.text.length > 0
            text: control.text
            color: control.primary ? "white" : "#d6e0ed"
            font.pixelSize: 16
            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: control.width > control.implicitWidth
        }
        Glyph { visible: control.trailingChevron; code: 0xF0140; font.pixelSize: 20 }
    }
}
