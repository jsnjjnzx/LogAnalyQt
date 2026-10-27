import QtQuick
import QtQuick.Controls
import LogAnalyQt.Next as Next

Item {
    id: control
    required property var backend
    required property var theme
    required property var resultModel
    required property int targetModelId
    property string namePrefix: "parse"
    property bool differenceMode: false
    property real columnRatio1: 0.34
    property real columnRatio2: 0.20
    property bool resettingModel: false
    readonly property int tableWidth: Math.max(520, width)
    readonly property int columnWidth1: Math.floor(tableWidth * columnRatio1)
    readonly property int columnWidth2: Math.floor(tableWidth * columnRatio2)
    readonly property int columnWidth3: tableWidth - columnWidth1 - columnWidth2
    readonly property int indentWidth: 16
    property alias listView: resultList
    signal fieldDoubleClicked(string name, string value)
    readonly property var emptyField: ({ name: "", value: "", description: "", descriptionSummary: "", descriptionDetail: "", indent: 0, offset: -1, length: 0, isFrameSeparator: true, isDiff: false, highlighted: false, hasChildren: false, expanded: false, treeGuides: [], hasParent: false, branchContinues: false })

    FontMetrics { id: byteMetrics; font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 13 }

    function autoFit() {
        if (!resultModel) return;
        const widths = resultModel.getColumnWidths(byteMetrics.averageCharacterWidth, indentWidth);
        columnRatio1 = Math.max(0.28, Math.min(0.40, widths[0] / tableWidth));
        columnRatio2 = Math.max(0.16, Math.min(0.26, widths[1] / tableWidth));
        if (columnRatio1 + columnRatio2 > 0.60) columnRatio2 = 0.60 - columnRatio1;
    }

    function escaped(value) {
        return String(value).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                            .replace(/\r?\n/g, "<br>");
    }

    Item {
        id: header
        width: parent.width
        height: 30
        clip: true
        Row {
            x: -resultList.contentX
            height: parent.height
            Repeater {
                model: ["字段 / 结构", "数据", "说明 / 解析结果"]
                Rectangle {
                    required property int index
                    required property string modelData
                    width: index === 0 ? control.columnWidth1 : index === 1 ? control.columnWidth2 : control.columnWidth3
                    height: header.height
                    color: control.theme.controlBackground
                    Text {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                        verticalAlignment: Text.AlignVCenter
                        text: modelData; textFormat: Text.PlainText
                        font.pixelSize: 13; font.bold: true
                        color: control.theme.text; elide: Text.ElideRight
                    }
                    Rectangle { height: 1; width: parent.width; anchors.bottom: parent.bottom; color: control.theme.groupBorder }
                    Rectangle {
                        anchors.right: parent.right; height: parent.height; width: 4
                        color: "transparent"
                        visible: index < 2
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SplitHCursor
                            property real lastX: 0
                            onPressed: mouse => { lastX = mapToItem(header, mouse.x, mouse.y).x; }
                            onPositionChanged: mouse => {
                                if (!pressed) return;
                                const x = mapToItem(header, mouse.x, mouse.y).x;
                                const delta = (x - lastX) / control.tableWidth;
                                const min = 64 / control.tableWidth;
                                if (index === 0 && control.columnRatio1 + delta >= min && control.columnRatio2 - delta >= min) {
                                    control.columnRatio1 += delta;
                                    control.columnRatio2 -= delta;
                                    lastX = x;
                                } else if (index === 1 && control.columnRatio2 + delta >= min && 1 - control.columnRatio1 - control.columnRatio2 - delta >= min) {
                                    control.columnRatio2 += delta;
                                    lastX = x;
                                }
                            }
                            onDoubleClicked: control.autoFit()
                        }
                    }
                }
            }
        }
    }

    ListView {
        id: resultList
        objectName: control.namePrefix + "-results"
        anchors.top: header.bottom; anchors.bottom: parent.bottom
        width: parent.width
        clip: true
        contentWidth: control.tableWidth
        flickableDirection: Flickable.HorizontalAndVerticalFlick
        currentIndex: -1
        onCountChanged: if (count === 0) Qt.callLater(function () {
            if (resultList.count === 0) resultList.currentIndex = -1;
        })
        model: control.resettingModel ? null : control.resultModel
        delegate: Rectangle {
            id: row
            required property var model
            required property int index
            // A model reset can invalidate roles before ListView disposes its old delegate.
            readonly property var field: model && model.name !== undefined ? model : control.emptyField
            objectName: control.namePrefix + "-field-" + index
            width: control.tableWidth
            readonly property bool selected: resultList.currentIndex === index
            readonly property string summary: row.field.descriptionSummary
            readonly property string detail: row.field.descriptionDetail
            readonly property bool showDetails: selected && !control.differenceMode &&
                                                (detail.length > 0 || summaryMetrics.advanceWidth > control.columnWidth3 - 16 || summary.indexOf("\n") >= 0)
            height: row.field.isFrameSeparator ? 20 : showDetails ? 48 : 26
            color: row.field.isFrameSeparator ? control.theme.tableHighlight
                   : row.field.isDiff ? Qt.rgba(control.theme.error.r, control.theme.error.g, control.theme.error.b, 0.22)
                   : selected || row.field.highlighted ? control.theme.tableHighlight
                   : row.field.hasChildren && row.field.indent === 0 ? Qt.rgba(1, 1, 1, 0.035)
                   : index % 2 ? control.theme.rowAlt : "transparent"
            Accessible.role: Accessible.ListItem
            Accessible.name: row.field.name + " " + row.field.value + " " + row.field.description
            Accessible.onPressAction: activate()
            TextMetrics { id: summaryMetrics; text: row.summary; font.pixelSize: 14 }

            function activate() {
                resultList.currentIndex = index;
                if (row.field.offset >= 0 && row.field.length > 0)
                    control.backend.handleLink("highlight:" + row.field.offset + "," + row.field.length, control.targetModelId);
            }

            MouseArea {
                id: rowMouse
                anchors.fill: parent
                visible: !row.field.isFrameSeparator
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        rowMenu.rowIndex = index;
                        rowMenu.rowName = row.field.name;
                        rowMenu.rowValue = row.field.value;
                        rowMenu.rowDescription = row.field.description;
                        rowMenu.hasChildren = row.field.hasChildren;
                        rowMenu.expanded = row.field.expanded;
                        rowMenu.popup();
                    } else row.activate();
                }
                onDoubleClicked: mouse => {
                    if (mouse.button === Qt.LeftButton) control.fieldDoubleClicked(row.field.name, row.field.value);
                }
            }
            ToolTip.visible: rowMouse.containsMouse && !rowMenu.visible
            ToolTip.delay: 800
            ToolTip.text: row.field.name + "\n" + row.field.value + "\n" + row.field.description

            Text {
                anchors.centerIn: parent
                visible: row.field.isFrameSeparator
                text: row.field.name; textFormat: Text.PlainText
                font.pixelSize: 11; color: control.theme.accent
            }
            Row {
                height: parent.height
                visible: !row.field.isFrameSeparator
                Item {
                    width: control.columnWidth1; height: parent.height; clip: true
                    Repeater {
                        model: row.visible ? row.modelGuides : []
                        Rectangle {
                            required property int modelData
                            x: modelData * control.indentWidth
                            width: 1; height: row.height; color: control.theme.groupBorder
                        }
                    }
                    Rectangle {
                        x: row.field.indent * control.indentWidth
                        width: 1
                        height: row.field.branchContinues ? parent.height : parent.height / 2
                        visible: row.field.hasParent
                        color: control.theme.groupBorder
                    }
                    Rectangle {
                        x: row.field.indent * control.indentWidth
                        y: parent.height / 2
                        width: 8; height: 1
                        visible: row.field.hasParent
                        color: control.theme.groupBorder
                    }
                    Text {
                        id: expander
                        objectName: row.objectName + "-expander"
                        x: 8 + row.field.indent * control.indentWidth
                        width: 16; height: parent.height
                        verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter
                        text: row.field.hasChildren ? row.field.expanded ? "▾" : "▸" : ""
                        font.pixelSize: 14; color: control.theme.subText
                        MouseArea {
                            anchors.fill: parent
                            enabled: row.field.hasChildren
                            cursorShape: Qt.PointingHandCursor
                            onClicked: control.resultModel.toggleExpand(index)
                        }
                    }
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 28 + row.field.indent * control.indentWidth
                        anchors.rightMargin: 6
                        verticalAlignment: Text.AlignVCenter
                        text: row.field.name.trim(); textFormat: Text.PlainText
                        font.pixelSize: 14; font.bold: row.field.hasChildren
                        color: control.theme.text; elide: Text.ElideRight
                    }
                    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: control.theme.groupBorder; opacity: 0.55 }
                }
                Item {
                    width: control.columnWidth2; height: parent.height; clip: true
                    Text {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 6
                        verticalAlignment: Text.AlignVCenter
                        text: row.field.value; textFormat: Text.PlainText
                        font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 13
                        color: control.theme.text; elide: Text.ElideRight
                    }
                    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: control.theme.groupBorder; opacity: 0.55 }
                }
                Item {
                    width: control.columnWidth3; height: parent.height; clip: true
                    Text {
                        id: summaryText
                        objectName: row.objectName + "-description"
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.StyledText
                        text: control.escaped(row.summary) + (row.detail.length ?
                              (row.showDetails ? "<br>" : " · ") + "<font color=\"" + control.theme.subText + "\">" + control.escaped(row.detail) + "</font>" : "")
                        font.pixelSize: 14
                        color: control.theme.text
                        wrapMode: row.showDetails ? Text.Wrap : Text.NoWrap
                        maximumLineCount: row.showDetails ? 2 : 1
                        elide: Text.ElideRight
                    }
                }
            }
            readonly property var modelGuides: row.field.treeGuides
            Rectangle {
                anchors.bottom: parent.bottom; height: 1; width: parent.width
                color: control.theme.groupBorder; opacity: 0.32
            }
        }
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}
    }

    Next.GlassMenu {
        id: rowMenu
        objectName: control.namePrefix + "-row-menu"
        property int rowIndex: -1
        property string rowName: ""
        property string rowValue: ""
        property string rowDescription: ""
        property bool hasChildren: false
        property bool expanded: false
        palette.window: control.theme.popupBackground
        palette.text: control.theme.text
        palette.mid: control.theme.groupBorder
        palette.highlight: control.theme.tableHighlight
        palette.highlightedText: control.theme.text
        Next.GlassMenuItem { text: "复制字段"; onTriggered: control.backend.copyToClipboard(rowMenu.rowName) }
        Next.GlassMenuItem { text: "复制数据"; onTriggered: control.backend.copyToClipboard(rowMenu.rowValue) }
        Next.GlassMenuItem { text: "复制说明"; onTriggered: control.backend.copyToClipboard(rowMenu.rowDescription) }
        Next.GlassMenuSeparator { visible: rowMenu.hasChildren }
        Next.GlassMenuItem {
            objectName: control.namePrefix + "-toggle-subtree"
            visible: rowMenu.hasChildren
            text: rowMenu.expanded ? "收起整棵子树" : "展开整棵子树"
            onTriggered: control.resultModel.toggleSubtree(rowMenu.rowIndex)
        }
    }
    Connections {
        target: control.resultModel
        function onModelAboutToBeReset() { control.resettingModel = true; }
        function onModelReset() {
            control.resettingModel = false;
            resultList.currentIndex = -1;
            rowMenu.close();
        }
    }
}
