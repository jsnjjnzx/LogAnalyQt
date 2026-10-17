import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.Button {
    id: control
    implicitHeight: 36
    padding: 8
    horizontalPadding: 14
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    background: ControlSurface {
        implicitWidth: 80
        implicitHeight: 36
        baseColor: control.highlighted || control.checked ? control.palette.highlight : control.down ? Qt.darker(control.palette.button, 1.15) : control.palette.button
        edgeColor: control.highlighted || control.checked ? Qt.lighter(control.palette.highlight, 1.15) : control.palette.mid
        focusColor: control.palette.highlight
        focused: control.visualFocus
        pressed: control.down
        hovered: control.hovered
    }
}
