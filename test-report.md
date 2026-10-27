# 测试结果

目标：spec.md 中 PARSE-01～06、RELEASE-01～03、PROCESS-01。建立于 2026-10-26。

## 2026-10-25 本地实现与交付证据（补录）

以下记录来自本次会话实际执行与固定测试包记录；版本为 4.9.1 加未提交的解析表格改动，不能替代 v5.0.0 远端验收。

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 通用 QML / 共用表格 | `python3 tests/prot698_qml_tests.py` | 83 passed，0 failed |
| SSH、同步与独立窗口 QML | `python3 tests/stage10_qml_tests.py` | 76 passed，0 failed |
| 本机构建 | `cmake --build build/macos-debug --target appLogAnalyQt -j 4` | 通过 |
| 相关协议 | prot698_boundary_tests 与 5 组南网上行基础/服务/数值/测量/透传测试 | 6/6 通过 |
| 应用运行时 | ui_migration_runtime、ui_mqtt_parse_runtime、ui_parse_table_runtime、prot698_boundary_tests | 最终 4/4 通过；45.54 秒 |
| 原生窗口 | `QT_QPA_PLATFORM=cocoa` 下运行 `tests/test_ui_migration_runtime.py ... --parse-table-only` | 手动、日志窄面板、对比三种视图通过 |
| 固定目录成品 | `macdeployqt`、库 ID 归一化、ad-hoc 签名、`scripts/ci/check_build.py --macos-bundle ...` | 126 个随包 Mach-O 为 arm64，依赖仅为系统或随包库；签名通过 |
| 脱离开发环境成品 | 将打包应用迁移后，用系统 PATH、无开发 Qt/QML/DYLD 环境和临时配置执行完整 Cocoa、重启保存、复杂表格和 MQTT 选区回归 | 全部通过；个人配置与其他 23 个交付文件校验不变 |

本地成品证据：`release_package/通联工坊-20261019-本地验证/验证结果.json`、`成品回归.log`、`解析表格-*.png`。这些本地文件不纳入版本源码提交。

## v5.0.0 提交前验收

执行日期：2026-10-26。以下为 CMake 5.0.0、当前工作区改动及审查修正后的实际结果。

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 构建与版本 | `cmake --build build/macos-debug --target appLogAnalyQt prot698_boundary_tests -j 4`；读取应用 Info.plist；`python3 scripts/ci/check_build.py` | 构建通过；应用与项目版本均为 5.0.0 |
| 发布工具 | `python3 -m unittest tests.test_release_automation tests.test_ci_packages tests.test_github_release tests.test_ci_workflow -q` | 授权回环监听环境 38 tests，OK；沙箱内有 3 个模拟 HTTP 服务绑定被拒绝，不作为产品失败 |
| 通用 QML | `python3 tests/prot698_qml_tests.py` | 83 passed，0 failed |
| SSH / 同步 QML | `python3 tests/stage10_qml_tests.py` | 76 passed，0 failed |
| 应用 / 协议 | `ctest --test-dir build/macos-debug -R '^(ui_migration_runtime|ui_mqtt_parse_runtime|ui_parse_table_runtime|prot698_boundary_tests)$' --output-on-failure` | 4/4 通过；44.28 秒；临时配置与本机回环 |
| Release 说明生成 | 临时 Git 仓库复制当前 CMake/CHANGELOG，创建仅限临时仓库的 v5.0.0 标签，执行现有 `generate()` | 通过；仅生成 5.0.0 的一条主要改进；未提前创建实际项目标签 |
| 文件与文档 | `git diff --check`；六份根目录文件与需求、改动范围、测试和审查对应检查 | 通过；审查见 review-record.md；未跟踪验证目录不纳入提交 |

构建与 QML 日志在本轮执行环境的 `/private/tmp/loganalyqt-v500-*.log`；本轮发布工具结果不得理解为真实发布已执行。未执行本地完整 CTest 集合，不将 4 项专项回归写作全量通过；全量远端验收由下述门禁负责。

## 远端与正式发布验收

必须按完全相同提交验证 main 的 Windows x64 和 macOS arm64 成功产物，之后才能推送 v5.0.0 标签。标签工作流必须复用该产物，正式 Release 与公开更新验证都成功后才可宣告发布。

远端证据记录于 `release_package/v5.0.0-release-verification.json` 和 GitHub 工作流；只有实际取得的 run ID、提交、附件及摘要才能写为通过。

## 未验证事项

- Windows 实机与真实 SSH/MQTT 设备、企业网络现场运行。
- Apple 公证（既有 macOS 发布合同为 ad-hoc 签名）。
- 本轮不是持续高负载性能验收。

## 2026-10-26 快捷命令编辑修复（T10 / CMD-01、02）

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 修复前复现 | 新增 `test_quickCommandEditOfflineRenamesInPlaceAndPersists`，修复代码前执行 `python3 tests/stage10_qml_tests.py --filter SshMonitorWorkspace::test_quickCommandEditOfflineRenamesInPlaceAndPersists` | 如预期失败：找不到编辑按钮（2 passed / 1 failed，含初始化与清理） |
| SSH / 同步 QML 回归 | `python3 tests/stage10_qml_tests.py` | 最终 80 passed / 0 failed / 0 skipped；17.85 秒；真实 QML 组件、隔离模拟后端、offscreen |
| 新增四项回归范围 | `tests/qml-stage10/tst_ssh_workspace.qml` 中 `test_quickCommand*` | 覆盖界面创建、离线编辑回填、名称/内容修改、原位顺序、取消、空输入、名称冲突、保存失败保留与重试、原命令被其他会话删除、回车提交、跨会话和独立窗口同步、不误发送、修改后的命令发送及工作区重建加载 |
| 本地编译 | `cmake --build build/macos-debug --target appLogAnalyQt -j 4` | 通过，包含修改后的 QML 资源及 AOT 编译；链接器提示既有 OpenSSL 静态库重复，不影响构建成功 |
| 文档及格式 | 六份根目录流程文件逐一核对；`git diff --check` | 通过；需求 CMD-01/02、任务 T10、实现、测试及自审对应一致；保留原有未跟踪验证目录 |

首轮完整测试为 78 passed / 2 failed：新增测试未等嵌套界面完成渲染便点击按钮，实际点击错位。增加真实渲染等待与创建/编辑状态断言后，两项专项和最终完整测试通过，没有放宽业务断言。最终日志：`/tmp/loganalyqt-command-qml.log`、`/tmp/loganalyqt-command-build.log`。

本次未执行 Windows 实机、真实 SSH 设备、全量 CTest 或正式安装包验证。持久化回归通过模拟 backend 设置存储和重新创建工作区验证序列化/加载，未据此宣称真实磁盘故障或安装版重启验收通过。未提交、推送或发布新版本。

## 2026-10-26 按会话保存 SSH 日志（T11 / SSHLOG-01～04）

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| macOS 本地构建 | `cmake --build build/macos-debug --target appLogAnalyQt ssh_stream_tests -j 4` | 通过；包括 SshTerminal、SshSessionLog、会话菜单与 QML AOT；既有 OpenSSL 静态库重复链接提示保留 |
| 原生流/模型/日志 | `ctest --test-dir build/macos-debug -R '^ssh_stream_tests$' --output-on-failure` | 1/1 通过，1.29 秒；程序实际输出 214 项断言通过，日志文件位于隔离临时目录 |
| 原生日志覆盖 | `tests/ssh_stream_tests.cpp` 的 sessionLogChecks，结合既有流与模型测试 | 验证 UTF-8/GB2312 拆分、ANSI、回车重绘及尾部缩短、及时读取未换行内容、40,000 字符长行、停止完成尾部、编码/连接边界、追加/覆盖、截断 UTF-8、同路径锁冲突、导出拒绝覆盖记录中文件、原子导出、无效路径/目录错误；验证工作线程开始/停止确认、多会话错误隔离、清屏/后台状态不关闭记录，以及快照包含待显示队列与最新尾部 |
| QML 与最终菜单交互 | `python3 tests/stage10_qml_tests.py` | 最终 83 passed / 0 failed / 0 skipped，18.366 秒；真实 QML、模拟后端、offscreen；包括原快捷命令回归 |
| 三项新增界面回归 | `test_sessionLogControlsAreIndependentAndSurviveDetaching`、`test_existingSessionLogOffersAppendOverwriteAndCancel`、`test_sessionLogExportAndErrorsBelongToSelectedTab` | 验证入口仅在 SSH 选项卡菜单、正确会话的开始/停止/导出状态、分离/收回、清屏/暂停跟随保留记录、关闭会话结束记录、追加/覆盖/取消、导出无追加选项、状态/路径/错误显示及窄会话布局 |
| 编译后应用启动与布局 | 编译产物使用 `-platform offscreen --ui-smoke --migration-action confirm --page 5 --size 1200 800 --screenshot /tmp/loganalyqt-session-log-ui.png` | 隔离测试目录启动成功；窗口布局截图检查通过，未替换安装版或读取用户配置；最终入口按用户要求仅保留在选项卡菜单 |
| 格式、范围与文档 | `git diff --check`、六份流程文件与源码逐一核对 | 通过；保留 T10 未提交改动及原有 `validation/ssh-monitor-windows/`；README 与 CHANGELOG 的“未发布”条目已更新 |

