import QtQuick
import QtQuick.Templates as T

T.ToolTip {
    id: control
    objectName: "workshop-tooltip"
    readonly property var appTheme: Theme.appTheme
    readonly property bool opaqueSurface: appTheme ? appTheme.opaqueSurfaces : false
    readonly property color surfaceColor: appTheme ? appTheme.popupBackground : "#292d35"
    readonly property color textColor: appTheme ? appTheme.text : "#eceef2"

    x: parent ? (parent.width - implicitWidth) / 2 : 0
    y: parent ? Math.min(parent.height, 40) + 8 : 8
    margins: 12
    horizontalPadding: 12
    verticalPadding: 8
    font.pixelSize: 12
    implicitWidth: Math.min(384,
        parent && parent.Window.window ? Math.max(40, parent.Window.window.width - 2 * margins) : 384,
        implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    contentItem: Text {
        objectName: "workshop-tooltip-text"
        text: control.text
        textFormat: Text.PlainText
        font: control.font
        color: control.textColor
        wrapMode: Text.Wrap
        maximumLineCount: 12
        elide: Text.ElideRight
        lineHeight: 1.25
    }

    background: Rectangle {
        objectName: "workshop-tooltip-surface"
        radius: 9
        gradient: Gradient {
            GradientStop {
                position: 0
                color: Qt.rgba(Qt.lighter(control.surfaceColor, 1.16).r,
                    Qt.lighter(control.surfaceColor, 1.16).g, Qt.lighter(control.surfaceColor, 1.16).b,
                    control.opaqueSurface ? 1 : 0.96)
            }
            GradientStop {
                position: 1
                color: Qt.rgba(control.surfaceColor.r, control.surfaceColor.g, control.surfaceColor.b,
                    control.opaqueSurface ? 1 : 0.92)
            }
        }
        border.width: 1
        border.color: "#42c6d3e5"
        Rectangle {
            z: -2
            anchors.fill: parent
            anchors.leftMargin: -4; anchors.rightMargin: -4
            anchors.bottomMargin: -7
            radius: 13
            color: "#12000000"
        }
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.leftMargin: -2; anchors.rightMargin: -2
            anchors.topMargin: 2; anchors.bottomMargin: -4
            radius: 11
            color: "#1c000000"
        }
    }
}
