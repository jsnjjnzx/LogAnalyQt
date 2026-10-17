# 阶段 8 本地回归与真实联调审核

审核者：GPT-6 Astra，2026-10-14。**阶段 8 在已登记的 macOS 范围内验收通过。** 最终源码、18/18全量回归及取消守卫版 L08 真实生成已核对；主线程凭据阻塞的可靠性门槛关闭。未强制原生重现长OS授权等待，不提升Windows、设备或全面标准认证状态。

## 当前证据与判定

| 边界 | 当前证据 | 审核判定 |
| --- | --- | --- |
| 最终 v3 构建 | 已读取 `validation/ai-framing/stage7-final-build.log`，70/70 完成 | 本机构建通过 |
| 全量回归 | 已读取 `stage7-final-tests.log`，18/18 CTest 通过、0 失败 | AI/698、实际应用迁移、QML、路径、CSG 各服务及协议同步本地回归通过 |
| 通用协议覆盖 | 已审核 524 项工具检查、28 项独立 Schema 检查、630 项协议边界检查及对应源码 | 在公开支持/限制范围内本地通过；非标准全面认证 |
| 控制器及 UI 生命周期 | 已核对正式控制器测试、35 项 QML 用例、真实 connectionRequestId 属性和取消清空断言 | 本地请求冻结、取消/迟到隔离及页面所有权通过 |
| macOS 安全凭据 | 随机 profile/假 key 实际存取改删，日志 PASS 并清理 | 当前平台基础接口通过，不涉及读取用户真实 Key |
| 真实接入配置和连接测试 | 已读取 live-deepseek.md：原生界面保存/工具连接成功，非敏感URL/model落盘，正常退出并重启同binary后恢复配置 | 该次保存、重启恢复和真实连接通过；审核未读取凭据 |
| 真实自然语言组帧 | L01读取日期时间、L02设置明确日期时间、L03多对象GET_LIST成功；L01复制/回填原有解析及HCS/FCS OK | 登记用例通过；其他服务不能据此全部宣称真实模型通过 |
| 缺参澄清及取消 | L04补值/对象后成功，L05提示词修复复测通过；L07原生取消状态保持、迟到无结果及保存完成；L08最终守卫版真实生成成功 | 已登记macOS范围通过 |
| Windows 原生、目标终端 | 当前无对应实际运行证据；本轮不发送设备 | 未验证／设备执行不在本轮范围 |

已复查最终关键源码：v3 摘要含字典与合同修订，代理显示地址由本地转换，记录 rows 按列校验并派生行数，ROAD 仅非空未知 cell 分支受限，旧人工入口保留；真实控制器 getter 与 QML 使用一致。当前无阻止本地整合的代码问题。

真实生成完成后需保留不含 Key 的最小证据：实际运行版本、完整用户需求、冻结终端标识/地址（按用户允许的展示范围）、所选服务与本地输入/规范化参数、成功帧、本地校验结果，以及复制/回填行为。模型成功生成一例不能提升全部服务/设备兼容声明。真实失败路径与本地故障替身测试分别登记。

## 真实联调及两项最小修复复核

已读取持续增补的 `validation/ai-framing/live-deepseek.md`，当前确认 L01–L03；审核使用独立非反射多项式 CRC 计算重新检查其中三条完整帧，起止符、声明长度、HCS、FCS 均通过，提取出的 APDU 与记录一致。L01 的复制/回填及原有解析器显示由主代理实际操作并记录，本审核没有冒称亲自点击。L02 日期字段与明确需求一致，L03 两个 OAD 顺序保持；没有新增业务专用分发。

凭据修复已在 review-stage3.md 记录：实际保存记录决定旧 profile 所有权，展示默认值不构成删除依据。该风险不能解释尚未复现的先前保存现象；两者分开登记。

prompt 修复只明确通用语义：“当前终端/所选终端”指冻结目标，无需重复确认；只有用户另给不一致的操作目标才先澄清。澄清字段前也先查询定义，类型/长度/格式只能根据实际定义说明，不编造。没有硬编码 OI、业务动作或放宽本地校验。已读取 `stage8-prompt-fix-build.log`、`stage8-prompt-fix-tests.log`，应用/controller 重建通过，settings/controller 两项测试通过；隔离 controller 125 项通过。**这些断言证明提示词及流程未回归，不能替代 L04 的真实模型行为验证。**

## 建议提交分组（仅方案，未执行）

现有 `39f0a1d` 保留用户原有 UI 基线、`1e3f50e` 为阶段 1、`3398a5e` 为阶段 2，不重置、不混写。后续建议按依赖顺序拆四组：

