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
    FontMetrics { id: frameMetrics; font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 13 }
    function resetSelection() { parseFrameText.deselect(); parseFrameText.cursorPosition = 0; }
    function autoFit() { popupCols.autoFit(); }
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
            Layout.fillWidth: true
            Layout.preferredHeight: 84
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

        Item {
            id: parseHeaderContainer
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            clip: true

            Row {
                x: -parseResultList.contentX
                height: parent.height

                Rectangle {
                    width: popupCols.w1
                    height: parent.height
                    color: theme ? theme.controlBackground : "#f5f5f5"
                    border.color: theme ? theme.controlBorder : "#cccccc"
                    Text {
                        anchors.centerIn: parent
                        text: "域/位域"
                        font.bold: true
                        color: theme ? theme.text : "#333333"
                    }

                    Rectangle {
                        width: 4
                        height: parent.height
                        anchors.right: parent.right
                        color: Qt.rgba(0, 0, 0, 0)

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SplitHCursor
                            property point lastPos

                            onPressed: mouse => {
                                lastPos = mapToItem(parseHeaderContainer, mouse.x, mouse.y);
                            }

                            onPositionChanged: mouse => {
                                if (pressed) {
                                    var curPos = mapToItem(parseHeaderContainer, mouse.x, mouse.y);
                                    var delta = curPos.x - lastPos.x;
                                    var deltaR = delta / popupCols.availableW;
                                    var minR = 50 / popupCols.availableW;

                                    if (popupCols.r1 + deltaR > minR && popupCols.r2 - deltaR > minR) {
                                        popupCols.r1 += deltaR;
                                        popupCols.r2 -= deltaR;
                                        lastPos = curPos;
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: popupCols.w2
                    height: parent.height
                    color: theme ? theme.controlBackground : "#f5f5f5"
                    border.color: theme ? theme.controlBorder : "#cccccc"
                    Text {
                        anchors.centerIn: parent
                        text: "值"
                        font.bold: true
                        color: theme ? theme.text : "#333333"
                    }

                    Rectangle {
                        width: 4
                        height: parent.height
                        anchors.right: parent.right
                        color: Qt.rgba(0, 0, 0, 0)

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SplitHCursor
                            property point lastPos

                            onPressed: mouse => {
                                lastPos = mapToItem(parseHeaderContainer, mouse.x, mouse.y);
                            }

                            onPositionChanged: mouse => {
                                if (pressed) {
                                    var curPos = mapToItem(parseHeaderContainer, mouse.x, mouse.y);
                                    var delta = curPos.x - lastPos.x;
                                    var deltaR = delta / popupCols.availableW;
                                    var minR = 50 / popupCols.availableW;

                                    if (popupCols.r2 + deltaR > minR && (1 - popupCols.r1 - popupCols.r2) - deltaR > minR) {
                                        popupCols.r2 += deltaR;
                                        lastPos = curPos;
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: popupCols.w3
                    height: parent.height
                    color: theme ? theme.controlBackground : "#f5f5f5"
                    border.color: theme ? theme.controlBorder : "#cccccc"
                    Text {
                        anchors.centerIn: parent
                        text: "说明"
                        font.bold: true
                        color: theme ? theme.text : "#333333"
                    }
                }
            }
        }

        ListView {
            id: parseResultList
            objectName: parseOutputWindow.namePrefix + "-results"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: popupCols.total
            flickableDirection: Flickable.HorizontalAndVerticalFlick
            model: parseOutputWindow.parseResultModel

            delegate: Rectangle {
                objectName: parseOutputWindow.namePrefix + "-field-" + index
                width: popupCols.total
                height: model.isFrameSeparator ? 20 : 30
                color: model.isFrameSeparator ? (theme ? theme.tableHighlight : "#eeeeee") : (model.highlighted ? (theme ? theme.tableHighlight : "#d0e8ff") : (index % 2 === 0 ? Qt.rgba(0, 0, 0, 0) : (theme ? theme.rowAlt : "#fafafa")))

                MouseArea {
                    anchors.fill: parent
                    visible: !model.isFrameSeparator
                    onClicked: {
                        var link = "highlight:" + model.offset + "," + model.length;
                        backend.handleLink(link, parseOutputWindow.parseSessionId);
                    }
                }

                Row {
                    height: parent.height
                    visible: !model.isFrameSeparator

                    Item {
                        width: popupCols.w1
                        height: parent.height

                        Text {
                            x: 5 + (model.indent * 20)
                            width: 16
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: Text.AlignHCenter
                            text: model.hasChildren ? (model.expanded ? "▼" : "▶") : ""
                            color: theme ? theme.subText : "#666666"
                            font.pixelSize: 10

                            MouseArea {
                                anchors.fill: parent
                                enabled: model.hasChildren
                                cursorShape: model.hasChildren ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: parseOutputWindow.parseResultModel.toggleExpand(index)
                            }
                        }

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 25 + (model.indent * 20)
                            verticalAlignment: Text.AlignVCenter
                            text: model.name
                            font.family: Next.PlatformFonts.monospaceFamily
                            color: theme ? theme.text : "#333333"
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            width: 1
                            height: parent.height
                            color: theme ? theme.controlBorder : "#cccccc"
                            anchors.right: parent.right
                        }
                    }

                    Item {
                        width: popupCols.w2
                        height: parent.height

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 5
                            verticalAlignment: Text.AlignVCenter
                            text: model.value
                            font.family: Next.PlatformFonts.monospaceFamily
                            color: theme ? theme.text : "#333333"
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            width: 1
                            height: parent.height
                            color: theme ? theme.controlBorder : "#cccccc"
                            anchors.right: parent.right
                        }
                    }

                    Item {
                        width: popupCols.w3
                        height: parent.height

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 5
                            verticalAlignment: Text.AlignVCenter
                            text: model.description
                            font.family: Next.PlatformFonts.monospaceFamily
                            color: theme ? theme.text : "#333333"
                            elide: Text.ElideRight
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: theme ? theme.controlBorder : "#cccccc"
                    anchors.bottom: parent.bottom
                }
            }

            ScrollBar.vertical: ScrollBar {}
            ScrollBar.horizontal: ScrollBar {}
        }
    }

    QtObject {
        id: popupCols
        property real r1: 0.4
        property real r2: 0.3
        property real minTotal: 400
        property real availableW: Math.max(parseHeaderContainer.width, minTotal)
        property int w1: Math.floor(availableW * r1)
        property int w2: Math.floor(availableW * r2)
        property int w3: availableW - w1 - w2
        property int total: availableW

        function autoFit() {
            if (parseOutputWindow.parseResultModel && parseOutputWindow.parseResultModel.getColumnWidths) {
                var widths = parseOutputWindow.parseResultModel.getColumnWidths(frameMetrics.averageCharacterWidth, 20);
                if (widths.length >= 2) {
                    var w1 = widths[0];
                    var w2 = widths[1];
                    var total = popupCols.availableW;
                    if (total <= 0)
                        return;

                    var newR1 = w1 / total;
                    var newR2 = w2 / total;

                    if (newR1 < 0.1)
                        newR1 = 0.1;
                    if (newR1 > 0.6)
                        newR1 = 0.6;
                    if (newR2 < 0.1)
                        newR2 = 0.1;
                    if (newR2 > 0.8)
                        newR2 = 0.8;

                    if (newR1 + newR2 > 0.9) {
                        var factor = 0.9 / (newR1 + newR2);
                        newR1 *= factor;
                        newR2 *= factor;
                    }

                    popupCols.r1 = newR1;
                    popupCols.r2 = newR2;
                }
            }
        }
    }
}
