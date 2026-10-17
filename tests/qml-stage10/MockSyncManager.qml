import QtQuick
import QtQml.Models
QtObject {
    property bool usesSystemSmb: true
    property string username: "legacy-user"
    property string password: "legacy-password"
    property int saveCredentialCalls: 0
    property string sharePath: "smb://files.example/protocols"
    property bool busy: false
    property bool connecting: false
    property bool credentialBusy: false
    property string credentialError: ""
    property string lastError: ""
    property ListModel statusModel: ListModel {}
    property int connectionCalls: 0
    property int cancelCalls: 0
    property int cancelOperationCalls: 0
    property int statusCalls: 0
    property int syncCalls: 0
    property int diffCalls: 0
    signal connectionFinished(bool success, string message)
    signal statusUpdated()
    signal statusFinished(bool success, string message)
    signal syncFinished(var result)
    signal diffFinished(string fileName, var diff)
    function connectToShare() { connectionCalls++; connecting = true }
    function cancelConnection() { cancelCalls++; connecting = false }
    function cancelPendingOperations() { cancelOperationCalls++ }
    function fetchSyncStatus() { statusCalls++; if (usesSystemSmb) busy = true; return true }
    function syncAll() { syncCalls++; if (!usesSystemSmb) return {uploaded:["legacy.xml"],downloaded:[],skipped:[],failed:[],errors:[]}; busy = true; return {pending:true} }
    function syncFile(fileName) { return syncAll() }
    function getXmlDiff(fileName) { diffCalls++; busy = true; return [] }
    function saveCredentials() { saveCredentialCalls++ }
    function clearStatus() { statusModel.clear() }
    function localProtocolDir() { return "/tmp/isolated-protocols" }
    function finishConnection(success) { connecting = false; connectionFinished(success,success ? "目录已确认可达" : "共享未挂载") }
    function finishStatus(success) { busy = false; if (success) statusUpdated(); statusFinished(success,success ? "状态读取完成" : "状态读取失败") }
}