调试过程中，新原生测试最初假设截断 UTF-8 固定产生一个替代字符；实际 Qt 解码器可对截断字节产生多个替代字符，改为严格检查内容仅有替代字符和结束换行。首轮新增菜单测试在独立窗口的 overlay 下查找声明于会话的对象，查找失败；修正对象查找后通过。随后按用户要求移除额外工具栏入口，改为通过原选项卡的菜单操作分离会话，并重新执行完整 QML 组通过。

最终日志：`/tmp/loganalyqt-session-log-build.log`、`/tmp/loganalyqt-session-log-qml.log`、`/tmp/loganalyqt-session-log-ui.log`；原生结果在 `build/macos-debug/Testing/Temporary/LastTest.log`。

未验证：Windows 实机、原生文件选择器的人工交互、真实 SSH 设备或新一轮加密回环网络、实际磁盘满/断电、慢盘持续负载、全量 CTest 及正式安装包。本机 Python 缺少 paramiko，未运行依赖它的网络 peer 测试；不得把独立解码/文件测试和模拟 QML 流程写作真实网络验收。文件故障验证涵盖无效路径、锁冲突和目录写入失败，不等同磁盘满。未提交、推送或发布。

## 2026-10-26 缓存与写入风险检查（T13）

静态核对 `SshWorker.appendData`、`SshSessionLog.feed/write` 和全部 flush 调用：每次接收都会提交文件刷新，未设置应用层时间/大小批量阈值；未完成尾部会重复写入。Qt QFile 默认缓冲不改变这一事实。详见 notes.md 的 T13 推导。

本项只做源码检查与官方文档核对，未运行 SSD 磨损、物理写入计数或持续压力测试，也未修改保存行为。不能把文件正确性断言或缓存存在理解为 SSD 寿命保证。

## 2026-10-26 本地自测包（T12 / LOCAL-01）

交付目录：`release_package/local-ssh-20261026/`；应用 `通联工坊.app`，压缩包 `TonglianWorkshop-local-20261026-macOS-arm64.zip`（约 57 MiB），另含 SHA256、readme.txt、package-checks.json。

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 最新构建检查 | `cmake --build build/macos-debug --target appLogAnalyQt -j 4` | 成功，ninja: no work to do；当前二进制已包含最新工作区源码 |
| Qt 依赖部署 | macdeployqt、移除三个不使用的数据库插件、fix_macos_bundle_ids.py；本地包增加 offscreen 诊断平台插件 | 完成；未附带个人配置和日志 |
| 架构与外部依赖 | `python3 scripts/ci/check_build.py --macos-bundle release_package/local-ssh-20261026/通联工坊.app` | 127 个 Mach-O 均含 arm64，依赖仅为随包或系统库，版本 5.0.0 |
| 本地签名 | `codesign --force --deep --sign - ...` 后 `codesign --verify --deep --strict ...` | 原位置及迁移后的临时目录校验通过；未公证 |
| 成品离屏启动 | 将最终应用复制到临时目录；清除 DYLD/QT/QML 开发环境，PATH 仅保留系统目录；临时用户配置下 `-platform offscreen --ui-smoke --migration-action confirm --page 5 --size 1200 800 --screenshot ...` | 成功退出，实时监控页面截图有效，应用日志无未排除警告/错误；数据目录在临时配置内 |
| 原生窗口检查 | 相同隔离方案下 `-platform cocoa` | 未通过：stderr 出现 PasteBoard/hiservices XPC 错误，进程 -6；未据此宣称原生启动成功或认定是产品故障，留待用户实际启动自测 |
| ZIP | ditto keepParent 打包最终应用并生成 SHA256 | 已生成；摘要和最终应用二进制摘要见随包 package-checks.json；未发布或替换安装版 |

部署日志：`/tmp/loganalyqt-local-20261026-package.log`；最终检查日志：`/tmp/loganalyqt-local-20261026-final-check.log`；截图：`/tmp/loganalyqt-local-20261026.png`。最初离屏检查因默认包不含 offscreen 插件失败，补充该诊断插件、重签名及重打 ZIP 后通过。

本任务未更改源代码行为，未重复执行 T11 已通过的完整界面/原生逻辑测试。Windows、真实 SSH 设备、原生文件选择器和 SSD 实际写入仍未验证；缓存检查结论见 T13。本包是 Debug 本地自测构建，版本仍为 5.0.0，不能作为正式发布附件。

## 2026-10-26 批量缓存与直接更新应用（T14、T15）

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 编译 | `cmake --build build/macos-debug --target appLogAnalyQt ssh_stream_tests -j 4`，文档和最终界面调整后重建 appLogAnalyQt | 通过，版本 5.0.0 Debug；既有 OpenSSL 重复链接提示保留 |
| 原生流/模型/日志 | `ctest --test-dir build/macos-debug -R '^ssh_stream_tests$' --output-on-failure` | 1/1 通过，1.25 秒；238 项断言 |
| 批量缓存文件行为 | sessionLogChecks：16 KiB 下限、精确阈值、空闲尾部定时 flush、连续 1,000 个单字节输入、停止/编码边界排空、非法参数 | 低于阈值文件保持为空；精确阈值立即可读；1,000 次小包在 flush 前没有文件数据写入，停止后一次性得到完整内容；独立 QTimer 回调验证没有后续输入也可 flush；原 ANSI、重绘、Unicode、追加/覆盖与冲突检查继续通过 |
| QML 回归 | `python3 tests/stage10_qml_tests.py` | 最终 85 passed / 0 failed，18.692 秒；真实 QML、模拟终端后端 |
| 设置与退出流程 | 新增 `test_logWriteSettingsValidatePersistAndReachOnlyTheirOwnSession`、`test_applicationExitWaitsForBusyLogOperationsAndStopAcknowledgements` | 会话菜单入口、范围校验、保存/取消、默认值、旧配置兼容、重建加载、正确参数传入、会话隔离通过；退出等待繁忙操作与停止确认，等待期禁止会话操作，失败不发出成功退出通知 |
| 成品依赖和签名 | 复用随包 Qt，替换主程序并重定位依赖/rpath；check_build.py；codesign strict 校验 | 127 个 Mach-O 架构和依赖检查通过；暂存应用、两个最终应用签名均通过 |
| 成品应用检查 | 清除 DYLD/QT/QML 开发环境，PATH 仅系统目录，临时用户配置下运行最终应用 `-platform offscreen --ui-smoke --migration-action confirm --ui-checks --page 5 --size 1200 800 --screenshot ...` | 完整应用 UI 检查成功退出，运行日志无未排除警告/错误，截图有效；未使用个人数据 |
| 原路径更新 | 旧应用移至各自 backups/before-log-buffer-20261026-195543，再复制验证后成品；失败路径提供旧应用恢复 | 最近交付目录与历史固定目录均已更新，最终二进制 SHA256 相同；未生成新 ZIP，原有 ZIP 保持上一轮内容；其他原有文件保留，仅追加历史使用说明和新增本轮验证记录 |

直接运行路径：

- `release_package/通联工坊-20261019-本地验证/通联工坊.app`
- `release_package/local-ssh-20261026/通联工坊.app`

两目录的 `buffer-build-checks.json` 记录最终摘要 `1fa308d7d050732bd95f7cfa0e1692c1e841da0f2e06ab63c4bf0f16af3d2e18`、旧应用备份路径、默认策略和不打 ZIP 的交付事实。日志：`/tmp/loganalyqt-buffer-build.log`、`/tmp/loganalyqt-buffer-native.log`、`/tmp/loganalyqt-buffer-qml.log`、`/tmp/loganalyqt-buffer-deploy.log`、`/tmp/loganalyqt-buffer-packaged-runtime.log`；截图 `/tmp/loganalyqt-buffer-packaged.png`。

