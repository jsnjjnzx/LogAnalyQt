# 698 通用组帧能力盘点（阶段 1）

盘点日期：2026-10-14。对象为当前工作区代码和 `protocol_xml/698_protocol.xml`；用户已确认按当前工程标准实现，本阶段不升级协议版本，也不是对 DL/T 698 全部版本的符合性认证。第1–9节保留阶段1库存基线；阶段2实际修复、已验证和限制见第10节。

## 1. 范围与证据口径

已检查 `src/protocols/698_prot/` 中 DataEncoder、OadManager、ServiceSchema、ServiceBuilder、FrameBuilder、Parser 及 GET/SET/ACTION/PROXY/REPORT/SECURITY 子解析器的相关路径；检查 `src/core/LogBackend.{h,cpp}` 的组帧入口与 `qml/SmartFraming/`、`qml/Next/SmartFraming/` 的直接调用方。本文的“已实现”指存在代码分支，“静态核验”指由代码与 XML 推导，不能替代独立测试、标准样例和真实终端验收。

XML 统计以现有加载器规则计算：同 OI 的最后一个 Item 整体覆盖前项；同一个有效 Item 内 `(Method, ID)` 的最后一个 Node 覆盖前项；只读取每个 Node 的第一个直接 SubItem，再递归其直接 SubItem 子项。未把被覆盖定义计入有效能力。

`LogBackend` 使用 `resolveProtocolPath("698_protocol.xml")`；外置用户字典可能优先于资源。以下计数对应仓库 XML，并不保证已经启动的应用加载同一份文件。通用 API 应返回实际字典的路径、版本、内容摘要及加载问题，避免把仓库快照当作运行时字典。

## 2. 已有通用调用链

现有调用链已经是“服务树 → 服务参数 → 确定性 APDU → 完整帧”，无需另造只认识两个动作的业务编码器。

| 入口/组件 | 目前行为 | 通用化缺口 |
| --- | --- | --- |
| `list698ServiceTypes()` | 返回 18 个服务及方向/分类 | 能力状态、禁用原因和输入约束未公开 |
| `get698ServiceSchema(code)` | 返回 piid、服务节点 kind、timeTag | 复合节点仅有 kind，字段约束和 choice 分支藏在 QML/C++ 中；未知 code 也生成根节点 |
| `listAllOad(filter)` | 列出有效 OI、属性/方法 ID、Name、OP | 缺字典问题、来源、可组帧性与动态类型标记；搜索不检查方法名称 |
| `getOadSchema(oi,id,isMethod,index)` | 返回 Name/Type/Unit/Scaler/enumValues/children | 丢 Size/TypeOption/Selected/Remark/Format/原始 DefVal；不返回 OP 与索引有效性 |
| `build698ApduFromTree(code,tree)` | `toServiceSpec` → `buildFromSpec`，字符串 Hex/`ERR:` | 只做薄校验，错误没有稳定代码和路径，默认值会掩盖缺参 |
| `buildFullFrame(apduHex,options)` | 地址、控制域、HCS/FCS、帧尾 | Hex 宽松解析、地址双路径顺序不一致、部分标志没有对应编码 |
| `verifyHexData(hex)` | 返回解析展示字段列表 | 不是严格合法性判断，不证明校验正确、消费完整或语义符合字典 |
| `getCurrent698Address()` / `getNextPiid()` | 从已解析帧取显示地址；序号在 1..63 循环 | 自动上下文必须经过严格帧校验；AI 草稿不能消耗正式 PIID |

自然语言层应调用统一查询/Schema/校验/预览接口。`set_time`、`set_address` 仅是测试样例，不应成为唯一工具或绕过上述通用调用链。

本项目已确定的工具合同为 `Ai698Tools`（`src/protocols/698_prot/Ai698Tools.{h,cpp}`）：统一 `invoke` 分发 `list_services/search_objects/get_definition/validate_draft/build_frame`；模型只提交 `serviceCode+treeValue`。`frameOptions` 由本地终端快照提供，模型不能改地址、方向、CA 等帧头上下文。阶段2应沿用该合同，避免平行建立另一套 AI 接口。

## 3. 18 个已注册服务

下表字段是现有 `treeValue` 的真实键。全部服务包含 `piid` 和可选 `timeTag`。PIID 包含 `seqNo/highPriority`；部分响应编码另使用 `acd`。`timeTag` 使用 `hasTimeTag/year/month/day/hour/minute/second/tiUnit/tiValue`。

