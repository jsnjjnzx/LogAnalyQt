import QtQuick
import QtQuick.Effects

Rectangle {
    id: surface
    property var theme
    color: theme ? theme.background : "#1d2025"
    Image { id: backdrop; anchors.fill: parent; source: "assets/glass-background.png"; fillMode: Image.PreserveAspectCrop; mirror: true; visible: false }
    MultiEffect { anchors.fill: parent; source: backdrop; blurEnabled: true; blurMax: 64; blur: 1; saturation: -0.35; brightness: 0.04; opacity: 0.85; visible: !theme || (theme.isDark && !theme.reducedTransparency) }
    Rectangle { anchors.fill: parent; color: !surface.theme || surface.theme.isDark ? "#351d2025" : "#30ffffff" }
}
