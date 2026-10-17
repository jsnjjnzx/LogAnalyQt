import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt

Item {
    id: root
    property var theme
    property var backend
    property string selectedKind: "request"
    property var groups: []
    property var interfaces: []
    property var schema: null
    property int selectedGroupIndex: -1
    property int selectedInterfaceIndex: -1
    property string payloadHex: ""
    property string messageHex: ""
    property string statusText: ""
    property bool statusIsError: false
    property var verifyFields: []

    signal framingResult(string hexStr, int protocolType)
    onVisibleChanged: if (!visible) verifyPopup.close()

    function currentGroup() {
        return selectedGroupIndex >= 0 && groups.length > selectedGroupIndex ? groups[selectedGroupIndex] : null
    }

    function currentInterface() {
        return selectedInterfaceIndex >= 0 && interfaces.length > selectedInterfaceIndex ? interfaces[selectedInterfaceIndex] : null
    }

    function setStatus(message, isError) {
        statusText = message
        statusIsError = isError
    }

    function clearGeneratedState() {
        payloadHex = ""
        messageHex = ""
        verifyFields = []
        statusText = ""
        statusIsError = false
    }

    function syncFieldEditor() {
        if (!fieldLoader.item)
            return
        fieldLoader.item.theme = root.theme
        fieldLoader.item.width = fieldLoader.width
        fieldLoader.item.schema = root.schema
    }

    function reload() {
        groups = backend ? backend.listAppInterfaceGroups() : []
        selectedGroupIndex = groups.length > 0 ? 0 : -1
        reloadInterfaces()
    }

    function reloadInterfaces() {
        var group = currentGroup()
        interfaces = group && backend ? backend.listAppInterfaces(group.iid, root.selectedKind, searchField.text) : []
        selectedInterfaceIndex = interfaces.length > 0 ? 0 : -1
        reloadSchema()
    }

    function reloadSchema() {
        var group = currentGroup()
        var iface = currentInterface()
        schema = group && iface && backend ? backend.getAppInterfaceSchema(group.iid, iface.iop, root.selectedKind) : null
        clearGeneratedState()
        Qt.callLater(syncFieldEditor)
    }

    function treeValue() {
        return fieldLoader.item && fieldLoader.item.getValue ? fieldLoader.item.getValue() : null
    }

    function buildPayload() {
        var group = currentGroup()
        var iface = currentInterface()
        if (!group || !iface) {
            setStatus("请先选择 APP 大项和命令", true)
            return
        }
        var hex = backend.buildAppPayloadFromTree(group.iid, iface.iop, root.selectedKind, treeValue())
        if (hex.startsWith("ERR:")) {
            setStatus(hex, true)
            return
        }
        payloadHex = hex
        messageHex = ""
        setStatus(hex.length === 0 ? "Payload 为空，适用于 NULL 消息体" : "APP Payload 组帧成功", false)
    }

    function buildMessage() {
        var group = currentGroup()
        var iface = currentInterface()
        if (!group || !iface) {
            setStatus("请先选择 APP 大项和命令", true)
            return
        }
        var hex = backend.buildAppMessageFromTree(group.iid, iface.iop, root.selectedKind, treeValue(), headerPanel.getOptions())
        if (hex.startsWith("ERR:")) {
            setStatus(hex, true)
            return
        }
        messageHex = hex
        setStatus("完整 APP 消息组帧成功", false)
    }

    function verifyCurrent() {
        if (!messageHex) {
            setStatus("请先生成完整 APP 消息", true)
            return
        }
        verifyFields = backend.verifySmartFrame("app", messageHex)
        if (!verifyFields || verifyFields.length === 0) {
            setStatus("APP 解析校验未返回结果", true)
            return
        }
        verifyPopup.open()
        setStatus("APP 解析校验完成", false)
    }

    Component.onCompleted: reload()
    onBackendChanged: reload()
    onThemeChanged: Qt.callLater(syncFieldEditor)
    onSchemaChanged: Qt.callLater(syncFieldEditor)
    onSelectedKindChanged: root.reloadInterfaces()

    component SmallButton: Next.GlassButton {
        id: button
        implicitHeight: 30
        contentItem: Text { text: button.text; color: root.theme ? root.theme.buttonText : "#333"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Next.GlassTextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: "搜索 IOP / 名称 / 描述"
                color: root.theme ? root.theme.text : "#333"
                placeholderTextColor: root.theme ? root.theme.placeholderText : "#888"

                onTextChanged: root.reloadInterfaces()
            }
            SmallButton { text: "生成 Payload"; onClicked: root.buildPayload() }
            SmallButton { text: "生成完整消息"; onClicked: root.buildMessage() }
            SmallButton { text: "解析校验"; enabled: root.messageHex.length > 0; onClicked: root.verifyCurrent() }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 8
                implicitHeight: 8
                color: SplitHandle.pressed ? (root.theme ? root.theme.buttonPressed : "#cccccc") : (SplitHandle.hovered ? (root.theme ? root.theme.buttonHover : "#dddddd") : "transparent")
            }

            Rectangle {
                SplitView.preferredWidth: 280
                SplitView.minimumWidth: 230
                color: root.theme ? root.theme.controlBackground : "#fff"
                border.color: root.theme ? root.theme.controlBorder : "#ccc"
                border.width: 1
                radius: 8

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        color: root.theme ? root.theme.background : "#f7f7f7"
                        radius: 8
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: root.theme ? root.theme.controlBorder : "#ddd" }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            Label { text: "APP 大项"; color: root.theme ? root.theme.text : "#333"; font.bold: true }
                            Item { Layout.fillWidth: true }
                            Label { text: root.groups.length + " 组"; color: root.theme ? root.theme.subText : "#666"; font.pixelSize: 11 }
                        }
                    }
                    ListView {
                        id: groupList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: 8
                        clip: true
                        model: root.groups
                        currentIndex: root.selectedGroupIndex
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            width: groupList.width
                            height: 42
                            color: index === root.selectedGroupIndex ? (root.theme ? root.theme.accentMuted : "#1976D2") : (mouseArea.containsMouse ? (root.theme ? root.theme.surfaceHover : "#eee") : "transparent")
                            radius: 8
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 0
                                Label { text: modelData.iidHex + "  " + modelData.name; color: index === root.selectedGroupIndex ? "#fff" : (root.theme ? root.theme.text : "#333"); elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 12 }
                                Label { text: modelData.count + " 个接口"; color: index === root.selectedGroupIndex ? "#fff" : (root.theme ? root.theme.subText : "#666"); font.pixelSize: 11 }
                            }
                            MouseArea { id: mouseArea; anchors.fill: parent; hoverEnabled: true; onClicked: { root.selectedGroupIndex = index; root.reloadInterfaces() } }
                        }
                    }
                }
            }

            Rectangle {
                SplitView.preferredWidth: 310
                SplitView.minimumWidth: 260
                color: root.theme ? root.theme.controlBackground : "#fff"
                border.color: root.theme ? root.theme.controlBorder : "#ccc"
                border.width: 1
                radius: 8

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        color: root.theme ? root.theme.background : "#f7f7f7"
                        radius: 8
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: root.theme ? root.theme.controlBorder : "#ddd" }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            Label { text: root.currentGroup() ? root.currentGroup().name : "命令列表"; color: root.theme ? root.theme.text : "#333"; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label { text: root.interfaces.length + " 项"; color: root.theme ? root.theme.subText : "#666"; font.pixelSize: 11 }
                        }
                    }
                    ListView {
                        id: interfaceList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: 8
                        clip: true
                        model: root.interfaces
                        currentIndex: root.selectedInterfaceIndex
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            width: interfaceList.width
                            height: 48
                            color: index === root.selectedInterfaceIndex ? (root.theme ? root.theme.accentMuted : "#1976D2") : (ifaceMouse.containsMouse ? (root.theme ? root.theme.surfaceHover : "#eee") : "transparent")
                            radius: 8
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 0
                                Label { text: modelData.iopHex + "  " + modelData.name; color: index === root.selectedInterfaceIndex ? "#fff" : (root.theme ? root.theme.text : "#333"); elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 12 }
                                Label { text: modelData.description || ""; color: index === root.selectedInterfaceIndex ? "#fff" : (root.theme ? root.theme.subText : "#666"); elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 11 }
                            }
                            MouseArea { id: ifaceMouse; anchors.fill: parent; hoverEnabled: true; onClicked: { root.selectedInterfaceIndex = index; root.reloadSchema() } }
                        }
                    }
                }
            }

            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 430
                color: root.theme ? root.theme.controlBackground : "#fff"
                border.color: root.theme ? root.theme.controlBorder : "#ccc"
                border.width: 1
                radius: 8

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        color: root.theme ? root.theme.background : "#f7f7f7"
                        radius: 8
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: root.theme ? root.theme.controlBorder : "#ddd" }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            Label { text: "消息头与字段"; color: root.theme ? root.theme.text : "#333"; font.bold: true }
                            Item { Layout.fillWidth: true }
                            Label { text: root.currentInterface() ? root.currentInterface().iopHex + "  " + root.currentInterface().name : "请选择 APP 命令"; color: root.theme ? root.theme.subText : "#666"; font.pixelSize: 11; elide: Text.ElideRight; Layout.preferredWidth: 260 }
                        }
                    }

                    SplitView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: 8
                        orientation: Qt.Vertical

                        handle: Rectangle {
                            implicitWidth: 8
                            implicitHeight: 8
                            color: SplitHandle.pressed ? (root.theme ? root.theme.buttonPressed : "#cccccc") : (SplitHandle.hovered ? (root.theme ? root.theme.buttonHover : "#dddddd") : "transparent")
                        }

                        Flickable {
                            id: fieldFlickable
                            SplitView.fillWidth: true
                            SplitView.fillHeight: true
                            clip: true
                            property real rightInset: (fieldScrollBar.visible ? fieldScrollBar.width : 0) + 8
                            property real contentAreaWidth: Math.max(0, width - rightInset)
                            contentWidth: width
                            contentHeight: fieldColumn.implicitHeight
                            ScrollBar.vertical: ScrollBar {
                                id: fieldScrollBar
                            }

                            ColumnLayout {
                                id: fieldColumn
                                width: fieldFlickable.contentAreaWidth
                                spacing: 8
                                Next.AppMsgHeaderPanel {
                                    id: headerPanel
                                    Layout.fillWidth: true
                                    theme: root.theme
                                    kind: root.selectedKind
                                    iid: root.currentGroup() ? root.currentGroup().iid : 0
                                    iop: root.currentInterface() ? root.currentInterface().iop : 0
                                }
                                Label { text: root.currentInterface() ? root.currentInterface().name : "请选择 APP 命令"; color: root.theme ? root.theme.text : "#333"; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                Loader {
                                    id: fieldLoader
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: item ? item.implicitHeight : 0
                                    active: root.schema && root.schema.type
                                    source: Qt.resolvedUrl("FieldEditor.qml")
                                    onLoaded: root.syncFieldEditor()
                                    onWidthChanged: root.syncFieldEditor()
                                    onActiveChanged: Qt.callLater(root.syncFieldEditor)
                                }
                                Label { visible: !root.schema || !root.schema.type; text: "当前消息类型没有字段定义"; color: root.theme ? root.theme.placeholderText : "#888"; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                            }
                        }

                        ColumnLayout {
                            SplitView.preferredHeight: 220
                            SplitView.minimumHeight: 180
                            spacing: 8
                            Next.HexPreview { Layout.fillWidth: true; Layout.preferredHeight: 92; theme: root.theme; title: "APP Payload"; hexText: root.payloadHex }
                            Next.HexPreview { Layout.fillWidth: true; Layout.fillHeight: true; theme: root.theme; title: "完整 APP 消息"; hexText: root.messageHex }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: root.statusText; color: root.statusIsError ? (root.theme ? root.theme.error : "#f44336") : (root.theme ? root.theme.subText : "#666"); elide: Text.ElideRight }
            SmallButton { text: "复制 Payload"; enabled: root.payloadHex.length > 0; onClicked: { backend.copyToClipboard(root.payloadHex); root.setStatus("已复制 Payload", false) } }
            SmallButton { text: "复制完整消息"; enabled: root.messageHex.length > 0; onClicked: { backend.copyToClipboard(root.messageHex); root.setStatus("已复制完整消息", false) } }
            SmallButton { text: "回填手动解析"; enabled: root.messageHex.length > 0; onClicked: root.framingResult(root.messageHex, 4) }
        }
    }

    Popup {
        id: verifyPopup
        modal: false
        parent: Overlay.overlay
        x: Math.round((Overlay.overlay.width - width) / 2)
        y: Math.round((Overlay.overlay.height - height) / 2)
        width: 720
        height: 460
        padding: 10
        background: Rectangle { color: root.theme ? root.theme.popupBackground : "#ffffff"; border.color: root.theme ? root.theme.controlBorder : "#cccccc"; radius: 12 }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label { text: "APP 解析校验结果"; color: root.theme ? root.theme.text : "#333"; font.bold: true }
                Item { Layout.fillWidth: true }
                SmallButton { text: "关闭"; onClicked: verifyPopup.close() }
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.verifyFields
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    width: ListView.view.width
                    height: 24
                    color: index % 2 === 0 ? (root.theme ? root.theme.controlBackground : "#fff") : (root.theme ? root.theme.rowAlt : "#f9f9f9")
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6 + Math.min(modelData.indent || 0, 10) * 16
                        anchors.rightMargin: 6
                        Label { text: modelData.name || ""; color: root.theme ? root.theme.text : "#333"; Layout.preferredWidth: 170; elide: Text.ElideRight; font.pixelSize: 12 }
                        Label { text: modelData.value || ""; color: root.theme ? root.theme.accent : "#1976D2"; Layout.preferredWidth: 180; elide: Text.ElideRight; font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 12 }
                        Label { text: modelData.description || ""; color: root.theme ? root.theme.subText : "#666"; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 12 }
                    }
                }
            }
        }
    }
}
