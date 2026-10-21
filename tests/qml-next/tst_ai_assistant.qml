import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import "../../qml/Next" as Next
Item {
    id: scene
    width: 1144; height: 752
    Next.GlassTheme { id: testTheme }
    MockAiController { id: control }
    QtObject {
        id: fakeBackend
        property string copied: ""
        function copyToClipboard(value) { copied = value }
    }
    Component { id: pageComponent; Next.AiAssistantPage { theme: testTheme; controller: control; backend: fakeBackend } }
    SignalSpy { id: settingsSignal; signalName: "requestSettings" }
    SignalSpy { id: resultSignal; signalName: "framingResult" }
    TestCase {
        name: "AiAssistantWorkflow"
        when: windowShown
        property var page
        function init() {
            failOnWarning(/Binding loop|TypeError|ReferenceError|Unable to assign/)
            control.reset(); fakeBackend.copied = ""
            page = createTemporaryObject(pageComponent, scene, {width:1144,height:752}); verify(page); renderPage()
            settingsSignal.target = page; settingsSignal.clear(); resultSignal.target = page; resultSignal.clear()
        }
        function cleanup() { settingsSignal.target = null; resultSignal.target = null }
        function item(name) {
            // ListView creates result delegates on a later render pass.
            tryVerify(function() { return findChild(page,name) !== null },5000,name)
            return findChild(page,name)
        }
        function renderPage() { page.Window.window.update(); verify(waitForRendering(page)) }
        function enter(field,text) { field.forceActiveFocus(); field.selectAll(); keyClick(Qt.Key_Backspace); for(let i=0;i<text.length;++i) keyClick(text.charAt(i)) }
        function prepare() { enter(item("ai-terminal-address"),"010203040506"); enter(item("ai-composer"),"read time") }
        function test_noDemonstrationAddressAndSettings() {
            compare(page.terminal.address, ""); verify(!item("ai-preview-submit").enabled)
            mouseClick(item("ai-open-settings")); compare(settingsSignal.count,1)
            control.hasApiKey = false; prepare(); verify(!item("ai-preview-submit").enabled)
            page.controller = null; verify(!item("ai-preview-submit").enabled)
        }
        function test_terminalSessionAndFrozenResult() {
            prepare(); const originalId = page.terminal.terminalId
            mouseClick(item("ai-preview-submit")); compare(control.submits,1); compare(control.lastTerminal.id,originalId)
            compare(control.lastTerminal.address,"010203040506"); verify(!item("ai-preview-submit").enabled)
            page.addTerminal(); compare(page.terminal.address,""); verify(page.terminal.terminalId !== originalId)
            enter(item("ai-terminal-address"),"AAAAAAAAAAAA")
            control.succeed(); wait(20); verify(page.resultValid)
            verify(item("ai-result-target").text.indexOf("010203040506") >= 0)
            verify(item("ai-result-target").text.indexOf("AAAAAAAAAAAA") < 0)
            renderPage()
            mouseClick(item("ai-copy-result")); compare(fakeBackend.copied,control.result.frameHex)
            mouseClick(item("ai-return-result")); compare(resultSignal.count,1); compare(resultSignal.signalArguments[0][0],control.result.frameHex); compare(resultSignal.signalArguments[0][1],1)
            control.result = {requestId:"old",terminal:control.lastTerminal,frameHex:"GG",apduHex:"05"}; verify(!page.resultValid)
            fakeBackend.copied = ""; verify(!item("ai-copy-result").enabled); compare(fakeBackend.copied,""); compare(resultSignal.count,1)
        }
        function test_clarificationUsesOriginalRequestAndCancel() {
            prepare(); mouseClick(item("ai-preview-submit")); const request = control.requestId
            control.ask(); wait(20); compare(page.clarificationRequestId,request)
            page.addTerminal(); enter(item("ai-composer"),"date and time")
            verify(item("ai-preview-submit").enabled)
            mouseClick(item("ai-preview-submit")); compare(control.answers,1); compare(page.framingDraft,""); compare(page.conversationModel.get(0).body,"read time"); compare(control.lastAnswerId,request); compare(control.lastTerminal.address,"010203040506")
            mouseClick(item("ai-cancel")); compare(control.cancels,1); verify(!page.resultValid)
            control.succeed(request); wait(20); verify(!page.resultValid); verify(findChild(page,"ai-return-result") === null || !findChild(page,"ai-return-result").enabled)
        }
        function test_lateCancelClickKeepsCompletedResult() {
            prepare(); mouseClick(item("ai-preview-submit")); const request = control.requestId
            control.succeed(); wait(20); verify(page.resultValid)
            page.cancelRequest()
            compare(control.cancels,0); compare(page.activeRequestId,request)
            compare(control.state,"succeeded"); verify(page.resultValid)
            compare(item("ai-result-frame").text,control.result.frameHex)
            verify(item("ai-return-result").enabled)
        }
        function test_interpretationNeverSubmits() {
            prepare(); const draft = page.framingDraft
            mouseClick(item("ai-parsing-mode")); enter(item("ai-composer"),"0501014000020000")
            verify(!item("ai-preview-submit").enabled); page.submitRequest(); compare(control.submits,0)
            verify(page.notice.indexOf("尚未实现") >= 0)
            mouseClick(item("ai-framing-mode")); compare(page.framingDraft,draft)
        }
        function test_addressValidationAndTerminalManagement() {
            enter(item("ai-composer"),"read time")
            for(const text of ["","123","GG","1".repeat(34)]) { enter(item("ai-terminal-address"),text); verify(!item("ai-preview-submit").enabled) }
            prepare(); const first = page.terminal.terminalId; page.addTerminal(); enter(item("ai-terminal-address"),"000000000002")
            item("ai-terminal-picker").currentIndex = 0; compare(page.terminal.terminalId,first); compare(item("ai-terminal-address").text,"010203040506")
            mouseClick(item("ai-advanced-toggle")); enter(item("ai-client-address").contentItem,"42"); keyClick(Qt.Key_Return); compare(page.terminal.clientAddress,42)
            page.removeTerminal(); compare(item("ai-terminal-picker").count,1)
        }
        function test_failureAndStaleClarificationAreNotResults() {
            prepare(); mouseClick(item("ai-preview-submit")); control.ask(); wait(20)
            enter(item("ai-composer"),"answer")
            page.clarificationRequestId = "expired"
            page.submitRequest(); compare(control.answers,0); verify(page.notice.indexOf("失效") >= 0)
            control.busy = false; control.statusText = "错误 <b>不应渲染</b>"; control.state = "failed"; wait(20)
            verify(!page.resultValid); compare(item("ai-status").textFormat,Text.PlainText)
            verify(findChild(page,"ai-return-result") === null || !findChild(page,"ai-return-result").enabled)
        }
        function test_exactParameterTextsAndFrozenTarget() {
            prepare(); mouseClick(item("ai-preview-submit")); control.succeed();
            const input = '{\n  "date":{"year":2026,"month":10,"day":14,"hour":15,"minute":0,"second":0},\n  "value":{"representation":"physical","value":"1.23","unit":"A"},\n  "counter":{"type":"long64_unsigned","value":"18446744073709551615"}\n}'
            const normalized = '{\n  "date":{"year":2026,"month":10,"day":14,"hour":15,"minute":0,"second":0},\n  "value":123,\n  "counter":{"type":"long64_unsigned","value":"18446744073709551615"}\n}'
            const result = Object.assign({},control.result)
            result.inputTreeText = input; result.normalizedTreeText = normalized; control.result = result; wait(20)
            const toggle = item("ai-parameters-toggle")
            tryCompare(toggle,"visible",true); verify(waitForPolish(toggle.parent)); renderPage()
            mouseClick(toggle); tryCompare(item("ai-parameters-panel"),"visible",true)
            compare(item("ai-input-parameters").text,input); compare(item("ai-encoded-parameters").text,normalized)
            compare(item("ai-input-parameters").textFormat,TextEdit.PlainText)
            verify(item("ai-encoded-parameters").text.indexOf("18446744073709551615") >= 0)
            page.addTerminal(); enter(item("ai-terminal-address"),"BBBBBBBBBBBB")
            compare(item("ai-input-parameters").text,input); compare(item("ai-encoded-parameters").text,normalized)
            verify(item("ai-result-target").text.indexOf("010203040506") >= 0)
        }
        function test_structuredWarningsAreReadable() {
            prepare(); mouseClick(item("ai-preview-submit")); control.succeed()
            const result = Object.assign({},control.result)
            result.warnings = [{code:"dictionary_duplicates",message:"存在重复定义，沿用最后生效项；Size仅供参考，长度按数据类型与服务结构校验",duplicateOiCount:2,duplicateMemberCount:1}]
            control.result = result; wait(20)
            verify(item("ai-result-warnings").text.indexOf("沿用最后生效项") >= 0)
            verify(item("ai-result-warnings").text.indexOf("重复对象：2") >= 0)
            verify(item("ai-result-warnings").text.indexOf("重复属性/方法：1") >= 0)
            verify(item("ai-result-warnings").text.indexOf("[object Object]") < 0)
            compare(item("ai-result-warnings").textFormat,Text.PlainText)
        }
        function test_destroyOwnedRequestPreventsLateSuccess() {
            prepare(); mouseClick(item("ai-preview-submit")); const request = control.requestId
            page.destroy(); wait(30); page = null
            compare(control.cancels,1); compare(control.state,"cancelled")
            control.succeed(request); compare(Object.keys(control.result).length,0)
            page = createTemporaryObject(pageComponent,scene,{width:1144,height:752}); wait(20)
            verify(!page.resultValid); compare(page.activeRequestId,"")
        }
        function test_destroyClarificationAndPreserveOtherOwner() {
            prepare(); mouseClick(item("ai-preview-submit")); control.ask(); wait(20)
            page.destroy(); wait(30); page = null; compare(control.cancels,1)
            control.reset(); page = createTemporaryObject(pageComponent,scene,{width:1144,height:752}); wait(20)
            prepare(); mouseClick(item("ai-preview-submit"))
            control.requestId = "other-page-request"
            page.destroy(); wait(30); page = null
            compare(control.cancels,0); verify(control.busy)
        }
        function test_hiddenPageKeepsOwnedRequest() {
            prepare(); mouseClick(item("ai-preview-submit")); page.visible = false; wait(30)
            compare(control.cancels,0); verify(control.busy)
            page.visible = true; page.cancelRequest()
        }
        function test_credentialPreparationWaitAndOwnedCancellation() {
            prepare(); control.hasApiKey = false; control.settingsBusy = true
            control.credentialStatus = "checking"; control.credentialMessage = "正在读取安全凭据状态"
            verify(!item("ai-preview-submit").enabled); compare(item("ai-credential-status").text,control.credentialMessage)
            page.submitRequest(); compare(control.submits,0)
            control.settingsBusy = false; control.credentialStatus = "ready"; control.hasApiKey = true
            mouseClick(item("ai-preview-submit")); compare(control.submits,1)
            control.state = "preparing"; control.settingsBusy = true; control.credentialStatus = "checking"
            verify(page.working); verify(item("ai-cancel").visible); verify(!item("ai-preview-submit").enabled)
            renderPage(); mouseClick(item("ai-cancel")); compare(control.cancels,1); verify(control.settingsBusy)
            control.credentialStatus = "waiting"; control.credentialMessage = "仍在等待系统授权，操作尚未物理完成"
            compare(item("ai-credential-status").text,control.credentialMessage); verify(!item("ai-preview-submit").enabled)
            page.submitRequest(); compare(control.submits,1)
            control.credentialStatus = "failed"; control.credentialMessage = "系统凭据读取失败"; control.settingsBusy = false
            compare(item("ai-credential-status").text,"系统凭据读取失败")
        }
        function test_conversationKeepsRoundsAndHistoricalFrames() {
            prepare(); page.submitRequest(); wait(20)
            compare(page.conversationModel.count,2); compare(item("ai-composer").text,"")
            control.ask(); wait(20)
            compare(page.conversationModel.get(1).body,control.clarification)
            enter(item("ai-composer"),"attribute 2"); page.submitRequest(); wait(20)
            compare(page.conversationModel.count,4); compare(page.conversationModel.get(2).body,"attribute 2")
            control.succeed(); wait(20)
            const originalFrame = page.conversationModel.get(3).frameData.frameHex
            page.visible = false; wait(10); page.visible = true
            enter(item("ai-composer"),"another request"); page.submitRequest(); wait(20)
            compare(page.conversationModel.count,6)
            compare(page.conversationModel.get(3).frameData.frameHex,originalFrame)
            page.cancelRequest(); wait(20)
            compare(page.conversationModel.get(5).kind,"cancelled")
            const view = item("ai-work-panel")
            view.followTail = false; view.positionViewAtIndex(3,ListView.Beginning); renderPage()
            mouseClick(item("ai-copy-result-history-3")); compare(fakeBackend.copied,originalFrame)
            mouseClick(item("ai-return-result-history-3")); compare(resultSignal.signalArguments[0][0],originalFrame)
            compare(page.conversationModel.count,6)
        }
        function test_promptPreviewCancelFailureAndEditedSave() {
            prepare(); page.submitRequest(); control.ask(); wait(20); renderPage()
            mouseClick(item("ai-save-prompt-0")); tryCompare(item("ai-prompt-dialog"),"opened",true)
            compare(item("ai-prompt-editor").text,"read time")
            mouseClick(item("ai-prompt-cancel")); tryCompare(item("ai-prompt-dialog"),"opened",false)
            compare(control.savedPrompt,""); verify(!page.conversationModel.get(0).saved)
            page.editPrompt(0); tryCompare(item("ai-prompt-dialog"),"opened",true)
            item("ai-prompt-editor").text = "Use attribute 2 for this object."
            control.failPromptSave = true; mouseClick(item("ai-prompt-save"))
            verify(item("ai-prompt-dialog").opened); verify(item("ai-prompt-error").text.length > 0)
            verify(!page.conversationModel.get(0).saved)
            control.failPromptSave = false; mouseClick(item("ai-prompt-save"))
            tryCompare(item("ai-prompt-dialog"),"opened",false)
            compare(control.savedPrompt,"Use attribute 2 for this object.")
            compare(page.conversationModel.get(0).body,"read time"); verify(page.conversationModel.get(0).saved)
            compare(control.state,"clarification")
        }
        function test_longConversationScrollAndCompactBounds() {
            prepare(); page.submitRequest(); control.succeed(); wait(20)
            for(let i=0;i<12;++i) {
                page.framingDraft = "Request " + i + " <b>literal</b> " + "long text ".repeat(20)
                page.submitRequest(); control.ask(); wait(10); page.cancelRequest(); wait(10)
            }
            compare(page.conversationModel.count,38)
            const view = item("ai-work-panel")
            page.framingDraft = "response while reading history"; page.submitRequest()
            view.forceLayout(); verify(waitForPolish(view)); renderPage()
            tryCompare(view,"atYEnd",true)
            mouseWheel(view,40,100,0,240)
            tryCompare(view,"followTail",false)
            tryVerify(() => !view.moving && !view.flicking)
            verify(waitForPolish(view)); renderPage()
            const readingPosition = view.contentY
            control.ask(); view.forceLayout(); verify(waitForPolish(view)); renderPage()
            verify(Math.abs(view.contentY - readingPosition) < 2)
            page.cancelRequest(); wait(20)
            view.followTail = false; view.positionViewAtBeginning(); verify(waitForPolish(view)); renderPage()
            const before = view.contentY
            page.height = 632; page.width = 920; verify(waitForPolish(view)); renderPage()
            verify(Math.abs(view.contentY - before) < 2)
            mouseClick(item("ai-scroll-bottom")); tryCompare(view,"atYEnd",true)
            for(const name of ["ai-preview-submit","ai-composer","ai-work-panel"]) {
                const node = item(name); const bottom = node.mapToItem(page,node.width,node.height)
                verify(bottom.x <= page.width && bottom.y <= page.height,name+" outside")
            }
        }
        function test_compactLayout() {
            page.width = 920; page.height = 632; renderPage()
            for(const name of ["ai-preview-submit","ai-composer","ai-suggestion-0","ai-suggestion-2","ai-manage-terminals"]) {
                const view = item(name); const bottom = view.mapToItem(page,view.width,view.height)
                verify(bottom.x <= page.width && bottom.y <= page.height,name+" outside")
            }
        }
    }
}