| 服务 | APDU 前缀 | 树字段 | 静态能力/限制 |
| --- | --- | --- | --- |
| GET_NORMAL | 05 01 | `oad` | OI/attrId/index → 4 字节；不检查对象存在和 GET OP |
| GET_LIST | 05 02 | `oads[]` | 1..255 OAD；同上 |
| GET_RECORD | 05 03 | `oad,rsd,rcsd` | RSD 0..11、RCSD/CSD 结构化；MS 部分分支仍是 Hex |
| GET_NEXT | 05 05 | `lastBlockNo` | 16 位 BE 写入；解析请求按 LE，且无整数范围校验 |
| SET_NORMAL | 06 01 | `setItem:{oad,dataValue}` | 按 XML 编码 Data；无 SET OP 检查 |
| SET_LIST | 06 02 | `setItems[]` | 1..255 写入项；同上 |
| ACTION_NORMAL | 07 01 | `actionItem:{omd,dataValue}` | 按方法 XML 编码参数；无 ACTION OP 检查 |
| ACTION_LIST | 07 02 | `actionItems[]` | 1..255 方法项；同上 |
| PROXY_GET_LIST | 09 01 | `totalTimeout,servers[]:{tsaHex,timeout,oads[]}` | 1..255 服务器，每项 1..255 OAD；TSA 仍输入 Hex；超时会截断 |
| GET_RESPONSE_NORMAL | 85 01 | `result:{oad,choice,dar,dataValue}` | choice=1 时 XML Data，否则 DAR；非法 choice 也走 DAR |
| GET_RESPONSE_LIST | 85 02 | `results[]` | 1..255 普通结果；同上 |
| GET_RESPONSE_RECORD | 85 03 | `recordResult:{oad,rcsd,choice,dar,rowCount,rowsHex}` | 记录数据行仍整段 Hex，未校验行列数量/类型 |
| GET_RESPONSE_NEXT | 85 05 | `nextResult:{end,blockNo,choice,dar,normalResults[],recordResults[]}` | choice 0/1/2；普通/记录列表依上；blockNo 截断风险 |
| SET_RESPONSE_NORMAL | 86 01 | `setResult:{oad,dar}` | DAR 范围 0..255；未编码 acd |
| SET_RESPONSE_LIST | 86 02 | `setResults[]` | 1..255；同上 |
| ACTION_RESPONSE_NORMAL | 87 01 | `actionResult:{omd,dar,hasData,dataValue}` | optional Data 存在位；返回值沿用方法参数定义，未独立建模 |
| ACTION_RESPONSE_LIST | 87 02 | `actionResults[]` | 1..255；同上 |
| PROXY_GET_RESPONSE_LIST | 89 01 | `servers[]:{tsaHex,results[]}` | 1..255 目标，每目标 1..255 普通结果；TSA Hex |

18 项全部有 Builder 分支，均只在尾部追加 TimeTag。GET/SET/ACTION/PROXY 响应解析入口却期待 FollowReport 后再 TimeTag，且 SET/ACTION 子解析器内部还自行读取 TimeTag；因此不能把“已有 Builder 分支”报告成“18 项全部可正确往返”。

Parser 还认识连接/释放、上报、安全、紧凑服务及部分其他 GET/SET/ACTION/PROXY 子类型；它们没有对应上述服务树/Builder，不属于现阶段可直接提供给 AI 的组帧服务。

## 4. 实际生效 XML 数据类型全表

仓库 XML 声明 `version=v1.0.1,date=2026-02-11`：744 个 Item，经整体覆盖后为 727 个 OI；有效属性 2668 个、方法 1876 个，共 4544 个有效 Node；递归 SubItem 12694 个。大小写及 `-`→`_` 归一化后有 36 种类型（含空类型）。

下表完整列出实际有效 SubItem；“有分支”不等于验证充分。整数应区分原始值与物理值，当前编码器不按 Scaler 自动反换算，解析器却会按 Scaler 显示物理值。

