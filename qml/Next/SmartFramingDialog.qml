import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt
import "SmartFraming" as SmartFraming

pragma ComponentBehavior: Bound

GlassPage {
    id: dialog
    visible: false
    title: "智能组帧"

    property var backend
    property string statusText: ""
    property bool statusIsError: false
    property string apduHex: ""
    property string frameHex: ""
    property var verifyFields: []
    property string appSelectedKind: appKindCombo.currentIndex === 1 ? "response" : (appKindCombo.currentIndex === 2 ? "event" : "request")

    signal framingResult(string hexStr, int protocolType)
    onVisibleChanged: if (!visible) verifyPopup.close()

    color: theme ? theme.background : "#ffffff"
    palette.highlight: theme ? theme.accentMuted : "#1976D2"



    function setStatus(message, isError) {
        statusText = message
        statusIsError = isError
    }

    function buildApdu() {
        if (!backend)
            return
        var value = serviceTree.getTreeValue()
        var hex = backend.build698ApduFromTree(serviceSelector.serviceCode, value)
        if (hex.startsWith("ERR:")) {
            setStatus(hex, true)
            return
        }
        apduHex = hex
        frameHex = ""
        setStatus("APDU 组帧成功", false)
    }

    function buildFrame() {
        if (!apduHex) {
            setStatus("请先生成 APDU", true)
            return
        }
        var hex = backend.buildFullFrame(apduHex, frameHeader.getOptions())
        if (hex.startsWith("ERR:")) {
            setStatus(hex, true)
            return
        }
        frameHex = hex
        setStatus("完整帧封装成功", false)
    }

    function verifyCurrent() {
        var target = frameHex || apduHex
        if (!target) {
            setStatus("暂无可校验数据", true)
            return
        }
        verifyFields = backend.verifyHexData(target)
        if (!verifyFields || verifyFields.length === 0) {
            setStatus("解析校验未返回结果", true)
            return
        }
        verifyPopup.open()
        setStatus("解析校验完成", false)
    }



    Item { id: titleBar; height: 0; width: parent.width }


    component StyledButton: Button {
        id: control
        implicitHeight: 36
        contentItem: Text {
            text: control.text
            font: control.font
            opacity: enabled ? 1.0 : 0.3
            color: theme ? theme.buttonText : "#333333"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            implicitWidth: 100
            implicitHeight: 36
            opacity: enabled ? 1 : 0.3
            color: control.down ? (theme ? theme.buttonPressed : "#cccccc") : (control.hovered ? (theme ? theme.buttonHover : "#dddddd") : (theme ? theme.buttonBackground : "#eeeeee"))
            border.color: theme ? theme.controlBorder : "#cccccc"
            border.width: 1
            radius: 8
        }
    }

    component StyledComboBox: ComboBox {
        id: control
        implicitHeight: 36
        textRole: "text"
        valueRole: "value"

        function itemTextAt(itemIndex) {
            if (!control.model || itemIndex < 0)
                return ""
            var item = control.model[itemIndex]
            if (item === undefined || item === null)
                return ""
            if (typeof item === "string")
                return item
            if (control.textRole && typeof item[control.textRole] !== "undefined")
                return item[control.textRole]
            if (typeof item.text !== "undefined")
                return item.text
            if (typeof item.name !== "undefined")
                return item.name
            return String(item)
        }

        indicator: Canvas {
            id: arrowCanvas
            x: control.width - width - control.rightPadding
            y: control.topPadding + (control.availableHeight - height) / 2
            width: 12
            height: 8
            contextType: "2d"

            Connections {
                target: control
                function onPressedChanged() { arrowCanvas.requestPaint() }
            }

            Connections {
                target: dialog.theme
                function onIsDarkChanged() { arrowCanvas.requestPaint() }
            }

            onPaint: {
                context.reset()
                context.moveTo(0, 0)
                context.lineTo(width, 0)
                context.lineTo(width / 2, height)
                context.closePath()
                context.fillStyle = dialog.theme ? dialog.theme.text : "#333"
                context.fill()
            }
        }

        contentItem: Text {
            leftPadding: 10
            rightPadding: control.indicator.width + control.spacing
            text: control.displayText
            color: dialog.theme ? dialog.theme.text : "#333"
            font: control.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            implicitWidth: 100
            implicitHeight: 36
            color: dialog.theme ? dialog.theme.controlBackground : "#ffffff"
            border.color: dialog.theme ? dialog.theme.controlBorder : "#cccccc"
            border.width: control.visualFocus ? 2 : 1
            radius: 8
        }

        delegate: ItemDelegate {
            id: delegateRoot
            required property int index
            width: control.width
            highlighted: control.highlightedIndex === index

            contentItem: Text {
                text: control.itemTextAt(delegateRoot.index)
                color: dialog.theme ? dialog.theme.text : "#333"
                font: control.font
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }

            background: Rectangle {
                color: delegateRoot.highlighted ? (dialog.theme ? dialog.theme.splitHandleHover : "#eeeeee") : (dialog.theme ? dialog.theme.controlBackground : "#ffffff")
            }
        }

        popup: Popup {
            y: control.height - 1
            width: control.width
            implicitHeight: Math.min(contentItem.implicitHeight, 320)
            padding: 1

            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
            }

            background: Rectangle {
                color: dialog.theme ? dialog.theme.controlBackground : "#ffffff"
                border.color: dialog.theme ? dialog.theme.controlBorder : "#cccccc"
                radius: 8
            }
        }
    }

    ColumnLayout {
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { text: "协议"; color: theme ? theme.text : "#333"; font.bold: true }
            SmartFraming.ProtocolSelector { id: protocolSelector; Layout.preferredWidth: 160; theme: dialog.theme; backend: dialog.backend }
            Label { text: "消息类型"; visible: protocolSelector.protocolCode === "app"; color: theme ? theme.text : "#333"; font.bold: true }
            StyledComboBox {
                id: appKindCombo
                visible: protocolSelector.protocolCode === "app"
                Layout.preferredWidth: 128
                currentIndex: 0
                model: [
                    { text: "请求", value: "request" },
                    { text: "响应", value: "response" },
                    { text: "事件", value: "event" }
                ]
            }
            Label { text: "APDU 服务"; visible: protocolSelector.protocolCode === "698"; color: theme ? theme.text : "#333"; font.bold: true }
            SmartFraming.ServiceSelector { id: serviceSelector; visible: protocolSelector.protocolCode === "698"; Layout.preferredWidth: 380; theme: dialog.theme; backend: dialog.backend }
            StyledButton { text: "重置当前树"; visible: protocolSelector.protocolCode === "698"; onClicked: serviceTree.reset() }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            visible: protocolSelector.protocolCode === "698"
            Layout.fillWidth: true; spacing: 10
            Item { Layout.fillWidth: true }
            StyledButton { text: "生成 APDU"; visible: protocolSelector.protocolCode === "698"; onClicked: dialog.buildApdu() }
            StyledButton { text: "封装完整帧"; visible: protocolSelector.protocolCode === "698"; enabled: dialog.apduHex.length > 0; onClicked: dialog.buildFrame() }
            StyledButton { text: "解析校验"; visible: protocolSelector.protocolCode === "698"; enabled: dialog.apduHex.length > 0 || dialog.frameHex.length > 0; onClicked: dialog.verifyCurrent() }
        }

        SmartFraming.AppMsgFramingPage {
            visible: protocolSelector.protocolCode === "app"
            Layout.fillWidth: true
            Layout.fillHeight: true
            theme: dialog.theme
            backend: dialog.backend
            selectedKind: dialog.appSelectedKind
            onFramingResult: function(hexStr, protocolType) {
                dialog.framingResult(hexStr, protocolType)
            }
        }

        SplitView {
            visible: protocolSelector.protocolCode === "698"
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 6
                implicitHeight: 6
                color: SplitHandle.pressed ? (theme ? theme.buttonPressed : "#cccccc") : (SplitHandle.hovered ? (theme ? theme.buttonHover : "#dddddd") : "transparent")
            }

            Rectangle {
                SplitView.preferredWidth: 610
                SplitView.minimumWidth: 460
                color: theme ? theme.controlBackground : "#ffffff"
                border.color: theme ? theme.controlBorder : "#cccccc"
                radius: 8

                Flickable {
                    id: serviceTreeFlickable
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    contentWidth: width
                    contentHeight: serviceTree.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.VerticalFlick

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    ScrollBar.horizontal: ScrollBar {
                        policy: ScrollBar.AlwaysOff
                    }

                    SmartFraming.ServiceTreeEditor {
                        id: serviceTree
                        width: serviceTreeFlickable.width
                        theme: dialog.theme
                        backend: dialog.backend
                        serviceCode: serviceSelector.serviceCode
                        onStatusMessage: function(message, isError) { dialog.setStatus(message, isError) }
                    }
                }
            }

            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 360
                color: theme ? theme.controlBackground : "#ffffff"
                border.color: theme ? theme.controlBorder : "#cccccc"
                radius: 8

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 10

                    SmartFraming.HexPreview {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 150
                        theme: dialog.theme
                        title: "APDU 结果"
                        hexText: dialog.apduHex
                    }

                    SmartFraming.FrameHeaderPanel {
                        id: frameHeader
                        Layout.fillWidth: true
                        theme: dialog.theme
                        defaultAddress: backend ? backend.getCurrent698Address() : ""
                        serverResponseMode: serviceSelector.serviceCode.indexOf("_RESPONSE") >= 0
                    }

                    SmartFraming.HexPreview {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        theme: dialog.theme
                        title: "完整帧结果"
                        hexText: dialog.frameHex
                    }
                }
            }
        }

        RowLayout {
            visible: protocolSelector.protocolCode === "698"
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: dialog.statusText
                color: dialog.statusIsError ? (theme ? theme.error : "#f44336") : (theme ? theme.subText : "#666")
                elide: Text.ElideRight
            }
            StyledButton { text: "复制 APDU"; enabled: apduHex.length > 0; onClicked: { backend.copyToClipboard(apduHex); setStatus("已复制 APDU", false) } }
            StyledButton { text: "复制完整帧"; enabled: frameHex.length > 0; onClicked: { backend.copyToClipboard(frameHex); setStatus("已复制完整帧", false) } }
            StyledButton { text: "回填手动解析"; enabled: frameHex.length > 0; onClicked: { dialog.framingResult(frameHex, 1); setStatus("已回填完整帧", false) } }
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
        background: Rectangle { color: theme ? theme.background : "#ffffff"; border.color: theme ? theme.controlBorder : "#cccccc"; radius: 6 }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label { text: "解析校验结果"; color: theme ? theme.text : "#333"; font.bold: true }
                Item { Layout.fillWidth: true }
                StyledButton { text: "关闭"; onClicked: verifyPopup.close() }
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: dialog.verifyFields
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    required property int index
                    required property var modelData
                    width: ListView.view.width
                    height: 24
                    color: index % 2 === 0 ? (theme ? theme.controlBackground : "#fff") : (theme ? theme.rowAlt : "#f9f9f9")
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6 + Math.min(modelData.indent || 0, 10) * 16
                        anchors.rightMargin: 6
                        Label { text: modelData.name || ""; color: theme ? theme.text : "#333"; Layout.preferredWidth: 170; elide: Text.ElideRight; font.pixelSize: 12 }
                        Label { text: modelData.value || ""; color: theme ? theme.accent : "#1976D2"; Layout.preferredWidth: 180; elide: Text.ElideRight; font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 12 }
                        Label { text: modelData.description || ""; color: theme ? theme.subText : "#666"; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 12 }
                    }
                }
            }
        }
    }
}
