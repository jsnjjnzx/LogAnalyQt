import QtQuick
import QtQuick.Effects

Rectangle {
    id: surface
    property var theme
    visible: !theme || !theme.nativeGlass
    color: theme ? theme.background : "#1d2025"
    Image { id: backdrop; anchors.fill: parent; source: "assets/glass-background.png"; fillMode: Image.PreserveAspectCrop; mirror: true; visible: false }
    MultiEffect { anchors.fill: parent; source: backdrop; blurEnabled: true; blurMax: 64; blur: 1; saturation: -0.35; brightness: 0.04; opacity: 0.85; visible: !theme || !theme.opaqueSurfaces }
    Rectangle { anchors.fill: parent; color: "#351d2025" }
}
