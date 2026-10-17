import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml/Next" as Next
Item {
    id: scene
    width: 1144; height: 752
    Next.GlassTheme { id: testTheme }
    MockAiController { id: control }
    QtObject {
        id: fakeBackend
        property var writes: ({})
        function setUiSetting(key,value) { const next = Object.assign({}, writes); next[key] = value; writes = next; return true }
    }
    Component { id: settingsComponent; Next.SystemSettingsPage { theme: testTheme; controller: control; backend: fakeBackend } }
    TestCase {
        name: "AiSystemSettings"
        when: windowShown
        property var page
        function init() {
            failOnWarning(/Binding loop|TypeError|ReferenceError|Unable to assign/)
            control.reset(); fakeBackend.writes = ({}); testTheme.isDark = true; testTheme.followSystem = false; testTheme.reducedTransparency = false
            page = createTemporaryObject(settingsComponent,scene,{width:1144,height:752}); verify(page); wait(30)
        }
        function item(name) { const found = findChild(page,name); verify(found !== null,name); return found }
        function enter(field,text) { field.forceActiveFocus(); field.selectAll(); keyClick(Qt.Key_Backspace); for(let i=0;i<text.length;++i) keyClick(text.charAt(i)) }
        function showModel() { mouseClick(item("settings-model-tab")); wait(20) }
        function test_promptFolderEntry() {
            showModel()
            compare(item("settings-prompt-path").text, control.promptFilePath)
            verify(item("settings-open-prompts").enabled)
            page.controller = null
            verify(!item("settings-open-prompts").enabled)
        }
        function test_preserveAppearanceSettings() {
            mouseClick(item("settings-dark")); compare(fakeBackend.writes["next/appearance/dark"],false); compare(fakeBackend.writes["next/appearance/followSystem"],false)
            mouseClick(item("settings-follow-system")); compare(fakeBackend.writes["next/appearance/followSystem"],true)
            mouseClick(item("settings-reduced-transparency")); compare(fakeBackend.writes["next/appearance/reducedTransparency"],true)
        }
        function test_configurationAndSecretLifecycle() {
            showModel(); compare(item("settings-endpoint").text,control.baseUrl); compare(item("settings-model").text,control.model)
            compare(item("settings-api-key").text,""); compare(item("settings-api-key").echoMode,TextInput.Password)
            enter(item("settings-api-key"),"fake-test-secret")
            mouseClick(item("settings-save")); compare(control.saves,1); compare(control.lastConfiguration.key,"fake-test-secret")
            verify(page.settingsPending); compare(item("settings-api-key").text,"fake-test-secret")
            verify(!item("settings-save").enabled); verify(!item("settings-clear-key").enabled); verify(!item("settings-test").enabled)
            control.completeSettings(); verify(!page.settingsPending)
            compare(item("settings-api-key").text,""); verify(item("settings-key-status").text.indexOf("fake-test-secret") < 0)
            compare(Object.keys(fakeBackend.writes).length,0)
            mouseClick(item("settings-clear-key")); compare(control.clearCalls,1); verify(control.hasApiKey); verify(page.settingsPending)
            control.completeSettings(); verify(!control.hasApiKey); compare(item("settings-api-key").text,"")
        }
        function test_failedSaveAndNoFakeConnectionSuccess() {
            showModel(); control.failSave = true; enter(item("settings-api-key"),"fake-retry-key")
            mouseClick(item("settings-save")); verify(page.settingsPending); verify(!page.noticeError)
            control.completeSettings(); verify(page.noticeError); verify(item("settings-operation-status").text.indexOf("失败") >= 0)
            compare(item("settings-api-key").text,"fake-retry-key")
            mouseClick(item("settings-test")); compare(control.tests,1); compare(control.saves,1); verify(control.connectionBusy)
            verify(!item("settings-save").enabled); verify(!item("settings-test").enabled); verify(!item("settings-clear-key").enabled)
            verify(item("settings-connection-status").text.indexOf("正在") >= 0)
            tryCompare(control,"settingsBusy",false)
            const cancel = item("settings-cancel-test")
            tryCompare(cancel,"visible",true); verify(waitForPolish(cancel.parent))
            mouseClick(cancel); tryCompare(control,"connectionStatus","cancelled"); verify(item("settings-save").enabled)
        }
        function test_missingControllerAndUnavailableCredentials() {
            showModel(); control.credentialAvailable = false; verify(!item("settings-save").enabled); verify(!item("settings-test").enabled)
            page.controller = null; verify(!item("settings-save").enabled); verify(!item("settings-test").enabled)
        }
        function test_destroyOnlyOwnedConnection() {
            showModel(); mouseClick(item("settings-test")); wait(20); verify(control.connectionBusy)
            page.destroy(); wait(30); page = null; compare(control.connectionCancels,1); verify(!control.connectionBusy)
            control.reset(); page = createTemporaryObject(settingsComponent,scene,{width:1144,height:752}); wait(20)
            showModel(); mouseClick(item("settings-test")); wait(20)
            control.connectionRequestId = "other-page-connection"
            page.destroy(); wait(30); page = null; compare(control.connectionCancels,0); verify(control.connectionBusy)
        }
        function test_hideDoesNotCancelConnection() {
            showModel(); mouseClick(item("settings-test")); wait(20)
            page.visible = false; wait(20); compare(control.connectionCancels,0); verify(control.connectionBusy)
            page.visible = true; page.cancelOwnedConnection()
        }
        function test_pendingOwnershipNoDuplicatesAndFinalSuccess() {
            showModel(); enter(item("settings-model"),"new-model"); enter(item("settings-api-key"),"pending-secret")
            mouseClick(item("settings-save")); const accepted = page.ownedSettingsOperationId
            verify(accepted.length > 0); compare(control.saves,1); compare(item("settings-model").text,"new-model")
            page.beginSettingsOperation("save"); compare(control.saves,1)
            control.settingsOperationFinished("another-operation",{ok:true,message:"other success"})
            compare(page.ownedSettingsOperationId,accepted); compare(item("settings-api-key").text,"pending-secret")
            control.settingsOperationFinished(accepted,{ok:true,pending:true,message:"accepted only"})
            verify(page.settingsPending); compare(item("settings-api-key").text,"pending-secret")
            control.credentialStatus = "waiting"; control.credentialMessage = "仍在等待系统确认，尚未完成保存"
            compare(item("settings-operation-status").text,control.credentialMessage)
            verify(!item("settings-save").enabled); compare(item("settings-api-key").text,"pending-secret")
            mouseClick(item("settings-appearance-tab")); compare(page.section,0)
            mouseClick(item("settings-model-tab")); compare(page.section,1); compare(page.ownedSettingsOperationId,accepted)
            control.completeSettings(); verify(!page.settingsPending); compare(item("settings-api-key").text,""); compare(control.model,"new-model")
        }
        function test_failedOperationRetainsEditedValues() {
            showModel(); enter(item("settings-endpoint"),"https://another.example/v1"); enter(item("settings-model"),"edited-model"); enter(item("settings-api-key"),"retry-secret")
            control.failSave = true; mouseClick(item("settings-save")); control.completeSettings()
            verify(page.noticeError); compare(item("settings-api-key").text,"retry-secret")
            compare(item("settings-endpoint").text,"https://another.example/v1"); compare(item("settings-model").text,"edited-model")
            verify(item("settings-save").enabled)
        }
        function test_rejectedOperationIsNotPendingOrSuccessful() {
            showModel(); enter(item("settings-api-key"),"rejected-secret"); control.rejectSettings = true
            mouseClick(item("settings-save")); verify(page.noticeError); verify(!page.settingsPending)
            compare(item("settings-api-key").text,"rejected-secret"); verify(item("settings-save").enabled)
        }
        function test_startupCredentialCheckIsNotMissingKey() {
            showModel(); control.hasApiKey = false; control.credentialAvailable = false; control.settingsBusy = true; control.credentialStatus = "checking"; control.credentialMessage = "正在读取凭据状态"
            compare(item("settings-key-status").text,"正在读取凭据状态"); verify(!item("settings-credential-unavailable").visible); verify(!item("settings-save").enabled); verify(!item("settings-test").enabled)
            control.credentialStatus = "failed"; control.credentialMessage = "系统凭据状态读取失败"; control.settingsBusy = false
            compare(item("settings-key-status").text,"系统凭据状态读取失败")
            verify(item("settings-key-status").text.indexOf("未保存") < 0)
        }
        function test_destroyPendingOperationDoesNotWriteNewPage() {
            showModel(); enter(item("settings-api-key"),"old-page-secret"); mouseClick(item("settings-save")); const operation = page.ownedSettingsOperationId
            page.destroy(); wait(30); page = null
            verify(control.settingsBusy); compare(control.pendingSettingsId,operation)
            page = createTemporaryObject(settingsComponent,scene,{width:1144,height:752}); wait(20); showModel()
            item("settings-api-key").text = "new-page-draft"
            item("settings-endpoint").text = "https://new-page.example"; item("settings-model").text = "new-page-model"
            control.completeSettings(); compare(item("settings-api-key").text,"new-page-draft"); compare(page.notice,"")
            compare(item("settings-endpoint").text,"https://new-page.example"); compare(item("settings-model").text,"new-page-model")
            verify(!page.settingsPending)
        }
        function test_queuedFastCompletionFollowsAcceptedId() {
            showModel(); control.hasApiKey = false; control.autoCompleteSettings = true
            enter(item("settings-api-key"),"fast-worker-secret")
            mouseClick(item("settings-save")); tryCompare(control,"settingsBusy",false)
            compare(control.saves,1); verify(!page.settingsPending); compare(item("settings-api-key").text,"")
            verify(!page.noticeError); verify(control.hasApiKey)
        }
        function test_clearFailureDoesNotPretendDeleted() {
            showModel(); enter(item("settings-api-key"),"keep-draft"); control.failSave = true
            mouseClick(item("settings-clear-key")); verify(page.settingsPending); verify(control.hasApiKey)
            compare(item("settings-api-key").text,"keep-draft"); control.completeSettings()
            verify(page.noticeError); verify(control.hasApiKey); compare(item("settings-api-key").text,"keep-draft")
        }
        function test_probeNotificationsKeepUnsavedConfiguration() {
            showModel()
            const url = "https://draft.example/v1"; const model = "draft-probe-model"
            enter(item("settings-endpoint"),url); enter(item("settings-model"),model); enter(item("settings-api-key"),"draft-probe-key")
            control.settingsChanged()
            compare(item("settings-endpoint").text,url); compare(item("settings-model").text,model)
            mouseClick(item("settings-test")); compare(control.lastConfiguration.url,url); compare(control.lastConfiguration.model,model)
            compare(item("settings-endpoint").text,url); compare(item("settings-model").text,model)
            tryCompare(control,"settingsBusy",false)
            compare(item("settings-endpoint").text,url); compare(item("settings-model").text,model)
            control.completeConnection("succeeded")
            compare(item("settings-endpoint").text,url); compare(item("settings-model").text,model)
            mouseClick(item("settings-test")); tryCompare(control,"settingsBusy",false); wait(20)
            mouseClick(item("settings-cancel-test"))
            compare(item("settings-endpoint").text,url); compare(item("settings-model").text,model)
            compare(item("settings-api-key").text,"draft-probe-key")
            mouseClick(item("settings-test")); tryCompare(control,"settingsBusy",false); control.completeConnection("failed")
            compare(item("settings-endpoint").text,url); compare(item("settings-model").text,model)
            mouseClick(item("settings-save")); compare(control.lastConfiguration.url,url); compare(control.lastConfiguration.model,model)
            control.completeSettings(); compare(control.baseUrl,url); compare(control.model,model)
        }
        function test_clearSuccessKeepsUnsavedEndpointAndModel() {
            showModel(); enter(item("settings-endpoint"),"https://unsaved.example"); enter(item("settings-model"),"unsaved-model")
            mouseClick(item("settings-clear-key")); control.completeSettings()
            compare(item("settings-endpoint").text,"https://unsaved.example"); compare(item("settings-model").text,"unsaved-model")
            verify(!control.hasApiKey); verify(!page.noticeError)
        }
        function test_runningGenerationLocksConfiguration() {
            showModel(); control.busy = true; control.state = "running"
            verify(!item("settings-endpoint").enabled); verify(!item("settings-save").enabled)
            control.busy = false; control.state = "clarification"; verify(!item("settings-save").enabled)
            control.state = "cancelled"; verify(item("settings-save").enabled)
        }
    }
}
