# 通用 698 AI 组帧：实现合同

状态：阶段 1 架构合同；2026-10-14 核对当前源码与 DeepSeek 官方文档。此文档不表示已经接入模型、通过组帧验证或完成设备验收。以下名字是后续并行实施的统一接口；修改合同须先通知整合者。

## 1. 范围与已有边界

自然语言 → 运行时查询本地服务/对象定义 → 模型组织 `serviceCode + treeValue` → 本地强校验 → 本地 APDU 编码与完整帧封装 → 页面展示、复制。对时、设地址只是验收用例，不进入生产代码的业务分支或专用工具。覆盖范围由本地 `Prot698ServiceSchema::serviceTypes()`、服务构建器、已加载的 698 XML 定义与数据编码器共同决定，不能声称支持尚未实现的 698 标准全部功能。

保留用户已选第 2 版 AI 页的布局、主题、终端栏和输入框。新增可用的组帧流程与结果区域，系统设置 page-9 增加“大模型接入”。“报文解释”保留为明确不可用的占位，不请求模型。本阶段没有发送设备功能，也不交付 SDK、代理服务器、插件框架、知识库或报文解释。

已检查的接入点：

| 当前源码 | 当前能力/后续影响 |
| --- | --- |
| `qml/Next/AiAssistantPage.qml` | 预览本地 ListModel、示例终端、输入和提示；当前没有网络/后端连接。替换预览流程，不能把演示终端当成用户配置。 |
| `qml/Next/Main.qml` | page-14 挂 AI 页，page-9 是系统外观设置；绑定下述控制器，page-9 采用可滚动内容避免新增字段溢出。 |
| `src/core/LogBackend.{h,cpp}` | 已有服务/对象查询、APDU/完整帧构建、UI 设置；增加一个控制器只读属性，保持旧组帧调用兼容。 |
| `src/common/AppSettings.h` | `appSettings()` 在可写数据根目录保存便携 INI；只用于普通配置，不能写 API Key。 |
| `src/core/SecurityStateStore.*` | Windows DPAPI 授权状态及冗余副本；不是 API Key 存储，也不在 macOS 支持该用途。 |
| `src/core/UpdateChecker.*` / CMake | Qt Network 已存在，不新增 HTTP 库；不要借用更新器 manager 或其日志/重定向逻辑。 |
| `src/core/main.cpp` / QML 模块 | LogBackend 为 QML 注册类型；AI 控制器由 backend 拥有，独立 UI 预览可无 backend 安全加载。 |
| `tests/` / CMake `BUILD_TESTING` | 现有独立 C++ 可执行测试和 Python 检查；新增少量边界测试，可注入网络替身且不依赖真实 Key。 |

当前服务有 18 类，包括读取/设置/执行的单项与列表、记录、下一块、代理及对应响应。使用列表返回值，不在模型层另存固定业务白名单。新增服务若缺完整输入定义或校验器，要在能力列表标出具体不支持的 kind，不能让模型自行猜字段。

## 2. 模块与稳定 C++ 接口

采用四个小职责单元，模型/控制器/凭据放 `src/core/ai/`，协议工具放 `src/protocols/698_prot/Ai698Tools.*`，无需引入通用 provider/agent 框架。

1. `Ai698Tools`：本地目录、完整输入定义、严格草稿校验、确定性构建；不联网、不接触密钥、不发送设备。
2. `DeepSeekClient`：一个非流式 Chat Completions HTTP 请求及取消/解析；不运行工具，不理解业务。
3. `AiCredentialStore`：系统安全存储的读/写/删；只有 C++ 使用。
4. `AiFramingController`：设置界面 facade、请求生命周期、有限工具循环、澄清上下文和结果；不重新实现协议编码。

阶段 3 的 `AiModelSettings` 是供控制器持有的薄 C++ 设置助手，复用 `appSettings` 文件位置，集中地址规范化、模型/Key格式校验、endpoint绑定与保存回滚；不另加 QML singleton 或 provider 架构层。`AiOperationResult` 提供 `{ok,code,message}` 到 QVariantMap 的转换。

`LogBackend` 增加 `Q_PROPERTY(AiFramingController* aiFraming READ aiFraming CONSTANT)`。控制器 parent 为 backend；QML 统一使用 `backend.aiFraming`。注册控制器为 QML 不可自行创建类型或采用项目已有等效注册方式；不能让 QML 创建第二套带 Key 的实例。

以下是契约签名而非本阶段代码；读写属性以各自 `...Changed` 信号通知。

