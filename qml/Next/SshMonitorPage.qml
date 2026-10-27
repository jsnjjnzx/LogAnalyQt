import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt.Next as Next

GlassPage {
    id: root
    title: "实时监控"
    component SessionOption: CheckDelegate {
        id: option
        required property var menu
        signal keyboardToggleRequested()
        implicitHeight: 34
        implicitWidth: Math.max(212, implicitContentWidth + leftPadding + rightPadding)
        leftPadding: 32; rightPadding: 12
        font.pixelSize: 13
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        highlighted: hovered || visualFocus
        function selectInMenu() {
            for (let i = 0; i < menu.count; ++i)
                if (menu.itemAt(i) === option) { menu.currentIndex = i; return }
        }
        onHoveredChanged: if (hovered) selectInMenu()
        onPressed: selectInMenu()
        Keys.onReturnPressed: keyboardToggleRequested()
        Keys.onEnterPressed: keyboardToggleRequested()
        indicator: Rectangle {
            x: 12; y: (option.height - height) / 2
            width: 14; height: 14; radius: 3
            color: "transparent"; border.width: 1
            border.color: option.highlighted ? option.palette.highlightedText : option.palette.text
            opacity: option.enabled ? 1 : 0.38
            Text {
                objectName: "menu-check-mark"
                anchors.centerIn: parent
                visible: option.checked; text: "✓"; font.pixelSize: 11
                color: parent.border.color
            }
        }
        contentItem: Text {
            text: option.text; textFormat: Text.PlainText; font: option.font
            color: option.highlighted ? option.palette.highlightedText : option.palette.text
            opacity: option.enabled ? 1 : 0.38
            verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 7
            color: option.highlighted && option.enabled ? option.palette.highlight : "transparent"
        }
    }
    property var backend
    property var sessions: []
    property var savedConnections: []
    property int currentIndex: -1
    readonly property var activeSession: currentIndex >= 0 && currentIndex < sessions.length ? sessions[currentIndex] : null
    property int nextSessionId: 1
    property bool ready: false
    property bool writable: true
    property string workspaceError: ""
    property var pendingClose: null
    property bool sidebarCollapsed: false
    property bool shuttingDown: false
    property bool waitingForLogShutdown: false
    enabled: !waitingForLogShutdown
    property var logShutdownTargets: []
    signal logShutdownReady(bool success)
    function hasOpenLogFiles() {
        return sessions.some(function(page) { return page.terminal.logging || page.terminal.logBusy; });
    }
    function finishLogsBeforeExit() {
        logShutdownTargets = sessions.filter(function(page) { return page.terminal.logging || page.terminal.logBusy; });
        waitingForLogShutdown = true;
        continueLogShutdown();
    }
    function continueLogShutdown() {
        if (!waitingForLogShutdown) return;
        let pending = false;
        for (const page of sessions) {
            const terminal = page.terminal;
            if (terminal.logBusy) pending = true;
            else if (terminal.logging) { pending = true; terminal.stopLogging(); }
        }
        if (!pending) {
            waitingForLogShutdown = false;
            const success = !logShutdownTargets.some(function(page) { return page.terminal.logError !== ""; });
            logShutdownTargets = [];
            logShutdownReady(success);
        }
    }
    property var monitorRules: []
    property int nextMonitorRuleId: 1
    property string monitorRuleError: ""

    function validMonitorRule(rule) {
        return rule && Number.isInteger(rule.id) && rule.id > 0 && rule.id < 2147483647
            && typeof rule.name === "string" && rule.name.trim().length > 0 && rule.name.length <= 100
            && typeof rule.pattern === "string" && rule.pattern.length > 0 && rule.pattern.length <= 4096
            && (rule.color === undefined || typeof rule.color === "string" && /^#[0-9a-fA-F]{6}$/.test(rule.color))
            && (rule.pauseOnMatch === undefined || typeof rule.pauseOnMatch === "boolean")
            && (rule.highlightWholeLine === undefined || typeof rule.highlightWholeLine === "boolean")
            && typeof rule.ignoreCase === "boolean" && backend.testMonitorExpression(rule.pattern, rule.ignoreCase, "").valid;
    }
    function saveMonitorRule(id, name, pattern, ignoreCase, color, pauseOnMatch, highlightWholeLine) {
        const rule = {id: id > 0 ? id : nextMonitorRuleId, name: name.trim(), pattern: pattern, ignoreCase: ignoreCase,
            color: color || "#e8b76a", pauseOnMatch: pauseOnMatch !== false, highlightWholeLine: highlightWholeLine !== false};
        if (!validMonitorRule(rule)) { monitorRuleError = "请填写名称及有效的正则表达式（最多 4096 字符）。"; return 0; }
        if (id < 1 && monitorRules.length >= 256) { monitorRuleError = "最多保存 256 条规则。"; return 0; }
        const before = monitorRules, next = nextMonitorRuleId;
        const found = monitorRules.some(function(value) { return value.id === id; });
        if (id > 0 && !found) { monitorRuleError = "该规则已被删除，请重新选择。"; return 0; }
        monitorRules = found ? monitorRules.map(function(value) { return value.id === id ? rule : value; }) : monitorRules.concat([rule]);
        if (id < 1) nextMonitorRuleId++;
        if (!saveWorkspace()) { monitorRules = before; nextMonitorRuleId = next; monitorRuleError = workspaceError; return 0; }
        monitorRuleError = "";
        return rule.id;
    }
    function removeMonitorRule(id) {
        const before = monitorRules, configs = sessions.map(function(page) { return page.monitorRuleIds; });
        const connections = savedConnections;
        monitorRules = monitorRules.filter(function(rule) { return rule.id !== id; });
        for (const page of sessions) page.monitorRuleIds = page.monitorRuleIds.filter(function(value) { return value !== id; });
        savedConnections = savedConnections.map(function(config) {
            const next = Object.assign({}, config);
            if (next.monitorRuleIds) next.monitorRuleIds = next.monitorRuleIds.filter(function(value) { return value !== id; });
            return next;
        });
        if (!saveWorkspace()) {
            monitorRules = before; savedConnections = connections;
            sessions.forEach(function(page, index) { page.monitorRuleIds = configs[index]; });
            monitorRuleError = workspaceError; return false;
        }
        monitorRuleError = ""; return true;
    }
    function openMonitorRules(window) {
        ruleWindow.transientParent = window || root.ownerWindow || null;
        ruleWindow.show(); ruleWindow.raise(); ruleWindow.requestActivate();
    }
    Next.SshMonitorRuleWindow { id: ruleWindow; theme: root.theme; library: root }

    QtObject {
        id: commandLibrary
        property var commands: []
        property bool loaded: false
    }
    Timer { id: saveTimer; interval: 500; onTriggered: root.saveWorkspace() }
    function scheduleSave() { if (ready) saveTimer.restart(); }
    function blankConfiguration(name) {
        return {name: name, host: "", port: "22", username: "root", password: "", encoding: "UTF-8", maxLines: "10000",
            autoReconnect: true, reconnectCount: "5", reconnectInterval: "2000", expanded: true, commandManagementExpanded: false};
    }
    function validConfiguration(config) {
        if (!config || typeof config !== "object") return false;
        for (const key of ["name", "host", "port", "username", "maxLines", "reconnectCount", "reconnectInterval"])
            if (typeof config[key] !== "string" || config[key].length > 1024) return false;
        return (config.password === undefined || typeof config.password === "string" && config.password.length <= 4096)
            && ["UTF-8", "GB2312"].indexOf(config.encoding) >= 0
            && typeof config.autoReconnect === "boolean" && typeof config.expanded === "boolean"
            && typeof config.commandManagementExpanded === "boolean"
            && (config.copyOnSelect === undefined || typeof config.copyOnSelect === "boolean")
            && (config.wrapLogLines === undefined || typeof config.wrapLogLines === "boolean")
            && (config.logFlushIntervalSeconds === undefined || Number.isInteger(config.logFlushIntervalSeconds) && config.logFlushIntervalSeconds >= 1 && config.logFlushIntervalSeconds <= 60)
            && (config.logBufferKiB === undefined || Number.isInteger(config.logBufferKiB) && config.logBufferKiB >= 16 && config.logBufferKiB <= 8192)
            && (config.monitorEnabled === undefined || typeof config.monitorEnabled === "boolean")
            && (config.monitorRuleIds === undefined || Array.isArray(config.monitorRuleIds) && config.monitorRuleIds.length <= 256
                && config.monitorRuleIds.every(function(id, index, ids) { return Number.isInteger(id) && id > 0 && id < 2147483647 && ids.indexOf(id) === index; }));
    }
    function addSession(config, id) {
        if (sessions.length >= 64) { workspaceError = "最多同时打开 64 个监控会话，请先关闭不需要的会话。"; return null; }
        const number = id === undefined ? nextSessionId++ : id;
        nextSessionId = Math.max(nextSessionId, number + 1);
        const page = sessionComponent.createObject(sessionHost, {sessionId: number, sessionName: "监控 " + number});
        if (!page) { workspaceError = "监控会话创建失败。"; return null; }
        if (config) page.restoreConfiguration(config);
        else if (ready) page.restoreConfiguration(blankConfiguration(page.sessionName));
        sessions = sessions.concat([page]);
        currentIndex = sessions.length - 1;
        scheduleSave();
        return page;
    }
    function saveWorkspace() {
        if (!ready) return true;
        if (!writable) return false;
        saveTimer.stop();
        const rows = sessions.map(function(page) { return {id: page.sessionId, config: page.connectionConfiguration()}; });
        const saved = backend && backend.setUiSetting("next/sshMonitor/workspace",
            JSON.stringify({version: 1, sessions: rows, connections: savedConnections, currentIndex: currentIndex, sidebarCollapsed: sidebarCollapsed,
                monitorRules: monitorRules, nextMonitorRuleId: nextMonitorRuleId}));
        workspaceError = saved ? "" : "监控配置保存失败，内容仍保留在当前窗口，请检查磁盘或目录权限后重试。";
        return !!saved;
    }
    function restoreWorkspace() {
        let state = null;
        const raw = backend.getUiSetting("next/sshMonitor/workspace", "");
        if (raw !== "") {
            try {
                state = JSON.parse(raw);
                if (!state || state.version !== 1 || !Array.isArray(state.sessions) || state.sessions.length > 64
                        || !Number.isInteger(state.currentIndex)
                        || (state.sessions.length ? state.currentIndex < 0 || state.currentIndex >= state.sessions.length : state.currentIndex !== -1))
                    throw new Error("Invalid monitor workspace");
                if (state.sidebarCollapsed !== undefined && typeof state.sidebarCollapsed !== "boolean")
                    throw new Error("Invalid monitor sidebar state");
                if (state.monitorRules !== undefined && (!Array.isArray(state.monitorRules) || state.monitorRules.length > 256
                        || !state.monitorRules.every(function(rule, index, rules) {
                            return root.validMonitorRule(rule) && rules.findIndex(function(value) { return value.id === rule.id; }) === index;
                        }))) throw new Error("Invalid monitor rule library");
                if (state.nextMonitorRuleId !== undefined && (!Number.isInteger(state.nextMonitorRuleId) || state.nextMonitorRuleId < 1
                        || state.nextMonitorRuleId >= 2147483647 || (state.monitorRules || []).some(function(rule) { return rule.id >= state.nextMonitorRuleId; })))
                    throw new Error("Invalid monitor rule identity");
                const ids = [];
                for (const row of state.sessions) {
                    if (!row || !Number.isSafeInteger(row.id) || row.id < 1 || ids.indexOf(row.id) >= 0 || !validConfiguration(row.config))
                        throw new Error("Invalid monitor session");
                    ids.push(row.id);
                }
                if (state.connections !== undefined && (!Array.isArray(state.connections)
                        || !state.connections.every(function(config) { return root.validConfiguration(config); })))
                    throw new Error("Invalid saved connections");
            } catch (error) {
                state = null;
                writable = backend.setUiSetting("next/sshMonitor/recovery", raw);
                workspaceError = writable ? "上次的监控配置无法读取，已保留原始备份，本次打开一个默认会话。"
                    : "监控配置无法读取且备份失败，已停止保存以保护原始配置。";
            }
        }
        if (state) {
            sidebarCollapsed = state.sidebarCollapsed || false;
            savedConnections = state.connections || [];
            monitorRules = state.monitorRules || [];
            nextMonitorRuleId = state.nextMonitorRuleId || monitorRules.reduce(function(next, rule) { return Math.max(next, rule.id + 1); }, 1);
            for (const row of state.sessions) addSession(row.config, row.id);
            currentIndex = state.currentIndex;
        } else addSession();
        ready = true;
    }
    function connectionKey(config) {
        return JSON.stringify([config.host.trim().toLowerCase(), Number(config.port), config.username.trim()]);
    }
    function rememberConnection(page) {
        const config = page.connectionConfiguration();
        if (!config.host.trim() || !config.username.trim() || !config.password || !page.targetMatchesBackend()) return;
        const key = connectionKey(config);
        const rows = savedConnections.slice();
        const index = rows.findIndex(function(value) { return root.connectionKey(value) === key; });
        if (index < 0) rows.push(config); else rows[index] = config;
        savedConnections = rows;
        saveWorkspace();
    }
    function connectSaved(config) {
        const page = addSession(config);
        if (page) page.connectAction();
        connectionsDialog.close();
        return page;
    }
    function removeSaved(index) {
        const before = savedConnections;
        savedConnections = savedConnections.filter(function(value, row) { return row !== index; });
        if (!saveWorkspace()) savedConnections = before;
    }
    function requestCloseSession(page) {
        if (!page || sessions.indexOf(page) < 0) return;
        if (page.sessionActive || page.pendingSshAction) { pendingClose = page; closeDialog.open(); }
        else removeSession(page);
    }
    function removeSession(page) {
        const index = sessions.indexOf(page);
        if (index < 0) return false;
        const before = sessions;
        const selected = currentIndex;
        sessions = sessions.filter(function(value) { return value !== page; });
        currentIndex = sessions.length ? Math.max(0, Math.min(selected - (index < selected ? 1 : 0), sessions.length - 1)) : -1;
        if (!saveWorkspace()) { sessions = before; currentIndex = selected; return false; }
        page.detached = false;
        page.monitorWindow.hide();
        page.closeSshSession();
        page.destroy();
        return true;
    }
    function finishWindowMove(page) {
        Qt.callLater(function() {
            if (root.sessions.indexOf(page) >= 0) page.endWindowMove();
        });
    }
    function locateSession(page) {
        if (!page || !page.detached) return;
        const window = page.monitorWindow;
        if (window.visibility === Window.Minimized) window.showNormal();
        else window.show();
        window.raise();
        window.requestActivate();
    }
    function detachSession(page) {
        if (!page || sessions.indexOf(page) < 0) return false;
        if (page.detached) { locateSession(page); return true; }
        page.beginWindowMove();
        page.detached = true;
        page.connectionConfigExpanded = false;
        const window = page.monitorWindow;
        if (!page.windowPositioned && root.ownerWindow) {
            window.screen = root.ownerWindow.screen;
            window.x = root.ownerWindow.x + 40;
            window.y = root.ownerWindow.y + 40;
            page.windowPositioned = true;
        }
        locateSession(page);
        finishWindowMove(page);
        return true;
    }
    function dockSession(page) {
        if (!page || !page.detached || sessions.indexOf(page) < 0) return false;
        page.beginWindowMove();
        page.detached = false;
        currentIndex = sessions.indexOf(page);
        page.monitorWindow.hide();
        root.requested();
        if (root.ownerWindow) {
            if (root.ownerWindow.visibility === Window.Minimized) root.ownerWindow.showNormal();
            else root.ownerWindow.show();
            root.ownerWindow.raise();
            root.ownerWindow.requestActivate();
        }
        finishWindowMove(page);
        return true;
    }
    function connectSession(page) {
        if (!page) return;
        currentIndex = sessions.indexOf(page);
        if (page.detached) locateSession(page);
        page.connectAction();
    }
    function shutdown() {
        if (shuttingDown) return;
        shuttingDown = true;
        ruleWindow.close();
        for (const page of sessions) {
            page.detached = false;
            page.monitorWindow.hide();
            page.closeSshSession();
        }
    }
    onSidebarCollapsedChanged: scheduleSave()
    onCurrentIndexChanged: {
        scheduleSave();
        Qt.callLater(function() {
            if (root.currentIndex >= 0) sessionList.positionViewAtIndex(root.currentIndex, ListView.Contain);
        });
    }
    onClosing: shutdown()
    Component.onCompleted: restoreWorkspace()
    Component.onDestruction: shutdown()

    Component {
        id: sessionComponent
        Next.LiveLogDialog {
            id: sessionPage
            objectName: "ssh-session-" + sessionId
            property bool detached: false
            enabled: !root.waitingForLogShutdown
            property bool windowPositioned: false
            readonly property alias monitorWindow: floatingWindow
            parent: detached ? floatingSessionHost : sessionHost
            anchors.fill: parent
            visible: detached ? floatingWindow.visible : root.visible && root.activeSession === sessionPage
            backend: root.backend
            theme: root.theme
            ownerWindow: detached ? floatingWindow : root.ownerWindow
            persistConnectionSettings: false
            workspaceManaged: true
            sharedCommands: commandLibrary
            monitorRuleLibrary: root
            onConfigurationEdited: root.scheduleSave()
            Connections {
                target: sessionPage.terminal
                function onConnectedChanged() { if (target.connected) root.rememberConnection(sessionPage); }
                function onLoggingChanged() {
                    if (root.waitingForLogShutdown) Qt.callLater(root.continueLogShutdown);
                }
            }
            ApplicationWindow {
                id: floatingWindow
                objectName: "ssh-monitor-window-" + sessionPage.sessionId
                title: sessionPage.sessionName + " · 实时监控 — 通联工坊"
                width: 1100; height: 760
                minimumWidth: 720; minimumHeight: 480
                flags: Qt.Window
                modality: Qt.NonModal
                transientParent: null
                visible: false
                color: root.theme ? root.theme.background : "#1d2025"
                palette: root.palette
                font: root.font
                onClosing: function(event) {
                    if (root.shuttingDown) return;
                    event.accepted = false;
                    root.dockSession(sessionPage);
                }
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 8
                        Label {
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            text: sessionPage.sessionName + " · " + sessionPage.stateText()
                            textFormat: Text.PlainText; elide: Text.ElideRight
                            color: root.theme ? root.theme.text : "white"
                        }
                        Next.GlassButton {
                            objectName: "ssh-window-maximize-" + sessionPage.sessionId
                            text: floatingWindow.visibility === Window.Maximized ? "还原" : "最大化"
                            onClicked: floatingWindow.visibility === Window.Maximized ? floatingWindow.showNormal() : floatingWindow.showMaximized()
                        }
                        Next.GlassButton {
                            objectName: "ssh-window-dock-" + sessionPage.sessionId
                            text: "收回主窗口"
                            onClicked: root.dockSession(sessionPage)
                        }
                    }
                    Item {
                        id: floatingSessionHost
                        Layout.fillWidth: true; Layout.fillHeight: true
                    }
                }
            }
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6
        Label {
            objectName: "ssh-workspace-error"; visible: text !== ""; Layout.fillWidth: true
            text: root.workspaceError; wrapMode: Text.Wrap; color: root.theme ? root.theme.error : "#f79a93"
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            spacing: 6
            Rectangle {
                objectName: "ssh-session-sidebar"
                Layout.preferredWidth: root.sidebarCollapsed ? 64 : 238
                Layout.fillHeight: true
                radius: 7
                color: root.theme ? root.theme.sidebar : "#202329"
                border.color: root.theme ? root.theme.groupBorder : "#3c424b"
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 5; spacing: 5
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            visible: !root.sidebarCollapsed
                            text: "监控会话 (" + root.sessions.length + ")"
                            Layout.fillWidth: true; Layout.leftMargin: 6
                            color: root.theme ? root.theme.subText : "#9199a5"
                        }
                        Next.GlassButton {
                            objectName: "ssh-sidebar-toggle"
                            text: root.sidebarCollapsed ? "›" : "‹"
                            Layout.fillWidth: root.sidebarCollapsed
                            implicitWidth: 32; implicitHeight: 32; padding: 0; horizontalPadding: 0
                            font.pixelSize: 22
                            Accessible.name: root.sidebarCollapsed ? "展开会话栏" : "折叠会话栏"
                            ToolTip.visible: hovered
                            ToolTip.text: Accessible.name
                            onClicked: root.sidebarCollapsed = !root.sidebarCollapsed
                        }
                    }
                    Next.GlassButton {
                        objectName: "ssh-new-session"
                        Layout.fillWidth: true
                        text: root.sidebarCollapsed ? "＋" : "＋ 新建监控"
                        implicitHeight: 34; horizontalPadding: 4
                        Accessible.name: "新建监控"
                        ToolTip.visible: hovered && root.sidebarCollapsed
                        ToolTip.text: "新建监控"
                        onClicked: root.addSession()
                    }
                    Next.GlassButton {
                        objectName: "ssh-saved-connections"
                        Layout.fillWidth: true
                        text: root.sidebarCollapsed ? "连接" : "已保存连接 (" + root.savedConnections.length + ")"
                        implicitHeight: 34; horizontalPadding: 4
                        Accessible.name: "已保存连接 (" + root.savedConnections.length + ")"
                        ToolTip.visible: hovered && root.sidebarCollapsed
                        ToolTip.text: Accessible.name
                        onClicked: connectionsDialog.open()
                    }
                    Next.GlassButton {
                        objectName: "ssh-monitor-rules"
                        Layout.fillWidth: true; implicitHeight: 34; horizontalPadding: 4
                        text: root.sidebarCollapsed ? "规则库" : "规则库 (" + root.monitorRules.length + ")"
                        Accessible.name: "规则库"
                        ToolTip.visible: hovered && root.sidebarCollapsed
                        ToolTip.text: "管理全部会话共享的规则库"
                        onClicked: root.openMonitorRules(root.ownerWindow)
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.topMargin: 3; Layout.bottomMargin: 3
                        height: 1; color: root.theme ? root.theme.groupBorder : "#3c424b"
                    }
                    ListView {
                        id: sessionList
                        objectName: "ssh-session-list"
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; spacing: 5
                        model: root.sessions
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: Rectangle {
                            id: sessionRow
                            required property var modelData
                            required property int index
                            readonly property var config: JSON.parse(modelData.configurationSnapshot)
                            objectName: "ssh-session-row-" + modelData.sessionId
                            width: sessionList.width
                            height: root.sidebarCollapsed ? 52 : 109
                            radius: 6
                            color: root.currentIndex === index ? (root.theme ? root.theme.navigationSelected : "#363c47")
                                : sessionSelect.hovered ? (root.theme ? root.theme.surfaceHover : "#393f49") : "transparent"
                            Next.GlassButton {
                                id: sessionSelect
                                anchors.fill: parent
                                padding: 0
                                Accessible.name: sessionRow.modelData.sessionName + " · " + sessionRow.modelData.stateText()
                                background: Rectangle { color: "transparent" }
                                ToolTip.visible: hovered && root.sidebarCollapsed
                                ToolTip.text: sessionRow.modelData.sessionName + " · " + sessionRow.modelData.stateText()
                                    + (sessionRow.config.host ? " · " + sessionRow.config.host : "") + "（右键更多操作）"
                                    + (sessionRow.modelData.monitorHit ? "\n命中：" + sessionRow.modelData.monitorHit.ruleName + " · 已暂停跟随" : "")
                                onClicked: root.currentIndex = sessionRow.index
                                onPressAndHold: sessionMenu.popup(sessionSelect, sessionSelect.width, 0)
                                Keys.onPressed: function(event) {
                                    if (event.key === Qt.Key_Menu || event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) {
                                        sessionMenu.popup(sessionSelect, sessionSelect.width, 0);
                                        event.accepted = true;
                                    }
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                onClicked: sessionMenu.popup(sessionSelect, sessionSelect.width, 0)
                            }
                            Rectangle {
                                visible: root.sidebarCollapsed && root.currentIndex === sessionRow.index
                                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                width: 3; height: 28; radius: 2
                                color: root.theme ? root.theme.accent : "#80b9ff"
                            }
                            Rectangle {
                                objectName: "ssh-session-badge-" + sessionRow.modelData.sessionId
                                visible: root.sidebarCollapsed
                                anchors.centerIn: parent
                                width: 34; height: 34; radius: 8
                                border.width: sessionRow.modelData.monitorHit ? 2 : 0
                                border.color: sessionRow.modelData.monitorHit ? sessionRow.modelData.monitorHitColor : "transparent"
                                color: root.currentIndex === sessionRow.index
                                    ? (root.theme ? root.theme.accentMuted : "#087af5")
                                    : (root.theme ? root.theme.controlBackground : "#191d23")
                                Label {
                                    anchors.centerIn: parent
                                    text: sessionRow.index + 1
                                    color: root.currentIndex === sessionRow.index ? "white" : (root.theme ? root.theme.text : "white")
                                    font.pixelSize: 13; font.bold: true
                                }
                                Rectangle {
                                    anchors.right: parent.right; anchors.bottom: parent.bottom
                                    anchors.rightMargin: -2; anchors.bottomMargin: -2
                                    width: 9; height: 9; radius: 5
                                    border.width: 2; border.color: root.theme ? root.theme.sidebar : "#202329"
                                    color: sessionRow.modelData.terminal.connected ? "#7ed6a0" : sessionRow.modelData.sessionActive ? "#e8b76a" : "#9199a5"
                                }
                            }
                            Next.GlassMenu {
                                id: sessionMenu
                                objectName: "ssh-session-options-" + sessionRow.modelData.sessionId
                                onCurrentIndexChanged: {
                                    const option = itemAt(currentIndex)
                                    if (option instanceof CheckDelegate) option.forceActiveFocus(Qt.TabFocusReason)
                                }
                                Next.GlassMenuItem {
                                    objectName: "ssh-session-detach-" + sessionRow.modelData.sessionId
                                    text: sessionRow.modelData.detached ? "定位独立窗口" : "独立窗口"
                                    onTriggered: sessionRow.modelData.detached ? root.locateSession(sessionRow.modelData) : root.detachSession(sessionRow.modelData)
                                }
                                Next.GlassMenuItem {
                                    objectName: "ssh-session-dock-" + sessionRow.modelData.sessionId
                                    visible: sessionRow.modelData.detached
                                    enabled: visible
                                    height: visible ? implicitHeight : 0
                                    text: "收回主窗口"
                                    onTriggered: root.dockSession(sessionRow.modelData)
                                }
                                Next.GlassMenuSeparator {}
                                SessionOption {
                                    objectName: "ssh-copy-on-select"
                                    menu: sessionMenu
                                    text: "选中即复制"
                                    checked: sessionRow.modelData.copyOnSelect
                                    onToggled: sessionRow.modelData.copyOnSelect = checked
                                    onKeyboardToggleRequested: sessionRow.modelData.copyOnSelect = !sessionRow.modelData.copyOnSelect
                                }
                                SessionOption {
                                    objectName: "ssh-wrap-lines"
                                    menu: sessionMenu
                                    text: "自动换行"
                                    checked: sessionRow.modelData.wrapLogLines
                                    onToggled: sessionRow.modelData.wrapLogLines = checked
                                    onKeyboardToggleRequested: sessionRow.modelData.wrapLogLines = !sessionRow.modelData.wrapLogLines
                                    ToolTip.visible: hovered
                                    ToolTip.text: "勾选后按窗口宽度换行；关闭时通过底部横向滚动条查看长行。"
                                }
                                Next.GlassMenuSeparator {}
                                Next.GlassMenuItem {
                                    objectName: "ssh-session-log-settings-" + sessionRow.modelData.sessionId
                                    text: "日志写入设置…"
                                    onTriggered: sessionRow.modelData.openLogSettings()
                                }
                                Next.GlassMenuItem {
                                    objectName: "ssh-session-log-start-" + sessionRow.modelData.sessionId
                                    text: "开始记录日志…"
                                    enabled: !sessionRow.modelData.terminal.logging && !sessionRow.modelData.terminal.logBusy
                                    ToolTip.visible: hovered
                                    ToolTip.text: "选择文件后记录该会话的新输出，保存为 UTF-8 文本；清屏和暂停跟随不影响记录。"
                                    onTriggered: sessionRow.modelData.requestLogFile(false)
                                }
                                Next.GlassMenuItem {
                                    objectName: "ssh-session-log-stop-" + sessionRow.modelData.sessionId
                                    text: "停止记录日志"
                                    enabled: sessionRow.modelData.terminal.logging && !sessionRow.modelData.terminal.logBusy
                                    onTriggered: sessionRow.modelData.terminal.stopLogging()
                                }
                                Next.GlassMenuItem {
                                    objectName: "ssh-session-log-export-" + sessionRow.modelData.sessionId
                                    text: "导出当前缓冲日志…"
                                    enabled: !sessionRow.modelData.terminal.logBusy
                                    ToolTip.visible: hovered
                                    ToolTip.text: "只导出该会话当前保留的日志及未换行尾部，已淘汰的内容无法恢复。"
                                    onTriggered: sessionRow.modelData.requestLogFile(true)
                                }
                                Next.GlassMenuSeparator {}
                                Next.GlassMenuItem {
                                    objectName: "ssh-session-remove-" + sessionRow.modelData.sessionId
                                    text: "删除会话…"
                                    onTriggered: root.requestCloseSession(sessionRow.modelData)
                                }
                            }
                            ColumnLayout {
                                visible: !root.sidebarCollapsed
                                anchors.fill: parent; anchors.margins: 7; spacing: 3
                                RowLayout {
                                    Layout.fillWidth: true
                                    Rectangle {
                                        width: 7; height: 7; radius: 4
                                        color: sessionRow.modelData.terminal.connected ? "#7ed6a0" : sessionRow.modelData.sessionActive ? "#e8b76a" : "#9199a5"
                                    }
                                    Label {
                                        visible: !root.sidebarCollapsed
                                        Layout.fillWidth: true; Layout.minimumWidth: 0
                                        text: sessionRow.modelData.sessionName
                                        textFormat: Text.PlainText; elide: Text.ElideRight
                                        color: root.theme ? root.theme.text : "white"
                                    }
                                    Next.GlassButton {
                                        id: sessionMenuButton
                                        objectName: "ssh-session-menu-" + sessionRow.modelData.sessionId
                                        text: "⋯"; implicitWidth: 28; implicitHeight: 27; padding: 0; horizontalPadding: 0
                                        Accessible.name: sessionRow.modelData.sessionName + " 更多操作"
                                        onClicked: {
                                            root.currentIndex = sessionRow.index;
                                            sessionMenu.popup(sessionMenuButton, 0, sessionMenuButton.height);
                                        }
                                    }
                                }
                                Label {
                                    visible: !root.sidebarCollapsed
                                    Layout.fillWidth: true
                                    text: sessionRow.config.host || "未配置主机"
                                    textFormat: Text.PlainText; elide: Text.ElideRight
                                    color: root.theme ? root.theme.subText : "#9199a5"; font.pixelSize: 11
                                }
                                RowLayout {
                                    visible: !root.sidebarCollapsed
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true; Layout.minimumWidth: 0
                                    text: sessionRow.modelData.monitorHit ? "命中 · 已暂停跟随" : sessionRow.modelData.monitorError ? "规则检测已停止"
                                        : sessionRow.modelData.terminal.receptionPaused ? "接收已停止"
                                        : (sessionRow.modelData.detached ? "↗ 独立窗口 · " : "") + sessionRow.modelData.stateText()
                                        elide: Text.ElideRight
                                    color: sessionRow.modelData.monitorHit && sessionRow.modelData.monitorHitColor.hslLightness >= 0.45 ? sessionRow.modelData.monitorHitColor
                                        : sessionRow.modelData.monitorError ? "#e8b76a" : (root.theme ? root.theme.subText : "#9199a5"); font.pixelSize: 11
                                    }
                                    Next.GlassButton {
                                        objectName: "ssh-session-connect-" + sessionRow.modelData.sessionId
                                        text: sessionRow.modelData.sessionActive || sessionRow.modelData.pendingSshAction ? "断开" : "连接"
                                        implicitWidth: 48; implicitHeight: 26; padding: 0; horizontalPadding: 0
                                        enabled: !sessionRow.modelData.passwordOperationId
                                        onClicked: root.connectSession(sessionRow.modelData)
                                    }
                                }
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                spacing: 0
                Item {
                    id: sessionHost
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ColumnLayout {
                        anchors.centerIn: parent
                        visible: root.activeSession !== null && root.activeSession.detached
                        Label {
                            text: "此会话正在独立窗口中监控"
                            color: root.theme ? root.theme.subText : "#9199a5"
                        }
                        Next.GlassButton {
                            objectName: "ssh-dock-session"; text: "收回主窗口"
                            Layout.alignment: Qt.AlignHCenter
                            onClicked: root.dockSession(root.activeSession)
                        }
                    }
                    Label {
                        anchors.centerIn: parent; visible: root.sessions.length === 0
                        text: "暂无监控会话，点击左侧「＋」新建监控"
                        color: root.theme ? root.theme.subText : "#9199a5"
                    }
                }
            }
        }
    }
    Next.GlassDialog {
        id: connectionsDialog
        objectName: "ssh-connections-dialog"
        palette: root.palette
        parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(760, root.width - 24); height: Math.min(480, root.height - 24)
        modal: true; title: "已保存连接"
        standardButtons: Dialog.NoButton
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            Next.GlassButton {
                text: "关闭"; Layout.rightMargin: 16; Layout.bottomMargin: 12
                onClicked: connectionsDialog.close()
            }
        }
        ColumnLayout {
            anchors.fill: parent
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: "连接成功后自动保存，账号和密码随连接记录一起保存。点击「连接」打开独立监控会话。"
                color: root.theme ? root.theme.subText : "#9199a5"
            }
            Label {
                visible: root.savedConnections.length === 0
                text: "暂无记录，请先新建监控并连接设备。"
                color: root.theme ? root.theme.subText : "#9199a5"
            }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; spacing: 6
                model: root.savedConnections
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    width: ListView.view.width; spacing: 8
                    Label {
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        text: modelData.name + "\n" + modelData.username + "@" + modelData.host + ":" + modelData.port
                        textFormat: Text.PlainText; elide: Text.ElideRight
                        color: root.theme ? root.theme.text : "white"
                    }
                    Next.GlassButton {
                        objectName: "ssh-saved-connect-" + index
                        text: "连接"; onClicked: root.connectSaved(modelData)
                    }
                    Next.GlassButton {
                        objectName: "ssh-saved-remove-" + index
                        text: "移除"; onClicked: root.removeSaved(index)
                    }
                }
            }
        }
    }
    Next.GlassDialog {
        id: closeDialog
        objectName: "ssh-close-confirmation"
        palette: root.palette
        parent: Overlay.overlay; anchors.centerIn: parent
        modal: true; title: "关闭监控会话？"
        standardButtons: Dialog.NoButton
        onClosed: root.pendingClose = null
        ColumnLayout {
            Label {
                text: root.pendingClose ? "将断开「" + root.pendingClose.sessionName + "」的 SSH 连接并删除此会话。" : ""
                textFormat: Text.PlainText; color: root.theme ? root.theme.text : "white"
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Next.GlassButton { objectName: "ssh-close-cancel"; text: "取消"; onClicked: closeDialog.close() }
                Next.GlassButton {
                    objectName: "ssh-close-accept"; text: "断开并关闭"
                    onClicked: { const page = root.pendingClose; closeDialog.close(); root.removeSession(page); }
                }
            }
        }
    }
}
