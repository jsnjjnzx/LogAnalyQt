import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar
    property var targetWindow
    property string title: ""
    property int titlePixelSize: 21
    property color titleBarColor: "#263344"
    property color titleTextColor: "#e8eef8"
    property color buttonHoverColor: "#40536d"
    property color closeButtonHoverColor: "#ab4650"
    property color closeButtonIconHoverColor: "white"
    readonly property bool embedded: targetWindow && targetWindow.embedded === true
    height: embedded ? 0 : 60
    visible: !embedded
    color: titleBarColor
    gradient: Gradient {
        GradientStop { position: 0; color: Qt.lighter(bar.titleBarColor, 1.08) }
        GradientStop { position: 1; color: bar.titleBarColor }
    }
    MouseArea {
        anchors.fill: parent; anchors.rightMargin: buttons.width + 20
        onPressed: if (bar.targetWindow) bar.targetWindow.startSystemMove()
        onDoubleClicked: if (bar.targetWindow) { if (bar.targetWindow.visibility === Window.Maximized) bar.targetWindow.showNormal(); else bar.targetWindow.showMaximized(); }
    }
    Label { anchors.left: parent.left; anchors.leftMargin: 22; anchors.verticalCenter: parent.verticalCenter; width: parent.width - buttons.width - 55; elide: Text.ElideRight; text: bar.title; color: bar.titleTextColor; font.pixelSize: bar.titlePixelSize; font.weight: Font.DemiBold }
    Row {
        id: buttons; anchors.right: parent.right; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter; spacing: 2
        Repeater {
            model: [{icon:0xF05B0,name:"最小化"},{icon:0xF05AF,name:"最大化"},{icon:0xF05AD,name:"关闭"}]
            delegate: Next.GlassButton {
                id: windowButton
                required property var modelData
                required property int index
                objectName: "window-control-" + index
                width: 36; height: 32
                readonly property bool restore: !!(index === 1 && bar.targetWindow && bar.targetWindow.visibility === Window.Maximized)
                Accessible.name: restore ? "还原" : modelData.name
                ToolTip.visible: hovered
                ToolTip.delay: 700
                ToolTip.text: windowButton.Accessible.name
                background: Rectangle {
                    radius: 7
                    color: windowButton.hovered ? (windowButton.index === 2 ? bar.closeButtonHoverColor : bar.buttonHoverColor) : "transparent"
                    border.width: windowButton.visualFocus ? 1 : 0
                    border.color: bar.titleTextColor
                }
                contentItem: Text {
                    text: String.fromCodePoint(windowButton.restore ? 0xF05B2 : windowButton.modelData.icon)
                    font.family: "Material Design Icons"; font.pixelSize: 16
                    color: windowButton.index === 2 && windowButton.hovered ? bar.closeButtonIconHoverColor : bar.titleTextColor
                    opacity: windowButton.hovered || windowButton.visualFocus ? 1 : 0.78
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                onClicked: {
                    if (!bar.targetWindow) return;
                    if (index === 0) bar.targetWindow.showMinimized();
                    else if (index === 2) bar.targetWindow.close();
                    else if (bar.targetWindow.visibility === Window.Maximized) bar.targetWindow.showNormal();
                    else bar.targetWindow.showMaximized();
                }
            }
        }
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#26ffffff" }
}
