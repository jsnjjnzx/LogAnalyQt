import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt

GlassPage {
    id: liveLogDialog
    visible: false
    title: "实时终端监控"
    color: theme ? theme.surface : "#ffffff"
    palette.highlight: theme ? theme.accentMuted : "#1976D2"

    property var backend
    property int sessionId: 0
    property string sessionName: "监控"
    property bool persistConnectionSettings: true
    property bool workspaceManaged: false
    property bool relocatingWindow: false
    property bool useSystemCredentials: false
    property var sharedCommands: null
    property var monitorRuleLibrary: null
    property bool monitorEnabled: false
    property var monitorRuleIds: []
    property var monitorHit: null
    property var monitorNotice: null
    readonly property var monitorEvent: monitorHit || monitorNotice
    readonly property color monitorHitColor: monitorHit && monitorHit.color ? monitorHit.color : "#e8b76a"
    property string monitorError: ""
    readonly property var selectedMonitorRules: monitorRuleLibrary
        ? monitorRuleLibrary.monitorRules.filter(function(rule) { return monitorRuleIds.indexOf(rule.id) >= 0; }) : []
    readonly property string monitorConfiguration: JSON.stringify([selectedMonitorRules, monitorEnabled, autoScroll])
    onMonitorConfigurationChanged: { monitorError = ""; configureMonitorRules(); }
    function configureMonitorRules() {
        if (!sshTerminal) return;
        if (!sshTerminal.setMonitorRules(selectedMonitorRules, monitorEnabled && autoScroll && monitorError === "")) {
            sshTerminal.setMonitorRules([], false);
            monitorError = "规则无效，请在规则库中检查表达式。";
        }
    }
    function toggleMonitorRule(id, checked) {
        monitorRuleIds = checked ? monitorRuleIds.indexOf(id) >= 0 ? monitorRuleIds : monitorRuleIds.concat([id])
            : monitorRuleIds.filter(function(value) { return value !== id; });
    }
    function resumeFollowing() {
        monitorHit = null; monitorNotice = null; monitorError = ""; autoScroll = true;
        configureMonitorRules();
        logListView.positionViewAtEnd();
    }
    function positionMonitorHit() {
        if (!monitorHit || autoScroll) return;
        const row = sshTerminal.indexForLineId(monitorHit.lineId);
        if (row >= 0) {
            logListView.positionViewAtIndex(row, ListView.Center);
            const item = logListView.itemAtIndex(row);
            viewAnchorId = monitorHit.lineId;
            viewAnchorOffset = item ? item.y - logListView.contentY : 0;
        } else if (monitorHit.lineId === liveSelectionId) logListView.positionViewAtEnd();
    }
    readonly property alias terminal: sshTerminal
    readonly property string configurationSnapshot: JSON.stringify(connectionConfiguration())
    signal configurationEdited()
    onConfigurationSnapshotChanged: configurationEdited()
    function connectionConfiguration() {
        return {name: sessionName, host: hostInput.text, port: portInput.text, username: userInput.text,
            password: secureCredentials ? "" : pwdInput.text,
            encoding: encodingCombo.currentText, maxLines: maxLinesInput.text,
            autoReconnect: sshTerminal.autoReconnectEnabled, reconnectCount: reconnectCountInput.text,
            reconnectInterval: reconnectIntervalInput.text, expanded: connectionConfigExpanded,
            commandManagementExpanded: commandManagementExpanded, copyOnSelect: copyOnSelect,
            monitorEnabled: monitorEnabled, monitorRuleIds: monitorRuleIds};
    }
    function restoreConfiguration(config) {
        sessionName = config.name;
        hostInput.text = config.host; portInput.text = config.port; userInput.text = config.username;
        encodingCombo.currentIndex = config.encoding === "GB2312" ? 1 : 0;
        sshTerminal.encoding = encodingCombo.currentText;
        maxLinesInput.text = config.maxLines;
        sshTerminal.autoReconnectEnabled = config.autoReconnect;
        reconnectCountInput.text = config.reconnectCount; reconnectIntervalInput.text = config.reconnectInterval;
        connectionConfigExpanded = config.expanded; commandManagementExpanded = config.commandManagementExpanded;
        copyOnSelect = config.copyOnSelect === true;
        monitorRuleIds = config.monitorRuleIds || [];
        monitorEnabled = config.monitorEnabled === true;
        sshTerminal.host = config.host; sshTerminal.username = config.username;
        if (portInput.acceptableInput) sshTerminal.port = Number(config.port);
        pwdInput.text = config.password || "";
        sshTerminal.password = pwdInput.text;
    }
    readonly property alias terminalInput: terminalCapture
    property bool connectionConfigExpanded: true
    property bool commandManagementExpanded: false
    readonly property bool secureCredentials: sshTerminal.usesSecureCredentials
    property var pendingSshAction: null
    property string passwordOperationId: ""
    property var passwordOperationTarget: null
    property string passwordNotice: ""
    property bool passwordNoticeError: false
    property var pendingHostKey: null
    readonly property bool sessionActive: sshTerminal.connected || sshTerminal.connectionState === SshTerminal.Connecting || sshTerminal.connectionState === SshTerminal.Reconnecting || sshTerminal.hostKeyPending
    function currentTarget() { return {host: hostInput.text.trim(), port: Number(portInput.text), username: userInput.text.trim()} }
    function sameTarget(a, b) { return !!a && !!b && a.host === b.host && a.port === b.port && a.username === b.username }
    function targetMatchesBackend() { return sameTarget(currentTarget(), {host: sshTerminal.host, port: sshTerminal.port, username: sshTerminal.username}) }
    function targetEdited() {
        if (!secureCredentials) return
        pwdInput.text = ""; passwordNotice = ""; pendingSshAction = null
        if (pendingHostKey) respondHostKey(false)
    }
    function beginSshAction(kind) {
        if (!secureCredentials || pendingSshAction || passwordOperationId || sessionActive) return
        const target = currentTarget()
        if (!target.host || !target.username || !portInput.acceptableInput || target.port < 1 || target.port > 65535) {
            passwordNotice = "请填写有效的主机、端口和用户名。"; passwordNoticeError = true; return
        }
        if (kind === "save" && !pwdInput.text) { passwordNotice = "请输入要保存的新密码。"; passwordNoticeError = true; return }
        const draft = pwdInput.text
        sshTerminal.host = target.host; sshTerminal.port = target.port; sshTerminal.username = target.username
        pendingSshAction = {kind: kind, target: target, password: draft}
        passwordNotice = sshTerminal.credentialBusy ? "正在核验目标凭据，请等待…" : ""; passwordNoticeError = false
        continueSshAction()
    }
    function continueSshAction() {
        if (!pendingSshAction || sshTerminal.credentialBusy) return
        const action = pendingSshAction
        pendingSshAction = null
        if (!sameTarget(action.target, currentTarget()) || !targetMatchesBackend()) return
        if (action.kind === "connect") {
            sshTerminal.password = action.password
            sshTerminal.maxBufferLines = parseInt(maxLinesInput.text) || 10000
            sshTerminal.reconnectMaxAttempts = parseInt(reconnectCountInput.text) || 0
            sshTerminal.reconnectIntervalMs = parseInt(reconnectIntervalInput.text) || 2000
            sshTerminal.connectToHost()
            if (sshTerminal.connectionState === SshTerminal.Connecting || sshTerminal.connected) pwdInput.text = ""
        } else {
            const id = action.kind === "save" ? sshTerminal.savePassword(action.password) : sshTerminal.clearSavedPassword()
            if (id) {
                passwordOperationId = id; passwordOperationTarget = action.target
                passwordNotice = action.kind === "save" ? "密码保存处理中，尚未确认完成…" : "密码清除处理中，尚未确认完成…"
            } else { passwordNotice = sshTerminal.credentialError || "凭据操作未受理。"; passwordNoticeError = true }
        }
    }
    function connectAction() {
        if (pendingSshAction || sessionActive) {
            pendingSshAction = null
            if (pendingHostKey) respondHostKey(false)
            sshTerminal.disconnectFromHost()
            return
        }
        if (!secureCredentials) {
            if (!hostInput.text.trim() || !userInput.text.trim() || !portInput.acceptableInput || !pwdInput.text) {
                passwordNotice = "请填写有效的主机、端口、账号和密码。"; passwordNoticeError = true; return;
            }
            passwordNotice = "";
            const draft = pwdInput.text;
            sshTerminal.host = hostInput.text.trim(); sshTerminal.port = Number(portInput.text)
            sshTerminal.username = userInput.text.trim(); sshTerminal.password = draft
            pwdInput.text = draft;
            sshTerminal.maxBufferLines = parseInt(maxLinesInput.text) || 10000
            sshTerminal.reconnectMaxAttempts = parseInt(reconnectCountInput.text) || 0
            sshTerminal.reconnectIntervalMs = parseInt(reconnectIntervalInput.text) || 2000
            sshTerminal.connectToHost()
            return
        }
        beginSshAction("connect")
    }
    function respondHostKey(accept) {
        const key = pendingHostKey
        pendingHostKey = null; hostKeyDialog.close()
        if (key) sshTerminal.confirmHostKey(key.requestId, key.fingerprint, accept)
    }
    function closeSshSession() {
        pendingSshAction = null; passwordOperationId = ""; passwordOperationTarget = null
        if (pendingHostKey) respondHostKey(false)
        if (secureCredentials) pwdInput.text = ""
        sshTerminal.disconnectFromHost()
    }
    Component.onDestruction: {
        closeSshSession();
        if (backend && parseSessionId >= 4) backend.closeParseSession(parseSessionId);
    }


    property bool autoScroll: true
    property bool copyOnSelect: false
    property var textSelectionAnchor: null
    property var textSelectionFocus: null
    property bool selectionPointerDown: false
    readonly property real liveSelectionId: sshTerminal.firstLineId + sshTerminal.currentLineCount
    readonly property var textSelectionRange: {
        if (!textSelectionAnchor || !textSelectionFocus) return null;
        const a = textSelectionAnchor, b = textSelectionFocus;
        return a.id < b.id || a.id === b.id && a.position <= b.position
            ? {firstId: a.id, firstOffset: a.position, lastId: b.id, lastOffset: b.position}
            : {firstId: b.id, firstOffset: b.position, lastId: a.id, lastOffset: a.position};
    }
    readonly property bool hasTextSelection: textSelectionRange !== null
        && (textSelectionRange.firstId !== textSelectionRange.lastId || textSelectionRange.firstOffset !== textSelectionRange.lastOffset)
    property int terminalFontSize: 13
    property int selectedLineIndex: -1
    property var localCustomCommands: []
    property var customCommands: sharedCommands ? sharedCommands.commands : localCustomCommands
    property bool customCommandsLoaded: false
    property string customCmdPendingDeleteName: ""

    property bool findBarVisible: false
    property string searchKeyword: ""
    // 注意：searchMatches 赋值时必须整体替换新数组，不得直接 push()，
    // 否则 QML 属性系统检测不到变化，UI 绑定不会刷新。（#2）
    property var searchMatches: []
    property int searchCursor: -1
    property real m_nextSearchLineId: -1
    property real selectedLineId: -1
    property real viewAnchorId: -1
    property real viewAnchorOffset: 0
    property bool viewUpdatePending: false
    onAutoScrollChanged: if (autoScroll) { viewAnchorId = -1; monitorHit = null; monitorNotice = null; }
    property int selectedHexStart: -1
    property int selectedHexEnd: -1
    property string selectedHexText: ""
    readonly property string parseableHexText: {
        if (!hasTextSelection) return validatedHex(selectedHexText);
        const range = textSelectionRange;
        if (range.lastId === liveSelectionId) {
            const tail = sshTerminal.liveLine.slice(range.firstId === liveSelectionId ? range.firstOffset : 0, range.lastOffset);
            return validatedHex(range.firstId === liveSelectionId ? tail
                : sshTerminal.selectedText(range.firstId, range.firstOffset, range.lastId, 0) + tail);
        }
        return validatedHex(sshTerminal.selectedText(range.firstId, range.firstOffset, range.lastId, range.lastOffset));
    }
    property alias parseWindowVisible: parseOutputWindow.visible
    property int parseSessionId: -1
    readonly property var parseResultModel: backend && parseSessionId >= 4 ? backend.parseModel(parseSessionId) : null
    property string parsedHex: ""

    // #3 协议类型常量对象，对应 ProtocolDetector.h 中 ProtocolType 枚举值，
    // 避免在上下文菜单中直接使用含义不明的魔法数字。
    // 注意：QML 属性名必须以小写字母开头，故命名为 protocolId
    readonly property var protocolId: ({
            "Auto": 0,
            "DLT698": 1,
            "IEC101": 2,
            "IEC104": 3,
            "AppMsg": 4,
            "ModuleMsg": 5,
            "GDW3762": 6,
            "DLT645": 7
        })

    Text {
        id: charMeasurer
        visible: false
        font.family: Next.PlatformFonts.monospaceFamily
        font.pixelSize: liveLogDialog.terminalFontSize
        // Monospace logs need no optional ligatures; Qt still shapes scripts that require it.
        font.kerning: false
        font.preferShaping: false
        text: "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    }
    readonly property real charWidth: charMeasurer.contentWidth / charMeasurer.text.length

    component MatchHighlights: Item {
        property string sourceText
        property var ranges: []
        property color markColor: "transparent"
        clip: true
        Repeater {
            model: parent.ranges
            Rectangle {
                id: highlight
                objectName: "ssh-match-span"
                required property var modelData
                x: prefixMetrics.advanceWidth
                width: endMetrics.advanceWidth - x
                height: parent.height
                color: Qt.rgba(parent.markColor.r, parent.markColor.g, parent.markColor.b, 0.3)
                TextMetrics { id: prefixMetrics; font: charMeasurer.font; text: highlight.parent.sourceText.slice(0, highlight.modelData.start) }
                TextMetrics { id: endMetrics; font: charMeasurer.font; text: highlight.parent.sourceText.slice(0, highlight.modelData.start + highlight.modelData.length) }
            }
        }
    }

    function beginWindowMove() {
        relocatingWindow = true;
        captureLogViewport();
        hostKeyDialog.close();
        terminalContextMenu.close();
        rulePicker.close(); matchDetails.close();
    }
    function endWindowMove() {
        relocatingWindow = false;
        logListView.forceLayout();
        applyLogViewport();
        if (pendingHostKey && visible) hostKeyDialog.open();
        else if (visible && sshTerminal.connected) terminalCapture.forceActiveFocus();
    }

    function openDialog() { requested(); }



    function stateText() {
        if (sshTerminal.hostKeyPending) return "待确认指纹";
        switch (sshTerminal.connectionState) {
        case SshTerminal.Connecting:
            return "连接中";
        case SshTerminal.Connected:
            return "已连接";
        case SshTerminal.Reconnecting:
            return "重连中";
        case SshTerminal.Failed:
            return "连接失败";
        case SshTerminal.Disconnected:
            return "已断开";
        default:
            return "未连接";
        }
    }

    function refreshSearchResult() {
        if (searchKeyword === "") {
            searchMatches = [];
            searchCursor = -1;
            m_nextSearchLineId = -1;
            return;
        }
        // 注意：必须整体赋值新数组（不得用 push），否则不触发 QML 属性变更通知 (#2)
        searchMatches = sshTerminal.findLineIds(searchKeyword);
        m_nextSearchLineId = sshTerminal.firstLineId + sshTerminal.currentLineCount;
        if (searchMatches.length === 0) {
            searchCursor = -1;
            return;
        }
        if (searchCursor < 0 || searchCursor >= searchMatches.length)
            searchCursor = 0;
    }

    function updateSearchResult() {
        if (searchKeyword === "") return;
        if (m_nextSearchLineId < 0) { refreshSearchResult(); return; }
        const selectedMatch = searchCursor >= 0 ? searchMatches[searchCursor] : -1;
        const firstId = sshTerminal.firstLineId;
        let first = 0;
        while (first < searchMatches.length && searchMatches[first] < firstId) first++;
        const added = sshTerminal.findLineIds(searchKeyword, Math.max(firstId, m_nextSearchLineId));
        if (first > 0 || added.length > 0) {
            let matches = searchMatches.slice(first);
            for (let i = 0; i < added.length; ++i)
                if (matches.length === 0 || added[i] > matches[matches.length - 1]) matches.push(added[i]);
            searchMatches = matches;
            const cursor = matches.indexOf(selectedMatch);
            searchCursor = matches.length === 0 ? -1 : Math.max(0, cursor);
        }
        m_nextSearchLineId = firstId + sshTerminal.currentLineCount;
    }

    function captureLogViewport() {
        if (!logListView || !sshTerminal || viewUpdatePending) return;
        viewUpdatePending = true;
        if (autoScroll) { viewAnchorId = -1; return; }
        const row = logListView.indexAt(1, logListView.contentY + 1);
        const item = logListView.itemAtIndex(row);
        viewAnchorId = sshTerminal.lineIdAt(row);
        viewAnchorOffset = item ? item.y - logListView.contentY : 0;
    }

    function applyLogViewport() {
        if (!logListView || !sshTerminal) return;
        viewUpdatePending = false;
        if (autoScroll) { logListView.positionViewAtEnd(); return; }
        if (viewAnchorId < 0) return;
        const row = sshTerminal.indexForLineId(viewAnchorId);
        if (row < 0) {
            logListView.positionViewAtBeginning();
            statusHint.text = "回看的旧日志已超出缓存范围，已定位到最早保留内容";
        } else {
            logListView.positionViewAtIndex(row, ListView.Beginning);
            const item = logListView.itemAtIndex(row);
            if (item) logListView.contentY = item.y - viewAnchorOffset;
        }
    }

    function jumpToSearch(forward) {
        if (searchKeyword === "")
            return;
        if (searchMatches.length === 0)
            refreshSearchResult();
        if (searchMatches.length === 0)
            return;
        if (searchCursor < 0) {
            searchCursor = 0;
        } else if (forward) {
            searchCursor = (searchCursor + 1) % searchMatches.length;
        } else {
            searchCursor = (searchCursor - 1 + searchMatches.length) % searchMatches.length;
        }
        var targetIndex = sshTerminal.indexForLineId(searchMatches[searchCursor]);
        if (targetIndex < 0) return;
        logListView.positionViewAtIndex(targetIndex, ListView.Center);
        selectLineAndExtractHex(targetIndex);
    }

    function validatedHex(text) {
        if (!/^[0-9A-Fa-f \t\r\n]+$/.test(text)) return "";
        const compact = text.replace(/\s/g, "");
        if (compact.length < 2 || compact.length % 2 !== 0) return "";
        return compact.toUpperCase().match(/../g).join(" ");
    }

    function extractHexSegmentFromLine(lineText, position) {
        if (!lineText || lineText.length === 0)
            return null;

        var hexPattern = /(^|[^0-9A-Za-z_])([0-9A-Fa-f]+(?:[ \t]+[0-9A-Fa-f]+)*)(?=$|[^0-9A-Za-z_])/g;
        var best = null;
        var match;

        while ((match = hexPattern.exec(lineText)) !== null) {
            var rawText = match[2];
            var start = match.index + match[1].length;
            var hexOnly = rawText.replace(/[ \t]/g, "");
            if (hexOnly.length < (position === undefined ? 10 : 2) || hexOnly.length % 2 !== 0)
                continue;
            if (position !== undefined && (position < start || position >= start + rawText.length)) continue;

            if (!best || hexOnly.length > best.hexOnly.length) {
                best = {
                    start: start,
                    end: start + rawText.length,
                    hexOnly: hexOnly
                };
            }
        }

        if (!best)
            return null;

        best.formattedHex = validatedHex(best.hexOnly);
        return best;
    }

    function logicalSelectionPoint(index, offset) {
        while (index > 0 && sshTerminal.lineSegmentOffset(index) > offset) --index;
        while (index + 1 < sshTerminal.currentLineCount) {
            const nextOffset = sshTerminal.lineSegmentOffset(index + 1);
            if (nextOffset === 0 || nextOffset >= offset) break;
            ++index;
        }
        return {id: sshTerminal.lineIdAt(index), position: offset - sshTerminal.lineSegmentOffset(index)};
    }
    function selectHexAt(x, y) {
        // positionAt returns the nearest caret; bias to the character under the pointer.
        const point = selectionPoint(x - charWidth / 2, y);
        clearTextSelection();
        selectedLineIndex = -1; selectedLineId = -1;
        selectedHexStart = -1; selectedHexEnd = -1; selectedHexText = "";
        if (!point) return;
        const index = sshTerminal.indexForLineId(point.id);
        if (index >= 0 && !sshTerminal.lineIsComplete(index)) return;
        const text = index >= 0 ? sshTerminal.lineAt(index) : sshTerminal.liveLine;
        const offset = index >= 0 ? sshTerminal.lineSegmentOffset(index) : 0;
        const hex = extractHexSegmentFromLine(text, offset + point.position);
        if (!hex) return;
        textSelectionAnchor = index >= 0 ? logicalSelectionPoint(index, hex.start) : {id: point.id, position: hex.start};
        textSelectionFocus = index >= 0 ? logicalSelectionPoint(index, hex.end) : {id: point.id, position: hex.end};
        autoScroll = false;
    }

    function applyHexSelectionOnCurrentLine() {
        if (!logListView || selectedLineIndex < 0)
            return;
        var item = logListView.itemAtIndex(selectedLineIndex);
        if (item && item["applyHexSelection"])
            item["applyHexSelection"]();
    }

    function clearTextSelection() {
        selectionPointerDown = false;
        textSelectionAnchor = null;
        textSelectionFocus = null;
    }
    function selectionContains(id) {
        return textSelectionRange !== null && id >= textSelectionRange.firstId && id <= textSelectionRange.lastId;
    }
    function applyTextSelection(editor, id) {
        if (!editor || !selectionContains(id)) return false;
        const range = textSelectionRange;
        editor.select(id === range.firstId ? Math.min(range.firstOffset, editor.length) : 0,
            id === range.lastId ? Math.min(range.lastOffset, editor.length) : editor.length);
        return true;
    }
    function selectionPoint(x, y) {
        const point = selectionArea.mapToItem(logListView.contentItem, x,
            Math.max(0, Math.min(selectionArea.height - 1, y)));
        const index = logListView.indexAt(1, point.y);
        const footer = logListView.footerItem;
        const row = index >= 0 ? logListView.itemAtIndex(index)
            : footer && footer.height > 0 && point.y >= footer.y && point.y < footer.y + footer.height ? footer : null;
        if (!row) return null;
        const id = index >= 0 ? sshTerminal.lineIdAt(index) : liveSelectionId;
        if (!textSelectionAnchor) textSelectionAnchor = {id: id, position: 0};
        if (!textSelectionFocus || textSelectionFocus.id !== id) textSelectionFocus = {id: id, position: 0};
        const position = row.selectionPosition(point.x, point.y);
        return {id: id, position: position};
    }
    function extendTextSelection() {
        const point = selectionPoint(selectionArea.mouseX, selectionArea.mouseY);
        if (point) textSelectionFocus = point;
        if (hasTextSelection) autoScroll = false;
    }
    function copyTextSelection() {
        if (!hasTextSelection) return;
        const range = textSelectionRange;
        const text = sshTerminal.selectedText(range.firstId, range.firstOffset, range.lastId, range.lastOffset);
        if (text === "") return;
        clipboardHelper.text = text;
        clipboardHelper.selectAll();
        clipboardHelper.copy();
        clipboardHelper.clear();
        statusHint.text = "已复制选中文字";
    }

    function selectLineAndExtractHex(lineIndex) {
        clearTextSelection();
        var oldIndex = selectedLineIndex;
        selectedHexStart = -1;
        selectedHexEnd = -1;
        selectedHexText = "";
        selectedLineIndex = lineIndex;
        selectedLineId = sshTerminal.lineIdAt(lineIndex);

        if (!sshTerminal.lineIsComplete(lineIndex)) {
            statusHint.text = "该条日志尚未完整或较早部分已淘汰，暂不作为完整报文解析";
            applyHexSelectionOnCurrentLine();
            return;
        }
        var lineText = sshTerminal.lineAt(lineIndex);
        var extracted = extractHexSegmentFromLine(lineText);
        if (extracted) {
            selectedHexStart = extracted.start;
            selectedHexEnd = extracted.end;
            selectedHexText = extracted.formattedHex;
        }

        Qt.callLater(function () {
            if (!logListView) return;
            if (oldIndex >= 0 && oldIndex !== selectedLineIndex) {
                var oldItem = logListView.itemAtIndex(oldIndex);
                if (oldItem && oldItem["applyHexSelection"])
                    oldItem["applyHexSelection"]();
            }
            applyHexSelectionOnCurrentLine();
        });
    }

    function parseSelectedHexWithProtocol(protocolType) {
        if (!backend || parseableHexText === "")
            return;

        if (parseSessionId < 4) parseSessionId = backend.createParseSession();
        if (parseSessionId < 4) return;
        parsedHex = parseableHexText;
        parseOutputWindow.resetSelection();
        backend.parseHex(parsedHex, encodingCombo.currentText, parseSessionId, protocolType);
        parseOutputWindow.show();
        Qt.callLater(function () { parseOutputWindow.autoFit(); });
    }

    function copySelectedHexToClipboard() {
        if (parseableHexText === "")
            return;
        clipboardHelper.text = parseableHexText;
        clipboardHelper.selectAll();
        clipboardHelper.copy();
    }

    function sendTerminalText(text) {
        if (!sshTerminal.connected || sshTerminal.receptionPaused || text === "") return;
        if (!monitorHit) { autoScroll = true; Qt.callLater(applyLogViewport); }
        sshTerminal.sendRawBytes(text);
    }
    function pasteTerminal() {
        terminalCapture.forceActiveFocus();
        sendTerminalText(sshTerminal.clipboardText());
    }
    function routeTerminalKey(event) {
        if (!sshTerminal.connected || sshTerminal.receptionPaused) return false;
        const mac = Qt.platform.os === "osx";
        const control = !!(event.modifiers & (mac ? Qt.MetaModifier : Qt.ControlModifier));
        const alt = !!(event.modifiers & Qt.AltModifier);
        const shift = !!(event.modifiers & Qt.ShiftModifier);
        if (mac && (event.modifiers & Qt.ControlModifier)) return false;
        if (terminalCapture.inputMethodComposing && !control) return false;
        let text = "";
        const arrows = ({[Qt.Key_Up]:"A", [Qt.Key_Down]:"B", [Qt.Key_Right]:"C", [Qt.Key_Left]:"D", [Qt.Key_Home]:"H", [Qt.Key_End]:"F"});
        if (arrows[event.key]) {
            const modifier = 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (control ? 4 : 0);
            text = "\x1b[" + (modifier > 1 ? "1;" + modifier : "") + arrows[event.key];
        } else if (control && event.key >= Qt.Key_A && event.key <= Qt.Key_Z) {
            text = String.fromCharCode(event.key - Qt.Key_A + 1);
        } else if (control && (event.key === Qt.Key_Space || event.key === Qt.Key_At)) text = "\x00";
        else if (control && event.key === Qt.Key_BracketLeft) text = "\x1b";
        else if (control && event.key === Qt.Key_Backslash) text = "\x1c";
        else if (control && event.key === Qt.Key_BracketRight) text = "\x1d";
        else if (control && event.key === Qt.Key_Backspace) text = "\x17";
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) text = "\r";
        else if (event.key === Qt.Key_Backspace) text = alt ? "\x1b\x7f" : "\x7f";
        else if (event.key === Qt.Key_Backtab || event.key === Qt.Key_Tab && shift) text = "\x1b[Z";
        else if (event.key === Qt.Key_Tab) text = "\t";
        else if (event.key === Qt.Key_Escape) text = "\x1b";
        else if ([Qt.Key_Insert, Qt.Key_Delete, Qt.Key_PageUp, Qt.Key_PageDown].indexOf(event.key) >= 0) {
            const number = ({[Qt.Key_Insert]:2, [Qt.Key_Delete]:3, [Qt.Key_PageUp]:5, [Qt.Key_PageDown]:6})[event.key];
            text = "\x1b[" + number + (control ? ";5" : "") + "~";
        } else if (!control && event.text && event.text.charCodeAt(0) >= 32) text = (alt ? "\x1b" : "") + event.text;
        if (text === "") return false;
        terminalCapture.forceActiveFocus();
        sendTerminalText(text);
        event.accepted = true;
        return true;
    }
    function sendQuickCommand(command) {
        if (!sshTerminal.connected || sshTerminal.receptionPaused) return;
        if (!monitorHit) autoScroll = true;
        sshTerminal.sendCommand(command);
        terminalCapture.forceActiveFocus();
        if (autoScroll) Qt.callLater(applyLogViewport);
    }

    function addCustomCommand(name, command) {
        var trimmedName = name ? name.trim() : "";
        var trimmedCommand = command ? command.trim() : "";
        if (trimmedName === "" || trimmedCommand === "")
            return;

        var list = customCommands.slice();
        var replaced = false;
        for (var i = 0; i < list.length; i++) {
            if (list[i].name === trimmedName) {
                list[i] = {
                    "name": trimmedName,
                    "command": trimmedCommand
                };
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            list.push({
                "name": trimmedName,
                "command": trimmedCommand
            });
        }
        if (sharedCommands) sharedCommands.commands = list;
        else localCustomCommands = list;
        saveCustomCommands();
    }

    function removeCustomCommandByName(name) {
        var targetName = name ? name.trim() : "";
        if (targetName === "")
            return;

        var list = [];
        for (var i = 0; i < customCommands.length; i++) {
            if (customCommands[i].name !== targetName)
                list.push(customCommands[i]);
        }
        if (sharedCommands) sharedCommands.commands = list;
        else localCustomCommands = list;
        saveCustomCommands();
    }

    function loadCustomCommands() {
        if (!backend)
            return;

        var raw = backend.getUiSetting("liveLog/customCommands", "[]");
        var parsed = [];
        try {
            parsed = JSON.parse(String(raw));
        } catch (e) {
            parsed = [];
        }

        var normalized = [];
        if (parsed && parsed.length !== undefined) {
            for (var i = 0; i < parsed.length; i++) {
                var item = parsed[i];
                var itemName = item && item.name ? String(item.name).trim() : "";
                var itemCommand = item && item.command ? String(item.command).trim() : "";
                if (itemName !== "" && itemCommand !== "") {
                    normalized.push({
                        "name": itemName,
                        "command": itemCommand
                    });
                }
            }
        }
        if (sharedCommands) sharedCommands.commands = normalized;
        else localCustomCommands = normalized;
    }

    function saveCustomCommands() {
        if (!backend)
            return;
        backend.setUiSetting("liveLog/customCommands", JSON.stringify(customCommands));
    }

    function requestRemoveCustomCommand(commandName) {
        var targetName = commandName ? String(commandName).trim() : "";
        if (targetName === "")
            return;
        customCmdPendingDeleteName = targetName;
        confirmDeleteCmdPopup.open();
    }

    function ensureCustomCommandsLoaded() {
        if (customCommandsLoaded || !backend)
            return;
        if (!sharedCommands || !sharedCommands.loaded) {
            loadCustomCommands();
            if (sharedCommands) sharedCommands.loaded = true;
        }
        customCommandsLoaded = true;
    }

    onBackendChanged: {
        customCommandsLoaded = false;
        ensureCustomCommandsLoaded();
    }
    onSharedCommandsChanged: {
        customCommandsLoaded = false;
        ensureCustomCommandsLoaded();
    }

    onVisibleChanged: {
        if (relocatingWindow) return;
        if (visible) {
            ensureCustomCommandsLoaded();
            if (monitorHit) Qt.callLater(positionMonitorHit);
            if (pendingHostKey) hostKeyDialog.open();
        } else hostKeyDialog.close();
    }

    onClosing: closeSshSession()



    Item { id: titleBar; height: 0; width: parent.width }


    SshTerminal {
        id: sshTerminal
        objectName: "ssh-terminal-backend"
        persistSettings: liveLogDialog.persistConnectionSettings
        useSystemCredentials: liveLogDialog.useSystemCredentials
        presentationActive: liveLogDialog.visible
        onReceptionPausedChanged: statusHint.text = receptionPaused
            ? "后台日志接收已停止，SSH 连接保留。点击“恢复接收”重新读取。"
            : "后台日志接收已恢复。"
        onCredentialStateChanged: liveLogDialog.continueSshAction()
        onPasswordOperationFinished: function(operationId, success, error) {
            if (operationId !== liveLogDialog.passwordOperationId) return
            const applies = liveLogDialog.sameTarget(liveLogDialog.passwordOperationTarget, liveLogDialog.currentTarget())
            liveLogDialog.passwordOperationId = ""; liveLogDialog.passwordOperationTarget = null
            if (!applies) return
            liveLogDialog.passwordNoticeError = !success
            liveLogDialog.passwordNotice = success ? "密码凭据操作完成。" : (error || "密码凭据操作失败。")
            if (success) pwdInput.text = ""
        }
        onHostKeyConfirmationRequested: function(host, port, algorithm, fingerprint, requestId) {
            if (liveLogDialog.pendingHostKey) liveLogDialog.respondHostKey(false)
            liveLogDialog.pendingHostKey = {host:host, port:port, algorithm:algorithm, fingerprint:fingerprint, requestId:requestId}
            if (liveLogDialog.visible) hostKeyDialog.open()
        }
        onHostKeyPendingChanged: {
            if (!hostKeyPending) { liveLogDialog.pendingHostKey = null; hostKeyDialog.close() }
        }

        onConnectedChanged: {
            if (connected) {
                Qt.callLater(function () {
                    if (liveLogDialog.autoScroll) logListView.positionViewAtEnd();
                    if (liveLogDialog.visible && liveLogDialog.terminalInput)
                        liveLogDialog.terminalInput.forceActiveFocus();
                });
            }
        }

        onSharedBufferOverflowed: {
            statusHint.text = "输出过快，已自动丢弃最早缓冲数据";
        }

        onLinesAboutToChange: liveLogDialog.captureLogViewport()

        onMonitorMatched: function(hit) {
            if (!liveLogDialog.monitorEnabled || !liveLogDialog.autoScroll) return;
            if (hit.error) {
                liveLogDialog.monitorError = hit.error;
                liveLogDialog.configureMonitorRules();
                return;
            }
            if (hit.pauseOnMatch === false) {
                liveLogDialog.monitorNotice = hit;
                statusHint.text = "命中「" + hit.ruleName + "」，已按规则颜色标记，继续跟随。";
                return;
            }
            liveLogDialog.monitorHit = hit;
            liveLogDialog.autoScroll = false;
            statusHint.text = "已命中「" + hit.ruleName + "」，暂停跟随；后台继续接收。点击状态栏可查看命中原文。";
            Qt.callLater(liveLogDialog.positionMonitorHit);
        }
        onLiveLineChanged: if (liveLogDialog.autoScroll) Qt.callLater(liveLogDialog.applyLogViewport)
        onLineCountChanged: {
            if (!sshTerminal) return;
            if (sshTerminal.currentLineCount === 0) viewAnchorId = -1;
            if (sshTerminal.currentLineCount === 0 || textSelectionRange
                    && (textSelectionRange.firstId < sshTerminal.firstLineId || textSelectionRange.lastId > liveSelectionId))
                clearTextSelection();
            updateSearchResult();
            selectedLineIndex = sshTerminal.indexForLineId(selectedLineId);
            if (selectedLineIndex < 0 || !sshTerminal.lineIsComplete(selectedLineIndex)) {
                selectedLineId = -1;
                selectedHexStart = -1; selectedHexEnd = -1; selectedHexText = "";
            }
            Qt.callLater(applyLogViewport);
        }
    }

    Popup {
        id: rulePicker; objectName: "ssh-rule-picker"
        parent: liveLogDialog.Overlay.overlay
        width: Math.min(420, parent ? parent.width - 20 : 420)
        padding: 12
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: theme ? theme.popupBackground : "#292d35"; border.color: cardBorder; radius: 9 }
        contentItem: ColumnLayout {
            spacing: 8
            Label { text: "当前会话使用的规则 · 已选 " + selectedMonitorRules.length + " 条"; color: labelColor; font.bold: true }
            Next.GlassTextField { id: ruleSearch; objectName: "ssh-rule-search"; Layout.fillWidth: true; placeholderText: "搜索规则名称或表达式" }
            ListView {
                objectName: "ssh-rule-list"
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(330, Math.max(54, count * 57))
                clip: true; spacing: 4
                model: monitorRuleLibrary ? monitorRuleLibrary.monitorRules.filter(function(rule) {
                    const keyword = ruleSearch.text.toLowerCase();
                    return rule.name.toLowerCase().indexOf(keyword) >= 0 || rule.pattern.toLowerCase().indexOf(keyword) >= 0;
                }) : []
                delegate: Next.GlassCheckBox {
                    required property var modelData
                    objectName: "ssh-rule-choice-" + modelData.id
                    width: ListView.view.width; height: 53
                    text: modelData.name + "\n" + modelData.pattern
                    checked: liveLogDialog.monitorRuleIds.indexOf(modelData.id) >= 0
                    onToggled: liveLogDialog.toggleMonitorRule(modelData.id, checked)
                    contentItem: Item {
                        Rectangle { x: 31; y: 8; width: 8; height: 8; radius: 4; color: modelData.color || "#e8b76a" }
                        Column {
                            x: 47; width: parent.width - 47; spacing: 4
                            Label { textFormat: Text.PlainText; text: modelData.name + (modelData.pauseOnMatch === false ? " · 仅标记" : " · 暂停跟随"); color: labelColor; font.pixelSize: 13 }
                            Label { textFormat: Text.PlainText; text: modelData.pattern; width: parent.width; elide: Text.ElideRight; color: theme ? theme.subText : "#aaaaaa"; font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 11 }
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar {}
                Label { anchors.centerIn: parent; visible: parent.count === 0; text: "暂无规则，请先新增"; color: labelColor }
            }
            Label { text: "按规则设置进行标记或暂停跟随；只监测新输出。"; color: theme ? theme.subText : "#aaaaaa"; font.pixelSize: 11 }
            Next.GlassButton {
                objectName: "ssh-rule-manage"; text: "打开规则库…"; Layout.fillWidth: true
                onClicked: { rulePicker.close(); if (monitorRuleLibrary) monitorRuleLibrary.openMonitorRules(liveLogDialog.Window.window); }
            }
        }
    }
    Popup {
        id: matchDetails; objectName: "ssh-match-details"
        parent: liveLogDialog.Overlay.overlay; anchors.centerIn: parent
        width: Math.min(640, parent ? parent.width - 24 : 640); padding: 12
        background: Rectangle { color: theme ? theme.popupBackground : "#292d35"; border.color: cardBorder; radius: 9 }
        contentItem: ColumnLayout {
            Label { textFormat: Text.PlainText; text: "命中原文" + (monitorEvent && monitorEvent.truncated ? "（过长内容已截取）" : "") + " · " + (monitorEvent ? monitorEvent.ruleName : ""); color: labelColor }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: 180
                TextArea { text: monitorEvent ? monitorEvent.text : ""; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: labelColor; font.family: Next.PlatformFonts.monospaceFamily }
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                Next.GlassButton { text: "关闭"; onClicked: matchDetails.close() }
            }
        }
    }

    Next.GlassDialog {
        id: hostKeyDialog; objectName: "ssh-host-key-dialog"
        palette: liveLogDialog.palette
        parent: Overlay.overlay; anchors.centerIn: parent; width: Math.min(620, liveLogDialog.width - 30)
        modal: false; title: "确认 SSH 主机密钥"; standardButtons: Dialog.NoButton; closePolicy: Popup.CloseOnEscape
        onClosed: if (liveLogDialog.pendingHostKey && liveLogDialog.visible && !liveLogDialog.relocatingWindow) liveLogDialog.respondHostKey(false)
        contentItem: ColumnLayout {
            spacing: 12
            Label { objectName: "ssh-host-key-target"; text: liveLogDialog.pendingHostKey ? liveLogDialog.pendingHostKey.host + ":" + liveLogDialog.pendingHostKey.port : ""; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: liveLogDialog.pendingHostKey ? "算法：" + liveLogDialog.pendingHostKey.algorithm : ""; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap }
            TextArea { objectName: "ssh-host-key-fingerprint"; text: liveLogDialog.pendingHostKey ? liveLogDialog.pendingHostKey.fingerprint : ""; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; Layout.fillWidth: true }
            Label { text: "请通过可信渠道核对目标与 SHA256 指纹。只有明确确认才会继续；关闭或取消均不信任。密钥变化会阻止连接，不提供覆盖按钮。"; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap }
            RowLayout {
                Next.GlassButton { objectName: "ssh-host-key-reject"; text: "拒绝并断开"; onClicked: liveLogDialog.respondHostKey(false) }
                Next.GlassButton { objectName: "ssh-host-key-accept"; text: "已核对，信任并继续"; enabled: sshTerminal.hostKeyPending && !!liveLogDialog.pendingHostKey; onClicked: liveLogDialog.respondHostKey(true) }
            }
        }
    }

    Next.GlassMenu {
        id: terminalContextMenu

        Next.GlassMenuItem {
            objectName: "ssh-parse-auto"
            text: "立即解析"
            enabled: parseableHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.Auto)
        }
        Next.GlassMenuItem {
            text: "按 DL/T 698 解析"
            enabled: parseableHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.DLT698)
        }
        Next.GlassMenuItem {
            text: "按 IEC 101 解析"
            enabled: parseableHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.IEC101)
        }
        Next.GlassMenuItem {
            text: "按 IEC 104 解析"
            enabled: parseableHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.IEC104)
        }
        Next.GlassMenuItem {
            text: "按 应用层消息 解析"
            enabled: parseableHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.AppMsg)
        }
        Next.GlassMenuItem {
            text: "按 模块消息 解析"
            enabled: parseableHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.ModuleMsg)
        }
        Next.GlassMenuItem {
            text: "按 Q/GDW 376.2 解析"
            enabled: parseableHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.GDW3762)
        }
        Next.GlassMenuItem {
            text: "按 DL/T 645 解析"
            enabled: parseableHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.DLT645)
        }
        Next.GlassMenuSeparator {}
        Next.GlassMenuItem {
            objectName: "ssh-copy-selection"
            text: "复制选中文字"
            enabled: liveLogDialog.hasTextSelection
            onTriggered: liveLogDialog.copyTextSelection()
        }
        Next.GlassMenuItem {
            text: "复制Hex串"
            enabled: parseableHexText !== ""
            onTriggered: copySelectedHexToClipboard()
        }
    }

    Shortcut {
        enabled: liveLogDialog.visible && liveLogDialog.Window.window && liveLogDialog.Window.window.active
        sequences: ["Ctrl+Shift+F"]
        onActivated: {
            findBarVisible = true;
            findInput.forceActiveFocus();
            findInput.selectAll();
        }
    }
    Shortcut {
        enabled: liveLogDialog.visible && liveLogDialog.Window.window && liveLogDialog.Window.window.active
        sequence: "F3"
        onActivated: jumpToSearch(true)
    }
    Shortcut {
        enabled: liveLogDialog.visible && liveLogDialog.Window.window && liveLogDialog.Window.window.active
        sequence: "Shift+F3"
        onActivated: jumpToSearch(false)
    }
    Shortcut {
        enabled: liveLogDialog.visible && liveLogDialog.Window.window && liveLogDialog.Window.window.active
        sequence: "Ctrl+Shift+C"
        onActivated: {
            if (hasTextSelection) copyTextSelection();
            else if (selectedLineIndex >= 0)
                sshTerminal.copyLineToClipboard(selectedLineIndex);
        }
    }
    Shortcut {
        enabled: liveLogDialog.visible && liveLogDialog.Window.window && liveLogDialog.Window.window.active
        sequence: "Ctrl+Shift+A"
        onActivated: sshTerminal.copyBufferToClipboard()
    }
    Shortcut {
        enabled: liveLogDialog.visible && sshTerminal.connected && liveLogDialog.Window.window && liveLogDialog.Window.window.active
        sequences: [StandardKey.Paste, "Ctrl+Shift+V"]
        onActivated: liveLogDialog.pasteTerminal()
    }

    readonly property color terminalBg: "#1a1d21"
    readonly property color terminalText: "#d4d4d4"
    readonly property color terminalMatchBg: "#5a4a00"
    readonly property color terminalBorder: "#3c3f44"
    readonly property color cardBg: theme ? theme.surface : "#ffffff"
    readonly property color cardBorder: theme ? theme.controlBorder : "#d0d3d8"
    readonly property color labelColor: theme ? theme.subText : "#606060"
    readonly property color primaryBtnBg: liveLogDialog.theme ? liveLogDialog.theme.accentMuted : "#1976D2"
    readonly property color primaryBtnHoverBg: liveLogDialog.theme ? liveLogDialog.theme.buttonHover : primaryBtnBg
    readonly property color focusAccentColor: liveLogDialog.theme ? liveLogDialog.theme.accentMuted : primaryBtnBg
    readonly property color successColor: liveLogDialog.theme ? liveLogDialog.theme.success : "#4CAF50"
    readonly property color warningColor: liveLogDialog.theme ? liveLogDialog.theme.warning : "#FF9800"
    readonly property color errorColor: liveLogDialog.theme ? liveLogDialog.theme.error : "#F44336"
    readonly property color mutedColor: liveLogDialog.theme ? liveLogDialog.theme.subText : "#888888"
    readonly property color successWeakBg: Qt.rgba(successColor.r, successColor.g, successColor.b, 0.22)
    readonly property color warningWeakBg: Qt.rgba(warningColor.r, warningColor.g, warningColor.b, 0.22)
    readonly property color errorWeakBg: Qt.rgba(errorColor.r, errorColor.g, errorColor.b, 0.22)
    readonly property color neutralWeakBg: Qt.rgba(cardBorder.r, cardBorder.g, cardBorder.b, 0.28)
    readonly property color dangerBtnBg: "#d44a3a"
    readonly property color connBtnBg: {
        switch (sshTerminal.connectionState) {
        case SshTerminal.Connecting:
        case SshTerminal.Reconnecting:
            return warningColor;
        case SshTerminal.Connected:
            return dangerBtnBg;
        default:
            return primaryBtnBg;
        }
    }

    // ── 与主界面一致的控件样式组件 ────────────────────────────────
    component StyledTextField: Next.GlassTextField {
        Layout.fillWidth: liveLogDialog.workspaceManaged
        implicitHeight: 36
        topPadding: 0
        bottomPadding: 0
        leftPadding: 8
        rightPadding: 8
        verticalAlignment: Text.AlignVCenter
        color: liveLogDialog.theme ? liveLogDialog.theme.text : "#333333"
        placeholderTextColor: liveLogDialog.theme ? liveLogDialog.theme.placeholderText : "#aaaaaa"
    }

    component StyledComboBox: Next.GlassComboBox {
        id: styledCbCtrl
        implicitHeight: 36
        leftPadding: 8

        delegate: ItemDelegate {
            width: styledCbCtrl.width
            contentItem: Text {
                text: modelData
                color: liveLogDialog.theme ? liveLogDialog.theme.text : "#333333"
                font: styledCbCtrl.font
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: styledCbCtrl.highlightedIndex === index ? (liveLogDialog.theme ? liveLogDialog.theme.splitHandleHover : "#e0e0e0") : "transparent"
            }
            highlighted: styledCbCtrl.highlightedIndex === index
        }

        indicator: Canvas {
            id: cbCanvas
            x: styledCbCtrl.width - width - styledCbCtrl.rightPadding
            y: styledCbCtrl.topPadding + (styledCbCtrl.availableHeight - height) / 2
            width: 12
            height: 8
            contextType: "2d"
            onAvailableChanged: if (available) requestPaint()
            Connections {
                target: styledCbCtrl
                function onPressedChanged() {
                    cbCanvas.requestPaint();
                }
            }
            Connections {
                target: liveLogDialog
                function onThemeChanged() {
                    cbCanvas.requestPaint();
                }
            }
            onPaint: {
                if (!available) return;
                const ctx = getContext("2d");
                if (!ctx) return;
                ctx.reset();
                ctx.moveTo(0, 0);
                ctx.lineTo(width, 0);
                ctx.lineTo(width / 2, height);
                ctx.closePath();
                ctx.fillStyle = liveLogDialog.theme ? liveLogDialog.theme.text : "#333333";
                ctx.fill();
            }
        }

        contentItem: Text {
            leftPadding: 0
            rightPadding: styledCbCtrl.indicator.width + styledCbCtrl.spacing
            text: styledCbCtrl.displayText
            font: styledCbCtrl.font
            color: liveLogDialog.theme ? liveLogDialog.theme.text : "#333333"
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        popup: Popup {
            y: styledCbCtrl.height - 1
            width: styledCbCtrl.width
            implicitHeight: Math.min(contentItem.implicitHeight, 400)
            padding: 1
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: styledCbCtrl.popup.visible ? styledCbCtrl.delegateModel : null
                currentIndex: styledCbCtrl.highlightedIndex
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
            }
            background: Rectangle {
                border.color: liveLogDialog.theme ? liveLogDialog.theme.controlBorder : "#cccccc"
                radius: 8
                color: liveLogDialog.theme ? liveLogDialog.theme.popupBackground : "#ffffff"
            }
        }
    }

    component StyledButton: Next.GlassButton {
        id: styledBtnCtrl
        implicitHeight: 36
        contentItem: Text {
            text: styledBtnCtrl.text
            font: styledBtnCtrl.font

            color: liveLogDialog.theme ? liveLogDialog.theme.buttonText : "#ffffff"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    TextEdit {
        id: clipboardHelper
        visible: false
        textFormat: TextEdit.PlainText
    }

    Next.ParseResultWindow {
        id: parseOutputWindow
        namePrefix: "ssh-parse"
        backend: liveLogDialog.backend
        theme: liveLogDialog.theme
        parseSessionId: liveLogDialog.parseSessionId
        parsedHex: liveLogDialog.parsedHex
        sessionName: liveLogDialog.sessionName
        transientParent: liveLogDialog.ownerWindow && liveLogDialog.ownerWindow.visible ? liveLogDialog.ownerWindow : liveLogDialog.Window.window
    }

    ColumnLayout {
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        spacing: 6

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: connectionRows.implicitHeight + 20
            color: cardBg; border.color: cardBorder; radius: 10
            ColumnLayout {
                id: connectionRows
                anchors.fill: parent; anchors.margins: 10; spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    StyledButton {
                        objectName: "ssh-config-toggle"
                        text: liveLogDialog.connectionConfigExpanded ? "▾ 收起配置" : "▸ 连接配置"
                        Accessible.name: "连接配置"
                        Accessible.description: liveLogDialog.connectionConfigExpanded ? "已展开" : "已折叠"
                        onClicked: liveLogDialog.connectionConfigExpanded = !liveLogDialog.connectionConfigExpanded
                    }
                    Label {
                        objectName: "ssh-target-summary"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: liveLogDialog.sessionName + " · " + (userInput.text.trim() || "用户") + "@" + (hostInput.text.trim() || "未设置主机") + ":" + portInput.text + " · " + encodingCombo.currentText
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: labelColor
                        font.pixelSize: 12
                    }
                    StyledButton {
                        objectName: "sshConnectAction"
                        implicitWidth: 96
                        enabled: !liveLogDialog.passwordOperationId
                        palette.button: connBtnBg
                        palette.mid: Qt.lighter(connBtnBg, 1.15)
                        text: {
                            if (liveLogDialog.pendingSshAction) return "取消等待";
                            switch (sshTerminal.connectionState) {
                            case SshTerminal.Connecting:
                                return "连接中…";
                            case SshTerminal.Reconnecting:
                                return "重连中…";
                            case SshTerminal.Connected:
                                return "断开";
                            default:
                                return "连接";
                            }
                        }
                        onClicked: liveLogDialog.connectAction()
                    }

                    StyledButton {
                        objectName: "ssh-pause"
                        text: autoScroll ? "暂停跟随" : "继续跟随"
                        implicitWidth: 96
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: autoScroll ? "只停止自动滚动到底部，后台仍持续接收和更新日志。\n如需停止后台接收，请点击“停止刷屏”。"
                            : "回到最新日志，恢复自动跟随和新日志规则监测。\n如已停止刷屏，需点击“恢复接收”才能收到新日志。"
                        Accessible.description: ToolTip.text
                        onClicked: {
                            if (autoScroll) autoScroll = false;
                            else resumeFollowing();
                        }
                    }

                    StyledButton {
                        objectName: "ssh-stop-reception"
                        text: sshTerminal.receptionPaused ? "恢复接收" : "停止刷屏"
                        implicitWidth: 96
                        checked: sshTerminal.receptionPaused
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: sshTerminal.receptionPaused ? "恢复当前会话的后台日志读取，保留原来的跟随状态。\n远端或系统中积压的内容可能随后出现。"
                            : "停止当前会话的后台日志读取，保留 SSH 连接。\n不再接收新日志，终端输入暂不可用；再次点击可恢复接收。"
                        Accessible.description: ToolTip.text
                        onClicked: sshTerminal.receptionPaused = !sshTerminal.receptionPaused
                    }

                    StyledButton {
                        objectName: "ssh-clear"
                        text: "清屏"
                        implicitWidth: 96
                        onClicked: { monitorHit = null; monitorNotice = null; sshTerminal.clearBuffer(); }
                    }

                    Next.GlassCheckBox {
                        objectName: "ssh-copy-on-select"
                        text: "选中即复制"
                        checked: liveLogDialog.copyOnSelect
                        onToggled: liveLogDialog.copyOnSelect = checked
                        ToolTip.visible: hovered
                        ToolTip.text: "拖动选中文字，松开鼠标后自动复制"
                    }
                }
                Flickable {
                    id: connectionScroll
                    objectName: "ssh-connection-scroll"
                    visible: liveLogDialog.connectionConfigExpanded
                    Layout.fillWidth: true
                    Layout.preferredHeight: liveLogDialog.workspaceManaged
                        ? Math.min(connectionFields.implicitHeight, liveLogDialog.height * 0.6)
                        : connectionFields.implicitHeight + (connectionScrollBar.visible ? connectionScrollBar.height : 0)
                    contentWidth: connectionFields.width
                    contentHeight: connectionFields.height
                    flickableDirection: liveLogDialog.workspaceManaged ? Flickable.VerticalFlick : Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    ScrollBar.vertical: ScrollBar {
                        policy: liveLogDialog.workspaceManaged ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                    }
                    ScrollBar.horizontal: ScrollBar {
                        id: connectionScrollBar
                        policy: ScrollBar.AsNeeded
                        visible: connectionScroll.contentWidth > connectionScroll.width
                    }
                    Connections {
                        target: liveLogDialog.Window.window
                        function onActiveFocusItemChanged() {
                            const field = target.activeFocusItem
                            if (!field || field.parent !== connectionFields) return
                            if (liveLogDialog.workspaceManaged) {
                                if (field.y < connectionScroll.contentY) connectionScroll.contentY = field.y;
                                else if (field.y + field.height > connectionScroll.contentY + connectionScroll.height)
                                    connectionScroll.contentY = field.y + field.height - connectionScroll.height;
                                return;
                            }
                            if (field.x < connectionScroll.contentX)
                                connectionScroll.contentX = field.x
                            else if (field.x + field.width > connectionScroll.contentX + connectionScroll.width)
                                connectionScroll.contentX = field.x + field.width - connectionScroll.width
                        }
                    }
                GridLayout {
                    id: connectionFields
                    width: liveLogDialog.workspaceManaged ? connectionScroll.width : Math.max(implicitWidth, connectionScroll.width)
                    columns: liveLogDialog.workspaceManaged ? (connectionScroll.width >= 800 ? 4 : 2) : 100
                    columnSpacing: 8
                    rowSpacing: 8
                Label { text: "名称"; color: labelColor; font.pixelSize: 12 }
                StyledTextField {
                    objectName: "ssh-session-name"
                    Layout.preferredWidth: 110
                    text: liveLogDialog.sessionName
                    onTextEdited: liveLogDialog.sessionName = text
                    maximumLength: 80
                }
                Label {
                    text: "主机"
                    color: labelColor
                    font.pixelSize: 12
                }
                StyledTextField {
                    id: hostInput
                    objectName: "ssh-host"
                    enabled: !liveLogDialog.sessionActive && !liveLogDialog.pendingSshAction && !liveLogDialog.passwordOperationId
                    onTextEdited: liveLogDialog.targetEdited()
                    Layout.preferredWidth: 130
                    text: sshTerminal.host
                    placeholderText: "192.168.1.1"
                }

                Label {
                    text: "端口"
                    color: labelColor
                    font.pixelSize: 12
                }
                StyledTextField {
                    id: portInput
                    objectName: "ssh-port"
                    enabled: !liveLogDialog.sessionActive && !liveLogDialog.pendingSshAction && !liveLogDialog.passwordOperationId
                    onTextEdited: liveLogDialog.targetEdited()
                    Layout.preferredWidth: 52
                    text: sshTerminal.port
                    validator: IntValidator {
                        bottom: 1
                        top: 65535
                    }
                }

                Label {
                    text: "用户"
                    color: labelColor
                    font.pixelSize: 12
                }
                StyledTextField {
                    id: userInput
                    objectName: "ssh-username"
                    enabled: !liveLogDialog.sessionActive && !liveLogDialog.pendingSshAction && !liveLogDialog.passwordOperationId
                    onTextEdited: liveLogDialog.targetEdited()
                    Layout.preferredWidth: 90
                    text: sshTerminal.username
                    placeholderText: "root"
                }

                Label {
                    text: "密码"
                    color: labelColor
                    font.pixelSize: 12
                }
                StyledTextField {
                    id: pwdInput
                    objectName: "ssh-password-draft"
                    Layout.preferredWidth: 140
                    enabled: !liveLogDialog.sessionActive && !liveLogDialog.pendingSshAction && !liveLogDialog.passwordOperationId
                    text: liveLogDialog.secureCredentials ? "" : sshTerminal.password
                    echoMode: TextInput.Password
                    placeholderText: !liveLogDialog.secureCredentials ? "连接密码（随连接保存）" : !liveLogDialog.targetMatchesBackend() ? "新目标密码" : sshTerminal.credentialBusy ? "正在核验凭据" : sshTerminal.hasSavedPassword ? "已保存；留空使用" : "输入本次密码"
                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                }

                Label {
                    text: "编码"
                    color: labelColor
                    font.pixelSize: 12
                }
                StyledComboBox {
                    id: encodingCombo
                    objectName: "ssh-encoding"
                    Layout.fillWidth: liveLogDialog.workspaceManaged
                    Layout.minimumWidth: 112
                    Layout.preferredWidth: 112
                    model: ["UTF-8", "GB2312"]
                    Component.onCompleted: {
                        currentIndex = (sshTerminal.encoding === "GB2312" || sshTerminal.encoding === "GBK") ? 1 : 0;
                    }
                    onActivated: function (index) {
                        sshTerminal.encoding = model[index];
                    }
                }

                Label {
                    text: "缓存行"
                    color: labelColor
                    font.pixelSize: 12
                }
                StyledTextField {
                    id: maxLinesInput
                    objectName: "ssh-buffer-lines"
                    Layout.preferredWidth: 72
                    text: sshTerminal.maxBufferLines
                    // #4 连接中设为只读，防止用户修改后戟期不生效造成误解
                    readOnly: sshTerminal.connected || sshTerminal.connectionState === SshTerminal.Connecting || sshTerminal.connectionState === SshTerminal.Reconnecting
                    opacity: readOnly ? 0.5 : 1.0
                    validator: IntValidator {
                        bottom: 100
                        top: 200000
                    }
                }

                // 竖分隔
                Rectangle {
                    visible: !liveLogDialog.workspaceManaged
                    width: 1
                    height: 22
                    color: cardBorder
                    opacity: 0.7
                }

                Label {
                    text: "次数"
                    color: labelColor
                    font.pixelSize: 12
                    opacity: sshTerminal.autoReconnectEnabled ? 1.0 : 0.4
                }
                StyledTextField {
                    id: reconnectCountInput
                    objectName: "ssh-reconnect-count"
                    Layout.preferredWidth: 46
                    text: sshTerminal.reconnectMaxAttempts
                    validator: IntValidator {
                        bottom: 0
                        top: 20
                    }
                    // #4 连接中同时禁用修改
                    enabled: sshTerminal.autoReconnectEnabled && !sshTerminal.connected && sshTerminal.connectionState !== SshTerminal.Connecting && sshTerminal.connectionState !== SshTerminal.Reconnecting
                    opacity: enabled ? 1.0 : 0.4
                }

                Label {
                    text: "间隔(ms)"
                    color: labelColor
                    font.pixelSize: 12
                    opacity: sshTerminal.autoReconnectEnabled ? 1.0 : 0.4
                }
                StyledTextField {
                    id: reconnectIntervalInput
                    objectName: "ssh-reconnect-interval"
                    Layout.preferredWidth: 68
                    text: sshTerminal.reconnectIntervalMs
                    validator: IntValidator {
                        bottom: 500
                        top: 30000
                    }
                    // #4 连接中同时禁用修改
                    enabled: sshTerminal.autoReconnectEnabled && !sshTerminal.connected && sshTerminal.connectionState !== SshTerminal.Connecting && sshTerminal.connectionState !== SshTerminal.Reconnecting
                    opacity: enabled ? 1.0 : 0.4
                }

                Label {
                    text: "自动重连"
                    color: labelColor
                    font.pixelSize: 12
                }
                Next.GlassSwitch {
                    objectName: "ssh-auto-reconnect"
                    checked: sshTerminal.autoReconnectEnabled
                    onToggled: sshTerminal.autoReconnectEnabled = checked
                    palette.button: liveLogDialog.theme ? liveLogDialog.theme.controlBackground : "#ffffff"
                    palette.buttonText: liveLogDialog.theme ? liveLogDialog.theme.text : "#333333"
                    palette.highlight: focusAccentColor
                }

                    Item { visible: !liveLogDialog.workspaceManaged; Layout.fillWidth: true }
                }
                }
                RowLayout {
                    visible: liveLogDialog.connectionConfigExpanded && liveLogDialog.secureCredentials
                    Layout.fillWidth: true
                    spacing: 8
                    StyledButton {
                        objectName: "ssh-save-password"; visible: liveLogDialog.secureCredentials; text: "保存密码"
                        enabled: !liveLogDialog.sessionActive && !liveLogDialog.pendingSshAction && !liveLogDialog.passwordOperationId && pwdInput.text.length > 0
                        onClicked: liveLogDialog.beginSshAction("save")
                    }
                    StyledButton {
                        objectName: "ssh-clear-password"; visible: liveLogDialog.secureCredentials; text: "清除已存密码"
                        enabled: !liveLogDialog.sessionActive && !liveLogDialog.pendingSshAction && !liveLogDialog.passwordOperationId && !sshTerminal.credentialBusy && liveLogDialog.targetMatchesBackend() && sshTerminal.hasSavedPassword
                        onClicked: liveLogDialog.beginSshAction("clear")
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        Rectangle {
            objectName: "ssh-monitor-rule-bar"
            visible: liveLogDialog.monitorRuleLibrary !== null
            Layout.fillWidth: true; height: 44; radius: 6
            color: monitorHit ? Qt.rgba(monitorHitColor.r, monitorHitColor.g, monitorHitColor.b, 0.18) : cardBg
            border.color: monitorHit ? monitorHitColor : cardBorder
            RowLayout {
                anchors.fill: parent; anchors.margins: 6; spacing: 8
                Next.GlassCheckBox {
                    objectName: "ssh-monitor-enabled"; text: "启用规则监测"
                    checked: monitorEnabled; onToggled: liveLogDialog.monitorEnabled = checked
                }
                Next.GlassButton {
                    id: ruleSelectButton; objectName: "ssh-rule-select"
                    text: "选择规则（" + selectedMonitorRules.length + "）▾"; implicitHeight: 32
                    onClicked: {
                        const point = mapToItem(rulePicker.parent, 0, height + 6);
                        rulePicker.x = Math.max(8, Math.min(point.x, rulePicker.parent.width - rulePicker.width - 8));
                        rulePicker.y = Math.max(8, Math.min(point.y, rulePicker.parent.height - rulePicker.implicitHeight - 8));
                        rulePicker.open();
                    }
                }
                Label {
                    objectName: "ssh-monitor-status"
                    textFormat: Text.PlainText
                    Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight
                    text: monitorError || (!monitorEnabled ? "监测未开启 · 已选 " + selectedMonitorRules.length + " 条规则"
                        : !selectedMonitorRules.length ? "请选择规则"
                        : monitorHit ? "已命中：" + monitorHit.ruleName + " · " + monitorHit.time
                        : sshTerminal.receptionPaused ? "接收已停止，恢复接收后继续监测"
                        : !autoScroll ? "已暂停跟随，点击继续后监测新日志"
                        : monitorNotice ? "最近命中（仅标记）：" + monitorNotice.ruleName : "监测中 · " + selectedMonitorRules.length + " 条规则")
                    color: monitorHit && monitorHitColor.hslLightness >= 0.45 ? monitorHitColor : monitorError ? "#e8b76a" : labelColor; font.pixelSize: 12
                    ToolTip.visible: statusMouse.containsMouse
                    ToolTip.text: monitorEvent ? "点击查看命中原文；即使日志被缓存淘汰，仍保留本次快照。" : text
                    MouseArea { id: statusMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: monitorEvent ? Qt.PointingHandCursor : Qt.ArrowCursor; onClicked: if (monitorEvent) matchDetails.open() }
                }
                Label {
                    visible: !!monitorHit && liveLogDialog.width > 920
                    text: (sshTerminal.receptionPaused ? "接收已停止" : sshTerminal.connected ? "后台接收中" : "当前未连接")
                        + " · +" + Math.max(0, liveSelectionId - (monitorHit ? monitorHit.receivedLineId : liveSelectionId)) + " 行"
                    color: labelColor; font.pixelSize: 11
                }
            }
        }

        // ── 查找栏 ────────────────────────────────────────────────
        Rectangle {
            visible: findBarVisible
            Layout.fillWidth: true
            height: 38
            color: cardBg
            border.color: cardBorder
            radius: 6

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 6

                Text {
                    text: "🔍"
                    font.pixelSize: 14
                    color: labelColor
                }
                StyledTextField {
                    id: findInput
                    Layout.fillWidth: true
                    placeholderText: "输入关键字…"
                    text: searchKeyword
                    onTextChanged: {
                        searchKeyword = text;
                        refreshSearchResult();
                    }
                    onAccepted: jumpToSearch(true)
                }

                // 结果计数胶囊
                Rectangle {
                    width: resultCountLabel.implicitWidth + 16
                    height: 22
                    radius: 11
                    color: searchMatches.length > 0 ? successWeakBg : errorWeakBg
                    Label {
                        id: resultCountLabel
                        anchors.centerIn: parent
                        text: searchMatches.length === 0 ? "无结果" : (searchCursor + 1) + " / " + searchMatches.length
                        color: searchMatches.length > 0 ? successColor : errorColor
                        font.pixelSize: 11
                    }
                }

                StyledButton {
                    text: "↑"
                    onClicked: jumpToSearch(false)
                    implicitWidth: 36
                }
                StyledButton {
                    text: "↓"
                    onClicked: jumpToSearch(true)
                    implicitWidth: 36
                }
                StyledButton {
                    text: "✕"
                    implicitWidth: 36
                    onClicked: {
                        findBarVisible = false;
                        searchKeyword = "";
                        refreshSearchResult();
                        if (terminalInput)
                            terminalInput.forceActiveFocus();
                    }
                }
            }
        }

        // ── 终端输出区域 ──────────────────────────────────────────
        Rectangle {
            id: terminalPane
            objectName: "ssh-terminal-pane"
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"
            border.color: terminalBorder
            radius: 6
            clip: true

            RowLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 6

                Rectangle {
                    id: terminalMainPane
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: terminalBg
                    radius: 6
                    clip: true
                    focus: true

                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: terminalCapture.forceActiveFocus()
                    }

                    Keys.onPressed: function (event) {
                        if (routeTerminalKey(event))
                            return;
                    }

                    ListView {
                        id: logListView
                        objectName: "ssh-log-list"
                        anchors.fill: parent
                        model: sshTerminal.lineModel
                        clip: true
                        reuseItems: true
                        cacheBuffer: 80
                        spacing: 0
                        bottomMargin: 0

                        WheelHandler {
                            target: null
                            acceptedModifiers: Qt.platform.os === "osx" ? Qt.MetaModifier : Qt.ControlModifier
                            onWheel: function(event) {
                                if (!event.angleDelta.y) { event.accepted = false; return; }
                                liveLogDialog.captureLogViewport();
                                liveLogDialog.terminalFontSize = Math.max(8, Math.min(32,
                                    liveLogDialog.terminalFontSize + (event.angleDelta.y > 0 ? 1 : -1)));
                                logListView.forceLayout();
                                Qt.callLater(liveLogDialog.applyLogViewport);
                            }
                        }

                        delegate: Rectangle {
                            id: lineItem
                            objectName: "ssh-log-line-" + stableId
                            property real stableId: model && model.lineId !== undefined ? model.lineId : -1
                            readonly property string displayText: model && model.lineText !== undefined ? model.lineText : ""
                            readonly property color ruleColor: model && model.monitorColor ? model.monitorColor : "transparent"
                            readonly property var pausedHit: liveLogDialog.monitorHit && liveLogDialog.monitorHit.lineId === stableId ? liveLogDialog.monitorHit : null
                            readonly property bool wholeLine: pausedHit ? pausedHit.highlightWholeLine !== false : !model || model.monitorWholeLine !== false
                            property int rowIndex: index
                            onStableIdChanged: applyHexSelection()
                            onRowIndexChanged: applyHexSelection()
                            width: logListView.width
                            height: Math.max(charMeasurer.implicitHeight, lineText.implicitHeight, selectionLoader.item ? selectionLoader.item.implicitHeight : 0) + 4
                            color: wholeLine && pausedHit
                                ? Qt.rgba(monitorHitColor.r, monitorHitColor.g, monitorHitColor.b, 0.3)
                                : wholeLine && ruleColor.a > 0 ? Qt.rgba(ruleColor.r, ruleColor.g, ruleColor.b, 0.25)
                                : (searchKeyword !== "" && displayText.toLowerCase().indexOf(searchKeyword.toLowerCase()) >= 0) ? terminalMatchBg : "transparent"
                            Rectangle {
                                width: 3; height: parent.height; color: lineItem.ruleColor.a > 0 ? lineItem.ruleColor : monitorHitColor
                                visible: lineItem.ruleColor.a > 0 || liveLogDialog.monitorHit !== null && liveLogDialog.monitorHit.lineId === lineItem.stableId
                            }

                            MatchHighlights {
                                objectName: "ssh-log-match-highlights"
                                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 6
                                sourceText: lineItem.displayText
                                ranges: lineItem.wholeLine ? [] : lineItem.ruleColor.a > 0 ? model.monitorRanges || [] : lineItem.pausedHit ? lineItem.pausedHit.ranges || [] : []
                                markColor: lineItem.pausedHit ? monitorHitColor : lineItem.ruleColor
                            }

                            Text {
                                id: lineText
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 6
                                verticalAlignment: Text.AlignVCenter
                                text: selectionLoader.active ? "" : lineItem.displayText
                                visible: !selectionLoader.active
                                textFormat: Text.PlainText
                                // Paint the visible row; selection and parsing keep the complete model text.
                                wrapMode: Text.WrapAnywhere
                                maximumLineCount: 1
                                font: charMeasurer.font
                                clip: true
                                color: terminalText
                            }

                            Loader {
                                id: selectionLoader
                                anchors.fill: parent
                                anchors.leftMargin: 10; anchors.rightMargin: 6
                                active: liveLogDialog.selectionContains(lineItem.stableId)
                                    || lineItem.stableId === selectedLineId && selectedHexStart >= 0 && selectedHexEnd > selectedHexStart
                                onLoaded: lineItem.applyHexSelection()
                                sourceComponent: TextEdit {
                                    objectName: "ssh-selected-text"
                                    text: lineItem.displayText
                                    textFormat: TextEdit.PlainText
                                    readOnly: true; selectByMouse: false; persistentSelection: true
                                    verticalAlignment: Text.AlignVCenter; wrapMode: TextEdit.NoWrap
                                    font: charMeasurer.font
                                    color: terminalText; selectedTextColor: terminalText; selectionColor: "#446688"
                                }
                            }

                            function selectionPosition(contentX, contentY) {
                                if (!selectionLoader.active || !selectionLoader.item) return 0;
                                const point = logListView.contentItem.mapToItem(selectionLoader.item, contentX, contentY);
                                return selectionLoader.item.positionAt(point.x, point.y);
                            }
                            Connections {
                                target: liveLogDialog
                                function onTextSelectionRangeChanged() { lineItem.applyHexSelection(); }
                            }
                            function applyHexSelection() {
                                if (!sshTerminal || !selectionLoader || !selectionLoader.item || index < 0) return;
                                if (liveLogDialog.applyTextSelection(selectionLoader.item, stableId)) return;
                                const offset = sshTerminal.lineSegmentOffset(index);
                                const start = Math.max(0, selectedHexStart - offset);
                                const end = Math.min(selectionLoader.item.length, selectedHexEnd - offset);
                                if (stableId === selectedLineId && selectedHexStart >= 0 && end > start)
                                    selectionLoader.item.select(start, end);
                                else
                                    selectionLoader.item.deselect();
                            }

                            Text {
                                visible: model && (model.continuation === true || model.continues === true)
                                text: "↳"; color: "#9aa4b2"; font.pixelSize: 11
                                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                            }

                        }

                        footer: Item {
                            width: logListView.width
                            height: sshTerminal.connected || sshTerminal.liveLine !== "" ? charMeasurer.implicitHeight + 4 : 0
                            property real cursorX: liveCursorMetrics.advanceWidth + sshTerminal.cursorPadding * liveLogDialog.charWidth
                            property real offsetX: Math.max(0, cursorX - width + 24)
                            property real caretX: 10 + cursorX - offsetX
                            readonly property var pausedHit: liveLogDialog.monitorHit && liveLogDialog.monitorHit.lineId === liveSelectionId ? liveLogDialog.monitorHit : null
                            readonly property bool wholeLine: pausedHit ? pausedHit.highlightWholeLine !== false : sshTerminal.liveMonitorWholeLine
                            readonly property color markColor: sshTerminal.liveMonitorColor || (pausedHit ? monitorHitColor : "transparent")
                            Rectangle {
                                objectName: "ssh-monitor-live-hit"
                                anchors.fill: parent
                                visible: parent.wholeLine && parent.markColor.a > 0
                                color: Qt.rgba(parent.markColor.r, parent.markColor.g, parent.markColor.b, 0.3)
                            }
                            Rectangle { width: 3; height: parent.height; color: parent.markColor; visible: parent.markColor.a > 0 }
                            MatchHighlights {
                                objectName: "ssh-live-match-highlights"
                                x: 10 - parent.offsetX; width: parent.width; height: parent.height
                                sourceText: sshTerminal.liveLine
                                ranges: parent.wholeLine ? [] : parent.pausedHit ? parent.pausedHit.ranges || [] : sshTerminal.liveMonitorRanges
                                markColor: parent.markColor
                            }
                            TextMetrics {
                                id: liveCursorMetrics
                                font: charMeasurer.font
                                text: sshTerminal.liveLine.slice(0, sshTerminal.cursorPosition)
                            }
                            function selectionPosition(contentX, contentY) {
                                if (!liveSelectionLoader.active || !liveSelectionLoader.item) return 0;
                                const point = logListView.contentItem.mapToItem(liveSelectionLoader.item, contentX, contentY);
                                return liveSelectionLoader.item.positionAt(point.x, point.y);
                            }
                            Connections {
                                target: liveLogDialog
                                function onTextSelectionRangeChanged() {
                                    liveLogDialog.applyTextSelection(liveSelectionLoader.item, liveLogDialog.liveSelectionId);
                                }
                            }
                            Loader {
                                id: liveSelectionLoader
                                x: 10 - parent.offsetX; width: parent.width; height: parent.height
                                active: liveLogDialog.selectionContains(liveLogDialog.liveSelectionId)
                                onLoaded: liveLogDialog.applyTextSelection(item, liveLogDialog.liveSelectionId)
                                sourceComponent: TextEdit {
                                    objectName: "ssh-selected-live-text"
                                    text: sshTerminal.liveLine; textFormat: TextEdit.PlainText
                                    readOnly: true; selectByMouse: false; persistentSelection: true
                                    font: charMeasurer.font; color: terminalText
                                    selectedTextColor: terminalText; selectionColor: "#446688"
                                    wrapMode: TextEdit.NoWrap
                                }
                            }
                            Text {
                                objectName: "ssh-terminal-live-line"
                                visible: !liveSelectionLoader.active
                                x: 10 - parent.offsetX
                                text: sshTerminal.liveLine
                                textFormat: Text.PlainText
                                font: charMeasurer.font
                                color: terminalText
                            }
                            Rectangle {
                                objectName: "ssh-terminal-caret"
                                x: parent.caretX; width: 2; height: charMeasurer.implicitHeight
                                color: terminalText
                                visible: sshTerminal.connected && terminalCapture.activeFocus && liveLogDialog.visible
                                SequentialAnimation on opacity {
                                    running: sshTerminal.connected && terminalCapture.activeFocus && liveLogDialog.visible
                                    loops: Animation.Infinite
                                    PropertyAction { value: 1 }
                                    PauseAnimation { duration: 600 }
                                    PropertyAction { value: 0 }
                                    PauseAnimation { duration: 600 }
                                }
                            }
                        }

                        ScrollBar.vertical: ScrollBar {
                            id: logVerticalBar
                            policy: ScrollBar.AsNeeded
                        }
                        ScrollBar.horizontal: ScrollBar {
                            id: logHorizontalBar
                            policy: ScrollBar.AsNeeded
                        }
                    }

                    MouseArea {
                        id: selectionArea
                        objectName: "ssh-text-selection-area"
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        preventStealing: true
                        property bool hexDoubleClick: false
                        onWheel: function(wheel) { wheel.accepted = false; }
                        onPressed: function(mouse) {
                            if (logVerticalBar.visible && mouse.x >= width - logVerticalBar.width
                                    || logHorizontalBar.visible && mouse.y >= height - logHorizontalBar.height) {
                                mouse.accepted = false;
                                return;
                            }
                            if (mouse.button === Qt.RightButton) {
                                if (!liveLogDialog.hasTextSelection) {
                                    const point = mapToItem(logListView.contentItem, mouse.x, mouse.y);
                                    const row = logListView.indexAt(1, point.y);
                                    if (row >= 0) liveLogDialog.selectLineAndExtractHex(row);
                                }
                                terminalContextMenu.popup();
                                return;
                            }
                            hexDoubleClick = false;
                            liveLogDialog.clearTextSelection();
                            selectedLineId = -1; selectedLineIndex = -1;
                            selectedHexStart = -1; selectedHexEnd = -1; selectedHexText = "";
                            const point = liveLogDialog.selectionPoint(mouse.x, mouse.y);
                            if (point) {
                                liveLogDialog.textSelectionAnchor = point;
                                liveLogDialog.textSelectionFocus = point;
                                liveLogDialog.selectionPointerDown = true;
                            }
                        }
                        onPositionChanged: if (liveLogDialog.selectionPointerDown) liveLogDialog.extendTextSelection()
                        onReleased: function(mouse) {
                            if (mouse.button === Qt.LeftButton && hexDoubleClick) {
                                hexDoubleClick = false;
                                liveLogDialog.selectionPointerDown = false;
                                if (liveLogDialog.copyOnSelect && liveLogDialog.hasTextSelection) liveLogDialog.copyTextSelection();
                                return;
                            }
                            if (mouse.button !== Qt.LeftButton || !liveLogDialog.selectionPointerDown) return;
                            liveLogDialog.extendTextSelection();
                            liveLogDialog.selectionPointerDown = false;
                            if (liveLogDialog.hasTextSelection) {
                                if (liveLogDialog.copyOnSelect) liveLogDialog.copyTextSelection();
                            } else {
                                const row = sshTerminal.indexForLineId(liveLogDialog.textSelectionAnchor.id);
                                liveLogDialog.clearTextSelection();
                                if (row >= 0) liveLogDialog.selectLineAndExtractHex(row);
                            }
                        }
                        onCanceled: { liveLogDialog.selectionPointerDown = false; hexDoubleClick = false; }
                        onDoubleClicked: function(mouse) {
                            hexDoubleClick = true;
                            liveLogDialog.selectHexAt(mouse.x, mouse.y);
                        }
                    }
                    Timer {
                        interval: 40; repeat: true
                        running: liveLogDialog.selectionPointerDown && (selectionArea.mouseY < 0 || selectionArea.mouseY >= selectionArea.height)
                        onTriggered: {
                            const delta = selectionArea.mouseY < 0 ? -charMeasurer.implicitHeight : charMeasurer.implicitHeight;
                            logListView.contentY = Math.max(logListView.originY, Math.min(
                                logListView.originY + Math.max(0, logListView.contentHeight - logListView.height), logListView.contentY + delta));
                            liveLogDialog.extendTextSelection();
                        }
                    }

                    TextInput {
                        id: terminalCapture
                        objectName: "ssh-terminal-input"
                        enabled: sshTerminal.connected && !sshTerminal.receptionPaused
                        focus: true
                        x: logListView.footerItem ? logListView.footerItem.caretX : 10
                        y: Math.max(0, Math.min(terminalMainPane.height - height,
                            logListView.footerItem ? logListView.footerItem.y - logListView.contentY : 0))
                        width: Math.max(liveLogDialog.charWidth * 2, terminalMainPane.width - x - 10)
                        height: charMeasurer.implicitHeight + 4
                        font: charMeasurer.font
                        color: terminalText
                        cursorVisible: false
                        clip: true
                        activeFocusOnTab: false
                        inputMethodHints: Qt.ImhNoPredictiveText
                        Accessible.name: liveLogDialog.sessionName + " SSH 终端"
                        onTextEdited: {
                            const committed = text;
                            clear();
                            liveLogDialog.sendTerminalText(committed);
                        }
                        Keys.priority: Keys.BeforeItem
                        Keys.onPressed: event => liveLogDialog.routeTerminalKey(event)
                    }

                }

            }
        }

        Popup {
            id: confirmDeleteCmdPopup
            width: 280
            height: 132
            modal: true
            focus: true
            x: (liveLogDialog.width - width) / 2
            y: (liveLogDialog.height - height) / 2
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

            background: Rectangle {
                radius: 8
                color: cardBg
                border.color: cardBorder
                border.width: 1
            }

            contentItem: ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    text: liveLogDialog.customCmdPendingDeleteName === "" ? "确认删除该快捷命令？" : ("确认删除快捷命令「" + liveLogDialog.customCmdPendingDeleteName + "」？")
                    color: theme ? theme.text : "#333333"
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                }

                Item {
                    Layout.fillHeight: true
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 8

                    StyledButton {
                        text: "取消"
                        implicitWidth: 64
                        onClicked: confirmDeleteCmdPopup.close()
                    }

                    StyledButton {
                        text: "删除"
                        implicitWidth: 64
                        onClicked: {
                            if (liveLogDialog.customCmdPendingDeleteName !== "")
                                liveLogDialog.removeCustomCommandByName(liveLogDialog.customCmdPendingDeleteName);
                            confirmDeleteCmdPopup.close();
                        }
                    }
                }
            }

            onClosed: liveLogDialog.customCmdPendingDeleteName = ""
        }

        RowLayout {
            objectName: "ssh-command-editor"
            visible: liveLogDialog.commandManagementExpanded
            Layout.fillWidth: true
            spacing: 4

            StyledTextField {
                id: customCmdNameInput
                objectName: "ssh-command-name"
                Layout.preferredWidth: 92
                placeholderText: "名称"
                onAccepted: customCmdContentInput.forceActiveFocus()
            }

            StyledTextField {
                id: customCmdContentInput
                objectName: "ssh-command-content"
                Layout.preferredWidth: 150
                placeholderText: "命令内容"
                onAccepted: {
                    liveLogDialog.addCustomCommand(customCmdNameInput.text, customCmdContentInput.text);
                    customCmdNameInput.text = "";
                    customCmdContentInput.text = "";
                    if (liveLogDialog.terminalInput)
                        liveLogDialog.terminalInput.forceActiveFocus();
                }
            }

            StyledButton {
                objectName: "ssh-command-add"
                text: "添加"
                implicitWidth: 72
                onClicked: {
                    liveLogDialog.addCustomCommand(customCmdNameInput.text, customCmdContentInput.text);
                    customCmdNameInput.text = "";
                    customCmdContentInput.text = "";
                    if (liveLogDialog.terminalInput)
                        liveLogDialog.terminalInput.forceActiveFocus();
                }
            }

            Item { Layout.fillWidth: true }
        }

        // ── 底部工具栏 ────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            // 自定义命令区
            RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 4

                Label {
                    text: "快捷命令"
                    color: labelColor
                    font.pixelSize: 12
                }

                StyledButton {
                    objectName: "ssh-command-management-toggle"
                    text: liveLogDialog.commandManagementExpanded ? "▾ 收起管理" : "▸ 管理"
                    Accessible.name: "快捷命令管理"
                    Accessible.description: liveLogDialog.commandManagementExpanded ? "已展开" : "已折叠"
                    onClicked: liveLogDialog.commandManagementExpanded = !liveLogDialog.commandManagementExpanded
                }

                Flickable {
                    objectName: "ssh-command-buttons"
                    Layout.preferredWidth: 280
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    contentWidth: customCmdRow.implicitWidth
                    contentHeight: 30
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: contentWidth > width

                    Row {
                        id: customCmdRow
                        spacing: 4

                        Repeater {
                            model: liveLogDialog.customCommands
                            delegate: Item {
                                width: customCmdBtn.implicitWidth
                                height: 30

                                StyledButton {
                                    id: customCmdBtn
                                    objectName: "ssh-command-run-" + index
                                    anchors.fill: parent
                                    text: modelData.name
                                    implicitWidth: Math.max(60, Math.min(160, modelData.name.length * 14 + 32))
                                    enabled: sshTerminal.connected && !sshTerminal.receptionPaused
                                    onClicked: liveLogDialog.sendQuickCommand(modelData.command)
                                }

                                Rectangle {
                                    objectName: "ssh-command-delete-" + index
                                    visible: liveLogDialog.commandManagementExpanded
                                    width: 14
                                    height: 14
                                    radius: 7
                                    anchors.top: parent.top
                                    anchors.right: parent.right
                                    anchors.topMargin: 1
                                    anchors.rightMargin: 1
                                    z: 2
                                    color: closeCmdArea.containsMouse ? errorColor : mutedColor
                                    border.color: cardBg
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "×"
                                        color: "#ffffff"
                                        font.pixelSize: 10
                                        font.bold: true
                                    }

                                    MouseArea {
                                        id: closeCmdArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: function (mouse) {
                                            liveLogDialog.requestRemoveCustomCommand(modelData.name);
                                            mouse.accepted = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    visible: liveLogDialog.customCommands.length === 0
                    text: "点击「管理」添加常用命令"
                    color: theme ? theme.subText : "#888888"
                    font.pixelSize: 12
                }
            }

            // 状态指示器
            Row {
                spacing: 8
                Layout.alignment: Qt.AlignVCenter

                // 胶囊：圆点 + 连接状态
                Rectangle {
                    height: 26
                    width: dotText.implicitWidth + 28
                    radius: 13
                    color: {
                        if (sshTerminal.connected)
                            return successWeakBg;
                        if (sshTerminal.connectionState === SshTerminal.Connecting || sshTerminal.connectionState === SshTerminal.Reconnecting)
                            return warningWeakBg;
                        return neutralWeakBg;
                    }

                    Row {
                        anchors.centerIn: parent
                        spacing: 6

                        Rectangle {
                            width: 8
                            height: 8
                            radius: 8
                            anchors.verticalCenter: parent.verticalCenter
                            color: {
                                if (sshTerminal.connected)
                                    return successColor;
                                if (sshTerminal.connectionState === SshTerminal.Connecting || sshTerminal.connectionState === SshTerminal.Reconnecting)
                                    return warningColor;
                                if (sshTerminal.connectionState === SshTerminal.Failed)
                                    return errorColor;
                                return mutedColor;
                            }
                            // #6 修复动画停止后 opacity 残留问题：
                            // 用中间属性 blinkOpacity 驱动闪烁，将 opacity 绑定到状态-----
                            // 状态结束时 opacity 自动回到 1.0，而非停留在动画中断当前分数。
                            id: connectionDot
                            property bool isBlinking: sshTerminal.connectionState === SshTerminal.Connecting || sshTerminal.connectionState === SshTerminal.Reconnecting
                            property real blinkOpacity: 1.0
                            opacity: isBlinking ? blinkOpacity : 1.0
                            Behavior on opacity {
                                NumberAnimation {
                                    duration: 200
                                }
                            }
                            SequentialAnimation on blinkOpacity {
                                running: connectionDot.isBlinking
                                loops: Animation.Infinite
                                NumberAnimation {
                                    to: 0.2
                                    duration: 600
                                }
                                NumberAnimation {
                                    to: 1.0
                                    duration: 600
                                }
                            }
                        }

                        Text {
                            id: dotText
                            anchors.verticalCenter: parent.verticalCenter
                            text: stateText()
                            color: {
                                if (sshTerminal.connected)
                                    return successColor;
                                if (sshTerminal.connectionState === SshTerminal.Connecting || sshTerminal.connectionState === SshTerminal.Reconnecting)
                                    return warningColor;
                                if (sshTerminal.connectionState === SshTerminal.Failed)
                                    return errorColor;
                                return mutedColor;
                            }
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }
                }

                // 附加状态信息（仅在与连接状态文字不同时才显示）
                Text {
                    visible: sshTerminal.statusMessage !== "" && sshTerminal.statusMessage !== stateText()
                    anchors.verticalCenter: parent.verticalCenter
                    text: sshTerminal.statusMessage
                    color: theme ? theme.subText : "#888888"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }
            }
        }

        Label {
            objectName: "ssh-credential-status"; visible: liveLogDialog.secureCredentials || liveLogDialog.passwordNotice !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
            text: liveLogDialog.passwordNotice || sshTerminal.credentialError || (sshTerminal.credentialBusy ? "正在核验安全凭据…" : !liveLogDialog.targetMatchesBackend() ? "目标已修改，将重新核验凭据。" : sshTerminal.hasSavedPassword ? "当前目标已保存密码，不回显原密码。" : "当前目标未保存密码。")
            color: liveLogDialog.passwordNoticeError || sshTerminal.credentialError ? errorColor : (theme ? theme.subText : "#888888")
        }

        // ── 快捷键提示栏 ──────────────────────────────────────────
        Label {
            id: statusHint
            Layout.fillWidth: true
            text: sshTerminal.connected ? "点击终端直接输入，按键发送到远端  ·  Ctrl+Shift+F 查找  ·  Ctrl+Shift+C 复制行  ·  Ctrl+Shift+A 复制全部" : "尚未连接  —  填写连接参数后点击「连接」"
            color: theme ? theme.subText : "#888"
            font.pixelSize: 11
            elide: Text.ElideRight
            topPadding: 1
            bottomPadding: 2
        }
    }
}
