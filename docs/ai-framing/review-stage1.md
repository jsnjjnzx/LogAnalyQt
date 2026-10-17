# 阶段 1 独立审核与阶段 2 准入

审核日期：2026-10-14。审核角色：GPT-6 Astra。对象：execution.md、capabilities.md、contracts.md、acceptance.md，以及当前工作区实际协议、后端和 QML 代码。采用 Ponytail full：只修复通用组帧所需边界，不新增 SDK、设备发送或报文解释。

## 结论

**阶段 1 的库存和风险记录合格，可以启动阶段 2 的确定性基础实现；不能据此认定 18 服务可用于 AI 成功组帧。** 下面 R1–R3 是进入相关功能实现前必须冻结的合同细节，R4–R6 是阶段 2 退出前必须处理的实现门槛。缺失外部标准/设备证据不阻止加载器、类型校验、错误路径和通用查询实现，但会阻止对存在实际歧义的编码分支作正确性声明或开放成功构建。

文档正确记录了：实际 last-wins 加载行为、18 个注册 Builder 分支与更多 Parser 标签的区别、Data/option 静默 Null、原始 Hex 入口、源码缺陷及外部验证缺口。验收矩阵明确区分本地自动检查、独立字节依据、真实 DeepSeek 和目标平台，不把既有 UI 测试当新功能验收；方向正确。

## 实际源码核验

| 证据 | 审核判断 |
| --- | --- |
| `Prot698OadManager.cpp:13–95` | 每次加载直接往现有对象表 insert；OI 整体覆盖、Node 键覆盖成立。仅提取 Name/Type/Unit/Scaler 和小写 enum；未保存 TypeOption、Selected、Size。应原子替换并保留覆盖诊断，不改变用户确认的 last-wins。 |
| `Prot698DataEncoder.cpp:62` | 未映射类型默认 Null。现有编码器不能作为模型输出的信任边界；未知/空类型不得借 Null 成功。 |
| `Prot698ServiceSchema.cpp:119–159` | 只检查 PIID 是否存在和若干非空列表，OAD/OMD 又补 attrId=2、methodId=1。缺字段、错误 JSON 类型或未知键必须在任何 QVariant 转换前拒绝。 |
| `LogBackend.cpp:2630–2696` | build 调用已有 Schema/Builder，verifyHexData 返回展示字段。展示字段非空不是 HCS/FCS、完整消费及语义校验成功。 |
| `Prot698FrameBuilder.cpp:25–39,72–85` | optionsFromMap 按原字节读取地址，而 defaultClientRequestOptions 翻转；帧长只写低 14 位但没有拒绝超长 APDU。AI 必须统一显示地址语义，并在分配/写入帧前限制长度。 |
| `Prot698Parser.cpp:29–71` | isValidFrame 为日志前缀识别容许尾随字节，只检查 FCS，未检查 HCS，且极小声明长度可使 FCS 下标无效。不要把它直接改成单帧严格语义而破坏日志流；严格单帧入口应明确区分，并修复公共边界越界。 |
| `qml/Next/SmartFraming/FrameHeaderPanel.qml:20` 与 `Prot698Parser.cpp:452–468` | 普通响应 UI 默认 funcCode=1；Parser 描述 1 为链路管理、3 为用户数据。仅凭这两处无法裁定线上的正确控制域。 |
| `Prot698ServiceBuilder.cpp:80–89` 与 `Prot698Parser.cpp:1474–1496` | Builder 统一追加 TimeTag，响应 Parser 还期待 FollowReport；既有响应编码/解析合同不自洽。不能通过修改测试预期掩盖。 |

本审核独立检查上述路径，没有把其他实施者的探针输出当作自己运行的结果，也没有重新核准 XML 全量计数或外部网页事实。

## 必须落实的最小修正

### R1：冻结递归值的精确表示和数值语义

contracts.md 已规定 serviceCode+treeValue，但尚未确定 option 如何选择、动态容器如何表示、ACTION 返回 Data 如何与方法入参分开。capabilities.md 给出 typed Data 建议，仍需形成可测试的最终合同。

建议由协议实施者在开始编码前补一张精确输入表及可验证示例：普通静态值保持已有树；动态 Data 用显式 type/value；option 用候选的稳定标识和值，不能按 Selected 自动选第一个；重复类型候选必须能区分；64 位超安全范围用十进制字符串；ACTION 返回值在缺少返回定义时使用显式 typed Data，不套入参定义。每种形状需要 required/additionalProperties、嵌套规则和错误路径。

