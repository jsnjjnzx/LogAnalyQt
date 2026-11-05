import QtQuick
import QtQuick.Controls
import QtQml.Models
import QtTest
import LogAnalyQt.Next as Next

Item {
    id: scene
    width: 1280; height: 900
    Next.GlassTheme { id: testTheme }
    Component { id: workspace; Next.SshMonitorPage { theme: testTheme; backend: testBackend } }
    Component { id: clipboardEditor; TextEdit { textFormat: TextEdit.PlainText; visible: false } }
    Component {
        id: resultModel
        ListModel { function getColumnWidths(charWidth, padding) { return [160, 200] } }
    }
    QtObject {
        id: testBackend
        property var settings: ({})
        property var models: ({})
        property int nextModel: 4
        property bool failSave: false
        property int protocolType: 7
        signal highlightRange(int start, int end, int modelId)
        function handleLink(link, id) {
            const parts = link.slice(10).split(",")
            highlightRange(Number(parts[0]) * 3, (Number(parts[0]) + Number(parts[1])) * 3 - 1, id)
        }
        function getUiSetting(key, fallback) { return settings[key] === undefined ? fallback : settings[key] }
        function setUiSetting(key, value) { if (failSave) return false; settings[key] = value; return true }
        function createParseSession() { const id = nextModel++; models[id] = resultModel.createObject(scene); return id }
        function closeParseSession(id) { if (models[id]) { models[id].destroy(); delete models[id] } }
        function parseModel(id) { return models[id] || null }
        function testMonitorExpression(pattern, ignoreCase, sample) {
            try {
                if (!pattern || pattern.length > 4096) return {valid:false,error:"请输入有效表达式",matches:[]}
                const regex = new RegExp(pattern, ignoreCase ? "gi" : "g")
                const matches = []; let base = 0
                for (const line of sample.split("\n")) {
                    regex.lastIndex = 0; let match
                    while ((match = regex.exec(line)) !== null && matches.length < 128) {
                        matches.push({start:base+match.index,end:base+match.index+match[0].length})
                        if (!match[0].length) regex.lastIndex++
                    }
                    base += line.length + 1
                }
                return {valid:true,error:"",matches:matches}
            } catch (error) { return {valid:false,error:String(error),matches:[]} }
        }
        function parseHex(text, encoding, id, protocol) {
            models[id].clear();
            models[id].append({name: "frame", value: text, description: "protocol " + protocol, isFrameSeparator: false,
                descriptionSummary: "protocol " + protocol, descriptionDetail: "", treeGuides: [], hasParent: false, branchContinues: false,
                highlighted: false, isDiff: false, indent: 0, hasChildren: false, expanded: false, offset: 0, length: 1});
        }
    }
    TestCase {
        name: "SshMonitorWorkspace"
        when: windowShown
        property var page: null
        function init() {
            failOnWarning(/Binding loop|TypeError|ReferenceError|Unable to assign/)
            testBackend.settings = {}; testBackend.models = {}; testBackend.nextModel = 4; testBackend.failSave = false
        }
        function cleanup() { if (page) { page.closing(); page.destroy(); page = null }; wait(40) }
        function make() {
            page = workspace.createObject(scene, {width: 1180, height: 800, visible: true})
            verify(page !== null); wait(40)
        }
        function item(parent, name) {
            let value = findChild(parent, name)
            if (!value && parent && parent.group && ["ssh-stop-reception", "ssh-clear", "ssh-monitor-enabled"].indexOf(name) >= 0) {
                if (parent.group.active !== parent || !parent.group.menu || !parent.group.menu.visible) openSessionMenu(parent)
                value = visualItem(parent.group.menu.contentItem, name)
            }
            if (!value && parent === page) for (const container of page.containers) {
                if (container.window) value = findChild(container.window, name) || visualItem(container.window.contentItem, name)
                if (value) break
            }
            verify(value !== null, name); return value
        }
        function visualItem(node, name) {
            if (node.objectName === name) return node
            if (node.children) for (const child of node.children) { const found = visualItem(child, name); if (found) return found }
            return null
        }
        function select(index) {
            page.activateSession(page.sessions[index]); wait(20)
            const tab = item(page, "ssh-session-row-" + page.sessions[index].sessionId)
            mouseClick(tab, 25, 13); wait(30); compare(page.currentIndex, index)
        }
        function openSessionMenu(session, submenu) {
            page.openSessionMenu(session); wait(30)
            const menu = session.group.menu
            verify(menu !== null && menu.visible)
            if (submenu) {
                const child = findChild(menu, submenu)
                verify(child !== null, submenu); child.open(); wait(20)
            }
        }
        function connectionAction(session) {
            openSessionMenu(session)
            const action = visualItem(session.group.menu.contentItem, "ssh-session-connect-" + session.sessionId)
            verify(action !== null); mouseClick(action); wait(20)
        }
        function toggleSessionOption(session, name) {
            openSessionMenu(session); wait(20)
            const option = visualItem(scene.Window.window.contentItem, name)
            verify(option !== null && option.visible, name)
            mouseClick(option); wait(20)
            session.group.menu.close(); wait(20)
        }
        function connected(session) { session.terminal.connectionState = 2; session.terminal.connected = true }
        function clipboard(value) {
            const editor = clipboardEditor.createObject(scene)
            if (value !== undefined) { editor.text = value; editor.selectAll(); editor.copy() }
            else editor.paste()
            const text = editor.text; editor.destroy()
            return text
        }
        function test_doubleClickTabAndSavedConfigurationAlwaysCreateIndependentConnections() {
            make()
            const a = page.activeSession, config = page.blankConfiguration("采集网关")
            config.host = "synthetic.example"; config.password = "synthetic-password"
            a.restoreConfiguration(config); connected(a); a.connectionConfigExpanded = false
            a.terminal.appendTestLines(["original log"], 20)
            a.selectLogFile("file:///tmp/original-session.log")
            const tab = item(page, "ssh-session-row-" + a.sessionId)
            mouseDoubleClickSequence(tab, 25, 13); wait(40)
            compare(page.sessions.length, 2)
            const b = page.activeSession
            verify(b !== a && b.terminal !== a.terminal)
            compare(b.terminal.connectCalls, 1); compare(b.terminal.host, "synthetic.example")
            compare(b.sessionName, "采集网关 · 1")
            compare(b.terminal.currentLineCount, 0); verify(!b.terminal.logging)
            verify(a.terminal.connected && a.terminal.logging); compare(a.terminal.currentLineCount, 1)
            compare(a.terminal.sentCommands.length, 0); compare(b.terminal.sentCommands.length, 0)
            page.savedConnections = [config]; wait(30)
            const saved = item(page, "ssh-saved-row-0")
            mouseDoubleClickSequence(saved, 35, 18); wait(30)
            const c = page.activeSession
            compare(page.sessions.length, 3); compare(c.terminal.connectCalls, 1)
            compare(c.sessionName, "采集网关 · 2")
            mouseDoubleClickSequence(saved, 35, 18); wait(30)
            compare(page.sessions.length, 4); compare(page.activeSession.sessionName, "采集网关 · 3")
            connected(page.activeSession); wait(20)
            compare(page.savedConnections.length, 1); compare(page.savedConnections[0].name, "采集网关")
            compare(item(page, "ssh-saved-row-0"), saved)
            item(page, "ssh-connection-search").text = "missing"; wait(20); verify(!item(page, "ssh-saved-row-0").visible)
            item(page, "ssh-connection-search").text = "synthetic"; wait(20); verify(item(page, "ssh-saved-row-0").visible)
        }
        function test_splitReorderMergeAndCrossWindowMovePreserveSessionState() {
            make()
            const a = page.activeSession, b = page.addSession(), c = page.addSession()
            connected(a); connected(b); connected(c)
            for (const session of [a,b,c]) session.connectionConfigExpanded = false
            a.terminal.appendTestLines(["A"], 20); b.terminal.appendTestLines(["B"], 20)
            a.autoScroll = false; a.terminal.receptionPaused = true
            a.selectLogFile("file:///tmp/group-move.log")
            const terminal = a.terminal, disconnects = terminal.disconnectCalls
            const first = a.group, container = first.container
            verify(page.moveSession(c, first, 0)); compare(first.tabs[0], c)
            verify(page.splitSession(b)); wait(30)
            compare(container.groups.length, 2); verify(c.visible && b.visible)
            verify(a.group !== b.group && a.terminal === terminal)
            verify(page.moveSession(a, b.group)); wait(30)
            verify(a.visible && c.visible); verify(!b.visible)
            compare(a.terminal.lineAt(0), "A"); verify(a.terminal.logging && a.terminal.receptionPaused && !a.autoScroll)
            compare(a.terminal.disconnectCalls, disconnects)
            verify(page.detachSession(a)); wait(30)
            const window = a.monitorWindow
            verify(page.moveSession(b, a.group)); wait(30)
            compare(a.group.tabs.length, 2); verify(window.visible)
            verify(page.splitSession(b)); wait(30)
            compare(a.group.container.groups.length, 2); verify(a.visible && b.visible)
            verify(page.moveSession(c, b.group, 0)); wait(30)
            verify(c.Window.window === window); compare(c.group.tabs[0], c)
            window.close(); wait(50)
            verify(!window.visible && !a.detached && !b.detached && !c.detached)
            compare(a.terminal, terminal); compare(a.terminal.disconnectCalls, disconnects)
            page.mergeGroups(container); wait(30); compare(container.groups.length, 1)
            compare(container.groups[0].tabs.length, 3)
        }
        function test_layoutRestoresTabOrderSplitRatioAndWindowsWithoutConnecting() {
            make()
            const a = page.activeSession, b = page.addSession(), c = page.addSession(), d = page.addSession()
            const ids = [a.sessionId,b.sessionId,c.sessionId,d.sessionId]
            page.splitSession(b); page.moveSession(d, b.group, 0)
            const container = a.group.container; container.splitRatio = 0.62
            page.detachSession(c); wait(30)
            const floating = c.group.container
            const e = page.addSession(page.blankConfiguration("第五个"), undefined, c.group)
            page.splitSession(e); floating.splitRatio = 0.44
            page.activateSession(d); wait(20)
            verify(page.saveWorkspace())
            const before = JSON.parse(testBackend.settings["next/sshMonitor/workspace"])
            compare(before.version, 2); compare(before.layout.length, 2)
            page.destroy(); page = null; wait(40); make()
            compare(page.sessions.length, 5)
            const restoredA = page.sessionById(ids[0]), restoredB = page.sessionById(ids[1]), restoredC = page.sessionById(ids[2]), restoredD = page.sessionById(ids[3])
            compare(restoredA.group.container.groups.length, 2)
            compare(restoredA.group.container.splitRatio, 0.62)
            compare(restoredB.group.tabs[0], restoredD); compare(restoredB.group.active, restoredD)
            verify(restoredC.detached && restoredC.monitorWindow.visible)
            compare(restoredC.group.container.groups.length, 2); compare(restoredC.group.container.splitRatio, 0.44)
            for (const session of page.sessions) { compare(session.terminal.connectCalls, 0); verify(!session.terminal.logging); compare(session.terminal.sentCommands.length, 0) }
        }
        function test_versionOneMigrationAndInvalidTopologyProtectExistingSettings() {
            const config = {version:1,sessions:[{id:7,config:{name:"旧会话",host:"old.example",port:"22",username:"root",password:"test",encoding:"UTF-8",maxLines:"10000",autoReconnect:true,reconnectCount:"5",reconnectInterval:"2000",expanded:false,commandManagementExpanded:false}}],connections:[],currentIndex:0}
            testBackend.settings["next/sshMonitor/workspace"] = JSON.stringify(config)
            make(); compare(page.sessions.length, 1); compare(page.activeSession.sessionId, 7)
            compare(page.activeSession.terminal.connectCalls, 0); verify(page.saveWorkspace())
            const updated = JSON.parse(testBackend.settings["next/sshMonitor/workspace"])
            compare(updated.version, 2); compare(updated.layout[0].groups[0].tabs[0], 7)
            page.destroy(); page = null; wait(30)
            updated.layout[0].groups[0].tabs.push(7)
            const invalid = JSON.stringify(updated)
            testBackend.settings["next/sshMonitor/workspace"] = invalid
            make(); compare(testBackend.settings["next/sshMonitor/recovery"], invalid)
            compare(page.sessions.length, 1); verify(page.workspaceError !== "")
        }
        function test_renameSaveFailureAndSessionLimitKeepLiveSessions() {
            make()
            const a = page.activeSession
            verify(page.renameSession(a, "业务日志")); compare(a.sessionName, "业务日志")
            verify(!page.renameSession(a, "  ")); compare(a.sessionName, "业务日志")
            testBackend.failSave = true
            verify(!page.renameSession(a, "不能保存")); compare(a.sessionName, "业务日志")
            testBackend.failSave = false
            for (let i = 1; i < 64; ++i) verify(page.addSession() !== null)
            compare(page.sessions.length, 64); compare(page.cloneSession(a), null)
            verify(page.workspaceError.indexOf("64") >= 0)
        }
        function dragTab(session, destination, x, y, cancel) {
            const target = destination.mapToGlobal(x, y)
            page.activateSession(session); wait(30)
            const tab = item(page, "ssh-session-row-" + session.sessionId)
            const point = tab.mapFromGlobal(target.x, target.y)
            tab.Window.window.requestActivate(); wait(100)
            mouseMove(tab, 25, 16); wait(20)
            mousePress(tab, 25, 13)
            mouseMove(tab, 45, 13, 30)
            mouseMove(tab, point.x, point.y, 40)
            verify(page.draggedSession === session)
            const ghost=item(tab,"ssh-tab-drag-preview-"+session.sessionId)
            verify(ghost.x>=0 && ghost.x+ghost.width<=ghost.parent.width+1)
            if (cancel) keyClick(Qt.Key_Escape)
            mouseRelease(tab, point.x, point.y); wait(50)
        }
        function test_realPointerDragReordersSplitsMovesAndEscapeCancels() {
            make()
            const a = page.activeSession, b = page.addSession(), c = page.addSession()
            for (const session of [a,b,c]) session.connectionConfigExpanded = false
            connected(a); a.terminal.appendTestLines(["keep while dragging"], 20)
            const originalGroup = a.group
            const tabC = item(page, "ssh-session-row-" + c.sessionId)
            dragTab(a, tabC, tabC.width - 3, 16, false)
            compare(originalGroup.tabs[2], a)
            dragTab(a, a.group.contentHost, a.group.contentHost.width - 8, 100, false)
            compare(a.group.container.groups.length, 2); verify(a.group !== b.group)
            const other = b.group
            dragTab(a, other.contentHost, 20, 100, true)
            verify(a.group !== other); verify(page.draggedSession === null)
            dragTab(a, other.contentHost, 20, 100, false)
            compare(a.group, other); compare(other.container.groups.length, 1)
            verify(a.terminal.connected); compare(a.terminal.lineAt(0), "keep while dragging")
            compare(a.terminal.disconnectCalls, 0)
        }
        function test_pointerDragAcrossWindowsAndOutsideKeepsTheConnection() {
            make()
            const a = page.activeSession, b = page.addSession()
            a.connectionConfigExpanded = false; b.connectionConfigExpanded = false
            connected(a); a.terminal.appendTestLines(["cross-window buffer"], 20)
            verify(page.detachSession(b)); wait(30)
            const window = b.monitorWindow
            window.x = 1400; window.y = 40; wait(30)
            dragTab(a, b.group.contentHost, b.group.contentHost.width / 2, 100, false)
            compare(a.group, b.group); compare(a.Window.window, window)
            verify(a.terminal.connected); compare(a.terminal.lineAt(0), "cross-window buffer")
            const tab = item(page, "ssh-session-row-" + a.sessionId)
            const point = tab.mapFromGlobal(-100, -100)
            mousePress(tab, 25, 13); mouseMove(tab, 45, 13, 30)
            mouseMove(tab, point.x, point.y, 40); verify(page.draggedSession === a)
            mouseRelease(tab, point.x, point.y); wait(60)
            verify(a.detached && a.monitorWindow !== window)
            verify(a.monitorWindow.visible && window.visible)
            compare(a.terminal.disconnectCalls, 0)
            a.monitorWindow.close(); window.close(); wait(40)
            verify(!a.detached && !b.detached)
        }
        function test_menuCanMoveToAnExistingWindowWithoutDragging() {
            make()
            const a = page.activeSession, b = page.addSession()
            connected(a); a.connectionConfigExpanded = false
            verify(page.detachSession(b)); wait(30)
            openSessionMenu(a, "ssh-layout-menu")
            const menu = item(page, "ssh-session-options-" + a.sessionId)
            compare(menu.otherWindows.length, 1)
            const target = item(menu, "ssh-window-target-" + b.group.container.windowId)
            target.triggered(); wait(30)
            compare(a.group, b.group); verify(a.detached && a.terminal.connected)
            compare(a.terminal.disconnectCalls, 0)
        }
        function test_thinTabsHaveNoControlRowsAndRightClickOwnsAllActions() {
            make()
            const a=page.activeSession
            a.connectionConfigExpanded=false
            wait(30)
            const tab=item(page,"ssh-session-row-"+a.sessionId)
            compare(tab.height,26)
            verify(findChild(page,"ssh-layout-single-0")===null && findChild(page,"ssh-layout-split-0")===null)
            verify(findChild(page,"ssh-session-actions-"+a.sessionId)===null)
            verify(!item(a,"ssh-monitor-rule-bar").visible)
            verify(a.y<=1 && a.group.contentHost.y<=28)
            mouseClick(tab,25,13,Qt.RightButton); wait(30)
            verify(a.group.menu.visible)
            for (const name of ["ssh-stop-reception","ssh-clear","ssh-monitor-enabled","ssh-menu-match-details"])
                verify(visualItem(a.group.menu.contentItem,name)!==null,name)
            verify(visualItem(a.group.menu.contentItem,"ssh-pause")===null)
            const stop=visualItem(a.group.menu.contentItem,"ssh-stop-reception")
            verify(!stop.checked)
            stop.forceActiveFocus(); keyClick(Qt.Key_Return); wait(20)
            verify(stop.checked && a.group.menu.visible)
            verify(item(a,"ssh-monitor-footer").text.indexOf("接收已停止")>=0)
            mouseClick(stop); wait(20); verify(!stop.checked && a.group.menu.visible)
            verify(a.autoScroll && !a.terminal.receptionPaused)
        }
        function test_leftAndRightEdgesPreviewBeforeCommitAndCenterMerges() {
            make()
            const a=page.activeSession, b=page.addSession()
            a.connectionConfigExpanded=false; b.connectionConfigExpanded=false
            a.autoScroll=false; a.terminal.appendTestLines(["keep anchor"],100)
            connected(a)
            const terminal=a.terminal
            verify(waitForPolish(page)); wait(30)
            const host=a.group.contentHost, container=a.group.container
            const center=host.mapToGlobal(host.width/2,100)
            page.draggedSession=a
            compare(page.tabDropTarget(center).split,false)
            for (const side of ["left","right"]) {
                const point=host.mapToGlobal(side==="left"?8:host.width-8,100)
                page.dragTarget=page.tabDropTarget(point)
                verify(page.dragTarget.split); compare(page.dragTarget.side,side)
                wait(20)
                const preview=item(page,"ssh-drop-preview-"+a.group.groupId)
                verify(preview.visible); compare(preview.width,host.width/2)
                compare(preview.x,side==="left"?0:host.width/2)
                compare(container.groups.length,1)
            }
            page.finishTabDrag(a,host.mapToGlobal(8,100)); wait(40)
            compare(container.groups.length,2); compare(container.groups[0].active,a)
            compare(a.terminal,terminal); compare(a.terminal.lineAt(0),"keep anchor"); verify(!a.autoScroll)
            page.mergeGroups(container); wait(40)
            const both=a.group
            page.draggedSession=a
            page.finishTabDrag(a,both.contentHost.mapToGlobal(both.contentHost.width-8,100)); wait(40)
            compare(container.groups.length,2); compare(container.groups[1].active,a)
            page.draggedSession=a
            page.finishTabDrag(a,b.group.contentHost.mapToGlobal(b.group.contentHost.width/2,100)); wait(40)
            compare(container.groups.length,1); compare(a.group,b.group)
            compare(terminal.disconnectCalls,0)
        }
        function connectionFixture(name) {
            const config=page.blankConfiguration(name)
            config.host="synthetic.example"; config.password="synthetic-password"; config.expanded=false
            return config
        }
        function test_returnConnectsOnlyTheCurrentTerminalAndNeverCancelsRetry() {
            make()
            const a=page.activeSession
            a.restoreConfiguration(connectionFixture("A"))
            const b=page.addSession(connectionFixture("B")); wait(30)
            page.activateSession(a); wait(20)
            mouseClick(item(page,"ssh-session-row-"+a.sessionId),25,13); wait(20)
            keyClick(Qt.Key_Return); wait(20)
            compare(a.terminal.connectCalls,1); compare(b.terminal.connectCalls,0)
            compare(a.terminal.rawInput.length,0)
            keyClick(Qt.Key_Return); keyClick(Qt.Key_Enter); wait(20)
            compare(a.terminal.connectCalls,1); compare(a.terminal.disconnectCalls,0)
            a.terminal.connectionState=4
            keyClick(Qt.Key_Return); wait(20)
            compare(a.terminal.connectCalls,2); compare(a.terminal.disconnectCalls,0)
            page.activateSession(b); wait(20)
            item(page,"ssh-session-row-"+b.sessionId).forceActiveFocus()
            keyClick(Qt.Key_Enter); wait(20)
            compare(b.terminal.connectCalls,1); compare(a.terminal.connectCalls,2)
            verify(page.detachSession(b)); wait(30)
            b.terminal.connectionState=4
            mouseClick(item(b,"ssh-terminal-pane"),40,80); wait(20)
            keyClick(Qt.Key_Return); wait(20)
            compare(b.terminal.connectCalls,2); compare(a.terminal.connectCalls,2)
        }
        function test_returnDoesNotStealMenuOrConfigurationEditing() {
            make()
            const a=page.activeSession
            a.restoreConfiguration(connectionFixture("A"))
            a.connectionConfigExpanded=true; wait(20)
            item(a,"ssh-host").forceActiveFocus(); keyClick(Qt.Key_Return); wait(20)
            compare(a.terminal.connectCalls,0)
            a.addCustomCommand("test","echo test"); editCommand(a,0)
            item(a,"ssh-command-content").forceActiveFocus(); keyClick(Qt.Key_Return); wait(20)
            compare(a.terminal.connectCalls,0); compare(a.terminal.sentCommands.length,0)
            a.commandManagementExpanded=false
            openSessionMenu(a)
            const menu=a.group.menu, stop=visualItem(menu.contentItem,"ssh-stop-reception")
            stop.forceActiveFocus(); keyClick(Qt.Key_Return); wait(20)
            verify(a.terminal.receptionPaused && menu.visible); compare(a.terminal.connectCalls,0)
            menu.close(); wait(20)
            a.focusTerminal(); keyClick(Qt.Key_Return,Qt.ControlModifier); wait(20)
            compare(a.terminal.connectCalls,0)
            const repeat={key:Qt.Key_Return,modifiers:Qt.NoModifier,isAutoRepeat:true,accepted:false}
            verify(a.routeTerminalKey(repeat)); verify(repeat.accepted); compare(a.terminal.connectCalls,0)
        }
        function test_connectedReturnRemainsTerminalInputAndBusyStatesNeverReconnect() {
            make()
            const a=page.activeSession
            a.restoreConfiguration(connectionFixture("A")); connected(a); wait(20)
            a.focusTerminal(); keyClick(Qt.Key_Return); wait(20)
            compare(a.terminal.rawInput.join(""),"\r"); compare(a.terminal.connectCalls,0)
            verify(!a.connectIfIdle()); compare(a.terminal.disconnectCalls,0)
            a.terminal.receptionPaused=true; keyClick(Qt.Key_Return); wait(20)
            compare(a.terminal.rawInput.join(""),"\r"); compare(a.terminal.connectCalls,0)
            a.terminal.connected=false; a.terminal.receptionPaused=false
            for (const state of [1,3]) { a.terminal.connectionState=state; verify(!a.connectIfIdle()) }
            a.terminal.connectionState=0; a.terminal.hostKeyPending=true; verify(!a.connectIfIdle())
            a.terminal.hostKeyPending=false; a.pendingSshAction={kind:"connect"}; verify(!a.connectIfIdle())
            a.pendingSshAction=null; a.passwordOperationId="test-pending"; verify(!a.connectIfIdle())
            compare(a.terminal.connectCalls,0); compare(a.terminal.disconnectCalls,0)
        }
        function test_manualReviewPausesAndResumesFollowing_data() {
            return [{tag:"wheel", scrollbar:false}, {tag:"scrollbar", scrollbar:true}]
        }
        function test_manualReviewPausesAndResumesFollowing(data) {
            make()
            const a = page.activeSession, b = page.addSession()
            page.activateSession(a); connected(a); a.connectionConfigExpanded = false
            const view = item(a, "ssh-log-list")
            const lines = []; for (let i = 0; i < 160; ++i) lines.push("history " + i)
            a.terminal.appendTestLines(lines, 1000)
            tryCompare(view, "atYEnd", true)
            verify(a.autoScroll)
            openSessionMenu(a)
            verify(visualItem(a.group.menu.contentItem, "ssh-pause") === null)
            a.group.menu.close()
            if (data.scrollbar) {
                const bar = item(a, "ssh-log-vertical-scroll")
                mousePress(bar, bar.width / 2, bar.height * (bar.position + bar.size / 2))
                mouseMove(bar, bar.width / 2, bar.height / 3, 50, Qt.LeftButton)
                mouseRelease(bar, bar.width / 2, bar.height / 3)
            } else mouseWheel(view, 40, 100, 0, 240)
            tryCompare(a, "autoScroll", false); tryCompare(view, "moving", false)
            verify(!view.atYEnd); verify(b.autoScroll); verify(!a.terminal.receptionPaused)
            const y = view.contentY, count = a.terminal.currentLineCount
            a.terminal.appendTestLines(["new while reviewing"], 1000); wait(40)
            compare(a.terminal.currentLineCount, count + 1)
            verify(Math.abs(view.contentY - y) < 1); verify(!view.atYEnd)

            view.positionViewAtEnd()
            tryCompare(a, "autoScroll", true); tryCompare(view, "atYEnd", true)
            a.terminal.appendTestLines(["new after reaching bottom"], 1000)
            tryCompare(view, "atYEnd", true)

            view.positionViewAtIndex(80, ListView.Beginning); view.contentY += 7; wait(40)
            tryCompare(a, "autoScroll", false)
            a.focusTerminal(); keyClick(Qt.Key_Return)
            tryCompare(a, "autoScroll", true); tryCompare(view, "atYEnd", true)
            compare(a.terminal.rawInput.join(""), "\r")
            a.terminal.appendTestLines(["new while following"], 1000)
            tryCompare(view, "atYEnd", true)
        }
        function test_pausedViewportDoesNotJumpWithinIncomingBatches_data() {
            return [{tag:"append", limit:400, wrap:false}, {tag:"prune", limit:160, wrap:false},
                {tag:"wrapped-append", limit:400, wrap:true}, {tag:"wrapped-prune", limit:160, wrap:true}]
        }
        function test_pausedViewportDoesNotJumpWithinIncomingBatches(data) {
            make()
            const a = page.activeSession
            a.connectionConfigExpanded = false; a.wrapLogLines = data.wrap
            const lines = []; for (let i = 0; i < 160; ++i) lines.push("history " + i + " " + "x".repeat(i % 5 * 50))
            a.terminal.appendTestLines(lines, data.limit); wait(40)
            a.autoScroll = false
            const view = item(a, "ssh-log-list")
            view.positionViewAtIndex(80, ListView.Beginning); view.contentY += 7; wait(40)
            const anchor = a.terminal.lineIdAt(view.indexAt(1, view.contentY + 1))
            const row = view.itemAtIndex(a.terminal.indexForLineId(anchor))
            const offset = row.y - view.contentY
            const offsets = []
            const track = function() { offsets.push(row.y - view.contentY) }
            view.contentYChanged.connect(track)
            try {
                for (let batch = 0; batch < 8; ++batch) {
                    a.terminal.appendTestLines(["incoming " + batch, "another " + batch], data.limit)
                    wait(30)
                    compare(a.terminal.lineIdAt(view.indexAt(1, view.contentY + 1)), anchor)
                    verify(Math.abs(row.y - view.contentY - offset) < 1)
                }
                verify(offsets.every(value => Math.abs(value - offset) < 1),
                    "paused viewport moved during restoration: " + JSON.stringify(offsets))
                verify(!a.autoScroll && !a.terminal.receptionPaused)
            } finally { view.contentYChanged.disconnect(track) }
            const overflow = []; for (let i = 0; i < data.limit + 10; ++i) overflow.push("overflow " + i)
            a.terminal.appendTestLines(overflow, data.limit)
            tryCompare(view, "atYBeginning", true)
            compare(a.terminal.lineIdAt(view.indexAt(1, view.contentY + 1)), a.terminal.firstLineId)
            verify(item(a, "ssh-footer-hint").text.indexOf("超出缓存范围") >= 0)
        }
        function test_pausedLongLinesKeepViewportAndWidthStable() {
            make()
            const a = page.activeSession
            a.connectionConfigExpanded = false
            const lines = []; for (let i = 0; i < 200; ++i) lines.push("history " + i)
            a.terminal.appendTestLines(lines, 1000); wait(40)
            a.autoScroll = false
            const view = item(a, "ssh-log-list")
            view.positionViewAtEnd(); view.contentY -= 7; wait(40)
            const anchor = a.terminal.lineIdAt(view.indexAt(1, view.contentY + 1))
            const row = view.itemAtIndex(a.terminal.indexForLineId(anchor))
            const offset = row.y - view.contentY
            const horizontalBar = item(a, "ssh-log-horizontal-scroll")
            const wide = "wide " + "x".repeat(1200)
            let contentWidth = 0
            for (let batch = 0; batch < 8; ++batch) {
                a.terminal.appendTestLines([batch % 2 ? "short " + batch : wide], 1000)
                a.terminal.liveLine = batch % 2 ? "" : wide
                wait(30)
                compare(a.terminal.lineIdAt(view.indexAt(1, view.contentY + 1)), anchor)
                verify(Math.abs(row.y - view.contentY - offset) < 1)
                if (batch === 0) contentWidth = view.contentWidth
                else compare(view.contentWidth, contentWidth)
                verify(horizontalBar.visible)
            }
            verify(!a.autoScroll)
        }
        function test_primaryMenuOptionsAndSharedFlatTerminalFrame() {
            make()
            const a=page.activeSession
            a.connectionConfigExpanded=false; wait(20)
            openSessionMenu(a)
            const menu=a.group.menu
            verify(findChild(menu,"ssh-display-menu")===null && findChild(menu,"ssh-connection-menu")===null)
            const config=visualItem(menu.contentItem,"ssh-session-config-"+a.sessionId)
            const connect=visualItem(menu.contentItem,"ssh-session-connect-"+a.sessionId)
            verify(config!==null && connect!==null); compare(config.text,"连接参数…")
            verify(visualItem(menu.contentItem, "ssh-pause") === null)
            for (const name of ["ssh-stop-reception","ssh-copy-on-select","ssh-wrap-lines"]) {
                const option=visualItem(menu.contentItem,name)
                verify(option!==null && option.visible); compare(option.menu,menu)
            }
            mouseClick(visualItem(menu.contentItem,"ssh-copy-on-select")); wait(20)
            mouseClick(visualItem(menu.contentItem,"ssh-wrap-lines")); wait(20)
            verify(a.copyOnSelect && a.wrapLogLines && menu.opened)
            mouseClick(config); wait(30); verify(item(page,"ssh-connection-editor").opened)
            item(page,"ssh-connection-editor").close(); wait(20)
            verify(!item(a,"ssh-connection-panel").visible)
            const frame=item(a,"ssh-terminal-pane"), group=a.group.visualHost
            compare(frame.radius,0); compare(frame.border.width,0); compare(group.radius,0)
            verify(!a.background.visible)
            compare(frame.mapToItem(group,0,0).x,1)
            compare(frame.width,group.width-2)
        }
        function test_thinDividerStillResizesAndPersistsBothSessions() {
            make()
            const a=page.activeSession,b=page.addSession()
            a.connectionConfigExpanded=false; b.connectionConfigExpanded=false
            a.terminal.appendTestLines(["left buffer"],100); b.terminal.appendTestLines(["right buffer"],100)
            page.splitSession(b); wait(40)
            const handle=item(page,"ssh-split-handle-0"), line=item(page,"ssh-split-line-0")
            compare(line.width,1); verify(handle.width>=6)
            const before=a.width, container=a.group.container
            mouseDrag(handle,handle.width/2,handle.height/2,60,0,Qt.LeftButton); wait(40)
            verify(a.width>before+30); verify(container.splitRatio>0.5)
            compare(a.terminal.lineAt(0),"left buffer"); compare(b.terminal.lineAt(0),"right buffer")
            const ratio=container.splitRatio
            verify(page.saveWorkspace())
            page.destroy(); page=null; wait(30); make()
            compare(page.sessions[0].group.container.splitRatio,ratio)
        }
        function test_stopScreenStopsReceptionAndKeepsIndependentWindow() {
            make()
            const a = page.activeSession; connected(a)
            a.connectionConfigExpanded = false; a.autoScroll = false
            const rows = []
            for (let i = 0; i < 200; ++i) rows.push("old " + i + " RX 680407000000")
            a.terminal.appendTestLines(rows, 300); wait(30)
            const view = item(a, "ssh-log-list")
            view.positionViewAtIndex(80, ListView.Beginning); wait(20)
            const stop = item(a, "ssh-stop-reception"), clear = item(a, "ssh-clear")
            verify(clear.y > stop.y)
            mouseClick(stop); wait(30)
            verify(a.terminal.receptionPaused); verify(stop.checked); verify(!a.autoScroll)
            compare(a.terminal.lineIdAt(view.indexAt(1, view.contentY + 1)), 80)
            const received = []
            for (let i = 0; i < 150; ++i) received.push("background " + i)
            a.terminal.appendTestLines(received, 100); wait(20)
            compare(a.terminal.currentLineCount, 200); compare(a.terminal.lineAt(80), rows[80])
            verify(a.terminal.getBufferText().indexOf("background 149") < 0)
            a.textSelectionAnchor = {id:80,position:0}; a.textSelectionFocus = {id:80,position:6}
            a.copyTextSelection(); compare(clipboard(), "old 80")
            a.selectLineAndExtractHex(80); a.parseSelectedHexWithProtocol(0); wait(20)
            compare(a.parsedHex, "68 04 07 00 00 00")
            const b = page.addSession(); b.terminal.appendTestLines(["second window"], 100); wait(20)
            verify(!b.terminal.receptionPaused); compare(b.terminal.currentLineCount, 1)
            verify(page.detachSession(a)); wait(30); verify(a.terminal.receptionPaused)
            mouseClick(item(a, "ssh-stop-reception")); wait(30)
            verify(!a.terminal.receptionPaused && !a.autoScroll)
            compare(a.terminal.currentLineCount, 350); compare(a.terminal.lineAt(349), "background 149")
            verify(!a.autoScroll)
            verify(page.dockSession(a)); wait(20)
        }
        function test_menuActionsFollowTheirOwnSessionAcrossGroupsAndWindows() {
            make()
            const a = page.activeSession; connected(a); a.connectionConfigExpanded = false
            a.terminal.appendTestLines(["session A output"], 100)
            const b = page.addSession(); connected(b); b.connectionConfigExpanded = false
            b.terminal.appendTestLines(["session B output"], 100)
            verify(page.splitSession(b)); wait(40)
            for (const session of [a,b]) {
                verify(!item(session, "ssh-monitor-rule-bar").visible)
                verify(findChild(session, "ssh-pause") === null)
                compare(item(page, "ssh-tab-bar-" + session.group.groupId).height, 26)
            }
            verify(findChild(page, "ssh-layout-single-0") === null)
            a.autoScroll = false; verify(!a.autoScroll && b.autoScroll)
            mouseClick(item(a,"ssh-stop-reception")); verify(a.terminal.receptionPaused && !b.terminal.receptionPaused)
            mouseClick(item(a,"ssh-clear")); compare(a.terminal.currentLineCount,0); compare(b.terminal.currentLineCount,1)
            verify(page.detachSession(a)); wait(30)
            mouseClick(item(a,"ssh-stop-reception")); verify(!a.terminal.receptionPaused && !a.autoScroll)
            verify(page.dockSession(a)); wait(30)
            verify(item(a,"ssh-monitor-footer").text.indexOf("暂停跟随") >= 0)
        }
        function test_scrollingTabsKeepsTheirContextMenuAvailable() {
            make()
            const a = page.activeSession
            for (let i=0;i<11;++i) page.addSession()
            page.activateSession(a); wait(40)
            const list=item(page,"ssh-tab-bar-"+a.group.groupId)
            list.cacheBuffer=0; list.positionViewAtEnd(); wait(40)
            openSessionMenu(a)
            verify(item(a,"ssh-stop-reception").visible)
            verify(findChild(a,"ssh-stop-reception")===null)
        }
        function test_footerHintAndConnectionShareTheBottomRow() {
            make()
            const a = page.activeSession
            a.connectionConfigExpanded = false
            const footer = item(a, "ssh-footer")
            const hint = item(a, "ssh-footer-hint")
            verify(findChild(a, "ssh-footer-command-hint") === null)
            const connection = item(a, "ssh-footer-connection")
            const badge = item(a, "ssh-connection-badge")
            function checkFooter() {
                verify(waitForPolish(a)); wait(20)
                compare(hint.parent, footer); compare(connection.parent, footer)
                const left = hint.mapToItem(footer, 0, 0)
                const right = connection.mapToItem(footer, 0, 0)
                verify(Math.abs(left.y + hint.height / 2 - right.y - connection.height / 2) <= 1)
                verify(Math.abs(right.x + connection.width - footer.width) <= 1)
                verify(left.x + hint.width <= right.x)
                const create = item(a, "ssh-command-create")
                verify(create.mapToItem(a, 0, 0).y + create.height <= footer.mapToItem(a, 0, 0).y)
                verify(badge.visible && hint.width >= 0)
            }
            for (const width of [760, 1180, 1560]) {
                page.width = width
                for (const state of [0, 1, 2, 3, 4]) {
                    a.terminal.connectionState = state; a.terminal.connected = state === 2
                    a.terminal.statusMessage = ""
                    checkFooter()
                }
            }
            verify(item(a, "ssh-command-create").visible)
            a.addCustomCommand("查看目录", "pwd"); wait(20)
            verify(item(a, "ssh-command-create").visible); checkFooter()
            a.terminal.statusMessage = "连接失败，等待恢复。".repeat(20)
            checkFooter()
            verify(page.detachSession(a)); wait(30)
            a.monitorWindow.width = 760; checkFooter()
            verify(page.dockSession(a)); wait(30); checkFooter()
        }
        function test_receptionPauseTooltipDoesNotToggleAutomaticFollowing() {
            make()
            const a = page.activeSession
            const stop = item(a, "ssh-stop-reception")
            verify(findChild(a, "ssh-pause") === null)
            verify(stop.ToolTip.text.indexOf("停止接收") >= 0)
            verify(waitForPolish(a))
            mouseMove(a, 1, 1)
            mouseMove(stop, stop.width/2, stop.height/2)
            tryCompare(stop, "hovered", true)
            tryVerify(() => stop.ToolTip.toolTip.visible, 5000, "reception pause tooltip must appear after hovering")
            mouseClick(stop); verify(a.terminal.receptionPaused)
            verify(stop.ToolTip.text.indexOf("保留 SSH 连接") >= 0)
            mouseClick(stop); wait(20); verify(a.autoScroll && !a.terminal.receptionPaused)
        }
        function test_monitorRulesAreSharedButSelectionAndColorArePerSession() {
            make()
            const first = page.saveMonitorRule(-1, "错误", "ERROR", false, "#f79a93")
            const second = page.saveMonitorRule(-1, "离线", "设备.*离线", true, "#80b9ff")
            verify(first > 0 && second > first)
            const a = page.activeSession
            const b = page.addSession(); wait(20)
            connected(a); connected(b)
            a.monitorRuleIds = [first]; a.monitorEnabled = true
            b.monitorRuleIds = [second]; b.monitorEnabled = true
            a.terminal.appendTestLines(["normal", "ERROR 1003", "later"], 100); wait(30)
            verify(!a.autoScroll && a.monitorHit !== null)
            compare(a.monitorHit.ruleName, "错误"); compare(a.monitorHit.color, "#f79a93")
            compare(a.monitorHitColor.toString(), "#f79a93")
            verify(b.autoScroll); verify(b.monitorHit === null)
            const id = a.monitorHit.lineId
            a.terminal.appendTestLines(["ERROR again", "still receiving"], 100); wait(20)
            compare(a.monitorHit.lineId, id); compare(a.terminal.currentLineCount, 5)
            a.sendTerminalText("x"); verify(!a.autoScroll)
            verify(page.detachSession(a)); wait(30)
            compare(a.monitorHit.lineId, id); verify(!a.autoScroll)
            a.resumeFollowing(); wait(20)
            verify(a.autoScroll && a.monitorHit === null && a.terminal.monitorArmed)
            a.terminal.appendTestLines(["normal after resume"], 100); wait(20); verify(a.autoScroll)
            a.terminal.appendTestLines(["ERROR new"], 100); wait(20); verify(!a.autoScroll)
            a.focusTerminal(); keyClick(Qt.Key_Return)
            tryCompare(a, "autoScroll", true)
            verify(a.monitorHit === null); verify(a.terminal.rawInput.join("").endsWith("\r"))
            b.terminal.appendTestLines(["设备 12 离线"], 100); wait(20)
            compare(b.monitorHit.color, "#80b9ff"); verify(!b.autoScroll)
            verify(page.dockSession(a)); wait(20)
            a.terminal.liveLine = "ERROR unfinished output"; wait(20)
            a.terminal.monitorMatched({ruleId:first,ruleName:"错误",color:"#f79a93",text:a.terminal.liveLine,lineId:a.liveSelectionId,receivedLineId:a.liveSelectionId,time:"12:00:01"})
            wait(20); verify(!a.autoScroll); verify(item(a, "ssh-monitor-live-hit").visible)
        }
        function test_corruptMonitorRulesAreBackedUpAndNeverArmed() {
            make()
            const config = page.activeSession.connectionConfiguration()
            page.destroy(); page = null; wait(30)
            const raw = JSON.stringify({version:1,sessions:[{id:1,config:config}],currentIndex:0,
                monitorRules:[{id:1,name:"invalid",pattern:"(",ignoreCase:false,color:"#80b9ff"}],nextMonitorRuleId:2})
            testBackend.settings["next/sshMonitor/workspace"] = raw
            make()
            compare(testBackend.settings["next/sshMonitor/recovery"], raw)
            compare(page.monitorRules.length, 0); verify(!page.activeSession.terminal.monitorArmed)
        }
        function test_monitorRuleChangesPersistenceDeletionAndSaveFailures() {
            make()
            const id = page.saveMonitorRule(-1, "错误", "ERROR", false, "#bb9cf5")
            verify(page.monitorRules[0].highlightWholeLine)
            const a = page.activeSession
            a.monitorRuleIds = [id]; a.monitorEnabled = true
            a.terminal.appendTestLines(["ERROR"], 100); wait(20)
            verify(a.monitorHit !== null)
            compare(page.saveMonitorRule(id, "错误更新", "FATAL", true, "#7ed6a0", false, false), id)
            verify(!a.autoScroll); compare(a.selectedMonitorRules[0].pattern, "FATAL")
            testBackend.failSave = true
            compare(page.saveMonitorRule(id, "失败修改", "changed", false, "#80b9ff"), 0)
            compare(page.monitorRules[0].pattern, "FATAL")
            verify(!page.removeMonitorRule(id)); compare(a.monitorRuleIds[0], id)
            testBackend.failSave = false
            verify(page.saveWorkspace())
            page.destroy(); page = null; wait(30); make()
            compare(page.monitorRules[0].color, "#7ed6a0")
            verify(page.monitorRules[0].pauseOnMatch === false)
            verify(page.monitorRules[0].highlightWholeLine === false)
            compare(page.activeSession.monitorRuleIds[0], id); verify(page.activeSession.monitorEnabled)
            verify(page.activeSession.autoScroll && page.activeSession.monitorHit === null)
            verify(page.removeMonitorRule(id)); compare(page.activeSession.monitorRuleIds.length, 0)
            compare(page.monitorRules.length, 0)
            verify(page.saveMonitorRule(-1, "bad", "(", false, "#80b9ff") === 0)
            verify(page.saveMonitorRule(-1, "bad color", "ERROR", false, "red") === 0)
            verify(!page.validMonitorRule({id:1,name:"bad scope",pattern:"ERROR",ignoreCase:false,highlightWholeLine:"false"}))
        }
        function test_monitorRuleEditorAndPicker() {
            make()
            compare(item(page, "ssh-monitor-rules").text, "规则库 (0)")
            mouseClick(item(page, "ssh-monitor-rules")); wait(30)
            const editor = item(page, "ssh-rule-window")
            verify(editor.visible)
            compare(editor.title, "规则库 — 通联工坊")
            item(editor, "ssh-rule-name").text = "设备超时"
            item(editor, "ssh-rule-pattern").text = "设备.*超时"
            verify(item(editor, "ssh-rule-pause-on-match").checked)
            mouseClick(item(editor, "ssh-rule-pause-on-match"))
            const scope = item(editor, "ssh-rule-highlight-scope")
            verify(!scope.checked)
            compare(scope.text, "仅正则匹配部分着色")
            mouseClick(scope)
            verify(scope.checked)
            const swatch = visualItem(editor.contentItem, "ssh-rule-color-80b9ff")
            verify(swatch !== null); mouseClick(swatch)
            tryCompare(item(editor, "ssh-rule-save"), "enabled", true)
            item(editor, "ssh-rule-sample").text = "设备 12 超时"
            mouseClick(item(editor, "ssh-rule-test")); wait(20)
            compare(item(editor, "ssh-rule-sample").selectedText, "设备 12 超时")
            mouseClick(item(editor, "ssh-rule-save")); wait(20)
            compare(page.monitorRules.length, 1); compare(page.monitorRules[0].color, "#80b9ff")
            verify(page.monitorRules[0].pauseOnMatch === false)
            verify(page.monitorRules[0].highlightWholeLine === false)
            verify(!editor.dirty)
            mouseClick(scope); verify(editor.dirty)
            editor.loadRule(page.monitorRules[0].id)
            verify(scope.checked); verify(!editor.dirty)
            mouseClick(scope)
            mouseClick(item(editor, "ssh-rule-save")); wait(20)
            verify(page.monitorRules[0].highlightWholeLine)
            editor.loadRule(page.monitorRules[0].id)
            verify(!scope.checked); verify(!editor.dirty)
            mouseClick(scope)
            mouseClick(item(editor, "ssh-rule-save")); wait(20)
            verify(page.monitorRules[0].highlightWholeLine === false)
            editor.close(); wait(20)
            const a = page.activeSession
            compare(item(page, "ssh-monitor-rules").text, "规则库 (1)")
            compare(item(a, "ssh-monitor-enabled").text, "启用规则监测")
            compare(item(a, "ssh-monitor-footer").text, "规则未启用")
            openSessionMenu(a, "ssh-rule-select")
            const rules = findChild(a.group.menu, "ssh-rule-select")
            const choice = visualItem(rules.contentItem, "ssh-menu-rule-" + page.monitorRules[0].id)
            verify(choice !== null && choice.visible)
            mouseClick(choice); wait(20)
            compare(a.monitorRuleIds[0], page.monitorRules[0].id)
            compare(rules.title, "选择规则（1）"); verify(rules.visible)
            mouseClick(visualItem(rules.contentItem,"ssh-menu-rule-search")); wait(30)
            verify(item(a,"ssh-rule-picker").visible)
            compare(item(a,"ssh-rule-manage").text,"打开规则库…")
            item(a,"ssh-rule-search").text="missing"; wait(20)
            compare(item(a,"ssh-rule-list").count,0)
            item(a,"ssh-rule-picker").close()
            mouseClick(item(a, "ssh-monitor-enabled")); verify(a.monitorEnabled)
            a.group.menu.close(); wait(20)
            compare(item(a, "ssh-monitor-footer").text, "规则监测：1 条")
            a.monitorRuleIds = []
            compare(item(a, "ssh-monitor-footer").text, "请选择规则")
            a.monitorRuleIds = [page.monitorRules[0].id]
            a.terminal.appendTestLines(["设备 11 超时", "设备 12 超时"], 100); wait(20)
            verify(a.autoScroll && a.monitorHit === null && a.monitorNotice !== null)
            compare(a.terminal.lineModel.get(0).monitorColor, "#80b9ff")
            compare(a.terminal.lineModel.get(1).monitorColor, "#80b9ff")
            a.autoScroll = false
            a.terminal.appendTestLines(["设备 12 超时"], 100); wait(20)
            verify(a.monitorHit === null)
        }
        function test_addSelectedOutputToRuleLibrary_data() {
            return [
                {tag:"literal", rows:["ERROR [42].* (x)+? ^$ {a}|\\ 中文😀"], live:"",
                    text:"ERROR [42].* (x)+? ^$ {a}|\\ 中文😀", pattern:"ERROR \\[42\\]\\.\\* \\(x\\)\\+\\? \\^\\$ \\{a\\}\\|\\\\ 中文😀", detached:false},
                {tag:"hex", rows:["RX AA BB CC DD"], live:"",
                    text:"AA BB CC DD", pattern:"AA BB CC DD", detached:false},
                {tag:"multiline-live", rows:["[A].", "", "中文(超时)"], live:"prompt$ ",
                    text:"[A].\n\n中文(超时)\nprompt$ ", pattern:"\\[A\\]\\.|中文\\(超时\\)|prompt\\$ ", detached:false},
                {tag:"detached", rows:["device timeout"], live:"",
                    text:"device timeout", pattern:"device timeout", detached:true}
            ]
        }
        function test_addSelectedOutputToRuleLibrary(data) {
            make()
            const a = page.activeSession, b = page.addSession()
            page.activateSession(a)
            a.connectionConfigExpanded = false
            a.terminal.appendTestLines(data.rows, 100); a.terminal.liveLine = data.live
            a.autoScroll = false
            if (data.detached) { verify(page.detachSession(a)); wait(40) }
            const start = data.tag === "hex" ? 3 : 0
            const lastId = data.live ? a.liveSelectionId : data.rows.length - 1
            const lastOffset = data.live ? data.live.length : data.rows[data.rows.length - 1].length
            a.textSelectionAnchor = {id:0,position:start}; a.textSelectionFocus = {id:lastId,position:lastOffset}
            clipboard("unchanged"); wait(30)
            const area = item(a, "ssh-text-selection-area")
            mouseClick(area, 40, 30, Qt.RightButton); wait(20)
            const action = item(a, "ssh-selection-add-rule")
            compare(action.text, "添加到规则库"); verify(action.enabled)
            mouseClick(action); wait(30)
            const editor = item(page, "ssh-rule-window")
            verify(editor.visible && editor.dirty); compare(editor.selectedRuleId, -1)
            compare(editor.transientParent, a.Window.window)
            compare(item(editor, "ssh-rule-pattern").text, data.pattern)
            compare(item(editor, "ssh-rule-sample").text, data.text)
            compare(page.monitorRules.length, 0); compare(clipboard(), "unchanged")
            compare(a.terminal.rawInput.length, 0); verify(!a.autoScroll)
            verify(!a.monitorEnabled && a.monitorRuleIds.length === 0)
            const check = testBackend.testMonitorExpression(data.pattern, false, data.text)
            verify(check.valid); verify(check.matches.length > 0)
            if (data.tag === "literal") compare(testBackend.testMonitorExpression(data.pattern, false, "ERROR 42xxx x").matches.length, 0)
            item(editor, "ssh-rule-name").text = "选区规则"
            mouseClick(visualItem(editor.contentItem, "ssh-rule-color-80b9ff"))
            const pauses = data.tag === "literal"
            if (!pauses) mouseClick(item(editor, "ssh-rule-pause-on-match"))
            mouseClick(item(editor, "ssh-rule-highlight-scope"))
            tryCompare(item(editor, "ssh-rule-save"), "enabled", true)
            mouseClick(item(editor, "ssh-rule-save")); wait(30)
            compare(page.monitorRules.length, 1); verify(!editor.dirty)
            compare(page.monitorRules[0].pattern, data.pattern)
            compare(page.monitorRules[0].color, "#80b9ff")
            compare(page.monitorRules[0].pauseOnMatch, pauses); verify(!page.monitorRules[0].highlightWholeLine)
            verify(a.monitorEnabled && a.monitorRuleIds[0] === page.monitorRules[0].id && !a.autoScroll)
            verify(!b.monitorEnabled && b.monitorRuleIds.length === 0)
            const count = a.terminal.currentLineCount, matching = data.text.split("\n")[0]
            a.terminal.appendTestLines([matching, "ordinary", matching], 100); wait(30)
            compare(a.terminal.lineModel.get(count).monitorColor, "#80b9ff")
            compare(a.terminal.lineModel.get(count + 2).monitorColor, "#80b9ff")
            verify(!a.autoScroll && a.monitorHit === null)
            a.resumeFollowing()
            a.terminal.appendTestLines([matching], 100); wait(30)
            compare(a.autoScroll, !pauses)
            compare(a.monitorHit !== null, pauses)
            const sourceId = a.sessionId
            verify(page.saveWorkspace())
            page.destroy(); page = null; wait(40); make()
            compare(page.monitorRules.length, 1); compare(page.monitorRules[0].pattern, data.pattern)
            compare(page.monitorRules[0].color, "#80b9ff"); compare(page.monitorRules[0].pauseOnMatch, pauses)
            const restored = page.sessions.find(function(session) { return session.sessionId === sourceId; })
            verify(restored.monitorEnabled); compare(restored.monitorRuleIds[0], page.monitorRules[0].id)
        }
        function test_ruleSelectionPreservesDraftAndRollsBackFailedSave() {
            make()
            const a = page.activeSession
            const id = page.saveMonitorRule(-1, "原规则", "original", false, "#e8b76a")
            page.openMonitorRules(page.ownerWindow); wait(30)
            const editor = item(page, "ssh-rule-window")
            editor.loadRule(id); item(editor, "ssh-rule-name").text = "未保存修改"
            page.addMonitorRuleFromText("selected[1]", page.ownerWindow, a.sessionId); wait(30)
            verify(item(editor, "ssh-rule-discard-dialog").visible)
            compare(item(editor, "ssh-rule-name").text, "未保存修改")
            mouseClick(item(editor, "ssh-rule-keep-editing")); wait(20)
            compare(editor.selectedRuleId, id); compare(page.monitorRules.length, 1)
            page.addMonitorRuleFromText("selected[2]", page.ownerWindow, a.sessionId); wait(30)
            mouseClick(item(editor, "ssh-rule-discard")); wait(20)
            compare(editor.selectedRuleId, -1)
            compare(item(editor, "ssh-rule-pattern").text, "selected\\[2\\]")
            tryCompare(item(editor, "ssh-rule-save"), "enabled", true)
            testBackend.failSave = true
            mouseClick(item(editor, "ssh-rule-save")); wait(20)
            compare(page.monitorRules.length, 1); compare(page.monitorRules[0].pattern, "original")
            verify(editor.dirty && page.monitorRuleError !== "")
            verify(!a.monitorEnabled && a.monitorRuleIds.length === 0)
            compare(item(editor, "ssh-rule-pattern").text, "selected\\[2\\]")
            testBackend.failSave = false
            mouseClick(item(editor, "ssh-rule-save")); wait(20)
            compare(page.monitorRules.length, 2); verify(!editor.dirty)
            verify(a.monitorEnabled); compare(a.monitorRuleIds[0], page.monitorRules[1].id)
        }
        function test_ruleSelectionCannotActivateAClosedSourceSession() {
            make()
            const a = page.activeSession
            page.addMonitorRuleFromText("selected", page.ownerWindow, a.sessionId); wait(200)
            const editor = item(page, "ssh-rule-window"), b = page.addSession()
            verify(page.removeSession(a)); wait(30)
            mouseClick(item(editor, "ssh-rule-save")); wait(20)
            compare(page.monitorRules.length, 0); verify(editor.dirty)
            verify(page.monitorRuleError.indexOf("来源连接已关闭") >= 0)
            verify(!b.monitorEnabled && b.monitorRuleIds.length === 0)
        }
        function test_ruleSelectionRejectsBlankAndKeepsOversizedTextForEditing() {
            make()
            const a = page.activeSession, editor = item(page, "ssh-rule-window")
            verify(!item(a, "ssh-selection-add-rule").enabled)
            page.addMonitorRuleFromText(" \r\n\t", page.ownerWindow)
            verify(!editor.visible); compare(page.monitorRules.length, 0)
            const text = "x".repeat(5000)
            page.addMonitorRuleFromText(text, page.ownerWindow); wait(200)
            compare(item(editor, "ssh-rule-pattern").text, text)
            compare(item(editor, "ssh-rule-sample").text, text)
            verify(editor.dirty && !editor.validation.valid)
            verify(!item(editor, "ssh-rule-save").enabled); compare(page.monitorRules.length, 0)
        }
        function test_connectionEncodingAlignsAndReconnectComesLast() {
            make()
            const a = page.activeSession
            a.connectionConfigExpanded = true
            const host = item(a, "ssh-host"), encoding = item(a, "ssh-encoding")
            const count = item(a, "ssh-reconnect-count"), interval = item(a, "ssh-reconnect-interval")
            const reconnect = item(a, "ssh-auto-reconnect")
            for (const width of [760, 1180, 1560]) {
                page.width = width; wait(30)
                const grid = host.parent
                compare(encoding.background.mapToItem(grid, 0, 0).x, host.background.mapToItem(grid, 0, 0).x,
                        "encoding and input borders must align at width " + width)
                compare(encoding.contentItem.mapToItem(grid, encoding.contentItem.leftPadding, 0).x,
                        host.mapToItem(grid, host.leftPadding, 0).x,
                        "encoding and input text must align at width " + width)
                for (const field of [count, interval])
                    verify(reconnect.y + reconnect.height / 2 > field.y + field.height / 2
                           || Math.abs(reconnect.y + reconnect.height / 2 - field.y - field.height / 2) < 1 && reconnect.x > field.x,
                           "automatic reconnect must follow retry count and interval")
            }
        }
        function test_matchOnlyHighlightsKeepOtherTextPlainAndSupportLiveOutput() {
            make()
            const a = page.activeSession; connected(a)
            a.connectionConfigExpanded = false
            const id = page.saveMonitorRule(-1, "匹配部分", "ERROR", true, "#7ed6a0", false, false)
            a.monitorRuleIds = [id]; a.monitorEnabled = true
            const text = "设备😀 <tag> error  &  ERROR tail"
            a.terminal.appendTestLines([text], 100); wait(40)
            const view = item(a, "ssh-log-list")
            view.forceLayout()
            const row = view.itemAtIndex(0); verify(row !== null)
            compare(row.wholeLine, false)
            compare(row.ruleColor.toString(), "#7ed6a0")
            compare(row.color.toString(), "#00000000")
            const highlights = item(row, "ssh-log-match-highlights")
            const spans = highlights.children.filter(child => child.objectName === "ssh-match-span")
            compare(spans.length, 2)
            verify(spans[0].x > 0 && spans[0].width > 0 && spans[1].x > spans[0].x + spans[0].width)
            compare(spans[0].color.toString(), "#4d7ed6a0")
            a.textSelectionAnchor = {id:0,position:0}; a.textSelectionFocus = {id:0,position:text.length}
            wait(20); a.copyTextSelection(); compare(clipboard(), text)
            a.terminal.liveLine = "prefix ERROR tail"
            a.terminal.liveMonitorColor = "#7ed6a0"; a.terminal.liveMonitorWholeLine = false
            a.terminal.liveMonitorRanges = [{start:7,length:5}]; wait(20)
            verify(!item(a, "ssh-monitor-live-hit").visible)
            const live = item(a, "ssh-live-match-highlights")
            compare(live.ranges.length, 1)
            verify(live.children.find(child => child.objectName === "ssh-match-span").width > 0)
            a.terminal.monitorMatched({ruleId:id,ruleName:"匹配部分",color:"#7ed6a0",highlightWholeLine:false,
                ranges:[{start:7,length:5}],text:a.terminal.liveLine,lineId:a.liveSelectionId,receivedLineId:a.liveSelectionId,time:"12:00:01"})
            wait(20); verify(!a.autoScroll)
            a.terminal.liveMonitorColor = ""; a.terminal.liveMonitorRanges = []; wait(20)
            verify(!item(a, "ssh-monitor-live-hit").visible)
            compare(live.ranges.length, 1)
        }
        function test_multipleRuleMarksOverlayAndLastWholeLineWins() {
            make()
            const a = page.activeSession; connected(a)
            a.connectionConfigExpanded = false
            const indexId = page.saveMonitorRule(-1, "index", "index", false, "#7ed6a0", false, false)
            const labelId = page.saveMonitorRule(-1, "label", "label", false, "#80b9ff", false, false)
            const logicId = page.saveMonitorRule(-1, "logic", "logic", false, "#e8b76a", false, true)
            const sessionId = page.saveMonitorRule(-1, "session", "SessionEncode", false, "#bb9cf5", false, true)
            a.monitorRuleIds = [indexId, labelId, logicId, sessionId]; a.monitorEnabled = true
            a.terminal.appendTestLines(["index[1] label[2] logic SessionEncode"], 100); wait(40)
            const row = a.terminal.lineModel.get(0)
            compare(row.monitorMarks.length, 4)
            const view = item(a, "ssh-log-list")
            view.forceLayout()
            const visualRow = view.itemAtIndex(0); verify(visualRow !== null)
            compare(visualRow.wholeLineColor.toString(), "#bb9cf5")
            const highlights = item(visualRow, "ssh-log-match-highlights")
            const spans = highlights.children.filter(child => child.objectName === "ssh-match-span")
            compare(spans.length, 2)
            compare(spans[0].spanColor.toString(), "#7ed6a0")
            compare(spans[1].spanColor.toString(), "#80b9ff")
            verify(spans[1].x > spans[0].x + spans[0].width)
            a.terminal.liveLine = "index[3] label[4] logic SessionEncode"
            a.terminal.liveMonitorMarks = [
                {ruleId:indexId, ruleName:"index", color:"#7ed6a0", wholeLine:false, ranges:[{start:0,length:5}]},
                {ruleId:labelId, ruleName:"label", color:"#80b9ff", wholeLine:false, ranges:[{start:9,length:5}]},
                {ruleId:logicId, ruleName:"logic", color:"#e8b76a", wholeLine:true},
                {ruleId:sessionId, ruleName:"session", color:"#bb9cf5", wholeLine:true}
            ]; wait(40)
            verify(item(a, "ssh-monitor-live-hit").visible)
            const liveHighlights = item(a, "ssh-live-match-highlights")
            const liveSpans = liveHighlights.children.filter(child => child.objectName === "ssh-match-span")
            compare(liveSpans.length, 2)
            compare(liveSpans[0].spanColor.toString(), "#7ed6a0")
            compare(liveSpans[1].spanColor.toString(), "#80b9ff")
            compare(liveHighlights.parent.wholeLineColor.toString(), "#bb9cf5")
        }
        function test_pauseRulesTakePrecedenceOverMarkOnlyRules() {
            make()
            const a = page.activeSession
            const mark = page.saveMonitorRule(-1, "标记", "WARN|ERROR", false, "#7ed6a0", false)
            const stop = page.saveMonitorRule(-1, "暂停", "ERROR", false, "#f79a93", true)
            a.monitorRuleIds = [mark, stop]; a.monitorEnabled = true
            a.terminal.appendTestLines(["WARN first", "WARN second"], 100); wait(20)
            verify(a.autoScroll && a.monitorHit === null)
            a.terminal.appendTestLines(["ERROR pause"], 100); wait(20)
            verify(!a.autoScroll); compare(a.monitorHit.ruleName, "暂停"); compare(a.monitorHitColor.toString(), "#f79a93")
            compare(a.terminal.lineModel.get(0).monitorColor, "#7ed6a0")
            compare(a.terminal.lineModel.get(2).monitorColor, "#f79a93")
            a.terminal.appendTestLines(["ERROR while paused", "ERROR repeated"], 100); wait(20)
            compare(a.terminal.lineModel.get(3).monitorColor, "#f79a93")
            compare(a.terminal.lineModel.get(4).monitorColor, "#f79a93")
            verify(!a.autoScroll); compare(a.monitorHit.ruleName, "暂停")
        }
        function test_hexDetection_data() {
            return [
                {tag: "spaced-first", text: "RX: 68 04 07 00 00 00 | TX: AABBCCDDEEFF00112233", position: 4, hex: "68 04 07 00 00 00"},
                {tag: "spaced-middle", text: "RX: 68 04 07 00 00 00", position: 11, hex: "68 04 07 00 00 00"},
                {tag: "spaced-last", text: "RX: 68 04 07 00 00 00", position: 20, hex: "68 04 07 00 00 00"},
                {tag: "compact", text: "RX [aaBBccDD]", position: 7, hex: "AA BB CC DD"},
                {tag: "one-byte", text: "RX: aF!", position: 4, hex: "AF"},
                {tag: "tabs", text: "RX: ab\tCD", position: 6, hex: "AB CD"},
                {tag: "word-after", text: "RX: 68 04 debug", position: 5, hex: "68 04"},
                {tag: "word-before", text: "received AB CD", position: 10, hex: "AB CD"},
                {tag: "non-hex-word", text: "RX: AABB invalid CC", position: 12, hex: ""},
                {tag: "odd-digits", text: "RX: ABCDE", position: 6, hex: ""},
                {tag: "single-digit", text: "RX: A", position: 4, hex: ""},
                {tag: "hex-in-word", text: "RX: frameAABB", position: 11, hex: ""},
                {tag: "hex-prefix", text: "RX: 0xAABB", position: 7, hex: ""},
                {tag: "identifier", text: "RX: id_AABB", position: 9, hex: ""}
            ]
        }
        function test_hexDetection(data) {
            make()
            const a = page.activeSession
            const hex = a.extractHexSegmentFromLine(data.text, data.position)
            compare(hex ? hex.formattedHex : "", data.hex)
        }
        function test_doubleClickSelectsClickedHexAndParsesFromContextMenu() {
            make()
            const a = page.activeSession
            a.terminal.appendTestLines(["RX: aabb | TX: 68 04 07 00 00 00"], 10); wait(30)
            const row = item(a, "ssh-log-line-0"), x = 10 + a.charWidth * 5.1, y = row.height / 2
            clipboard("unchanged")
            mouseDoubleClickSequence(row, x, y); wait(20)
            verify(a.hasTextSelection)
            compare(item(a, "ssh-log-text").selectedText, "aabb")
            compare(a.parseableHexText, "AA BB"); compare(clipboard(), "unchanged")
            mouseClick(row, x, y, Qt.RightButton); wait(20)
            const parse = item(a, "ssh-parse-auto")
            compare(parse.text, "立即解析"); verify(parse.enabled)
            mouseClick(parse); wait(30)
            verify(a.parseWindowVisible)
            compare(a.parsedHex, "AA BB")
            compare(a.parseResultModel.get(0).description, "protocol 0")
            a.copyOnSelect = true
            mouseDoubleClickSequence(row, 10 + a.charWidth * 19.1, y); wait(20)
            compare(clipboard(), "68 04 07 00 00 00")
        }
        function test_invalidDoubleClickAndManualSelectionCannotParse() {
            make()
            const a = page.activeSession
            a.terminal.appendTestLines(["RX: ABCDE | invalid | 680407000000"], 10); wait(30)
            const row = item(a, "ssh-log-line-0")
            mouseDoubleClickSequence(row, 10 + a.charWidth * 6.1, row.height / 2); wait(20)
            verify(!a.hasTextSelection); compare(a.parseableHexText, "")
            verify(!item(a, "ssh-parse-auto").enabled)
            a.textSelectionAnchor = {id: 0, position: 11}; a.textSelectionFocus = {id: 0, position: 18}
            compare(a.parseableHexText, ""); a.parseSelectedHexWithProtocol(0)
            verify(!a.parseWindowVisible); compare(a.parseSessionId, -1)
            a.textSelectionAnchor = {id: 0, position: 22}; a.textSelectionFocus = {id: 0, position: 34}
            compare(a.parseableHexText, "68 04 07 00 00 00")
        }
        function test_doubleClickOnEitherHalfOfEachHexDigit() {
            make()
            const a = page.activeSession
            a.terminal.appendTestLines(["RX: AABB!"], 10); wait(30)
            const row = item(a, "ssh-log-line-0")
            for (let position = 4; position < 8; ++position) {
                for (const fraction of [0.05, 0.95]) {
                    mouseDoubleClickSequence(row, 10 + a.charWidth * (position + fraction), row.height / 2); wait(10)
                    compare(a.parseableHexText, "AA BB", "digit " + position + " fraction " + fraction)
                }
            }
        }
        function test_doubleClickAcrossSegmentsAndLivePrompt() {
            make()
            const a = page.activeSession
            a.terminal.appendTestLines(["RX: aabb", "ccdd!", "next"], 10)
            a.terminal.lineModel.setProperty(0, "continues", true)
            a.terminal.lineModel.setProperty(1, "continuation", true); wait(30)
            const row = item(a, "ssh-log-line-1")
            mouseDoubleClickSequence(row, 10 + a.charWidth * 1.1, row.height / 2); wait(20)
            compare(a.textSelectionRange.firstId, 0); compare(a.textSelectionRange.firstOffset, 4)
            compare(a.textSelectionRange.lastId, 1); compare(a.textSelectionRange.lastOffset, 4)
            compare(a.parseableHexText, "AA BB CC DD")
            a.copyTextSelection(); compare(clipboard(), "aabbccdd")
            connected(a); a.terminal.liveLine = "RX: AA BB!"; wait(30)
            const footer = item(a, "ssh-log-list").footerItem
            mouseDoubleClickSequence(footer, 10 + a.charWidth * 5.1, footer.height / 2); wait(20)
            compare(a.parseableHexText, "AA BB")
            a.terminal.liveLine = "RX: GG BB!"; wait(20)
            compare(a.parseableHexText, "")
        }
        function test_dragSelectionCopiesOnReleaseAndCanBeDisabled() {
            make()
            const a = page.activeSession
            a.terminal.appendTestLines(["abcdef", "second row"], 10); wait(30)
            const row = item(a, "ssh-log-line-0")
            const x1 = 10 + a.charWidth * 1.05, x4 = 10 + a.charWidth * 4.05, y = row.height / 2
            verify(findChild(a, "ssh-copy-all") === null)
            verify(!a.copyOnSelect)
            toggleSessionOption(a, "ssh-copy-on-select"); verify(a.copyOnSelect)
            clipboard("untouched")
            mousePress(row, x1, y)
            mouseMove(row, x4, y, 20, Qt.LeftButton); wait(20)
            verify(a.hasTextSelection)
            compare(clipboard(), "untouched")
            mouseRelease(row, x4, y); wait(20)
            compare(clipboard(), "bcd")
            toggleSessionOption(a, "ssh-copy-on-select"); verify(!a.copyOnSelect)
            clipboard("keep")
            mousePress(row, x4, y)
            mouseMove(row, x1, y, 20, Qt.LeftButton)
            mouseRelease(row, x1, y); wait(20)
            verify(a.hasTextSelection); compare(clipboard(), "keep")
            a.copyTextSelection(); compare(clipboard(), "bcd")
            a.terminal.appendTestLines(["replacement"], 2); wait(20)
            verify(!a.hasTextSelection); compare(clipboard(), "bcd")
        }
        function test_selectionCopiesMarkupAndUnicodeAsPlainText() {
            make()
            const a = page.activeSession
            const text = "<b>中文😀</b>"
            a.terminal.appendTestLines([text], 10); wait(20)
            a.textSelectionAnchor = {id: 0, position: 0}
            a.textSelectionFocus = {id: 0, position: text.length}
            a.copyTextSelection()
            compare(clipboard(), text)
        }
        function test_selectionKeepsTheSameVisibleEditorForHistoryAndLiveRows() {
            make()
            const a = page.activeSession; connected(a)
            const historyText = "history line stays visible"
            const liveText = "prompt$ stays visible"
            a.terminal.appendTestLines([historyText], 10)
            a.terminal.liveLine = liveText; wait(30)
            const row = item(a, "ssh-log-line-0")
            const footer = item(a, "ssh-log-list").footerItem
            const editor = item(row, "ssh-log-text")
            const live = item(footer, "ssh-terminal-live-line")
            compare(editor.text, historyText)
            compare(live.text, liveText)
            verify(editor.visible && live.visible)
            verify(findChild(row, "ssh-selected-text") === null)
            verify(findChild(footer, "ssh-selected-live-text") === null)

            a.textSelectionAnchor = {id: 0, position: 0}
            a.textSelectionFocus = {id: 0, position: historyText.length}; wait(20)
            verify(item(row, "ssh-log-text") === editor)
            verify(editor.visible)
            compare(editor.selectedText, historyText)

            a.clearTextSelection(); wait(20)
            verify(item(row, "ssh-log-text") === editor)
            verify(editor.visible)
            compare(editor.selectedText, "")
            compare(editor.text, historyText)

            a.textSelectionAnchor = {id: a.liveSelectionId, position: 0}
            a.textSelectionFocus = {id: a.liveSelectionId, position: liveText.length}; wait(20)
            verify(item(footer, "ssh-terminal-live-line") === live)
            verify(live.visible)
            compare(live.selectedText, liveText)

            a.clearTextSelection(); wait(20)
            verify(item(footer, "ssh-terminal-live-line") === live)
            verify(live.visible)
            compare(live.selectedText, "")
            compare(live.text, liveText)
        }
        function test_dragSelectionAcrossRowsAndLivePrompt() {
            make()
            const a = page.activeSession; connected(a)
            a.copyOnSelect = true
            a.terminal.liveLine = "prompt line"
            a.terminal.appendTestLines(["abcdef", "ghijkl"], 10); wait(30)
            const first = item(a, "ssh-log-line-0"), last = item(a, "ssh-log-line-1")
            mousePress(last, 10 + a.charWidth * 4.05, last.height / 2)
            mouseMove(first, 10 + a.charWidth * 1.05, first.height / 2, 20, Qt.LeftButton)
            mouseRelease(first, 10 + a.charWidth * 1.05, first.height / 2); wait(20)
            compare(clipboard(), "bcdef\nghij")
            const footer = item(a, "ssh-log-list").footerItem
            mousePress(footer, 10 + a.charWidth * 2.05, footer.height / 2)
            mouseMove(footer, 10 + a.charWidth * 5.05, footer.height / 2, 20, Qt.LeftButton)
            mouseRelease(footer, 10 + a.charWidth * 5.05, footer.height / 2); wait(20)
            compare(clipboard(), "omp")
            verify(item(a, "ssh-terminal-live-line").selectedText === "omp")
        }
        function test_copyOnSelectIsPerSessionAndRestoresWithoutCopying() {
            make()
            const a = page.activeSession
            toggleSessionOption(a, "ssh-copy-on-select")
            const b = page.addSession(); wait(20)
            verify(a.copyOnSelect); verify(!b.copyOnSelect)
            clipboard("preserved")
            page.detachSession(a); wait(30); page.dockSession(a); wait(30)
            verify(a.copyOnSelect); compare(clipboard(), "preserved")
            verify(page.saveWorkspace())
            page.destroy(); page = null; wait(30); make()
            verify(page.sessions[0].copyOnSelect); verify(!page.sessions[1].copyOnSelect)
            compare(clipboard(), "preserved")
        }
        function test_configurationSidebarAndTabMenusKeepIndependentWindowActions() {
            make()
            const sidebar = item(page, "ssh-session-sidebar")
            for (const name of ["ssh-sidebar-toggle", "ssh-new-session", "ssh-saved-connections"]) {
                let node = item(page, name)
                while (node && node !== sidebar) node = node.parent
                compare(node, sidebar)
            }
            const a = page.activeSession; connected(a)
            const disconnects = a.terminal.disconnectCalls
            openSessionMenu(a, "ssh-layout-menu")
            mouseClick(item(scene.Window.window.contentItem, "ssh-session-detach-" + a.sessionId)); wait(40)
            verify(a.detached && a.monitorWindow.visible)
            openSessionMenu(a, "ssh-layout-menu")
            const dock = visualItem(findChild(a.group.menu, "ssh-layout-menu").contentItem, "ssh-session-dock-" + a.sessionId)
            verify(dock !== null && dock.visible); mouseClick(dock); wait(40)
            verify(!a.detached); compare(a.terminal.disconnectCalls, disconnects)
            verify(waitForPolish(page))
            mouseClick(item(page, "ssh-sidebar-toggle"))
            tryCompare(page, "sidebarCollapsed", true)
            verify(waitForPolish(page))
            tryVerify(() => sidebar.width <= 50, 5000, "collapsed sidebar width=" + sidebar.width)
            verify(item(page, "ssh-session-row-" + a.sessionId).visible)
            mouseClick(item(page, "ssh-new-session")); wait(20)
            compare(page.sessions.length, 2)
            select(0); compare(page.activeSession, a)
            openSessionMenu(a)
            mouseClick(item(scene.Window.window.contentItem, "ssh-session-config-" + a.sessionId)); wait(20)
            verify(item(page, "ssh-connection-editor").opened)
            verify(!item(a, "ssh-connection-panel").visible)
            item(page, "ssh-connection-editor").close()
        }
        function test_sidebarConfigurationConnectionAndCollapseAreIndependent() {
            make()
            const a = page.activeSession
            const configA = page.blankConfiguration("设备 A")
            configA.host = "a.example"; configA.port = "2222"; configA.password = "password-a"
            a.restoreConfiguration(configA)
            const b = page.addSession(page.blankConfiguration("设备 B")); wait(30)
            b.connectionConfigExpanded = true
            item(b, "ssh-host").text = "b.example"
            item(b, "ssh-username").text = "user-b"
            item(b, "ssh-password-draft").text = "password-b"
            connectionAction(a)
            compare(page.activeSession, a); compare(a.terminal.connectCalls, 1)
            compare(a.terminal.host, "a.example"); compare(a.terminal.port, 2222)
            compare(a.terminal.password, "password-a"); compare(b.terminal.connectCalls, 0)
            connectionAction(b)
            compare(page.activeSession, b); compare(b.terminal.connectCalls, 1)
            compare(b.terminal.host, "b.example"); compare(b.terminal.username, "user-b")
            compare(b.terminal.password, "password-b"); compare(a.terminal.password, "password-a")
            const before = item(page, "ssh-log-list").width
            mouseClick(item(page, "ssh-sidebar-toggle")); wait(30)
            verify(page.sidebarCollapsed); verify(item(page, "ssh-log-list").width > before + 100)
            verify(page.saveWorkspace())
            page.destroy(); page = null; wait(40); make()
            verify(page.sidebarCollapsed); compare(page.sessions.length, 2)
            compare(item(page.sessions[1], "ssh-host").text, "b.example")
            compare(page.sessions[0].terminal.connectCalls, 0); compare(page.sessions[1].terminal.connectCalls, 0)
        }
        function test_detachAndNativeCloseKeepTheSameSessionAndViewport() {
            make()
            const a = page.activeSession; connected(a)
            a.connectionConfigExpanded = false; a.autoScroll = false; a.terminalFontSize = 18
            a.terminal.liveLine = "device-a$ "
            const rows = []
            for (let i = 0; i < 200; ++i) rows.push("Row " + i + " RX 68 04 07 00 00 00")
            a.terminal.appendTestLines(rows, 300); wait(40)
            const view = item(a, "ssh-log-list")
            view.positionViewAtIndex(80, ListView.Beginning); wait(30)
            a.selectLineAndExtractHex(80); a.parseSelectedHexWithProtocol(3); wait(30)
            const terminal = a.terminal; const model = terminal.lineModel
            const parseModel = a.parseResultModel; const selected = a.selectedHexText
            const disconnects = terminal.disconnectCalls; const connects = terminal.connectCalls
            verify(page.detachSession(a)); wait(50)
            const window = a.monitorWindow
            verify(a.detached && window.visible && a.visible && !a.autoScroll)
            compare(a.ownerWindow, window); verify(window.transientParent === null)
            compare(window.modality, Qt.NonModal); verify((window.flags & Qt.Window) !== 0)
            compare(a.terminal, terminal); compare(a.terminal.lineModel, model)
            compare(a.parseResultModel, parseModel); verify(a.parseWindowVisible)
            compare(a.selectedHexText, selected); compare(a.terminalFontSize, 18)
            compare(terminal.lineIdAt(view.indexAt(1, view.contentY + 1)), 80)
            page.visible = false; wait(20)
            verify(a.visible); verify(terminal.presentationActive)
            terminal.appendTestLines(["background while detached"], 300); wait(30)
            compare(terminal.currentLineCount, 201)
            page.visible = true
            window.width = 1400; window.height = 850; wait(30)
            verify(view.width > 1200)
            window.close(); wait(50)
            verify(!a.detached && !window.visible && a.visible && !a.autoScroll)
            compare(page.activeSession, a); compare(a.terminal, terminal)
            compare(terminal.disconnectCalls, disconnects); compare(terminal.connectCalls, connects)
            compare(a.selectedHexText, selected); compare(a.terminalFontSize, 18)
            compare(terminal.lineIdAt(view.indexAt(1, view.contentY + 1)), 80)
            verify(a.parseWindowVisible); compare(a.parseResultModel, parseModel)
        }
        function test_multipleNativeWindowsMaximizeDockAndShutdownIndependently() {
            make()
            const a = page.activeSession; connected(a)
            const b = page.addSession(); connected(b)
            verify(page.detachSession(a)); verify(page.detachSession(b)); wait(50)
            const wa = a.monitorWindow; const wb = b.monitorWindow
            verify(wa !== wb); verify(wa.visible && wb.visible)
            const aDisconnects = a.terminal.disconnectCalls; const bDisconnects = b.terminal.disconnectCalls
            wa.showMaximized(); wait(30); compare(wa.visibility, Window.Maximized)
            wa.showNormal(); wait(30); compare(wa.visibility, Window.Windowed)
            b.connectionConfigExpanded = true; wait(20)
            verify(b.connectionConfigExpanded); compare(b.ownerWindow, wb)
            verify(page.dockSession(a)); wait(30)
            verify(!wa.visible && wb.visible); verify(!a.detached && b.detached)
            compare(a.terminal.disconnectCalls, aDisconnects); compare(b.terminal.disconnectCalls, bDisconnects)
            testBackend.failSave = true
            verify(!page.removeSession(b)); verify(wb.visible && b.detached)
            compare(b.terminal.disconnectCalls, bDisconnects)
            testBackend.failSave = false
            page.closing(); wait(30)
            verify(!wa.visible && !wb.visible)
            verify(!a.terminal.connected && !b.terminal.connected)
        }
        function test_pendingHostKeyMovesWithoutRejectingOrAcceptingIt() {
            make()
            const a = page.activeSession
            a.useSystemCredentials = true
            a.terminal.connectionState = 1
            a.terminal.requestKey("move-key", "SHA256:move-fingerprint"); wait(30)
            verify(a.pendingHostKey !== null)
            verify(page.detachSession(a)); wait(50)
            verify(item(a, "ssh-host-key-dialog").visible)
            compare(a.terminal.confirmations.length, 0); verify(a.terminal.hostKeyPending)
            verify(page.dockSession(a)); wait(50)
            verify(item(a, "ssh-host-key-dialog").visible)
            compare(a.terminal.confirmations.length, 0); verify(a.terminal.hostKeyPending)
            a.respondHostKey(false)
            compare(a.terminal.confirmations.length, 1); verify(!a.terminal.confirmations[0].accept)
        }
        function test_detachedWindowKeyboardAndSearchOnlyReachTheFocusedSession() {
            make()
            const a = page.activeSession; connected(a)
            const b = page.addSession(); connected(b)
            page.detachSession(a); page.detachSession(b); wait(40)
            const originalParent = parent
            try {
                parent = a.monitorWindow.contentItem
                a.monitorWindow.requestActivate(); wait(30)
                a.terminalInput.forceActiveFocus()
                keyClick(Qt.Key_X)
                compare(a.terminal.rawInput.join(""), "x"); compare(b.terminal.rawInput.length, 0)
                keyClick(Qt.Key_F, Qt.ControlModifier | Qt.ShiftModifier); wait(20)
                verify(a.findBarVisible); verify(!b.findBarVisible)
                parent = b.monitorWindow.contentItem
                b.monitorWindow.requestActivate(); wait(30)
                b.terminalInput.forceActiveFocus()
                keyClick(Qt.Key_Y)
                compare(b.terminal.rawInput.join(""), "y"); compare(a.terminal.rawInput.join(""), "x")
                keyClick(Qt.Key_F, Qt.ControlModifier | Qt.ShiftModifier); wait(20)
                verify(b.findBarVisible)
            } finally {
                parent = originalParent
            }
        }
        function test_connectionEditorKeepsLiveStateAndDoesNotPutParametersAboveTerminal() {
            make()
            const a = page.activeSession
            const config = page.blankConfiguration("设备 A")
            config.host = "draft.example"; config.port = "2222"; config.password = "synthetic-draft"
            a.restoreConfiguration(config)
            connected(a); a.terminal.logging = true; wait(30)
            const connects = a.terminal.connectCalls, disconnects = a.terminal.disconnectCalls
            verify(!item(a, "ssh-config-heading").visible)
            verify(!item(a, "ssh-connection-scroll").visible)
            verify(item(a, "ssh-connection-panel").height < 80)

            openSessionMenu(a); wait(20)
            mouseClick(item(scene.Window.window.contentItem, "ssh-session-config-" + a.sessionId)); wait(30)
            const editor = item(page, "ssh-connection-editor")
            verify(editor.opened)
            verify(!item(page, "ssh-editor-host").enabled)
            const encoding = item(page, "ssh-editor-encoding")
            compare(encoding.leftPadding + encoding.contentItem.leftPadding,
                    item(page, "ssh-editor-host").leftPadding)
            verify(findChild(page, "ssh-editor-auto-follow") === null)
            item(page, "ssh-editor-copy-on-select").checked = true
            item(page, "ssh-editor-wrap-lines").checked = true
            mouseClick(item(page, "ssh-editor-save")); wait(30)
            verify(!editor.opened)
            verify(a.terminal.connected && a.terminal.logging)
            verify(a.copyOnSelect && a.wrapLogLines)
            compare(a.terminal.connectCalls, connects); compare(a.terminal.disconnectCalls, disconnects)
            verify(!item(a, "ssh-config-heading").visible)
            const saved = page.savedConnections.find(function(value) { return value.host === "draft.example" })
            verify(!!saved && saved.autoFollowEnabled === undefined && saved.copyOnSelect && saved.wrapLogLines)

            verify(page.detachSession(a)); wait(30)
            openSessionMenu(a); wait(20)
            mouseClick(item(scene.Window.window.contentItem, "ssh-session-config-" + a.sessionId)); wait(30)
            verify(editor.opened && !item(page, "ssh-editor-host").enabled)
            mouseClick(item(page, "ssh-editor-cancel")); wait(20)
            verify(!editor.opened)
            verify(a.terminal.connected && a.terminal.logging)
        }
        function test_removedSummaryLeavesNoEmptyPanelButKeepsConfigErrorsAndLogging() {
            make()
            const a = page.activeSession
            const panel = item(a, "ssh-connection-panel")
            verify(findChild(a, "ssh-target-summary") === null)
            a.connectionConfigExpanded = false; wait(20)
            verify(!panel.visible)
            verify(findChild(a, "ssh-pause") === null)
            a.terminal.lastError = "synthetic connection failure"; wait(20)
            a.terminal.connectionState = 4; wait(20)
            verify(panel.visible && item(a, "ssh-current-connection-error").visible)
            a.terminal.lastError = ""; wait(20); verify(!panel.visible)
            a.terminal.logging = true; wait(20)
            verify(panel.visible && item(a, "ssh-log-status").visible)
            a.terminal.logging = false; wait(20); verify(!panel.visible)
            openSessionMenu(a); wait(20)
            mouseClick(item(scene.Window.window.contentItem, "ssh-session-config-" + a.sessionId)); wait(20)
            verify(item(page, "ssh-connection-editor").opened)
            verify(!panel.visible && !item(a, "ssh-connection-scroll").visible)
            item(page, "ssh-connection-editor").close()
            verify(page.detachSession(a)); wait(30)
            verify(!panel.visible && !item(a, "sshConnectAction").visible)
            verify(page.dockSession(a)); wait(30)
            verify(!panel.visible && findChild(a,"ssh-pause") === null)
        }
        function test_onlyCurrentConnectionErrorAppearsOutsideRemoteOutput() {
            make()
            const a = page.activeSession, status = item(a, "ssh-current-connection-error")
            verify(!status.visible)
            a.terminal.lastError = "当前连接失败"
            a.terminal.connectionState = 4
            verify(status.visible); verify(status.text.indexOf("当前连接失败") >= 0)
            compare(a.terminal.currentLineCount, 0)
            a.terminal.lastError = ""; connected(a)
            verify(!status.visible)
        }
        function test_connectionRetryDoesNotPresentPreviousErrorAsCurrentFailure_data() {
            return [{tag: "docked", detached: false}, {tag: "detached", detached: true}]
        }
        function test_connectionRetryDoesNotPresentPreviousErrorAsCurrentFailure(data) {
            make()
            const a = page.activeSession
            a.connectionConfigExpanded = false
            const error = item(a, "ssh-current-connection-error")
            const progress = item(a, "ssh-connection-progress")
            if (data.detached) { verify(page.detachSession(a)); wait(30) }
            openSessionMenu(a)
            const button = visualItem(a.group.menu.contentItem, "ssh-session-connect-" + a.sessionId)
            verify(button !== null)
            keyClick(Qt.Key_Escape); wait(20)
            a.terminal.lastError = "上一轮 TCP 失败"
            a.terminal.connectionState = 3
            a.terminal.statusMessage = "连接中断，0.5秒后尝试重连 (1/5)"
            wait(20)
            verify(!error.visible); verify(progress.visible)
            compare(progress.text, a.terminal.statusMessage); compare(button.text, "取消")
            a.terminal.connectionState = 1
            a.terminal.statusMessage = "正在重连 (1/5)..."
            wait(20)
            verify(!error.visible); verify(progress.visible); compare(button.text, "取消")
            a.terminal.connectionState = 2; a.terminal.connected = true
            wait(20)
            verify(!error.visible && !progress.visible)
            verify(!item(a, "ssh-connection-panel").visible)
            compare(button.text, "断开")
            a.terminal.connected = false; a.terminal.connectionState = 4
            wait(20)
            verify(error.visible && !progress.visible)
            verify(error.text.indexOf("上一轮 TCP 失败") >= 0); compare(button.text, "连接")
            a.terminal.connectionState = 5
            wait(20); verify(!error.visible && !progress.visible)
            compare(a.terminal.currentLineCount, 0)
        }
        function test_longLinesSupportHorizontalScrollingAndWrappedSelection() {
            make()
            const a = page.activeSession
            a.connectionConfigExpanded = false; page.width = 954
            const text = "prefix " + "x".repeat(300) + " AA BB CC DD"
            a.terminal.appendTestLines([text], 10); wait(30)
            const view = item(a, "ssh-log-list"), row = item(a, "ssh-log-line-0")
            verify(waitForPolish(a))
            tryVerify(() => view.contentWidth > view.width * 2)
            verify(item(a, "ssh-log-horizontal-scroll").visible)
            verify(view.y + view.height <= item(a, "ssh-log-horizontal-scroll").y + 1)
            const hex = text.indexOf("AA BB")
            view.contentX = Math.min(view.contentWidth - view.width, hex * a.charWidth - 30)
            a.autoScroll = false; wait(20)
            mouseDoubleClickSequence(row, 10 + a.charWidth * (hex + 3.1), row.height / 2); wait(20)
            compare(a.parseableHexText, "AA BB CC DD")
            a.copyTextSelection(); compare(clipboard(), "AA BB CC DD")
            toggleSessionOption(a, "ssh-wrap-lines")
            verify(a.wrapLogLines); tryCompare(view, "contentX", 0)
            compare(view.contentWidth, view.width)
            verify(!item(a, "ssh-log-horizontal-scroll").visible)
            tryVerify(() => row.height > a.terminalFontSize * 3)
            const editor = item(row, "ssh-log-text")
            compare(editor.selectedText, "AA BB CC DD")
            const start = editor.positionToRectangle(2), end = editor.positionToRectangle(100)
            verify(end.y > start.y)
            a.clearTextSelection(); wait(20)
            mousePress(row, 10 + start.x + a.charWidth * 0.05, start.y + start.height / 2)
            mouseMove(row, 10 + end.x + a.charWidth * 0.05, end.y + end.height / 2, 20, Qt.LeftButton)
            mouseRelease(row, 10 + end.x + a.charWidth * 0.05, end.y + end.height / 2); wait(20)
            a.copyTextSelection(); compare(clipboard(), text.slice(2, 100))
            const point = item(row, "ssh-log-text").positionToRectangle(hex + 3)
            mouseDoubleClickSequence(row, 10 + point.x + a.charWidth * 0.55, point.y + point.height / 2); wait(20)
            compare(a.parseableHexText, "AA BB CC DD")
        }
        function test_followingLongLinesKeepsHorizontalPosition() {
            make()
            const a = page.activeSession
            a.connectionConfigExpanded = false
            const view = item(a, "ssh-log-list")
            const wide = "wide " + "x".repeat(600)
            a.terminal.appendTestLines([wide, wide, wide], 1000); wait(40)
            tryVerify(() => view.atYEnd && view.contentWidth > view.width * 2)
            connected(a)
            a.focusTerminal()
            const position = Math.min(view.contentWidth - view.width, 180)
            view.contentX = position; wait(20); compare(view.contentX, position)
            for (let i = 0; i < 3; ++i) {
                a.terminal.appendTestLines(["incoming " + i + " " + "y".repeat(600)], 1000); wait(40)
                compare(view.contentX, position)
                verify(view.atYEnd)
            }
            a.terminal.liveLine = "prompt$ " + "z".repeat(400)
            a.terminal.cursorPosition = a.terminal.liveLine.length
            a.sendTerminalText("z"); wait(40)
            verify(view.contentX > position)
            verify(view.footerItem.caretX <= view.contentX + view.width)
            wait(700)
            const typedPosition = view.contentX
            a.terminal.appendTestLines(["after typing " + "y".repeat(600)], 1000); wait(40)
            verify(Math.abs(view.contentX - typedPosition) < 1)
        }
        function test_wrappingAndCopyOptionsAreOrderedPerSessionAndPersist() {
            make()
            const a = page.activeSession
            openSessionMenu(a); wait(20)
            const copy = visualItem(scene.Window.window.contentItem, "ssh-copy-on-select")
            const wrap = visualItem(scene.Window.window.contentItem, "ssh-wrap-lines")
            verify(copy !== null && wrap !== null)
            verify(wrap.y > copy.y && wrap.y - copy.y <= copy.height + 4)
            keyClick(Qt.Key_Escape); wait(20)
            verify(findChild(a, "ssh-copy-on-select") === null)
            toggleSessionOption(a, "ssh-copy-on-select")
            toggleSessionOption(a, "ssh-wrap-lines")
            connected(a)
            a.terminal.liveLine = "prompt " + "p".repeat(350); a.terminal.cursorColumn = a.terminal.liveLine.length
            wait(30)
            const view = item(a, "ssh-log-list"), footer = view.footerItem
            tryVerify(() => footer.height > a.terminalFontSize * 3)
            verify(footer.caretY > 0)
            const b = page.addSession(); wait(20)
            verify(a.wrapLogLines && a.copyOnSelect)
            verify(!b.wrapLogLines && !b.copyOnSelect)
            verify(page.detachSession(a)); wait(30)
            verify(a.wrapLogLines); verify(page.dockSession(a)); wait(20)
            verify(page.saveWorkspace())
            page.destroy(); page = null; wait(40); make()
            verify(page.sessions[0].wrapLogLines && page.sessions[0].copyOnSelect)
            verify(!page.sessions[1].wrapLogLines)
            const config = page.blankConfiguration("旧配置")
            page.activeSession.restoreConfiguration(config); verify(!page.activeSession.wrapLogLines)
            config.wrapLogLines = "true"; verify(!page.validConfiguration(config))
        }
        function test_checkedMenuIndicatorsStayInTheirOwnGutter() {
            make()
            const a = page.activeSession
            a.copyOnSelect = true
            openSessionMenu(a); wait(20)
            const copy = visualItem(scene.Window.window.contentItem, "ssh-copy-on-select")
            const wrap = visualItem(scene.Window.window.contentItem, "ssh-wrap-lines")
            verify(copy !== null && wrap !== null)
            for (const option of [copy, wrap]) {
                const mark = option.indicator
                verify(mark.x >= 0)
                verify(mark.x + mark.width + 4 <= option.contentItem.x)
                compare(mark.y + mark.height / 2, option.height / 2)
                verify(mark.visible)
                compare(mark.width, mark.height)
                verify(mark.border.width > 0)
                compare(item(mark, "menu-check-mark").visible, option.checked)
            }
            mouseMove(copy, copy.width / 2, copy.height / 2); wait(20)
            verify(copy.highlighted)
            compare(copy.indicator.border.color, copy.contentItem.color)
            compare(item(copy.indicator, "menu-check-mark").color, copy.contentItem.color)
            mouseClick(copy); wait(20)
            verify(!a.copyOnSelect)
            keyClick(Qt.Key_Escape); wait(20)
            toggleSessionOption(a, "ssh-wrap-lines")
            verify(a.wrapLogLines)
        }
        function test_sessionOptionsStayOpenForMouseAndKeyboard() {
            make()
            const a=page.activeSession
            openSessionMenu(a)
            const menu=a.group.menu
            const copy=visualItem(menu.contentItem,"ssh-copy-on-select"), wrap=visualItem(menu.contentItem,"ssh-wrap-lines")
            for (let i=0;i<2;++i) {
                mouseClick(copy); wait(20); compare(a.copyOnSelect,i===0); verify(menu.opened)
                mouseClick(wrap); wait(20); compare(a.wrapLogLines,i===0); verify(menu.opened)
            }
            menu.currentIndex=0; copy.forceActiveFocus(); keyClick(Qt.Key_Space); wait(20)
            verify(a.copyOnSelect && menu.opened)
            menu.currentIndex=1; wrap.forceActiveFocus(); keyClick(Qt.Key_Return); wait(20)
            verify(a.wrapLogLines && menu.opened)
            keyClick(Qt.Key_Escape); wait(20); verify(!menu.visible)
            a.group.menu.close(); wait(20)
            openSessionMenu(a,"ssh-log-menu")
            mouseClick(visualItem(findChild(a.group.menu,"ssh-log-menu").contentItem,"ssh-session-log-settings-"+a.sessionId)); wait(20)
            verify(!a.group.menu.visible && item(a,"ssh-log-settings").visible)
        }
        function test_terminalTextHasHigherContrastAcrossOutputAndSelection() {
            make()
            const a = page.activeSession
            a.connectionConfigExpanded = false; connected(a)
            a.terminal.appendTestLines(["RX 68 04 07 00 中文日志"], 10)
            a.terminal.liveLine = "root@device:~# "; wait(30)
            const row = item(a, "ssh-log-line-0")
            compare(item(row, "ssh-log-text").color, a.terminalText)
            compare(item(a, "ssh-terminal-live-line").color, a.terminalText)
            const luminance = function(color) {
                const linear = c => c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)
                return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b)
            }
            verify((luminance(a.terminalText) + 0.05) / (luminance(a.terminalBg) + 0.05) > 15)
            mouseDoubleClickSequence(row, 10 + a.charWidth * 4, row.height / 2); wait(20)
            const editor = item(row, "ssh-log-text")
            compare(editor.color, a.terminalText); compare(editor.selectedTextColor, a.terminalText)
            a.wrapLogLines = true; wait(20)
            compare(item(a, "ssh-terminal-live-line").color, a.terminalText)
        }
        function test_wrappedMatchHighlightsSpanMultipleVisualRows() {
            make()
            const a = page.activeSession
            a.wrapLogLines = true; a.connectionConfigExpanded = false; page.width = 954
            const id = page.saveMonitorRule(-1, "跨行标记", "(?:ERROR)+", false, "#80b9ff", false, false)
            a.monitorRuleIds = [id]; a.monitorEnabled = true
            a.terminal.appendTestLines(["prefix " + "ERROR".repeat(70) + " end"], 10); wait(30)
            const row = item(a, "ssh-log-line-0")
            verify(waitForPolish(a))
            const mark = item(row, "ssh-log-match-highlights")
            tryVerify(() => mark.children.filter(child => child.objectName === "ssh-wrapped-match-span").length > 1)
            for (const span of mark.children.filter(child => child.objectName === "ssh-wrapped-match-span")) {
                verify(span.width > 0 && span.height > 0)
                verify(span.x >= 0 && span.x + span.width <= mark.width + 1)
                verify(span.y >= 0 && span.y + span.height <= row.height + 1)
            }
            a.terminal.lineModel.setProperty(0, "lineText", "short"); wait(20)
            tryVerify(() => mark.children.filter(child => child.objectName === "ssh-wrapped-match-span").length === 0)
        }
        function test_quickCommandPencilIsAnOverlayAndNeverSends() {
            make()
            const a = page.activeSession; connected(a)
            a.addCustomCommand("查看日志", "tail -f app.log")
            tryVerify(() => visualItem(a, "ssh-command-edit-0") !== null)
            const run = visualItem(a, "ssh-command-run-0"), edit = visualItem(a, "ssh-command-edit-0")
            verify(!a.commandManagementExpanded)
            verify(edit.visible && edit.enabled && edit.width <= 20 && edit.height <= 20)
            compare(edit.parent, run.parent); compare(edit.parent.width, run.width)
            verify(edit.x + edit.width <= run.width && edit.x > run.width / 2)
            verify(waitForRendering(edit)); mouseClick(edit)
            verify(a.commandManagementExpanded)
            compare(item(a, "ssh-command-content").text, "tail -f app.log")
            compare(a.terminal.sentCommands.length, 0)
            const remove = visualItem(a, "ssh-command-delete-0")
            verify(remove.x + remove.width < edit.x)
        }
        function test_logWriteSettingsValidatePersistAndReachOnlyTheirOwnSession() {
            make()
            const a = page.activeSession
            compare(a.logFlushIntervalSeconds, 1); compare(a.logBufferKiB, 256)
            openSessionMenu(a, "ssh-log-menu"); wait(20)
            const overlay = scene.Window.window.contentItem
            mouseClick(item(overlay, "ssh-session-log-settings-" + a.sessionId)); wait(20)
            verify(item(a, "ssh-log-settings").visible)
            item(a, "ssh-log-interval").text = "0"
            verify(!item(a, "ssh-log-settings-save").enabled)
            item(a, "ssh-log-interval").text = "2"
            item(a, "ssh-log-buffer-size").text = "8193"
            verify(!item(a, "ssh-log-settings-save").enabled)
            item(a, "ssh-log-buffer-size").text = "32"
            mouseClick(item(a, "ssh-log-settings-save")); wait(20)
            compare(a.logFlushIntervalSeconds, 2); compare(a.logBufferKiB, 32)
            a.selectLogFile("file:///tmp/buffered.log")
            compare(a.terminal.logCalls[0].intervalMs, 2000)
            compare(a.terminal.logCalls[0].bufferKiB, 32)
            const b = page.addSession(); wait(20)
            compare(b.logFlushIntervalSeconds, 1); compare(b.logBufferKiB, 256)
            verify(page.saveWorkspace())
            page.destroy(); page = null; wait(40); make()
            compare(page.sessions[0].logFlushIntervalSeconds, 2); compare(page.sessions[0].logBufferKiB, 32)
            compare(page.sessions[1].logFlushIntervalSeconds, 1); compare(page.sessions[1].logBufferKiB, 256)
            const config = page.blankConfiguration("旧配置")
            verify(page.validConfiguration(config))
            page.activeSession.restoreConfiguration(config)
            compare(page.activeSession.logFlushIntervalSeconds, 1); compare(page.activeSession.logBufferKiB, 256)
            config.logBufferKiB = "256"; verify(!page.validConfiguration(config))
            config.logBufferKiB = 256; config.logFlushIntervalSeconds = 61; verify(!page.validConfiguration(config))
            page.activeSession.openLogSettings(); wait(20)
            item(page.activeSession, "ssh-log-interval").text = "5"
            mouseClick(item(page.activeSession, "ssh-log-settings-cancel"))
            compare(page.activeSession.logFlushIntervalSeconds, 1)
        }
        function test_applicationExitWaitsForBusyLogOperationsAndStopAcknowledgements() {
            make()
            const a = page.activeSession
            a.selectLogFile("file:///tmp/exit-a.log")
            const b = page.addSession()
            b.selectLogFile("file:///tmp/exit-b.log")
            b.terminal.logBusy = true
            const ready = []
            page.logShutdownReady.connect(function(success) { ready.push(success) })
            verify(page.hasOpenLogFiles())
            page.finishLogsBeforeExit(); wait(20)
            compare(ready.length, 0); verify(!a.terminal.logging); verify(b.terminal.logging)
            verify(!page.enabled && !b.enabled)
            b.terminal.logBusy = false
            tryCompare(b.terminal, "logging", false)
            tryCompare(page, "waitingForLogShutdown", false)
            compare(ready.length, 1); verify(ready[0]); verify(!page.hasOpenLogFiles())
            verify(page.enabled && b.enabled)
            a.selectLogFile("file:///tmp/exit-failure.log")
            a.terminal.logError = "模拟停止时的写入错误"
            page.finishLogsBeforeExit(); wait(20)
            compare(ready.length, 2); verify(!ready[1])
        }
        function test_sessionLogControlsAreIndependentAndSurviveDetaching() {
            make()
            const a = page.activeSession
            const b = page.addSession()
            b.selectLogFile("file:///tmp/session-b.log")
            verify(b.terminal.logging); verify(!a.terminal.logging)
            compare(b.terminal.logCalls[0].kind, "start")
            compare(b.terminal.logCalls[0].append, false)
            compare(b.terminal.sentCommands.length, 0)
            verify(findChild(b, "ssh-log-menu") === null)
            verify(findChild(b, "ssh-log-start") === null)
            verify(item(b, "ssh-log-status").visible)
            verify(item(b, "ssh-log-status").text.indexOf("session-b.log") >= 0)
            page.width = 954
            const status = item(b, "ssh-log-status")
            verify(waitForRendering(status))
            const corner = status.mapToItem(b, status.width, status.height)
            verify(corner.x <= b.width && corner.y <= b.height)
            mouseClick(item(b, "ssh-clear")); verify(b.terminal.logging)
            b.autoScroll = false; verify(b.terminal.logging)
            select(0); verify(b.terminal.logging)
            verify(page.detachSession(b)); wait(30)
            verify(b.terminal.logging && b.monitorWindow.visible)
            openSessionMenu(b, "ssh-log-menu"); wait(20)
            const overlay = findChild(b.group.menu, "ssh-log-menu").contentItem
            verify(!item(overlay, "ssh-session-log-start-" + b.sessionId).enabled)
            verify(item(overlay, "ssh-session-log-stop-" + b.sessionId).enabled)
            mouseClick(item(overlay, "ssh-session-log-stop-" + b.sessionId))
            verify(!b.terminal.logging); compare(b.terminal.logCalls[1].kind, "stop")
            verify(page.dockSession(b)); wait(20)
            b.selectLogFile("file:///tmp/restarted.log")
            verify(b.terminal.logging)
            b.closeSshSession(); verify(!b.terminal.logging)
        }
        function test_existingSessionLogOffersAppendOverwriteAndCancel() {
            make()
            const a = page.activeSession
            a.terminal.existingLogFile = true
            a.selectLogFile("file:///tmp/existing.log")
            verify(item(a, "ssh-log-file-conflict").visible)
            compare(a.terminal.logCalls.length, 0)
            mouseClick(item(a, "ssh-log-file-cancel")); wait(20)
            compare(a.terminal.logCalls.length, 0)
            compare(String(a.pendingLogFile), "")
            a.selectLogFile("file:///tmp/existing.log"); wait(20)
            mouseClick(item(a, "ssh-log-file-append"))
            verify(a.terminal.logging); verify(a.terminal.logCalls[0].append)
            a.terminal.stopLogging()
            a.selectLogFile("file:///tmp/existing.log"); wait(20)
            mouseClick(item(a, "ssh-log-file-overwrite"))
            verify(a.terminal.logging); verify(!a.terminal.logCalls[2].append)
        }
        function test_sessionLogExportAndErrorsBelongToSelectedTab() {
            make()
            const a = page.activeSession
            const b = page.addSession()
            b.terminal.existingLogFile = true
            b.exportCurrentLog = true
            b.selectLogFile("file:///tmp/export.log"); wait(20)
            verify(!item(b, "ssh-log-file-append").visible)
            mouseClick(item(b, "ssh-log-file-overwrite"))
            compare(b.terminal.logCalls[0].kind, "export")
            compare(a.terminal.logCalls.length, 0)
            verify(!b.terminal.logging)
            b.terminal.logError = "磁盘写入失败"
            verify(item(b, "ssh-log-status").visible)
            verify(item(b, "ssh-log-status").text.indexOf("磁盘写入失败") >= 0)
            verify(!item(a, "ssh-log-status").visible)
            openSessionMenu(a, "ssh-log-menu"); wait(20)
            const overlay = scene.Window.window.contentItem
            verify(item(overlay, "ssh-session-log-start-" + a.sessionId).enabled)
            verify(!item(overlay, "ssh-session-log-stop-" + a.sessionId).enabled)
            verify(item(overlay, "ssh-session-log-export-" + a.sessionId).enabled)
            keyClick(Qt.Key_Escape)
        }
        function editCommand(session, index) {
            session.commandManagementExpanded = true
            tryVerify(() => visualItem(session, "ssh-command-edit-" + index) !== null)
            const edit = visualItem(session, "ssh-command-edit-" + index)
            verify(edit.enabled && edit.visible)
            verify(waitForPolish(session))
            verify(waitForRendering(edit))
            verify(!edit.contentItem.truncated)
            mouseClick(edit)
            compare(item(session, "ssh-command-add").text, "保存修改")
            verify(waitForPolish(session))
            verify(waitForRendering(item(session, "ssh-command-add")))
        }
        function test_quickCommandEditOfflineRenamesInPlaceAndPersists() {
            make()
            const a = page.activeSession
            a.commandManagementExpanded = true
            verify(waitForPolish(a))
            verify(waitForRendering(item(a, "ssh-command-add")))
            item(a, "ssh-command-name").text = "原命令"
            item(a, "ssh-command-content").text = "echo old"
            mouseClick(item(a, "ssh-command-add"))
            compare(a.customCommands.length, 1)
            compare(a.customCommands[0].name, "原命令")
            a.addCustomCommand("其他命令", "pwd")
            editCommand(a, 0)
            compare(item(a, "ssh-command-name").text, "原命令")
            compare(item(a, "ssh-command-content").text, "echo old")
            compare(item(a, "ssh-command-add").text, "保存修改")
            verify(!item(a, "ssh-command-add").contentItem.truncated)
            item(a, "ssh-command-name").text = " 新名称 "
            item(a, "ssh-command-content").text = " echo updated "
            mouseClick(item(a, "ssh-command-add"))
            compare(a.customCommands.length, 2)
            compare(a.customCommands[0].name, "新名称")
            compare(a.customCommands[0].command, "echo updated")
            compare(a.customCommands[1].name, "其他命令")
            compare(a.terminal.sentCommands.length, 0)
            compare(item(a, "ssh-command-add").text, "添加")
            compare(item(a, "ssh-command-name").text, "")
            const saved = JSON.parse(testBackend.settings["liveLog/customCommands"])
            compare(saved[0].command, "echo updated")
            page.destroy(); page = null; wait(40); make()
            compare(page.activeSession.customCommands.length, 2)
            compare(page.activeSession.customCommands[0].name, "新名称")
            compare(page.activeSession.customCommands[0].command, "echo updated")
        }
        function test_quickCommandEditSharesContentAndKeepsSendBehavior() {
            make()
            const a = page.activeSession
            a.addCustomCommand("共用命令", "echo old")
            const b = page.addSession(); connected(b)
            editCommand(b, 0)
            item(b, "ssh-command-content").text = "echo new"
            item(b, "ssh-command-content").forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(a.customCommands[0].command, "echo new")
            compare(b.terminal.sentCommands.length, 0)
            compare(item(b, "ssh-command-content").text, "")
            b.commandManagementExpanded = false; wait(20)
            mouseClick(visualItem(b, "ssh-command-run-0"))
            compare(b.terminal.sentCommands.length, 1)
            compare(b.terminal.sentCommands[0], "echo new")
            compare(a.terminal.sentCommands.length, 0)
            verify(page.detachSession(b)); wait(30)
            editCommand(b, 0)
            item(b, "ssh-command-content").text = "echo detached"
            mouseClick(item(b, "ssh-command-add"))
            compare(a.customCommands[0].command, "echo detached")
            compare(b.terminal.sentCommands.length, 1)
        }
        function test_quickCommandEditCancelAndInvalidDraftKeepOriginal() {
            make()
            const a = page.activeSession
            a.addCustomCommand("原命令", "echo old")
            a.addCustomCommand("已占用", "pwd")
            const saved = testBackend.settings["liveLog/customCommands"]
            editCommand(a, 0)
            item(a, "ssh-command-name").text = "已占用"
            item(a, "ssh-command-content").text = "echo changed"
            mouseClick(item(a, "ssh-command-add"))
            verify(item(a, "ssh-command-error").visible)
            compare(a.customCommands[0].name, "原命令")
            compare(a.customCommands[1].command, "pwd")
            compare(item(a, "ssh-command-content").text, "echo changed")
            item(a, "ssh-command-name").text = " "
            mouseClick(item(a, "ssh-command-add"))
            verify(item(a, "ssh-command-error").visible)
            compare(testBackend.settings["liveLog/customCommands"], saved)
            item(a, "ssh-command-name").text = "原命令"
            item(a, "ssh-command-content").text = " "
            mouseClick(item(a, "ssh-command-add"))
            compare(testBackend.settings["liveLog/customCommands"], saved)
            mouseClick(item(a, "ssh-command-cancel"))
            compare(item(a, "ssh-command-add").text, "添加")
            compare(item(a, "ssh-command-name").text, "")
            verify(!item(a, "ssh-command-error").visible)
            compare(a.customCommands[0].command, "echo old")
            editCommand(a, 0)
            item(a, "ssh-command-content").text = "echo failure"
            testBackend.failSave = true
            mouseClick(item(a, "ssh-command-add"))
            verify(item(a, "ssh-command-error").visible)
            compare(a.customCommands[0].command, "echo old")
            compare(item(a, "ssh-command-content").text, "echo failure")
            compare(testBackend.settings["liveLog/customCommands"], saved)
            testBackend.failSave = false
            mouseClick(item(a, "ssh-command-add"))
            compare(a.customCommands[0].command, "echo failure")
        }
        function test_quickCommandRemovedInOtherSessionCannotBeRecreatedByStaleEdit() {
            make()
            const a = page.activeSession
            a.addCustomCommand("原命令", "echo old")
            const b = page.addSession()
            editCommand(b, 0)
            a.removeCustomCommandByName("原命令")
            item(b, "ssh-command-name").text = "新名称"
            mouseClick(item(b, "ssh-command-add"))
            compare(a.customCommands.length, 0)
            verify(item(b, "ssh-command-error").visible)
            compare(item(b, "ssh-command-name").text, "新名称")
        }
        function test_quickCommandFoldersCreateExpandEditAndPersist() {
            make()
            const a = page.activeSession
            a.addCustomCommand("查看日志", "tail -f app.log")
            a.beginCreateCommandFolder()
            item(a, "ssh-folder-name").text = "运维"
            mouseClick(item(a, "ssh-folder-save")); wait(30)
            compare(a.commandFolders.length, 1)
            compare(a.commandFolders[0].name, "运维")
            compare(a.rootCommands.length, 1)
            verify(a.moveCustomCommandToFolder("查看日志", "运维"))
            compare(a.rootCommands.length, 0)

            const folder = visualItem(a, "ssh-command-folder-0")
            verify(folder !== null && waitForRendering(folder))
            mouseClick(folder); wait(30)
            const popup = a.folderPopup
            verify(popup.visible)
            compare(a.openFolderCommands.length, 1)
            mouseClick(visualItem(popup.contentItem, "ssh-folder-close-current")); wait(100)
            verify(!popup.visible)

            a.beginAddCustomCommand("运维")
            item(a, "ssh-command-name").text = "磁盘占用"
            item(a, "ssh-command-content").text = "df -h"
            mouseClick(item(a, "ssh-command-add")); wait(30)
            compare(a.openFolderCommands.length, 0)
            compare(a.customCommands.filter(entry => entry.folder === "运维").length, 2)

            mouseClick(folder); wait(30)
            let run = null, edit = null, remove = null
            tryVerify(() => {
                run = visualItem(popup.contentItem, "ssh-folder-command-run-0")
                edit = visualItem(popup.contentItem, "ssh-folder-command-edit-0")
                remove = visualItem(popup.contentItem, "ssh-folder-command-delete-0")
                return run !== null && edit !== null && remove !== null
            })
            compare(run.height, edit.height)
            compare(edit.width, edit.height)
            compare(remove.width, remove.height)
            compare(run.height, remove.height)
            mouseClick(edit); wait(30)
            compare(a.customCmdFolder, "运维")
            verify(a.commandManagementExpanded)
            compare(item(a, "ssh-command-folder").currentIndex, 1)
            item(a, "ssh-command-content").text = "tail -n 100 app.log"
            mouseClick(item(a, "ssh-command-add")); wait(30)
            compare(a.customCommands.find(entry => entry.name === "查看日志").command, "tail -n 100 app.log")

            const b = page.addSession(); wait(20)
            compare(b.commandFolders.length, 1)
            compare(b.customCommands.filter(entry => entry.folder === "运维").length, 2)
            const saved = JSON.parse(testBackend.settings["liveLog/customCommands"])
            compare(saved.filter(entry => entry.type === "folder").length, 1)
            compare(saved.filter(entry => entry.folder === "运维").length, 2)
            verify(page.saveWorkspace())
            page.destroy(); page = null; wait(40); make()
            compare(page.activeSession.commandFolders.length, 1)
            compare(page.activeSession.customCommands.filter(entry => entry.folder === "运维").length, 2)
        }
        function test_quickCommandFolderRenameDeleteAndSaveFailureRollback() {
            make()
            const a = page.activeSession
            a.addCustomCommand("查看日志", "tail -f app.log")
            a.beginCreateCommandFolder()
            item(a, "ssh-folder-name").text = "运维"
            mouseClick(item(a, "ssh-folder-save")); wait(30)
            verify(a.moveCustomCommandToFolder("查看日志", "运维"))

            a.editCommandFolder("运维")
            item(a, "ssh-folder-name").text = "日常运维"
            mouseClick(item(a, "ssh-folder-save")); wait(30)
            compare(a.commandFolders[0].name, "日常运维")
            compare(a.customCommands.find(entry => entry.name === "查看日志").folder, "日常运维")

            testBackend.failSave = true
            verify(!a.moveCustomCommandToFolder("查看日志", ""))
            compare(a.customCommands.find(entry => entry.name === "查看日志").folder, "日常运维")
            verify(item(a, "ssh-command-error").visible)
            testBackend.failSave = false

            a.removeCommandFolderByName("日常运维")
            compare(a.commandFolders.length, 0)
            compare(a.rootCommands.length, 1)
            compare(a.rootCommands[0].command, "tail -f app.log")
        }
        function test_quickCommandFolderScrollsManyCommands() {
            make()
            const a = page.activeSession
            a.beginCreateCommandFolder()
            item(a, "ssh-folder-name").text = "批量"
            mouseClick(item(a, "ssh-folder-save")); wait(30)
            for (let i = 0; i < 10; ++i)
                verify(a.addCustomCommand("命令" + i, "echo " + i, undefined, "批量"))
            mouseClick(visualItem(a, "ssh-command-folder-0")); wait(100)
            let list = null
            tryVerify(() => {
                list = visualItem(a.folderPopup.contentItem, "ssh-folder-command-list")
                return list !== null && list.contentHeight > list.height
            })
            list.contentY = list.contentHeight - list.height; wait(30)
            verify(list.contentY > 0)
            compare(a.openFolderCommands.length, 10)
        }
        function test_quickCommandFolderDropTargetMovesCommand() {
            make()
            const a = page.activeSession
            a.addCustomCommand("拖拽命令", "pwd")
            a.addCustomCommand("独立命令", "ls")
            a.beginCreateCommandFolder()
            item(a, "ssh-folder-name").text = "网络"
            mouseClick(item(a, "ssh-folder-save")); wait(100)
            connected(a)
            const run = visualItem(a, "ssh-command-run-0")
            const folder = visualItem(a, "ssh-command-folder-0")
            const other = visualItem(a, "ssh-command-run-1")
            verify(run !== null)
            verify(folder !== null)
            verify(other !== null)
            verify(run.visible && run.width > 0)
            verify(folder.visible && folder.width > 0)
            const runX = run.x, otherX = other.x
            compare(Object.keys(a.commandFolderTargets).length, 1)
            verify(folder.mapToItem(a, 0, 0).x < run.mapToItem(a, 0, 0).x)
            const target = folder.mapToItem(run, folder.width / 2, folder.height / 2)
            mousePress(run, run.width / 2, run.height / 2)
            mouseMove(run, run.width / 2 + 30, run.height / 2, 60)
            tryVerify(() => a.draggedCommandName === "拖拽命令")
            compare(run.x, runX)
            compare(other.x, otherX)
            mouseMove(run, target.x, target.y, 80)
            tryVerify(() => a.commandDropFolder === "网络")
            mouseRelease(run, target.x, target.y); wait(50)
            const command = a.customCommands.find(entry => entry.name === "拖拽命令")
            compare(command.folder, "网络")
            compare(a.rootCommands.length, 1)
            compare(a.rootCommands[0].name, "独立命令")
            verify(folder.mapToItem(a, 0, 0).x < visualItem(a, "ssh-command-run-0").mapToItem(a, 0, 0).x)
            compare(a.terminal.sentCommands.length, 0)
        }
        function test_folderChipAndCommandHaveClearGap() {
            make()
            const a = page.activeSession
            a.beginCreateCommandFolder()
            item(a, "ssh-folder-name").text = "444"
            mouseClick(item(a, "ssh-folder-save")); wait(30)
            a.addCustomCommand("333", "echo 333"); wait(50)
            const folder = visualItem(a, "ssh-command-folder-0")
            const command = visualItem(a, "ssh-command-run-0")
            const folderRight = folder.mapToItem(a, 0, 0).x + folder.width
            const commandLeft = command.mapToItem(a, 0, 0).x
            verify(commandLeft - folderRight >= 6)
        }
        function test_quickCommandReorderRootCommandsByPointerDrag() {
            make()
            const a = page.activeSession
            a.addCustomCommand("一", "echo 1")
            a.addCustomCommand("二", "echo 2")
            a.addCustomCommand("三", "echo 3")
            connected(a)
            wait(50)
            const first = visualItem(a, "ssh-command-run-0")
            const third = visualItem(a, "ssh-command-run-2")
            verify(first !== null && third !== null)
            const firstX = first.x
            const target = third.mapToItem(first, third.width * 0.8, third.height / 2)
            mousePress(first, first.width / 2, first.height / 2)
            mouseMove(first, first.width / 2 + 30, first.height / 2, 60)
            tryVerify(() => a.draggedCommandName === "一")
            compare(first.x, firstX)
            mouseMove(first, target.x, target.y, 80)
            tryVerify(() => a.commandReorderTarget === "三" && a.commandReorderAfter)
            verify(visualItem(a, "ssh-command-reorder-indicator").visible)
            mouseRelease(first, target.x, target.y); wait(50)
            compare(a.rootCommands.length, 3)
            compare(a.rootCommands[0].name, "二")
            compare(a.rootCommands[1].name, "三")
            compare(a.rootCommands[2].name, "一")
            compare(a.terminal.sentCommands.length, 0)
            const saved = JSON.parse(testBackend.settings["liveLog/customCommands"])
            compare(saved.map(entry => entry.name).join(","), "二,三,一")
        }
        function test_quickCommandReorderFoldersByPointerDrag() {
            make()
            const a = page.activeSession
            for (const name of ["甲", "乙", "丙"]) {
                a.beginCreateCommandFolder()
                item(a, "ssh-folder-name").text = name
                mouseClick(item(a, "ssh-folder-save")); wait(20)
            }
            a.addCustomCommand("独立命令", "pwd")
            wait(50)
            const first = visualItem(a, "ssh-command-folder-0")
            const last = visualItem(a, "ssh-command-folder-2")
            verify(first !== null && last !== null)
            const firstX = first.x
            const target = last.mapToItem(first, last.width * 0.8, last.height / 2)
            mousePress(first, first.width / 2, first.height / 2)
            mouseMove(first, first.width / 2 + 30, first.height / 2, 60)
            tryVerify(() => a.draggedFolderName === "甲")
            compare(first.x, firstX)
            verify(!a.folderPopup.visible)
            mouseMove(first, target.x, target.y, 80)
            tryVerify(() => a.commandReorderTarget === "丙" && a.commandReorderAfter)
            verify(visualItem(a, "ssh-command-reorder-indicator").visible)
            mouseRelease(first, target.x, target.y); wait(50)
            compare(a.commandFolders.map(entry => entry.name).join(","), "乙,丙,甲")
            compare(a.rootCommands.length, 1)
            compare(a.terminal.sentCommands.length, 0)
            const saved = JSON.parse(testBackend.settings["liveLog/customCommands"])
            compare(saved.filter(entry => entry.type === "folder").map(entry => entry.name).join(","), "乙,丙,甲")
        }
        function test_switchingKeepsConnectionsLogsDraftsAndCommandsIndependent() {
            make()
            const a = page.activeSession
            connected(a)
            item(a, "ssh-host").text = "a.example"
            item(a, "ssh-password-draft").text = "draft-only-a"
            a.terminal.liveLine = "device-a$ echoed-a"
            a.terminal.cursorColumn = 18
            a.connectionConfigExpanded = false; a.autoScroll = false
            a.terminal.appendTestLines(["A first", "A second"], 10)
            const aDisconnects = a.terminal.disconnectCalls
            mouseClick(item(page, "ssh-new-session")); wait(40)
            const b = page.activeSession
            verify(a !== b); verify(!a.visible); verify(b.visible)
            compare(item(b, "ssh-host").text, ""); verify(!b.connectionConfigExpanded)
            verify(!a.terminal.persistSettings && !b.terminal.persistSettings)
            connected(b)
            item(b, "ssh-host").text = "b.example"
            b.terminal.appendTestLines(["B only"], 10)
            a.terminal.appendTestLines(["A background"], 10)
            b.addCustomCommand("共用命令", "echo synthetic")
            compare(a.customCommands.length, 1)
            b.sendQuickCommand(b.customCommands[0].command)
            compare(b.terminal.sentCommands.length, 1); compare(a.terminal.sentCommands.length, 0)
            select(0)
            compare(a.terminal.disconnectCalls, aDisconnects); verify(a.terminal.connected)
            compare(a.terminal.currentLineCount, 3); compare(b.terminal.currentLineCount, 1)
            compare(item(a, "ssh-host").text, "a.example")
            compare(item(a, "ssh-password-draft").text, "draft-only-a")
            compare(a.terminal.liveLine, "device-a$ echoed-a")
            verify(!a.connectionConfigExpanded); verify(!a.autoScroll)
            a.terminal.clearBuffer(); compare(b.terminal.currentLineCount, 1)
            page.visible = false; page.visible = true; wait(20)
            compare(a.terminal.disconnectCalls, aDisconnects); verify(b.terminal.connected)
        }
        function test_parserWindowsAndModelsAreIndependent() {
            make()
            const a = page.activeSession
            a.terminal.appendTestLines(["RX 68 04 07 00 00 00"], 10)
            a.selectLineAndExtractHex(0); a.parseSelectedHexWithProtocol(3); wait(30)
            verify(a.parseWindowVisible); verify(a.parseSessionId >= 4)
            const aModel = a.parseResultModel
            compare(aModel.get(0).value.replace(/ /g, ""), "680407000000")
            page.addSession(); wait(30)
            const b = page.activeSession
            b.terminal.appendTestLines(["RX 05 01 02 40 00 02 00 00"], 10)
            b.selectLineAndExtractHex(0); b.parseSelectedHexWithProtocol(1); wait(30)
            verify(b.parseWindowVisible); verify(a.parseSessionId !== b.parseSessionId)
            compare(b.parseResultModel.get(0).value.replace(/ /g, ""), "0501024000020000")
            compare(aModel.get(0).value.replace(/ /g, ""), "680407000000")
            compare(testBackend.protocolType, 7)
            const id = a.parseSessionId
            verify(page.removeSession(a)); wait(30)
            verify(testBackend.parseModel(id) === null); verify(b.parseWindowVisible)
            compare(page.activeSession, b)
        }
        function test_parserShowsOriginalFrameAndHighlightsOnlyItsOwnBytes() {
            make()
            const a = page.activeSession
            a.terminal.appendTestLines(["RX 05 01 02 40 00 02 00 00"], 10)
            a.selectLineAndExtractHex(0); a.parseSelectedHexWithProtocol(0); wait(30)
            const frameA = item(a, "ssh-parse-frame")
            compare(frameA.text, "05 01 02 40 00 02 00 00")
            verify(frameA.readOnly && frameA.persistentSelection)
            a.parseResultModel.setProperty(0, "offset", 3)
            a.parseResultModel.setProperty(0, "length", 4)
            const listA = item(a, "ssh-parse-results")
            tryVerify(() => listA.itemAtIndex(0) !== null)
            mouseClick(listA.itemAtIndex(0)); wait(20)
            compare(frameA.selectedText, "40 00 02 00")
            compare(a.parseResultModel.count, 1)
            const b = page.addSession(); wait(20)
            b.terminal.appendTestLines(["RX 68 04 07 00 00 00"], 10)
            b.selectLineAndExtractHex(0); b.parseSelectedHexWithProtocol(0); wait(30)
            const frameB = item(b, "ssh-parse-frame")
            const listB = item(b, "ssh-parse-results")
            tryVerify(() => listB.itemAtIndex(0) !== null)
            mouseClick(listB.itemAtIndex(0)); wait(20)
            compare(frameB.selectedText, "68"); compare(frameA.selectedText, "40 00 02 00")
            testBackend.highlightRange(3, 5, 1)
            compare(frameB.selectedText, "68"); compare(frameA.selectedText, "40 00 02 00")
            b.parseSelectedHexWithProtocol(0); wait(20)
            compare(frameB.selectedText, "")
            a.terminal.appendTestLines(["RX AABBCCDDEEFF"], 10)
            a.selectLineAndExtractHex(1); a.parseSelectedHexWithProtocol(0); wait(20)
            compare(frameA.text, "AA BB CC DD EE FF"); compare(frameA.selectedText, "")
            compare(frameB.text, "68 04 07 00 00 00")
        }
        function test_terminalKeysAreSentImmediatelyAndOnlyRemoteEchoIsDisplayed() {
            make()
            const a = page.activeSession; connected(a)
            a.terminal.liveLine = "test$ "; a.terminal.cursorColumn = 6
            const input = a.terminalInput
            page.forceActiveFocus()
            mouseClick(item(a, "ssh-terminal-pane"), 40, 100); wait(30)
            verify(input.activeFocus)
            verify(findChild(a, "ssh-command-send") === null)
            verify(findChild(a, "ssh-command-input") === null)
            keyClick(Qt.Key_X)
            compare(a.terminal.rawInput.join(""), "x")
            compare(input.text, ""); compare(a.terminal.liveLine, "test$ ")
            keyClick(Qt.Key_Backspace); keyClick(Qt.Key_Left); keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Tab); keyClick(Qt.Key_Return)
            compare(a.terminal.rawInput.join(""), "x\x7f\x1b[D\x1b[A\t\r")
            const control = Qt.platform.os === "osx" ? Qt.MetaModifier : Qt.ControlModifier
            keyClick(Qt.Key_C, control)
            compare(a.terminal.rawInput[a.terminal.rawInput.length - 1], "\x03")
            a.terminal.liveLine = "test$ remote-echo"
            wait(20); compare(input.text, "")
            const b = page.addSession(); connected(b); wait(30)
            b.terminalInput.forceActiveFocus(); keyClick(Qt.Key_Y)
            compare(b.terminal.rawInput.join(""), "y")
            compare(a.terminal.rawInput.join(""), "x\x7f\x1b[D\x1b[A\t\r\x03")
            b.terminal.clipboardContents = "粘贴内容\nsecond-line"
            b.pasteTerminal()
            compare(b.terminal.rawInput[1], "粘贴内容\nsecond-line")
            compare(b.terminalInput.text, "")
        }
        function test_terminalPasteShortcutPreservesFullClipboard_data() {
            return [
                {tag: "multiline", value: "粘贴内容\nsecond-line\r\n末行😀"},
                {tag: "long", value: "begin-" + "中文😀abcdef".repeat(6000) + "-end"},
                {tag: "alternate", value: "中文\nsecond-line", alternate: true},
                {tag: "alternate-long", value: "中文\n😀".repeat(8000), alternate: true}
            ]
        }
        function test_terminalRightClickWithoutSelectionShowsCommonActions_data() {
            return [{tag: "untouched", selectLine: false}, {tag: "previous-line-click", selectLine: true}]
        }
        function test_terminalRightClickWithoutSelectionShowsCommonActions(data) {
            make()
            const a = page.activeSession; connected(a)
            a.terminal.appendTestLines(["RX 68 04 07 00 00 00"], 10); wait(30)
            const row = item(a, "ssh-log-line-0")
            if (data.selectLine) {
                mouseClick(row, 30, row.height / 2); wait(20)
                compare(a.parseableHexText, "68 04 07 00 00 00")
            }
            verify(!a.hasTextSelection)
            const selectedId = a.selectedLineId
            mouseClick(row, 30, row.height / 2, Qt.RightButton); wait(20)
            verify(item(a, "ssh-terminal-actions-menu").visible)
            verify(!item(a, "ssh-terminal-parse-menu").visible)
            verify(!a.hasTextSelection); compare(a.selectedLineId, selectedId)
            const text = "中文\r\n" + "abcdef😀".repeat(6000)
            clipboard(text); a.terminal.clipboardContents = text
            mouseClick(item(a, "ssh-terminal-paste")); wait(20)
            compare(a.terminal.rawInput.length, 1); compare(a.terminal.rawInput[0], text)
            verify(a.terminalInput.activeFocus); compare(a.terminalInput.text, "")
            mouseClick(a.terminalInput, 10, a.terminalInput.height / 2, Qt.RightButton); wait(20)
            verify(item(a, "ssh-terminal-actions-menu").visible)
            keyClick(Qt.Key_Escape)
        }
        function test_terminalCommonActionsSelectCopyAndClearOnlyCurrentBuffer() {
            make()
            const a = page.activeSession
            a.terminal.appendTestLines(["first", "中文 second"], 10)
            a.terminal.liveLine = "test$ tail"
            const b = page.addSession(); b.terminal.appendTestLines(["other"], 10); select(0); wait(20)
            const area = item(a, "ssh-text-selection-area")
            mouseClick(area, 40, 300, Qt.RightButton); wait(20)
            mouseClick(item(a, "ssh-terminal-select-all")); wait(20)
            verify(a.hasTextSelection); verify(!a.autoScroll)
            a.copyTextSelection(); compare(clipboard(), "first\n中文 second\ntest$ tail")
            verify(!b.hasTextSelection)
            mouseClick(area, 40, 300, Qt.RightButton); wait(20)
            verify(item(a, "ssh-terminal-parse-menu").visible)
            verify(!item(a, "ssh-terminal-actions-menu").visible)
            verify(item(a, "ssh-copy-selection").enabled)
            verify(!item(a, "ssh-parse-auto").enabled)
            keyClick(Qt.Key_Escape); a.clearTextSelection()
            mouseClick(area, 40, 300, Qt.RightButton); wait(20)
            mouseClick(item(a, "ssh-terminal-copy-all")); wait(20)
            compare(a.terminal.copyBufferCalls, 1); compare(b.terminal.copyBufferCalls, 0)
            mouseClick(area, 40, 300, Qt.RightButton); wait(20)
            mouseClick(item(a, "ssh-terminal-clear")); wait(20)
            compare(a.terminal.currentLineCount, 0); compare(b.terminal.currentLineCount, 1)
        }
        function test_terminalCommonActionsDisableUnavailableOperations() {
            make()
            const a = page.activeSession
            const area = item(a, "ssh-text-selection-area")
            mouseClick(area, 40, area.height - 40, Qt.RightButton); wait(20)
            verify(item(a, "ssh-terminal-actions-menu").visible)
            for (const name of ["paste", "select-all", "copy-all", "clear"])
                verify(!item(a, "ssh-terminal-" + name).enabled, name)
            keyClick(Qt.Key_Escape); connected(a); a.terminal.receptionPaused = true
            mouseClick(area, 40, area.height - 40, Qt.RightButton); wait(20)
            verify(!item(a, "ssh-terminal-paste").enabled)
            keyClick(Qt.Key_Escape); a.terminal.receptionPaused = false
            mouseClick(area, 40, area.height - 40, Qt.RightButton); wait(20)
            verify(item(a, "ssh-terminal-paste").enabled)
            keyClick(Qt.Key_Escape)
        }
        function test_terminalPasteShortcutPreservesFullClipboard(data) {
            make()
            const a = page.activeSession; connected(a)
            const b = page.addSession(); connected(b); select(0)
            clipboard(data.value)
            a.terminal.clipboardContents = data.value
            a.terminalInput.forceActiveFocus(); wait(20)
            verify(a.terminalInput.activeFocus)
            keyClick(Qt.Key_V, data.alternate ? Qt.ControlModifier | Qt.ShiftModifier : Qt.ControlModifier)
            compare(a.terminal.rawInput.length, 1)
            compare(a.terminal.rawInput[0].length, data.value.length)
            compare(a.terminal.rawInput[0], data.value)
            compare(a.terminalInput.text, "")
            compare(b.terminal.rawInput.length, 0)
        }
        function test_terminalPasteKeepsConfigEditingAndRespectsConnectionState() {
            make()
            const a = page.activeSession; connected(a)
            const text = "pasted.example"
            clipboard(text); a.terminal.clipboardContents = text
            a.connectionConfigExpanded = true
            wait(40)
            const field = item(a, "ssh-session-name")
            field.text = ""; field.forceActiveFocus(); wait(20)
            verify(field.activeFocus)
            keyClick(Qt.Key_V, Qt.ControlModifier)
            compare(field.text, text); compare(a.terminal.rawInput.length, 0)
            field.text = ""; field.forceActiveFocus()
            keyClick(Qt.Key_V, Qt.ControlModifier | Qt.ShiftModifier)
            compare(a.terminal.rawInput.length, 0)
            a.terminal.receptionPaused = true
            a.terminalInput.forceActiveFocus(); wait(20)
            keyClick(Qt.Key_V, Qt.ControlModifier)
            a.pasteTerminal()
            compare(a.terminal.rawInput.length, 0)
            a.terminal.receptionPaused = false; a.terminal.connected = false
            a.pasteTerminal()
            compare(a.terminal.rawInput.length, 0)
        }
        function test_ctrlWheelZoomKeepsTextMetricsSelectionAndSessions() {
            make()
            const a = page.activeSession; connected(a)
            a.terminal.liveLine = "test$ "
            a.terminal.appendTestLines(["RX 68 04 07 00 00 00"], 10)
            a.selectLineAndExtractHex(0); wait(30)
            const view = item(a, "ssh-log-list")
            const control = Qt.platform.os === "osx" ? Qt.MetaModifier : Qt.ControlModifier
            const row = item(a, "ssh-log-line-0")
            const oldHeight = row.height; const oldWidth = a.charWidth
            const selected = item(a, "ssh-log-text").selectedText
            const caretX = view.footerItem.caretX
            mouseWheel(view, 40, 20, 0, 120, Qt.NoButton, control)
            tryCompare(a, "terminalFontSize", 14)
            compare(item(a, "ssh-log-text").font.pixelSize, 14)
            compare(item(a, "ssh-terminal-live-line").font.pixelSize, 14)
            compare(a.terminalInput.font.pixelSize, 14)
            compare(item(a, "ssh-log-text").selectedText, selected)
            verify(row.height > oldHeight); verify(a.charWidth > oldWidth)
            verify(view.footerItem.caretX > caretX)
            mouseWheel(view, 40, 20, 0, -120, Qt.NoButton, control)
            tryCompare(a, "terminalFontSize", 13)
            mouseWheel(a.terminalInput, 1, 1, 0, 120, Qt.NoButton, control)
            tryCompare(a, "terminalFontSize", 14)
            a.terminalFontSize = 32
            mouseWheel(view, 40, 20, 0, 120, Qt.NoButton, control)
            compare(a.terminalFontSize, 32)
            a.terminalFontSize = 8
            mouseWheel(view, 40, 20, 0, -120, Qt.NoButton, control)
            compare(a.terminalFontSize, 8)
            a.terminalFontSize = 18
            const b = page.addSession(); wait(30)
            compare(b.terminalFontSize, 13)
            select(0); compare(a.terminalFontSize, 18)
            compare(a.terminal.rawInput.length, 0)
        }
        function test_ctrlWheelPreservesViewportAndPlainWheelScrolls() {
            make()
            const a = page.activeSession
            a.connectionConfigExpanded = false
            let lines = []
            for (let i = 0; i < 200; ++i) lines.push("Log row " + i)
            a.terminal.appendTestLines(lines, 200); wait(40)
            const view = item(a, "ssh-log-list")
            const control = Qt.platform.os === "osx" ? Qt.MetaModifier : Qt.ControlModifier
            a.autoScroll = false
            view.positionViewAtIndex(80, ListView.Beginning); wait(30)
            const anchor = a.terminal.lineIdAt(view.indexAt(1, view.contentY + 1))
            mouseWheel(view, 40, 20, 0, 120, Qt.NoButton, control); wait(30)
            compare(a.terminalFontSize, 14)
            compare(a.terminal.lineIdAt(view.indexAt(1, view.contentY + 1)), anchor)
            const before = view.contentY
            mouseWheel(view, 40, 20, 0, -120, Qt.NoButton, Qt.NoModifier)
            tryVerify(function() { return view.contentY > before })
            compare(a.terminalFontSize, 14)
            view.cancelFlick()
            a.autoScroll = true; a.applyLogViewport(); wait(30)
            mouseWheel(view, 40, 20, 0, 120, Qt.NoButton, control); wait(30)
            verify(view.atYEnd)
        }
        function test_backgroundHostKeyWaitsForItsOwnTab() {
            make()
            const a = page.activeSession
            a.terminal.connectionState = 1
            page.addSession(); wait(30)
            a.terminal.requestKey("a-key", "SHA256:a-fingerprint")
            wait(30)
            compare(a.terminal.confirmations.length, 0); verify(a.terminal.hostKeyPending)
            verify(a.pendingHostKey !== null)
            select(0); verify(item(a, "ssh-host-key-dialog").visible)
            select(1); compare(a.terminal.confirmations.length, 0); verify(a.terminal.hostKeyPending)
            select(0); mouseClick(item(a, "ssh-host-key-accept"))
            compare(a.terminal.confirmations.length, 1); verify(a.terminal.confirmations[0].accept)
        }
        function test_closeConfirmsOnlyTheTargetSession() {
            make()
            const a = page.activeSession; connected(a)
            page.addSession(); wait(30)
            const b = page.activeSession; connected(b)
            const disconnects = b.terminal.disconnectCalls
            page.requestCloseSession(a); wait(20)
            verify(item(page, "ssh-close-confirmation").visible)
            mouseClick(item(page, "ssh-close-cancel")); compare(page.sessions.length, 2)
            verify(a.terminal.connected && b.terminal.connected)
            page.requestCloseSession(a); wait(20)
            mouseClick(item(page, "ssh-close-accept")); wait(40)
            compare(page.sessions.length, 1); compare(page.activeSession, b)
            compare(b.terminal.disconnectCalls, disconnects); verify(b.terminal.connected)
        }
        function test_configurationRestoresPlaintextPasswordsWithoutConnecting() {
            make()
            const a = page.activeSession
            item(a, "ssh-host").text = "saved-a.example"
            item(a, "ssh-port").text = "2222"
            item(a, "ssh-password-draft").text = "saved-password-a"
            a.connectionConfigExpanded = false
            page.addSession(); wait(30)
            const b = page.activeSession
            item(b, "ssh-host").text = "saved-b.example"
            item(b, "ssh-buffer-lines").text = "12000"
            b.terminal.autoReconnectEnabled = false
            verify(page.saveWorkspace())
            const saved = testBackend.settings["next/sshMonitor/workspace"]
            verify(saved.indexOf("saved-password-a") >= 0)
            page.destroy(); page = null; wait(40); make()
            compare(page.sessions.length, 2); compare(page.currentIndex, 1)
            compare(item(page.sessions[0], "ssh-host").text, "saved-a.example")
            compare(item(page.sessions[0], "ssh-port").text, "2222")
            compare(item(page.sessions[0], "ssh-password-draft").text, "saved-password-a")
            verify(!page.sessions[0].connectionConfigExpanded)
            compare(item(page.sessions[1], "ssh-host").text, "saved-b.example")
            compare(item(page.sessions[1], "ssh-buffer-lines").text, "12000")
            verify(!page.sessions[1].terminal.autoReconnectEnabled)
            for (const session of page.sessions) {
                compare(session.terminal.connectCalls, 0); verify(!session.secureCredentials)
            }
        }
        function test_corruptAndUnwritableSettingsArePreserved() {
            testBackend.settings["next/sshMonitor/workspace"] = "broken-workspace"
            testBackend.failSave = true
            make()
            verify(!page.writable); verify(!page.saveWorkspace())
            compare(testBackend.settings["next/sshMonitor/workspace"], "broken-workspace")
            verify(page.workspaceError !== "")
        }
        function test_connectionEditorHasNoFollowSwitchAndIgnoresLegacySetting() {
            make()
            const a = page.activeSession
            const config = page.blankConfiguration("设备 A")
            config.host = "follow.example"; config.port = "2222"
            config.username = "operator"; config.password = "follow-password"
            config.autoFollowEnabled = false
            a.restoreConfiguration(config)
            connected(a); wait(40)
            compare(page.savedConnections.length, 1)
            verify(a.autoScroll)
            verify(a.connectionConfiguration().autoFollowEnabled === undefined)
            verify(page.savedConnections[0].autoFollowEnabled === undefined)
            verify(!item(a, "ssh-connection-panel").visible)
            verify(!item(a, "ssh-connection-scroll").visible)

            const tab = item(page, "ssh-session-row-" + a.sessionId)
            mouseClick(tab, 25, 13, Qt.RightButton); wait(30)
            verify(visualItem(a.group.menu.contentItem, "ssh-pause") === null)
            const action = visualItem(a.group.menu.contentItem, "ssh-session-config-" + a.sessionId)
            verify(action !== null); compare(action.text, "连接参数…")
            mouseClick(action); wait(40)
            const editor = item(page, "ssh-connection-editor")
            verify(editor.opened)
            verify(!item(page, "ssh-editor-host").enabled)
            verify(findChild(page, "ssh-editor-auto-follow") === null)
            item(page, "ssh-editor-copy-on-select").checked = true
            testBackend.failSave = true
            mouseClick(item(page, "ssh-editor-save")); wait(30)
            verify(editor.opened && item(page, "ssh-editor-error").visible)
            verify(!a.copyOnSelect && !page.savedConnections[0].copyOnSelect)
            testBackend.failSave = false
            mouseClick(item(page, "ssh-editor-save")); wait(30)
            verify(!editor.opened && a.copyOnSelect)
            verify(page.savedConnections[0].copyOnSelect)
            verify(page.saveWorkspace())
            const persisted = JSON.parse(testBackend.settings["next/sshMonitor/workspace"])
            verify(persisted.sessions[0].config.autoFollowEnabled === undefined)
            verify(persisted.connections[0].autoFollowEnabled === undefined)
            page.destroy(); page = null; wait(40); make()
            verify(page.sessions[0].autoScroll)
            verify(page.sessions[0].autoFollowEnabled === undefined)
            verify(page.savedConnections[0].autoFollowEnabled === undefined)
        }
        function test_savedConnectionContextMenuEditsStoredParameters() {
            make()
            const config = page.blankConfiguration("采集网关")
            config.host = "old.example"; config.username = "old-user"
            config.password = "old-password"
            page.savedConnections = [config]
            verify(page.saveWorkspace())

            const row = item(page, "ssh-saved-row-0")
            mouseClick(row, 35, 20, Qt.RightButton); wait(30)
            const edit = item(scene.Window.window.contentItem, "ssh-saved-edit-0")
            verify(edit !== null); mouseClick(edit); wait(40)
            const editor = item(page, "ssh-connection-editor")
            verify(editor.opened)
            item(page, "ssh-editor-host").text = "new.example"
            item(page, "ssh-editor-port").text = "2200"
            item(page, "ssh-editor-username").text = "operator"
            item(page, "ssh-editor-password").text = "new-password"
            item(page, "ssh-editor-copy-on-select").checked = true
            mouseClick(item(page, "ssh-editor-save")); wait(30)
            verify(!editor.opened)
            compare(page.savedConnections[0].host, "new.example")
            compare(page.savedConnections[0].port, "2200")
            compare(page.savedConnections[0].username, "operator")
            compare(page.savedConnections[0].password, "new-password")
            verify(page.savedConnections[0].copyOnSelect)

            const restored = page.connectSaved(page.savedConnections[0]); wait(30)
            compare(restored.terminal.host, "new.example")
            compare(restored.terminal.port, 2200)
            compare(restored.terminal.username, "operator")
            compare(restored.terminal.password, "new-password")
            verify(restored.autoScroll && restored.copyOnSelect)
        }
        function test_savedConnectionListKeepsAccountAndPasswordAfterClosingTabs() {
            make()
            const a = page.activeSession
            const config = page.blankConfiguration("测试设备 A")
            config.host = "saved-a.example"; config.port = "2222"
            config.username = "test-a"; config.password = "plain-password-a"
            a.restoreConfiguration(config)
            verify(!a.secureCredentials)
            verify(!item(a, "ssh-save-password").visible)
            connected(a); wait(30)
            compare(page.savedConnections.length, 1)
            compare(page.savedConnections[0].username, "test-a")
            compare(page.savedConnections[0].password, "plain-password-a")
            a.terminal.connected = false; a.terminal.connectionState = 5
            verify(page.removeSession(a)); wait(30)
            compare(page.sessions.length, 0); compare(page.savedConnections.length, 1)
            page.destroy(); page = null; wait(40); make()
            compare(page.savedConnections.length, 1); compare(page.sessions.length, 0)
            const restored = page.connectSaved(page.savedConnections[0]); wait(40)
            compare(restored.terminal.connectCalls, 1)
            compare(restored.terminal.host, "saved-a.example"); compare(restored.terminal.port, 2222)
            compare(restored.terminal.username, "test-a"); compare(restored.terminal.password, "plain-password-a")
            compare(item(restored, "ssh-password-draft").text, "plain-password-a")
            restored.terminal.connectionState = 5
            item(restored, "ssh-password-draft").text = "updated-password-a"
            restored.connectAction(); connected(restored); wait(30)
            compare(page.savedConnections.length, 1)
            compare(page.savedConnections[0].password, "updated-password-a")
            page.removeSaved(0); compare(page.savedConnections.length, 0)
        }
        function test_failedSaveKeepsTheSessionAndEmptyWorkspaceRestores() {
            make()
            const a = page.activeSession
            testBackend.failSave = true
            verify(!page.removeSession(a)); compare(page.activeSession, a); compare(page.sessions.length, 1)
            testBackend.failSave = false
            verify(page.removeSession(a)); compare(page.sessions.length, 0); compare(page.currentIndex, -1)
            page.destroy(); page = null; wait(40); make()
            compare(page.sessions.length, 0)
            mouseClick(item(page, "ssh-new-session")); compare(page.sessions.length, 1)
        }
    }
}