```cpp
// Ai698Tools.h：域实现者与控制器之间只使用 JSON 值，保持 JSON 类型。
struct AiFrameContext {
    QString requestId;
    QJsonObject terminal;     // 第 3 节终端快照
    QDateTime submittedAt;   // 带系统时区/offset 的提交瞬间
    int piidSequence;        // 控制器从本地 PIID 分配器取得，0..63
    QString definitionId;   // 本次加载定义的内容摘要/修订标识
};

class Ai698Tools {
public:
    QJsonArray definitions() const; // DeepSeek tools[]，5 个函数定义
    QJsonObject invoke(const QString &name, const QJsonObject &arguments,
                       const AiFrameContext &context) const;
};

// DeepSeekClient 是 QObject；每个 client 同时最多一个活动请求。
// manager 为可选构造注入，不拥有外部 manager；空则自己创建。
DeepSeekClient(QNetworkAccessManager *manager = nullptr, QObject *parent = nullptr);
void complete(const QString &requestId, const QUrl &baseUrl,
              const QByteArray &apiKey, const QJsonObject &body);
void cancel(const QString &requestId);
// 信号携带本地 requestId；message 是经过 envelope 校验的原 assistant message。
void completed(QString requestId, QJsonObject message, QString finishReason);
void failed(QString requestId, QString code, QString safeMessage);

// AiCredentialStore 不暴露到 QML；空值与读失败必须可区分。
bool contains(const QString &profileId, bool *exists, QString *error = nullptr) const;
bool read(const QString &profileId, QByteArray *key, QString *error) const;
bool write(const QString &profileId, const QByteArray &key, QString *error);
bool remove(const QString &profileId, QString *error);
```

控制器作为唯一 QML 接口：

| 属性 | 类型/含义 |
| --- | --- |
| `baseUrl`, `model` | QString，只读，已保存的普通配置；默认 `https://api.deepseek.com` 与 `deepseek-flash`，用户可编辑模型 ID。 |
| `hasApiKey`, `credentialAvailable` | bool；只表示是否存在/平台存储是否可用，永不返回 Key、Key 片段或可逆封装值。 |
| `busy` | bool，`running`/`validating`/`building` 时为 true；澄清等待时为 false。 |
| `state` | QString：`idle/running/clarification/validating/building/succeeded/failed/cancelled`。 |
| `requestId` | QString，本地 UUID；不把供应商响应 id 当作取消或状态关联 id。 |
| `statusText`, `errorCode` | QString，可展示的本地摘要和稳定错误码，不含原始响应/敏感信息。 |
| `clarification` | QString；模型需要用户补充的单个清晰问题。 |
| `result` | QVariantMap；第 6 节成功对象，只有本地 build 成功才能填写。 |
| `connectionBusy`, `connectionStatus`, `connectionMessage` | bool/QString/QString；与生成结果状态分开，状态为 `idle/testing/succeeded/failed/cancelled`。 |

```cpp
Q_INVOKABLE QVariantMap saveConfiguration(const QString &baseUrl,
                                         const QString &model,
                                         const QString &newApiKey);
Q_INVOKABLE QVariantMap clearApiKey();
Q_INVOKABLE QString testConnection(const QString &baseUrl,
                                   const QString &model,
                                   const QString &temporaryApiKey);
Q_INVOKABLE void cancelConnectionTest();
Q_INVOKABLE QString submit(const QString &text, const QVariantMap &terminal);
Q_INVOKABLE bool answerClarification(const QString &requestId, const QString &answer);
Q_INVOKABLE void cancel();
```

保存/清除在阶段8改为异步：受理返回 `{ok:true,pending:true,code:"pending",operationId,message}`，最终 `settingsOperationFinished(operationId,{ok,code,message})` 才表示事务结果；受理不能显示保存成功或清空密钥输入。字符串方法返回已接收操作的 UUID；输入错误返回空字符串并更新相关错误属性，绝不能启动 HTTP。`submit` 忙时拒绝为 `busy`，不隐式替换；UI 显示取消按钮。`testConnection` 与生成互斥，免得用户修改配置影响活动请求。`cancel()` 对澄清等待也有效。

## 3. 终端、上下文与地址所有权

QML `submit(text, terminal)` 传入以下精确字段，控制器验证后深拷贝：

```json
{
  "id": "用户终端的稳定本地ID",
  "name": "终端名称",
  "address": "000000000001",
  "addressType": 0,
  "logicAddress": 0,
  "clientAddress": 0
}
```

`address` 是页面显示的十六进制物理地址，1..16 字节，偶数长度，禁止非十六进制字符；提交边界只容许 trim 和统一大写，不自动删除任意非法字符。`addressType` 0..3、`logicAddress` 0..1、`clientAddress` 0..255，必须实际整数；不能以 toInt 把字符串/小数/空值变零。name/id 限长，未知字段拒绝。示例终端只用于预览，实际应用首次无配置必须引导填写真实终端。终端列表和选中项仅保持当前会话内状态；本轮只持久化模型接入配置及安全凭据。

`requestId`、终端快照、提交时间/系统时区、定义修订和模型配置在 submit 时冻结。PIID 在后台凭据读取成功后、首轮模型请求前由本地分配器分配一次并冻结；凭据失败不消耗 PIID。活动时编辑终端栏不能更改在途帧。澄清续问继续使用原快照；需要更换终端或重新解释“现在”时取消并重新提交。定义同步替换期间必须串行化或检测修订变化后报 `context_changed`，不能一次会话混用两个 XML 版本。用户已确认重复定义沿用当前 last-wins，并向用户提示发生覆盖；AI 查询和构建必须使用同一份最终生效定义。

