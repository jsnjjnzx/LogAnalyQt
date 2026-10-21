# SSH 监控性能与功能验收

本目录直接编译生产 `SshTerminal`，并加载生产 `LiveLogDialog.qml`。SSH 服务端只监听本机回环地址，使用临时生成的密钥和合成凭据，不执行系统 shell、不连接真实设备、不使用系统钥匙串。

## 测试数据与磁盘使用

- 压力日志按批次在线生成，经真实加密 SSH 连接传输，在内存中检查序号、中文内容和完整性；不生成、回放或保存日志载荷文件。
- 服务端每次只生成小批数据。客户端历史有界，测试器只保留固定大小延迟直方图和每秒一条内存统计。
- QML 导入、合成连接配置、主机信任记录需要少量临时文件。设置 `TMPDIR` 到**已经确认的 RAM 文件系统**，可以让这些文件及完整应用回归的诊断日志也只写内存。macOS 的 `/tmp` 本身不是 RAM 磁盘。
- `--output` 保存小型统计 JSON；`--progress` 每分钟更新小型统计文件，建议也放在 RAM 中。都不包含原始日志载荷。
- 测试脚本不会自动安装依赖、创建或格式化磁盘。调用者需先准备独立 Python 环境中的 Paramiko。

## 构建与运行

需要 Qt 6.11 或更高版本（当前项目构建环境为 6.11.1）、CMake、C++17 编译器。Unix 使用系统 iconv 处理 GBK/GB2312，Windows 使用系统代码页 936；不引入 Qt Core5Compat。

```sh
cmake -S tests/ssh-performance -B build/ssh-performance -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/ssh-performance -j 6

build/ssh-performance/ssh_stream_tests
python tests/run_ssh_success_session.py --binary build/ssh-performance/ssh_success_session_tests
python tests/run_ssh_stream_session.py --binary build/ssh-performance/ssh_stream_session_tests

python tests/run_ssh_performance.py --binary build/ssh-performance/ssh_performance \
  --seconds 600 --rate 1048576 --output single-session.json
python tests/run_ssh_performance.py --binary build/ssh-performance/ssh_performance \
  --seconds 600 --sessions 3 --rate 524288 --headless --output three-sessions.json
python tests/run_ssh_performance.py --binary build/ssh-performance/ssh_performance \
  --seconds 60 --input-probe --output input-response.json
python tests/run_ssh_performance.py --binary build/ssh-performance/ssh_performance \
  --seconds 60 --sessions 3 --rate 524288 --headless --burst-first --output burst-fairness.json
```

完整应用的界面回归仍由 `tests/test_ui_migration_runtime.py` 执行，其中新增了实际点击日志、右键菜单以及真实 IEC 104/698 字段校验。QML 测试还检查选中高亮、搜索、历史淘汰和暂停回看。

### 完整应用性能测试

主项目配置 `-DLOGANALYQT_BUILD_PROTOCOL_TESTS=ON` 后，应用内编入同一测量器。`--full-app` 打开完整窗口、生产监控页面及真实 `LogBackend`；只有 SSH 凭据存储被隔离测试实现替代。应用设置和诊断日志写入 `TMPDIR` 中的独立测试目录。正常启动不启用测量器。

```sh
python tests/run_ssh_performance.py --binary /path/to/appLogAnalyQt \
  --full-app --seconds 180 --output full-app.json
python tests/run_ssh_performance.py --binary /path/to/appLogAnalyQt \
  --full-app --seconds 300 --interaction-probe --output interactions.json
python tests/run_ssh_performance.py --binary /path/to/appLogAnalyQt \
  --full-app --seconds 60 --view-state paused --output paused.json
python tests/run_ssh_performance.py --binary /path/to/appLogAnalyQt \
  --full-app --seconds 60 --view-state background --output background.json
```

完整窗口固定为 1440 × 900 逻辑像素，记录实际缩放倍率。测试开始前检查前台和可见状态，运行期间每秒检查一次；仅 `controlled_window: true` 的运行可纳入前台 CPU 对照。`validated` 代表数据与功能检查通过，两者独立。正式对照期间不要并行构建、采样或运行其他压力任务；CPU 关键场景至少重复三次并报告中位数与范围。`--view-state background` 指切换应用内部页面，窗口仍保持前台。

`--interaction-probe` 每 600 ms 从后台向 GUI 队列投入一项操作，逐项检查结果，再记录同步该操作的 Qt 帧交换时间。输入使用真实终端按键事件；暂停、继续、切页、选行使用窗口鼠标事件；滚动使用窗口滚轮事件。搜索通过设置真实搜索输入框触发，解析通过内存中的 698 示例调用生产选行与解析方法，并核对 OAD 字段。它们不包含操作系统输入设备延迟，也不能代替右键菜单路径的独立功能回归。各操作单独报告样本数、P95、最大值和失败次数。

`--full-app --sessions 3` 在完整应用中建立三个真实监控标签，每秒轮换当前标签，检查连接及会话对象保持不变。结果中的 `ui_session_count`、`tab_switch_checks`、`tab_switch_errors` 分别记录界面会话数、切换次数和错误次数；这项功能检查不代替独立的性能对照。

## 基线和测量口径

