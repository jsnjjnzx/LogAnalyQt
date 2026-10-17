# 阶段 7 通用覆盖独立终审

审核者：GPT-6 Astra，2026-10-14。范围：当前 v3 的 ServiceSchema、Ai698Tools、字典修订标识、严格 Parser 及对应工具/边界测试。**阶段 7 本地验收通过，允许整合。** 已核对最终 v3 构建日志和 18/18 正式 CTest，无剩余本阶段代码阻断。不自行运行共享构建，不读取真实 Key。

## 实际代码结论

1. 代理目标在 AI Schema 中要求 `tsa.address/addressType/logicAddress`，缺字段、错误类型和未知键先被拒绝，再复用本地显示地址转换生成描述字节和反转后的地址。AI 不能再提供 tsaHex；旧人工入口仍可使用 tsaHex，新旧表示不能同时给出。内层目标没有从外层终端暗填。
2. 记录 choice=1 使用二维 rows，Schema 与语义校验分别控制容器、数量、列数及具体列定义。普通 OAD 列按当前字典的 DataDef 逐 cell 校验，包含单位、physical/raw、动态 Data、option 和无损 64 位规则；行数由 rows.size() 生成，不接收 AI rowCount/rowsHex。
3. 规范化后的行值继续走同一 toServiceSpec/Encoder；toServiceSpec 现在使用校验产出的规范化树，因此不会丢掉新转换。编码器私有记录方法仅在这条已校验路径调用，cell 索引有前置精确列数检查。RECORD/NEXT 共用相同记录编码入口。
4. 非空 ROAD cell 返回具体 unsupported_definition；请求、DAR、空 rows 和旧人工明确字节路径保留。没有根据关联 OAD 数量猜 cell 容器。严格 Reader 改成每个 CSD 一列，与记录封装层级一致。
5. definitionId 合同前缀从 v2 升为 v3，并继续包含实际 XML 内容摘要；控制器在下一回调/工具前检测修订变化，旧会话不能混用新定义。重复定义仍 last-wins+warning，Size 仍为参考。
6. choice 缺字段错误选择按已提供的常量分支匹配，改善缺 rows 时误提示 DAR 的问题；没有放宽输入集合。RSD 精确字段路径修正已由测试覆盖。

## 独立依据复核

本审核通过 GitHub 连接器重新读取固定提交 `aa1d15614c635ca7e4661a43156584c8bc41df45` 的第一手文件，不只转述实施者结论：

- [a_result_record.go](https://github.com/vedrlabs/go-dlt698-45/blob/aa1d15614c635ca7e4661a43156584c8bc41df45/dt/a_result_record.go)：记录行编码在行数之后依次调用 Data 编码，记录结果包含 OAD、RCSD 和分支。
- [data4.go](https://github.com/vedrlabs/go-dlt698-45/blob/aa1d15614c635ca7e4661a43156584c8bc41df45/dt/data4.go)：RCSD.Length 返回 CSD 项数，支持严格 Reader 的最小列数修正。
- [data3.go](https://github.com/vedrlabs/go-dlt698-45/blob/aa1d15614c635ca7e4661a43156584c8bc41df45/dt/data3.go)：TSA 长度、描述字节和地址负载的布局有旁证；其 Encode 对类型位及 16 字节边界存在局限，不能作为整套地址行为真值。本实现未照搬这些处理，显示顺序和可用逻辑地址范围继续采用当前工程已经验证的合同。

这些证据是独立实现交叉核验，不是适用标准全文、厂商认证或设备采集。尤其独立实现支持任意 tagged Data，并不能确定目标 ROAD cell 应选择 array 还是 structure。

## 已读取的测试证据

- `/private/tmp/ai698-stage7-tools.log`：524 个工具断言，0 失败；28 项独立 Draft202012 JSON Schema 检查通过。
- `/private/tmp/ai698-stage7-boundary.log`：630 个协议边界断言，0 失败。
- 审核阅读了对应新测试：结构化 TSA 的类型位/逻辑位/边界/反转及旧人工字节一致；普通列 RECORD/NEXT、physical 转换、嵌套结构/数组/uint64、空记录、255/256 行、精确列数、缺 rows 路径；ROAD 非空拒绝、空行可用及人工显式 tagged Data 的一列回归。
- v3 之前的七组正式集成日志 `stage4-6-integrated-tests.log` 全部通过，证明阶段 4–6 已接入；不能代替本次 v3 最终构建。真实 controller.connectionRequestId 已暴露并有 property/取消后清空断言，先前 getter 缺口关闭。
- 最终读取 `validation/ai-framing/stage7-final-build.log`：构建完成至 70/70；`stage7-final-tests.log`：18/18 CTest 通过，0 失败，涵盖 AI/698 新路径、实际应用迁移、QML、路径、既有 CSG 各类服务以及协议同步回归。复查当前源码中的 v3、tsa/rows/ROAD 分支及真实 connectionRequestId，未发现与已审核合同不一致的新增变更。`git diff --check` 无问题。

## 剩余边界

非空 ROAD 仍需当前目标的一条正确响应及 RCSD/关联 OAD 定义、单 cell 容器和排列证据；空 Type、未知 OP/类型、非零 AI 索引/模式、opaque/MS 原始分支、扩展逻辑地址、分帧和加扰仍明确限制。代理结构化 TSA 仅扩展 servers[].tsa，不宣称所有 Data 内地址字段均已语义转换。

用户已通过软件保存接入配置，主代理报告真实连接测试成功；真实组帧仍在原生 UI 联调，尚未计作通过。最终 v3 联调必须使用 v3 构建实例。Windows 原生行为及设备执行未验证，设备发送本就不在范围；阶段 8 继续单独记录。