| 规范化类型 | 数量 | XML 拼写（有差异时） | 当前编码及缺口 |
| --- | ---: | --- | --- |
| array | 1571 | array | 取第一个 child 当元素模板；上限 255；不校验输入一定为 list，也不识别 option 模板 |
| structure | 1823 | structure | 顺序 list；缺字段补无效 QVariant 后常编码为 0，多字段被忽略；不校验精确字段数 |
| null | 294 | NULL 283/null 11 | 写类型 00；合法空类型应与“未知类型”明确分离 |
| bool | 146 | bool | QVariant.toBool；非 bool 输入也会转换 |
| bit_string | 210 | bit_string | 位数+打包值；除 `1` 外其他字符一律当 0 |
| integer | 611 | integer | 有符号 8 位；转换失败/溢出未拒绝 |
| unsigned | 688 | unsigned | 无符号 8 位；掩码截断 |
| enum | 707 | enum 671/ENUM 36 | 8 位；不强制枚举集合；大写 ENUM 的 DefVal 没被解析 |
| long | 155 | long | 有符号 16 位 BE；截断/转换未检查 |
| long_unsigned | 939 | long_unsigned 938/long-unsigned 1 | 无符号 16 位 BE；同上 |
| double_long | 197 | double_long | 有符号 32 位 BE；同上 |
| double_long_unsigned | 987 | double_long_unsigned | 无符号 32 位 BE；同上 |
| long64 | 242 | long64 | 有符号 64 位 BE；转换未检查；QML Number 会损失大整数精度 |
| long64_unsigned | 67 | long64_unsigned | 无符号 64 位 BE；同上 |
| float32 | 4 | float32 | memcpy 主机字节序；Parser 同样 memcpy，往返不能证明线序正确；NaN/Inf 未拒绝 |
| octet_string | 257 | octet_string | Hex/bytes→长度+内容；是合理的字节字段，不能凭名称生成负载 |
| visible_string | 402 | visible_string | Latin1；非 Latin1 字符可能丢失，Size 不生效 |
| date_time | 1 | date_time | date_time_s+week+ms；week/ms 只掩码，没有范围/日历校验 |
| date_time_s | 828 | date_time_s | 检查单字段上限，仍接受 0 月/日、2 月 30 日；缺项变 0 |
| date | 17 | date | 直接掩码，未验证日期 |
| time | 11 | time 9/Time 2 | 直接掩码，未验证时间 |
| oi | 119 | OI | 固定 2 字节 Hex；应提供 OI 选择器与字典引用 |
| oad | 696 | OAD | 固定 4 字节 Hex；应使用 OI/属性/索引结构 |
| omd | 2 | OMD | 固定 4 字节 Hex；应使用 OI/方法/模式结构 |
| ti | 56 | TI | unit 0..255/value 0..65535；未限制有效单位枚举 |
| tsa | 207 | TSA | Hex→长度+内容；没有 TSA 内部地址结构验证 |
| scaler_unit | 491 | Scaler_Unit | 两个掩码字节；无范围/单位验证 |
| csd | 11 | CSD | Data 内仅原始 Hex；服务 RCSD 的 CSD 另有结构化编码 |
| ms | 16 | MS | Data 内仅原始 Hex；RSD 内另有部分结构化编码 |
| comdcb | 18 | COMDCB | 原始 Hex，内部字段/长度未验证 |
| region | 7 | Region | 原始 Hex，内部字段/长度未验证 |
| sid | 3 | SID | 原始 Hex，内部字段/长度未验证 |
| sid_mac | 4 | SID_MAC | 原始 Hex，内部字段/长度未验证 |
| data | 873 | Data 808/data 57/DATA 8 | 没有类型映射，静默编码为 Null，属于真实动态 Data 缺失 |
| option | 28 | option | 没有类型映射；TypeOption/Selected 未加载，静默编码为 Null |
| 空类型 | 6 | Type="" | 同样静默 Null；不能推断为无参数 |

编码器还映射了本快照未出现的 `boolean/utf8_string/float64/ROAD/MAC/RN/RSD/RCSD/VQDS/Checksum` 等；它们应在能力查询中与“实际字典出现”分开。尤其 ROAD 被和 OAD/OMD 一起限制为 4 字节，而 CSD 的 ROAD 分支包含 OAD 列表；此处内部语义不一致，需要独立样例后修正，不能宣称 ROAD 已完整支持。

### 4.1 动态 Data 和 option 的真正形态

`Data` 不是“未知就写 00”的同义词。例：0000/方法2 XML Remark 直接写“输入报文，例如 unsigned 11 01”；6E12 等结构内也有 Data 附加参数。最小通用输入应是显式带类型的递归值，如 `{type:"unsigned",value:1}`；array/structure 内仍允许动态 Data 子值。未知类型必须报错，明确 `{type:"null",value:null}` 才能写 Null。AI 缺少实际类型或值时必须返回待补参数。

28 个 option 位于 0000/属性2、200D/属性2/3/4、200E/属性2/3/4、2049/属性2/3/4、204A/属性2/3/4、2401/属性4，每处两候选：long 与 double_long，或有/无符号、32/16 位选择。0000 和 2401 有一个 Selected=true；200D/200E/2049/204A 两候选均 Selected=true，不能依据 Selected 自动唯一选型。option 表示候选数据类型，不应直接当协议中的有判别字节 choice 编码。需要把候选名/类型/Scaler 公开，由用户/已确认上下文选一个，并按选中类型编码。

空 Type 的 6 个节点是 2024/属性2（事件发生源）、6002/方法128、6002/方法129、6012/方法3、6E11/方法3、6E13/方法3。需要字典维护者确认真实类型或明确 Null，不能由 AI 猜测。

### 4.2 原始 Hex 的分类与处理边界

