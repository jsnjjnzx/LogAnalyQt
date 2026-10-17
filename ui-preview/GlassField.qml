import QtQuick
import QtQuick.Controls

TextField {
    id: control
    property int iconCode: 0xF0349
    property bool invalid: false
    implicitHeight: 44
    leftPadding: 49
    rightPadding: 14
    color: "#e3eaf4"
    placeholderTextColor: "#95a0ae"
    selectionColor: "#3266a5"
    selectedTextColor: "white"
    font.pixelSize: 16
    selectByMouse: true
    Accessible.name: placeholderText
    background: Rectangle {
        radius: 9
        color: "#70252d37"
        border.color: control.invalid ? "#ff9b89" : control.activeFocus ? "#6dabff" : "#55546476"
        Glyph { x: 14; height: parent.height; code: control.iconCode; color: "#9aa8b8" }
    }
}
