# MQTT 实现与验证记录

日期：2026-10-18。界面采用 `design/mqtt-preview/v2/` 已确认的上下布局。用户名、密码按用户要求直接保存在本地 JSON 文件中。

## 已实现

- 原生 QML MQTT 页面、上下分区拖动/展开、连接与服务配置面板。
- libmosquitto 客户端；一个外部连接与一个本机测试客户端可以同时工作。
- 应用管理的 Mosquitto 子进程；端口/网卡、匿名或用户名密码认证、可选 TLS、接入记录与服务事件。
- MQTT 3.1.1/5.0 基础收发、QoS 0/1/2、Retain、订阅通配符、会话与遗嘱选项。
- 原始载荷保存、文本/JSON/Hex、过滤、暂停、边界缓存、复制、JSONL 导出、发送历史。
- 配置/草稿/布局恢复；不自动建立网络连接；损坏配置保留。
- 接收区左侧可收起的订阅列表、12 种预设颜色与自定义色值、独立的接收显示勾选；颜色和显示选项随连接保存。
- Windows 静态依赖与内嵌 Broker 资源；macOS 应用包辅助程序。未改变现有发布资产命名或版本号。

## 已验证：本机 macOS

独立构建目录为 `build/mqtt-validation`。测试使用临时配置、随机回环端口和合成报文，没有连接公司终端。

`mqtt_tests` 已通过：

- Hex 零字节、JSON 校验不改变原始输入、无效主题/端口、缓存条数与字节上限。
- 明文配置保存、重启恢复、独立 Client ID、损坏文件不覆盖。
- 本机 Broker 与两个客户端的实际收发，QoS 0/1/2 确认状态。
- 主题/内容过滤、无效过滤保持旧视图、暂停时继续接收。
- 连续 1,000 条本机消息收发及主事件循环响应检查。
- 保留消息、新订阅者收到 Retain 标志、清空界面不删除 Broker 数据。
- 独立原始 MQTT 连接异常断开后的遗嘱。
- JSONL 导出还原二进制载荷。
- MQTT 5、持久会话离线取消订阅、断线重连恢复、拒绝离线新发送。
- 端口占用不接管其他监听者、用户名密码认证、错误认证停止重试。
- TLS、主机名解析、证书错误、中文目录与中文载荷。
- 超大入站包在读取包体前拒绝。
- 独立受控 Broker 夹具中，QoS 1 重连保持 Packet ID / DUP；QoS 2 从 PUBREL 阶段继续确认。用户发送计数保持 1。

`test_mqtt_broker_lifecycle.py` 已通过：管理进程被强制结束后，辅助程序退出、端口释放。

原生 Cocoa `--ui-smoke --mqtt-checks` 已通过大窗口及 1200×800 窗口检查，包括真实按钮收发、暂停/恢复、单区展开、拖动分区、切换页面保持连接、配置编辑与无效端口提示。截图位于本目录，图片为本机回环验收数据。

Broker 辅助程序的 `otool -L` 仅列出系统库；它不依赖额外安装的 Mosquitto、OpenSSL 或 cJSON 动态库。

独立应用包已完成 126 个 Mach-O 文件的架构与依赖检查，以及本机签名校验。复制到临时目录、移除 Qt/MinGW/DYLD 开发环境路径后，原生页面与 MQTT 回环检查均通过。Homebrew 框架的绝对 install ID 已在打包阶段规范化；对应测试确认不会修改原始库。

额外验证了更新器与 MQTT Broker 的资源嵌入：两者使用独立符号，零字节和高位字节保持不变。打包/发布工具的原有 25 项测试及新增打包修复测试通过。正式用户 MQTT 配置目录未被本轮验收创建，测试配置位于隔离目录。

## 主题颜色与多选显示扩展

本轮扩展保持 V2 上下布局和全宽发送区。主题颜色同时用于消息列表和详情标题；多个订阅重叠时，显示采用“任一匹配订阅勾选”，颜色采用“最具体订阅、同等具体程度取列表顺序”。显示选择只改变接收记录，不取消网络订阅。旧配置自动补齐默认颜色并全部显示。

本机已通过三主题实际收发、隐藏期间后台接收、重新显示历史、颜色即时更新、通配符优先级、配置恢复、编辑订阅保留外观设置及按显示选择导出测试。实际界面新增、勾选、颜色修改、收起列表和发送区宽度检查通过；原生 Cocoa 的 1440×900 与 1200×800 窗口检查通过。