| 类别 | 现有路径 | 通用接口处理 |
| --- | --- | --- |
| 标识符 | OI/OAD/OMD | 保留编码器字节出口；输入使用结构和实际字典引用，严格定长 |
| 地址 | FrameOptions.serverAddrHex、代理 tsaHex、MS.tsaItems、Data TSA | 使用统一显示地址语义，明确地址类型/逻辑地址/线序；不能把显示 Hex 与线序 Hex 混用 |
| 真实字节负载 | octet_string、MAC/RN/Checksum、SID_MAC 的密钥相关负载 | 用户或可信数据源提供；严格 Hex、长度与结构检查；AI 不编造有效认证数据 |
| 未结构化协议结构 | Data 中 CSD/MS/COMDCB/Region/SID/SID_MAC，RSD.MS 的 type5/6/7.rawHex | 标注 opaque/unsupported；保留用户明确提供的 Hex 的受控入口，不能声称已自然语言语义覆盖 |
| 整段结果绕过 | recordResult.rowsHex | 应按 RCSD 列定义逐行构建 Data 并验证 rowCount；不能让 AI 提供整行 Hex 绕过校验 |
| 内部中间产物 | serviceSpec.dataHex/rsdHex/rcsdHex/resultHex/recordHex | 由本地 Schema/编码器生成，不作为 AI 工具参数 |
| 最终预览/人工调试 | APDU Hex、完整帧 Hex | 保留输出与手动入口；AI 自动路径须回到结构化校验链 |

## 5. 字典重复、OP 与索引

### 5.1 OI 重复及现行覆盖

17 个 OI 重复：`22F0,31F0,31F1,6035,6040,6041,6042,6E00,6E01,6E02,6E12,6E14,6E16,6E18,6E23,6E31,7000`。当前 `loadXml` 的 `m_objects.insert(oi,objDef)` 是后项整体替换，不合并属性/方法。

其中 12 组序列化内容相同（不计 Item 末尾空白）；5 组有实质差异：

| OI | 当前最后项生效的后果 |
| --- | --- |
| 6E00 | 前项属性3“写保护剩余允许时间”和属性4“非增补代理任务优先级”全部消失 |
| 6E12 | 加工方式枚举从 0/1/2 缩成只有 0 |
| 6E14 | 请求/响应报文格式枚举丢失部分选项，验证方式枚举从 0..4 缩成 0..2 |
| 6E16 | 响应格式枚举丢失部分值，ERR-CODE 16“通信模块异常”消失 |
| 6E31 | 后项反而新增格式 22/34/35/36/83 等；不能统一以“前者较全”决定取舍 |

同一有效 OI 内还有 6 个重复 Node 键：2060/属性3、2061/属性3、2062/属性3、8105/方法3、F206/属性5、F209/属性30。前三个 B 相间谐波字段被“换算及单位”覆盖；8105“添加控制单元”被“更新控制单元”覆盖；F209“网络基本信息（山西专用）”被“拒绝从节点上报使能标志”覆盖。F206 的重复需保留诊断，即使内容相同。

用户已明确决定：组帧和解析均沿用当前实际生效定义，保留 last-wins；重复本身只生成可追溯 warning，不阻止组帧。诊断应包含 OI/Node 键、前后定义位置、最终采用项和内容差异，界面提示“存在重复，采用最后定义”。不要静默全表合并、重新编号或改成首项胜出。仓库 XML 与外置字典均执行相同规则。

加载器还存在成功重载不清空旧 m_objects、未验证根元素、OI/Node ID/Method/OP 基本格式的问题。应先解析临时对象集，验证成功后一次替换；失败保持上一份完整字典。

### 5.2 OP 与索引

有效属性 OP：GET 1085、GET|SET 1582、SET 1；有效方法 OP：ACTION 1857、ATCION 19。目前 OP 只是列表元数据与 Parser 的 mismatch 提示，Schema/Builder 没有强制。

19 个 ATCION 位于 F200/F202/F203/F204/F205/F207/F208/F20A/F20B/F20E/F220/F221 的方法3，FF14 的方法127/128/129，以及 F400 的方法127/128/129/130。不能在严格 OP 校验前无诊断地自动当 ACTION。应公开字典问题并让维护者确认修正。

`getAttrDataDef` 索引解析现状：index<=0 返回整根；array 跳到第一个 child；若为 structure 且 index 在范围内，返回第 index 个 child；越界仍返回 current，标量的非零 index 也返回标量。OAD 编码本身仅保证 attrId/index 0..255，没有数据定义存在/索引语义检查；属性特征位也没有独立模型。通用接口必须先明确 API 接受“逻辑属性编号”还是“完整属性描述字节”，不能掩码/默认值猜测。

## 6. 已核实的编码与验证问题

