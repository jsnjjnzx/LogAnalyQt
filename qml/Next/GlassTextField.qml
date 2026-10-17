import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.TextField {
    id: control
    implicitHeight: 36
    leftPadding: 12
    rightPadding: 12
    color: palette.text
    placeholderTextColor: palette.placeholderText
    selectionColor: palette.highlight
    selectedTextColor: palette.highlightedText
    selectByMouse: true
    opacity: enabled ? 1 : 0.45
    background: ControlSurface {
        implicitWidth: 120
        implicitHeight: 36
        inset: true
        baseColor: control.palette.base
        edgeColor: control.palette.mid
        focusColor: control.palette.highlight
        focused: control.activeFocus
    }
}
