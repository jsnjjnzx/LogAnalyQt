import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.MenuItem {
    id: control
    implicitHeight: 34
    implicitWidth: Math.max(212, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: checkable ? 32 : 12
    rightPadding: subMenu ? 30 : 12
    font.pixelSize: 13
    hoverEnabled: true
    indicator: Rectangle {
        x: 12
        y: (control.height - height) / 2
        width: 14; height: 14
        visible: control.checkable
        radius: 3
        color: "transparent"
        border.width: 1
        border.color: control.highlighted ? control.palette.highlightedText : control.palette.text
        opacity: control.enabled ? 1 : 0.38
        Text {
            objectName: "menu-check-mark"
            anchors.centerIn: parent
            visible: control.checked
            text: "✓"
            font.pixelSize: 11
            color: parent.border.color
        }
    }
    contentItem: Text {
        text: control.text
        textFormat: Text.PlainText
        font: control.font
        color: control.highlighted ? control.palette.highlightedText : control.palette.text
        opacity: control.enabled ? 1 : 0.38
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 7
        color: control.highlighted && control.enabled ? control.palette.highlight : "transparent"
    }
}
