import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: parseOutputWindow
    required property var backend
    required property var theme
    required property int parseSessionId
    required property string parsedHex
    property string sessionName: "协议解析"
    property string namePrefix: "parse"
    readonly property var parseResultModel: backend && parseSessionId >= 4 ? backend.parseModel(parseSessionId) : null
    FontMetrics { id: frameMetrics; font: parseFrameText.font }
    function resetSelection() { parseFrameText.deselect(); parseFrameText.cursorPosition = 0; }
    function autoFit() { parseTable.autoFit(); }
    objectName: namePrefix + "-window"
    title: parseOutputWindow.sessionName + " · 协议解析"
    width: 880; height: 620
    minimumWidth: 520; minimumHeight: 360
    color: parseOutputWindow.theme ? parseOutputWindow.theme.background : "#282c33"
    visible: false
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: parseOutputWindow.sessionName + " · 协议解析"
                color: theme ? theme.text : "#333333"
                font.bold: true
            }

            Item {
                Layout.fillWidth: true
            }

            Next.GlassButton {
                text: "关闭"
                implicitWidth: 72
                palette.button: theme ? theme.buttonBackground : "#eeeeee"
                palette.buttonText: theme ? theme.buttonText : "#333333"
                palette.mid: theme ? theme.controlBorder : "#cccccc"
                onClicked: parseOutputWindow.close()
            }
        }

        Label {
            text: "原始报文"
            color: theme ? theme.subText : "#666666"
        }
        ScrollView {
            objectName: parseOutputWindow.namePrefix + "-frame-scroll"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.ceil(frameMetrics.lineSpacing * 8) + parseFrameText.topPadding + parseFrameText.bottomPadding
            clip: true
            TextArea {
                id: parseFrameText
                objectName: parseOutputWindow.namePrefix + "-frame"
                text: parseOutputWindow.parsedHex
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                persistentSelection: true
                wrapMode: TextEdit.Wrap
                font.family: Next.PlatformFonts.monospaceFamily
                font.pixelSize: 13
                color: theme ? theme.accent : "#1976D2"
                selectionColor: theme ? theme.tableHighlight : "#446688"
                background: Rectangle { color: theme ? theme.controlBackground : "#f5f5f5"; radius: 6 }
                Accessible.name: "原始报文"
            }
        }
        Connections {
            target: parseOutputWindow.backend
            function onHighlightRange(start, end, modelId) {
                if (modelId === parseOutputWindow.parseSessionId) parseFrameText.select(start, end);
            }
        }

        Next.ParseResultTable {
            id: parseTable
            objectName: parseOutputWindow.namePrefix + "-table"
            Layout.fillWidth: true
            Layout.fillHeight: true
            backend: parseOutputWindow.backend
            theme: parseOutputWindow.theme
            resultModel: parseOutputWindow.parseResultModel
            targetModelId: parseOutputWindow.parseSessionId
            namePrefix: parseOutputWindow.namePrefix
        }
    }
}