2026-10-18，[本轮双平台云端构建](https://github.com/jsnjjnzx/LogAnalyQt/actions/runs/39115042043)通过，构建源码提交为 `eb935cd5afbc84b4d3a6e0536834c89a51ffef19`。Windows 的 23 组 CTest 和移除开发环境后的单 EXE 运行检查通过；macOS 的 28 组 CTest 和成品运行检查通过。仍使用独立测试分支，未合并主分支、未发布正式版本。

本地 macOS 包另外完成了 126 个 Mach-O 文件的架构与依赖检查、本地签名校验、清理开发环境路径后的完整界面回归，以及最终 ZIP 解压后的主题颜色与显示交互检查。实际打开颜色面板，确认 12 个色块全部可见且第 12 个可以正常选中。

本轮交付文件位于 `output/mqtt-topics/`，内部版本号仍为 `4.0.1`：

- `LogAnalyQt-MQTT-Topics-Windows-x64.exe`，SHA-256：`d2bc19705d70dbf2ea4cad27fe3278892c05682f3369be21de7344192e61b6dc`。另有包含 EXE 和校验文件的同名 ZIP；EXE 与云端成品字节一致，ZIP 内文件已再次核对。
- `LogAnalyQt-MQTT-Topics-macOS-arm64.zip`，SHA-256：`42c8dce11d5d65d11ac733cb1d6068d5c5c9e972280f680c6ba024ca07ec50b4`。这是本地签名的 macOS 测试包。

用户已反馈上一轮基础测试版大致正常；本轮新增功能在用户远程 Windows 和实际终端上的效果仍待用户复测。以下保留上一轮构建记录。

## 基础 MQTT 版本的 Windows 与 macOS 云端构建

2026-10-18，[GitHub Actions 测试构建](https://github.com/jsnjjnzx/LogAnalyQt/actions/runs/39111111214)通过。构建源码提交为 `38f4fb763066b5b10ea409c6264790b4add6cb7f`，位于独立分支 `codex/mqtt-windows-preview-20261018`。未合并主分支，未发布正式版本。

- Windows x64：23 组 CTest 全部通过，包括 MQTT 收发、TLS、中文路径、QoS 重连恢复、管理进程异常退出后的 Broker 清理，以及界面与工作区恢复回归。
- Windows 单 EXE：依赖检查通过；隐藏 Qt 目录并清理开发环境 PATH 后，成品实际提取和启动内置 Broker，完成 MQTT 回环收发、V2 界面操作、辅助程序清理与更新端点 HTTPS 检查。
- macOS arm64：28 组 CTest 全部通过，成品 MQTT 与界面回归通过。
- 本地下载的 Windows EXE 已验证 x64 架构及 SHA-256，另存测试版文件名后字节保持一致。压缩包也已解读并核对其中 EXE 的校验值。

基础 MQTT 测试版（上一轮）：`output/mqtt-preview/LogAnalyQt-MQTT-Test-Windows-x64.exe`。内部版本号仍为 `4.0.1`。

SHA-256：`42ac35114fb04174a216fb1e1042c39bdd451fecc45a30b4b5e7ccc6d87647fd`。

Windows 实际构建期间修复了网卡变量与系统宏重名、Broker 管道日志缓冲，并补齐界面测试的渲染等待及崩溃清理夹具的显式日志配置。上述结果包含这些修复。

## 构建与发布验证入口

- CTest：`mqtt_tests`、`mqtt_broker_lifecycle_tests`。
- 打包应用：`tests/test_ui_migration_runtime.py` 包含 MQTT 回环验收和明确的成功标记检查。
- Windows CI 隐藏 Qt/MinGW 并移除开发环境 PATH 后运行单 EXE；MQTT 验收需实际提取并启动内置 Broker。
- Windows OpenSSL 3.5.9 通过 MSYS2 的 Perl/make 和项目的 MinGW 工具链静态构建。最终软件不依赖 MSYS2。
- 固定源码：Mosquitto 2.1.2、cJSON 1.7.19，下载均校验 SHA-256。许可证随应用资源分发。
- Windows 证书路径使用 UTF-8，Mosquitto 的预检查与 OpenSSL 采用一致的文件读取 API，已通过 Windows CI 的 TLS 与中文路径测试。加密私钥不会在客户端后台线程等待控制台口令。

## 待验证边界

- Windows CI 已通过；用户的远程 Windows 电脑、显示环境及实际部署目录仍待用户实测。
- 公司终端、公司局域网、终端自带 Broker 的实际兼容性未验证。
- 本地签名与本机运行不能替代正式签名、公证或公司发布验收。