模型不接收可写 frameOptions，不允许通过工具参数或 treeValue 覆盖最外层服务器地址、SA 类型、逻辑地址或 CA。即使用户自然语言要求换目标，也先要求在终端栏修改后重新提交。业务中的“设置通信地址”是对象属性值，仍按对象定义处理，不能修改外层地址快照。代理服务的内层 TSA 是业务字段，来自用户明确要求；不能误当成外层地址或从当前终端暗填。

现有 `defaultClientRequestOptions(displayAddress)` 翻转显示地址，`optionsFromMap(serverAddrHex)` 则直接按线序读取。AI 路径统一用前者得到 serverAddr，然后仅赋已验证的 SA/逻辑地址/CA，避免混用导致双反转。帧方向按服务目录本地派生：客户请求 `dir=false, prm=true`；服务器响应 `dir=true, prm=false`。现有 FrameHeaderPanel 给响应默认 `funcCode=1`，但解析器将 1 解释为链路管理、3 解释为用户数据，因此它不能成为未经验证的合同真值；协议实施者必须用独立规范/例帧核对 18 类服务的 APDU 编码和帧控制域，确认后再构建对应完整帧，未确认前不宣称响应可用。`split=false, scramble=false`。不把服务器响应一概套客户请求帧头。

默认 PIID 由本地分配器一次分配并冻结，默认普通优先级、ACD=false、不带时间标签；这是运输层的明确默认，不是缺少业务参数时填零。PIID 在模型 treeValue 中出现必须与快照匹配，不能让模型重复分配。用户确实指定其他优先级/时间标签时由定义与本地校验处理。`get_definition` 返回这份已冻结默认值，让模型无需猜测。

自然时间由提交时间辅助理解；系统时区不等于终端时区。日期、秒、时区/终端墙上时间语义或枚举业务含义不明确时，先澄清。业务字段没有隐式默认；不能从机器本地时间、示例值或编码器的 toInt 默认值补齐。

## 4. 五个通用工具

工具外层参数均为 object，`additionalProperties:false`，由本地按工具名白名单分派，绝不能反射调用任意 Q_INVOKABLE、shell、文件或网络。每个 function schema 用稳定字段；服务/对象树的可变结构放在返回的 `inputSchema`，不是把所有业务写入系统 prompt。

| 工具名 | 精确输入 | 返回 data 核心字段 |
| --- | --- | --- |
| `list_services` | `{}` | `items`：serviceTypes 当前完整目录，附 `definitionId`、`buildSupported`、`unsupportedReasons`、`verificationStatus`。 |
| `search_objects` | `{query:string, offset:integer, limit:integer}` | `items,total,offset,hasMore`；query 最长 200，offset≥0，limit 1..50。覆盖名称/OI/属性和方法关键词，无匹配与加载失败不同。 |
| `get_definition` | service：`{kind:"service",serviceCode:string}`；object：`{kind:"object",oi:string,memberId:integer,isMethod:boolean,dataIndex:integer}` | service 模式：完整服务 `inputSchema`、kind 说明、默认 PIID、示例形状；object 模式：对象名称/属性方法名称与权限/类型/单位/scaler/枚举/children/真实输入形状。两者返回可构建状态与验证证据状态。 |
| `validate_draft` | `{serviceCode:string, treeValue:object}` | `valid,errors,definitionId`，只校验、不分配 PIID、不更改地址/生成状态。 |
| `build_frame` | `{serviceCode:string, treeValue:object}` | 第 6 节本地结果；重新完整校验，使用冻结上下文确定性构建。无 endpoint、terminal 或任意 frameOptions 参数。 |

`get_definition` 各分支只允许自己的字段，不要求另一分支的无关键填空。serviceCode 必须非空；object 的 OI 为 4 位 hex，memberId/dataIndex 在类型允许范围内；memberId=0 表示返回对象全部属性/方法索引且 dataIndex 必须为 0，不返回不存在的成员默认值。调用方参数是严格类型，不通过错误类型猜模式。详细数据结构来自 `Prot698OadManager`，但 `getOadSchema` 本身不含对象/属性名称与读写权限，工具须补齐已有 manager 数据，避免模型只能看到一个裸 dataDef。

能力对象固定为 `buildSupported:boolean, unsupportedReasons:array, verificationStatus:"pending"|"local_verified"|"independent_verified"`。是否实现与验证证据分开，由本地赋值；没有设备联调本身不关闭已明确的确定性构建，未实现或线格式尚不确定的分支必须关闭并返回具体路径/原因。18 个服务仍全部可发现，不能把有歧义的分支藏起来。

工具结果统一 JSON envelope：

