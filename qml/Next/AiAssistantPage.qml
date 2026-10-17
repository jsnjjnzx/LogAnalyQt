import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    property string iconFont: ""
    property var backend: null
    property var controller: backend && backend.aiFraming ? backend.aiFraming : null
    signal requestSettings()
    signal framingResult(string hex, int modelId)
    property int nextTerminalId: 2
    property string activeRequestId: ""
    property var submittedTerminal: null
    property string clarificationRequestId: ""
    property string clarificationDraft: ""
    property bool parametersExpanded: false
    readonly property bool working: controller ? controller.busy : false
    readonly property bool clarifying: controller && controller.state === "clarification"
    readonly property bool showWork: mode === 0 && controller && controller.state !== "idle"
    readonly property var result: controller ? controller.result : ({})
    readonly property bool resultValid: !!(controller && controller.state === "succeeded"
        && activeRequestId && result && result.requestId === activeRequestId
        && result.requestId === controller.requestId && result.terminal
        && typeof result.frameHex === "string" && /^[0-9A-Fa-f]+$/.test(result.frameHex)
        && result.frameHex.length % 2 === 0 && typeof result.apduHex === "string"
        && /^[0-9A-Fa-f]+$/.test(result.apduHex) && result.apduHex.length % 2 === 0
        && typeof result.inputTreeText === "string" && result.inputTreeText.length > 0
        && typeof result.normalizedTreeText === "string" && result.normalizedTreeText.length > 0)

    function syncLifecycle() {
        if (controller && (controller.state === "cancelled" || controller.state === "failed")) {
            activeRequestId = ""; clarificationRequestId = ""
            return
        }
        if (controller && controller.state === "clarification") {
            if (clarificationRequestId !== controller.requestId) clarificationDraft = ""
            clarificationRequestId = controller.requestId
        }
    }
    function terminalSnapshot() {
        return {id: terminal.terminalId, name: terminal.name, address: terminal.address.trim().toUpperCase(),
            addressType: terminal.addressType, logicAddress: terminal.logicAddress, clientAddress: terminal.clientAddress}
    }
    function submitRequest() {
        notice = ""
        if (mode !== 0) { notice = "报文解释尚未实现，本轮仅提供自然语言组帧。"; return }
        if (!controller) { notice = "尚未接入组帧控制器，请在实际应用中使用。"; return }
        if (controller.settingsBusy) return
        if (clarifying) {
            if (clarificationRequestId !== controller.requestId || clarificationRequestId !== activeRequestId) {
                notice = "这条澄清已失效，请重新提交需求。"; return
            }
            if (controller.answerClarification(clarificationRequestId, clarificationDraft.trim())) clarificationDraft = ""
            return
        }
        if (!addressValid || !framingDraft.trim() || working) return
        const snapshot = terminalSnapshot()
        const accepted = controller.submit(framingDraft.trim(), snapshot)
        if (accepted) { activeRequestId = accepted; submittedTerminal = snapshot; clarificationDraft = ""; parametersExpanded = false; syncLifecycle() }
    }
    function cancelRequest() {
        if (!controller || !activeRequestId || activeRequestId !== controller.requestId
                || (!controller.busy && controller.state !== "clarification")) return
        activeRequestId = ""; clarificationRequestId = ""
        controller.cancel()
    }
    Component.onDestruction: {
        if (controller && activeRequestId && activeRequestId === controller.requestId
                && (controller.busy || controller.state === "clarification")) cancelRequest()
    }
    function warningText() {
        const entries = resultValid && result.warnings ? result.warnings : []
        const messages = []
        for (let i = 0; i < entries.length; ++i) {
            const entry = entries[i]
            if (typeof entry === "string") messages.push(entry)
            else if (entry && typeof entry.message === "string") {
                let message = entry.message
                if (typeof entry.duplicateOiCount === "number" && typeof entry.duplicateMemberCount === "number")
                    message += "（重复对象：" + entry.duplicateOiCount + "；重复属性/方法：" + entry.duplicateMemberCount + "）"
                messages.push(message)
            }
        }
        return messages.join("\n")
    }
    function copyResult() {
        if (resultValid && backend && typeof backend.copyToClipboard === "function") {
            backend.copyToClipboard(result.frameHex); notice = "完整报文已复制。"
        }
    }
    Connections {
        target: root.controller
        ignoreUnknownSignals: true
        function onStateChanged() { root.syncLifecycle() }
        function onClarificationChanged() { root.syncLifecycle() }
    }
    property int mode: 0
    property string framingDraft: ""
    property string parsingDraft: ""
    property string notice: ""
    property bool advanced: false
    readonly property bool compact: height < 700
    readonly property var terminal: terminals.get(terminalPicker.currentIndex)
    readonly property bool addressValid: terminal !== undefined && terminal !== null
        && /^[0-9a-fA-F]{2,32}$/.test(terminal.address.trim()) && terminal.address.trim().length % 2 === 0
    readonly property string draft: mode === 0 ? framingDraft : parsingDraft

    // Terminal records live only in this page session; requests receive detached snapshots.
    ListModel {
        id: terminals
        ListElement { terminalId: "terminal-1"; name: "当前终端"; address: ""; addressType: 0; logicAddress: 0; clientAddress: 0 }
    }

    function updateTerminal(key, value) {
        if (terminalPicker.currentIndex >= 0)
            terminals.setProperty(terminalPicker.currentIndex, key, value)
        notice = ""
    }
    function setDraft(value) {
        if (mode === 0) framingDraft = value
        else parsingDraft = value
        notice = ""
    }
    function useSuggestion(index) {
        if (index === 2) {
            mode = 1
        } else {
            mode = 0
            setDraft(index === 0
                ? "把当前终端的时间设置为 " + Qt.formatDate(new Date(), "yyyy-MM-dd") + " 15:00:00"
                : "读取当前终端的日期时间")
        }
        composer.forceActiveFocus()
    }
    function addTerminal() {
        terminals.append({terminalId: "terminal-" + nextTerminalId++, name: "新终端", address: "", addressType: 0, logicAddress: 0, clientAddress: 0})
        terminalPicker.currentIndex = terminals.count - 1
        terminalManager.close()
        addressField.forceActiveFocus()
    }
    function removeTerminal() {
        if (terminals.count <= 1) return
        const index = terminalPicker.currentIndex
        terminalPicker.currentIndex = 0
        terminals.remove(index)
        terminalManager.close()
    }
    onModeChanged: notice = mode === 1 ? "报文解释尚未实现，本轮仅提供自然语言组帧。" : ""

    component Glyph: Text {
        property int code: 0xF02FD
        text: String.fromCodePoint(code)
        font.family: root.iconFont
        font.pixelSize: 22
        color: root.theme.subText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        Accessible.ignored: true
    }
    component UiButton: Button {
        id: button
        property bool primary: false
        property bool selected: false
        implicitHeight: 38
        padding: 10
        font.pixelSize: 14
        hoverEnabled: true
        Accessible.name: text
        background: Rectangle {
            radius: 7
            color: button.primary ? root.theme.accentMuted : button.selected ? root.theme.tableHighlight
                : button.hovered ? root.theme.surfaceHover : root.theme.buttonBackground
            border.color: button.activeFocus || button.selected ? root.theme.accent : root.theme.controlBorder
            opacity: button.enabled ? 1 : 0.45
        }
        contentItem: Text {
            text: button.text; font: button.font
            color: button.primary ? "white" : root.theme.text
            opacity: button.enabled ? 1 : 0.5
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    component Field: TextField {
        id: field
        implicitHeight: 38
        color: root.theme.text
        placeholderTextColor: root.theme.placeholderText
        selectionColor: root.theme.tableHighlight
        font.pixelSize: 14
        leftPadding: 12
        background: Rectangle {
            radius: 6; color: root.theme.controlBackground
            border.color: field.activeFocus ? root.theme.accent : root.theme.controlBorder
        }
    }
    component Choice: ComboBox {
        id: choice
        implicitHeight: 38
        font.pixelSize: 14
        palette.text: root.theme.text
        palette.windowText: root.theme.text
        palette.base: root.theme.controlBackground
        palette.button: root.theme.buttonBackground
        palette.highlight: root.theme.tableHighlight
        background: Rectangle {
            radius: 6; color: root.theme.controlBackground
            border.color: choice.activeFocus ? root.theme.accent : root.theme.controlBorder
        }
        contentItem: Text {
            text: choice.displayText; color: root.theme.text; font: choice.font
            leftPadding: 12; rightPadding: 32; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
        indicator: Glyph { x: choice.width - 30; y: (choice.height - height) / 2; code: 0xF0140 }
        delegate: ItemDelegate {
            width: choice.width
            highlighted: choice.highlightedIndex === index
            contentItem: Text { text: choice.textAt(index); color: root.theme.text; font.pixelSize: 14; elide: Text.ElideRight }
            background: Rectangle { color: parent.highlighted ? root.theme.tableHighlight : root.theme.surface }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 14
        GlassPage {
            theme: root.theme
            Layout.preferredWidth: root.width < 1000 ? 246 : 276
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 16
                spacing: 12
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    ColumnLayout {
                        width: parent.width
                        spacing: root.compact ? 12 : 18
                        RowLayout {
                            spacing: 10
                            Glyph { code: 0xF018D; font.pixelSize: 27 }
                            Label { text: "当前终端"; color: root.theme.text; font.pixelSize: 19; font.bold: true }
                        }
                        Label { text: "先选择终端，再描述你的需求。"; color: root.theme.subText; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 8
                            Label { text: "终端"; color: root.theme.text }
                            Choice {
                                id: terminalPicker; objectName: "ai-terminal-picker"
                                Layout.fillWidth: true; model: terminals; textRole: "name"
                                onCurrentIndexChanged: root.notice = ""
                                Accessible.name: "当前终端"
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 8
                            Label { text: "终端地址"; color: root.theme.text }
                            Field {
                                id: addressField; objectName: "ai-terminal-address"
                                Layout.fillWidth: true
                                text: root.terminal ? root.terminal.address : ""
                                placeholderText: "输入终端地址"
                                font.family: Qt.platform.os === "osx" ? "Menlo" : "Consolas"
                                onTextEdited: root.updateTerminal("address", text)
                                Accessible.name: "终端地址"
                            }
                            Label {
                                Layout.fillWidth: true; wrapMode: Text.Wrap
                                text: root.mode === 1 ? "解释报文时以报文内的地址为准"
                                    : root.addressValid ? "用于 698 数据链路层组帧" : "请输入 2–32 位、偶数长度的十六进制地址"
                                color: root.mode === 0 && !root.addressValid ? root.theme.warning : root.theme.subText
                                font.pixelSize: 12
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 8
                            Label { text: "地址类型"; color: root.theme.text }
                            Choice {
                                objectName: "ai-address-type"; Layout.fillWidth: true
                                model: ["单地址", "通配地址", "组地址", "广播地址"]
                                currentIndex: root.terminal ? root.terminal.addressType : 0
                                onActivated: root.updateTerminal("addressType", currentIndex)
                                Accessible.name: "地址类型"
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "逻辑地址"; color: root.theme.text; Layout.fillWidth: true }
                            CompactSpinBox {
                                objectName: "ai-logic-address"
                                from: 0; to: 1; editable: true; Layout.preferredWidth: 128
                                value: root.terminal ? root.terminal.logicAddress : 0
                                onValueChanged: if (root.terminal && root.terminal.logicAddress !== value) root.updateTerminal("logicAddress", value)
                                Accessible.name: "逻辑地址"
                            }
                        }
                        Button {
                            objectName: "ai-advanced-toggle"
                            Layout.fillWidth: true; implicitHeight: 34
                            onClicked: root.advanced = !root.advanced
                            Accessible.name: "高级参数"; Accessible.description: root.advanced ? "已展开" : "已折叠"
                            background: Rectangle { radius: 6; color: parent.hovered ? root.theme.surfaceHover : "transparent" }
                            contentItem: RowLayout {
                                Glyph { code: root.advanced ? 0xF0140 : 0xF0142 }
                                Label { text: "高级参数"; color: root.theme.text; Layout.fillWidth: true }
                            }
                        }
                        RowLayout {
                            visible: root.advanced; Layout.fillWidth: true
                            Label { text: "客户机地址 CA"; color: root.theme.subText; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            CompactSpinBox {
                                objectName: "ai-client-address"; from: 0; to: 255; editable: true
                                value: root.terminal ? root.terminal.clientAddress : 0
                                onValueChanged: if (root.terminal && root.terminal.clientAddress !== value) root.updateTerminal("clientAddress", value)
                                Accessible.name: "客户机地址 CA"
                            }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.groupBorder }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 8
                            Label { text: "协议"; color: root.theme.text }
                            Field { text: "DL/T 698"; readOnly: true; Layout.fillWidth: true; color: root.theme.subText; Accessible.name: "协议 DL/T 698" }
                        }
                    }
                }
                Button {
                    objectName: "ai-manage-terminals"; Layout.fillWidth: true; implicitHeight: 36
                    Accessible.name: "管理终端"
                    onClicked: { terminalName.text = root.terminal.name; terminalManager.open() }
                    background: Rectangle { radius: 6; color: parent.hovered ? root.theme.surfaceHover : "transparent" }
                    contentItem: RowLayout {
                        Glyph { code: 0xF08BB; color: root.theme.accent }
                        Label { text: "管理终端"; color: root.theme.accent; Layout.fillWidth: true }
                    }
                }
            }
        }
        GlassPage {
            theme: root.theme
            Layout.fillWidth: true; Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10
                spacing: 12
                RowLayout {
                    spacing: 6
                    UiButton { objectName: "ai-framing-mode"; text: "自然语言组帧"; selected: root.mode === 0; Layout.preferredWidth: 186; implicitHeight: 46; onClicked: root.mode = 0 }
                    UiButton { objectName: "ai-parsing-mode"; text: "报文解释"; selected: root.mode === 1; Layout.preferredWidth: 176; implicitHeight: 46; onClicked: root.mode = 1 }
                    Item { Layout.fillWidth: true }
                    UiButton { objectName: "ai-open-settings"; text: "模型设置"; onClicked: root.requestSettings() }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.groupBorder }
                Item {
                    visible: !root.showWork
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 32, 480)
                        spacing: root.compact ? 8 : 12
                        Label {
                            Layout.fillWidth: true
                            text: root.mode === 0 ? "用一句话，组织一条报文" : "让报文，变成看得懂的业务"
                            color: root.theme.text; font.pixelSize: root.compact ? 23 : 26; font.bold: true
                            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                        }
                        Label {
                            Layout.fillWidth: true; Layout.bottomMargin: root.compact ? 8 : 14
                            text: root.mode === 0 ? "填写真实终端地址后，描述你要完成的操作。" : "报文解释尚未实现，请使用现有人工解析工具。"
                            color: root.theme.subText; font.pixelSize: 14
                            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                        }
                        Repeater {
                            model: [
                                {title:"设置终端时间", detail:"将时间设置为指定日期和时刻", icon:0xF0150},
                                {title:"读取终端时间", detail:"读取当前终端的日期时间", icon:0xF01DA},
                                {title:"解释一条 698 报文", detail:"分析并解释一条 698 格式报文", icon:0xF09EE}
                            ]
                            delegate: Button {
                                required property var modelData
                                required property int index
                                objectName: "ai-suggestion-" + index
                                Layout.fillWidth: true
                                implicitHeight: root.compact ? 64 : 76
                                leftPadding: 18; rightPadding: 14
                                Accessible.name: modelData.title
                                onClicked: root.useSuggestion(index)
                                background: Rectangle {
                                    radius: 8; color: parent.hovered ? root.theme.surfaceHover : "transparent"
                                    border.color: parent.activeFocus ? root.theme.accent : root.theme.groupBorder
                                }
                                contentItem: RowLayout {
                                    spacing: 18
                                    Glyph { code: modelData.icon; font.pixelSize: 30 }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 5
                                        Label { text: modelData.title; color: root.theme.text; font.pixelSize: 15; font.bold: true }
                                        Label { text: modelData.detail; color: root.theme.subText; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
                                    }
                                    Glyph { code: 0xF0142 }
                                }
                            }
                        }
                    }
                }
                ScrollView {
                    objectName: "ai-work-panel"
                    visible: root.showWork
                    Layout.fillWidth: true; Layout.fillHeight: true
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 12
                        Label { objectName: "ai-status"; textFormat: Text.PlainText; text: root.controller ? root.controller.statusText : ""; color: root.controller && root.controller.state === "failed" ? root.theme.error : root.theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Label { textFormat: Text.PlainText; visible: !!root.submittedTerminal; text: root.submittedTerminal ? "本次请求终端：" + root.submittedTerminal.name + " · " + root.submittedTerminal.address : ""; color: root.theme.subText; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Label { visible: root.working || root.clarifying; text: "本次请求使用提交时的终端信息。更换目标请取消后重新提交。"; color: root.theme.subText; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Label { objectName: "ai-clarification"; textFormat: Text.PlainText; visible: root.clarifying; text: root.controller ? root.controller.clarification : ""; color: root.theme.warning; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        UiButton { objectName: "ai-cancel"; visible: root.working || root.clarifying; text: "取消本次请求"; onClicked: root.cancelRequest() }
                        ColumnLayout {
                            objectName: "ai-result-panel"; visible: root.resultValid; Layout.fillWidth: true; spacing: 10
                            Label { text: "已构建 · 本地验证"; color: root.theme.success; font.pixelSize: 18; font.bold: true }
                            Label { objectName: "ai-result-target"; textFormat: Text.PlainText; text: root.resultValid ? "结果终端：" + root.result.terminal.name + " · " + root.result.terminal.address : ""; color: root.theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            Label { textFormat: Text.PlainText; text: root.resultValid ? (root.result.serviceName || root.result.serviceCode || "") + "\n" + (root.result.summary || "") : ""; color: root.theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            RowLayout {
                                UiButton { objectName: "ai-copy-result"; text: "复制完整报文"; enabled: root.resultValid && !!root.backend; onClicked: root.copyResult() }
                                UiButton { objectName: "ai-return-result"; text: "回填手动解析"; enabled: root.resultValid; onClicked: if (root.resultValid) root.framingResult(root.result.frameHex, 1) }
                            }
                            UiButton { objectName: "ai-parameters-toggle"; text: root.parametersExpanded ? "收起参数明细" : "查看输入参数 / 编码参数"; onClicked: root.parametersExpanded = !root.parametersExpanded }
                            ColumnLayout {
                                objectName: "ai-parameters-panel"; visible: root.parametersExpanded; Layout.fillWidth: true; spacing: 8
                                Label { text: "输入参数"; color: root.theme.subText }
                                TextArea { objectName: "ai-input-parameters"; text: root.resultValid ? root.result.inputTreeText : ""; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; color: root.theme.text; Layout.fillWidth: true; background: Rectangle { color: root.theme.controlBackground; radius: 6 } }
                                Label { text: "编码参数（本地校验后的线值）"; color: root.theme.subText }
                                TextArea { objectName: "ai-encoded-parameters"; text: root.resultValid ? root.result.normalizedTreeText : ""; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; color: root.theme.text; Layout.fillWidth: true; background: Rectangle { color: root.theme.controlBackground; radius: 6 } }
                            }
                            Label { text: "APDU"; color: root.theme.subText }
                            TextArea { objectName: "ai-result-apdu"; text: root.resultValid ? root.result.apduHex : ""; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; color: root.theme.text; Layout.fillWidth: true; font.family: Qt.platform.os === "osx" ? "Menlo" : "Consolas"; background: Rectangle { color: root.theme.controlBackground; radius: 6 } }
                            Label { text: "完整报文"; color: root.theme.subText }
                            TextArea { objectName: "ai-result-frame"; text: root.resultValid ? root.result.frameHex : ""; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; color: root.theme.text; Layout.fillWidth: true; font.family: Qt.platform.os === "osx" ? "Menlo" : "Consolas"; background: Rectangle { color: root.theme.controlBackground; radius: 6 } }
                            Label { objectName: "ai-result-warnings"; textFormat: Text.PlainText; text: root.warningText(); visible: text.length > 0; color: root.theme.warning; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            Label { text: "尚未发送到设备。"; color: root.theme.subText }
                        }
                    }
                }
                Label {
                    objectName: "ai-preview-notice"
                    textFormat: Text.PlainText
                    visible: text.length > 0; text: root.notice
                    color: root.theme.warning; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 13
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: root.compact ? 146 : 160
                    radius: 9; color: "transparent"; border.color: root.theme.groupBorder
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 6
                        ScrollView {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            clip: true
                            TextArea {
                                id: composer; objectName: "ai-composer"
                                text: root.mode === 0 && root.clarifying ? root.clarificationDraft : root.draft
                                onTextChanged: { if (root.mode === 0 && root.clarifying) { if (text !== root.clarificationDraft) root.clarificationDraft = text } else if (text !== root.draft) root.setDraft(text) }
                                placeholderText: root.mode === 0 ? (root.clarifying ? "补充本次需求缺少的信息…" : "例如：读取当前终端的日期时间") : "报文解释尚未实现"
                                color: root.theme.text; placeholderTextColor: root.theme.placeholderText
                                selectionColor: root.theme.tableHighlight
                                font.pixelSize: 14; padding: 12
                                wrapMode: TextEdit.Wrap
                                Accessible.name: root.mode === 0 ? "组帧需求" : "待解释的报文"
                                background: Rectangle { radius: 6; color: root.theme.controlBackground; border.color: composer.activeFocus ? root.theme.accent : root.theme.controlBorder }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Label {
                                text: root.mode === 0 ? "当前终端：" + (root.terminal ? root.terminal.name : "") : "终端地址从报文中读取"
                                Layout.fillWidth: true; elide: Text.ElideRight; color: root.theme.subText; font.pixelSize: 12
                            }
                            Label { text: composer.length + " 字"; color: root.theme.subText; font.pixelSize: 12 }
                            UiButton {
                                objectName: "ai-preview-submit"
                                text: root.mode === 0 ? (root.clarifying ? "补充并继续" : "生成报文") : "暂未实现"
                                primary: true; Layout.preferredWidth: 128
                                enabled: root.mode === 0 && !!root.controller && !root.working && !root.controller.connectionBusy && !root.controller.settingsBusy && root.controller.hasApiKey && root.controller.credentialAvailable && (root.clarifying ? root.clarificationDraft.trim().length > 0 : root.addressValid && root.framingDraft.trim().length > 0)
                                onClicked: root.submitRequest()
                            }
                        }
                    }
                }
                RowLayout {
                    spacing: 8
                    Glyph { code: 0xF02FD; font.pixelSize: 19 }
                    Label { objectName: "ai-credential-status"; textFormat: Text.PlainText; text: root.controller ? (root.controller.credentialStatus !== "ready" ? (root.controller.credentialMessage || "安全凭据状态尚未确认") : root.controller.hasApiKey ? "模型：" + root.controller.model : "请先在模型设置中保存接入信息") : "静态预览 · 尚未接入组帧控制器"; color: root.theme.subText; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                }
            }
        }
    }

    Dialog {
        id: terminalManager
        objectName: "ai-terminal-manager"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 400; modal: true; title: "管理当前终端"
        palette.window: root.theme.background; palette.windowText: root.theme.text
        palette.text: root.theme.text; palette.button: root.theme.buttonBackground; palette.buttonText: root.theme.text
        background: Rectangle { radius: 12; color: root.theme.background; border.color: root.theme.controlBorder }
        contentItem: ColumnLayout {
            spacing: 14
            Label { text: "终端名称"; color: root.theme.text }
            Field { id: terminalName; objectName: "ai-terminal-name"; Layout.fillWidth: true; Accessible.name: "终端名称" }
            Label { text: "终端资料仅在本次打开期间保留。\n新增终端后，请填写它的地址。"; color: root.theme.subText; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 13 }
            RowLayout {
                UiButton { objectName: "ai-add-terminal"; text: "新增终端"; onClicked: root.addTerminal() }
                UiButton { objectName: "ai-remove-terminal"; text: "删除当前终端"; enabled: terminals.count > 1; onClicked: root.removeTerminal() }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                UiButton { text: "取消"; onClicked: terminalManager.close() }
                UiButton { objectName: "ai-save-terminal"; text: "确定"; primary: true; enabled: terminalName.text.trim().length > 0; onClicked: { root.updateTerminal("name", terminalName.text.trim()); terminalManager.close() } }
            }
        }
    }
}