| 问题 | 代码证据/影响 | 修正落点 |
| --- | --- | --- |
| 地址双路径顺序不同 | `defaultClientRequestOptions` 反转显示地址，`optionsFromMap` 不反转；QML 把显示地址直接送 map | FrameBuilder、LogBackend；两套 FrameHeaderPanel 调用方 |
| 无效地址静默恢复默认 | Hex 宽松 fromHex；map 地址为空时 LogBackend 用整份默认 options 覆盖，方向/CA 等随之丢失 | 严格 options 解析，显式缺失/无效区分 |
| 完整帧标志与内容不符 | split=true 仅设位，未写 2 字节分帧信息；scramble=true 仅设位，APDU 未加扰 | 对尚未实现的标志拒绝；完整实现后再开放 |
| 帧字段截断 | funcCode/saType/logicAddress/ca 掩码；logicAddress 仅支持 1 bit；长度超14位被截断 | 字段范围、受支持语义、总长度检查 |
| 响应默认功能码存疑 | 两套 FrameHeaderPanel 的 serverResponseMode 将 funcCode 置1，Parser 将1解释为链路管理、3解释为用户数据 | 独立标准/真实普通响应样例核验后决定；不能因UI已有1就确认其正确，模型不得修改本地帧头 |
| 整数与枚举静默转零/截断 | 无转换成功标志，静态窄化或 &0xff；枚举不检查集合 | DataEncoder 统一严格值验证；64位十进制字符串 |
| 日期不严格 | date_time_s 允许 month/day=0 及不存在日期，date/time 无检查 | 日期/时间完整性与日历检查；通配/未指定值需明确建模 |
| 动态/未知类型写 Null | typeFromName 默认 Null，Data/option/空类型真实触发 | OadManager 保留元数据；DataEncoder 区分动态/未知/Null |
| 复合值缺失被补零 | structure 缺项 QVariant()，array 非list转空；多余结构字段忽略 | 容器类型及字段数量严格验证，错误附字段路径 |
| 枚举元数据不完整 | parseSubItem 只在 item.type=="enum" 时读取 DefVal | 归一化后解析 ENUM；严格解析枚举数值而非失败转0 |
| GET/SET/ACTION OP 未强制 | encodeDataForOad 只拿 dataDef；GET 连存在性也不要求 | ServiceSchema/OadManager 权限与定义查询 |
| 服务树薄验证 | validateTreeValue 仅检查 piid 键与部分非空列表；未知code/缺字段靠默认值流入 | 完整服务 Schema、必填/choice/类型/范围/未知键校验 |
| 结果 choice 宽松 | 普通/记录结果 choice 非1就按0编码，隐藏非法分支 | choice精确集合、分支字段互斥 |
| 方法返回值与参数定义混用 | encodeActionResultFromMap 直接 getMethodDataDef | 对无独立返回定义的情况要求显式 typed Data；不得猜返回结构 |
| 超时/块号未验证 | appendUInt16 无0..65535检查；PIID用 &0x3f | ServiceSchema/ServiceBuilder 输入检查 |
| GET_NEXT 自身不一致 | Builder 请求/响应 appendUInt16 BE；Parser_get.cpp 请求 case5 LE、响应 case5 BE | 将请求解析与其余路径统一，以独立字节向量检查 |
| 响应尾不一致及重复读取 | Builder只TimeTag；parseApdu期待FollowReport+TimeTag；SET/ACTION子函数又读TimeTag | 统一由 APDU 层处理尾域，明确响应的两个optional，测试非空时间标签 |
| “解析成功”不等于合法 | parseWithAddress 使用 `hcsOk || !fields.isEmpty()`；坏FCS/截断仍可能isValid | 严格验证结果与展示解析分开；完整帧边界/CRC/尾/APDU消费长度均检查 |
| 解析会越过 APDU 边界 | 完整帧 parseApdu 传入整个帧 data，尾解析可能消费FCS；随后外层强行移动到预定APDU末端 | 对 APDU 切片进行受界限解析，明确实际消费字节 |
| Hex 入口不一致 | Data/Service层部分检查长度；LogBackend buildFullFrame/verifyHexData 仅fromHex | 统一严格Hex入口，拒绝奇数、非法字符和丢弃字符 |
| 浮点线序依赖主机 | 编码/解析均memcpy，没有显式端序 | 独立标准向量确定线序后固定实现；检查有限值 |
| 长度编码可截断 | encodeLength只支持至0xffff但不拒绝更长值；复合数量使用固定1字节 | 显式当前上限，超限报错；需要更大规模时再扩展长格式 |

## 7. 阶段 2 的最小完整修正清单