先冻结改动前的 `SshTerminal.cpp/.h` 和 QML。独立构建时可通过 `-DSSH_SOURCE_DIR=/path/to/frozen/src/core` 编译冻结的 SSH 源码；运行时用 `--qml-source /path/to/frozen/qml/Next` 加载同一份旧界面。冻结目录需保留相对头文件路径。基线和优化版使用相同 Qt、编译模式、生成器、窗口大小和 5000 行历史设置。

每条压力记录为 512 字节，包含会话号、递增序号、发送时间、中文与十六进制内容。速率是每会话的目标速率；同时记录服务端实际发送耗时，以识别 SSH 反压或发送端瓶颈。

| 字段 | 含义 |
|---|---|
| `received_lines` / `content_errors` | 对进入模型的每条记录检查序号、会话和固定内容；正常历史淘汰不算接收丢失 |
| `cpu_seconds` | 测量区间内客户端整个进程消耗的 CPU 时间，包含校验器和 Qt 显示 |
| `cpu_percent_one_core` | CPU 时间除以墙钟时间；100% 表示占满一个核心，不是整机所有核心的百分比 |
| `payload_mib_per_second` | 实际校验完成的数据量除以总耗时，含结束时的排空和短暂画面收尾 |
| `sender_to_model` | 同机发送时间到记录进入客户端模型的延迟，是客户端接收后处理延迟的保守上界 |
| `model_to_next_frame` | 在 Qt 场景同步时锁定待显示批次，到随后帧交换的时间；不等同于硬件显示延迟 |
| `latest_visible_sender_to_frame` | 自动跟随模式下，最新提交记录的发送时间到对应 Qt 帧交换的代理指标；不宣称每条中间日志都曾物理上屏 |
| `event_loop_delay` | 10 ms GUI 心跳定时器的额外延迟，反映主线程阻塞，不替代真实输入测试 |
| `input_response` | 可选探针从后台排入 GUI 队列，到真实终端接收控件处理按键并发送原始输入的延迟；交替检查 ASCII 按键与中文输入法提交，字符只发送到隔离回环 peer，不执行系统命令 |
| `rss_*` | 客户端进程驻留内存；包含 Qt 等开销，不等同于历史文本预算或 macOS 的 physical footprint |
| `model_resets` | 测量期间模型重置次数；正常追加和历史滚动应为零 |
| `session_progress` | 每会话校验行数和最长模型提交间隔，用于检查突发输出时其他会话仍然推进 |
| `diagnostic_only` | 使用 `--profile` 时为真；采样和内存映射检查会干扰运行，不纳入正式 CPU/延迟对照 |

超过 10 分钟的测试另保留 `first_600s` 快照，便于将初始 10 分钟的性能与基线比较，再用完整长测检查内存趋势。`--headless` 验证后端并发，帧延迟字段无意义；它不代表已经交付多设备管理界面。

`--burst-first` 将会话 0 每五秒的数据量集中在约半秒内发送，其余时间等待；另外两个会话持续发送。验收同时要求内容完整、会话 1 和 2 的最长提交间隔小于一秒。该指标检查会话是否被饿死，不是前台操作延迟。

macOS 可用 `--profile /path/on/ram/profile.txt` 单独诊断至少 30 秒的测试。启动 12 秒后采集稳态线程栈及前后两份内存分布；文件只含聚合诊断信息，不含压力日志。系统内存检查会暂时停顿被测进程，因此此类结果只能解释热点，不能证明交互或延迟验收通过。

## 实现边界

- 每会话待提交记录按 4 MiB 预算控制，历史按 32 MiB 预算和配置行数共同控制；计费含字符串容量及记录对象，容器/分配器和 Qt 界面开销由进程内存指标另行观察。
- 未完成长行分为最多 8192 个 UTF-16 单元的显示片段，保持逻辑行关联和代理对边界。缺少前半部分或尚未结束的记录有片段提示，不作为完整报文解析。
- 队列到达高水位时暂停读取，消费后恢复；不会通过任意删除原始字节来提高吞吐。远端生产、传输和断线场景仍可能导致远端自身丢失，本测试不作远端完整性保证。
- 连接、解码与发送由各自工作线程串行处理；UI 批量提交有字节和行数预算。前台合并刷新间隔 180 ms，后台 500 ms，积压批次有界续处理。
- 普通日志行只绘制可见宽度内的一行，避免为屏幕外的字符生成绘制几何；模型、搜索、复制和报文解析仍使用完整文本。选中报文时继续按需创建原有 `TextEdit`，保留完整选区。未换行提示符和远端回显在日志末尾显示，当前行支持 CR、退格、光标左右移动和擦除；这不是完整的 VT 屏幕模拟器。
- 每次 UI 提交最多 4096 行/2 MiB。Tab 截获原始响应最多 256 KiB，过载恢复与迟到响应也经过接收预算，避免空行密集的响应一次展开为大量待显示对象。
- 独立测试程序的三会话使用不同测试配置目录；完整应用测试使用生产多会话界面和共享数据目录，验证会话配置隔离与首次确认指纹的并发写入。完整日志保存和完整 VT 终端模拟不在本测试范围。
- 本机成功不代替 Windows 实机、真实网络/设备日志、系统钥匙串或正式发行验收。