边界：空闲定时写入测试用真实 QTimer 调用真实文件缓存 flush，未通过真实 SSH 网络触发工作线程定时器；退出确认测试使用模拟终端状态，成品 smoke 不替代“真实设备记录中关闭应用”的人工验收。未做 SSD 物理写入/磨损测量、磁盘满/断电、慢盘长时间压力、Windows 或本轮 Cocoa 原生窗口测试。异常退出仍可能丢失尚未刷新的缓存，界面已提示；设置更改于下次开始记录生效。

## 2026-10-26 连接记录核对（T16）

读取用户上传 image.png 的文字提取结果，静态核对 tryNextAddress、onWorkerError、scheduleReconnect 和 onWorkerConnected，以及现有 ssh_success_session_tests.cpp 的 reconnectNoticeChecks。确认成功回调不追加失败日志，重连成功保留之前的错误记录并追加成功提示。这里只查阅既有测试代码，未在本次运行该网络测试，不宣称真实设备或首次连接失败原因已复现。没有产品代码修改，未重新编译或替换本地应用。

## 2026-10-26 连接提示、长行与命令小笔（T17～19）

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 本机构建 | `cmake --build build/macos-debug --target appLogAnalyQt ssh_stream_tests -j 4` | 成功；包含 QML AOT、当前错误处理和成品检查调用方；既有 OpenSSL 重复链接提示保留 |
| QML 回归 | `python3 tests/stage10_qml_tests.py` | 最终 90 passed / 0 failed，19.758 秒；无未排除的绑定、索引或类型警告 |
| 长行与菜单新增覆盖 | test_longLinesSupportHorizontalScrollingAndWrappedSelection、test_wrappingAndCopyOptionsAreOrderedPerSessionAndPersist、test_wrappedMatchHighlightsSpanMultipleVisualRows | 验证横向内容宽度与滚动条、末端 HEX 双击/复制、开启换行后横滚关闭、跨视觉行拖选精确内容、换行后 HEX 解析、规则标记跨行位置及文本缩短时无越界；实时尾部行高/光标、菜单选项顺序、每会话隔离、分离/收回、旧配置与保存恢复通过 |
| 当前错误与角标编辑 | test_onlyCurrentConnectionErrorAppearsOutsideRemoteOutput、test_quickCommandPencilIsAnOverlayAndNeverSends；旧命令编辑/保存/取消回归 | 当前错误提示显隐、右上角 18 像素小笔覆盖于原命令按钮内、收起管理时可编辑、不触发发送、与删除不重叠通过 |
| 原生流/模型/日志 | `ctest --test-dir build/macos-debug -R '^ssh_stream_tests$' --output-on-failure` | 1/1 通过，1.26 秒，244 项断言；新增真实 SshTerminal 错误/恢复回调断言，确认状态显示错误、成功清除、远端业务 ERROR 不删除且不追加连接失败正文 |
| 成品 | 重定位最新主程序，复用随包 Qt；check_build.py、codesign strict；清理开发环境和临时配置下运行 `--ui-smoke --ui-checks --page 5 -platform offscreen` | 127 个 Mach-O 的架构/依赖及签名通过；完整应用 UI 检查通过，包括实际打开会话菜单切换复制选项；无未排除日志错误，截图有效 |
| 交付 | 更新 local-ssh-20261026 与历史固定测试路径下的 通联工坊.app | 两处最终二进制 SHA256 均为 `14bbddce4c958794d067d68b6c9135f5344291766da9fb8bb7be184c6a6ec456`，各自签名通过，旧应用备份存在；不生成 ZIP、不替换安装版 |
| 文档与格式 | 六份流程文件、README、未发布 CHANGELOG、`git diff --check` | 一致且通过；保留此前未提交改动及原有验证目录 |

调试记录：旧测试最初仍查找已移除的工具栏控件，改为实际打开菜单操作；两个新增静态行测试误用 waitForRendering 等待下一帧而在页面已渲染后超时，改为布局就绪及实际业务断言。换行光标曾因文本和布局更新次序不同产生 QTextCursor 索引越界，增加当前编辑器长度钳制和布局依赖后消除。曾尝试以 bottomMargin 留出横向滚动条空间，既有暂停/缩放回归发现 atYEnd 不再准确；改为滚动条位于视口外并缩小视口，原断言保留且专项及最终全组通过。

证据：`/tmp/loganalyqt-monitor-feedback-build.log`、`/tmp/loganalyqt-monitor-feedback-qml.log`、`/tmp/loganalyqt-monitor-feedback-native.log`、`/tmp/loganalyqt-monitor-feedback-deploy.log`、`/tmp/loganalyqt-monitor-feedback-packaged-runtime.log`、`/tmp/loganalyqt-monitor-feedback-packaged.png`。两处交付目录的 monitor-feedback-build-checks.json 包含摘要和旧应用路径，备份目录名 before-monitor-feedback-20261026-203050。

未验证：本轮 Cocoa 原生交互、Windows、真实 SSH 网络首次失败的原因、真实设备持续高吞吐与 SSD 物理写入。ssh_success_session_tests.cpp 的既有重连断言已同步更新但未执行依赖网络 peer 的测试；本地原生回调断言不能替代真实网络重连验收。未提交或远端发布。

## 2026-10-26 菜单勾号与终端文字（T20）

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 菜单与对比度回归 | `python3 tests/stage10_qml_tests.py` | 92 passed / 0 failed，20.728 秒；新增真实菜单勾号与文字不重叠、纵向居中、勾选显隐、悬停颜色及点击状态断言；正文/选区/实时尾行同色且对比度大于 15:1；原复制、换行、长行、规则和会话回归通过 |
| 构建 | `cmake --build build/macos-debug --target appLogAnalyQt -j 4` | 通过，包含两处 QML AOT；仅既有 OpenSSL 重复链接提示 |
| 实际渲染 | 临时 QML 测试副本抓取已勾选菜单、悬停态及中文日志 | 最终专项 3 passed / 0 failed，已人工查看截图，勾号在文字左侧且垂直居中，正文显示更亮；辅助脚本 save() 返回值误判已修正，见 notes.md |
| 成品检查 | check_build.py、codesign strict、清理 Qt/QML/DYLD 环境及临时配置下 `--ui-smoke --ui-checks --page 5 -platform offscreen` | 127 个 Mach-O 的 arm64/依赖通过，ad-hoc 签名通过，完整成品 UI 检查退出码 0，截图有效，运行时日志无错误 |
| 交付与格式 | 两处既定测试路径更新、主程序 SHA256 一致、备份存在、`git diff --check` | 已更新可执行应用，无新 ZIP；各目录 menu-contrast-build-checks.json 记录摘要及备份路径；六份流程文件已核对 |

证据：`/tmp/loganalyqt-menu-contrast-build.log`、`/tmp/loganalyqt-menu-contrast-qml.log`、`/tmp/loganalyqt-menu-contrast-visual.log`、`/tmp/loganalyqt-menu-contrast-preview.png`、`/tmp/loganalyqt-menu-contrast-deploy.log`、`/tmp/loganalyqt-menu-contrast-packaged-runtime.log`、`/tmp/loganalyqt-menu-contrast-packaged.png`。

本轮只改 QML 显示，没有重跑原生 SSH 流测试，不把前轮 244 项断言写作本轮执行。Cocoa 原生窗口、Windows、不同屏幕的主观观感及真实设备未验证；未提交或远端发布。

## 2026-10-26 单路径清理与菜单方框（T21）

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 清理 | 扫描项目所有应用目录，对比固定保留路径后逐项删除其他“通联工坊.app” | 删除 7 个同名应用（含旧版备份）；最终扫描仅 1 个该名称应用，清单为固定目录 app-cleanup-20261026.json；构建内部产物及仅含配置的历史空壳保留 |
| QML 回归 | `python3 tests/stage10_qml_tests.py` | 92 passed / 0 failed，20.562 秒；验证两个菜单方框无论选中与否都可见、方形尺寸及边框、框内勾号跟随 checked、对齐和悬停颜色、点击后状态变化；已有复制/换行/独立会话等回归通过 |
| 渲染检查 | 临时测试副本抓取已勾选“选中即复制”和未勾选“自动换行” | 专项 3 passed / 0 failed；人工查看截图，勾选框及空方框均正常，不与正文重叠 |
| 本机构建 | `cmake --build build/macos-debug --target appLogAnalyQt -j 4` | 成功；仅既有 OpenSSL 重复链接提示 |
| 成品与唯一交付 | 127 个 Mach-O 架构/依赖、codesign strict、隔离环境/配置离屏 `--ui-smoke --ui-checks --page 5`，最终摘要与单路径扫描 | 全部通过；固定路径新版 SHA256 为 `60a7efde05fcd16b425f131c95595af94285a9090403b0d82ec023bcd852db6d`；无新增 ZIP 或旧应用备份；menu-box-build-checks.json 保存证据 |
| 流程与格式 | 六份流程文件、README、`git diff --check` | 已核对；历史双路径交付记录保留，最新规则明确取代；细小显示改动不单列 CHANGELOG |

