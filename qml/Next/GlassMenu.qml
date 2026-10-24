import QtQuick
import QtQuick.Controls.Basic as Basic
import LogAnalyQt.Next as Next

Basic.Menu {
    id: control
    padding: 6
    spacing: 2
    font.pixelSize: 13
    delegate: Next.GlassMenuItem {}
    background: Rectangle {
        implicitWidth: 224; implicitHeight: 40
        radius: 12
        color: control.palette.window
        border.color: control.palette.mid
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.topMargin: 4; anchors.bottomMargin: -4
            radius: parent.radius
            color: "#24000000"
        }
    }
}
