import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.Menu {
    id: control
    padding: 5
    background: Rectangle {
        implicitWidth: 200; implicitHeight: 40
        radius: 10; color: control.palette.window; border.color: control.palette.mid
    }
}
