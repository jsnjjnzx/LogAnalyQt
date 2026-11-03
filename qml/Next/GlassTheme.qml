import QtQuick

Item {
    visible: false
    property bool lightMode: false
    function markerTextColor(color) { return lightMode ? Qt.darker(color, 2.4) : color }
    property bool reducedTransparency: false
    property bool nativeBackdropAvailable: false
    property bool systemReducedTransparency: false
    readonly property bool opaqueSurfaces: reducedTransparency || systemReducedTransparency
    readonly property bool nativeGlass: nativeBackdropAvailable && !opaqueSurfaces
    property bool isManualMode: false
    property bool isDiffMode: false
    readonly property color background: lightMode ? "#fbfcfc" : "#1d2025"
    readonly property color text: lightMode ? "#252b31" : "#eceef2"
    readonly property color subText: lightMode ? "#626d75" : (nativeGlass ? "#b6bdc9" : "#a7adb7")
    readonly property color controlBackground: lightMode ? "#ffffff" : (nativeGlass ? "#70151b26" : "#e6191d23")
    readonly property color controlBorder: lightMode ? "#dfe5e9" : (nativeGlass ? "#38ffffff" : "#444a53")
    readonly property color groupBorder: lightMode ? "#e1e7eb" : (nativeGlass ? "#26ffffff" : "#3c424b")
    readonly property color readOnlyBackground: controlBackground
    readonly property color splitHandle: "transparent"
    readonly property color splitHandleHover: lightMode ? "#cbd7df" : "#536071"
    readonly property color splitHandlePressed: lightMode ? "#245b8d" : "#0a7aff"
    readonly property color placeholderText: lightMode ? "#6d7982" : "#9199a5"
    readonly property color buttonBackground: lightMode ? "#ffffff" : (nativeGlass ? "#35a5b4ce" : "#d531363e")
    readonly property color buttonHover: lightMode ? "#f5f9fc" : "#434a55"
    readonly property color buttonPressed: lightMode ? "#edf3f7" : "#232a34"
    readonly property color buttonText: text
    readonly property color tableHighlight: lightMode ? "#edf5fc" : "#b0254568"
    readonly property color accent: lightMode ? "#245b8d" : "#80b9ff"
    readonly property color accentMuted: lightMode ? "#286497" : "#087af5"
    readonly property color surface: lightMode ? (opaqueSurfaces ? "#ffffff" : "#f5ffffff") : (opaqueSurfaces ? "#282c33" : nativeGlass ? "#30202a3b" : "#ae282c33")
    readonly property color surfaceHover: lightMode ? "#f3f8fb" : (nativeGlass ? "#309aaecb" : "#393f49")
    readonly property color sidebar: lightMode ? (opaqueSurfaces ? "#fafbf9" : nativeGlass ? "#59ffffff" : "#e6fafbf9") : (opaqueSurfaces ? "#202329" : nativeGlass ? "#20131b2b" : "#b51e2127")
    readonly property color navigationSelected: lightMode ? "#edf5fc" : (nativeGlass ? "#3d9aa9cd" : "#be363c47")
    readonly property color separator: lightMode ? "#e9eef1" : "#30ffffff"
    readonly property color highlightEdge: lightMode ? "#ffffff" : "#18ffffff"
    readonly property color popupBackground: lightMode ? "#ffffff" : "#292d35"
    readonly property color success: lightMode ? "#287451" : "#7ed6a0"
    readonly property color warning: lightMode ? "#926000" : "#e8b76a"
    readonly property color error: lightMode ? "#bf3838" : "#f79a93"
    readonly property color terminalBackground: lightMode ? "#ffffff" : "#1a1d21"
    readonly property color terminalText: lightMode ? "#252b31" : "#f2f4f8"
    readonly property color terminalMatch: lightMode ? "#ffe6a3" : "#5a4a00"
    readonly property color terminalBorder: lightMode ? "#e1e7eb" : "#3c3f44"
    readonly property color terminalSelection: lightMode ? "#cce2ff" : "#446688"
    readonly property color monitorFrame: lightMode ? "#a3bdcf" : "#465971"
    readonly property color monitorFrameInactive: lightMode ? "#e1e7eb" : "#343b46"
    readonly property color monitorTab: lightMode ? "#fafcfd" : "#20262f"
    readonly property color monitorTabHover: lightMode ? "#f3f8fb" : "#29313c"
    readonly property color monitorTabSelected: lightMode ? "#edf5fc" : "#2b3c53"
    readonly property color rowAlt: lightMode ? "#030f315b" : "#08ffffff"
}
