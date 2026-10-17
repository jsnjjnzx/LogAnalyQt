import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar
    property var targetWindow
    property string title: ""
    property color titleBarColor: "#263344"
    property color titleTextColor: "#e8eef8"
    property color buttonHoverColor: "#40536d"
    property color closeButtonHoverColor: "#ab4650"
    property color closeButtonIconHoverColor: "white"
    property bool isDarkTheme: true
    property bool showThemeToggle: false
    signal themeToggleClicked()
    readonly property bool embedded: targetWindow && targetWindow.embedded === true
    height: embedded ? 0 : 72
    visible: !embedded
    color: titleBarColor
    gradient: Gradient {
        GradientStop { position: 0; color: Qt.lighter(bar.titleBarColor, 1.2) }
        GradientStop { position: 1; color: bar.titleBarColor }
    }
    MouseArea {
        anchors.fill: parent; anchors.rightMargin: buttons.width + 20
        onPressed: if (bar.targetWindow) bar.targetWindow.startSystemMove()
        onDoubleClicked: if (bar.targetWindow) { if (bar.targetWindow.visibility === Window.Maximized) bar.targetWindow.showNormal(); else bar.targetWindow.showMaximized(); }
    }
    Label { anchors.left: parent.left; anchors.leftMargin: 22; anchors.verticalCenter: parent.verticalCenter; width: parent.width - buttons.width - 55; elide: Text.ElideRight; text: bar.title; color: bar.titleTextColor; font.pixelSize: 24; font.bold: true }
    Row {
        id: buttons; anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter; spacing: 5
        Button { visible: bar.showThemeToggle; text: bar.isDarkTheme ? "浅色" : "深色"; onClicked: bar.themeToggleClicked() }
        Repeater {
            model: [{label:"−",name:"最小化"},{label:"□",name:"最大化 / 还原"},{label:"×",name:"关闭"}]
            delegate: Button {
                required property var modelData
                required property int index
                width: 36; height: 32
                Accessible.name: modelData.name
                background: Rectangle { radius: 8; color: parent.hovered ? (parent.index === 2 ? bar.closeButtonHoverColor : bar.buttonHoverColor) : "transparent" }
                contentItem: Text { text: modelData.label; font.pixelSize: 21; color: bar.titleTextColor; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
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
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Qt.lighter(bar.titleBarColor, 1.5) }
}
