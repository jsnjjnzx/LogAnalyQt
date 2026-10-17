import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.CheckBox {
    id: control
    spacing: 8
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    indicator: ControlSurface {
        implicitWidth: 19; implicitHeight: 19
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 5
        baseColor: control.checkState !== Qt.Unchecked ? control.palette.highlight : control.palette.base
        edgeColor: control.palette.mid
        focused: control.visualFocus
        focusColor: control.palette.highlight
        pressed: control.down
        hovered: control.hovered
        Text {
            anchors.centerIn: parent
            text: control.checkState === Qt.PartiallyChecked ? "−" : "✓"
            visible: control.checkState !== Qt.Unchecked
            color: "white"; font.pixelSize: 14; font.bold: true
        }
    }
}
