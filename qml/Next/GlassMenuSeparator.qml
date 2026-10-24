import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.MenuSeparator {
    leftPadding: 10; rightPadding: 10
    topPadding: 5; bottomPadding: 5
    contentItem: Rectangle {
        implicitWidth: 192; implicitHeight: 1
        color: palette.mid
        opacity: 0.6
    }
}