1. **字典保真与诊断**：扩展现有 SubItem 保存 TypeOption/Selected/Size/Remark/Format/原始 DefVal，规范化类型；公开实际加载来源/版本/摘要与重复/空类型/坏OP诊断；原子重载。保持字典驱动，不引入硬编码对象白名单。
2. **严格通用 Data 编码**：未知类型报错，动态 Data 支持显式 typed value 递归；option 支持明确候选类型且保留候选Scaler；数组/结构精确验证；整数/64位/枚举/日期/Hex/长度验证，并返回字段路径。动态参数缺失交给澄清，不默认 Null。
3. **完整服务 Schema 与统一校验**：覆盖现有全部18服务和 PIID/TimeTag/RSD/MS/RCSD/结果的真实字段、choice、必填与边界；通过已确定的 `Ai698Tools.invoke` 合同公开查询/校验/组帧；所有入口复用本地验证，AI不能直接给内部 serviceSpec Hex 或 frameOptions。
4. **权限与索引**：在通用服务层验证 OI/属性/方法存在、服务方向对应 OP、索引范围及能力状态；重复/冲突沿用已确认 last-wins，附带可追溯warning。纯手工原始 APDU 调试可独立保留，不能绕回AI自动路径。
5. **帧封装及响应尾**：统一显示地址输入和反转一次的线序出口，拒绝非法/缺失地址与不支持分帧/加扰；校验本地终端快照中的帧范围和方向；以独立样例核验普通响应funcCode；统一响应 FollowReport/TimeTag 与 SET/ACTION 重复尾解析；修正 GET_NEXT 端序。
6. **独立验证**：提供严格完整帧校验/受界限APDU验证，检查HCS/FCS、长度、起止符、解析消费完整与字典约束；不能用字段非空代表成功。预览返回结构化验证问题及编码说明。
7. **小而完整的验证集**：18服务基础向量与响应尾非空变体；36类型的状态覆盖（支持/待补参数/不支持）；整数边界、64位精确输入、非法日期、非法Hex、容器字段数、越界索引、只读SET、未知方法、动态Data/option、重复字典、坏CRC/截断/多余APDU、两条地址入口一致性、分帧/加扰拒绝。对时/通信地址仅作为两个端到端样例。

变更影响面：`Prot698OadManager.{h,cpp}`、`Prot698DataEncoder.{h,cpp}`、`Prot698ServiceSchema.{h,cpp}`、`Prot698ServiceBuilder.{h,cpp}`、`Prot698FrameBuilder.{h,cpp}`、`Prot698Parser.{h,cpp}` 及 GET/SET/ACTION 子解析器；`LogBackend.{h,cpp}` 的通用导出；两套 SmartFraming 的 FieldEditor/ServiceTreeEditor/FrameHeaderPanel 调用者；独立测试 target 与构建注册。应在独立提交中处理这些变化，避免覆盖现存新版 UI 工作。

不必在阶段2补齐尚未注册的全部698服务或所有opaque结构，但必须如实暴露每个服务/字段/类型的支持状态，不能把“接受一段Hex”宣称为“自然语言通用语义组帧”。记录 rowsHex 和未结构化结构若暂留，必须阻止 AI 推测 Hex 并把待补字段列出。

## 8. 需要用户或字典维护者确认的真实歧义

可独立推进严格验证、接口与不含歧义的节点；以下内容须定点澄清，不能自动选择：

- 重复定义的运行策略已由用户确认 last-wins+warning，因此不再请求确认，也不阻止当前组帧。若后续维护字典内容，6E00/6E12/6E14/6E16/6E31 的版本差异与重复 Node 正确编号仍须可信资料，不能自行重编号、合并枚举。
- option 的多 Selected=true 表示设备兼容候选还是配置错误；对具体设备如何决定 long/double_long 与对应Scaler。在未确认时要求显式选型。
- 6 个空类型是否应为显式 Null 或其他动态 Data；19 个 ATCION 是否全部为 ACTION 笔误。
- 用户输入数字的默认语义是字典物理单位还是协议原始整数；需要明示并保证预览同时显示原始值和单位，不能依赖当前编码/解析不对称行为。
- 在当前工程标准内，已有代码没有足够证据决定时间通配值、属性描述特征位、扩展逻辑地址、浮点线序、方法返回 Data 定义的具体约定；不升级标准版本。先支持确定的普通日期/逻辑地址0或1与明确typed返回值，对其余返回待补/不支持，必要时仅请求相应现行资料或例帧。

## 9. 本阶段验收状态

- 已完成：对仓库 XML 按实际加载规则独立统计；18项服务与实际数据类型/OP/覆盖/Hex路径盘点；逐项代码静态核验；明确阶段2触达路径与必须澄清的字典问题。
- 未执行：未改业务实现，未运行应用、编译、协议往返或硬件测试，未确认运行中应用实际加载的外置字典，未引用外部标准裁定厂商歧义。
- 验收边界：本文是库存与修正依据，不能作为“已修复”“18服务已验证”或“真实终端已接受”的证明。下一步应先独立审核本文，再开始阶段2实现。

## 10. 阶段2实施记录

阶段1已由独立Astra审核准入，具体冻结合同见 `review-stage1.md`。以下描述当前实施结果；前述库存表中的原缺陷不应被误读为修复后状态。

### 已实现的共享基础

