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
                highlighted: false, indent: 0, hasChildren: false, expanded: false, offset: 0, length: 1});
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
        function item(parent, name) { const value = findChild(parent, name); verify(value !== null, name); return value }
        function visualItem(node, name) {
            if (node.objectName === name) return node
            if (node.children) for (const child of node.children) { const found = visualItem(child, name); if (found) return found }
            return null
        }
        function select(index) {
            const tab = item(page, "ssh-session-row-" + page.sessions[index].sessionId)
            mouseClick(tab, 25, 18); wait(30); compare(page.currentIndex, index)
        }
        function connected(session) { session.terminal.connectionState = 2; session.terminal.connected = true }
        function clipboard(value) {
            const editor = clipboardEditor.createObject(scene)
            if (value !== undefined) { editor.text = value; editor.selectAll(); editor.copy() }
            else editor.paste()
            const text = editor.text; editor.destroy()
            return text
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
            const stop = item(a, "ssh-stop-reception"), pause = item(a, "ssh-pause"), clear = item(a, "ssh-clear")
            verify(stop.x > pause.x && stop.x < clear.x)
            compare(stop.width, pause.width); compare(stop.height, pause.height)
            mouseClick(stop); wait(30)
            verify(a.terminal.receptionPaused); compare(stop.text, "恢复接收"); verify(!a.autoScroll)
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
        function test_followAndReceptionPauseHaveDistinctHoverHelp() {
            make()
            const a = page.activeSession
            const pause = item(a, "ssh-pause"), stop = item(a, "ssh-stop-reception")
            verify(pause.ToolTip.text.indexOf("自动滚动") >= 0)
            verify(stop.ToolTip.text.indexOf("后台日志读取") >= 0)
            verify(waitForPolish(a))
            mouseMove(a, 1, 1)
            mouseMove(pause, pause.width/2, pause.height/2)
            tryCompare(pause, "hovered", true)
            tryVerify(() => pause.ToolTip.toolTip.visible, 5000, "follow pause tooltip must appear after hovering")
            mouseMove(stop, stop.width/2, stop.height/2)
            tryCompare(stop, "hovered", true)
            tryVerify(() => stop.ToolTip.toolTip.visible, 5000, "reception pause tooltip must appear after hovering")
            mouseClick(pause); verify(!a.autoScroll && !a.terminal.receptionPaused)
            a.terminal.appendTestLines(["updates while follow is paused"], 100); wait(20)
            compare(a.terminal.currentLineCount, 1)
            mouseClick(stop); verify(a.terminal.receptionPaused)
            verify(stop.ToolTip.text.indexOf("积压") >= 0)
            mouseClick(pause); verify(a.autoScroll && a.terminal.receptionPaused)
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
            mouseClick(item(a, "ssh-pause")); wait(20)
            verify(a.autoScroll && a.monitorHit === null && a.terminal.monitorArmed)
            a.terminal.appendTestLines(["normal after resume"], 100); wait(20); verify(a.autoScroll)
            a.terminal.appendTestLines(["ERROR new"], 100); wait(20); verify(!a.autoScroll)
            b.terminal.appendTestLines(["设备 12 离线"], 100); wait(20)
            compare(b.monitorHit.color, "#80b9ff"); verify(!b.autoScroll)
            verify(page.dockSession(a)); wait(20)
            a.resumeFollowing(); a.terminal.liveLine = "ERROR unfinished output"; wait(20)
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
            compare(item(a, "ssh-monitor-status").text, "监测未开启 · 已选 0 条规则")
            mouseClick(item(a, "ssh-rule-select")); wait(30)
            verify(item(a, "ssh-rule-picker").visible)
            compare(item(a, "ssh-rule-manage").text, "打开规则库…")
            const list = item(a, "ssh-rule-list")
            tryVerify(() => list.itemAtIndex(0) !== null)
            mouseClick(list.itemAtIndex(0), 12, 25); wait(20)
            compare(a.monitorRuleIds[0], page.monitorRules[0].id)
            compare(item(a, "ssh-rule-select").text, "选择规则（1）▾")
            compare(item(a, "ssh-monitor-status").text, "监测未开启 · 已选 1 条规则")
            item(a, "ssh-rule-picker").close()
            mouseClick(item(a, "ssh-monitor-enabled")); verify(a.monitorEnabled)
            compare(item(a, "ssh-monitor-status").text, "监测中 · 1 条规则")
            a.monitorRuleIds = []
            compare(item(a, "ssh-monitor-status").text, "请选择规则")
            a.monitorRuleIds = [page.monitorRules[0].id]
            a.terminal.appendTestLines(["设备 11 超时", "设备 12 超时"], 100); wait(20)
            verify(a.autoScroll && a.monitorHit === null && a.monitorNotice !== null)
            compare(a.terminal.lineModel.get(0).monitorColor, "#80b9ff")
            compare(a.terminal.lineModel.get(1).monitorColor, "#80b9ff")
            a.autoScroll = false
            a.terminal.appendTestLines(["设备 12 超时"], 100); wait(20)
            verify(a.monitorHit === null)
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
            compare(item(a, "ssh-selected-text").selectedText, "aabb")
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
            mouseClick(item(a, "ssh-copy-on-select")); verify(a.copyOnSelect)
            clipboard("untouched")
            mousePress(row, x1, y)
            mouseMove(row, x4, y, 20, Qt.LeftButton); wait(20)
            verify(a.hasTextSelection)
            compare(clipboard(), "untouched")
            mouseRelease(row, x4, y); wait(20)
            compare(clipboard(), "bcd")
            mouseClick(item(a, "ssh-copy-on-select")); verify(!a.copyOnSelect)
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
            verify(item(a, "ssh-selected-live-text").selectedText === "omp")
        }
        function test_copyOnSelectIsPerSessionAndRestoresWithoutCopying() {
            make()
            const a = page.activeSession
            mouseClick(item(a, "ssh-copy-on-select"))
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
        function test_sidebarOwnsActionsAndSessionMenuOwnsDetachedWindow() {
            make()
            const sidebar = item(page, "ssh-session-sidebar")
            for (const name of ["ssh-sidebar-toggle", "ssh-new-session", "ssh-saved-connections"]) {
                let node = item(page, name)
                while (node && node !== sidebar) node = node.parent
                compare(node, sidebar, name + " must belong to the session sidebar")
            }
            verify(findChild(page, "ssh-active-config") === null)
            verify(findChild(page, "ssh-detach-session") === null)
            const a = page.activeSession; connected(a)
            const disconnects = a.terminal.disconnectCalls
            mouseClick(item(page, "ssh-session-menu-" + a.sessionId)); wait(20)
            const overlay = scene.Window.window.contentItem
            verify(item(overlay, "ssh-session-detach-" + a.sessionId).visible)
            mouseClick(item(overlay, "ssh-session-detach-" + a.sessionId)); wait(40)
            verify(a.detached && a.monitorWindow.visible)
            mouseClick(item(page, "ssh-sidebar-toggle")); wait(20)
            verify(page.sidebarCollapsed)
            verify(sidebar.width >= 60)
            verify(item(page, "ssh-session-badge-" + a.sessionId).visible)
            mouseClick(item(page, "ssh-session-row-" + a.sessionId), 25, 18, Qt.RightButton); wait(20)
            verify(item(overlay, "ssh-session-dock-" + a.sessionId).visible)
            mouseClick(item(overlay, "ssh-session-dock-" + a.sessionId)); wait(40)
            verify(!a.detached); compare(a.terminal.disconnectCalls, disconnects)
            mouseClick(item(page, "ssh-saved-connections")); wait(20)
            verify(item(page, "ssh-connections-dialog").visible)
            item(page, "ssh-connections-dialog").close()
            mouseClick(item(page, "ssh-new-session")); wait(20)
            compare(page.sessions.length, 2)
            select(0); compare(page.activeSession, a)
            mouseClick(item(page, "ssh-sidebar-toggle")); wait(20)
            verify(!page.sidebarCollapsed)
            mouseClick(item(page, "ssh-session-menu-" + a.sessionId)); wait(20)
            verify(findChild(overlay, "ssh-session-config-" + a.sessionId) === null)
            verify(item(overlay, "ssh-session-detach-" + a.sessionId).visible)
            keyClick(Qt.Key_Escape); wait(20)
            mouseClick(item(a, "ssh-config-toggle")); wait(20)
            verify(a.connectionConfigExpanded)
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
            mouseClick(item(page, "ssh-session-connect-" + a.sessionId))
            compare(page.activeSession, a); compare(a.terminal.connectCalls, 1)
            compare(a.terminal.host, "a.example"); compare(a.terminal.port, 2222)
            compare(a.terminal.password, "password-a"); compare(b.terminal.connectCalls, 0)
            mouseClick(item(page, "ssh-session-connect-" + b.sessionId))
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
            verify(a.detached && window.visible && a.visible)
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
            verify(!a.detached && !window.visible && a.visible)
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
            compare(item(b, "ssh-host").text, ""); verify(b.connectionConfigExpanded)
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
            const selected = item(a, "ssh-selected-text").selectedText
            const caretX = view.footerItem.caretX
            mouseWheel(view, 40, 20, 0, 120, Qt.NoButton, control)
            tryCompare(a, "terminalFontSize", 14)
            compare(item(a, "ssh-selected-text").font.pixelSize, 14)
            compare(item(a, "ssh-terminal-live-line").font.pixelSize, 14)
            compare(a.terminalInput.font.pixelSize, 14)
            compare(item(a, "ssh-selected-text").selectedText, selected)
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
