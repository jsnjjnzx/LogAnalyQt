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
