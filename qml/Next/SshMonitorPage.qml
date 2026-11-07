import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt.Next as Next

GlassPage {
    id: root
    title: "实时监控"
    property var backend
    property var sessions: []
    property var savedConnections: []
    property var fileWindows: ({})
    property int currentIndex: -1
    readonly property var activeSession: currentIndex >= 0 && currentIndex < sessions.length ? sessions[currentIndex] : null
    property int nextSessionId: 1
    property int nextGroupId: 1
    property int nextWindowId: 1
    property var containers: [mainContainer]
    property var renameTarget: null
    property var draggedSession: null
    property bool dragCancelled: false
    property var dragTarget: null
    property int windowStack: 1
    Connections {
        target: root.ownerWindow || root.Window.window
        function onActiveChanged() { if (target.active) mainContainer.stackingOrder = ++root.windowStack; }
    }
    function tabDropTarget(point) {
        const ordered = containers.slice().sort(function(a, b) { return b.stackingOrder - a.stackingOrder; });
        for (const container of ordered) {
            if (container.window && !container.window.visible) continue;
            for (const group of container.groups) {
                if (!group.visualHost || !group.visualHost.visible) continue;
                const local = group.visualHost.mapFromGlobal(point.x, point.y);
                if (local.x < 0 || local.y < 0 || local.x >= group.visualHost.width || local.y >= group.visualHost.height) continue;
                const header = group.tabHost;
                const position = header.mapFromGlobal(point.x, point.y);
                if (position.y >= 0 && position.y < header.height) {
                    let index = header.indexAt(position.x + header.contentX, position.y);
                    if (index < 0) index = group.tabs.length;
                    else { const tab = header.itemAtIndex(index); if (tab && position.x + header.contentX > tab.x + tab.width / 2) ++index; }
                    return {group: group, index: index, split: false, inHeader: true};
                }
                const content = group.contentHost.mapFromGlobal(point.x, point.y);
                const edge = Math.min(64, group.contentHost.width * 0.15);
                const side = content.x < edge ? "left" : content.x > group.contentHost.width - edge ? "right" : "";
                return {group: group, index: group.tabs.length, side: side,
                    split: content.y >= 0 && (!group.active || content.y < group.active.monitorPreviewBottom) && side !== "" && container.groups.length === 1
                        && (group.tabs.length > 1 || draggedSession && draggedSession.group !== group)};
            }
        }
        return null;
    }
    function finishTabDrag(page, point) {
        const target = tabDropTarget(point), cancelled = dragCancelled;
        draggedSession = null; dragTarget = null;
        if (cancelled || !page || sessions.indexOf(page) < 0) return;
        if (target) {
            let index = target.index;
            if (target.group === page.group && index > page.group.tabs.indexOf(page)) --index;
            moveSession(page, target.group, index);
            if (target.split) splitSession(page, target.side);
        } else {
            const windows = containers.filter(function(container) { return !!container.window && container.window.visible; }).map(function(container) { return container.window; });
            const main = ownerWindow || page.Window.window;
            if (main) windows.push(main);
            if (!windows.some(function(window) { return point.x >= window.x && point.x < window.x + window.width && point.y >= window.y && point.y < window.y + window.height; })) detachSession(page);
        }
    }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: root.draggedSession !== null; onActivated: root.dragCancelled = true }
    QtObject {
        id: mainContainer
        property int windowId: 0
        property int stackingOrder: 0
        property var window: null
        property var groups: []
        property var activeGroup: null
        property real splitRatio: 0.5
    }
    Component {
        id: containerComponent
        QtObject {
            property int windowId: 0
            property int stackingOrder: 0
            property var window: null
            property var groups: []
            property var activeGroup: null
            property real splitRatio: 0.5
        }
    }
    Component {
        id: groupComponent
        QtObject {
            property int groupId: 0
            property var container: null
            property var tabs: []
            property var active: null
            property Item contentHost: null
            property Item visualHost: null
            property Item tabHost: null
            property var menu: null
        }
    }
    property bool ready: false
    property bool writable: true
    property string workspaceError: ""
    property var pendingClose: null
    property bool sidebarCollapsed: false
    property bool shuttingDown: false
    property var connectionEditorPage: null
    property int connectionEditorSavedIndex: -1
    property string connectionEditorOldKey: ""
    property string connectionEditorNameSuffix: ""
    property bool applyingConfiguration: false
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
    function saveMonitorRule(id, name, pattern, ignoreCase, color, pauseOnMatch, highlightWholeLine, sourceSessionId) {
        const rule = {id: id > 0 ? id : nextMonitorRuleId, name: name.trim(), pattern: pattern, ignoreCase: ignoreCase,
            color: color || "#e8b76a", pauseOnMatch: pauseOnMatch !== false, highlightWholeLine: highlightWholeLine !== false};
        if (!validMonitorRule(rule)) { monitorRuleError = "请填写名称及有效的正则表达式（最多 4096 字符）。"; return 0; }
        if (id < 1 && monitorRules.length >= 256) { monitorRuleError = "最多保存 256 条规则。"; return 0; }
        const before = monitorRules, next = nextMonitorRuleId;
        const found = monitorRules.some(function(value) { return value.id === id; });
        if (id > 0 && !found) { monitorRuleError = "该规则已被删除，请重新选择。"; return 0; }
        const source = sourceSessionId > 0 ? sessions.find(function(page) { return page.sessionId === sourceSessionId; }) : null;
        if (sourceSessionId > 0 && !source) { monitorRuleError = "来源连接已关闭，请从当前连接重新添加规则。"; return 0; }
        const previousIds = source ? source.monitorRuleIds : null, previousEnabled = source ? source.monitorEnabled : false;
        monitorRules = found ? monitorRules.map(function(value) { return value.id === id ? rule : value; }) : monitorRules.concat([rule]);
        if (id < 1) nextMonitorRuleId++;
        if (source) { source.toggleMonitorRule(rule.id, true); source.monitorEnabled = true; }
        if (!saveWorkspace()) {
            monitorRules = before; nextMonitorRuleId = next;
            if (source) { source.monitorRuleIds = previousIds; source.monitorEnabled = previousEnabled; }
            monitorRuleError = workspaceError; return 0;
        }
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
    function addMonitorRuleFromText(text, window, sourceSessionId) {
        if (!text.trim()) return;
        openMonitorRules(window);
        ruleWindow.requestAction(function() { ruleWindow.loadText(text, sourceSessionId); });
    }
    Next.SshMonitorRuleWindow { id: ruleWindow; theme: root.theme; library: root }

    function openFileTransfer(config) {
        if (!config || shuttingDown) return;
        const key = connectionKey(config);
        let window = fileWindows[key];
        if (!window) {
            window = fileWindowComponent.createObject(root);
            if (!window) return;
            fileWindows[key] = window;
            window.closing.connect(function() {
                delete root.fileWindows[key];
                window.destroy();
            });
        }
        window.present(root.ownerWindow || root.Window.window, config);
    }
    Component {
        id: fileWindowComponent
        Next.SshFileTransferWindow { theme: root.theme; palette: root.palette }
    }

    QtObject {
        id: commandLibrary
        property var commands: []
        property bool loaded: false
    }
    Timer { id: saveTimer; interval: 500; onTriggered: root.saveWorkspace() }
    function scheduleSave() { if (ready) saveTimer.restart(); }
    function blankConfiguration(name) {
        return {name: name, host: "", port: "22", username: "root", password: "", encoding: "UTF-8", maxLines: "10000",
            autoReconnect: true, reconnectCount: "5", reconnectInterval: "2000", expanded: false,
            commandManagementExpanded: false};
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
            && (config.logDestination === undefined || config.logDestination === "file" || config.logDestination === "folder")
            && (config.logFile === undefined || typeof config.logFile === "string" && config.logFile.length <= 4096
                && (config.logFile === "" || config.logFile.startsWith("file:")))
            && (config.logFolder === undefined || typeof config.logFolder === "string" && config.logFolder.length <= 4096)
            && (config.logNameTemplate === undefined || typeof config.logNameTemplate === "string" && config.logNameTemplate.length <= 256)
            && (config.logRotateBySize === undefined || typeof config.logRotateBySize === "boolean")
            && (config.logRotateSizeMiB === undefined || Number.isInteger(config.logRotateSizeMiB) && config.logRotateSizeMiB >= 1 && config.logRotateSizeMiB <= 4096)
            && (config.logRotateByDuration === undefined || typeof config.logRotateByDuration === "boolean")
            && (config.logRotateMinutes === undefined || Number.isInteger(config.logRotateMinutes) && config.logRotateMinutes >= 1 && config.logRotateMinutes <= 1440)
            && (config.logDestination !== "folder" || typeof config.logFolder === "string" && config.logFolder.length > 0
                && typeof config.logNameTemplate === "string" && config.logNameTemplate.length > 0)
            && (config.monitorEnabled === undefined || typeof config.monitorEnabled === "boolean")
            && (config.monitorRuleIds === undefined || Array.isArray(config.monitorRuleIds) && config.monitorRuleIds.length <= 256
                && config.monitorRuleIds.every(function(id, index, ids) { return Number.isInteger(id) && id > 0 && id < 2147483647 && ids.indexOf(id) === index; }));
    }

    function createGroup(container) {
        const group = groupComponent.createObject(root, {groupId: nextGroupId++, container: container});
        container.groups = container.groups.concat([group]);
        if (!container.activeGroup) container.activeGroup = group;
        return group;
    }
    function targetGroup() {
        return activeSession && activeSession.group ? activeSession.group : mainContainer.activeGroup || mainContainer.groups[0];
    }
    function activateSession(page) {
        if (!page || sessions.indexOf(page) < 0) return;
        page.group.active = page;
        page.group.container.activeGroup = page.group;
        currentIndex = sessions.indexOf(page);
        scheduleSave();
    }
    function addSession(config, id, group) {
        if (sessions.length >= 64) { workspaceError = "最多同时打开 64 个监控会话，请先关闭不需要的会话。"; return null; }
        const number = id === undefined ? nextSessionId++ : id;
        nextSessionId = Math.max(nextSessionId, number + 1);
        group = group || targetGroup();
        const page = sessionComponent.createObject(sessionHost, {sessionId: number, sessionName: "监控 " + number, group: group});
        if (!page) { workspaceError = "监控会话创建失败。"; return null; }
        if (config) page.restoreConfiguration(config);
        else if (ready) page.restoreConfiguration(blankConfiguration(page.sessionName));
        sessions = sessions.concat([page]);
        group.tabs = group.tabs.concat([page]);
        activateSession(page);
        return page;
    }
    function newConnectedSession(config, group) {
        const copy = Object.assign({}, config);
        const base = (copy.name || copy.host || "监控").replace(/ · \d+$/, "");
        const used = sessions.map(function(page) { return page.sessionName; });
        let number = 1;
        while (used.indexOf(base + " · " + number) >= 0) ++number;
        copy.name = base + " · " + number;
        copy.expanded = false;
        const page = addSession(copy, undefined, group);
        if (page) {
            page.connectAction();
            if (!page.sessionActive && !page.pendingSshAction) page.connectionConfigExpanded = true;
        }
        return page;
    }
    function cloneSession(page) {
        return page && sessions.indexOf(page) >= 0 ? newConnectedSession(page.connectionConfiguration(), page.group) : null;
    }
    function layoutSnapshot() {
        return containers.filter(function(container) {
            return container === mainContainer || container.groups.some(function(group) { return group.tabs.length > 0; });
        }).map(function(container) {
            const window = container.window;
            const groups = container.groups.filter(function(group) { return group.tabs.length > 0; });
            if (!groups.length) groups.push(container.groups[0]);
            return {windowId: container.windowId, splitRatio: container.splitRatio,
                activeGroup: Math.max(0, groups.indexOf(container.activeGroup)),
                geometry: window ? {x: window.x, y: window.y, width: window.width, height: window.height} : null,
                groups: groups.map(function(group) {
                    return {tabs: group.tabs.map(function(page) { return page.sessionId; }), activeId: group.active ? group.active.sessionId : -1};
                })};
        });
    }
    function validLayout(layout, ids) {
        if (!Array.isArray(layout) || !layout.length || layout.length > 65 || layout[0].windowId !== 0) return false;
        const seen = [], windows = [];
        for (const container of layout) {
            if (!Number.isSafeInteger(container.windowId) || container.windowId < 0 || container.windowId >= 2147483647 || windows.indexOf(container.windowId) >= 0
                    || !Number.isFinite(container.splitRatio) || container.splitRatio < 0.1 || container.splitRatio > 0.9
                    || !Array.isArray(container.groups) || container.groups.length < 1 || container.groups.length > 2
                    || !Number.isInteger(container.activeGroup) || container.activeGroup < 0 || container.activeGroup >= container.groups.length) return false;
            windows.push(container.windowId);
            const rect = container.geometry;
            if (container.windowId && (!rect || ![rect.x, rect.y, rect.width, rect.height].every(Number.isFinite)
                    || rect.width < 360 || rect.width > 20000 || rect.height < 300 || rect.height > 20000
                    || Math.abs(rect.x) > 100000 || Math.abs(rect.y) > 100000)) return false;
            for (const group of container.groups) {
                if (!Array.isArray(group.tabs) || group.tabs.length > 64 || (group.tabs.length ? group.tabs.indexOf(group.activeId) < 0 : group.activeId !== -1)) return false;
                for (const id of group.tabs) {
                    if (ids.indexOf(id) < 0 || seen.indexOf(id) >= 0) return false;
                    seen.push(id);
                }
            }
            if (container.windowId && !container.groups.some(function(group) { return group.tabs.length; })) return false;
        }
        return seen.length === ids.length;
    }
    function saveWorkspace() {
        if (!ready) return true;
        if (!writable) return false;
        saveTimer.stop();
        const rows = sessions.map(function(page) { return {id: page.sessionId, config: page.connectionConfiguration()}; });
        const saved = backend && backend.setUiSetting("next/sshMonitor/workspace",
            JSON.stringify({version: 2, sessions: rows, connections: savedConnections, currentIndex: currentIndex,
                sidebarCollapsed: sidebarCollapsed, monitorRules: monitorRules, nextMonitorRuleId: nextMonitorRuleId,
                layout: layoutSnapshot()}));
        workspaceError = saved ? "" : "监控配置保存失败，内容仍保留在当前窗口，请检查磁盘或目录权限后重试。";
        return !!saved;
    }
    function createMonitorWindow(saved) {
        if (!saved) {
            const cached = containers.find(function(value) { return value.window && !value.window.visible && value.groups.every(function(group) { return !group.tabs.length; }); });
            if (cached) return cached;
        }
        const container = containerComponent.createObject(root, {windowId: saved ? saved.windowId : nextWindowId++});
        nextWindowId = Math.max(nextWindowId, container.windowId + 1);
        const window = windowComponent.createObject(root, {container: container});
        if (!window) { container.destroy(); workspaceError = "独立窗口创建失败。"; return null; }
        container.window = window;
        containers = containers.concat([container]);
        createGroup(container);
        if (saved) {
            container.splitRatio = saved.splitRatio;
            window.width = Math.max(window.minimumWidth, saved.geometry.width);
            window.height = Math.max(window.minimumHeight, saved.geometry.height);
            // Keep the restored title bar reachable after a display is removed.
            const rect = window.screen.virtualX === undefined ? null : window.screen;
            window.x = rect ? Math.max(rect.virtualX, Math.min(saved.geometry.x, rect.virtualX + rect.width - 120)) : saved.geometry.x;
            window.y = rect ? Math.max(rect.virtualY, Math.min(saved.geometry.y, rect.virtualY + rect.height - 80)) : saved.geometry.y;
        } else if (ownerWindow) {
            window.screen = ownerWindow.screen;
            window.x = ownerWindow.x + 40; window.y = ownerWindow.y + 40;
        }
        return container;
    }
    function restoreLayout(layout) {
        for (const container of containers) for (const group of container.groups) { group.tabs = []; group.active = null; }
        for (const saved of layout) {
            const container = saved.windowId === 0 ? mainContainer : createMonitorWindow(saved);
            if (!container) continue;
            container.splitRatio = saved.splitRatio;
            while (container.groups.length < saved.groups.length) createGroup(container);
            saved.groups.forEach(function(value, index) {
                const group = container.groups[index];
                group.tabs = value.tabs.map(function(id) { return root.sessionById(id); });
                for (const page of group.tabs) page.group = group;
                group.active = value.activeId < 0 ? null : root.sessionById(value.activeId);
            });
            container.activeGroup = container.groups[saved.activeGroup];
            if (container.window) container.window.show();
        }
        const placed = containers.reduce(function(all, container) {
            return all.concat(container.groups.reduce(function(tabs, group) { return tabs.concat(group.tabs); }, []));
        }, []);
        for (const page of sessions) if (placed.indexOf(page) < 0) moveSession(page, mainContainer.groups[0]);
    }
    function sessionById(id) { return sessions.find(function(page) { return page.sessionId === id; }) || null; }
    function cleanGroup(group) {
        const container = group.container;
        if (group.tabs.length) return;
        if (container.groups.length > 1) {
            container.groups = container.groups.filter(function(value) { return value !== group; });
            container.activeGroup = container.groups[0];
            Qt.callLater(function() { group.destroy(); });
        }
        if (container.window && !container.groups.some(function(value) { return value.tabs.length; })) container.window.hide();
    }
    function moveSession(page, group, index) {
        if (!page || sessions.indexOf(page) < 0 || !group) return false;
        const from = page.group;
        page.beginWindowMove();
        from.tabs = from.tabs.filter(function(value) { return value !== page; });
        if (from.active === page) from.active = from.tabs.length ? from.tabs[Math.min(Math.max(0, index || 0), from.tabs.length - 1)] : null;
        const tabs = group.tabs.slice();
        tabs.splice(index === undefined ? tabs.length : Math.max(0, Math.min(index, tabs.length)), 0, page);
        group.tabs = tabs;
        if (from.container.window) page.lastMonitorWindow = from.container.window;
        page.group = group;
        if (group.container.window) page.lastMonitorWindow = group.container.window;
        activateSession(page);
        if (from !== group) cleanGroup(from);
        Qt.callLater(function() { if (root.sessions.indexOf(page) >= 0) page.endWindowMove(); });
        scheduleSave();
        return true;
    }
    function openSessionMenu(page) {
        if (!page || sessions.indexOf(page) < 0) return;
        activateSession(page);
        Qt.callLater(function() {
            if (root.sessions.indexOf(page) < 0 || !page.group.menu) return;
            const header = page.group.tabHost;
            const tab = header.itemAtIndex(page.group.tabs.indexOf(page));
            page.group.menu.popup(tab || header, 0, 26);
        });
    }
    function splitSession(page, side) {
        if (!page) return false;
        const container = page.group.container;
        const affected = container.groups.reduce(function(all, group) { return all.concat(group.tabs); }, []);
        for (const session of affected) session.beginWindowMove();
        const destination = container.groups.length < 2 ? createGroup(container)
            : container.groups.find(function(group) { return group !== page.group; });
        page.connectionConfigExpanded = false;
        const moved = moveSession(page, destination);
        if (moved && side === "left" && container.groups.indexOf(destination) > 0) container.groups = [destination, container.groups[0]];
        Qt.callLater(function() { for (const session of affected) if (root.sessions.indexOf(session) >= 0) session.endWindowMove(); });
        return moved;
    }
    function mergeGroups(container) {
        if (container.groups.length < 2) return;
        const destination = container.groups[0], source = container.groups[1];
        const active = container.activeGroup.active;
        for (const page of source.tabs.slice()) moveSession(page, destination);
        if (!source.tabs.length) cleanGroup(source);
        if (active) activateSession(active);
        scheduleSave();
    }
    function locateSession(page) {
        if (!page || !page.detached) return;
        activateSession(page);
        const window = page.monitorWindow;
        if (window.visibility === Window.Minimized) window.showNormal(); else window.show();
        window.raise(); window.requestActivate();
    }
    function detachSession(page) {
        if (!page || sessions.indexOf(page) < 0) return false;
        const container = createMonitorWindow();
        if (!container) return false;
        page.connectionConfigExpanded = false;
        moveSession(page, container.groups[0]);
        locateSession(page);
        return true;
    }
    function dockSession(page) {
        if (!page || !page.detached || sessions.indexOf(page) < 0) return false;
        moveSession(page, mainContainer.activeGroup || mainContainer.groups[0]);
        root.requested();
        if (ownerWindow) { ownerWindow.show(); ownerWindow.raise(); ownerWindow.requestActivate(); }
        return true;
    }
    function dockWindow(container) {
        const tabs = container.groups.reduce(function(all, group) { return all.concat(group.tabs); }, []);
        for (const page of tabs) dockSession(page);
    }
    function connectSession(page) { if (page) { activateSession(page); page.connectAction(); } }
    function requestCloseSession(page) {
        if (!page || sessions.indexOf(page) < 0) return;
        if (page.sessionActive || page.pendingSshAction || page.terminal.logging || page.terminal.logBusy) {
            pendingClose = page; closeDialog.open();
        } else removeSession(page);
    }
    function removeSession(page) {
        const index = sessions.indexOf(page);
        if (index < 0) return false;
        const before = sessions, selected = currentIndex, group = page.group, tabs = group.tabs, active = group.active;
        sessions = sessions.filter(function(value) { return value !== page; });
        group.tabs = group.tabs.filter(function(value) { return value !== page; });
        if (group.active === page) group.active = group.tabs[Math.min(tabs.indexOf(page), group.tabs.length - 1)] || null;
        currentIndex = sessions.length ? Math.max(0, Math.min(selected - (index < selected ? 1 : 0), sessions.length - 1)) : -1;
        if (!saveWorkspace()) { sessions = before; group.tabs = tabs; group.active = active; currentIndex = selected; return false; }
        cleanGroup(group);
        page.closeSshSession(); page.destroy();
        return true;
    }
    function requestRename(page) { renameTarget = page; renameInput.text = page.sessionName; renameDialog.open(); }
    function renameSession(page, name) {
        name = name.trim();
        if (!page || !name || name.length > 100) return false;
        const before = page.sessionName; page.sessionName = name;
        if (!saveWorkspace()) { page.sessionName = before; return false; }
        return true;
    }
    function shutdown() {
        if (shuttingDown) return;
        saveWorkspace(); shuttingDown = true; saveTimer.stop(); ruleWindow.close();
        for (const key of Object.keys(fileWindows)) fileWindows[key].close();
        for (const container of containers) if (container.window) container.window.hide();
        for (const page of sessions) page.closeSshSession();
    }
    onSidebarCollapsedChanged: scheduleSave()
    onCurrentIndexChanged: { const page = sessions[currentIndex]; if (page && page.group) { page.group.active = page; page.group.container.activeGroup = page.group; } scheduleSave(); }
    onClosing: shutdown()
    Component.onCompleted: { createGroup(mainContainer); restoreWorkspace(); }
    Component.onDestruction: shutdown()
    function restoreWorkspace() {
        let state = null;
        const raw = backend.getUiSetting("next/sshMonitor/workspace", "");
        if (raw !== "") {
            try {
                state = JSON.parse(raw);
                if (!state || [1, 2].indexOf(state.version) < 0 || !Array.isArray(state.sessions) || state.sessions.length > 64
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
                if (state.version === 2 && !validLayout(state.layout, ids))
                    throw new Error("Invalid monitor layout");
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
            if (state.version === 2) restoreLayout(state.layout);
            for (const page of sessions) {
                const saved = savedConnections.find(function(config) {
                    return root.connectionKey(config) === root.connectionKey(page.connectionConfiguration());
                });
                if (saved) page.savedConnectionKey = root.connectionKey(saved);
            }
            currentIndex = state.currentIndex;
            if (sessions[currentIndex]) activateSession(sessions[currentIndex]);
        } else addSession();
        ready = true;
    }
    function connectionKey(config) {
        return JSON.stringify([config.host.trim().toLowerCase(), Number(config.port), config.username.trim()]);
    }
    function savedConfiguration(config, page) {
        const row = Object.assign({}, config);
        row.host = String(row.host || "").trim();
        row.username = String(row.username || "").trim();
        row.password = page && page.secureCredentials ? "" : String(row.password || "");
        row.expanded = false;
        delete row.autoFollowEnabled;
        return row;
    }
    function saveSessionLogSettings(page, config) {
        const target = page.connectionConfiguration();
        const key = connectionKey(target);
        const hasTarget = target.host.trim() && target.username.trim() && Number(target.port) > 0;
        const matching = hasTarget ? sessions.filter(function(session) {
            return connectionKey(session.connectionConfiguration()) === key;
        }) : [page];
        const before = matching.map(function(session) { return session.logConfiguration(); });
        const savedBefore = savedConnections;
        applyingConfiguration = true;
        for (const session of matching) session.restoreLogConfiguration(config);
        if (hasTarget) {
            savedConnections = savedConnections.map(function(row) {
                return connectionKey(row) === key ? Object.assign({}, row, config) : row;
            });
        }
        const saved = saveWorkspace();
        if (!saved) {
            savedConnections = savedBefore;
            matching.forEach(function(session, index) { session.restoreLogConfiguration(before[index]); });
        }
        applyingConfiguration = false;
        return saved;
    }
    function openConnectionEditor(page, savedIndex) {
        let config = null, index = savedIndex === undefined ? -1 : savedIndex;
        if (page) {
            if (sessions.indexOf(page) < 0) return;
            activateSession(page);
            config = page.connectionConfiguration();
            index = savedConnections.findIndex(function(value) { return connectionKey(value) === page.savedConnectionKey; });
            if (index < 0) index = savedConnections.findIndex(function(value) { return connectionKey(value) === connectionKey(config); });
            const suffix = page.sessionName.match(/^(.*?) · (\d+)$/);
            connectionEditorNameSuffix = index >= 0 && suffix ? " · " + suffix[2] : "";
        } else {
            if (index < 0 || index >= savedConnections.length) return;
            config = Object.assign({}, savedConnections[index]);
            connectionEditorNameSuffix = "";
        }
        const draft = Object.assign({}, config);
        if (index >= 0) draft.name = savedConnections[index].name;
        connectionEditorPage = page;
        connectionEditorSavedIndex = index;
        connectionEditorOldKey = page ? page.savedConnectionKey || connectionKey(config) : connectionKey(config);
        const matchingPage = page || sessions.find(function(value) {
            return connectionKey(value.connectionConfiguration()) === connectionKey(config);
        }) || null;
        connectionEditor.openFor(draft, {
            identityLocked: !!page && page.sessionActive,
            secureCredentials: matchingPage ? matchingPage.secureCredentials : false,
            hasSavedPassword: matchingPage ? matchingPage.terminal.hasSavedPassword : false,
            credentialStatus: matchingPage ? matchingPage.passwordNotice || matchingPage.terminal.credentialError : "",
            canManageCredentials: !!page && page.secureCredentials
        });
    }
    function commitConnectionEditor(config, keepOpen) {
        const page = connectionEditorPage;
        let index = connectionEditorSavedIndex;
        const beforeRows = savedConnections;
        const beforeConfig = page ? page.connectionConfiguration() : null;
        applyingConfiguration = true;
        if (page) {
            if (index < 0) index = savedConnections.findIndex(function(value) { return connectionKey(value) === connectionEditorOldKey; });
            const pageConfig = Object.assign({}, config, {name: config.name + connectionEditorNameSuffix});
            if (!page.applyConnectionConfiguration(pageConfig)) {
                applyingConfiguration = false;
                connectionEditor.showError("当前连接正在使用，主机、端口、用户和密码需断开后修改。");
                return false;
            }
        }
        const rows = savedConnections.slice();
        if (page && index >= 0) rows[index] = savedConfiguration(config, page);
        else if (page && config.host.trim() && config.username.trim()
                && (page.secureCredentials || config.password) && page.targetMatchesBackend())
            rows.push(savedConfiguration(config, page));
        else if (!page && index >= 0) rows[index] = savedConfiguration(config, null);
        savedConnections = rows;
        if (!saveWorkspace()) {
            savedConnections = beforeRows;
            if (page && beforeConfig) page.applyConnectionConfiguration(beforeConfig);
            applyingConfiguration = false;
            connectionEditor.showError(workspaceError);
            return false;
        }
        if (page && index >= 0) page.savedConnectionKey = connectionKey(rows[index]);
        applyingConfiguration = false;
        if (!keepOpen) connectionEditor.close();
        return true;
    }
    function syncSavedConnection(page) {
        if (!page || sessions.indexOf(page) < 0 || !page.targetMatchesBackend()) return;
        const config = page.connectionConfiguration(), key = connectionKey(config);
        let index = savedConnections.findIndex(function(value) { return page.savedConnectionKey && connectionKey(value) === page.savedConnectionKey; });
        if (index < 0) index = savedConnections.findIndex(function(value) { return connectionKey(value) === key; });
        if (index < 0) return;
        const rows = savedConnections.slice();
        rows[index] = savedConfiguration(Object.assign({}, config, {name: rows[index].name}), page);
        savedConnections = rows;
        page.savedConnectionKey = key;
    }
    function saveEditorCredential(password) {
        const message = connectionEditor.validationMessage();
        if (message) { connectionEditor.showError(message); return; }
        if (!connectionEditorPage || !commitConnectionEditor(connectionEditor.configuration(), true)) return;
        if (!connectionEditorPage.saveConnectionCredential(password))
            connectionEditor.showError("请在断开连接后保存系统密码。");
        else connectionEditor.credentialStatus = "系统密码保存处理中…";
    }
    function clearEditorCredential() {
        if (!connectionEditorPage) return;
        if (!connectionEditorPage.clearConnectionCredential())
            connectionEditor.showError("请在断开连接后清除系统密码。");
    }
    function rememberConnection(page) {
        const config = page.connectionConfiguration();
        if (!config.host.trim() || !config.username.trim() || (!config.password && !page.secureCredentials) || !page.targetMatchesBackend()) return;
        const key = connectionKey(config);
        const rows = savedConnections.slice();
        const index = rows.findIndex(function(value) { return root.connectionKey(value) === key; });
        config.name = index < 0 ? config.name.replace(/ · \d+$/, "") : rows[index].name;
        if (index < 0) rows.push(savedConfiguration(config, page)); else rows[index] = savedConfiguration(config, page);
        savedConnections = rows;
        page.savedConnectionKey = key;
        saveWorkspace();
    }
    function connectSaved(config) {
        const page = newConnectedSession(config, mainContainer.activeGroup);
        if (page) page.savedConnectionKey = connectionKey(config);
        return page;
    }
    function removeSaved(index) {
        const before = savedConnections;
        savedConnections = savedConnections.filter(function(value, row) { return row !== index; });
        if (!saveWorkspace()) savedConnections = before;
    }

    Component {
        id: sessionComponent
        Next.LiveLogDialog {
            id: sessionPage
            objectName: "ssh-session-" + sessionId
            readonly property Item connectionEditorOverlay: Overlay.overlay
            property var group: null
            property var lastMonitorWindow: null
            readonly property var monitorWindow: group && group.container.window ? group.container.window : lastMonitorWindow
            parent: group && group.contentHost ? group.contentHost : sessionHost
            anchors.fill: parent
            detached: !!(group && group.container.window)
            visible: !!group && group.active === sessionPage && (detached ? monitorWindow.visible : root.visible)
            enabled: !root.waitingForLogShutdown
            backend: root.backend; theme: root.theme
            ownerWindow: detached ? monitorWindow : root.ownerWindow
            persistConnectionSettings: false; workspaceManaged: true
            sharedCommands: commandLibrary; monitorRuleLibrary: root
            commitLogSettings: function(config) { return root.saveSessionLogSettings(sessionPage, config); }
            onConfigurationEdited: {
                if (root.applyingConfiguration) return;
                root.syncSavedConnection(sessionPage);
                root.scheduleSave();
            }
            onConnectionSettingsRequested: root.openConnectionEditor(sessionPage)
            Connections {
                target: sessionPage.terminal
                function onConnectedChanged() { if (target.connected) root.rememberConnection(sessionPage); }
                function onLoggingChanged() { if (root.waitingForLogShutdown) Qt.callLater(root.continueLogShutdown); }
            }
        }
    }
    Component {
        id: windowComponent
        ApplicationWindow {
            id: floatingWindow
            required property var container
            objectName: "ssh-monitor-window-" + container.windowId
            readonly property var activePage: container.activeGroup ? container.activeGroup.active : null
            title: (activePage ? activePage.sessionName + " · " : "") + "实时监控 — 通联工坊"
            width: 1100; height: 760; minimumWidth: 440; minimumHeight: 480
            flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint |
                   Qt.WindowCloseButtonHint | Qt.WindowSystemMenuHint
            modality: Qt.NonModal; transientParent: null
            color: root.theme ? root.theme.background : "#1d2025"; palette: root.palette; font: root.font
            onActiveChanged: if (active) container.stackingOrder = ++root.windowStack
            onXChanged: root.scheduleSave()
            onYChanged: root.scheduleSave()
            onWidthChanged: root.scheduleSave()
            onHeightChanged: root.scheduleSave()
            onClosing: function(event) { if (!root.shuttingDown) { event.accepted = false; root.dockWindow(container); } }
            ColumnLayout {
                anchors.fill: parent; spacing: 0
                RowLayout {
                    Layout.fillWidth: true; Layout.fillHeight: false; Layout.margins: 6
                    Label {
                        text: "实时监控"; color: root.theme ? root.theme.text : "white"
                        Layout.fillWidth: true; Layout.fillHeight: true
                        verticalAlignment: Text.AlignVCenter
                        MouseArea {
                            anchors.fill: parent
                            enabled: floatingWindow.visibility === Window.Windowed
                            onPressed: floatingWindow.startSystemMove()
                        }
                    }
                    Next.GlassButton {
                        objectName: "ssh-window-maximize-" + (floatingWindow.activePage ? floatingWindow.activePage.sessionId : 0)
                        text: floatingWindow.visibility === Window.Maximized ? "还原" : "最大化"; implicitHeight: 28
                        onClicked: floatingWindow.visibility === Window.Maximized ? floatingWindow.showNormal() : floatingWindow.showMaximized()
                    }
                    Next.GlassButton {
                        objectName: "ssh-window-dock-" + (floatingWindow.activePage ? floatingWindow.activePage.sessionId : 0)
                        text: "收回主窗口"; implicitHeight: 28; onClicked: root.dockWindow(floatingWindow.container)
                    }
                }
                Next.SshMonitorSurface { workspace: root; container: floatingWindow.container; Layout.fillWidth: true; Layout.fillHeight: true }
            }
        }
    }
    Item { id: sessionHost; visible: false }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 6; spacing: 6
        Label {
            objectName: "ssh-workspace-error"; visible: text !== ""; Layout.fillWidth: true
            text: root.workspaceError; wrapMode: Text.Wrap; color: root.theme ? root.theme.error : "#f79a93"
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 6
            Rectangle {
                id: sidebar
                objectName: "ssh-session-sidebar"
                Layout.preferredWidth: root.sidebarCollapsed ? 42 : 210; Layout.fillHeight: true
                color: root.theme ? root.theme.sidebar : "#202329"; radius: 6
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 6; spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Label { visible: !root.sidebarCollapsed; text: "SSH 连接"; color: root.theme ? root.theme.text : "white"; Layout.fillWidth: true }
                        Next.GlassButton {
                            objectName: "ssh-sidebar-toggle"; text: root.sidebarCollapsed ? "›" : "‹"
                            implicitWidth: 28; implicitHeight: 28; padding: 0; horizontalPadding: 0
                            Accessible.name: root.sidebarCollapsed ? "展开 SSH 连接" : "折叠 SSH 连接"
                            onClicked: root.sidebarCollapsed = !root.sidebarCollapsed
                        }
                    }
                    Next.GlassButton {
                        objectName: "ssh-new-session"; text: root.sidebarCollapsed ? "+" : "+ 新建连接"
                        Layout.fillWidth: true; implicitHeight: 30; horizontalPadding: root.sidebarCollapsed ? 0 : 8
                        Accessible.name: "新建连接"; onClicked: root.addSession(undefined, undefined, mainContainer.activeGroup)
                    }
                    Next.GlassTextField {
                        id: search; objectName: "ssh-connection-search"; visible: !root.sidebarCollapsed
                        Layout.fillWidth: true; implicitHeight: 30; placeholderText: "搜索连接"
                    }
                    ListView {
                        objectName: "ssh-saved-connections"; visible: !root.sidebarCollapsed
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 2
                        model: root.savedConnections.length
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ItemDelegate {
                            id: savedRow
                            required property int index
                            readonly property var config: root.savedConnections[index]
                            readonly property bool matches: (config.name + " " + config.host + " " + config.username).toLowerCase().indexOf(search.text.toLowerCase()) >= 0
                            objectName: "ssh-saved-row-" + index
                            width: ListView.view.width; height: matches ? 52 : 0; visible: matches
                            highlighted: !!root.activeSession && root.connectionKey(root.activeSession.connectionConfiguration()) === root.connectionKey(config)
                            Accessible.name: config.name + " " + config.host
                            Accessible.description: "右键、Enter 或菜单键打开连接操作"
                            Keys.onReturnPressed: savedMenu.popup()
                            Keys.onEnterPressed: savedMenu.popup()
                            Keys.onMenuPressed: savedMenu.popup()
                            contentItem: Column {
                                spacing: 3
                                Label { text: savedRow.config.name; color: root.theme ? root.theme.text : "white"; width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText }
                                Label { text: savedRow.config.host + ":" + savedRow.config.port; color: root.theme ? root.theme.subText : "#a7adb7"; font.pixelSize: 11; width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText }
                            }
                            background: Rectangle { radius: 5; color: savedRow.down || savedRow.highlighted ? (root.theme && (root.theme.lightMode || root.theme.monokaiMode) ? root.theme.navigationSelected : "#334b6b") : savedRow.hovered ? (root.theme && (root.theme.lightMode || root.theme.monokaiMode) ? root.theme.surfaceHover : "#303742") : "transparent" }
                            TapHandler { acceptedButtons: Qt.RightButton; onTapped: savedMenu.popup() }
                            Next.GlassMenu {
                                id: savedMenu
                                Next.GlassMenuItem {
                                    objectName: "ssh-saved-edit-" + savedRow.index; text: "编辑连接参数…"
                                    onTriggered: root.openConnectionEditor(null, savedRow.index)
                                }
                                Next.GlassMenuItem {
                                    objectName: "ssh-saved-connect-tab-" + savedRow.index; text: "在新标签中连接"
                                    onTriggered: root.connectSaved(savedRow.config)
                                }
                                Next.GlassMenuItem { text: "在新窗口中连接"; onTriggered: { const page = root.connectSaved(savedRow.config); if (page) root.detachSession(page); } }
                                Next.GlassMenuItem {
                                    objectName: "ssh-saved-transfer-menu-" + savedRow.index; text: "文件传输…"
                                    onTriggered: root.openFileTransfer(savedRow.config)
                                }
                                Next.GlassMenuItem { objectName: "ssh-saved-remove-" + savedRow.index; text: "移除配置"; onTriggered: root.removeSaved(savedRow.index) }
                            }
                        }
                        Label { anchors.centerIn: parent; visible: root.savedConnections.length === 0; text: "暂无保存的连接"; color: root.theme ? root.theme.subText : "#a7adb7"; font.pixelSize: 12 }
                    }
                    Item { visible: root.sidebarCollapsed; Layout.fillHeight: true }
                    Next.GlassButton {
                        objectName: "ssh-monitor-rules"; text: root.sidebarCollapsed ? "规则" : "规则库 (" + root.monitorRules.length + ")"
                        Layout.fillWidth: true; implicitHeight: 30; horizontalPadding: root.sidebarCollapsed ? 0 : 8; font.pixelSize: 12; onClicked: root.openMonitorRules(root.ownerWindow)
                    }
                }
            }
            Next.SshMonitorSurface { workspace: root; container: mainContainer; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 0 }
        }
    }
    component ConnectionEditor: Next.GlassDialog {
        id: editor
        objectName: "ssh-connection-editor"
        anchors.centerIn: parent
        modal: true
        title: "连接参数"
        standardButtons: Dialog.NoButton
        closePolicy: Popup.CloseOnEscape
        width: Math.min(720, parent ? parent.width - 28 : 720)
        padding: 16

        property var sourceConfig: ({})
        property bool identityLocked: false
        property bool secureCredentials: false
        property bool hasSavedPassword: false
        property bool canManageCredentials: false
        property string credentialStatus: ""
        property string errorText: ""

        signal commitRequested(var config)
        signal savePasswordRequested(string password)
        signal clearPasswordRequested()

        function openFor(config, options) {
            sourceConfig = Object.assign({}, config || {});
            delete sourceConfig.autoFollowEnabled;
            identityLocked = !!options.identityLocked;
            secureCredentials = !!options.secureCredentials;
            hasSavedPassword = !!options.hasSavedPassword;
            canManageCredentials = !!options.canManageCredentials;
            credentialStatus = options.credentialStatus || "";
            errorText = "";
            nameInput.text = sourceConfig.name || "";
            hostInput.text = sourceConfig.host || "";
            portInput.text = sourceConfig.port || "22";
            userInput.text = sourceConfig.username || "root";
            passwordInput.text = secureCredentials ? "" : sourceConfig.password || "";
            encodingCombo.currentIndex = sourceConfig.encoding === "GB2312" || sourceConfig.encoding === "GBK" ? 1 : 0;
            maxLinesInput.text = sourceConfig.maxLines || "10000";
            autoReconnectSwitch.checked = sourceConfig.autoReconnect !== false;
            reconnectCountInput.text = sourceConfig.reconnectCount || "5";
            reconnectIntervalInput.text = sourceConfig.reconnectInterval || "2000";
            copyOnSelectSwitch.checked = sourceConfig.copyOnSelect === true;
            wrapLinesSwitch.checked = sourceConfig.wrapLogLines === true;
            open();
        }
        function configuration() {
            return Object.assign({}, sourceConfig, {
                name: nameInput.text.trim(), host: hostInput.text.trim(), port: portInput.text.trim(),
                username: userInput.text.trim(),
                password: secureCredentials ? passwordInput.text || sourceConfig.password || "" : passwordInput.text,
                encoding: encodingCombo.currentText, maxLines: maxLinesInput.text,
                autoReconnect: autoReconnectSwitch.checked, reconnectCount: reconnectCountInput.text,
                reconnectInterval: reconnectIntervalInput.text,
                copyOnSelect: copyOnSelectSwitch.checked, wrapLogLines: wrapLinesSwitch.checked
            });
        }
        function validationMessage() {
            if (!nameInput.text.trim() || nameInput.text.length > 100) return "请填写 1～100 字符的连接名称。";
            if (!hostInput.text.trim() || hostInput.text.length > 1024) return "请填写有效的主机。";
            if (!userInput.text.trim() || userInput.text.length > 1024) return "请填写用户名。";
            if (!portInput.acceptableInput || Number(portInput.text) < 1 || Number(portInput.text) > 65535) return "端口须为 1～65535。";
            if (!maxLinesInput.acceptableInput) return "缓存行须为 100～200000。";
            if (autoReconnectSwitch.checked && !reconnectCountInput.acceptableInput) return "重连次数须为 0～20。";
            if (autoReconnectSwitch.checked && !reconnectIntervalInput.acceptableInput) return "重连间隔须为 500～30000 毫秒。";
            return "";
        }
        function submit() {
            const message = validationMessage();
            if (message) { errorText = message; return; }
            errorText = "";
            commitRequested(configuration());
        }
        function showError(text) {
            errorText = text || "连接参数保存失败。";
            if (!opened) open();
        }

        contentItem: ColumnLayout {
            spacing: 10
            Label {
                text: "连接"; color: root.theme ? root.theme.text : "#333333"
                font.pixelSize: 13; font.weight: Font.DemiBold
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 4; columnSpacing: 8; rowSpacing: 8

                Label { text: "名称"; color: root.theme ? root.theme.subText : "#777777"; font.pixelSize: 12 }
                Next.GlassTextField {
                    id: nameInput; objectName: "ssh-editor-name"
                    Layout.fillWidth: true; implicitHeight: 32; maximumLength: 100
                }
                Label { text: "主机"; color: root.theme ? root.theme.subText : "#777777"; font.pixelSize: 12 }
                Next.GlassTextField {
                    id: hostInput; objectName: "ssh-editor-host"
                    Layout.fillWidth: true; implicitHeight: 32; enabled: !editor.identityLocked
                    placeholderText: "192.168.1.1"
                }

                Label { text: "端口"; color: root.theme ? root.theme.subText : "#777777"; font.pixelSize: 12 }
                Next.GlassTextField {
                    id: portInput; objectName: "ssh-editor-port"
                    Layout.fillWidth: true; implicitHeight: 32; enabled: !editor.identityLocked
                    validator: IntValidator { bottom: 1; top: 65535 }
                }
                Label { text: "用户"; color: root.theme ? root.theme.subText : "#777777"; font.pixelSize: 12 }
                Next.GlassTextField {
                    id: userInput; objectName: "ssh-editor-username"
                    Layout.fillWidth: true; implicitHeight: 32; enabled: !editor.identityLocked
                    placeholderText: "root"
                }

                Label { text: "密码"; color: root.theme ? root.theme.subText : "#777777"; font.pixelSize: 12 }
                Next.GlassTextField {
                    id: passwordInput; objectName: "ssh-editor-password"
                    Layout.fillWidth: true; implicitHeight: 32; enabled: !editor.identityLocked
                    echoMode: TextInput.Password
                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                    placeholderText: editor.secureCredentials
                        ? (editor.hasSavedPassword ? "已保存；留空沿用" : "系统密码") : "连接密码（随连接保存）"
                }
                Label { text: "编码"; color: root.theme ? root.theme.subText : "#777777"; font.pixelSize: 12 }
                Next.GlassComboBox {
                    id: encodingCombo; objectName: "ssh-editor-encoding"
                    Layout.fillWidth: true; implicitHeight: 32; enabled: !editor.identityLocked
                    leftPadding: 0 // Basic.ComboBox 的内容项已自带 12px 左缩进
                    model: ["UTF-8", "GB2312"]
                }

                Label { text: "缓存行"; color: root.theme ? root.theme.subText : "#777777"; font.pixelSize: 12 }
                Next.GlassTextField {
                    id: maxLinesInput; objectName: "ssh-editor-buffer"
                    Layout.fillWidth: true; implicitHeight: 32; readOnly: editor.identityLocked
                    validator: IntValidator { bottom: 100; top: 200000 }
                }
                Label { text: "自动重连"; color: root.theme ? root.theme.subText : "#777777"; font.pixelSize: 12 }
                Next.GlassSwitch {
                    id: autoReconnectSwitch; objectName: "ssh-editor-auto-reconnect"
                    Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                    implicitHeight: 28
                }
            }

            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Label { text: "重连次数"; color: root.theme ? root.theme.subText : "#777777"; font.pixelSize: 12 }
                Next.GlassTextField {
                    id: reconnectCountInput; objectName: "ssh-editor-reconnect-count"
                    Layout.preferredWidth: 70; implicitHeight: 32
                    enabled: autoReconnectSwitch.checked
                    validator: IntValidator { bottom: 0; top: 20 }
                }
                Label { text: "间隔(ms)"; color: root.theme ? root.theme.subText : "#777777"; font.pixelSize: 12 }
                Next.GlassTextField {
                    id: reconnectIntervalInput; objectName: "ssh-editor-reconnect-interval"
                    Layout.preferredWidth: 90; implicitHeight: 32
                    enabled: autoReconnectSwitch.checked
                    validator: IntValidator { bottom: 500; top: 30000 }
                }
                Item { Layout.fillWidth: true }
            }

            Label {
                text: "显示"; color: root.theme ? root.theme.text : "#333333"
                font.pixelSize: 13; font.weight: Font.DemiBold
            }
            GridLayout {
                Layout.fillWidth: true; columns: 4; columnSpacing: 8
                Label { text: "选中即复制"; color: root.theme ? root.theme.subText : "#777777"; font.pixelSize: 12 }
                Next.GlassSwitch {
                    id: copyOnSelectSwitch; objectName: "ssh-editor-copy-on-select"
                    Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter; implicitHeight: 28
                }
                Label { text: "自动换行"; color: root.theme ? root.theme.subText : "#777777"; font.pixelSize: 12 }
                Next.GlassSwitch {
                    id: wrapLinesSwitch; objectName: "ssh-editor-wrap-lines"
                    Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter; implicitHeight: 28
                }
            }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: editor.identityLocked
                    ? "连接期间仅可修改复制、换行和重连参数；主机、端口、用户与密码需断开后修改。"
                    : "复制、换行及连接参数会随此连接保存，下次重连或再次打开时沿用。"
                color: root.theme ? root.theme.subText : "#777777"; font.pixelSize: 11
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 6
                visible: editor.secureCredentials || editor.credentialStatus !== ""
                Label {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                    text: editor.credentialStatus || (editor.hasSavedPassword ? "当前连接已在系统安全凭据中保存密码。" : "当前连接尚未保存系统密码。")
                    color: root.theme ? root.theme.subText : "#777777"; font.pixelSize: 11
                }
                RowLayout {
                    visible: editor.canManageCredentials; spacing: 8
                    Next.GlassButton {
                        objectName: "ssh-editor-save-password"; text: "保存密码"
                        enabled: !editor.identityLocked && passwordInput.text.length > 0
                        onClicked: editor.savePasswordRequested(passwordInput.text)
                    }
                    Next.GlassButton {
                        objectName: "ssh-editor-clear-password"; text: "清除已存密码"
                        enabled: !editor.identityLocked && editor.hasSavedPassword
                        onClicked: editor.clearPasswordRequested()
                    }
                    Item { Layout.fillWidth: true }
                }
            }
            Label {
                objectName: "ssh-editor-error"
                Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                visible: text !== ""; text: editor.errorText
                color: root.theme ? root.theme.error : "#d44a3a"; font.pixelSize: 12
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight; spacing: 8
                Next.GlassButton { objectName: "ssh-editor-cancel"; text: "取消"; onClicked: editor.close() }
                Next.GlassButton {
                    objectName: "ssh-editor-save"; text: "保存"; highlighted: true
                    onClicked: editor.submit()
                }
            }
        }
    }
    ConnectionEditor {
        id: connectionEditor
        parent: root.connectionEditorPage ? root.connectionEditorPage.connectionEditorOverlay : Overlay.overlay
        onCommitRequested: function(config) { root.commitConnectionEditor(config, false) }
        onSavePasswordRequested: function(password) { root.saveEditorCredential(password) }
        onClearPasswordRequested: root.clearEditorCredential()
    }
    Connections {
        target: root.connectionEditorPage
        function onPasswordNoticeChanged() {
            if (!connectionEditor.opened) return;
            connectionEditor.credentialStatus = target.passwordNotice;
            connectionEditor.hasSavedPassword = target.terminal.hasSavedPassword;
        }
    }
    Next.GlassDialog {
        id: renameDialog; objectName: "ssh-rename-dialog"
        parent: Overlay.overlay; anchors.centerIn: parent; modal: true; title: "重命名标签"; standardButtons: Dialog.NoButton
        onOpened: { renameInput.forceActiveFocus(); renameInput.selectAll(); }
        ColumnLayout {
            Next.GlassTextField { id: renameInput; objectName: "ssh-rename-input"; maximumLength: 100; Layout.preferredWidth: 260; onAccepted: if (root.renameSession(root.renameTarget, text)) renameDialog.close() }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Next.GlassButton { text: "取消"; onClicked: renameDialog.close() }
                Next.GlassButton { objectName: "ssh-rename-save"; text: "保存"; enabled: renameInput.text.trim().length > 0; onClicked: if (root.renameSession(root.renameTarget, renameInput.text)) renameDialog.close() }
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
