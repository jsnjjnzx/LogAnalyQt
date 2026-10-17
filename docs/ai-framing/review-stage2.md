# 阶段 2 独立审核

审核者：GPT-6 Astra。日期：2026-10-14。基线：阶段 1 提交 `1e3f50e`；审核当前未提交的协议、两套人工组帧界面及正式边界测试差异。

## 当前结论

**阶段 2 本地验收通过。** 已核对提交 `3398a5e` 及最终调用者修复，未发现剩余阻断。独占应用构建和集成回归通过；本结论不等于标准认证或设备验收。

## 独立来源与允许声明

`tests/prot698_sources.md` 将固定 Go 提交 `aa1d15614c635ca7e4661a43156584c8bc41df45` 中的固定请求 literal、响应实现推导、浮点实现推导和 RFC CRC 方法分别登记。证据类别合理：可支持当前工程的普通响应功能码 3、FollowReport→TimeTag 和浮点大端修正，但不是标准全文核验或设备认证。一个固定 GET 帧不证明全部 18 项业务标准正确。

## 已确认改善

- 未知/空类型不再默认 Null；QJsonValue 严格类型校验先于 QVariant 转换，动态 Data、option 候选及 64 位字符串保留明确表示。
- 原子字典替换及 SHA256 定义标识已实现；重复 OI/成员保持 last-wins 并留诊断；本次不是标准版次升级。
- 单帧长度上限、帧头参数和非法 Hex 检查已加入；`isValidFrame` 保留日志前缀语义并补 HCS/最小边界；另有严格单帧与受界限 APDU 消费入口。
- 响应尾、PIID-ACD、GET_NEXT 字节序及浮点线序有集中修正；ACTION 返回 Data 不再自动复用方法入参定义。
- 正式测试使用独立固定请求和独立 CRC 实现，同时保留合成字典的 18 服务逻辑检查；合成字典明确未冒充标准对象。

## 审核发现及修复追踪

| ID | 级别/问题 | 最小修正与复核要求 | 当前状态 |
| --- | --- | --- | --- |
| S2-01 | P1：GET_RECORD 的 RSD 只有 object/description，没有分支字段定义，模型无法查询完整输入合同 | 暴露 RSD/MS/selector 递归 Schema、choice 分支及不支持分支；验证 Schema 与实际输入一致 | 实施者已补 `$defs.rsd` 和分支，已静态看到，待正式测试 |
| S2-02 | P1：shape 遇无 properties/type 的 RSD/Data 不遍历；叶子校验器计数各自重置，可绕过全树数量限制 | 入口统一扫描整个 JSON 树节点、深度及字符串预算，再做语义校验；加跨多个叶子的总量负例 | 实施者已补统一预算，待边界测试和最终复核 |
| S2-03 | P1：member 对 AI 和人工均禁止非零 index/mode，旧人工合法结构索引被误拒绝 | AI 未确定分支保持明确不支持；人工索引按 manager 有效定义范围验证，mode 保留旧合法范围 | 实施者报告已修，待正式旧调用者测试 |
| S2-04 | P1：TSA 只限制总长度，描述字节与实际地址长度不一致仍通过 | JSON 与严格 APDU TSA 入口都核对描述和长度；扩展逻辑地址无明确支持时拒绝 | JSON 修复已静态看到，待双入口测试 |
| S2-05 | P1：两套 ACTION 响应编辑器仍加载方法入参 Schema，输出普通值；新编码器要求 typed 返回值，hasData=true 无法正常使用 | 两套界面提供明确返回类型和值入口，不按入参猜返回类型；覆盖真实界面到构建调用 | 正在修复，不接受只构造 JSON 的替代测试 |
| S2-06 | P1：parseWithAddress 调用 validateSingleFrame 默认开启限定 18 服务 APDU 检查；LogBackend 据 isValid 分流，使既有连接/释放/上报/安全帧被误拒绝 | 广泛解析使用完整帧完整性检查；AI 成功链单独强制限定 APDU 严格验证；保留非 18 项有效帧回归 | 已报实施者与主代理，待修复 |
| S2-07 | 已澄清：Size 元数据语义与固定长度约束 | 用户明确沿用现有引擎，Size 仅参考；长度按数据类型和服务结构校验，不从不一致 Size 推断定长或关闭字段。固定长度原子类型、TSA 描述和总预算仍强制 | 需求已确认，此项不再阻断；仍需执行类型/结构长度负例 |
| S2-08 | P1：旧 FieldEditor 对 date_time_s/date/time 一律输出完整日期时间键；新严格校验误拒绝 week/ms 等，使人工对时失败 | AI 保持精确键；人工兼容现有已知日期编辑器键或修改两套调用者，并按实际类型验证 | 实施者报告已补 manual 兼容，待真实输出形状回归 |
| S2-09 | P2：请求 PIID 接受 acd=true 后编码静默丢弃；整数/TI/日期定义不公开范围 | 请求明确 acd=false 或缺省，响应才允许 true；输入 Schema 展示本地真实范围 | 实施者报告已修，待最终复核 |
| S2-10 | P2：AI Hex 任意位置移除 `0x`，会将 `A0xB` 等非法内容改成有效字节 | AI 仅接受连续 Hex 或单个开头 `0x`；人工保留约定容错。新增非法中间前缀负例 | 已静态看到修复，待测试 |
| S2-11 | P1：LogBackend.buildFullFrame 无地址的非空 options 被整个默认对象替换；例如 split=true 被清掉后成功，dir/prm 等也会丢失 | 只补缺失的地址并保留其他已校验字段，或明确拒绝这种输入；覆盖无地址 split/scramble 拒绝和合法方向保留 | 已静态复核修正：仅填 serverAddr、传播默认地址错误；MigrationUiChecks 已加分帧/加扰拒绝和 0x83 控制域保持断言，待正式 app 回归 |

