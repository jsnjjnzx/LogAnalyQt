# 阶段 4 通用工具独立审核

审核者：GPT-6 Astra，2026-10-14。对象：`Ai698Tools.{h,cpp}`、阶段 2 共用校验器及 `tests/ai698_tools_tests.cpp`。**阶段 4 本地验收通过。** 工具层代码、隔离测试及七组正式主程序集成测试已核对，无剩余本阶段阻断。

## 已检查边界

五工具为白名单分发，参数只接受 JSON object；没有反射、文件、网络或任意执行入口。每次调用核对冻结 definitionId 和本地终端上下文，模型参数中无可写终端/帧头。默认仅补运输层 PIID 与 absent TimeTag；显式序号不匹配拒绝。build_frame 重新走严格业务校验、已有确定性编码器和严格单帧验证，方向/功能码由本地服务目录决定。

字典来自实际运行时 manager，重复 warning 去掉本地路径后提供给模型；非零索引、opaque、记录行等限制继续明确返回。定义超预算返回错误，不截断为不完整 Schema。查询结果中的支持状态与独立验证状态分开，没有设备通过声明。

## 发现与修复

| ID | 问题 | 要求及状态 |
| --- | --- | --- |
| S4-01 / P1 | definitions 合并 get_definition 两种参数时将根 properties.kind 覆盖成 const object；合法 service 分支与根 Schema 矛盾。invoke 直接调用测试无法发现 | 已修并通过独立 Draft202012 Schema 验证：根 enum service/object，分支各自 const；正例和跨分支负例均有实际检查 |
| S4-02 / P2 | build 结果只带原始 treeValue，physical 转换后的 raw 没有展示出口；摘要仅列 OI/成员，UI 无法核对模型填入的值 | 工具端已新增 normalizedTree/inputTreeText/normalizedTreeText，均由本地生成；physical、日期、uint64 精确文本有断言。工具端关闭，界面展开另归阶段 6 |

## 已核对的验证

实际读取 `/private/tmp/ai698-tools-stage4.log`：工具测试 409 项、0 失败；独立 JSON Schema 21 项通过。检查测试与源码后确认覆盖实际定义 Schema、两种 get_definition 分支、18 服务查询、冻结 PIID/目标/定义、错误工具与参数、opaque 禁止、physical/uint64、预算及完整帧。Schema 检查使用本机已有 Python jsonschema，与生产 C++ 校验逻辑独立；其他机器缺依赖时会显式 SKIP，不能将 SKIP 计作这项通过。

另复核 RSD 错误路径修正，避免给 selector 值追加不存在的 dataValue 路径。最终 `git diff --check` 无问题。没有自行运行共享构建，避免与主代理冲突；临时日志由主代理归档后再记录正式整合结果。

未验证真实模型使用这些工具的成功率；本阶段只审本地工具接口和确定性行为，不等同设备或标准认证。

代理内层 TSA 当前仍为明确的编码 `tsaHex` 输入；已有描述字节/长度校验不等于自动将自然语言显示地址转换为 TSA。阶段 7 必须区分用户提供编码 TSA 与显示地址：未实现本地转换时，应披露/澄清所需输入，不能把模型自行算出的字节当作已验证地址转换能力。

阶段 7 复核更新：七组正式集成测试已通过，阶段 4 正式本地 gate 关闭。此前代理输入限制已在 v3 中由本地结构化 `servers[].tsa` 转换解决，记录 rows 同时扩展；新范围和剩余 ROAD 限制以 review-stage7.md 为准。