按主代理最终协调，既有人工入口继续采用原始值语义；AI 明确 physical/raw 和单位，禁止隐式换算。不能将自然语言“10 A”直接当原始整数。属性特征位、非零 index 的具体语义和 ATCION 等不明确定义，先返回局部 unsupported_definition，不阻断整个字典，也不猜改字典。

### 冻结补充合同（阶段 2 直接执行）

以下是本审核与主代理协调后的实现边界，优先于三份初稿中的含糊表述；实施者应同步回 contracts.md 后编码，不另建架构层。

- `get_definition` 分支使用严格对象：service 为 `{kind:"service",serviceCode:"GET_NORMAL"}`；object 为 `{kind:"object",oi:"4000",memberId:2,isMethod:false,dataIndex:0}`。每分支只允许自己的键；object 的 memberId=0 表示成员索引且 dataIndex 必须为 0。不要求另一分支的无关参数填空。
- 静态非数值沿用已存在的值形状；array/structure 为按定义顺序的 JSON 数组，structure 字段数必须精确。动态 Data 为 `{type:"unsigned",value:1}`，type 采用规范化类型名；动态 array/structure 的 value 是 typed Data 数组，例如 `{type:"structure",value:[{type:"unsigned",value:1},{type:"null",value:null}]}`。未知 type 拒绝，Null 必须显式 null。
- option 为 `{optionIndex:0,value:123}`，optionIndex 是当前父节点 children 中候选的零起始位置；get_definition 同时返回这个位置、TypeOption、Name、Scaler 与 Selected。请求冻结 definitionId，因此位置不会在一次会话内漂移。明确选择后按 TypeOption 编码，不额外写 choice 字节；Selected 不自动选型。
- 带单位/Scaler 的数值叶子在 AI 输入中必须为 `{representation:"raw",value:123,unit:"A"}` 或 `{representation:"physical",value:"1.23",unit:"A"}`。unit 必须与当前定义一致；无单位定义使用空字符串。raw value 为精确 JSON 整数或超安全整数范围的十进制整数字符串；physical value 为十进制字符串，避免先经 double 舍入。固定整数类型按 `physical = raw × 10^scaler` 做精确十进制换算；无法整除、超范围或单位不匹配拒绝。不得对浮点或 scaler 含义不明确节点猜换算。没有单位/Scaler 语义的整数直接使用原始值。枚举按整数集合验证，不套物理倍率。工具负责把已验证结果转换为旧编码器 raw 值，旧手动入口不变。
- 动态 typed Data 的数值值默认是显式类型的线值；若其类型来自带单位候选/静态定义，仍遵守上述叶子表示。方法响应 `hasData=true` 时要求 typed Data，不能复用方法入参定义；`hasData=false` 禁止 dataValue。
- AI 路径不开放尚未结构化的 opaque/rawHex/rowsHex；真实 octet_string 等已定义字节字段使用严格十六进制和长度校验。确需 opaque 用户导入时另行冻结可信来源绑定，本阶段不加此功能。
- 能力对象采用 `buildSupported:boolean, unsupportedReasons:array, verificationStatus:"pending"|"local_verified"|"independent_verified"`。状态必须来自本地实现/证据，不由模型声明。缺少设备联调本身不关闭确定性构建；只有未实现或线格式尚未确定的分支关闭 build，并返回路径和原因。
- 终端列表和选中项保持当前会话内状态；本轮只持久化模型接入配置及其安全凭据。contracts.md 所述终端持久化及 acceptance.md 的终端重启恢复要求移出本轮验收。终端快照冻结仍必须执行。

数值包装、optionIndex 与动态容器均须在 get_definition 的 inputSchema 明示，并由同一校验器验证，不能只在文档或提示词说明。这些选择不改变用户字典内容，也不把业务歧义转成默认值。

### R2：使原始 Hex 限制可由本地执行

“只接受用户明确提供的 rawHex”不能仅写进 prompt：当前 AiFrameContext 没有可信原始输入绑定，工具收到一个 Hex 无法知道来自用户还是模型编造。最低成本安全实现是 AI 工具禁用未结构化的 opaque/rawHex/rowsHex 分支并返回具体路径；真实 octet_string 等已定义字节类型仍按严格类型/长度规则处理。若后续保留 opaque 导入，先增加本地用户确认的字段绑定，而不能接受模型自报来源。

