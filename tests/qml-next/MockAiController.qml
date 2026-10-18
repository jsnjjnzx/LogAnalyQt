import QtQuick
QtObject {
    property string promptFilePath: "/isolated/prompts/ai_698_framing.md"
    property url promptDirectoryUrl: "file:///isolated/prompts"
    property string baseUrl: "https://api.deepseek.com"
    property string model: "mock-model"
    property bool hasApiKey: true
    property bool credentialAvailable: true
    property bool busy: false
    property string state: "idle"
    property string requestId: ""
    property string statusText: ""
    property string errorCode: ""
    property string clarification: ""
    property var result: ({})
    property bool connectionBusy: false
    property string connectionRequestId: ""
    property string pendingProbeReadId: ""
    property int connectionCancels: 0
    property string connectionStatus: "idle"
    property string connectionMessage: "尚未测试连接"
    property bool failSave: false
    property bool rejectSettings: false
    property bool autoCompleteSettings: false
    property bool settingsBusy: false
    property string credentialStatus: "ready"
    property string credentialMessage: ""
    property int settingsSequence: 0
    property string pendingSettingsId: ""
    property var pendingMutation: ({})
    signal settingsOperationFinished(string operationId, var reply)
    property string savedPrompt: ""
    property bool failPromptSave: false
    function appendPromptExperience(text) {
        if (failPromptSave) return {ok:false,message:"文件不可写，原内容保留"}
        savedPrompt = text
        return {ok:true,message:"已加入 698 提示词，下次新组帧生效"}
    }
    property int submits: 0
    property int answers: 0
    property int cancels: 0
    property int saves: 0
    property int tests: 0
    property int clearCalls: 0
    property var lastTerminal: ({})
    property var lastConfiguration: ({})
    property string lastAnswerId: ""
    property var cancelledIds: ({})
    signal settingsChanged()
    function reset() {
        savedPrompt = ""; failPromptSave = false
        baseUrl = "https://api.deepseek.com"; model = "mock-model"; hasApiKey = true; credentialAvailable = true
        busy = false; state = "idle"; requestId = ""; statusText = ""; errorCode = ""; clarification = ""; result = ({})
        connectionRequestId = ""; pendingProbeReadId = ""; connectionCancels = 0; connectionBusy = false; connectionStatus = "idle"; connectionMessage = "尚未测试连接"
        settingsBusy = false; credentialStatus = "ready"; credentialMessage = ""; settingsSequence = 0; pendingSettingsId = ""; pendingMutation = ({}); rejectSettings = false; autoCompleteSettings = false; cancelledIds = ({}); failSave = false; submits = 0; answers = 0; cancels = 0; saves = 0; tests = 0; clearCalls = 0; lastTerminal = ({}); lastConfiguration = ({})
    }
    function submit(text, terminal) {
        if (busy) return ""
        submits++; lastTerminal = JSON.parse(JSON.stringify(terminal)); requestId = "request-" + submits
        result = ({}); busy = true; state = "running"; statusText = "正在处理需求"; return requestId
    }
    function answerClarification(id, answer) {
        if (id !== requestId || state !== "clarification") return false
        lastAnswerId = id; answers++; busy = true; state = "running"; clarification = ""; return true
    }
    function cancel() { const ids = Object.assign({}, cancelledIds); ids[requestId] = true; cancelledIds = ids; cancels++; busy = false; state = "cancelled"; statusText = "已取消"; result = ({}) }
    function ask() { busy = false; clarification = "请补充需要读取的属性。"; state = "clarification" }
    function succeed(id) {
        if (cancelledIds[id || requestId]) return
        result = {requestId: id || requestId, serviceCode: "GET_NORMAL", serviceName: "读取单个属性",
            terminal: JSON.parse(JSON.stringify(lastTerminal)), apduHex: "0501014000020000", frameHex: "68160043010000160016", summary: "合成控制器结果", inputTreeText: '{\n  "oad": {"oi":"4000","attrId":2,"index":0}\n}', normalizedTreeText: '{\n  "oad": {"oi":"4000","attrId":2,"index":0}\n}', treeValue: {oad:{oi:"4000",attrId:2,index:0}}, warnings:[]}
        busy = false; state = "succeeded"; statusText = "本地构建完成"
    }
    function acceptSettings(kind, mutation) {
        if (rejectSettings || settingsBusy) return {ok:false,code:"busy",message:"安全凭据操作忙，请稍后重试"}
        pendingMutation = Object.assign({kind:kind}, mutation)
        pendingSettingsId = "settings-" + (++settingsSequence)
        settingsBusy = true; credentialStatus = "checking"; credentialMessage = "正在访问安全凭据存储"
        settingsChanged()
        if (autoCompleteSettings) Qt.callLater(completeSettings)
        return {ok:true,pending:true,operationId:pendingSettingsId,code:"pending",message:kind === "save" ? "正在保存设置…" : "正在清除密钥…"}
    }
    function saveConfiguration(url, selectedModel, key) {
        saves++; lastConfiguration = {url:url,model:selectedModel,key:key}
        return acceptSettings("save",lastConfiguration)
    }
    function clearApiKey() { clearCalls++; return acceptSettings("clear",{}) }
    function completeSettings() {
        const id = pendingSettingsId
        if (!id) return
        if (!failSave) {
            if (pendingMutation.kind === "save") {
                baseUrl = pendingMutation.url; model = pendingMutation.model
                if (pendingMutation.key) hasApiKey = true
            } else hasApiKey = false
        }
        settingsBusy = false; credentialStatus = "ready"; credentialMessage = ""; pendingSettingsId = ""; pendingMutation = ({})
        settingsChanged()
        settingsOperationFinished(id,failSave ? {ok:false,code:"settings_write_failed",message:"设置写入失败"} : {ok:true,code:"",message:"设置操作完成"})
    }
    function testConnection(url, selectedModel, key) {
        tests++; lastConfiguration = {url:url,model:selectedModel,key:key}; connectionRequestId = "connection-" + tests; connectionBusy = true
        connectionStatus = "testing"; connectionMessage = "正在测试实际工具响应"
        pendingProbeReadId = connectionRequestId; settingsBusy = true; credentialStatus = "checking"; settingsChanged()
        Qt.callLater(completeProbeCredentials)
        return connectionRequestId
    }
    function completeProbeCredentials() {
        if (!pendingProbeReadId) return
        pendingProbeReadId = ""; settingsBusy = false; credentialStatus = "ready"; settingsChanged()
    }
    function completeConnection(status) {
        completeProbeCredentials(); connectionBusy = false; connectionRequestId = ""; connectionStatus = status
        connectionMessage = status === "succeeded" ? "连接测试成功" : "连接测试失败"; settingsChanged()
    }
    function cancelConnectionTest() { connectionCancels++; connectionRequestId = ""; connectionBusy = false; connectionStatus = "cancelled"; connectionMessage = "连接测试已取消"; settingsChanged() }
}
