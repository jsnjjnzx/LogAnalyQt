import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import LogAnalyQt.Next as Next
Item {
    id: scene
    width: 1280; height: 900
    Next.GlassTheme { id: testTheme }
    Window { id: ownerWindow; visible: false; width: 1280; height: 900 }
    QtObject {
        id: mockBackend
        property var popupResultModel: []
        property int protocolType: 0
        signal highlightRange(int start, int end, int modelId)
        function getUiSetting(key,fallback) { return fallback }
        function setUiSetting(key,value) { return true }
    }
    Component { id: syncMock; MockSyncManager {} }
    Component { id: nextSync; Next.ProtocolSyncDialog { theme: testTheme } }
    Component { id: nextSsh; Next.LiveLogDialog { theme: testTheme; backend: mockBackend; useSystemCredentials: true } }
    TestCase {
        name: "RemoteSecurityViews"
        when: windowShown
        property var page
        property var manager
        function init() { failOnWarning(/Binding loop|TypeError|ReferenceError|Unable to assign/); page = null; manager = null }
        function cleanup() { if (page) { page.close(); page.destroy(); page = null }; if (manager) { manager.destroy(); manager = null }; wait(30) }
        function make(component, extra) {
            const properties = Object.assign({width:1180,height:800,ownerWindow:ownerWindow},extra || {})
            page = component.createObject(scene,properties); verify(page !== null); page.visible = true; wait(40)
        }
        function item(name) { const found = findChild(page,name); verify(found !== null,name); return found }
        function visualItem(node,name) {
            if (node.objectName === name) return node
            if (node.children) for (let i=0;i<node.children.length;++i) { const found = visualItem(node.children[i],name); if (found) return found }
            return null
        }
        function edit(name,value) { const field = item(name); field.text = value; field.textEdited() }
        function openSync() { manager = syncMock.createObject(scene); make(nextSync,{syncManager:manager}) }
        function connectSync() {
            mouseClick(item("protocol-sync-connect")); compare(manager.connectionCalls,1); verify(!page.remoteReady)
            manager.finishConnection(true); compare(manager.statusCalls,1); manager.finishStatus(true); wait(20); verify(page.remoteReady)
        }
        function test_promptSyncSelection() {
            openSync()
            const prompts = syncMock.createObject(scene,{sharePath:"smb://test/share/prompts"})
            page.promptSyncManager = prompts
            verify(item("sync-content-selector").visible)
            page.selectContent(1)
            compare(page.activeManager,prompts); compare(item("protocol-sync-path").text,prompts.sharePath)
            page.startConnection(); compare(prompts.connectionCalls,1); compare(manager.connectionCalls,0)
            page.selectContent(0); verify(page.promptsSelected)
            prompts.finishConnection(true); prompts.finishStatus(true); wait(20)
            verify(page.remoteReady)
            page.requestSync(""); compare(prompts.syncCalls,1); compare(manager.syncCalls,0)
            prompts.busy = false; prompts.syncFinished({uploaded:["ai_698_framing.md"],failed:[],errors:[]}); wait(20)
            verify(item("protocol-sync-result-text").text.indexOf("ai_698_framing.md")>=0)
            item("protocol-sync-result").close()
            page.selectContent(0); compare(page.activeManager,manager); verify(!page.remoteReady)
            compare(item("protocol-sync-path").text,manager.sharePath)
            prompts.destroy()
        }
        function test_syncFinderAndPendingResults() {
            openSync(); verify(!item("protocol-sync-path").readOnly)
            verify(findChild(page,"protocol-sync-legacy-password") === null); verify(findChild(page,"protocol-sync-legacy-user") === null)
            verify(!item("protocol-sync-all").enabled); connectSync()
            mouseClick(item("protocol-sync-all")); compare(manager.syncCalls,1); compare(page.pendingAction,"sync")
            verify(!item("protocol-sync-result").visible); verify(!item("protocol-sync-all").enabled)
            manager.busy = false; manager.syncFinished({uploaded:["a.xml"],downloaded:[],skipped:[],failed:["b.xml"],errors:[{file:"b.xml",error:"远端拒绝写入"}]}); wait(20)
            verify(item("protocol-sync-result").visible); verify(page.statusIsError)
            const text = item("protocol-sync-result-text").text
            verify(text.indexOf("a.xml")>=0); verify(text.indexOf("b.xml")>=0); verify(text.indexOf("远端拒绝写入")>=0)
        }
        function test_syncCancelAndDiffFailure() {
            openSync(); mouseClick(item("protocol-sync-connect")); wait(20)
            mouseClick(item("protocol-sync-connect")); compare(manager.cancelCalls,1); verify(!page.remoteReady)
            manager.connectionFinished(true,"late mount"); compare(manager.statusCalls,0)
            page.startConnection(); manager.finishConnection(true); manager.finishStatus(true)
            page.requestDiff("698_protocol.xml"); compare(manager.diffCalls,1); compare(page.diffData.length,0)
            manager.finishStatus(false); verify(page.statusIsError); compare(page.diffData.length,0)
            page.startConnection(); manager.finishConnection(true); manager.finishStatus(true)
            page.requestSync(""); page.close(); verify(manager.cancelOperationCalls>0)
            manager.busy = false; manager.syncFinished({uploaded:["late.xml"],errors:[]}); verify(!item("protocol-sync-result").visible)
        }
        function test_sshDraftAndRealCredentialCompletion() {
            make(nextSsh); const ssh = item("ssh-terminal-backend")
            compare(item("ssh-password-draft").text,"")
            edit("ssh-password-draft","old-draft"); edit("ssh-host","new.example"); compare(item("ssh-password-draft").text,"")
            edit("ssh-port","2222"); edit("ssh-username","new-user"); edit("ssh-password-draft","new-draft")
            mouseClick(item("ssh-save-password")); tryCompare(ssh,"saveCalls",1)
            compare(ssh.host,"new.example"); compare(ssh.port,2222); compare(ssh.username,"new-user"); compare(ssh.savedDraft,"new-draft")
            verify(page.passwordOperationId.length>0); compare(item("ssh-password-draft").text,"new-draft")
            const operation = page.passwordOperationId
            ssh.passwordOperationFinished("unrelated-password-operation",true,"")
            compare(page.passwordOperationId,operation); compare(item("ssh-password-draft").text,"new-draft")
            ssh.finishPassword(false); verify(page.passwordNoticeError); compare(item("ssh-password-draft").text,"new-draft")
            mouseClick(item("ssh-save-password")); tryCompare(ssh,"saveCalls",2); ssh.finishPassword(true)
            compare(item("ssh-password-draft").text,""); verify(!page.passwordNoticeError)
            mouseClick(item("ssh-clear-password")); compare(ssh.clearCalls,1); verify(ssh.hasSavedPassword)
            ssh.finishPassword(true); verify(!ssh.hasSavedPassword)
        }
        function test_sshEncodingTextFits() {
            make(nextSsh)
            const encoding = item("ssh-encoding")
            for (const index of [0, 1]) {
                encoding.currentIndex = index
                wait(20)
                verify(!encoding.contentItem.truncated, encoding.currentText + " must be fully visible")
                verify(encoding.contentItem.x + encoding.contentItem.leftPadding + encoding.contentItem.contentWidth <= encoding.indicator.x,
                       "encoding text must not overlap its arrow")
            }
        }
        function test_sshConfigFoldPreservesDraftsAndReleasesSpace() {
            make(nextSsh)
            const ssh = item("ssh-terminal-backend")
            verify(page.connectionConfigExpanded)
            edit("ssh-host", "monitor.example")
            edit("ssh-port", "2222")
            edit("ssh-username", "operator")
            edit("ssh-password-draft", "fold-test-draft")
            item("ssh-buffer-lines").text = "8000"
            item("ssh-reconnect-count").text = "4"
            item("ssh-reconnect-interval").text = "3000"
            item("ssh-encoding").currentIndex = 1
            wait(30)
            const terminal = item("ssh-terminal-pane")
            const expandedHeight = terminal.height
            const disconnects = ssh.disconnectCalls
            mouseClick(item("ssh-config-toggle"))
            tryCompare(item("ssh-connection-scroll"), "visible", false)
            verify(!item("ssh-save-password").visible)
            verify(!item("ssh-clear-password").visible)
            tryVerify(function() { return terminal.height > expandedHeight + 30 })
            compare(item("ssh-target-summary").text, "监控 · operator@monitor.example:2222 · GB2312")
            compare(ssh.connectCalls, 0); compare(ssh.disconnectCalls, disconnects)
            page.visible = false; page.visible = true
            verify(!page.connectionConfigExpanded)
            mouseClick(item("ssh-config-toggle"))
            tryCompare(item("ssh-connection-scroll"), "visible", true)
            compare(item("ssh-host").text, "monitor.example")
            compare(item("ssh-port").text, "2222")
            compare(item("ssh-username").text, "operator")
            compare(item("ssh-password-draft").text, "fold-test-draft")
            compare(item("ssh-buffer-lines").text, "8000")
            compare(item("ssh-reconnect-count").text, "4")
            compare(item("ssh-reconnect-interval").text, "3000")
            compare(item("ssh-encoding").currentText, "GB2312")
            edit("ssh-host", "very-long-monitor-hostname-".repeat(8) + ".example")
            page.width = 900
            mouseClick(item("ssh-config-toggle")); verify(waitForPolish(page))
            const summary = item("ssh-target-summary")
            verify(summary.truncated)
            const summaryRight = summary.mapToItem(page, summary.width, 0).x
            const connectLeft = item("sshConnectAction").mapToItem(page, 0, 0).x
            verify(summaryRight <= connectLeft, "summary must not overlap connection actions")
            verify(findChild(page, "ssh-copy-on-select") === null)
            for (const name of ["ssh-config-toggle", "ssh-pause", "ssh-clear"])
                verify(!item(name).contentItem.truncated, name + " text must fit")
        }
        function test_sshFoldedControlsKeepWorking() {
            make(nextSsh)
            const ssh = item("ssh-terminal-backend")
            edit("ssh-password-draft", "connect-fold-draft")
            mouseClick(item("ssh-config-toggle"))
            mouseClick(item("sshConnectAction")); tryCompare(ssh, "connectCalls", 1)
            verify(!page.connectionConfigExpanded)
            ssh.connectionState = 2; ssh.connected = true
            ssh.appendTestLines(["RX 68 04 07 00 00 00", "tail"], 10)
            wait(30)
            page.selectLineAndExtractHex(0)
            page.searchKeyword = "RX"; page.refreshSearchResult()
            page.parseWindowVisible = true
            mouseClick(item("ssh-config-toggle")); mouseClick(item("ssh-config-toggle"))
            compare(ssh.currentLineCount, 2)
            compare(page.selectedLineIndex, 0)
            compare(page.selectedHexText.replace(/ /g, ""), "680407000000")
            compare(page.searchMatches.length, 1)
            verify(page.parseWindowVisible)
            mouseClick(item("ssh-pause")); verify(!page.autoScroll)
            mouseClick(item("ssh-pause")); verify(page.autoScroll)
            verify(findChild(page, "ssh-copy-on-select") === null)
            page.copyOnSelect = true; verify(page.copyOnSelect)
            verify(findChild(page, "ssh-copy-all") === null)
            mouseClick(item("ssh-clear")); compare(ssh.currentLineCount, 0)
            const disconnects = ssh.disconnectCalls
            mouseClick(item("sshConnectAction")); compare(ssh.disconnectCalls, disconnects + 1)
            verify(!page.connectionConfigExpanded)
        }
        function test_sshCommandManagementFold() {
            make(nextSsh)
            const ssh = item("ssh-terminal-backend")
            verify(!page.commandManagementExpanded)
            verify(!item("ssh-command-editor").visible)
            mouseClick(item("ssh-command-management-toggle"))
            tryCompare(item("ssh-command-editor"), "visible", true)
            wait(30)
            verify(!item("ssh-command-add").contentItem.truncated)
            item("ssh-command-name").text = "测试命令"
            item("ssh-command-content").text = "echo synthetic"
            const expandedHeight = item("ssh-terminal-pane").height
            mouseClick(item("ssh-command-management-toggle"))
            tryVerify(function() { return item("ssh-terminal-pane").height > expandedHeight + 20 })
            page.visible = false; page.visible = true
            verify(!page.commandManagementExpanded)
            mouseClick(item("ssh-command-management-toggle"))
            wait(30)
            compare(item("ssh-command-name").text, "测试命令")
            compare(item("ssh-command-content").text, "echo synthetic")
            mouseClick(item("ssh-command-add")); compare(page.customCommands.length, 1)
            tryVerify(function() { return visualItem(page, "ssh-command-delete-0") !== null })
            const remove = visualItem(page, "ssh-command-delete-0")
            const run = visualItem(page, "ssh-command-run-0")
            verify(remove.visible)
            mouseClick(item("ssh-command-management-toggle")); verify(!remove.visible); wait(30)
            verify(run.visible); verify(!run.enabled)
            verify(!run.contentItem.truncated)
            ssh.connectionState = 2; ssh.connected = true
            mouseClick(run)
            compare(ssh.sentCommands.length, 1); compare(ssh.sentCommands[0], "echo synthetic")
            compare(page.customCommands.length, 1)
            mouseClick(item("ssh-command-management-toggle")); verify(remove.visible); wait(30)
            mouseClick(remove, remove.width / 2, remove.height / 2)
            compare(page.customCmdPendingDeleteName, "测试命令")
        }
        function test_sshSelectionAndSearchSurviveEviction() {
            make(nextSsh)
            const ssh = item("ssh-terminal-backend")
            ssh.appendTestLines(["old", "RX 68 04 07 00 00 00", "tail"], 3)
            wait(30)
            page.searchKeyword = "RX"; page.refreshSearchResult()
            compare(page.searchMatches.length, 1)
            page.selectLineAndExtractHex(1)
            wait(20)
            const identity = page.selectedLineId
            compare(page.selectedHexText.replace(/ /g, ""), "680407000000")
            compare(item("ssh-selected-text").selectedText.replace(/ /g, ""), "680407000000")
            ssh.appendTestLines(["new"], 3); wait(30)
            compare(page.selectedLineId, identity)
            compare(page.selectedLineIndex, 0)
            compare(page.searchMatches[0], identity)
            ssh.clearBuffer(); wait(30)
            compare(page.selectedHexText, "")
            compare(page.searchMatches.length, 0)
            ssh.appendTestLines(["RX 05 01 02 40 00 02 00 00"], 3); wait(30)
            page.selectLineAndExtractHex(0)
            verify(page.selectedLineId > identity)
            compare(page.selectedHexText.replace(/ /g, ""), "0501024000020000")
        }
        function test_sshPausedViewportSurvivesNewRows() {
            make(nextSsh)
            const ssh = item("ssh-terminal-backend")
            let lines = []
            for (let i = 0; i < 200; ++i) lines.push("Log row " + i)
            ssh.appendTestLines(lines, 200); wait(40)
            const view = item("ssh-log-list")
            page.autoScroll = false
            view.positionViewAtIndex(80, ListView.Beginning); wait(30)
            page.captureLogViewport()
            const anchor = page.viewAnchorId
            verify(anchor >= 0)
            ssh.appendTestLines(["next 1", "next 2", "next 3"], 200); wait(40)
            compare(ssh.lineIdAt(view.indexAt(1, view.contentY + 1)), anchor)
            page.autoScroll = true; page.applyLogViewport(); wait(30)
            verify(view.atYEnd)
        }
        function test_sshCaretBlinksWithoutIntermediateOpacity() {
            make(nextSsh)
            page.terminal.connected = true
            page.terminalInput.forceActiveFocus()
            const caret = item("ssh-terminal-caret")
            tryVerify(function() { return caret.visible })
            let shown = false, hidden = false
            for (let i = 0; i < 14; ++i) {
                verify(caret.opacity === 0 || caret.opacity === 1, "caret must not animate intermediate opacity")
                shown = shown || caret.opacity === 1
                hidden = hidden || caret.opacity === 0
                wait(100)
            }
            verify(shown && hidden, "caret still blinks")
            page.terminal.connected = false
            tryVerify(function() { return !caret.visible })
        }
        function test_sshBlankLineKeepsItsHeight() {
            make(nextSsh)
            const ssh = item("ssh-terminal-backend")
            ssh.appendTestLines([""], 100); wait(30)
            verify(item("ssh-log-line-0").height >= 17, "a blank line still occupies one text row")
        }
        function test_sshLongRowKeepsFullTextForSearchAndSelection() {
            make(nextSsh)
            const ssh = item("ssh-terminal-backend")
            const text = "中文内容".repeat(300) + " RX 68 04 07 00 00 00"
            ssh.appendTestLines([text], 100); wait(30)
            const row = item("ssh-log-line-0")
            let display = null
            for (const child of row.children) if (child.text === text) display = child
            verify(display !== null)
            for (const width of [902, 1600]) {
                page.width = width; wait(30)
                compare(display.lineCount, 1)
                verify(display.contentWidth <= display.width + 1)
                verify(row.height < 40)
                compare(ssh.lineAt(0), text)
            }
            page.searchKeyword = "RX"; page.refreshSearchResult()
            compare(page.searchMatches.length, 1)
            page.selectLineAndExtractHex(0); wait(30)
            compare(item("ssh-selected-text").text, text)
            compare(item("ssh-selected-text").selectedText.replace(/ /g, ""), "680407000000")
        }
        function test_sshConnectionOptionsStayOnOneScrollableRow() {
            make(nextSsh)
            item("ssh-terminal-backend").autoReconnectEnabled = true
            const scroll = item("ssh-connection-scroll")
            const encoding = item("ssh-encoding")
            const fields = [item("ssh-host"), encoding, item("ssh-buffer-lines"),
                            item("ssh-reconnect-count"), item("ssh-reconnect-interval"), item("ssh-auto-reconnect")]
            for (const width of [902, 1160, 1600]) {
                page.width = width
                wait(30)
                for (const field of fields) {
                    compare(field.parent, fields[0].parent, "all connection options must share one row")
                    verify(Math.abs(field.y + field.height / 2 - fields[0].y - fields[0].height / 2) < 1)
                    verify(field.width > 0 && field.y + field.height <= scroll.height)
                }
                for (const index of [0, 1]) {
                    encoding.currentIndex = index
                    encoding.activated(index)
                    wait(10)
                    verify(!encoding.contentItem.truncated, encoding.currentText + " must fit at width " + width)
                    compare(item("ssh-terminal-backend").encoding, encoding.currentText)
                }
                compare(scroll.ScrollBar.horizontal.visible, scroll.contentWidth > scroll.width)
                if (width === 902) {
                    verify(scroll.contentWidth > scroll.width)
                    scroll.contentX = scroll.contentWidth - scroll.width
                    const last = fields[fields.length - 1]
                    const right = last.mapToItem(scroll, last.width, 0).x
                    verify(right <= scroll.width && right > 0, "horizontal scrolling must reveal automatic reconnect at the end")
                    fields[0].forceActiveFocus()
                    tryCompare(scroll, "contentX", fields[0].x)
                    last.forceActiveFocus()
                    wait(20)
                    verify(last.mapToItem(scroll, last.width, 0).x <= scroll.width,
                           "keyboard focus must scroll the active field into view")
                }
            }
        }
        function test_xmlDiffPreservesLiteralMarkup() {
            openSync(); connectSync(); page.requestDiff("698_protocol.xml")
            manager.busy = false; manager.diffFinished("698_protocol.xml",[{leftType:"equal",rightType:"equal",leftText:"<b>literal XML</b>",rightText:"<Item Name=\"<b>\" />"}]); wait(30)
            const window = item("protocol-diff-window")
            const left = visualItem(window.contentItem,"protocol-diff-left-text"); const right = visualItem(window.contentItem,"protocol-diff-right-text")
            verify(left !== null); verify(right !== null)
            compare(left.text,"<b>literal XML</b>"); compare(left.textFormat,Text.PlainText)
            compare(right.text,"<Item Name=\"<b>\" />"); compare(right.textFormat,Text.PlainText)
        }
        function diffItem(window, name) {
            const found = visualItem(window.contentItem, name); verify(found !== null, name); return found;
        }
        function openDiff(rows) {
            openSync(); connectSync(); page.requestDiff("698_protocol.xml");
            manager.busy = false; manager.diffFinished("698_protocol.xml", rows);
            const window = item("protocol-diff-window"); tryCompare(window, "visible", true); wait(40); return window;
        }
        function diffRows() {
            const rows = [];
            for (let i = 0; i < 80; ++i) rows.push({leftType: "same", rightType: "same", leftText: "<Item index=\"" + i + "\" />", rightText: "<Item index=\"" + i + "\" />"});
            rows[0].leftType = "del"; rows[0].rightType = "empty";
            for (const i of [35, 36]) { rows[i].leftType = "del"; rows[i].rightType = "add"; }
            rows[79].leftType = "empty"; rows[79].rightType = "add";
            rows[20] = {leftType:"fold", rightType:"fold", leftText:"省略相同内容", rightText:"省略相同内容"};
            return rows;
        }
        function test_protocolDiffOverviewAndBlockNavigation() {
            const window = openDiff(diffRows());
            compare(window.changeBlocks.length, 3); compare(window.changeBlocks[1].start, 35); compare(window.changeBlocks[1].end, 36);
            compare(window.currentBlock, 0);
            const list = diffItem(window, "protocol-diff-list");
            const overview = diffItem(window, "protocol-diff-overview");
            const previous = diffItem(window, "protocol-diff-prev");
            const next = diffItem(window, "protocol-diff-next");
            verify(!previous.enabled); verify(next.enabled);
            mouseClick(next); compare(window.currentBlock, 1); compare(list.currentIndex, 35); verify(list.contentY > 0);
            mouseClick(overview, 9, overview.height - 1); compare(window.currentBlock, 2); compare(list.currentIndex, 79); verify(!next.enabled);
            const viewport = diffItem(window, "protocol-diff-viewport"); verify(viewport.y > 0);
            keyClick(Qt.Key_Up); compare(window.currentBlock, 1);
            mouseClick(overview, 9, 0); compare(window.currentBlock, 0);
            keyClick(Qt.Key_Down); compare(window.currentBlock, 1);
            mouseClick(previous); compare(window.currentBlock, 0);
            window.width = 600; window.height = 400; wait(30);
            verify(overview.width > 0 && overview.height > 0); verify(next.x + next.width <= window.width);
            mouseClick(overview, 9, overview.height * 36 / 80); compare(window.currentBlock, 1);
        }
        function test_protocolDiffResetAndIdenticalFiles() {
            const window = openDiff(diffRows());
            window.jumpBlock(2); compare(window.currentBlock, 2);
            page.diffData = [{leftType:"same", rightType:"same", leftText:"<Same />", rightText:"<Same />"}];
            tryCompare(window, "currentBlock", -1); compare(window.changeBlocks.length, 0);
            verify(!diffItem(window, "protocol-diff-prev").enabled); verify(!diffItem(window, "protocol-diff-next").enabled);
            verify(diffItem(window, "protocol-diff-summary").text.indexOf("无差异") >= 0);
            mouseClick(diffItem(window, "protocol-diff-overview")); compare(window.currentBlock, -1);
            page.diffData = [{leftType:"empty", rightType:"add", leftText:"", rightText:"<New />"}];
            tryCompare(window, "currentBlock", 0); compare(window.changeBlocks.length, 1);
            verify(window.changeBlocks[0].added); verify(!window.changeBlocks[0].deleted);
            window.close(); tryCompare(page, "diffData", []);
            page.requestDiff("other.xml"); manager.busy = false; manager.diffFinished("other.xml", diffRows());
            tryCompare(window, "visible", true); tryCompare(window, "currentBlock", 0); compare(window.diffFileName, "other.xml");
            page.diffData = []; tryCompare(window, "currentBlock", -1);
        }
        function test_protocolDiffShowsLoadingAndIgnoresCancelledResult() {
            openSync(); connectSync(); page.requestDiff("module_msg_protocol.xml");
            const window = item("protocol-diff-window");
            tryCompare(window, "visible", true); verify(window.loading);
            compare(diffItem(window, "protocol-diff-close").text, "取消");
            verify(diffItem(window, "protocol-diff-summary").text.indexOf("正在比较") >= 0);
            mouseClick(diffItem(window, "protocol-diff-close"));
            verify(!window.visible); compare(manager.cancelOperationCalls, 1); compare(page.pendingAction, "");
            manager.busy = false; manager.diffFinished("module_msg_protocol.xml", diffRows());
            verify(!window.visible); compare(page.diffData.length, 0);
            page.requestDiff("698_protocol.xml"); verify(window.visible); verify(window.loading);
            manager.busy = false; manager.diffFinished("698_protocol.xml", diffRows()); wait(40);
            verify(!window.loading); compare(window.currentBlock, 0); compare(window.changeBlocks.length, 3);
        }
        function test_legacyPlatformSyncContract() {
            manager = syncMock.createObject(scene,{usesSystemSmb:false})
            make(nextSync,{syncManager:manager})
            verify(item("protocol-sync-path").readOnly)
            compare(item("protocol-sync-legacy-user").text,"legacy-user"); compare(item("protocol-sync-legacy-password").echoMode,TextInput.Password)
            mouseClick(item("protocol-sync-connect")); compare(manager.saveCredentialCalls,1)
            manager.finishConnection(true); compare(manager.statusCalls,1)
            mouseClick(item("protocol-sync-all")); compare(manager.syncCalls,1); verify(item("protocol-sync-result").visible)
            verify(item("protocol-sync-result-text").text.indexOf("legacy.xml")>=0)
        }
        function test_sshPendingIdentityCancelDoesNotConnectLater() {
            make(nextSsh); const ssh = item("ssh-terminal-backend")
            edit("ssh-host","wait.example"); edit("ssh-password-draft","temporary-wait-password")
            page.beginSshAction("connect"); verify(page.pendingSshAction !== null)
            page.connectAction(); verify(page.pendingSshAction === null)
            ssh.finishRefresh(); wait(30); compare(ssh.connectCalls,0)
            ssh.credentialBusy = true; page.beginSshAction("connect"); page.close(); ssh.finishRefresh(); wait(30)
            compare(ssh.connectCalls,0)
        }
        function test_sshFreshKeyAndChangedKeyFailure() {
            make(nextSsh); const ssh = item("ssh-terminal-backend")
            ssh.requestKey("old-key","SHA256:old"); wait(20)
            ssh.requestKey("current-key","SHA256:current"); wait(30)
            compare(item("ssh-host-key-fingerprint").text,"SHA256:current")
            mouseClick(item("ssh-host-key-accept")); wait(20)
            const accepted = ssh.confirmations.filter(function(entry){ return entry.accept })
            compare(accepted.length,1); compare(accepted[0].id,"current-key")
            ssh.hostKeyPending = false; ssh.connectionState = 4; ssh.statusMessage = "主机密钥已变化，连接已阻止"
            wait(20); verify(!item("ssh-host-key-dialog").visible); verify(!item("ssh-host-key-accept").enabled)
            compare(ssh.confirmations.filter(function(entry){ return entry.accept }).length,1)
        }
        function test_sshLegacyPasswordContract() {
            make(nextSsh); const ssh = item("ssh-terminal-backend"); ssh.usesSecureCredentials = false; wait(20)
            compare(item("ssh-password-draft").text,"must-not-render-saved-password")
            verify(!item("ssh-save-password").visible); verify(!item("ssh-clear-password").visible)
            edit("ssh-host","legacy-target.example"); compare(item("ssh-password-draft").text,"must-not-render-saved-password")
            mouseClick(item("sshConnectAction")); compare(ssh.connectCalls,1)
            compare(ssh.password,"must-not-render-saved-password"); compare(ssh.saveCalls,0); compare(ssh.clearCalls,0)
        }
        function test_sshHostKeyExplicitAcceptAndCloseReject() {
            make(nextSsh); const ssh = item("ssh-terminal-backend")
            edit("ssh-password-draft","temporary-connect-password"); mouseClick(item("sshConnectAction")); tryCompare(ssh,"connectCalls",1)
            compare(item("ssh-password-draft").text,"")
            ssh.requestKey("key-1","SHA256:first-fingerprint"); wait(30)
            compare(ssh.confirmations.length,0); verify(item("ssh-host-key-dialog").visible)
            compare(item("ssh-host-key-target").text,"old.example:22"); compare(item("ssh-host-key-fingerprint").text,"SHA256:first-fingerprint")
            mouseClick(item("ssh-host-key-accept")); compare(ssh.confirmations.length,1); verify(ssh.confirmations[0].accept)
            compare(ssh.confirmations[0].id,"key-1"); compare(ssh.confirmations[0].fingerprint,"SHA256:first-fingerprint")
            ssh.requestKey("key-2","SHA256:second-fingerprint"); wait(30); item("ssh-host-key-dialog").close(); wait(30)
            compare(ssh.confirmations[ssh.confirmations.length-1].accept,false)
            ssh.requestKey("key-3","SHA256:third-fingerprint"); wait(30); page.close(); wait(30)
            compare(ssh.confirmations[ssh.confirmations.length-1].accept,false)
        }
    }
}
