import QtQuick
import QtQuick.Effects

Rectangle {
    property var theme
    color: theme && !theme.isDark ? "#eaf0f6" : "#202934"
    Image { id: backdrop; anchors.fill: parent; source: "assets/glass-background.png"; fillMode: Image.PreserveAspectCrop; visible: false }
    MultiEffect { anchors.fill: parent; source: backdrop; blurEnabled: true; blurMax: 48; blur: 0.75; brightness: 0.04; visible: !theme || (theme.isDark && !theme.reducedTransparency) }
    Rectangle { anchors.fill: parent; color: !theme || theme.isDark ? "#30243141" : "#30ffffff" }
}