1. **模型接入基础**：AiCredentialStore、AiModelSettings、DeepSeekClient 及各自测试/原生凭据检查；CMake 仅纳入 BASE_SOURCES、系统框架链接与 client/settings/native-check 测试部分。
2. **通用工具及 v3 覆盖**：Ai698Tools、ServiceSchema/OadManager/Parser 的当前 v3 增量、tools/boundary 测试及相应 CMake 工具目标和共用测试源列表；同步 v3 合同、能力、验收说明。保留 ROAD 精确限制，不用“全协议支持”作为提交标题。
3. **控制器和页面接线**：AiFramingController、LogBackend/main 的 facade/注册、Next AI/系统设置/Main/Navigation/QML CMake、对应控制器与 QML 测试；CMake 增加控制器源及其测试目标。此组依赖前两组，确保独立提交不引用尚不存在的源文件。
4. **验收和交付记录**：审核记录、执行状态及选定的本轮脱敏日志/截图。阶段 8 尚在联调，文档必须保留当前边界；后续真实结果另作小提交，不提前写完成。

共享 CMake 需要按上述依赖拆分暂存差异，不能在第一组整文件暂存而引入尚未提交的 Controller/Tools。每组提交前核对 staged 文件清单与 diff；若不便安全拆分互相依赖的 CMake，宁可合并相关实现组并明确范围，也不制造不可构建的中间提交。

只纳入明确本任务文件和证据，不使用全量 `git add`。`.superdesign/`、外部南网规范 Markdown、Terminal3.log、csg-upstream-appendix-i-envelope-design.md、docs/superpowers/、既有 ui-preview/ 及无关 validation 目录保持原样。本轮未提交、未暂存任何文件，也未读取 Key 或启动共享构建。

## 新发现：系统凭据阻塞主线程（重新开审）

已只读核对 `/private/tmp/loganalyqt-ai-ui-sample.txt`：2026-10-14 22:06 的隔离 preview 进程采样中，主线程的 183 次采样均落在 `AiFramingController::submit → AiModelSettings::keyForRequest → AiCredentialStore::read → SecItemCopyMatching → SecurityServer::ClientSession::decrypt → mach_msg` 调用链；主代理同时报告原生 AX 连续超时。该证据足以确认主线程正在等待系统凭据读取，**不能证明等待原因就是授权弹窗、ad-hoc 升级、重签或先前配置问题**。