## 已复核验证与最终退出条件

已读取 `validation/ai-framing/stage2-protocol-reviewed.log`：623 个协议边界断言、0 失败。新增测试覆盖了本审核提出的 64 位 physical 极值/溢出、TSA 描述、manual 日期完整键与 AI 精确键区别、非 18 项 LINK/REPORT/SECURITY 的日志路径保留、有效人工索引、全树节点/深度以及请求 ACD。测试中的旧服务例帧明确是合成回归，不作为标准 golden。

已读取 `validation/ai-framing/stage2-action-editors-reviewed.log`：19 项通过、0 失败；包括两套真实 ServiceTreeEditor 的单项/列表共四种 ACTION 响应编辑交互，检查显式类型、64 位字符串、复合 typed Data、Null 及关闭 hasData 后不输出 dataValue。测试后端只提供定义替身；协议目标另验证相同 typed 值的实际编码，因此不能把它写成设备端到端测试。

最新代码已静态确认：S2-01/02/03/04/05/06/08/09/10 修正落实；S2-07 按用户明确 Size 参考语义处理。再次执行 `git diff --check` 无问题。

最终已读取 `stage2-final-build.log` 的主程序链接完成记录及 `stage2-3-integration-tests.log`：ui_migration_runtime、prot698_boundary_tests、ai_client_tests、ai_settings_tests 四项目标全部通过。`stage2-3-ctest-detail.log` 确认协议断言为 630 项、0 失败，并保留实际迁移解析、隔离多页签、恢复与错误保护回归输出。`stage2-qml-tests.log` 为 19 项通过。S2-11 的真实后端无地址分帧/加扰拒绝、方向保持和非法输入负例在 MigrationUiChecks 中执行通过。

上表的“待”是发现与修复追踪历史，全部本阶段代码阻断现已关闭；以本节最终复核为准。允许整合阶段 2，不因设备联调缺失而冻结已明确的确定性能力。

最终差异 `git diff --check` 无问题，两套界面和已有解析/人工组帧实际回归已核对。当前未验证真实 DeepSeek、Windows、设备行为；这些不属于阶段 2 本地验收声明。

未提交 Git，也未并发修改协议实施者拥有的文件。独立来源的协议含义仍是实现旁证；任何发布说明不得写成标准认证或设备已接受。
