import QtQuick
import QtQml.Models
QtObject {
    id: root
    enum ConnectionState { Idle, Connecting, Connected, Reconnecting, Failed, Disconnected }
    property bool usesSecureCredentials: true
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
    property bool autoReconnectEnabled: false
    property int reconnectMaxAttempts: 2
    property int reconnectIntervalMs: 2000
    property int reconnectAttempt: 0
    property int maxBufferLines: 10000
    property int currentLineCount: 0
    property ListModel lineModel: ListModel {}
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
    function sendCommand(command) {}
    function sendRawBytes(value) {}
    function sendTabCompletion(prefix) {}
    function getBufferText() { return "" }
    function clearBuffer() {}
    function findLineIndexes(keyword, sensitive) { return [] }
    function findLineIndexesInRange(keyword, start, end, sensitive) { return [] }
    function lineAt(index) { return "" }
    function copyLineToClipboard(index) {}
    function copyBufferToClipboard() {}
    function clipboardText() { return "" }
}
