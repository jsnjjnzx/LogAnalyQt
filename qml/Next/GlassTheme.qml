import QtQuick

Item {
    visible: false
    property string colorMode: "dark"
    readonly property bool lightMode: colorMode === "light"
    readonly property bool monokaiMode: colorMode === "monokai"
    function markerTextColor(color) { return lightMode ? Qt.darker(color, 2.4) : color }
    property bool reducedTransparency: false
    property bool nativeBackdropAvailable: false
    property bool systemReducedTransparency: false
    readonly property bool opaqueSurfaces: reducedTransparency || systemReducedTransparency
    readonly property bool nativeGlass: nativeBackdropAvailable && !opaqueSurfaces && !monokaiMode
    property bool isManualMode: false
    property bool isDiffMode: false
    readonly property color background: monokaiMode ? "#44463e" : lightMode ? "#fbfcfc" : "#1d2025"
    readonly property color text: monokaiMode ? "#f8f8f2" : lightMode ? "#252b31" : "#eceef2"
    readonly property color subText: monokaiMode ? "#c9caba" : lightMode ? "#626d75" : (nativeGlass ? "#b6bdc9" : "#a7adb7")
    readonly property color controlBackground: monokaiMode ? "#55584e" : lightMode ? "#ffffff" : (nativeGlass ? "#70151b26" : "#e6191d23")
    readonly property color controlBorder: monokaiMode ? "#65695b" : lightMode ? "#dfe5e9" : (nativeGlass ? "#38ffffff" : "#444a53")
    readonly property color groupBorder: monokaiMode ? "#5b5e52" : lightMode ? "#e1e7eb" : (nativeGlass ? "#26ffffff" : "#3c424b")
    readonly property color readOnlyBackground: controlBackground
    readonly property color splitHandle: "transparent"
    readonly property color splitHandleHover: monokaiMode ? "#6e7263" : lightMode ? "#cbd7df" : "#536071"
    readonly property color splitHandlePressed: monokaiMode ? "#66d9ef" : lightMode ? "#245b8d" : "#0a7aff"
    readonly property color placeholderText: monokaiMode ? "#a5a899" : lightMode ? "#6d7982" : "#9199a5"
    readonly property color buttonBackground: monokaiMode ? "#55584e" : lightMode ? "#ffffff" : (nativeGlass ? "#35a5b4ce" : "#d531363e")
    readonly property color buttonHover: monokaiMode ? "#62665a" : lightMode ? "#f5f9fc" : "#434a55"
    readonly property color buttonPressed: monokaiMode ? "#4a4d43" : lightMode ? "#edf3f7" : "#232a34"
    readonly property color buttonText: text
    readonly property color highlightedText: monokaiMode ? "#1d2a2c" : "white"
    readonly property color tableHighlight: monokaiMode ? "#29f92672" : lightMode ? "#edf5fc" : "#b0254568"
    readonly property color accent: monokaiMode ? "#66d9ef" : lightMode ? "#245b8d" : "#80b9ff"
    readonly property color accentMuted: monokaiMode ? "#66d9ef" : lightMode ? "#286497" : "#087af5"
    readonly property color surface: monokaiMode ? "#4b4e45" : lightMode ? (opaqueSurfaces ? "#ffffff" : "#f5ffffff") : (opaqueSurfaces ? "#282c33" : nativeGlass ? "#30202a3b" : "#ae282c33")
    readonly property color surfaceHover: monokaiMode ? "#55594e" : lightMode ? "#f3f8fb" : (nativeGlass ? "#309aaecb" : "#393f49")
    readonly property color sidebar: monokaiMode ? "#393b34" : lightMode ? (opaqueSurfaces ? "#fafbf9" : nativeGlass ? "#59ffffff" : "#e6fafbf9") : (opaqueSurfaces ? "#202329" : nativeGlass ? "#20131b2b" : "#b51e2127")
    readonly property color navigationSelected: monokaiMode ? "#2266d9ef" : lightMode ? "#edf5fc" : (nativeGlass ? "#3d9aa9cd" : "#be363c47")
    readonly property color separator: monokaiMode ? "#2af8f8f2" : lightMode ? "#e9eef1" : "#30ffffff"
    readonly property color highlightEdge: monokaiMode ? "#18f8f8f2" : lightMode ? "#ffffff" : "#18ffffff"
    readonly property color popupBackground: monokaiMode ? "#4a4c43" : lightMode ? "#ffffff" : "#292d35"
    readonly property color success: monokaiMode ? "#a6e22e" : lightMode ? "#287451" : "#7ed6a0"
    readonly property color warning: monokaiMode ? "#e6db74" : lightMode ? "#926000" : "#e8b76a"
    readonly property color error: monokaiMode ? "#ff9ab0" : lightMode ? "#bf3838" : "#f79a93"
    readonly property color terminalBackground: monokaiMode ? "#30322b" : lightMode ? "#ffffff" : "#1a1d21"
    readonly property color terminalText: monokaiMode ? "#f8f8f2" : lightMode ? "#252b31" : "#f2f4f8"
    readonly property color terminalMatch: monokaiMode ? "#5f5124" : lightMode ? "#ffe6a3" : "#5a4a00"
    readonly property color terminalBorder: monokaiMode ? "#5f6354" : lightMode ? "#e1e7eb" : "#3c3f44"
    readonly property color terminalSelection: monokaiMode ? "#6a4351" : lightMode ? "#cce2ff" : "#446688"
    readonly property color monitorFrame: monokaiMode ? "#6f7963" : lightMode ? "#a3bdcf" : "#465971"
    readonly property color monitorFrameInactive: monokaiMode ? "#565a4d" : lightMode ? "#e1e7eb" : "#343b46"
    readonly property color monitorTab: monokaiMode ? "#474a41" : lightMode ? "#fafcfd" : "#20262f"
    readonly property color monitorTabHover: monokaiMode ? "#515449" : lightMode ? "#f3f8fb" : "#29313c"
    readonly property color monitorTabSelected: monokaiMode ? "#4f5748" : lightMode ? "#edf5fc" : "#2b3c53"
    readonly property color rowAlt: monokaiMode ? "#08f8f8f2" : lightMode ? "#030f315b" : "#08ffffff"
}
