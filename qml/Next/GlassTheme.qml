import QtQuick

Item {
    visible: false
    property bool reducedTransparency: false
    property bool nativeBackdropAvailable: false
    property bool systemReducedTransparency: false
    readonly property bool opaqueSurfaces: reducedTransparency || systemReducedTransparency
    readonly property bool nativeGlass: nativeBackdropAvailable && !opaqueSurfaces
    property bool isManualMode: false
    property bool isDiffMode: false
    readonly property color background: "#1d2025"
    readonly property color text: "#eceef2"
    readonly property color subText: nativeGlass ? "#b6bdc9" : "#a7adb7"
    readonly property color controlBackground: nativeGlass ? "#70151b26" : "#e6191d23"
    readonly property color controlBorder: nativeGlass ? "#38ffffff" : "#444a53"
    readonly property color groupBorder: nativeGlass ? "#26ffffff" : "#3c424b"
    readonly property color readOnlyBackground: controlBackground
    readonly property color splitHandle: "transparent"
    readonly property color splitHandleHover: "#536071"
    readonly property color splitHandlePressed: "#0a7aff"
    readonly property color placeholderText: "#9199a5"
    readonly property color buttonBackground: nativeGlass ? "#35a5b4ce" : "#d531363e"
    readonly property color buttonHover: "#434a55"
    readonly property color buttonPressed: "#232a34"
    readonly property color buttonText: text
    readonly property color tableHighlight: "#b0254568"
    readonly property color accent: "#80b9ff"
    readonly property color accentMuted: "#087af5"
    readonly property color surface: opaqueSurfaces ? "#282c33" : nativeGlass ? "#30202a3b" : "#ae282c33"
    readonly property color surfaceHover: nativeGlass ? "#309aaecb" : "#393f49"
    readonly property color sidebar: opaqueSurfaces ? "#202329" : nativeGlass ? "#20131b2b" : "#b51e2127"
    readonly property color navigationSelected: nativeGlass ? "#3d9aa9cd" : "#be363c47"
    readonly property color separator: "#30ffffff"
    readonly property color highlightEdge: "#18ffffff"
    readonly property color popupBackground: "#292d35"
    readonly property color success: "#7ed6a0"
    readonly property color warning: "#e8b76a"
    readonly property color error: "#f79a93"
    readonly property color rowAlt: "#08ffffff"
}