```json
{"ok":true,"data":{},"error":null}
```

```json
{
  "ok": false,
  "data": null,
  "error": {
    "code": "invalid_arguments",
    "message": "属性编号必须是整数",
    "path": "$.treeValue.setItem.oad.attrId"
  }
}
```

`validate_draft` 的 errors 是 `{code,message,path}` 列表；缺业务字段可用 `missing_field`，便于模型形成用户问题。错误只含本地可控说明，不回传路径、凭据或原始网络错误。

完整 `inputSchema` 必须描述现有服务树中各 kind，包括 OAD/OMD、PIID/ACD、TimeTag、set/action item、列表、RSD/RCSD/CSD/MS、代理/记录/响应/选择分支；单有 `schemaForService()` 的高层 kind 不够。按现有树字段与编码器复用，以下冻结形状须同时体现在 inputSchema 与本地递归验证，不改变旧手动入口的 raw 值语义：

| 值类 | 精确 AI 形状与规则 |
| --- | --- |
| 静态非数值 | 沿用已有形状；array/structure 是按定义顺序的 JSON 数组，structure 字段数精确。 |
| 动态 Data | `{type:"unsigned",value:1}`；动态容器如 `{type:"structure",value:[{type:"unsigned",value:1},{type:"null",value:null}]}`。规范化 type 白名单，显式 Null 只能 `value:null`。 |
| option | `{optionIndex:0,value:123}`；index 是冻结定义中 children 候选的零起始位置。定义同时返回位置、TypeOption、Name、Scaler、Selected；Selected 不自动选，显式选择按 TypeOption 编码且不额外追加 choice 字节。 |
| 有单位/Scaler 的数值叶子 | `{representation:"raw",value:123,unit:"A"}` 或 `{representation:"physical",value:"1.23",unit:"A"}`。unit 精确匹配，无单位为空串；raw 为精确整数/超安全范围十进制整数字符串，physical 必须十进制字符串。固定整数按 physical=raw×10^scaler 精确换算，不能整除、溢出、单位不符均拒绝；浮点/不明 scaler 不猜换算。 |
| 无单位/Scaler 整数、枚举 | 整数保持原始值，枚举仅按整数集合验证，不套物理倍率。动态 Data 的数值是显式类型的线值；若来自有单位候选/静态定义仍用上述包装。 |
| 方法响应 | `hasData=true` 时要求 typed Data，不能套用方法入参定义；false 时禁止 dataValue。 |

定义/示例必须与当前输入契约可验证，不允许示例里出现业务默认值暗示模型补齐。不明属性特征位、非零 index、ATCION 等定义局部返回 unsupported_definition，不猜改字典、不阻断其他可用对象。

### 本地验证要求

信任边界保留 QJsonValue 类型，先验证再转换 QVariant。对工具 JSON 和 treeValue 检查：必填、未知键、选择分支、容器类型、嵌套深度/数量、整数必须有限无小数、枚举、数值范围、hex 格式/长度、真实日历与时间、对象/属性/方法存在性及可写/可执行权限、数组/结构的定义匹配、单位与 scaler。64 位整数超出 JSON 安全整数范围时使用定义明确的十进制字符串表示并以 Qt 的成功标志转换，不能经 double 丢精度。未知类型返回 `unsupported_definition`，不猜编码。

现有 `validateTreeValue()` 只检查少量字段，DataEncoder 多处 toInt/cast 可截断；它们仍是编码入口，但不能代替 AI 边界校验。拒绝日期 2 月 30 日、缺少秒、bool 当整数、"abc" 转零、整数溢出和错误枚举。校验路径必须告诉用户/模型哪个字段有问题。AI 路径禁止尚未结构化的 opaque/rawHex/rowsHex 透传，即使模型自称用户提供也拒绝；真正已定义的 octet_string 等字节字段仍按严格 hex/长度校验。未来若需要 opaque 导入，须另行设计可信用户字段绑定，本阶段不增加。

为了单点修复，协议类型的实质范围规则可下沉复用到现有编码器，但不得无故改变旧手动 UI 的容错行为；凡变更共用编码器必须检索旧调用者并加入对应回归用例。禁止只在 prompt 中写“请遵守范围”作为验证。

## 5. DeepSeek HTTP 与工具循环

使用 Qt6 Network 的非流式 `POST {baseUrl}/chat/completions`，Bearer Key 只在 C++ 请求头。baseUrl 是服务根地址或 `/v1` 根地址；规范化一个尾部斜线，不重复追加 `/v1`，拒绝粘贴已含 `/chat/completions` 的值并给出清晰提示。HTTPS 必须正常验证证书，禁止 ignoreSslErrors；禁自动重定向，避免 Authorization 被转发。URL 禁 userinfo、query、fragment；生产实现没有 HTTP/localhost 测试开关或忽略证书选项。测试替身通过构造注入 manager，不放宽生产 endpoint。

