SSH 安全测试（本阶段仅 macOS）

`ssh_security_tests` 使用临时便携目录及可注入的假安全存储，覆盖主机信任记录、旧 INI 密码迁移及失败保留、目标绑定、显式密码操作完成 ID、停滞网络取消、析构，以及系统凭据操作尚未返回时的析构。审核回归还覆盖迁移期间 INI 的主机、端口、用户名、密码分别变化时保留新值，缺失旧身份字段不猜默认目标，存储返回 false 但没有错误文本时仍报告失败，以及旧目标已提交事务的真实结果不会更新当前目标。测试不读取应用实际配置或用户 SSH 密钥。

在 root 集成 CMake 目标后运行：

```sh
cmake --build build/macos-debug --target ssh_security_tests
build/macos-debug/ssh_security_tests
build/macos-debug/ssh_security_tests --local-sshd
```

`--local-sshd` 额外使用系统 `/usr/sbin/sshd` 和 `/usr/bin/ssh-keygen`，在 `QTemporaryDir` 内生成两把 Ed25519 测试主机密钥，配置只监听 `127.0.0.1` 的随机端口。使用不存在的测试用户名和合成密码，不尝试用户真实账号。它检查真实服务端日志：首次确认前、错误确认 ID 或指纹、取消后的迟到确认、明确拒绝、密钥变化时，均没有 password 认证事件；接受并成功保存主机信任后才出现 password 认证；持久记录可以复用；密钥变化不弹普通信任框、不自动重连。另有已信任主机读取密码期间取消，等待假钥匙串返回后仍不得认证的断言，并验证读取期间清除密码先取消旧认证、以及读取超过实际 15 秒连接期限后返回仍为零密码认证。由于包含真实超时等待，此完整测试约需 20 秒。

日志仅输出 PASS 或失败断言；SSH 失败时为定位问题可能打印仅含合成数据的临时 sshd 日志。监听随机本机端口在受限沙箱下可能需要测试执行权限。首次独立构建位于 `/private/tmp/loganaly-stage10-ssh`，未修改共享 build。

主机密钥变化的人工处理：

1. 停止连接，通过独立渠道向服务器管理员核对目标 host、port、算法以及 `SHA256:` 指纹。
2. 完全关闭本应用，备份用户数据目录中的 `ssh-known-hosts.json`（Mac 为 `~/Library/Application Support/LogAnalyQt/`，Windows 仍为程序同级目录）。
3. 只有核实服务器正常换钥后，手工删除 JSON 中对应 `[host]:port` 的单个记录，保留其他端点的信任记录及有效 JSON 格式。读取失败也必须人工检查，不能用“仍然连接”绕过。
4. 重开应用并显式连接，核对新弹出的目标及指纹后确认。程序只有原子保存成功后才发送密码，不提供覆盖现有记录的 API。

主机信任文件仅保存端点与主机公钥 SHA256 指纹；密码存于 macOS 钥匙串的独立 SSH 域，按规范 JSON 元组中的 host、port、username 绑定。旧 INI 密码必须通过安全存储写入并读回验证后刷新并复核旧 INI 身份与密码均未改变才删除；身份缺失或 source_changed 会保留字段，不猜默认目标；不同的已有钥匙串密码会触发冲突，保留原值供人工处理。

未验证 Windows、本机之外的设备、实际用户凭据或成功登录后设备终端行为。AddressSanitizer 测试覆盖本阶段 C++ 代码；Qt 和已构建的 libssh2 本身并未重新插桩。
