import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.Switch {
    id: control
    property bool trailingIndicator: false
    spacing: 12
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    indicator: Rectangle {
        implicitWidth: 48; implicitHeight: 28
        x: control.trailingIndicator ? control.width - control.rightPadding - width : control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? control.palette.highlight : control.palette.mid
        border.color: control.visualFocus ? control.palette.text : Qt.lighter(color, 1.12)
        Rectangle {
            x: 3 + control.visualPosition * (parent.width - width - 6)
            y: 3; width: 22; height: 22; radius: 11
            gradient: Gradient {
                GradientStop { position: 0; color: "#ffffff" }
                GradientStop { position: 1; color: "#e0e3e8" }
            }
            border.color: "#20000000"
            Behavior on x { NumberAnimation { duration: 120 } }
        }
    }
}