设置 URL 为用户明确输入的兼容服务地址，不硬编码某个公司代理；模型不能提供或修改 URL。网络错误摘要不包含完整 Authorization、输入文本、响应正文或带隐私路径。配置使用哪个 URL，就明确显示给用户；程序不会读取环境变量或其他项目文件中的 Key。

每轮请求 body：

```json
{
  "model": "配置的模型ID",
  "stream": false,
  "thinking": {"type": "disabled"},
  "max_tokens": 4096,
  "messages": [],
  "tools": [],
  "tool_choice": "auto"
}
```

首轮 messages 含固定系统边界、已冻结运输层默认和定义修订，以及用户需求；不发送完整日志/文件，也不一次上传整个 XML。仅上传当前需求和查到的必要定义片段。正常工具循环不同时开启 `response_format`，不依赖 beta strict。模型说明性 content 永远不能直接成为十六进制结果。`stop` 且无工具调用视为澄清文字；为空则 `empty_response`；文字只作问题/说明，不认为已构建成功。

非流式响应读取 `choices[0].message`；需校验 envelope、assistant role 和 finish_reason。`message.content` 可以为 null；`tool_calls[]` 中 `id` 非空不重复、type 必须 function、`function.name` 在白名单、`function.arguments` **是字符串中的 JSON object**，需二次 parse 并逐字段验证。不要错误地当它已经是 JSON object。一个响应可有多个调用；保存完整 assistant message，再按原顺序串行运行、逐个追加 `{"role":"tool","tool_call_id":原id,"content":序列化的工具结果JSON字符串}`，不能只保留最后一个调用或丢失 id。

处理终止原因：

| finish_reason | 行为 |
| --- | --- |
| `tool_calls` | 至少一个合法调用；按预算执行，使用相同 requestId 继续。 |
| `stop` | 无调用时进入 clarification；若携带调用则作为协议不一致失败，不能猜测执行。 |
| `length` | `truncated_response`；即使 arguments 看起来可 parse 也不执行。 |
| `content_filter` | `content_filtered`，无结果。 |
| `insufficient_system_resource` / `aborted` | `provider_interrupted`，无结果。 |
| 未知/缺少、空 choices 或非法消息 | `invalid_response`，无结果。 |

一轮里若出现 `build_frame`，先确认该响应所有调用的 envelope/name/JSON 都合法；build 必须为该批最后一个调用且最多一次，否则报 `invalid_tool_sequence`。本地成功后直接终止会话，不再要求模型复述帧；失败作为工具错误可交给模型修正/澄清，但本地结果区保持空。网络响应 envelope 不合法、arguments 不是合法 JSON object、未知工具名均直接安全失败并终止；只有合法工具调用中的业务/Schema 校验失败才回模型，在有限预算内修正。无法配对的调用直接失败。

固定内部预算先采用：每次 submit（含澄清续问）最多 10 次 HTTP、30 次工具调用、4 个澄清往返；单 HTTP 45 秒，总运行时间 180 秒（不计等待用户回答）；HTTP 响应最大 2 MiB，arguments 最大 128 KiB，树最大深度 32/总节点 10000。目录分页不超过 50；单条定义若超过预算明确失败，不能截断成不完整 inputSchema。预算都是内部常量，不增加高级配置 UI；实测合理后可调整并注明。达到上限报 `budget_exceeded`，不自动重试付费请求。

每个回调检查本地 requestId、请求代数/活动 reply 与状态；cancel 先使请求失效再 abort，后到回调不得写 result 或继续 HTTP。timeout 使用计时器与 abort，区分用户取消。澄清后不换 requestId、不重置预算；新 submit 清空旧对话和结果并换 UUID。会话只存内存，不写 INI、日志或崩溃诊断正文。

当前官方默认开启 thinking；本实现显式关闭。解析时保留供应商 assistant message 中的 `reasoning_content` 以防兼容服务仍返回它，但不展示/记录推理内容，不私自改成思考模式。若未来开启 thinking，所有携 tools 的历史 assistant reasoning_content 均需按官方要求原样回传，不能套用“每个用户轮次删推理”的旧假设。

## 6. 结果与错误

成功 result 仅由本地 build_frame 创建，QML 不从模型文本正则提取帧：

```json
{
  "requestId": "本地UUID",
  "serviceCode": "GET_NORMAL",
  "serviceName": "读取单个属性",
  "treeValue": {},
  "terminal": {},
  "submittedAt": "ISO8601含offset",
  "definitionId": "本次定义摘要",
  "apduHex": "本地生成的大写连续HEX",
  "frameHex": "本地生成的大写连续HEX",
  "summary": "本地目录名称与参数摘要",
  "warnings": []
}
```

示例空 treeValue/terminal 只表示对象类型，真实成功值必须完整。页面展示冻结终端、服务名、APDU 和完整帧，并提供复制。输出多条帧的需求先澄清选一条；本次结果是一条可审核的完整帧。只能标注“已构建/本地验证”，不能显示“已发送/设备执行成功”。