证据：`/tmp/loganalyqt-menu-box-qml.log`、`/tmp/loganalyqt-menu-box-build.log`、`/tmp/loganalyqt-menu-box-visual.log`、`/tmp/loganalyqt-menu-box-preview.png`、`/tmp/loganalyqt-menu-box-deploy.log`、`/tmp/loganalyqt-menu-box-packaged-runtime.log`、`/tmp/loganalyqt-menu-box-packaged.png`。

本轮未改后端，未重跑原生 SSH 流测试。Windows、Cocoa 原生窗口和真实设备未验证；未提交或远端发布。

## 2026-10-26 本地非源码目录清理（T22）

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 删除边界 | 根据 Git 跟踪清单逐项拦截源码删除；清理目标生成清单并删除 | 删除的所有目标均不含 Git 跟踪文件；旧打包、构建/输出缓存及验收生成物共 827,353,620 字节，release_package 只剩固定测试目录 |
| 源码完整性 | 清理前后对跟踪文件和必需的新 SshSessionLog.h 做 SHA256/符号链接核对 | 773 份文件/链接完全一致；正式 tests、预览源码、构建脚本及验收文档保留；之后仅更新本轮文档 |
| 当前测试目录 | 清理前后对目录现有文件和链接做摘要核对 | 原有 2086 份完全一致，应用未替换；新增清理清单及保留数据 |
| 保护历史数据 | 297 份历史配置/Git/补丁备份和 1 份迁移 INI 移动前后摘要核对 | 298 份保留到固定目录的保留数据中，摘要完全一致，无个人数据内容输出 |
| 成品独立性 | 删除 build 后执行 check_build.py、codesign strict；无 Qt/QML/DYLD 开发环境、临时个人配置下执行完整离屏 UI smoke | 127 个 Mach-O 的 arm64/依赖通过；签名通过；`--ui-smoke --ui-checks --page 5 -platform offscreen` 退出 0，截图有效、无未排除日志错误 |
| 文档与格式 | 六份流程文件、README、最终源文件摘要及 `git diff --check` | 核对一致；历史路径按新清理范围声明失效，不伪造为仍存在 |

清单：固定测试目录 project-cleanup-20261026.json。运行证据：`/tmp/loganalyqt-project-cleanup.log`、`/tmp/loganalyqt-project-cleanup-runtime.log`、`/tmp/loganalyqt-project-cleanup-runtime.png`。

本轮无产品代码变更，不重新编译或重复 QML/原生 SSH 测试。Windows、Cocoa 原生窗口及真实 SSH 设备未验证；下次编译需重新生成构建缓存。未提交或远端发布。

## 2026-10-26 交付目录直放应用（T23）

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 应用迁移 | 从日期目录移动至 release_package/通联工坊.app，前后核对每个文件/链接摘要 | 2049 份摘要一致，主程序 SHA256 为 60a7efde05fcd16b425f131c95595af94285a9090403b0d82ec023bcd852db6d，无重新编译 |
| 旧测试内容 | 删除原日期目录及剩余测试附件，枚举 release_package | 38 份附件及旧目录已删除，目录仅保留通联工坊.app，无测试附件、报告、压缩包或应用备份 |
| 非测试历史数据 | Git/补丁/个人配置历史数据另存根目录 .local-backup，前后摘要核对 | 298 份完全一致，未混入交付目录，不输出数据内容 |
| 签名/新路径启动 | codesign --verify --deep --strict；清理 Qt/QML/DYLD 环境、临时配置执行新路径完整离屏 UI smoke | 均通过；退出码 0，截图有效，无未排除的运行时错误 |
| 流程 | 六份文档、README、新路径和 git diff --check | 一致通过，旧路径仅作为历史记录保留 |

证据：`/tmp/loganalyqt-direct-release-cleanup.json`、`/tmp/loganalyqt-direct-release-runtime.log`、`/tmp/loganalyqt-direct-release-runtime.png`。本轮无代码变化，不重复 QML/原生 SSH 测试；Cocoa 原生窗口、Windows、真实设备未验证。未提交或远端发布。

## 2026-10-26 复选设置保持菜单打开（T24）

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 真实 QML 回归 | `PYTHONDONTWRITEBYTECODE=1 python3 tests/stage10_qml_tests.py` | 最终 93 passed / 0 failed，21.189 秒；新增连续勾选/取消后 menu.opened 始终为真、上下方向键定位、Space/Return 切换、Escape/外部关闭及普通设置命令关闭；旧方框布局、按会话保存/恢复、复制/长行/分离窗口回归通过 |
| 视觉专项 | 临时测试副本截取两个选项切换后的仍打开菜单 | 3 passed / 0 failed，人工查看截图，框、勾号与文字保持正常 |
| 完整构建 | `cmake --preset macos-debug -DLOGANALYQT_BUILD_PROTOCOL_TESTS=ON`；`cmake --build build/macos-debug --target bundle_mqtt_broker -j 4` | 从已清理的构建目录重新配置/编译成功，主程序和 Broker 均生成；原依赖源码包 SHA256 校验通过；既有 Qt policy/可选 Vulkan/重复 OpenSSL 提示未改动 |
| 成品 | check_build.py、codesign strict，清理开发环境、临时配置下 `--ui-smoke --ui-checks --page 5 -platform offscreen` | 127 个 Mach-O 架构/依赖通过，ad-hoc 签名通过；完整应用检查退出 0、截图有效、运行日志无错误；真实后端 UI 检查新增菜单切换后仍可见的断言，然后显式关闭再执行拖选复制 |
| 唯一交付 | 更新 release_package/通联工坊.app，最终摘要/签名/目录核对 | SHA256 为 63d909434fec8404b90025356375a19ea46d3a3adca3f53402ffe9817fa7af79；交付目录仅保留应用，无压缩包或旧版备份 |
| 流程 | 六份流程文件、README、`git diff --check` | 一致通过；本轮细小交互修复不单列 CHANGELOG |

调试中如实记录失败：最初重配因代理拒绝下载失败；下载固定依赖并核对原摘要后恢复。控件最初缺少菜单焦点同步、使用 clicked 未覆盖快速双击，专项/复制回归发现后修复。首次成品启动未启用编译期测试入口，进入普通启动并遇已有应用的数据锁而超时，未写作通过；开启 LOGANALYQT_BUILD_PROTOCOL_TESTS 并重新编译后才完成隔离验收。

证据：`/tmp/loganalyqt-menu-stay-configure.log`、`/tmp/loganalyqt-menu-stay-build.log`、`/tmp/loganalyqt-menu-stay-qml.log`、`/tmp/loganalyqt-menu-stay-visual.log`、`/tmp/loganalyqt-menu-stay-preview.png`、`/tmp/loganalyqt-menu-stay-deploy.log`、`/tmp/loganalyqt-menu-stay-packaged-runtime.log`、`/tmp/loganalyqt-menu-stay-packaged.png`、`/tmp/loganalyqt-menu-stay-build-checks.json`。

未修改 SSH 后端，本轮未单独运行原生 SSH 流测试。Windows、Cocoa 原生窗口、真实设备未验证；未提交或远端发布。

## 2026-10-27 v5.0.1 提交前验证（T25）

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 版本一致性 | check_build.py、CMake/README/CHANGELOG 及构建 Info.plist 核对 | 项目与新构建均为 5.0.1，CHANGELOG 只列主要功能和重要修复 |
| 发布工具 | python3 -m unittest tests.test_release_automation tests.test_ci_packages tests.test_github_release tests.test_ci_workflow -v | 38/38 通过；首次沙箱运行有 3 项因禁止本机端口绑定而报错，允许本机测试服务后重跑全部通过 |
| SSH/监控真实 QML | PYTHONDONTWRITEBYTECODE=1 python3 tests/stage10_qml_tests.py | 93 passed / 0 failed，21.077 秒；允许的 offscreen raise() 提示之外无异常警告 |
| 本机编译 | cmake --build build/macos-debug --target ssh_stream_tests bundle_mqtt_broker -j 4 | 成功；主程序、日志原生测试、Broker 已生成，既有重复 OpenSSL 链接提示未改动 |
| 原生流/模型/日志 | ctest --test-dir build/macos-debug -R '^ssh_stream_tests$' --verbose --output-on-failure | 1/1 通过，244 项断言，1.29 秒；文件使用隔离临时目录 |
| 文档与格式 | 逐一核对六份流程文档及 git diff --check | 通过；未加入个人配置或 .local-backup |

