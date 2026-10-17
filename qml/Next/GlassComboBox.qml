import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.ComboBox {
    id: control
    implicitHeight: 36
    leftPadding: 12
    rightPadding: 12
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    indicator: Text {
        x: control.width - width - 12; y: (control.height - height) / 2
        text: "⌄"; font.pixelSize: 17; color: control.palette.text
    }
    background: ControlSurface {
        implicitWidth: 120
        implicitHeight: 36
        inset: true
        baseColor: control.palette.base
        edgeColor: control.palette.mid
        focusColor: control.palette.highlight
        focused: control.visualFocus
        hovered: control.hovered
        pressed: control.down
    }
}