其他稳定错误码：`invalid_input`, `configuration_missing`, `credential_unavailable`, `credential_write_failed`, `credential_cleanup_failed`, `settings_write_failed`, `busy`, `tls_error`, `network_error`, `timeout`, `request_too_large`, `response_too_large`, `http_auth`, `http_balance`, `http_rate_limit`, `http_server`, `http_redirect`, `http_invalid_request`, `unknown_tool`, `invalid_arguments`, `missing_field`, `unknown_object`, `unsupported_definition`, `context_changed`, `build_failed`, `cancelled`。HTTP 401/402/429/5xx 映射本地说明；400/422 提示配置/请求格式错误；非 JSON/HTML 响应不能泄露正文到 UI。保留 HTTP status 作为非敏感诊断即可，不把完整供应商 error.message 直接输出。

## 7. 设置、安全存储与真实连接测试

普通配置用 `appSettings()` 的 `aiFraming/baseUrl`、`aiFraming/model`。不要使用 `getUiSetting/setUiSetting` 保存 Key；也不能把 Key 写入 terminal、QML persistent property、framing templates、同步 XML、debug 或日志。设置页 API Key 字段采用 Password 显示，页面只显示“已保存/未保存”，保存或清除成功立即清空输入框。调用期间 Key 必须短期存在 QML 输入缓冲与 C++ 内存，此合同不承诺 QString/QByteArray 能在所有复制路径实现可靠内存擦除。

macOS 用 Security.framework 的 Generic Password Keychain；Windows 用 Credential Manager `CredWriteW/CredReadW/CredDeleteW`（advapi32 已链接）；Keychain framework 由 CMake APPLE 分支链接。单一 profile 使用规范化 baseUrl 摘要作为账户标识，service/target 固定为 LogAnalyQt 的 AI 接入用途；相同 Key 不自动跨 endpoint 复用。凭据不随便携目录复制。暂不增加 Linux Secret Service 依赖；其他平台或系统存储不可用时仍可构建，`credentialAvailable=false`，保存/生成明确报错，不回退明文文件。

页面 `hasApiKey` 只查询当前 profile 存在性，实际使用时才通过 C++ read 取得 Key。macOS 查询仅请求属性；Windows generic credentials 无纯 metadata API，精确 CredRead 后只判断存在，不复制 blob，擦除系统返回缓冲并 CredFree，不枚举其他凭据。原生凭据测试与生产 service/account 分离，不能读取已有真实 Key 作为测试。

`saveConfiguration`：newApiKey 非空时替换该 endpoint 的凭据；为空只允许在相同已保存 baseUrl 下保留已有 Key。首次保存或变更 baseUrl 且没有新 Key 时返回 `configuration_missing`；不能把旧 endpoint 的 Key 送往新 endpoint。model 变更可保留相同 endpoint 的 Key。系统存储/INI sync 失败必须返回失败，不在 UI 显示保存成功；baseUrl 切换时清理旧 profile 凭据，清理失败回退原配置与新凭据并明确反馈恢复是否完成。`clearApiKey` 删除当前 profile 凭据，已不存在也视为成功；失败仍显示现存状态，不能只清 hasApiKey 内存值。清除前取消活动连接/生成并使回调失效。

`testConnection` 对填写的 URL/model 与临时 Key 发起真实小请求，不自动保存。如果临时 Key 为空，只能使用同 baseUrl 的已保存 Key；跨 endpoint 要求重新输入。不得用“字段非空/GET主页/HTTP 200”假装连接成功。用一个独立 `connection_probe` 函数和随机 nonce，强制调用该工具，明确 `thinking.disabled`，返回正确 tool name/JSON nonce、有效 tool_calls finish_reason 后才显示成功；不执行业务工具，不产生组帧结果、不发送终端信息。该检查证明 endpoint/鉴权/model 的一次真实工具响应可用，不证明后续所有业务生成正确。连接测试也支持取消、45 秒超时、响应大小/JSON 检查和上述错误映射。

“真实连接测试”按钮可能产生供应商少量计费，应在按钮附近说明；用户主动点击就是本次调用。此次架构阶段不查找本机 Key、不发真实 API 请求。后续真实账号验证由用户在设置页填写后点击，不要求把 Key 贴到聊天。

## 8. 测试注入与验收分层

使用自定义 `QNetworkAccessManager::createRequest` / QNetworkReply 替身注入 DeepSeekClient，捕获请求 JSON/headers，排程返回 fixture、延迟、错误与取消；测试 Key 只能为固定假值。替身测试使用同样 HTTPS URL，不通过发布可执行程序的 insecure flag 打本地 HTTP。控制器测试可注入该 client 与内存 credential store；依赖注入只在 C++ 构造边界，不向 QML 暴露设置 manager/回包/跳过验证入口。

必要自动检查：

