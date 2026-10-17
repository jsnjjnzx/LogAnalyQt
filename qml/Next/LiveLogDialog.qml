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
    property var cmdInput: null
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
            sshTerminal.host = hostInput.text; sshTerminal.port = parseInt(portInput.text) || 22
            sshTerminal.username = userInput.text; sshTerminal.password = pwdInput.text
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
        pwdInput.text = ""
        sshTerminal.disconnectFromHost()
    }
    Component.onDestruction: closeSshSession()


    property bool autoScroll: true
    property int selectedLineIndex: -1
    property var cmdHistory: []
    property int historyIndex: -1
    property string pendingInput: ""
    property var customCommands: []
    property bool customCommandsLoaded: false
    property string customCmdPendingDeleteName: ""
    // #9 命令历史上限提取为属性，方便后续调整，不再硬编码 200
    readonly property int cmdHistoryMaxSize: 200

    property bool findBarVisible: false
    property string searchKeyword: ""
    // 注意：searchMatches 赋值时必须整体替换新数组，不得直接 push()，
    // 否则 QML 属性系统检测不到变化，UI 绑定不会刷新。（#2）
    property var searchMatches: []
    property int searchCursor: -1
    // #1 增量搜索：记录上次执行全量/增量搜索时的行号，避免 onLineCountChanged 中重复全量遍历
    property int m_lastSearchedLineCount: 0
    property int selectedHexStart: -1
    property int selectedHexEnd: -1
    property string selectedHexText: ""
    property bool parsePanelVisible: false
    property real parsePanelWidth: 0
    readonly property real parsePanelMinWidth: 380
    readonly property real parsePanelMaxRatio: 0.65

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
        font.pixelSize: 13  // #10 与终端文本保持同一字体规格，确保 charWidth 计算准确
        text: "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    }
    readonly property real charWidth: charMeasurer.contentWidth / charMeasurer.text.length

    function openDialog() { requested(); }



    function stateText() {
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
            m_lastSearchedLineCount = 0;
            return;
        }
        // 注意：必须整体赋值新数组（不得用 push），否则不触发 QML 属性变更通知 (#2)
        searchMatches = sshTerminal.findLineIndexes(searchKeyword, false);
        // 记录本次全量搜索的行数基准，后续增量搜索从此处接续
        m_lastSearchedLineCount = sshTerminal.currentLineCount;
        if (searchMatches.length === 0) {
            searchCursor = -1;
            return;
        }
        if (searchCursor < 0 || searchCursor >= searchMatches.length)
            searchCursor = 0;
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
        var targetIndex = searchMatches[searchCursor];
        logListView.positionViewAtIndex(targetIndex, ListView.Center);
        selectedLineIndex = targetIndex;
    }

    function prevWordStart(text, pos) {
        var index = Math.max(0, Math.min(pos, text.length));
        while (index > 0 && /\s/.test(text.charAt(index - 1)))
            index--;
        while (index > 0 && !/\s/.test(text.charAt(index - 1)))
            index--;
        return index;
    }

    function nextWordStart(text, pos) {
        var index = Math.max(0, Math.min(pos, text.length));
        while (index < text.length && !/\s/.test(text.charAt(index)))
            index++;
        while (index < text.length && /\s/.test(text.charAt(index)))
            index++;
        return index;
    }

    function extractHexSegmentFromLine(lineText) {
        if (!lineText || lineText.length === 0)
            return null;

        var hexPattern = /[0-9A-Fa-f]+(?:[ \t]+[0-9A-Fa-f]+)*/g;
        var best = null;
        var match;

        while ((match = hexPattern.exec(lineText)) !== null) {
            var rawText = match[0];
            var hexOnly = rawText.replace(/[ \t]/g, "");
            if (hexOnly.length < 10 || hexOnly.length % 2 !== 0)
                continue;

            if (!best || hexOnly.length > best.hexOnly.length) {
                best = {
                    start: match.index,
                    end: match.index + rawText.length,
                    hexOnly: hexOnly
                };
            }
        }

        if (!best)
            return null;

        var formatted = "";
        for (var i = 0; i < best.hexOnly.length; i += 2) {
            if (i > 0)
                formatted += " ";
            formatted += best.hexOnly.substr(i, 2).toUpperCase();
        }

        best.formattedHex = formatted;
        return best;
    }

    function applyHexSelectionOnCurrentLine() {
        if (selectedLineIndex < 0)
            return;
        var item = logListView.itemAtIndex(selectedLineIndex);
        if (item && item["applyHexSelection"])
            item["applyHexSelection"]();
    }

    function selectLineAndExtractHex(lineIndex) {
        var oldIndex = selectedLineIndex;
        selectedLineIndex = lineIndex;
        selectedHexStart = -1;
        selectedHexEnd = -1;
        selectedHexText = "";

        var lineText = sshTerminal.lineAt(lineIndex);
        var extracted = extractHexSegmentFromLine(lineText);
        if (extracted) {
            selectedHexStart = extracted.start;
            selectedHexEnd = extracted.end;
            selectedHexText = extracted.formattedHex;
        }

        Qt.callLater(function () {
            if (oldIndex >= 0 && oldIndex !== selectedLineIndex) {
                var oldItem = logListView.itemAtIndex(oldIndex);
                if (oldItem && oldItem["applyHexSelection"])
                    oldItem["applyHexSelection"]();
            }
            applyHexSelectionOnCurrentLine();
        });
    }

    function parseSelectedHexWithProtocol(protocolType) {
        if (!backend || selectedHexText === "")
            return;

        var oldProtocolType = backend.protocolType;
        backend.protocolType = protocolType;
        backend.parseHex(selectedHexText, encodingCombo.currentText, 3);
        backend.protocolType = oldProtocolType;

        parsePanelVisible = true;
        if (parsePanelWidth <= 0)
            parsePanelWidth = Math.max(parsePanelMinWidth, terminalPane.width * 0.42);
        Qt.callLater(function () {
            popupCols.autoFit();
        });
    }

    function clampParsePanelWidth(widthValue) {
        var maxWidth = Math.max(parsePanelMinWidth, terminalPane.width * parsePanelMaxRatio);
        return Math.max(parsePanelMinWidth, Math.min(widthValue, maxWidth));
    }

    function copySelectedHexToClipboard() {
        if (selectedHexText === "")
            return;
        clipboardHelper.text = selectedHexText;
        clipboardHelper.selectAll();
        clipboardHelper.copy();
    }

    function handleCmdCtrlShortcut(event) {
        if (!sshTerminal.connected || !cmdInput)
            return false;
        if (!(event.modifiers & Qt.ControlModifier) || (event.modifiers & Qt.AltModifier))
            return false;

        cmdInput.forceActiveFocus();

        if (event.key === Qt.Key_C) {
            // #5 有文本选中时执行系统复制，无选中时发送 SIGINT
            if (cmdInput.selectedText.length > 0) {
                cmdInput.copy();
            } else {
                sshTerminal.sendRawBytes("\x03");
            }
            event.accepted = true;
            return true;
        }
        if (event.key === Qt.Key_V) {
            cmdInput.insert(cmdInput.cursorPosition, sshTerminal.clipboardText());
            event.accepted = true;
            return true;
        }
        if (event.key === Qt.Key_A) {
            cmdInput.cursorPosition = 0;
            event.accepted = true;
            return true;
        }
        if (event.key === Qt.Key_E) {
            cmdInput.cursorPosition = cmdInput.length;
            event.accepted = true;
            return true;
        }
        if (event.key === Qt.Key_Left) {
            cmdInput.cursorPosition = prevWordStart(cmdInput.text, cmdInput.cursorPosition);
            event.accepted = true;
            return true;
        }
        if (event.key === Qt.Key_Right) {
            cmdInput.cursorPosition = nextWordStart(cmdInput.text, cmdInput.cursorPosition);
            event.accepted = true;
            return true;
        }
        if (event.key === Qt.Key_Backspace) {
            var start = prevWordStart(cmdInput.text, cmdInput.cursorPosition);
            if (start < cmdInput.cursorPosition)
                cmdInput.remove(start, cmdInput.cursorPosition);
            event.accepted = true;
            return true;
        }
        if (event.key === Qt.Key_Delete) {
            var end = nextWordStart(cmdInput.text, cmdInput.cursorPosition);
            if (end > cmdInput.cursorPosition)
                cmdInput.remove(cmdInput.cursorPosition, end);
            event.accepted = true;
            return true;
        }
        return false;
    }

    function routeKeyToCmdInput(event) {
        if (!sshTerminal.connected || !cmdInput)
            return false;
        if (handleCmdCtrlShortcut(event))
            return true;
        if (event.modifiers & Qt.AltModifier)
            return false;
        if (event.modifiers & Qt.ControlModifier)
            return false;

        cmdInput.forceActiveFocus();

        if (event.key === Qt.Key_Backspace) {
            if (cmdInput.cursorPosition > 0)
                cmdInput.remove(cmdInput.cursorPosition - 1, cmdInput.cursorPosition);
            event.accepted = true;
            return true;
        }
        if (event.key === Qt.Key_Delete) {
            if (cmdInput.cursorPosition < cmdInput.length)
                cmdInput.remove(cmdInput.cursorPosition, cmdInput.cursorPosition + 1);
            event.accepted = true;
            return true;
        }
        if (event.key === Qt.Key_Left) {
            cmdInput.cursorPosition = Math.max(0, cmdInput.cursorPosition - 1);
            event.accepted = true;
            return true;
        }
        if (event.key === Qt.Key_Right) {
            cmdInput.cursorPosition = Math.min(cmdInput.length, cmdInput.cursorPosition + 1);
            event.accepted = true;
            return true;
        }
        if (event.key === Qt.Key_Home) {
            cmdInput.cursorPosition = 0;
            event.accepted = true;
            return true;
        }
        if (event.key === Qt.Key_End) {
            cmdInput.cursorPosition = cmdInput.length;
            event.accepted = true;
            return true;
        }
        if (event.text && event.text.length > 0 && event.text.charCodeAt(0) >= 32) {
            cmdInput.insert(cmdInput.cursorPosition, event.text);
            logListView.positionViewAtEnd();
            event.accepted = true;
            return true;
        }
        return false;
    }

    function sendCommandAndTrackHistory(cmd) {
        if (!sshTerminal.connected)
            return;
        sshTerminal.sendCommand(cmd);
        if (cmd !== "" && (cmdHistory.length === 0 || cmdHistory[cmdHistory.length - 1] !== cmd)) {
            var h = cmdHistory.slice();
            h.push(cmd);
            if (h.length > cmdHistoryMaxSize)
                h = h.slice(h.length - cmdHistoryMaxSize);
            cmdHistory = h;
        }
        historyIndex = -1;
        pendingInput = "";
        logListView.positionViewAtEnd();
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
        customCommands = list;
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
        customCommands = list;
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
        customCommands = normalized;
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
        loadCustomCommands();
        customCommandsLoaded = true;
    }

    onBackendChanged: {
        customCommandsLoaded = false;
        ensureCustomCommandsLoaded();
    }

    onVisibleChanged: {
        if (visible) ensureCustomCommandsLoaded()
        else if (pendingHostKey) respondHostKey(false)
    }

    onClosing: closeSshSession()



    Item { id: titleBar; height: 0; width: parent.width }


    SshTerminal {
        id: sshTerminal
        objectName: "ssh-terminal-backend"
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
            if (!liveLogDialog.visible) { sshTerminal.confirmHostKey(requestId, fingerprint, false); return }
            if (liveLogDialog.pendingHostKey) liveLogDialog.respondHostKey(false)
            liveLogDialog.pendingHostKey = {host:host, port:port, algorithm:algorithm, fingerprint:fingerprint, requestId:requestId}
            hostKeyDialog.open()
        }
        onHostKeyPendingChanged: {
            if (!hostKeyPending) { liveLogDialog.pendingHostKey = null; hostKeyDialog.close() }
        }

        onConnectedChanged: {
            if (connected) {
                Qt.callLater(function () {
                    logListView.positionViewAtEnd();
                    if (liveLogDialog.cmdInput)
                        liveLogDialog.cmdInput.forceActiveFocus();
                });
            }
        }

        onSharedBufferOverflowed: {
            statusHint.text = "输出过快，已自动丢弃最早缓冲数据";
        }

        onLineCountChanged: {
            if (autoScroll)
                logListView.positionViewAtEnd();
            if (searchKeyword !== "") {
                var newCount = sshTerminal.currentLineCount;
                if (m_lastSearchedLineCount <= 0 || m_lastSearchedLineCount >= newCount) {
                    // 关键字初帶入或缓冲区被清除，必须全量刷新
                    refreshSearchResult();
                } else {
                    // 增量搜索：只检查新增行，避免对全部缓冲行重复遍历 (#1)
                    var newMatches = sshTerminal.findLineIndexesInRange(searchKeyword, m_lastSearchedLineCount, newCount - 1, false);
                    if (newMatches.length > 0) {
                        // 必须整体替换数组才能触发 QML 属性更新 (#2)
                        var merged = searchMatches.slice();
                        for (var i = 0; i < newMatches.length; i++)
                            merged.push(newMatches[i]);
                        searchMatches = merged;
                    }
                }
                m_lastSearchedLineCount = newCount;
            }
        }
    }

    Next.GlassDialog {
        id: hostKeyDialog; objectName: "ssh-host-key-dialog"
        parent: Overlay.overlay; anchors.centerIn: parent; width: Math.min(620, liveLogDialog.width - 30)
        modal: true; title: "确认 SSH 主机密钥"; standardButtons: Dialog.NoButton; closePolicy: Popup.CloseOnEscape
        onClosed: if (liveLogDialog.pendingHostKey) liveLogDialog.respondHostKey(false)
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

        MenuItem {
            text: "自动检测解析"
            enabled: selectedHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.Auto)
        }
        MenuItem {
            text: "按 DL/T 698 解析"
            enabled: selectedHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.DLT698)
        }
        MenuItem {
            text: "按 IEC 101 解析"
            enabled: selectedHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.IEC101)
        }
        MenuItem {
            text: "按 IEC 104 解析"
            enabled: selectedHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.IEC104)
        }
        MenuItem {
            text: "按 应用层消息 解析"
            enabled: selectedHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.AppMsg)
        }
        MenuItem {
            text: "按 模块消息 解析"
            enabled: selectedHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.ModuleMsg)
        }
        MenuItem {
            text: "按 Q/GDW 376.2 解析"
            enabled: selectedHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.GDW3762)
        }
        MenuItem {
            text: "按 DL/T 645 解析"
            enabled: selectedHexText !== ""
            onTriggered: parseSelectedHexWithProtocol(protocolId.DLT645)
        }
        MenuSeparator {}
        MenuItem {
            text: "复制Hex串"
            enabled: selectedHexText !== ""
            onTriggered: copySelectedHexToClipboard()
        }
    }

    Shortcut {
        enabled: liveLogDialog.visible
        sequences: [StandardKey.Find]
        onActivated: {
            findBarVisible = true;
            findInput.forceActiveFocus();
            findInput.selectAll();
        }
    }
    Shortcut {
        enabled: liveLogDialog.visible
        sequence: "F3"
        onActivated: jumpToSearch(true)
    }
    Shortcut {
        enabled: liveLogDialog.visible
        sequence: "Shift+F3"
        onActivated: jumpToSearch(false)
    }
    Shortcut {
        enabled: liveLogDialog.visible
        sequence: "Ctrl+L"
        onActivated: sshTerminal.clearBuffer()
    }
    Shortcut {
        enabled: liveLogDialog.visible
        sequence: "Ctrl+Shift+C"
        onActivated: {
            if (selectedLineIndex >= 0)
                sshTerminal.copyLineToClipboard(selectedLineIndex);
        }
    }
    Shortcut {
        enabled: liveLogDialog.visible
        sequence: "Ctrl+Shift+A"
        onActivated: sshTerminal.copyBufferToClipboard()
    }
    // #8 Ctrl+Shift+V 与 Ctrl+V 功能完全重复，已移除，保留下方 Ctrl+V 即可
    Shortcut {
        enabled: liveLogDialog.visible
        sequence: "Ctrl+V"
        onActivated: {
            if (!cmdInput)
                return;
            cmdInput.forceActiveFocus();
            cmdInput.insert(cmdInput.cursorPosition, sshTerminal.clipboardText());
        }
    }

    // 终端背景色：深色模式用暗色，浅色模式用近白色
    readonly property color terminalBg: (theme && theme.isDark) ? "#1a1d21" : "#f5f6f7"
    readonly property color terminalText: (theme && theme.isDark) ? "#d4d4d4" : "#1e1e1e"
    readonly property color terminalMatchBg: (theme && theme.isDark) ? "#5a4a00" : "#fff3a0"
    readonly property color terminalBorder: (theme && theme.isDark) ? "#3c3f44" : "#d0d3d8"
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
    readonly property color successWeakBg: liveLogDialog.theme ? Qt.rgba(liveLogDialog.theme.success.r, liveLogDialog.theme.success.g, liveLogDialog.theme.success.b, liveLogDialog.theme.isDark ? 0.22 : 0.16) : "#dff0d8"
    readonly property color warningWeakBg: liveLogDialog.theme ? Qt.rgba(liveLogDialog.theme.warning.r, liveLogDialog.theme.warning.g, liveLogDialog.theme.warning.b, liveLogDialog.theme.isDark ? 0.22 : 0.16) : "#fef9e7"
    readonly property color errorWeakBg: liveLogDialog.theme ? Qt.rgba(liveLogDialog.theme.error.r, liveLogDialog.theme.error.g, liveLogDialog.theme.error.b, liveLogDialog.theme.isDark ? 0.22 : 0.16) : "#f0d4d4"
    readonly property color neutralWeakBg: liveLogDialog.theme ? Qt.rgba(liveLogDialog.theme.controlBorder.r, liveLogDialog.theme.controlBorder.g, liveLogDialog.theme.controlBorder.b, liveLogDialog.theme.isDark ? 0.28 : 0.22) : "#f0f0f0"
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
                context.reset();
                context.moveTo(0, 0);
                context.lineTo(width, 0);
                context.lineTo(width / 2, height);
                context.closePath();
                context.fillStyle = liveLogDialog.theme ? liveLogDialog.theme.text : "#333333";
                context.fill();
            }
        }

        contentItem: Text {
            leftPadding: 10
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
            if (backend && backend.popupResultModel && backend.popupResultModel.getColumnWidths) {
                var widths = backend.popupResultModel.getColumnWidths(liveLogDialog.charWidth, 20);
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
                RowLayout { Layout.fillWidth: true; spacing: 8
                Label {
                    text: "主机"
                    color: labelColor
                    font.pixelSize: 12
                }
                StyledTextField {
                    id: hostInput
                    objectName: "ssh-host"
                    enabled: !liveLogDialog.secureCredentials || (!liveLogDialog.sessionActive && !liveLogDialog.pendingSshAction && !liveLogDialog.passwordOperationId)
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
                    enabled: !liveLogDialog.secureCredentials || (!liveLogDialog.sessionActive && !liveLogDialog.pendingSshAction && !liveLogDialog.passwordOperationId)
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
                    enabled: !liveLogDialog.secureCredentials || (!liveLogDialog.sessionActive && !liveLogDialog.pendingSshAction && !liveLogDialog.passwordOperationId)
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
                    enabled: !liveLogDialog.secureCredentials || (!liveLogDialog.sessionActive && !liveLogDialog.pendingSshAction && !liveLogDialog.passwordOperationId)
                    text: liveLogDialog.secureCredentials ? "" : sshTerminal.password
                    echoMode: TextInput.Password
                    placeholderText: !liveLogDialog.secureCredentials ? "••••••••" : !liveLogDialog.targetMatchesBackend() ? "新目标密码" : sshTerminal.credentialBusy ? "正在核验凭据" : sshTerminal.hasSavedPassword ? "已保存；留空使用" : "输入本次密码"
                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                }

                Label {
                    text: "编码"
                    color: labelColor
                    font.pixelSize: 12
                }
                StyledComboBox {
                    id: encodingCombo
                    Layout.preferredWidth: 84
                    model: ["UTF-8", "GB2312"]
                    Component.onCompleted: {
                        currentIndex = (sshTerminal.encoding === "GB2312" || sshTerminal.encoding === "GBK") ? 1 : 0;
                    }
                    onActivated: function (index) {
                        sshTerminal.encoding = model[index];
                    }
                }

                    Item { Layout.fillWidth: true }
                }
                RowLayout { Layout.fillWidth: true; spacing: 8
                Label {
                    text: "缓存行"
                    color: labelColor
                    font.pixelSize: 12
                }
                StyledTextField {
                    id: maxLinesInput
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
                    width: 1
                    height: 22
                    color: cardBorder
                    opacity: 0.7
                }

                Label {
                    text: "自动重连"
                    color: labelColor
                    font.pixelSize: 12
                }
                Next.GlassSwitch {
                    checked: sshTerminal.autoReconnectEnabled
                    onToggled: sshTerminal.autoReconnectEnabled = checked
                    palette.button: liveLogDialog.theme ? liveLogDialog.theme.controlBackground : "#ffffff"
                    palette.buttonText: liveLogDialog.theme ? liveLogDialog.theme.text : "#333333"
                    palette.highlight: focusAccentColor
                }

                Label {
                    text: "次数"
                    color: labelColor
                    font.pixelSize: 12
                    opacity: sshTerminal.autoReconnectEnabled ? 1.0 : 0.4
                }
                StyledTextField {
                    id: reconnectCountInput
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

                    Item { Layout.fillWidth: true }
                }
                RowLayout { Layout.fillWidth: true; spacing: 8
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
                // 连接/断开按钮
                Rectangle {
                    width: 72
                    height: 30
                    radius: 8
                    color: connectBtnArea.containsMouse ? Qt.lighter(connBtnBg, 1.15) : connBtnBg
                    Behavior on color {
                        ColorAnimation {
                            duration: 120
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: {
                            if (liveLogDialog.pendingSshAction) return "取消等待";
                            switch (sshTerminal.connectionState) {
                            case SshTerminal.Connecting:
                                return "连接中…";
                            case SshTerminal.Reconnecting:
                                return "重连中…";
                            case SshTerminal.Connected:
                                return "断  开";
                            default:
                                return "连  接";
                            }
                        }
                        color: "#ffffff"
                        font.pixelSize: 13
                        font.bold: true
                    }

                    MouseArea {
                        id: connectBtnArea
                        objectName: "sshConnectAction"
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        enabled: !liveLogDialog.passwordOperationId
                        onClicked: liveLogDialog.connectAction()
                    }
                }

                StyledButton {
                    text: autoScroll ? "⏸ 暂停" : "▶ 继续"
                    implicitWidth: 78
                    onClicked: {
                        autoScroll = !autoScroll;
                        if (autoScroll)
                            logListView.positionViewAtEnd();
                    }
                }

                StyledButton {
                    text: "清屏"
                    implicitWidth: 56
                    onClicked: sshTerminal.clearBuffer()
                }

                StyledButton {
                    text: "复制全部"
                    implicitWidth: 72
                    onClicked: sshTerminal.copyBufferToClipboard()
                }
                    Item { Layout.fillWidth: true }
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
                        if (cmdInput)
                            cmdInput.forceActiveFocus();
                    }
                }
            }
        }

        // ── 终端输出区域 ──────────────────────────────────────────
        Rectangle {
            id: terminalPane
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

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        onPressed: {
                            if (liveLogDialog.cmdInput) {
                                liveLogDialog.cmdInput.forceActiveFocus();
                                liveLogDialog.cmdInput.cursorPosition = liveLogDialog.cmdInput.length;
                            }
                            mouse.accepted = false;
                        }
                    }

                    Keys.onPressed: function (event) {
                        if (routeKeyToCmdInput(event))
                            return;
                    }

                    ListView {
                        id: logListView
                        anchors.fill: parent
                        model: sshTerminal.lineModel
                        clip: true
                        reuseItems: true
                        cacheBuffer: 80
                        spacing: 0
                        bottomMargin: sshTerminal.connected ? 32 : 0

                        delegate: Rectangle {
                            id: lineItem
                            width: logListView.width
                            height: lineText.implicitHeight + 4
                            color: (searchKeyword !== "" && model.lineText.toLowerCase().indexOf(searchKeyword.toLowerCase()) >= 0) ? terminalMatchBg : "transparent"

                            TextEdit {
                                id: lineText
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 6
                                verticalAlignment: Text.AlignVCenter
                                text: model.lineText
                                readOnly: true
                                selectByMouse: false
                                persistentSelection: true
                                wrapMode: TextEdit.NoWrap
                                font.family: Next.PlatformFonts.monospaceFamily
                                font.pixelSize: 13
                                color: terminalText
                                selectedTextColor: terminalText
                                selectionColor: (theme && theme.isDark) ? "#446688" : "#9ec6ff"
                            }

                            function applyHexSelection() {
                                if (index === selectedLineIndex && selectedHexStart >= 0 && selectedHexEnd > selectedHexStart)
                                    lineText.select(selectedHexStart, selectedHexEnd);
                                else
                                    lineText.deselect();
                            }

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onPressed: function (mouse) {
                                    selectLineAndExtractHex(index);
                                    if (mouse.button === Qt.RightButton)
                                        terminalContextMenu.popup();
                                }
                                onDoubleClicked: sshTerminal.copyLineToClipboard(index)
                            }
                        }

                        footer: sshTerminal.connected ? inputRowComponent : null
                        footerPositioning: ListView.OverlayFooter

                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }
                        ScrollBar.horizontal: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }
                    }

                    Component {
                        id: inputRowComponent

                        Rectangle {
                            width: logListView.width
                            height: 28
                            color: "transparent"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 8
                                spacing: 4

                                Text {
                                    text: ">"
                                    color: theme ? theme.accent : "#2196F3"
                                    font.family: Next.PlatformFonts.monospaceFamily
                                    font.pixelSize: 14
                                    font.bold: true
                                }

                                Next.GlassTextField {
                                    id: terminalInput
                                    Layout.fillWidth: true
                                    font.family: Next.PlatformFonts.monospaceFamily
                                    font.pixelSize: 13
                                    color: terminalText
                                    background: Item {}
                                    placeholderText: ""
                                    activeFocusOnTab: false

                                    Component.onCompleted: liveLogDialog.cmdInput = terminalInput
                                    Component.onDestruction: if (liveLogDialog.cmdInput === terminalInput)
                                        liveLogDialog.cmdInput = null

                                    function sendCmd() {
                                        var cmd = terminalInput.text;
                                        liveLogDialog.sendCommandAndTrackHistory(cmd);
                                        terminalInput.text = "";
                                    }

                                    Keys.priority: Keys.BeforeItem
                                    Keys.onReturnPressed: {
                                        sendCmd();
                                        event.accepted = true;
                                    }
                                    Keys.onEnterPressed: {
                                        sendCmd();
                                        event.accepted = true;
                                    }
                                    Keys.onPressed: function (event) {
                                        if (liveLogDialog.handleCmdCtrlShortcut(event))
                                            return;
                                        if (event.key === Qt.Key_Up) {
                                            var hist = liveLogDialog.cmdHistory;
                                            if (hist.length === 0)
                                                return;
                                            if (liveLogDialog.historyIndex === -1) {
                                                liveLogDialog.pendingInput = terminalInput.text;
                                                liveLogDialog.historyIndex = hist.length;
                                            }
                                            if (liveLogDialog.historyIndex > 0) {
                                                liveLogDialog.historyIndex--;
                                                terminalInput.text = hist[liveLogDialog.historyIndex];
                                                terminalInput.cursorPosition = terminalInput.text.length;
                                            }
                                            event.accepted = true;
                                            return;
                                        }
                                        if (event.key === Qt.Key_Down) {
                                            var hist2 = liveLogDialog.cmdHistory;
                                            if (liveLogDialog.historyIndex === -1)
                                                return;
                                            if (liveLogDialog.historyIndex < hist2.length - 1) {
                                                liveLogDialog.historyIndex++;
                                                terminalInput.text = hist2[liveLogDialog.historyIndex];
                                            } else {
                                                liveLogDialog.historyIndex = -1;
                                                terminalInput.text = liveLogDialog.pendingInput;
                                            }
                                            terminalInput.cursorPosition = terminalInput.text.length;
                                            event.accepted = true;
                                            return;
                                        }
                                        if (event.key === Qt.Key_Tab) {
                                            sshTerminal.sendTabCompletion(terminalInput.text);
                                            event.accepted = true;
                                            return;
                                        }
                                        if (event.key === Qt.Key_Insert && (event.modifiers & Qt.ControlModifier)) {
                                            if (selectedLineIndex >= 0)
                                                sshTerminal.copyLineToClipboard(selectedLineIndex);
                                            event.accepted = true;
                                        }
                                        if (event.key === Qt.Key_Insert && (event.modifiers & Qt.ShiftModifier)) {
                                            terminalInput.insert(terminalInput.cursorPosition, sshTerminal.clipboardText());
                                            event.accepted = true;
                                        }
                                    }

                                    Connections {
                                        target: sshTerminal
                                        function onTabCompletionReady(completedText, candidates) {
                                            if (completedText !== "") {
                                                terminalInput.text = completedText;
                                                terminalInput.cursorPosition = completedText.length;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: parsePanelSplitter
                    Layout.preferredWidth: 6
                    Layout.fillHeight: true
                    visible: parseSidePanel.visible
                    color: parsePanelDragArea.pressed ? (theme ? theme.splitHandlePressed : "#888888") : (parsePanelDragArea.containsMouse ? (theme ? theme.splitHandleHover : "#aaaaaa") : "transparent")

                    MouseArea {
                        id: parsePanelDragArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.SplitHCursor
                        property real startMouseX: 0
                        property real startPanelWidth: 0

                        onPressed: function (mouse) {
                            startMouseX = mouse.x;
                            startPanelWidth = parseSidePanel.width;
                        }

                        onPositionChanged: function (mouse) {
                            if (!pressed)
                                return;
                            var delta = mouse.x - startMouseX;
                            parsePanelWidth = clampParsePanelWidth(startPanelWidth - delta);
                        }
                    }
                }

                Rectangle {
                    id: parseSidePanel
                    Layout.preferredWidth: clampParsePanelWidth(parsePanelWidth > 0 ? parsePanelWidth : Math.max(parsePanelMinWidth, terminalPane.width * 0.42))
                    Layout.minimumWidth: parsePanelMinWidth
                    Layout.maximumWidth: Math.max(parsePanelMinWidth, terminalPane.width * parsePanelMaxRatio)
                    Layout.fillHeight: true
                    // #7 使用 >0.01 避免浮点精度导致面板淡出后永远不被隐藏
                    visible: parsePanelVisible || opacity > 0.01
                    opacity: parsePanelVisible ? 1 : 0
                    enabled: opacity > 0
                    color: cardBg
                    border.color: cardBorder
                    radius: 6

                    Behavior on opacity {
                        NumberAnimation {
                            duration: 180
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                text: "协议解析输出"
                                color: theme ? theme.text : "#333333"
                                font.bold: true
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            StyledButton {
                                text: "关闭"
                                implicitWidth: 56
                                onClicked: parsePanelVisible = false
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
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            contentWidth: popupCols.total
                            flickableDirection: Flickable.HorizontalAndVerticalFlick
                            model: backend ? backend.popupResultModel : null

                            delegate: Rectangle {
                                width: popupCols.total
                                height: model.isFrameSeparator ? 20 : 30
                                color: model.isFrameSeparator ? (theme ? theme.tableHighlight : "#eeeeee") : (model.highlighted ? (theme ? theme.tableHighlight : "#d0e8ff") : (index % 2 === 0 ? Qt.rgba(0, 0, 0, 0) : (theme ? theme.rowAlt : "#fafafa")))

                                MouseArea {
                                    anchors.fill: parent
                                    visible: !model.isFrameSeparator
                                    onClicked: {
                                        var link = "highlight:" + model.offset + "," + model.length;
                                        backend.handleLink(link, 3);
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
                                                onClicked: backend.popupResultModel.toggleExpand(index)
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

        // ── 底部工具栏 ────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            // 自定义命令区
            RowLayout {
                spacing: 4

                Label {
                    text: "快捷命令"
                    color: labelColor
                    font.pixelSize: 12
                }

                StyledTextField {
                    id: customCmdNameInput
                    Layout.preferredWidth: 92
                    placeholderText: "名称"
                    onAccepted: customCmdContentInput.forceActiveFocus()
                }

                StyledTextField {
                    id: customCmdContentInput
                    Layout.preferredWidth: 150
                    placeholderText: "命令内容"
                    onAccepted: {
                        liveLogDialog.addCustomCommand(customCmdNameInput.text, customCmdContentInput.text);
                        customCmdNameInput.text = "";
                        customCmdContentInput.text = "";
                        if (liveLogDialog.cmdInput)
                            liveLogDialog.cmdInput.forceActiveFocus();
                    }
                }

                StyledButton {
                    text: "添加"
                    implicitWidth: 52
                    onClicked: {
                        liveLogDialog.addCustomCommand(customCmdNameInput.text, customCmdContentInput.text);
                        customCmdNameInput.text = "";
                        customCmdContentInput.text = "";
                        if (liveLogDialog.cmdInput)
                            liveLogDialog.cmdInput.forceActiveFocus();
                    }
                }

                Flickable {
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
                                    anchors.fill: parent
                                    text: modelData.name
                                    implicitWidth: Math.max(60, Math.min(130, modelData.name.length * 14 + 18))
                                    enabled: sshTerminal.connected
                                    onClicked: liveLogDialog.sendCommandAndTrackHistory(modelData.command)
                                }

                                Rectangle {
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
                    text: "先添加常用命令按钮"
                    color: theme ? theme.subText : "#888888"
                    font.pixelSize: 12
                }
            }

            Item {
                Layout.fillWidth: true
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
            objectName: "ssh-credential-status"; visible: liveLogDialog.secureCredentials; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
            text: liveLogDialog.passwordNotice || sshTerminal.credentialError || (sshTerminal.credentialBusy ? "正在核验安全凭据…" : !liveLogDialog.targetMatchesBackend() ? "目标已修改，将重新核验凭据。" : sshTerminal.hasSavedPassword ? "当前目标已保存密码，不回显原密码。" : "当前目标未保存密码。")
            color: liveLogDialog.passwordNoticeError || sshTerminal.credentialError ? errorColor : (theme ? theme.subText : "#888888")
        }

        // ── 快捷键提示栏 ──────────────────────────────────────────
        Label {
            id: statusHint
            Layout.fillWidth: true
            text: sshTerminal.connected ? "Ctrl+F 查找  ·  F3 / Shift+F3 跳转  ·  Ctrl+Shift+C 复制行  ·  Ctrl+Shift+A 复制全部  ·  Ctrl+L 清屏" : "尚未连接  —  填写连接参数后点击「连接」"
            color: theme ? theme.subText : "#888"
            font.pixelSize: 11
            elide: Text.ElideRight
            topPadding: 1
            bottomPadding: 2
        }
    }
}
