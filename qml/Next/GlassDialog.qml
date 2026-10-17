import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.Dialog {
    id: control
    padding: 20
    background: Rectangle { radius: 14; color: control.palette.window; border.color: control.palette.mid }
    header: Basic.Label {
        text: control.title; visible: text.length > 0
        padding: 20; bottomPadding: 4
        font.pixelSize: 18; font.bold: true; color: control.palette.windowText
        elide: Text.ElideRight
    }
}