最初误将独立性能工程的 ssh_success_session_tests 作为主工程 target，Ninja 提示 unknown target；移除该不存在的目标后重新构建成功，未将该独立网络测试记为本轮已运行。

证据：/tmp/loganalyqt-v501-tooling.log、/tmp/loganalyqt-v501-qml.log、/tmp/loganalyqt-v501-build.log、/tmp/loganalyqt-v501-native.log。本次源码提交时，本机构建尚未替换唯一验证应用；远端 Windows/macOS 构建、成品启动和公开更新验收必须使用本次提交的 GitHub 工作流结果，未完成前不记为通过。真实设备与 SSD 物理写入未验证。

### T25 本地 5.0.1 成品补充验收

- 复用现有 macOS 部署步骤，暂存于 build/macos-debug/deploy-v501；macdeployqt 在沙箱内停滞，结束该进程后在允许原生工具的环境中部署完成。原生部署路径使用已安装的 /opt/homebrew/bin/macdeployqt，最初不存在的 /opt/homebrew/opt/qt/bin 路径未作为成功执行。按项目脚本移除未用 SQL 驱动并修正 dylib IDs。
- 首次成品离屏运行失败，原因是 macdeployqt 默认只部署 Cocoa 插件。补入当前 Qt 的 offscreen 插件后重新签名，114 个 Mach-O 的 arm64/随包依赖检查、codesign deep strict 通过。
- 清理 Qt/QML/DYLD 环境变量后，在临时个人数据目录中运行 tests/test_ui_migration_runtime.py，全套通过：配置迁移与冲突保护、11 个独立页签和重启保留、监控规则持久化、MQTT 本机收发及 Broker 清理；未使用真实设备。证据：/tmp/loganalyqt-v501-packaged-runtime.log。
- 已直接更新 release_package/通联工坊.app；版本 5.0.1，主程序 SHA256 为 1b77e7a807330eb9784c749d87c0eec7a8efeddc75c8f979b715824fe19dd3d3；最终签名和暂存/交付摘要一致。目录中只保留应用，清除 Finder 自动生成的 .DS_Store；不创建 ZIP 或旧应用备份。结构化本地验收记录位于 build/macos-debug/v5.0.1-local-verification.json。

本地交付为 Debug/ad-hoc，非 Apple 公证版；不等同于 GitHub Release 的跨平台发行验收。

### T25 远端构建、正式发行与公开更新最终验收