这不要求删掉旧手动调试入口；两者不能在 AI 调用链上互相绕过。记录行若暂不结构化，服务目录必须明确标出该分支未开放，不能把任意 rowsHex 透传计入通用覆盖。

### R3：区分支持状态和证据状态

list_services/get_definition 应同时给出可构建状态、限制原因与验证证据状态，避免一个 supported=true 同时暗示“本地有分支”和“独立标准已验证”。全部 18 项必须可发现；暂不支持的分支应可查询其原因，而非从目录消失。

存在实质编码歧义的响应控制域、响应尾、浮点线序等，在取得当前适用资料前不得开放成功构建。普通已明确路径可实现并标注独立依据待验证；这不是把整个实现阶段无限等待外部材料，也不能自动选择 1 或 3 来让测试通过。验收矩阵所有待项继续保留，不以永久裁减范围替代解决。

### R4：严格 JSON 校验与共享编码修复的边界

同一个定义源驱动查询和递归验证；先保留 QJsonValue 类型，拒绝 bool-as-number、小数、溢出、未知键、缺字段、未知分支、无效日期、错枚举及数组/结构不匹配，再转换给旧编码器。校验不能只包住 SET_NORMAL 两个例子。共享编码器修复未知类型、截断等根因时，检查人工 UI、APP 组帧和 Parser 调用者；旧合法输入应保持，旧非法输入不要求继续成功。

字典成功重载应原子替换，失败保持旧表；definitionId 应覆盖实际字典和相关服务/输入合同修订，不能只对路径或 XML 版本字符串取值。默认 PIID 继续用现有本地分配器，每请求只分配一次；validate/build 不重新分配。

### R5：完整帧边界及受限解析

阶段 2 增加严格单帧验证：精确长度、最小结构长度、地址长度、起止符、HCS/FCS、无尾随字节；拒绝未实现的分帧/加扰。当前长度编码只有 14 位，必须在构建前检查 `9 + addressBytes + apduBytes <= 0x3FFF`，而不是先掩码；以后支持其他长度形式需另有明确合同。

APDU 完整消费检查需有受界限入口或可靠 offset 结果，不能从展示字段数反推。独立 CRC 自检可证明校验算法与覆盖区间，但 CRC 正确不证明字段语义或控制域正确。

### R6：保留可复现测试证据

把缺陷探针转成正式拒绝断言，而非复制错误现状作 golden。最小测试涵盖：静态类型边界、动态 Data/option、64 位无损、深度/数量、未知字段、权限和索引、字典重载/覆盖诊断、两条地址入口、短/超长帧、坏 HCS 但重新算正确 FCS、尾随字节及无效响应分支。18 服务每项应有状态及对应测试/阻塞原因。独立样例缺失继续标待验证，不得用同源 builder/parser 往返替代。

## 阶段 2 可执行分工

| 责任 | 可以开始的工作 | 必须交付给整合者 |
| --- | --- | --- |
| 协议实施者 | 冻结 R1/R2 输入表；加载器元数据与原子替换；递归校验；必要的 Encoder/FrameBuilder 边界修复；为后续 Ai698Tools 留统一入口 | 实际文件清单、精确输入示例、支持/限制目录、旧调用者回归、尚需资料的具体字段；不擅自升级标准或改字典含义 |
| 验证实施者 | 独立维护正式边界/负例及帧校验测试；检查 18 服务状态；整理外部 golden 登记格式 | 自己计算的 CRC/长度预期、运行命令和日志、未通过与未运行项；不在并行时修改协议实现 |
| AI 实施者 | 依赖冻结合同审阅 facade/工具 JSON，阶段 3 的设置与安全存储可按协调另行开展 | 不重新定义协议数据表示、不在 prompt 内替代校验；共享 LogBackend/CMake 变更先由主代理分配 |
| Astra 审核整合者 | 按最终差异与调用链复核 R1–R6，核对证据和原有用户改动 | 每项通过/阻断及最小修正；不接受全量 stage 或重置用户工作区 |

本阶段只新增本审核文件，未修改业务、XML、测试或构建，也未提交 Git。未执行完整构建、原生 UI、真实模型、Windows 或设备测试；当前结论仅为阶段准入和静态审核，外部协议歧义仍可能影响最终可开放范围。
