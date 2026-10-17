# 隔离 SSH 成功会话测试

这是一项可选的 macOS 工程验收，不是产品运行依赖，也不连接公司设备或系统 sshd。服务端用 [官方 PyPI Paramiko](https://pypi.org/project/paramiko/) 的 [ServerInterface](https://docs.paramiko.org/en/stable/api/server.html) 和 [Transport](https://docs.paramiko.org/en/stable/api/transport.html) 完成真正的 SSH 握手、密码认证、PTY 请求及 shell channel；测试没有自行实现 SSH 加密。

## 隔离边界

- 仅绑定 `127.0.0.1`、操作系统分配的随机端口；另一个随机端口故意不发送 SSH banner，用来测试握手取消。
- RSA 主机密钥每次在内存中新建，进程结束即丢弃。账号固定为 `loganalyqt-test`，密码为公开的合成值 `synthetic-loopback-password`；不创建系统用户，不访问 PAM、真实密码或钥匙串。
- 服务端只解释 `echo <文本>` 和固定的测试断线指令；其他输入返回 `TEST_REJECTED`，从不调用 shell、exec、eval 或系统命令。PTY/shell 是 SSH 协议层的测试 channel，不是系统终端。
- C++ harness 直接编译当前 `SshTerminal`，注入线程安全内存凭据仓库和 `QTemporaryDir`。INI 与 `ssh-known-hosts.json` 只写临时目录，不读取或修改全局 known_hosts。
- 测试进程结束会关闭 listener、channel、transport；临时数据目录自动清理。测试不能证明真实钥匙串授权、真实设备、系统 shell 全部行为或 Windows。

文件：`ssh_success_session_server.py` 提供测试 peer；`run_ssh_success_session.py` 启停 peer 并独立核对服务端计数；`ssh_success_session_tests.cpp` 驱动产品 worker。

## 复现

先准备可删除的测试虚拟环境。以下为本次实际版本，不写入项目 requirements 或根 CMake：

```sh
python3 -m venv /private/tmp/loganalyqt-ssh-success-venv
/private/tmp/loganalyqt-ssh-success-venv/bin/python -m pip install --disable-pip-version-check --no-cache-dir --index-url https://pypi.org/simple paramiko==5.0.0 bcrypt==5.0.0 cffi==2.0.0 cryptography==50.0.2 invoke==3.0.3 PyNaCl==1.6.2 pycparser==2.23 typing_extensions==4.16.0
```

该安装只用于测试；首次需要访问官方 PyPI。Python 需 3.9 或更新版本。项目自身正常构建应已产生 `build/macos-debug/third_party/libssh2/src/libssh2.a`。为避免和主构建并发写入，同步复制归档到临时工程，再只构建临时 target：

```sh
mkdir -p /private/tmp/loganalyqt-ssh-success-build-src
cp build/macos-debug/third_party/libssh2/src/libssh2.a /private/tmp/loganalyqt-ssh-success-build-src/libssh2-snapshot.a
cat > /private/tmp/loganalyqt-ssh-success-build-src/CMakeLists.txt <<'CMAKE'
cmake_minimum_required(VERSION 3.21)
project(SshSuccessSessionChecks LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network)
find_package(OpenSSL REQUIRED)
set(ROOT /Users/zx/CodeHaven/LogAnalyQt)
add_executable(ssh_success_session_tests ${ROOT}/tests/ssh_success_session_tests.cpp
    ${ROOT}/src/core/SshTerminal.cpp ${ROOT}/src/core/SshTerminal.h
    ${ROOT}/src/core/SecureCredentialStore.cpp)
target_include_directories(ssh_success_session_tests PRIVATE ${ROOT}/src/core ${ROOT}/third_party/libssh2/include)
target_link_libraries(ssh_success_session_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network
    ${CMAKE_CURRENT_SOURCE_DIR}/libssh2-snapshot.a OpenSSL::Crypto "-framework Security" "-framework CoreFoundation")
CMAKE
cmake -S /private/tmp/loganalyqt-ssh-success-build-src -B /private/tmp/loganalyqt-ssh-success-build -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build /private/tmp/loganalyqt-ssh-success-build -j4
/private/tmp/loganalyqt-ssh-success-venv/bin/python tests/run_ssh_success_session.py --binary /private/tmp/loganalyqt-ssh-success-build/ssh_success_session_tests
```

如项目不在上述目录，仅修改临时 CMake 的 `ROOT`；Qt/OpenSSL 路径按本机调整。runner 不自动安装依赖、不自动运行共享构建，缺依赖或二进制直接失败，不把 SKIP 当成功。网络 sandbox 需允许这一项回环测试。

## 已执行结果（2026-10-15）

日志：`/private/tmp/loganalyqt-ssh-success-session.log`。本次使用 Paramiko 5.0.0、Qt 6.11.1、OpenSSL 3.6.3，产品源码和项目 libssh2 归档的临时副本。

- **37 项检查，0 失败，3 次完整认证成功会话**。
- 首次 SHA256 指纹与服务端独立计算值一致，用户确认前服务端认证次数为 0；确认后进入 Connected，并收到 `TEST_ECHO:LOOPBACK_FIRST` 和 UTF-8 中文回显。
- 断开后用同一 pin 和注入仓库内保存的合成密码重新连接，无重复 TOFU 提示；第二次命令真实回显。
- 服务端主动断开后自动重连并真实回显；再次进入重连计时后主动取消，没有额外认证。
- 新临时信任目录中的首次密钥等待被取消，迟到接受不创建 pin、不认证、不连接；无 banner 的握手取消也不阻塞主线程返回。
- 独立服务计数：`connections=4, authAttempts=3, authAccepted=3, pty=3, shell=3, drops=2`；收到 `LOOPBACK_FIRST`、`中文回显`、`LOOPBACK_SECOND`、`AUTO_RECONNECTED`。

成功证据是双方完成真实 SSH 协议与字节收发，不再只有零认证/发起认证的负例。系统钥匙串、Finder、公司 SSH 主机及 Windows 验收仍分别记录；这里没有替代它们。

本次编译时来源标识：项目 libssh2 为 1.11.0；复制归档 SHA256 为 `526111b05df0453b18ab57c3b9579668f90d482578e6d8b1bc2b70ab7d8319ac`。`SshTerminal.cpp` 为 `413496fee988a0f6ebd5e857e29b1f7ae7bc1206a43d2cb5f2e856bc168e5a2f`，`SshTerminal.h` 为 `08a563c938836f88a193b0e5b44531897cea6a04cca321f13d6bf819dd7a91e2`。后续源码或归档变化应重新构建、运行，不沿用本次通过结论。
