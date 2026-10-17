import QtQuick
import QtQuick.Controls

Pane {
    id: page
    property string title: ""
    property var theme
    property var ownerWindow
    property color color: theme ? theme.surface : "#222c37"
    property bool embedded: true
    signal requested()
    signal closing()
    signal closed()
    function show() { requested(); }
    function open() { requested(); }
    function close() { closing(); closed(); }
    padding: 0
    font.pixelSize: 14
    palette.window: theme ? theme.background : "#202934"
    palette.windowText: theme ? theme.text : "#e8eef8"
    palette.text: theme ? theme.text : "#e8eef8"
    palette.base: theme ? theme.controlBackground : "#1d2630"
    palette.button: theme ? theme.buttonBackground : "#354456"
    palette.buttonText: theme ? theme.text : "#e8eef8"
    palette.mid: theme ? theme.controlBorder : "#444a53"
    palette.dark: theme ? theme.groupBorder : "#3c424b"
    palette.light: theme ? theme.highlightEdge : "#18ffffff"
    palette.placeholderText: theme ? theme.placeholderText : "#9199a5"
    palette.highlight: theme ? theme.accentMuted : "#1978ee"
    palette.brightText: "white"
    palette.highlightedText: "white"
    background: Rectangle {
        radius: 14; color: page.color
        border.color: page.theme ? page.theme.groupBorder : "#3c424b"
        Rectangle { x: 14; y: 1; width: Math.max(0, parent.width - 28); height: 1; color: page.theme ? page.theme.highlightEdge : "#18ffffff" }
    }
}