- HTTP 形状、null content+工具调用、一次多个 tool_call_id、JSON字符串参数、unknown tool、未知键/错误类型、length/aborted/空 choices/HTTP错误均按合同处理。
- 工具发现覆盖全部现有服务；至少读取/设置/执行/列表/记录/代理/响应各一例；定义来自当前 XML。整数溢出/小数/错误枚举/非法hex/真实日历/缺必填拒绝；64位精度不丢失。
- 上下文地址冻结、显示地址到线序一次翻转、响应帧头、工具或用户文字不能覆盖外层地址；复用旧组帧构建器的结果应相同，并至少留独立已知正确例帧或 CRC/解析检查，不能只用同一个 builder 计算 expected。
- 取消后迟到结果、45秒超时、总预算、澄清续问与新 requestId、定义变化；不能取消后又显示成功。
- 假 Key 不出现在 INI/日志/错误/UI可读属性；不同 URL 不复用 Key；保存/清除失败保留可信状态；临时连接测试不持久化。
- QML 保持所选版布局/主题、窄窗口和滚动、键盘访问、取消/澄清/结果复制；报文解释不可误触发送模型请求。独立 ui-preview 缺 backend 时安全展示不可用状态。

验收分开报告：已实现（源码）→ 已验证（本地自动测试/QML运行/网络替身）→ 待真实验证（macOS Keychain、Windows Credential Manager、真实 DeepSeek账号）→ 不在范围（设备执行）。两种系统凭据和真实 Key 都不能由 mock 测试替代。Windows 原生安全存储若当前环境无法运行须明确列待验证。

## 9. 当前未知与最小用户输入

不阻塞独立编码的未知：用户实际 endpoint/model/Key、目标终端列表、业务中的时区和缺失字段、独立例帧及本机 Windows 验收环境。配置通过设置页输入，终端通过终端栏输入，业务歧义在该请求中澄清；不能为完成演示替用户填写。当前已选第 2 版布局且不做解释/SDK/设备发送，不再重复询问。

真实验收最小需要：用户在设置页填自己的 DeepSeek 接入信息并点击测试；填写一个有效终端；给出一条信息完整的业务需求。如果要宣称协议字节与目标一致，还需要该业务的已知正确帧或现有独立标准例帧。任何当前定义未覆盖的对象/服务，先返回具体不足再向用户确认目标定义，不能凭经验扩展。

## 10. 官方来源与日期

以下均于 2026-10-14 现场读取，只使用 DeepSeek 官方资料；请求格式和模型可用性会变化，实际账号仍需真实连接测试。