- `OadManager`：完整保留TypeOption/Selected/Size/Remark/Format/DefVal及hasScaler；大小写enum统一解析；加载成功原子替换，失败保留旧字典；last-wins保持，重复OI/Node键均有含来源和前后位置的warning；公开实际sourcePath、diagnostics与覆盖XML内容及输入合同修订的SHA-256 definitionId。
- `DataEncoder`：未知/空类型独立Unknown，不能变成Null；递归typed Data与父children零起始optionIndex；静态array/structure精确类型与字段数量；严格整数/枚举/日期/位串/Hex与数量预算；64位超安全整数使用十进制字符串。AI有单位或Scaler的整数叶子明确raw/physical和unit，physical采用字符串十进制位移及精确整除检查；旧人工入口保留raw语义。
- `ServiceSchema`：新增保留QJson类型的`validateJsonTreeValue`，覆盖18服务的必填、未知字段、类型、choice、OP、定义存在性及数据叶子；完整服务inputSchema包含RSD/MS/selector递归分支和动态Data定义。整树入口统一限制8192节点、40层深度、2MiB字符串总量；Data另有限制4096节点/32层。请求PIID的acd只能为false，响应使用PIID-ACD。
- 数值、日期、TI和64位字符串输入的约束随inputSchema公开；ACTION返回Data独立于方法入参，hasData=false禁止携带dataValue。
- `FrameBuilder`：保留旧serverAddrHex线序合同，新增明确的physicalAddressHex显示地址入口且只翻转一次；两套FrameHeaderPanel已改用后者。严格检查地址、帧头范围和14位总长度；拒绝尚未实现的分帧/加扰；错误地址不应静默变成默认目标。
- `Parser`：新增严格`validateSingleFrame`和受界限`validateApdu`，检查精确单帧长度、HCS/FCS、起止符、APDU完整消费及受支持分支。旧isValidFrame仍支持合法首帧后的日志字节；parseWithAddress按完整帧CRC判断，保留连接/安全/上报等非18项的旧解析展示。展示解析不再跨APDU边界读取CRC字节。
- 修复GET_NEXT请求解析的大小端、SET/ACTION子解析器重复读取尾域、响应FollowReport在TimeTag之前且缺省双00、浮点显式大端，以及普通响应UI功能码3。两套ActionResultEditor使用明确返回类型选择，复用FieldEditor；64位输入保留十进制字符串，复合返回值输入typed Data数组，避免套用方法入参定义。
- 修复两套FieldEditor在销毁后仍触发Qt.callLater的生命周期问题：由组件自身0ms单次Timer排队刷新，销毁时自动取消；optional显示条件显式转bool，避免缺省schema字段产生QML警告。

### 用户已明确的兼容决策

1. 重复OI/属性继续采用当前last-wins并提示，重复本身不阻止构建。
2. Size沿用现有引擎，仅为参考。公开referenceSize/sizeConstraint，不将XML所有正Size擅自解释为固定字节数；按Data类型固有宽度、TSA描述符、服务结构和总预算验证长度。实际XML存在octet_string Size=1却保存多字节报文、TSA Size=6/7混用，不能猜测统一语义。
3. AI的非零属性index/方法mode仍局部unsupported_definition；旧人工入口保留Manager能够解析的有效结构索引及已有mode范围。原FieldEditor为日期/时间统一输出的额外字段只在人工入口兼容，AI仍严格采用所属类型字段。
4. AI字节输入接受连续偶数Hex，可带唯一开头0x；不接受中间插入0x或任意分隔符。旧人工调试容错与AI边界分开。
5. 模型只提交serviceCode+treeValue；阶段4工具按service方向本地派生DIR/PRM、功能码3，终端快照冻结地址相关字段，模型不能覆盖帧头。

### 独立依据与验证边界

响应功能码3、响应FollowReport/TimeTag顺序、float32/float64大端使用独立Go实现固定commit `aa1d15614c635ca7e4661a43156584c8bc41df45`交叉核验。来源、blob和字节向量登记在 `tests/prot698_sources.md`；这属于独立实现依据，不是标准全文认证或真实设备验收。

协议实施者已在`/private/tmp`隔离直编并运行验证实施者的正式边界测试：当时565个断言、0失败，含18服务合成字典正例与独立CRC/浮点/响应向量。验证实施者随后补充整树预算、TSA、广泛解析、64位physical等回归并独立报告623个断言、0失败；最终命令和日志以其报告为准。两套真实QML组件测试已由协议实施者运行并由验证实施者复跑：19通过、0失败，仅字体别名提示，无非字体QML警告。正式完整应用回归交由主代理执行；尚未声明真实模型、目标设备或Windows通过。

### 仍显式受限的分支

- AI不接受未结构化opaque Data、MS type5/6/7.rawHex、记录rowsHex；目录保留18项，并公开具体分支限制。记录服务的DAR分支及GET_RESPONSE_NEXT普通结果分支可构建。
- 空Type、未知类型、ATCION等未知OP不猜补；只在受影响定义返回局部unsupported_definition。
- 扩展逻辑地址、分帧、加扰尚未开放；TSA必须同时满足固有长度和首字节地址长度描述一致。
- Size参考值不证明厂商语义正确；本地严格校验、独立实现向量及真实终端接受是不同证据状态。目录verificationStatus保守标pending，后续整合应按正式证据更新，不能由模型自行提升。

### 阶段4可复用接口