Apple 的 [SecItemCopyMatching 文档](https://developer.apple.com/documentation/security/secitemcopymatching(_:_:)) 明确说明此 API 会阻塞调用线程，建议后台队列或异步调用；[Security 并发说明](https://developer.apple.com/documentation/security/working-with-concurrency) 也提示某些操作可能等待用户，避免多个队列/线程并发调用。`kSecUseAuthenticationUIFail` 只影响需要认证时的返回行为，且已弃用，不提供系统 IPC 硬超时，因此不能单独作为本缺陷的修复依据。

与实施者协商的最小安全方向：所有 UI 可达的系统凭据调用移到一个串行后台执行器，包括属性查询、submit/probe 读取、save/clear 及回滚；getter 使用缓存，不在绑定求值时调用系统存储。读取等待期间界面必须可响应，取消/超时使请求失效，迟到读取不得发 HTTP 或写成功结果。系统调用本身不能被强杀线程；worker/store 生命周期不能依赖已销毁控制器，也不能在主线程析构时等待它返回。未返回的操作必须有数量限制，不能重复提交无限创建线程或积压事务。

保存/删除是有副作用的事务：异步接口只能在实际完成后报保存/删除成功；开始后不能将超时或页面关闭描述成“已撤销”。同一存储事务仍需串行，失败/晚返回结果应刷新可信缓存。该方向尚待实施和审核，不把方案写成已修复。

安全恢复不删除现有配置/profile、不改钥匙串 ACL、不终止系统安全服务。若当前隔离验收实例确无未保存业务编辑且无法正常退出，重新核对实例身份后仅结束该应用实例，再运行修复版；系统授权由用户按正常系统流程完成。先使用阻塞 fake store 验证 UI 心跳、取消/超时、晚返回、销毁和事务串行，再进行真实环境恢复验证，不借测试访问真实 Key。

前三组实现已由主代理提交 `ad00f17`、`4100bfd`、`7a4e663`；本可靠性修复必须作为后续单独提交及回归记录。已有阶段通过结论是此前证据范围，不能掩盖这次新发现。

后续用户确认：当时确有系统授权窗口，用户已自行处理，应用窗口恢复可点击。这补充了系统授权等待的实际背景；仍不能据此确定为何触发授权或归因于 ad-hoc 更新。系统正常授权可能持续等待，UI 线程直接承受此等待的设计问题仍成立，后台化修复及其取消/生命周期回归不撤回。修复后可在已正常授权的环境复测，不要求用户重新暴露密钥或改变安全策略。

### 已协商的实现与重新验收门槛

认可实施者采用一个 worker QObject 独占 settings 与共享所有权 credential store，移入无 controller parent 的 QThread；输入冻结后 queued 调用，结果以 typed queued 信号回主线程，控制器析构只断开及 quit，不 wait/terminate。worker 与线程完成后按 Qt 的 finished/deleteLater 模式释放；测试注入 store 也必须有足够生命周期，不能保留会悬空的借用指针。此为当前单控制器内的串行事务，不声称跨进程原子性。

重新验收必须看到以下证据后才能解除阻断：

- 阻塞初始化 contains 或 submit read 时，UI 事件循环心跳继续；所有系统 store 调用线程均非 UI。
- 取消/只读超时后迟到 key 不会发 HTTP、构建帧或覆盖新状态；未完成物理操作仍有明确 busy/waiting 状态，不无限排队。
- 保存/删除的 pending 回应不清空输入、不显示完成；只有匹配 operationId 的实际完成结果才更新成功状态。系统事务开始后不能谎称取消已撤销。
- 控制器在阻塞读取期间、以及刚构造即销毁时均不等待、不发生 UAF；store 保持存活至线程真实完成后释放。
- 原配置失败恢复、跨 endpoint 不复用、清 Key、连接测试、工作流及 QML 状态全部回归；worker 的 key 只用于 C++ 私有续接，不进入面向 QML 的完成结果。
- 修复版在当前已授权 macOS 环境正常使用；真实授权等待场景若未再次触发，应明确只由阻塞替身证明响应性，不能虚构原生复现通过。

初稿静态复核：worker 持有共享 store、队列调用与私有 key 续接、控制器析构不等待的路径符合方案；已读取异步 QML 日志，43 项通过。仍向实施者提出两项收口：凭据 completed 回调在停止定时器前检查实际只读 deadline，防止 timer 事件尚未派发时放行迟到结果；Main 标题的旧 hasApiKey 二选状态也需识别 checking/waiting/failed，不能将未知状态显示为未配置。修复与最终测试完成前不关闭此次可靠性问题。

后续静态与隔离测试复核：上述 deadline 和 Main 标题均已修。实际读取 `/private/tmp/loganalyqt-ai-foundation-build/credential-worker-controller.log`、`credential-worker-asan-tests.log`，普通/ASan 各 294 项、0 失败；settings/client 各通过，QML 当时 43 项通过。对应测试验证阻塞 contains 的 UI 心跳、纯缓存 getter、读取取消/超时迟到零 HTTP、拒绝第二事务、变更事务 pending 及实际成功/失败、销毁不等待/store 保活释放，并刻意只派送 completed MetaCall、不派发超时事件，确认逾期生成/连接均不能发 HTTP。C++ 可靠性代码门槛通过，但不等于整体异步交付已完成。

主代理随后发现异步 UI 回归：probe 开始/结束发 settingsChanged，设置页在没有 ownedSettingsOperationId 时 loadConfiguration，会覆盖用户尚未保存的测试 URL/model 草稿。该项已交界面实施者修复并补回归；正确边界为状态通知不改草稿，初始化或匹配的保存成功才刷新配置，清 Key 不应顺带覆盖其他 endpoint 草稿。最终 UI 修复及全量正式回归完成前，阶段 8 仍未通过。

最终草稿保护已静态复核：设置页不再因 settingsChanged 重载输入；只在初始化/controller 更换或匹配保存成功时刷新，清除成功只清 Key。已读取 `/private/tmp/loganalyqt-stage2-tests/async-settings-draft-tests.log` 的45项 QML通过日志，并检查 probe 开始/读取完成/网络完成/取消均保留未保存URL/model/key、后续保存参数一致及clear不覆盖草稿的用例。普通/ASan 294项日志已归档为 `stage8-async-controller-tests.log`、`stage8-async-asan-tests.log`；`stage8-async-final-build.log` 已完成14/14主程序链接与打包。当前无剩余源码审核阻断，等待主代理最终正式全量CTest及原生已授权环境复测后关闭可靠性门槛。

## 最终取消守卫与 macOS 收口

已复核 `AiAssistantPage.cancelRequest` 最后修正：必须同时拥有当前 requestId，且控制器仍 busy 或处于 clarification，才清页面 ID 并调用取消。请求已成功时迟到的点击直接返回，不隐藏已成功结果；外来请求同样不受影响。与 C++ 取消状态检查一致，未改变协议、Key 或模型调用逻辑。

已实际读取 `stage8-final-build.log`、`stage8-final-tests.log` 和 `stage8-final-detailed-tests.log`：最终应用重新链接完成；18/18 CTest 通过、0 失败，总计27.31秒；详情包含QML46、工具524、控制器294检查通过。异步凭据、设置草稿及取消守卫当前无剩余源码阻断。

已读取 L07：后台凭据版提交后约0.42秒可访问性查询返回，显示准备读取与可用取消入口；生成中原生取消后再次观察仍取消且无迟到结果；同配置留空Key保存实际完成。此时系统已由用户正常授权，不能冒称再次原生复现长期授权等待；该长期等待、完成回调抢先于timer、对象析构等由阻塞替身及ASan验证。

已读取 L08：实现提交 `1ef45ab` 重启恢复配置，先出现后台凭据准备状态，随后真实 GET_NORMAL 4000/2 成功，APDU `0501024000020000`，完整帧与已独立核验 CRC 的 L01 一致；参数、完整结果及复制/回填入口保留。Git 当前实现提交亦核对为 `1ef45ab`。阶段8 macOS边界正式关闭，允许进入本轮交付。

Windows原生/凭据、未支持ROAD分支、设备执行和全面标准符合性继续保留既有边界；有限真实用例通过不提升为所有模型请求均可靠。
