import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import LogAnalyQt as Original
import LogAnalyQt.Next as Next
Item {
    id: scene
    width: 1280; height: 900
    Next.GlassTheme { id: testTheme }
    Window { id: ownerWindow; visible: false; width: 1280; height: 900 }
    QtObject {
        id: backend
        property var popupResultModel: []
        property int protocolType: 0
        function getUiSetting(key,fallback) { return fallback }
        function setUiSetting(key,value) { return true }
    }
    Component { id: syncMock; MockSyncManager {} }
    Component { id: nextSync; Next.ProtocolSyncDialog { theme: testTheme } }
    Component { id: oldSync; Original.ProtocolSyncDialog { theme: null } }
    Component { id: nextSsh; Next.LiveLogDialog { theme: testTheme; backend: backend } }
    Component { id: oldSsh; Original.LiveLogDialog { theme: null; backend: backend } }
    TestCase {
        name: "RemoteSecurityViews"
        when: windowShown
        property var page
        property var manager
        function init() { failOnWarning(/Binding loop|TypeError|ReferenceError|Unable to assign/); page = null; manager = null }
        function cleanup() { if (page) { page.close(); page.destroy(); page = null }; if (manager) { manager.destroy(); manager = null }; wait(30) }
        function variants() { return [{tag:"next",modern:true},{tag:"original",modern:false}] }
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
        function openSync(data) { manager = syncMock.createObject(scene); make(data.modern?nextSync:oldSync,{syncManager:manager}) }
        function connectSync() {
            mouseClick(item("protocol-sync-connect")); compare(manager.connectionCalls,1); verify(!page.remoteReady)
            manager.finishConnection(true); compare(manager.statusCalls,1); manager.finishStatus(true); wait(20); verify(page.remoteReady)
        }
        function test_promptSyncSelection_data() { return variants() }
        function test_promptSyncSelection(data) {
            openSync(data)
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
        function test_syncFinderAndPendingResults_data() { return variants() }
        function test_syncFinderAndPendingResults(data) {
            openSync(data); verify(!item("protocol-sync-path").readOnly)
            verify(findChild(page,"protocol-sync-legacy-password") === null); verify(findChild(page,"protocol-sync-legacy-user") === null)
            verify(!item("protocol-sync-all").enabled); connectSync()
            mouseClick(item("protocol-sync-all")); compare(manager.syncCalls,1); compare(page.pendingAction,"sync")
            verify(!item("protocol-sync-result").visible); verify(!item("protocol-sync-all").enabled)
            manager.busy = false; manager.syncFinished({uploaded:["a.xml"],downloaded:[],skipped:[],failed:["b.xml"],errors:[{file:"b.xml",error:"远端拒绝写入"}]}); wait(20)
            verify(item("protocol-sync-result").visible); verify(page.statusIsError)
            const text = item("protocol-sync-result-text").text
            verify(text.indexOf("a.xml")>=0); verify(text.indexOf("b.xml")>=0); verify(text.indexOf("远端拒绝写入")>=0)
        }
        function test_syncCancelAndDiffFailure_data() { return variants() }
        function test_syncCancelAndDiffFailure(data) {
            openSync(data); mouseClick(item("protocol-sync-connect")); wait(20)
            mouseClick(item("protocol-sync-connect")); compare(manager.cancelCalls,1); verify(!page.remoteReady)
            manager.connectionFinished(true,"late mount"); compare(manager.statusCalls,0)
            page.startConnection(); manager.finishConnection(true); manager.finishStatus(true)
            page.requestDiff("698_protocol.xml"); compare(manager.diffCalls,1); compare(page.diffData.length,0)
            manager.finishStatus(false); verify(page.statusIsError); compare(page.diffData.length,0)
            page.startConnection(); manager.finishConnection(true); manager.finishStatus(true)
            page.requestSync(""); page.close(); verify(manager.cancelOperationCalls>0)
            manager.busy = false; manager.syncFinished({uploaded:["late.xml"],errors:[]}); verify(!item("protocol-sync-result").visible)
        }
        function test_sshDraftAndRealCredentialCompletion_data() { return variants() }
        function test_sshDraftAndRealCredentialCompletion(data) {
            make(data.modern?nextSsh:oldSsh); const ssh = item("ssh-terminal-backend")
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
        function test_xmlDiffPreservesLiteralMarkup_data() { return variants() }
        function test_xmlDiffPreservesLiteralMarkup(data) {
            openSync(data); connectSync(); page.requestDiff("698_protocol.xml")
            manager.busy = false; manager.diffFinished("698_protocol.xml",[{leftType:"equal",rightType:"equal",leftText:"<b>literal XML</b>",rightText:"<Item Name=\"<b>\" />"}]); wait(30)
            const window = item("protocol-diff-window")
            const left = visualItem(window.contentItem,"protocol-diff-left-text"); const right = visualItem(window.contentItem,"protocol-diff-right-text")
            verify(left !== null); verify(right !== null)
            compare(left.text,"<b>literal XML</b>"); compare(left.textFormat,Text.PlainText)
            compare(right.text,"<Item Name=\"<b>\" />"); compare(right.textFormat,Text.PlainText)
        }
        function test_legacyPlatformSyncContract_data() { return variants() }
        function test_legacyPlatformSyncContract(data) {
            manager = syncMock.createObject(scene,{usesSystemSmb:false})
            make(data.modern?nextSync:oldSync,{syncManager:manager})
            verify(item("protocol-sync-path").readOnly)
            compare(item("protocol-sync-legacy-user").text,"legacy-user"); compare(item("protocol-sync-legacy-password").echoMode,TextInput.Password)
            mouseClick(item("protocol-sync-connect")); compare(manager.saveCredentialCalls,1)
            manager.finishConnection(true); compare(manager.statusCalls,1)
            mouseClick(item("protocol-sync-all")); compare(manager.syncCalls,1); verify(item("protocol-sync-result").visible)
            verify(item("protocol-sync-result-text").text.indexOf("legacy.xml")>=0)
        }
        function test_sshPendingIdentityCancelDoesNotConnectLater_data() { return variants() }
        function test_sshPendingIdentityCancelDoesNotConnectLater(data) {
            make(data.modern?nextSsh:oldSsh); const ssh = item("ssh-terminal-backend")
            edit("ssh-host","wait.example"); edit("ssh-password-draft","temporary-wait-password")
            page.beginSshAction("connect"); verify(page.pendingSshAction !== null)
            page.connectAction(); verify(page.pendingSshAction === null)
            ssh.finishRefresh(); wait(30); compare(ssh.connectCalls,0)
            ssh.credentialBusy = true; page.beginSshAction("connect"); page.close(); ssh.finishRefresh(); wait(30)
            compare(ssh.connectCalls,0)
        }
        function test_sshFreshKeyAndChangedKeyFailure_data() { return variants() }
        function test_sshFreshKeyAndChangedKeyFailure(data) {
            make(data.modern?nextSsh:oldSsh); const ssh = item("ssh-terminal-backend")
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
        function test_sshLegacyPasswordContract_data() { return variants() }
        function test_sshLegacyPasswordContract(data) {
            make(data.modern?nextSsh:oldSsh); const ssh = item("ssh-terminal-backend"); ssh.usesSecureCredentials = false; wait(20)
            compare(item("ssh-password-draft").text,"must-not-render-saved-password")
            verify(!item("ssh-save-password").visible); verify(!item("ssh-clear-password").visible)
            edit("ssh-host","legacy-target.example"); compare(item("ssh-password-draft").text,"must-not-render-saved-password")
            mouseClick(item("sshConnectAction")); compare(ssh.connectCalls,1)
            compare(ssh.password,"must-not-render-saved-password"); compare(ssh.saveCalls,0); compare(ssh.clearCalls,0)
        }
        function test_sshHostKeyExplicitAcceptAndCloseReject_data() { return variants() }
        function test_sshHostKeyExplicitAcceptAndCloseReject(data) {
            make(data.modern?nextSsh:oldSsh); const ssh = item("ssh-terminal-backend")
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
