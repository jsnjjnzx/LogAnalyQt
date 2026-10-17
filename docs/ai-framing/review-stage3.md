# 阶段 3 模型设置与网络基础独立审核

审核者：GPT-6 Astra。日期：2026-10-14。审核 `src/core/ai/AiCredentialStore.*`、`AiModelSettings.*`、`DeepSeekClient.*` 和对应测试。**阶段 3 基础类本地验收通过，macOS 系统凭据生命周期实测通过；系统设置 UI 和完整模型流程留在后续接线验收。**

**后续可靠性状态更新：** 阶段 8 原生联调发现系统凭据读取阻塞主线程，凭据异步边界重新开审；此前成功测试不覆盖系统服务长期等待。当前不能宣称该边界已完整交付，采样事实与修复验收要求见 review-stage8.md。

## 已检查的边界

配置类仅将 URL/model 保存至普通 INI，密钥通过独立平台凭据实现；endpoint 规范化后生成 profile 摘要，相同 key 不自动跨 endpoint 使用。保存失败和旧凭据清理失败有恢复路径，并显式报告无法恢复的情况；临时连接 key 不写入配置。macOS 与 Windows 分别使用系统凭据 API，其他平台不明文降级。

网络类只发 HTTPS POST，禁止自动重定向、检查证书错误，具有 45 秒超时、响应及 arguments 大小限制。取消先断开/清空活动 reply，再 abort，旧回调按 reply 身份隔离。供应商错误正文不直接转发 UI；连接测试要求指定工具正确返回随机 nonce，不将 HTTP 200 单独当成成功。

上述源码边界已结合下述最终构建、测试与 macOS Keychain 实测核对。Windows Credential Manager、真实 DeepSeek 和 QML 设置页面接入不在本次基础类通过声明内。

## 已冻结的最小行为及合同同步项

主代理已确定：HTTP 响应 envelope 不合法、arguments 不是合法 JSON object、未知工具名，直接安全失败并终止请求。只有**合法工具调用中的业务/Schema 校验失败**才作为 tool 结果返回模型，在控制器总预算内修正。

这与当前 DeepSeekClient 的安全失败实现一致，不需要新 callback 通道。contracts.md 第 5 节已同步上述两层边界，阶段 5 状态机仍须覆盖它。未知工具/坏 JSON 不应作为可成功执行结果，合法参数的业务错误也不应误变成整个网络层协议失败。

## 验收范围

- 已验证正式 settings/client 测试：保存/恢复失败、跨 endpoint 不复用、临时 key 不持久化、假 key 不进 INI/错误消息，以及取消、超时、迟到回复、响应上限和 nonce 错误。
- 已验证 macOS 主程序和测试链接；平台编译证据与真实系统凭据行为分开记录。
- 实际设置页、控制器 facade 和真实账号连接在后续阶段验收；阶段 3 目前是基础类交付，不能写成设置 UI 已可用。

未发现剩余基础类阻断，允许整合。审核未读取任何真实密钥、未发真实 API 请求、未运行 Windows；macOS 凭据实测由主代理执行，审核核对其源码和日志。

## 后续收到的本地证据

已读取 `validation/ai-framing/stage3-foundation-tests.log`：正式 CTest 的 `ai_client_tests`、`ai_settings_tests` 两项目标均通过（0 失败）。审核同时读取了两套测试及生产源码，确认覆盖配置恢复、跨 endpoint、临时 key、假 key 脱敏、工具 envelope、取消/迟到回包、超时及连接 nonce；不是仅凭退出码接受。

最终 `stage2-final-build.log` 已确认主程序链接，`stage2-3-integration-tests.log` 再次确认 client/settings 两项目标通过。

另已读取 `native-credential-tests.log` 和 `tests/ai_credential_native_check.cpp`：测试使用随机 profile 与临时假 key，在 macOS 系统存储实际执行写入、存在检查、读取、替换、删除及删除后不存在检查，结果 PASS；该路径不读取用户真实凭据，并确认清理。它证明当前 macOS 的凭据基础接口可用，不证明 Windows Credential Manager 或真实 DeepSeek。

合同错误分层已同步，不需要扩展网络 callback。系统设置 UI 接线仍另行验收。

## 阶段 8 最小修复复核

**阶段 3 基础可提交，新增修复审核通过。** `saveConfiguration` 从同一 QSettings 快照读取实际 oldUrl/oldModel；只有 oldUrl 存在且能规范化才认为有旧配置。首次在只有 UI 配置或空 INI 的实例保存非默认 endpoint，不再将展示用默认 URL 当作该实例拥有的旧凭据，也不再自动删除其 profile。首次没有接入记录且留空 Key 时明确拒绝保存，不隐式借用默认 profile 完成保存。

新增测试用 fake store 记录 removedProfiles，并预放另一个实例的默认假 key，验证新 profile 增加、默认 profile 不被删除及空 Key 拒绝。实际读取 `stage8-settings-fix-build.log` 和 `stage8-settings-fix-tests.log`：应用/settings/controller 重建通过，两项定向 CTest 全通过；隔离日志另确认 settings PASS、controller 123 项通过。没有测试访问真实凭据。

此确定性风险由代码及隔离回归证明，**不等于先前“用户报告保存后未配置”现象的原因**；先前现象未复现、原因未证实。后续用户在软件重新保存，主代理观察非敏感 URL/model 落盘，并正常退出、重启同一 binary 后配置恢复且可真实生成，证明该次保存持久化成功。审核没有读取真实 Key。
