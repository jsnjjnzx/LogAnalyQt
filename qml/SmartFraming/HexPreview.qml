import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt

Item {
    id: root
    property var theme
    property string hexText: ""
    property string title: "Hex"
    implicitHeight: 120

    function formattedHex() {
        var clean = (root.hexText || "").replace(/\s/g, "").toUpperCase()
        var rows = []
        for (var rowStart = 0; rowStart < clean.length; rowStart += 64) {
            var row = clean.substring(rowStart, rowStart + 64)
            var bytes = []
            for (var byteStart = 0; byteStart < row.length; byteStart += 2)
                bytes.push(row.substring(byteStart, byteStart + 2))
            rows.push(bytes.join(" "))
        }
        return rows.join("\n")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.title
                color: root.theme ? root.theme.text : "#333"
                font.bold: true
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Label {
                text: root.hexText.length > 0 ? (Math.floor(root.hexText.replace(/\s/g, "").length / 2) + " 字节") : "暂无结果"
                color: root.theme ? root.theme.subText : "#666"
                font.pixelSize: 11
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                readOnly: true
                wrapMode: TextArea.Wrap
                text: root.formattedHex()
                color: root.theme ? root.theme.accent : "#1976D2"
                placeholderText: "生成结果会显示在这里"
                placeholderTextColor: root.theme ? root.theme.placeholderText : "#888"
                font.family: PlatformFonts.monospaceFamily
                font.pixelSize: 12
                selectionColor: root.theme ? root.theme.accentMuted : "#1976D2"
                background: Rectangle {
                    color: root.theme ? root.theme.controlBackground : "#ffffff"
                    border.color: root.theme ? root.theme.controlBorder : "#cccccc"
                    radius: 4
                }
            }
        }
    }
}
