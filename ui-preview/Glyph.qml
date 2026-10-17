import QtQuick

Text {
    property int code: 0xF09EE
    text: String.fromCodePoint(code)
    font.family: icons.name
    font.pixelSize: 24
    color: "#cbd8e8"
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    FontLoader {
        id: icons
        source: Qt.resolvedUrl("../assets/materialdesignicons-webfont.ttf").toString().startsWith("qrc:")
                ? "qrc:/icons/materialdesignicons-webfont.ttf"
                : Qt.resolvedUrl("../assets/materialdesignicons-webfont.ttf")
    }
}
