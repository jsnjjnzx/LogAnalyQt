import QtQuick
import QtQml.Models
QtObject {
    id: root
    enum ConnectionState { Idle, Connecting, Connected, Reconnecting, Failed, Disconnected }
    property bool useSystemCredentials: false
    property bool usesSecureCredentials: useSystemCredentials
    property string host: "old.example"
    property int port: 22
    property string username: "old-user"
    property string password: "must-not-render-saved-password"
    property string encoding: "UTF-8"
    property bool hasSavedPassword: true
    property bool credentialBusy: false
    property string credentialError: ""
    property bool hostKeyPending: false
    property bool connected: false
    property int connectionState: 0
    property string statusMessage: ""
    property string lastError: ""
    property bool logging: false
    property bool logBusy: false
    onLogBusyChanged: loggingChanged()
    property string logFilePath: ""
    property string logError: ""
    property bool existingLogFile: false
    property var logCalls: []
    signal logOperationFinished(bool success, string message)
    function suggestedLogFile(name) { return "file:///tmp/ssh-" + name + ".log" }
    function logFileExists(file) { return existingLogFile }
    function startLogging(file, append, intervalMs, bufferKiB) {
        logCalls = logCalls.concat([{kind:"start",file:String(file),append:append,intervalMs:intervalMs,bufferKiB:bufferKiB}])
        logging = true; logFilePath = String(file); logOperationFinished(true, "已开始记录")
        return true
    }
    function stopLogging() {
        if (!logging) return false
        logCalls = logCalls.concat([{kind:"stop"}]); logging = false
        logOperationFinished(true, "已停止记录"); return true
    }
    function exportLog(file) {
        logCalls = logCalls.concat([{kind:"export",file:String(file)}])
        logOperationFinished(true, "已导出"); return true
    }
    property bool autoReconnectEnabled: false
    property int reconnectMaxAttempts: 2
    property int reconnectIntervalMs: 2000
    property int reconnectAttempt: 0
    property int maxBufferLines: 10000
    readonly property int currentLineCount: lineModel.count
    property real receivedFirstLineId: 0
    readonly property real firstLineId: receivedFirstLineId
    property bool receptionPaused: false
    property var pendingPeerLines: []
    onReceptionPausedChanged: {
        if (!receptionPaused && pendingPeerLines.length) {
            const lines = pendingPeerLines; pendingPeerLines = []
            appendTestLines(lines, maxBufferLines)
        }
    }
    property bool presentationActive: true
    property string liveLine: ""
    property string liveMonitorColor: ""
    property bool liveMonitorWholeLine: true
    property var liveMonitorRanges: []
    property int cursorColumn: 0
    property int cursorPosition: liveLine.length
    property int cursorPadding: 0
    property bool persistSettings: true
    property ListModel receiveModel: ListModel { dynamicRoles: true }
    readonly property var lineModel: receiveModel
    property bool initialized: false
    property int connectCalls: 0
    property int disconnectCalls: 0
    property int saveCalls: 0
    property int clearCalls: 0
    property string pendingPasswordId: ""
    property string savedDraft: ""
    property var confirmations: []
    property string keyId: ""
    property string keyFingerprint: ""
    signal credentialStateChanged()
    signal passwordOperationFinished(string operationId, bool success, string error)
    signal hostKeyConfirmationRequested(string host, int port, string algorithm, string fingerprint, string requestId)
    signal sharedBufferOverflowed()
    signal lineCountChanged()
    signal monitorMatched(var hit)
    property var monitorRules: []
    property bool monitorArmed: false
    function setMonitorRules(rules, armed) { monitorRules = rules; monitorArmed = armed; return true }
    signal linesAboutToChange()
    signal connectionError(string error)
    signal tabCompletionReady(string completedText, var candidates)
    Component.onCompleted: initialized = true
    onHostChanged: if (initialized) refreshIdentity()
    onPortChanged: if (initialized) refreshIdentity()
    onUsernameChanged: if (initialized) refreshIdentity()
    function refreshIdentity() { if (!usesSecureCredentials) return; connected = false; connectionState = 0; hostKeyPending = false; credentialBusy = true; credentialStateChanged(); Qt.callLater(finishRefresh) }
    function finishRefresh() { credentialBusy = false; credentialStateChanged() }
    function connectToHost() { if (credentialBusy) return; connectCalls++; connectionState = 1 }
    function disconnectFromHost() { disconnectCalls++; connected = false; connectionState = 5; hostKeyPending = false }
    function savePassword(value) { if (credentialBusy) return ""; saveCalls++; savedDraft = value; pendingPasswordId = "password-" + saveCalls; credentialBusy = true; credentialStateChanged(); return pendingPasswordId }
    function clearSavedPassword() { if (credentialBusy) return ""; clearCalls++; pendingPasswordId = "clear-" + clearCalls; credentialBusy = true; credentialStateChanged(); return pendingPasswordId }
    function finishPassword(success) {
        const id = pendingPasswordId; pendingPasswordId = ""; credentialBusy = false; credentialError = success ? "" : "合成凭据操作失败"
        if (success) hasSavedPassword = id.indexOf("clear-") !== 0
        credentialStateChanged(); passwordOperationFinished(id, success, credentialError)
    }
    function requestKey(id, fingerprint) { keyId = id; keyFingerprint = fingerprint; hostKeyPending = true; hostKeyConfirmationRequested(host, port, "ssh-ed25519", fingerprint, id) }
    function confirmHostKey(id, fingerprint, accept) {
        const entries = confirmations.slice(); entries.push({id:id,fingerprint:fingerprint,accept:accept}); confirmations = entries
        if (id !== keyId || fingerprint !== keyFingerprint || !hostKeyPending) return
        hostKeyPending = false
        if (!accept) disconnectFromHost()
    }
    property var sentCommands: []
    function sendCommand(command) { sentCommands = sentCommands.concat([command]) }
    property var rawInput: []
    function sendRawBytes(value) { rawInput = rawInput.concat([value]) }
    function sendTabCompletion(prefix) {}
    function getBufferText() {
        const lines = []
        for (let i = 0; i < receiveModel.count; ++i) lines.push(receiveModel.get(i).lineText)
        return lines.join("\n")
    }
    function clearBuffer() {
        linesAboutToChange(); receivedFirstLineId += receiveModel.count; receiveModel.clear()
        lineCountChanged()
    }
    function findLineIndexes(keyword, sensitive) { return [] }
    function findLineIndexesInRange(keyword, start, end, sensitive) { return [] }
    function findLineIds(keyword, fromId) {
        let result = []
        for (let i = 0; i < currentLineCount; ++i)
            if ((fromId === undefined || firstLineId + i >= fromId) && lineAt(i).toLowerCase().indexOf(keyword.toLowerCase()) >= 0)
                result.push(firstLineId + i)
        return result
    }
    function lineIdAt(index) { return index >= 0 && index < currentLineCount ? firstLineId + index : -1 }
    function indexForLineId(id) { return id >= firstLineId && id < firstLineId + currentLineCount ? id - firstLineId : -1 }
    function logicalStart(index) {
        while (index > 0 && lineModel.get(index).continuation) --index
        return index
    }
    function lineIsComplete(index) {
        if (index < 0 || index >= currentLineCount) return false
        const start = logicalStart(index)
        while (index + 1 < currentLineCount && lineModel.get(index).continues) ++index
        return !lineModel.get(start).continuation && !lineModel.get(index).continues
    }
    function lineSegmentOffset(index) {
        let offset = 0
        for (let i = logicalStart(index); i < index; ++i) offset += lineModel.get(i).lineText.length
        return offset
    }
    function lineAt(index) {
        if (index < 0 || index >= currentLineCount) return ""
        let text = ""
        for (let i = logicalStart(index); i < currentLineCount; ++i) {
            text += lineModel.get(i).lineText
            if (!lineModel.get(i).continues) break
        }
        return text
    }
    function appendTestLines(lines, limit) {
        if (receptionPaused) { pendingPeerLines = pendingPeerLines.concat(lines); return }
        linesAboutToChange()
        const nextId = receivedFirstLineId + receiveModel.count
        for (const text of lines) receiveModel.append({lineText:text,lineId:receivedFirstLineId+receiveModel.count,continuation:false,continues:false,monitorColor:"",monitorRuleName:"",monitorWholeLine:true,monitorRanges:[]})
        while (receiveModel.count > limit) { receiveModel.remove(0); receivedFirstLineId++ }
        for (let i = 0; i < lines.length && monitorArmed; ++i) {
            const found = monitorRules.filter(rule => new RegExp(rule.pattern, rule.ignoreCase ? "i" : "").test(lines[i]))
            const rule = found.find(value => value.pauseOnMatch !== false) || found[0]
            if (!rule) continue
            const ranges = []
            if (rule.highlightWholeLine === false) {
                const regex = new RegExp(rule.pattern, rule.ignoreCase ? "gi" : "g"); let match
                while ((match = regex.exec(lines[i])) !== null) {
                    if (match[0].length) ranges.push({start:match.index,length:match[0].length})
                    else regex.lastIndex++
                }
            }
            const row = nextId + i - receivedFirstLineId
            if (row >= 0 && row < receiveModel.count) {
                receiveModel.setProperty(row, "monitorColor", rule.color || "#e8b76a")
                receiveModel.setProperty(row, "monitorRuleName", rule.name)
                receiveModel.setProperty(row, "monitorWholeLine", rule.highlightWholeLine !== false)
                receiveModel.setProperty(row, "monitorRanges", ranges)
            }
            monitorMatched({ruleId:rule.id,ruleName:rule.name,color:rule.color || "#e8b76a",pauseOnMatch:rule.pauseOnMatch !== false,
                highlightWholeLine:rule.highlightWholeLine !== false,ranges:ranges,
                text:lines[i],lineId:nextId+i,receivedLineId:receivedFirstLineId+receiveModel.count,time:"12:00:00"})
        }
        lineCountChanged()
    }
    function copyLineToClipboard(index) {}
    function copyBufferToClipboard() {}
    function selectedText(firstId, firstOffset, lastId, lastOffset) {
        const first = firstId - firstLineId, last = lastId - firstLineId
        if (first < 0 || last < first || last > currentLineCount) return ""
        let result = ""
        for (let row = first; row <= last; ++row) {
            const text = row === currentLineCount ? liveLine : lineModel.get(row).lineText
            result += text.slice(row === first ? firstOffset : 0, row === last ? lastOffset : text.length)
            if (row < last && !lineModel.get(row).continues) result += "\n"
        }
        return result
    }
    property string clipboardContents: ""
    function clipboardText() { return clipboardContents }
}
