import QtQuick

Rectangle {
    id: surface
    property color baseColor: "#31363e"
    property color edgeColor: "#444a53"
    property bool pressed: false
    property bool hovered: false
    property bool focused: false
    property color focusColor: "#80b9ff"
    property bool inset: false
    radius: 8
    border.color: focused ? focusColor : edgeColor
    border.width: focused ? 2 : 1
    gradient: Gradient {
        GradientStop { position: 0; color: surface.inset || surface.pressed ? Qt.darker(surface.baseColor, 1.08) : Qt.lighter(surface.baseColor, surface.hovered ? 1.18 : 1.10) }
        GradientStop { position: 1; color: surface.baseColor }
    }
    Rectangle {
        x: parent.radius; y: 1; width: Math.max(0, parent.width - 2 * x); height: 1
        color: surface.inset || surface.pressed ? "#14000000" : "#18ffffff"
    }
    Rectangle {
        x: parent.radius; y: parent.height - 1; width: Math.max(0, parent.width - 2 * x); height: 1
        color: surface.inset ? "#0cffffff" : "#18000000"
    }
}
