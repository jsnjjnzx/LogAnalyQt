import QtQuick

Item {
    visible: false
    property bool isDark: true
    property bool followSystem: false
    property bool reducedTransparency: false
    property bool isManualMode: false
    property bool isDiffMode: false
    readonly property color background: isDark ? "#1d2025" : "#edf0f4"
    readonly property color text: isDark ? "#eceef2" : "#262a32"
    readonly property color subText: isDark ? "#a7adb7" : "#5c6470"
    readonly property color controlBackground: isDark ? "#e6191d23" : "#f5ffffff"
    readonly property color controlBorder: isDark ? "#444a53" : "#bbc2cc"
    readonly property color groupBorder: isDark ? "#3c424b" : "#c4cad3"
    readonly property color readOnlyBackground: controlBackground
    readonly property color splitHandle: "transparent"
    readonly property color splitHandleHover: isDark ? "#536071" : "#97a6ba"
    readonly property color splitHandlePressed: "#0a7aff"
    readonly property color placeholderText: isDark ? "#9199a5" : "#697482"
    readonly property color buttonBackground: isDark ? "#d531363e" : "#f2f5f8"
    readonly property color buttonHover: isDark ? "#434a55" : "#e3e9f1"
    readonly property color buttonPressed: isDark ? "#232a34" : "#cedbed"
    readonly property color buttonText: text
    readonly property color tableHighlight: isDark ? "#b0254568" : "#cfe3fc"
    readonly property color accent: isDark ? "#80b9ff" : "#0965ce"
    readonly property color accentMuted: "#087af5"
    readonly property color surface: isDark ? (reducedTransparency ? "#282c33" : "#ae282c33") : (reducedTransparency ? "#ffffff" : "#cfffffff")
    readonly property color surfaceHover: isDark ? "#393f49" : "#e4e9f0"
    readonly property color sidebar: isDark ? (reducedTransparency ? "#202329" : "#b51e2127") : (reducedTransparency ? "#e4e8ee" : "#bde3e8f0")
    readonly property color navigationSelected: isDark ? "#be363c47" : "#c8d6e7"
    readonly property color separator: isDark ? "#30ffffff" : "#24000000"
    readonly property color highlightEdge: isDark ? "#18ffffff" : "#d9ffffff"
    readonly property color popupBackground: isDark ? "#292d35" : "#f8f9fb"
    readonly property color success: isDark ? "#7ed6a0" : "#20743b"
    readonly property color warning: isDark ? "#e8b76a" : "#8f5c0d"
    readonly property color error: isDark ? "#f79a93" : "#b13237"
    readonly property color rowAlt: isDark ? "#08ffffff" : "#06000000"
    Connections {
        target: Qt.styleHints
        function onColorSchemeChanged() {
            if (followSystem) isDark = Qt.styleHints.colorScheme === Qt.ColorScheme.Dark;
        }
    }
}