`Prot698OadManager::{definitionId,sourcePath,diagnostics}`；`Prot698DataEncoder::{subItemToSchema,validateJsonValue,toExactJson}`；`Prot698ServiceSchema::{serviceTypes,schemaForService,validateJsonTreeValue,toServiceSpec}`；`Prot698Parser::{validateSingleFrame,validateApdu}`。工具应使用这些同源Schema和验证入口，不能复制一套提示词专用数据约束。

## 11. 阶段7覆盖收口（现行v3，替代此前相关受限项）

已新增以下通用结构化能力，字段和条件均从ServiceSchema的完整inputSchema公开；没有`set_time/set_address`等生产特例。

| 服务/字段 | 当前可构建范围 | 本地保证 |
| --- | --- | --- |
| PROXY_GET_LIST/PROXY_GET_RESPONSE_LIST `servers[].tsa` | 显示地址、地址类型、逻辑地址结构；1..16字节地址、类型0..3、逻辑地址0..1 | 复用FrameBuilder反转一次，生成descriptor及TSA字节；内层目标不从外层暗填，模型不计算Hex |
| GET_RESPONSE_RECORD `recordResult.rows` | RCSD普通OAD列、按行的二维值数组；允许显式空rows | 每cell按当前字典验证与编码；精确列数，自动行数，含单位/64位/嵌套类型规则 |
| GET_RESPONSE_NEXT `recordResults[].rows` | 同一记录编码器及上述范围 | 保留NEXT既有块号/末帧/列表封装，不另造编码路径 |
| ROAD列 | 请求、DAR、空rows及原人工Hex路径 | 不把关联OAD数当CSD列数；非空AI cell容器仍需目标证据 |

AI不再接受代理`tsaHex`或记录`rowCount/rowsHex`；旧人工入口保持兼容，新旧表示同时指定会拒绝。`normalizedTree`中的TSA Hex、记录raw cell和行数编码均来自本地，原`treeValue/inputTreeText`保留用户参数；未改UI、controller通用分发或外层目标所有权。其他Data类型中的TSA字段仍遵循其现有已公开输入形状，本次显示地址扩展具体用于代理目标，不宣称所有地址字节字段均已语义转换。

### 独立编码依据

继续使用固定提交`aa1d15614c635ca7e4661a43156584c8bc41df45`，于本阶段通过GitHub connector读取第一手源码：

- [dt/a_result_record.go](https://github.com/vedrlabs/go-dlt698-45/blob/aa1d15614c635ca7e4661a43156584c8bc41df45/dt/a_result_record.go)，blob `5cff6f4a90b038d35bedc6fbd8e7841b01f2656c`：ARecordRow编码用列数划分行、写行数后逐cell调用Data编码，AResultRecord将它置于OAD/RCSD/choice之后。
- [dt/data4.go](https://github.com/vedrlabs/go-dlt698-45/blob/aa1d15614c635ca7e4661a43156584c8bc41df45/dt/data4.go)，blob `180248da8164ad204ccc3ee398f1f86694251e6f`：RCSD列数是CSD项数量。既有展示Parser也按CSD计列；严格Reader原来对ROAD累加关联OAD数量的计数错误已最小修正并加合成人工回归，不据此推断ROAD实际cell类型。
- [dt/data3.go](https://github.com/vedrlabs/go-dlt698-45/blob/aa1d15614c635ca7e4661a43156584c8bc41df45/dt/data3.go)，blob `fa1fdd1f9850b1711186d258460934d154390b0e`：TSA的长度、地址描述与地址负载布局有独立实现旁证。显示顺序反转仍沿用已经测试的当前工程FrameBuilder契约；没有照搬该项目TSA构造器的显示顺序或边界处理。

以上是独立实现对照，不是标准全文认证或目标设备采集。记录测试冻结按这些字段逐项推导的字节向量，未从本项目Builder反取expected。

### 验证与剩余资料

当前隔离自检：工具524次检查、0失败，独立Draft202012 JSON Schema28项通过；协议边界630次检查、0失败。日志分别为`/private/tmp/ai698-stage7-tools.log`和`/private/tmp/ai698-stage7-boundary.log`。其中覆盖TSA类型/逻辑位/1与16字节边界/地址反转/非法及缺参、旧manual字节一致；记录RECORD/NEXT、列数/类型/单位/64位、空记录、255行上限及ROAD局部限制。正式整合、Astra复核和主程序回归另由整合者记录。

仍需一条**适用于当前目标的非空CSD(ROAD)记录响应**：完整APDU或完整帧、对应RCSD/关联OAD定义，以及ROAD那一个cell解出的array/structure形状与排列。开源通用Data接口或现有解析器的合成structure提示均不足以替代该目标证据。空Type、未知OP/类型、非零AI索引/模式、opaque/MS原始分支等限制保持，不修改字典猜含义；真实模型、Windows和终端执行仍不计作已通过。