| 范围 | 实际证据 | 结果 |
|---|---|---|
| 同一提交双平台构建 | [40437509782](https://github.com/jsnjjnzx/LogAnalyQt/actions/runs/40437509782)，head 6352f434210f075a0a2c65482feb6a6687657c0b | 总体 success；Windows 常规 CTest 25/25、UI 1/1；macOS 常规 CTest 28/28、UI/原生激活 2/2，两个平台预检/稳定性、打包、独立启动及 HTTPS 更新探测通过 |
| 产物来源门禁 | 运行 find_validated_build.py --repository jsnjjnzx/LogAnalyQt --commit 6352f434210f075a0a2c65482feb6a6687657c0b | 通过；选择 40437509782，两个平台成功、两份产物未过期且同仓库同提交，随后才推送 v5.0.1 标签 |
| 正式发行 | [v5.0.1](https://github.com/jsnjjnzx/LogAnalyQt/releases/tag/v5.0.1)、[40438953030](https://github.com/jsnjjnzx/LogAnalyQt/actions/runs/40438953030) | 发布任务 success；非草稿/非预发布且 latest；7 个附件名称完整，说明来源于 CHANGELOG |
| 元数据与摘要 | 保存远端 release.json，下载 update.json 后通过 Python assert 检查 | update.json 实际 SHA256 与 GitHub digest 一致；清单 6 项附件的名称、大小、摘要、下载地址与远端元数据完全一致；公开前上传摘要门禁由现有发布脚本完成 |
| Windows/macOS 公开更新 | 同一发布 run 的两个 Verify published update 任务，生产客户端与 tests/test_github_update.py | 均 success；真实公开下载、SHA256、保存/替换、重启至 5.0.1、主窗口可见（macOS 激活）、工作区草稿和便携数据保留均通过 |
| 六份流程文档与交付 | spec/backlog/AGENTS/notes/test-report/review-record、固定本地应用、git diff --check | 已逐一核对；历史记录与最新发行结果区分明确，不将生成物或个人配置纳入源码提交 |

最终证据位于 `build/v5.0.1-release-evidence/`：verification.json、release.json、update.json、update-verification.log。公开来源以对应 Actions 和 Release 链接为准；本地应用证据见上一节。

验证边界：CI 使用受控运行器与合成数据，不代表实体 Windows 电脑、用户企业网络或真实 SSH 设备验收。macOS 仍为 ad-hoc 签名，未经 Apple 公证；未测 SSD 物理写入量。

## 2026-10-27 会话栏操作集中（T26）

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 真实 QML | PYTHONDONTWRITEBYTECODE=1 python3 tests/stage10_qml_tests.py | 最终 95 passed / 0 failed，21.490 秒；覆盖同排顺序/宽度/完整文字、非当前会话操作隔离、配置菜单、独立窗口往返、侧栏折叠及列表虚拟化；原接收、跟随、复制、规则和命令编辑等通过 |
| 实际渲染 | 临时测试副本运行会话操作专项，捕获主区和 ⋯ 菜单，人工查看 | 3 passed / 0 failed；四按钮同排、文字无重叠，右侧重复按钮消失，菜单有连接配置入口 |
| 编译 | cmake --build build/macos-debug --target bundle_mqtt_broker -j 4 | 成功；最终 QML 编译和应用链接通过，既有重复 OpenSSL 链接提示未改动 |
| 部署与签名 | 复用相同 Qt 依赖，更新主程序/Info.plist/Broker 并重定位依赖，check_build.py、codesign deep strict | 114 个 Mach-O 的 arm64/随包依赖通过，ad-hoc 签名通过，无新增依赖 |
| 成品完整 UI | 清理 Qt/QML/DYLD 环境，在临时配置下运行 tests/test_ui_migration_runtime.py | 最终通过；生产后端侧栏按钮真实点击、停止读取与独立窗口恢复、配置迁移、会话/规则/工作区重启保留、MQTT 本机收发和 Broker 清理通过 |
| 唯一交付与流程 | 直接更新 release_package/通联工坊.app；源/目标摘要、签名、交付目录及六份流程文件核对 | 版本保留 5.0.1，包含本轮未发布改动；目录只放应用，无 ZIP/旧应用备份，git diff --check 通过 |

初轮回归发现重定位后按钮顺序不固定、原 RowLayout 尺寸残留导致覆盖/点击错误、菜单键盘目标增加；固定槽及条件填满并同步测试后通过。列表专项最初因 ListView 保留 currentItem 未实际销毁目标行，测试取消内部 currentIndex 后覆盖真实销毁/重建，最终通过。截图工具 save() 不返回成功布尔值，首次辅助断言失败，改为保存后核对文件存在和大小，再人工检查图片。上述失败未写作通过。

最终主程序 SHA256：383799da100629aa64f619314fe69165773e4131ecebf49454cde11c5f2bd96d。结构化验收：build/macos-debug/sidebar-actions-verification.json。日志：/tmp/loganalyqt-sidebar-actions-qml.log、/tmp/loganalyqt-sidebar-actions-build.log、/tmp/loganalyqt-sidebar-actions-visual.log、/tmp/loganalyqt-sidebar-actions-packaged-runtime.log。截图：/tmp/loganalyqt-sidebar-actions.png、/tmp/loganalyqt-sidebar-menu.png。

未改 SSH 后端，本轮未重复原生 SSH 流专项。Windows、Cocoa 原生交互及真实设备未验证；本地 Debug/ad-hoc 应用不代表正式 Release，本轮未提交、推送或发布。

## 2026-10-27 四按钮尺寸与字号统一（T27）

- `python3 tests/stage10_qml_tests.py`：95 passed / 0 failed，22.033 秒；已有侧栏布局回归增加四按钮宽 51、高 26、字号 11、字体族/字重相同的断言，保留完整文字、同排不重叠、会话隔离及独立窗口行为检查。
- `cmake --build build/macos-debug --target bundle_mqtt_broker -j 4`：成功，只有既有重复 OpenSSL 链接提示。复用相同 Qt 依赖部署最新主程序，114 个 Mach-O 的 arm64/随包依赖和 codesign deep strict 通过。
- 成品在临时 LOGANALYQT_TEST_HOME、清理 Qt/QML/DYLD 环境下执行 `--ui-smoke --page 5 --size 1280 900 --screenshot /tmp/loganalyqt-uniform-buttons.png -platform offscreen`：退出 0，截图有效，无未排除运行警告；人工查看实际应用截图，四按钮等宽等高、文字大小一致且完整。
- 唯一应用 `release_package/通联工坊.app` 已更新，源/目标摘要一致，最终签名通过，交付目录只保留应用。主程序 SHA256：84959c85c8f2427b17e3dd64bdbbf14737735971cfb4d5f318db43c6938f687f。结构化记录 `build/macos-debug/uniform-buttons-verification.json`；日志 `/tmp/loganalyqt-uniform-buttons-qml.log`、`/tmp/loganalyqt-uniform-buttons-build.log`、`/tmp/loganalyqt-uniform-buttons-runtime.log`。

本轮仅尺寸/字体修改，未重复完整应用迁移/MQTT、原生 SSH 后端专项或 Windows/Cocoa/真实设备验收。保持 5.0.1 本地 Debug/ad-hoc 版本，未提交或发布；六份流程文件与本轮范围及结果已核对。

## 2026-10-27 全局提示、底部状态与虚线命令入口（T28～T30）

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 监控/全局提示真实 QML | python3 tests/stage10_qml_tests.py | 最终 107 passed / 0 failed，24.430 秒；覆盖空命令占位、追加/保存/取消、编辑/删除入口、不误发命令、失败草稿保护；底部同排/靠右、760/1180/1560 宽度、连接五状态、长附加状态、独立窗口；全局样式、纯文本特殊字符、长文、四边缘、跨窗、动态减少透明度和后续点击 |
| 解析与通用控件 | python3 tests/prot698_qml_tests.py，启用同一 WorkshopStyle | 83 passed / 0 failed，4.131 秒；原解析/组帧/数值/窗口交互通过 |
| 导航定位回归 | RemoteSecurityViews::test_protocolDiffOverviewAndBlockNavigation --repeat 3 | 原用例未削弱，3 轮均通过；最终采用下方优先定位，避免新尺寸覆盖上方导航 |
| 构建 | cmake --build build/macos-debug --target bundle_mqtt_broker -j 4 | 成功；最终 QML/资源/主程序重新生成；仅既有重复 OpenSSL 链接提示 |
| 实际渲染 | 临时测试副本运行底栏专项并抓取空/非空命令、悬浮提示 | 专项 3 passed / 0 failed，1.498 秒；已人工查看三张图，虚线可见、管理按钮消失，命令名及角标正常，底部状态靠右且同一中心线 |
| 独立成品 | 相同 Qt 依赖部署、check_build.py、codesign deep strict | 114 个 Mach-O 的 arm64/随包依赖通过，ad-hoc 签名通过；未依赖源码目录的样式文件 |
| 完整成品 UI/MQTT | 清理 Qt/QML/DYLD 环境、临时个人数据下运行 tests/test_ui_migration_runtime.py | 最终通过；生产应用新增断言确认嵌入的全局 ToolTip 样式及实际 uiTheme 绑定生效；配置迁移、11 个独立页签、重启恢复、共享规则、实际侧栏停止读取/独立窗口恢复、MQTT 本机收发和 Broker 清理通过 |
| 交付与流程 | 直接更新唯一 release_package/通联工坊.app，摘要/签名/目录及六份流程文件核对 | 源/目标一致；交付目录只保留应用，无 ZIP/日期目录/旧应用备份；git diff --check 通过 |

调试失败如实记录：自定义样式最初用版本 1.0 导入触发 Basic 1.0 回退错误，改为无版本导入；GPU 阴影在软件渲染中使背景不可见，改为两层低透明度圆角阴影。初版上方定位会覆盖差异导航，改为下方优先后原测试重复通过，未使用私有引擎属性或改写输入处理。底栏初版自引用宽度引发递归重排，改为基于外层页面宽度后消失。新编辑器测试最初未等待布局、无后续帧时错误等待 Rendering，改为等待实际几何/可用状态；四字命令名被旧宽度预估截断，改用真实 implicitContentWidth 后通过。失败不计为通过。

最终主程序 SHA256：ad9738bd0ec9316e67fe3e38f082f400882db2ab8af051e53adae48d6fa15520。结构化记录：build/macos-debug/final-interactions-verification.json。主要日志：/tmp/loganalyqt-final-interactions-qml.log、/tmp/loganalyqt-glass-tooltip-parser.log、/tmp/loganalyqt-final-interactions-build.log、/tmp/loganalyqt-final-interactions-visual.log、/tmp/loganalyqt-final-interactions-packaged-runtime.log。截图：/tmp/loganalyqt-final-empty-command.png、/tmp/loganalyqt-final-commands.png、/tmp/loganalyqt-final-tooltip.png。

提示正文保留完整原字符串，显示最多 12 行并省略超出部分，解析字段仍可使用原右键复制。全局提示的当前 25 个声明均复用同一样式，主/独立窗口共享和减少透明度有专门回归。未修改后端日志/协议/持久化格式，不重复原生 SSH 流专项；未验证 Windows 原生显示、Cocoa 交互或真实设备。版本为 5.0.1 本地 Debug/ad-hoc，未公证，本轮未提交或发布。

## 2026-10-27 首连提示核查、移除摘要与缩窄入口（T31）

- SSH 核查：只读检查工作线程 TCP/握手/断开、GUI generation 过滤、scheduleReconnect、onWorkerConnected 清错及 QML 错误绑定；应用日志按 SSH 诊断类别扫描，无可还原该次错误的记录。用户补充偶发且记不清原文，允许尽力核查。未连接用户设备，未复现或确定具体首次失败原因；没有修改 SSH 重试、认证或错误处理逻辑。
- 原生状态回归：`ctest --test-dir build/macos-debug -R '^ssh_stream_tests$' --verbose --output-on-failure`，1/1 通过，244 项断言，0.72 秒；包含当前错误、成功清除和保留真实远端 ERROR。不是实际 SSH 首连复现，不替代真实网络验收。
- 真实 QML：`python3 tests/stage10_qml_tests.py`，108 passed / 0 failed，24.616 秒；新增移除摘要、无残留空卡片、真实错误/日志状态保留、菜单展开配置与独立窗口往返；现有添加入口补充 96 像素宽及完整文字断言，所有此前全局提示/底栏/命令操作回归通过。
- 构建与成品：`cmake --build build/macos-debug --target bundle_mqtt_broker -j 4` 成功；114 个 Mach-O 的 arm64/随包依赖与 codesign deep strict 通过。临时个人配置、清理开发环境下 `tests/test_ui_migration_runtime.py` 完整通过，覆盖实际 UI、配置/工作区迁移恢复、全局样式绑定及 MQTT 本机收发/助手清理。
- 视觉专项：临时测试副本 3 passed / 0 failed，0.822 秒，已人工查看 `/tmp/loganalyqt-summary-removal-preview.png`；顶部摘要和空白卡片消失，菜单配置仍可用，96 像素虚线入口文字完整。
- 交付：直接更新 `release_package/通联工坊.app`；源/目标摘要一致，最终签名通过，目录只放应用。主程序 SHA256：37decb582ddf0a3527ce4baf4c193fa190abe87ebe67253f57790caf13db6058。记录：`build/macos-debug/summary-removal-verification.json`；日志 `/tmp/loganalyqt-summary-removal-qml.log`、`/tmp/loganalyqt-first-connect-native.log`、`/tmp/loganalyqt-summary-removal-build.log`、`/tmp/loganalyqt-summary-removal-packaged-runtime.log`。

六份流程文件及 git diff --check 核对通过。本轮没有查明非稳定首连失败的底层原因，不把清错回归当成原因已定位；Windows/Cocoa 原生交互、真实 SSH 设备未验证，版本仍为 5.0.1 本地 Debug/ad-hoc，未提交、推送或发布。

## 2026-10-27 v5.0.2 提交前回归（T32）

| 范围 | 实际执行 | 结果 |
|---|---|---|
| 版本与格式 | check_build.py、CMake/README/CHANGELOG、git diff --check | 5.0.2 一致，格式通过，说明仅包含两项主要改进 |
| 监控与全局提示 QML | python3 tests/stage10_qml_tests.py | 108 passed / 0 failed，24.128 秒 |
| 解析与通用控件 | python3 tests/prot698_qml_tests.py | 83 passed / 0 failed，4.197 秒 |
| 发布工具 | python3 -m unittest tests.test_release_automation tests.test_ci_packages tests.test_github_release tests.test_ci_workflow -v | 38/38 通过，2.171 秒；允许临时 127.0.0.1 测试服务 |
| 本机重新构建 | cmake --build build/macos-debug --target bundle_mqtt_broker ssh_stream_tests -j 4 | 成功，主程序/Broker/原生测试已生成，只有既有重复 OpenSSL 链接提示 |
| SSH 流/模型/日志 | ctest --test-dir build/macos-debug -R '^ssh_stream_tests$' --verbose --output-on-failure | 1/1 通过，244 项断言；包括当前错误与成功清除，不代表偶发真实首连问题已复现或修复 |

日志：/tmp/loganalyqt-v502-qml.log、/tmp/loganalyqt-v502-parser.log、/tmp/loganalyqt-v502-tooling.log、/tmp/loganalyqt-v502-build.log、/tmp/loganalyqt-v502-native.log。六份流程文件与当前版本及范围已核对。

提交时，远端同提交双平台构建、正式 Release 与公开更新仍待执行；本地固定应用也须后续更新为 5.0.2。历史本地成品证据见 T31，本节不把历史结果当作本次发行结果。

### T32 本地 5.0.2 成品验收

复用同一 Qt 依赖部署本次编译的主程序/Info.plist/Broker 并重定位依赖，114 个 Mach-O 的 arm64/随包依赖、codesign deep strict 均通过。清理 Qt/QML/DYLD 环境、使用临时个人数据执行 tests/test_ui_migration_runtime.py，完整通过全局样式/主题断言、配置迁移和恢复、11 个独立页签、共享规则、工作区重启保留及 MQTT 本机收发/助手清理。

已直接更新 release_package/通联工坊.app，版本 5.0.2，签名及源/目标摘要一致，目录只保留应用，无额外压缩包或旧应用备份。主程序 SHA256：3e5ef243571d4686275b7c32be1ae1745e7499dbaea4eff2bc30ed7f66c870d9。结构化记录 build/macos-debug/v5.0.2-local-verification.json；运行日志 /tmp/loganalyqt-v502-packaged-runtime.log。此本地 Debug/ad-hoc 成品不等同于远端正式发行验收。

### T32 远端发行与公开更新最终验收

| 范围 | 实际证据 | 结果 |
|---|---|---|
| 同提交双平台构建 | [40447726139](https://github.com/jsnjjnzx/LogAnalyQt/actions/runs/40447726139)，head 0d04eb13de6701ba3251180737864835c1d5cb02 | 总体 success；Windows 常规 CTest 25/25、UI 1/1，macOS 常规 CTest 28/28、UI/原生激活 2/2；预检/稳定性、打包、独立启动与 HTTPS 更新探测均通过 |
| 产物门禁 | find_validated_build.py --repository jsnjjnzx/LogAnalyQt --commit 0d04eb13de6701ba3251180737864835c1d5cb02 | 通过，选择 40447726139；两个平台成功、产物未过期、同仓库同 main 提交 |
| 标签一致性 | Git Database API 注释对象及引用、本地对象 SHA 和 commits/v5.0.2 | 标签目标为已验证源码；原 Git 推送 TLS 失败未冒称成功，API 创建后原 push 工作流正常触发 |
| 正式 Release | [v5.0.2](https://github.com/jsnjjnzx/LogAnalyQt/releases/tag/v5.0.2)、[40449577483](https://github.com/jsnjjnzx/LogAnalyQt/actions/runs/40449577483) | 发布任务 success，非草稿/非预发布且 latest；7 个附件完整，说明与本版 CHANGELOG 一致 |
| 更新元数据 | 保存 release.json、下载 update.json 并运行 assert | update.json 实际 SHA256 与 GitHub digest 一致；6 个产物条目的名称、大小、摘要、下载地址与远端完全一致；公开前所有上传摘要由发布脚本门禁验证 |
| 双平台公开更新 | 同一发布 run 的 Windows/macOS Verify published update 任务 | 均 success；模拟旧客户端走生产更新路径，实际公开下载、SHA256、替换、重启至 5.0.2、主窗口可见/激活、工作区草稿和便携数据保留通过 |
| 六份流程文档和格式 | spec/backlog/AGENTS/notes/test-report/review-record、git diff --check | 核对一致；历史本地阶段“未提交/未发布”记录保留历史含义，最终补记只改证据文档 |

最终证据位于 `build/v5.0.2-release-evidence/`：verification.json、release.json、update.json、update-verification.log。正式远端结果以所链接的 Actions 和 Release 为准。

限制：偶发首次 SSH 粉红提示仍未定位，本次未改 SSH 连接逻辑；受控 CI/合成数据不代表实体 Windows、企业网络或真实 SSH 设备验收。macOS 为 ad-hoc 签名，未经 Apple 公证。

## 2026-10-27 macOS 顶栏双击最大化（T33）

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 修复前复现 | 新增 NativeNavigationChrome 双击用例，修复前运行 | 失败：预期 Maximized（4），实际 Windowed（2）；当时 nav-window-drag 确实无双击处理 |
| 完整 QML | QT_QPA_PLATFORM=offscreen python3 tests/prot698_qml_tests.py | 最终 89 passed / 0 failed，4.361 秒；覆盖连续最大化/还原、单击仍调用拖动、全屏保持、非原生侧栏行为、真实 QQuickWindow 及原有标题栏/解析/通用控件 |
| 真实 Cocoa 双击 | 同一测试入口，NativeNavigationChrome::test_doubleClickOnARealWindowMaximizesAndRestores --repeat 3 | 3 轮均通过，每轮使用 Qt 标准双击序列在真实 Cocoa 窗口加载生产 Navigation，完成两次最大化/还原并恢复原尺寸；系统输入法 CFMessagePort 提示未造成用例失败，不作为输入法验收 |
| 原生控件与全屏 | QT_QPA_PLATFORM=cocoa ctest -R '^mac_backdrop_tests$' --verbose --output-on-failure | 1/1 通过，7.31 秒；原生材质、红黄绿、最小化/恢复、缩放、两次全屏往返及清理正常 |
| 主窗口处理与 UI | QT_QPA_PLATFORM=cocoa python3 tests/test_ui_migration_runtime.py build/macos-debug/appLogAnalyQt.app/Contents/MacOS/appLogAnalyQt | 最终完整通过；主窗口两处生产 QML 双击信号处理各执行两轮最大化/还原，检查原生窗口状态和完整几何恢复、顶部区域不侵入页面；配置迁移/重启恢复、会话/规则和 MQTT 本机收发正常。这里验证处理函数，不冒称 C++ 手工事件等同真实鼠标序列 |
| 构建与部署 | cmake --build build/macos-debug --target bundle_mqtt_broker mac_backdrop_tests -j 4；更新内嵌 CHANGELOG 后增量构建；随包依赖/签名 | 成功，114 个 Mach-O 的 arm64/系统及随包依赖通过，codesign deep strict 通过，仅既有重复 OpenSSL 链接提示 |
| 最终独立成品 Cocoa | 清理 Qt/QML/DYLD 环境，使用临时配置运行 tests/test_ui_migration_runtime.py build/macos-debug/title-doubleclick/通联工坊.app/Contents/MacOS/appLogAnalyQt | 完整 UI/MQTT 通过，含新增主窗口处理及几何断言；通过后更新唯一 release_package/通联工坊.app，摘要/签名一致 |

调试失败未记为通过：首版测试夹具 theme 同名引用自身产生 TypeError，改为独立 testTheme。C++ 手工 MouseButtonDblClick 投递未触发主窗口 QML 链路，直接信号诊断能正常最大化，转用 Qt 标准双击序列做手势验收并单独验证生产主窗口处理。一次完整 Cocoa 回归在 MQTT Broker 配置控件定位失败；独立复测、随后串行完整复测和最终独立成品回归均通过，未改动 MQTT 或放宽其断言，失败原因未确认。

证据：/tmp/loganalyqt-title-doubleclick-before.log、/tmp/loganalyqt-title-doubleclick-qml.log、/tmp/loganalyqt-title-doubleclick-real-window.log、/tmp/loganalyqt-title-doubleclick-cocoa.log、/tmp/loganalyqt-title-doubleclick-native-ui.log、/tmp/loganalyqt-title-doubleclick-packaged.log。结构化记录：build/macos-debug/title-doubleclick-verification.json。最终主程序 SHA256：e626f8e9a4dbcd1f7eb29c0225f34b888ac260d5ec1413f30d0a61f1960fdadf。

六份流程文档和 git diff --check 已核对。未连接真实 SSH 设备，未重跑后端协议专项或 Windows 实机验收；未做人工鼠标/多显示器长期验收。版本保持 5.0.2 本地 Debug/ad-hoc，修复列入未发布 CHANGELOG，本轮未提交或发布。

## 2026-10-27 明确收起入口与四组参数表单（T34）

| 范围 | 执行方式 | 实际结果 |
|---|---|---|
| 真实 QML 回归 | python3 tests/stage10_qml_tests.py | 最终 109 passed / 0 failed，24.927 秒；主区/独立窗口鼠标及 Space 收起、菜单重开、草稿/连接/持续记录保留；四组同排、30 像素字段、编码完整、校验/只读/重连与原监控/命令/全局提示回归通过 |
| 正常/窄区域 | 工作区宽度 940/1180、单会话 560/902/1160/1600 的布局及焦点检查 | 正常和主窗口最小宽度四组完整可见；极窄区域水平滚动，键盘焦点可将字段带入视口，收起按钮始终位于滚动区外 |
| 渲染专项 | 临时测试副本捕获普通和主窗口最小宽度图，人工查看 | 3 passed / 0 failed，0.543 秒；标题右侧按钮明确，首两行各四组参数、末行余下两组，无重叠或空白占位；截图 /tmp/loganalyqt-compact-config.png、/tmp/loganalyqt-compact-config-min-width.png |
| 构建 | cmake --build build/macos-debug --target bundle_mqtt_broker -j 4，README 更新后资源增量构建 | 成功；只有既有重复 OpenSSL 链接提示，未增加依赖 |
| Cocoa 原始成品 | 临时配置下运行 tests/test_ui_migration_runtime.py | 完整 UI/MQTT 通过；包含上一轮 macOS 顶栏处理与几何断言，配置迁移、工作区/会话重启恢复及 MQTT 本机收发正常 |
| 最终随包验收 | 114 个 Mach-O 架构/依赖、codesign deep strict；清理 Qt/QML/DYLD 环境后对 build/macos-debug/compact-config/通联工坊.app 运行同一 Cocoa 完整回归 | 均通过；确认独立应用可运行，主题/全局样式与本轮表单共同生效 |
| 唯一交付与流程 | 更新 release_package/通联工坊.app，源/目标摘要、签名、目录与六份流程文件核对 | 一致；目录只放应用，无 ZIP 或旧应用备份，git diff --check 通过 |

最初保留的旧“所有参数单行”断言按新需求失败，随后替换为四组参数/三行、窄区域和焦点可达性检查，保留编码激活、完整显示和配置行为验证；未把失败写作通过。

最终主程序 SHA256：0bbc01585c323bbc31486afd8695d97550af9051755eea87660a97d869ed91a2。结构化记录：build/macos-debug/compact-config-verification.json。日志：/tmp/loganalyqt-compact-config-qml.log、/tmp/loganalyqt-compact-config-build.log、/tmp/loganalyqt-compact-config-visual.log、/tmp/loganalyqt-compact-config-native-ui.log、/tmp/loganalyqt-compact-config-packaged.log。

未改后端/协议/持久化格式，未重复 SSH 后端专项或 Windows 实机验收；保留之前 macOS 顶栏修复。版本仍为 5.0.2 本地 Debug/ad-hoc，未提交、推送或发布。

## 2026-10-27 v5.0.3 提交前回归（T35）

| 范围 | 实际执行 | 结果 |
|---|---|---|
| 版本/格式 | check_build.py、CMake/README/CHANGELOG、git diff --check | 5.0.3 一致，格式通过，主要说明两项 |
| 监控/配置/全局提示 | python3 tests/stage10_qml_tests.py | 109 passed / 0 failed，23.862 秒 |
| 窗口/解析/通用控件 | QT_QPA_PLATFORM=offscreen python3 tests/prot698_qml_tests.py | 89 passed / 0 failed，4.218 秒；macOS 真实 Cocoa 历史结果见 T33，未将本次 offscreen 写成 Cocoa |
| 发布工具 | python3 -m unittest tests.test_release_automation tests.test_ci_packages tests.test_github_release tests.test_ci_workflow -v | 38/38 通过，2.163 秒；临时 HTTP 服务仅监听 127.0.0.1 |
| 本机构建 | cmake --build build/macos-debug --target bundle_mqtt_broker -j 4 | 成功，主程序/Broker 重新生成；仅既有重复 OpenSSL 链接提示 |

日志：/tmp/loganalyqt-v503-qml.log、/tmp/loganalyqt-v503-parser.log、/tmp/loganalyqt-v503-tooling.log、/tmp/loganalyqt-v503-build.log。提交前逐一核对六份流程文档，发布范围为 T33/T34，后端和持久化格式未变。

本次提交时，远端同提交构建、正式附件和公开更新尚待执行，本地固定应用也须后续更新为 5.0.3；不得把 T33/T34 或上一版本结果替代本次发行验收。

### T35 本地 5.0.3 成品验收

本次新编译主程序/Info.plist/Broker 复用相同 Qt 依赖部署，114 个 Mach-O 的 arm64/随包依赖与 codesign deep strict 通过。清理 Qt/QML/DYLD 环境，使用临时个人数据对独立应用执行 Cocoa tests/test_ui_migration_runtime.py，完整 UI/MQTT 通过，含 macOS 顶栏处理/几何恢复、全局样式、配置/工作区迁移和重启保留及本机 Broker 清理。

已更新唯一 release_package/通联工坊.app，版本 5.0.3；源/目标主程序摘要一致，签名通过，目录只保留应用，无压缩包/日期目录/旧应用备份。主程序 SHA256：876d2771387da30ee907202c3e449d240ee256aa23892ca131542c485fe261b2。结构化记录：build/macos-debug/v5.0.3-local-verification.json，日志 /tmp/loganalyqt-v503-packaged-runtime.log。本地 Debug/ad-hoc 不代表正式远端发行验收。

### T35 远端发行与公开更新最终验收

| 范围 | 实际证据 | 结果 |
|---|---|---|
| 同提交双平台构建 | [40480869250](https://github.com/jsnjjnzx/LogAnalyQt/actions/runs/40480869250)，head 4093a81f9d37640f175ab38c5f186b00df0ae8b4 | 总体 success；Windows 常规 CTest 25/25、UI 1/1，macOS 常规 CTest 28/28、UI/原生激活 2/2；预检/稳定性、打包、独立启动及 HTTPS 探测通过 |
| 产物来源门禁 | find_validated_build.py --repository jsnjjnzx/LogAnalyQt --commit 4093a81f9d37640f175ab38c5f186b00df0ae8b4 | 实际通过，选择 40480869250；两个平台成功、同仓库同 main 提交、未过期产物 |
| 标签与正式发行 | v5.0.3、[40482963607](https://github.com/jsnjjnzx/LogAnalyQt/actions/runs/40482963607) | 门禁后创建并推送标签，目标为已验证源码；发布任务 success，正式非草稿/非预发布且 latest，7 附件完整，说明来自本版 CHANGELOG |
| 更新元数据 | 保存 release.json、下载 update.json 后 assert 核对 | update.json 实际 SHA256 与 GitHub digest 一致，6 个产物名称/大小/摘要/下载地址完全匹配；公开前所有上传摘要由现有发布脚本验证 |
| 双平台公开更新 | 发布 run 的 macOS/Windows Verify published update | 均 success；模拟旧客户端走生产更新路径，实际公开下载、SHA256、替换、重启到 5.0.3、主窗可见/激活和工作区草稿/便携数据保留通过 |
| 流程与格式 | 六份根流程文档、git diff --check | 核对一致；历史本地未发布记录保留历史含义，后续补记仅改证据文档 |

远端材料：`build/v5.0.3-release-evidence/verification.json`、release.json、update.json、update-verification.log；公开结果以所链接的 Actions 和 [Release](https://github.com/jsnjjnzx/LogAnalyQt/releases/tag/v5.0.3) 为准。

首次 main 直连推送超时，核对未更新后重试同一提交成功，不把超时写成成功。受控 CI 与合成数据不代表实体 Windows、企业网络或真实 SSH 设备验收；macOS 为 ad-hoc，未经 Apple 公证。旧偶发首连提示未在本次修改 SSH 逻辑或宣称修复。