- [Chat Completions API](https://api-docs.deepseek.com/api/create-chat-completion/)：POST路径、messages/tool_call_id、tools、JSON字符串 arguments、finish_reason 与 thinking 参数；本地必须验证模型生成参数。
- [Tool Calls](https://api-docs.deepseek.com/guides/tool_calls/)：assistant+tool 消息续接、多工具模式、strict 为 beta 且额外约束 schema；本合同采用普通工具调用。
- [Thinking Mode](https://api-docs.deepseek.com/guides/thinking_mode/)：默认启用、显式 disabled、带工具的历史 reasoning_content 回传要求；本合同显式禁用思考。
- [Multi-round Conversation](https://api-docs.deepseek.com/guides/multi_round_chat/)：API 无状态，客户端每轮传完整会话。
- [JSON Output](https://api-docs.deepseek.com/guides/json_mode/)：json_object 需明确 JSON 提示，仍有空内容/截断边界；正常工具循环不依赖它。
- [Models & Pricing](https://api-docs.deepseek.com/quick_start/pricing/)：当前文档模型名为 deepseek-flash / deepseek-v4-pro；可配置模型ID，默认只随经核对的版本调整，不把旧模型名硬编码为唯一选项。
- [Error Codes](https://api-docs.deepseek.com/quick_start/error_codes/)：401鉴权、402余额、429限流、400/422参数和500/503服务问题映射。

## 11. 阶段7通用覆盖补充（现行输入合同v3）

2026-10-14：以下两项扩展复用既有服务、字典与校验器，不新增专用业务工具或发送设备能力。`definitionId`的合同前缀修订为`698-framing-contract-v3`，与实际XML内容共同取摘要；旧会话不能混用新输入定义。

### 代理目标使用显示地址结构

PROXY_GET_LIST与PROXY_GET_RESPONSE_LIST的`servers[]`在AI路径必填`tsa`：

```json
{"tsa":{"address":"112233445566","addressType":2,"logicAddress":1}}
```

这是字段形状示例，示例地址/类型不是业务默认。`address`为1..16字节连续Hex的显示顺序，`addressType`为0..3实际整数，`logicAddress`为0..1实际整数；三项都必须明确，未知键、bool当整数、超范围或缺字段均拒绝。没有内层CA字段，不从外层终端快照暗填目标或地址类型。

本地复用FrameBuilder既有显示地址契约反转一次，再生成TSA地址描述字节；既有ServiceBuilder添加TSA长度。模型不能提供/计算`tsaHex`、描述字节或线序。原`tsaHex`仅保留旧人工入口，结构化`tsa`与`tsaHex`不能同时给出。输入详情保留结构，`normalizedTree`展示本地产生的`tsaHex`，外层终端和帧头快照保持冻结。

### 记录响应使用按列的rows

GET_RESPONSE_RECORD的`recordResult`及GET_RESPONSE_NEXT的`nextResult.recordResults[]`在`choice=1`时，AI必填`rows`，不接受`rowCount/rowsHex`：

```json
{
  "choice":1,
  "rows":[["<第1列值>","<第2列值>"]]
}
```

该示例只说明二维数组形状；真实cell按`rcsd.items`列顺序查询各OAD的`inputSchema`填写，不把占位符当值。每行cell数必须精确等于CSD列数；非空记录本轮支持`CSD.choice=0`的普通OAD列。cell复用该属性的完整类型、枚举、array/structure、动态Data/optionIndex、单位/scaler和64位规则；含单位数值仍需raw/physical包装。每次最多255行、255列，行列乘积最多4096，并受现有整树预算约束。

本地从`rows.size()`生成行数，按行、按列依次编码带tag的Data，不额外给每行加structure或array标签。旧人工`rowCount+rowsHex`调试路径仍保持，AI不能借其绕过按列校验。`choice=0`仍为DAR分支，禁止夹带rows。

`rows=[]`明确表达空数据行，普通OAD/ROAD列均可使用。非空ROAD cell的实际容器（array或structure）、关联属性排列和目标记录定义尚缺目标例帧，因此只对此分支返回`unsupported_definition`，不猜类型；ROAD请求、DAR和旧人工路径不关闭。公开inputSchema使用条件约束区分非空普通OAD列与空rows，同时给出可达分支限制。

以上转换发生在ServiceSchema的同源校验/规范化路径，控制器继续透传generic tree。成功结果的原始输入、规范化raw树及两个C++格式化文本字段沿用第6节，不让模型编写编码结果。独立实现来源、固定blob和本地验证边界见capabilities.md第11节及acceptance.md第10节。


## 阶段 8：系统凭据等待与主线程边界

2026-10-14 原生采样确认 submit 在主线程调用 SecItemCopyMatching 时等待 SecurityServer IPC；用户后来确认确有系统授权框且自行处理后恢复。不能由此推断 ad-hoc 签名是唯一触发原因。修复保持现有授权策略、存储命名空间和 OS 权限，不读取其他应用凭据、不全局禁止系统交互。

每个生产控制器使用一个串行 AiCredentialWorker/QThread，worker 拥有 AiModelSettings 和 shared_ptr<AiCredentialStore>。所有存在查询、读、保存、清除及保存内部 prior-read/rollback 均在该线程执行；QML getter 只读缓存。初始refresh也是唯一物理任务，不允许无限排队或为取消另开线程。控制器销毁只断开回调并请求线程退出，不join、不强杀OS调用；worker和store活到当前操作返回再释放。本合同不声称多进程或多生产控制器共享同profile时有全局事务锁。

新增 settingsBusy、credentialStatus（checking/ready/failed/waiting）及credentialMessage。未知、失败或等待状态不能被UI直接解释成“未保存密钥”。submit先返回requestId并进入preparing（busy=true）；连接测试先进入connectionBusy。凭据读取后还要核验请求仍有效及定义未变化，只有当前有效请求才可启动HTTP。配置文件位置在worker创建时固定，URL/model在提交时冻结；外部配置变化不得混用其他endpoint的key。

凭据操作等待30秒后：只读操作使逻辑生成/连接测试超时失效，但settingsBusy保持true并说明系统操作仍未返回；迟到结果最多刷新缓存，不能启动旧HTTP。用户取消也只结束逻辑请求，不取消系统授权、不释放仍在使用的store。保存/清除属于变更事务，30秒只切换waiting提示，不宣称失败、成功或已撤销；只有事务真正返回才发settingsOperationFinished。物理操作未返回前拒绝第二项凭据事务。清除先取消逻辑会话；如果旧读仍在等待，清除返回busy，待其返回后由用户重新发起，避免旧读与删除并发或恢复已清状态。

页面销毁不假称已受理的保存/清除已撤销；应用在系统操作等待中退出不承诺自动回滚，复启应重新读取可信状态。Key只通过worker到控制器的私有C++完成参数交接，不进入QML最终reply、snapshot或getter。测试注入使用shared_ptr保活，不能借用可能先销毁的store。

第一手依据：[Apple SecItemCopyMatching 性能说明](https://developer.apple.com/documentation/security/secitemcopymatching(_:_:))明确该函数阻塞调用线程并建议后台调用；[Working with concurrency](https://developer.apple.com/documentation/security/working-with-concurrency)说明Security调用并发边界。关闭认证UI不是硬超时保证，本轮不以它替代后台化。
