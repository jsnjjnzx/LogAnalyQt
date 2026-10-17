import QtQuick

Item {
    visible: false
    property bool isDark: true
    property bool followSystem: false
    property bool reducedTransparency: false
    property bool isManualMode: false
    property bool isDiffMode: false
    readonly property color background: isDark ? "#202934" : "#eaf0f6"
    readonly property color text: isDark ? "#e8eef8" : "#263449"
    readonly property color subText: isDark ? "#a6b7ce" : "#576c86"
    readonly property color controlBackground: isDark ? "#dd1d2630" : "#ebffffff"
    readonly property color controlBorder: isDark ? "#526175" : "#bdcddd"
    readonly property color groupBorder: isDark ? "#4a596b" : "#b8cbdc"
    readonly property color readOnlyBackground: controlBackground
    readonly property color splitHandle: isDark ? "#293849" : "#b4c7dc"
    readonly property color splitHandleHover: isDark ? "#5b799c" : "#789ac0"
    readonly property color splitHandlePressed: "#529dff"
    readonly property color placeholderText: isDark ? "#91a3b9" : "#657992"
    readonly property color buttonBackground: isDark ? "#ad354456" : "#e9f0f7"
    readonly property color buttonHover: isDark ? "#4c6078" : "#d4e4f6"
    readonly property color buttonPressed: "#356ca7"
    readonly property color buttonText: text
    readonly property color tableHighlight: isDark ? "#355d90" : "#c8e0ff"
    readonly property color accent: isDark ? "#91c4ff" : "#1267ca"
    readonly property color accentMuted: "#116adf"
    readonly property color surface: isDark ? "#dd222c37" : "#edffffff"
    readonly property color surfaceHover: isDark ? "#35445a" : "#e3edf9"
    readonly property color success: isDark ? "#7ed6a0" : "#20743b"
    readonly property color warning: isDark ? "#e8b76a" : "#8f5c0d"
    readonly property color error: isDark ? "#f79a93" : "#b13237"
    readonly property color rowAlt: isDark ? "#202a36" : "#f2f6fb"
    Connections {
        target: Qt.styleHints
        function onColorSchemeChanged() {
            if (followSystem) isDark = Qt.styleHints.colorScheme === Qt.ColorScheme.Dark;
        }
    }
}
