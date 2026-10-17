# 第10阶段独立实现审核

审核者：GPT-6 Astra。日期：2026-10-15。范围仅为用户授权的 Mac SSH、安全凭据、系统 SMB 连接与同步、错误/取消及便携目录行为。**最终审核通过：本轮已报告的阻断项均已修复，最终源码正式构建及21项整合回归通过。实际SSH验收由用户明确暂缓，保留待项；不阻塞本轮约定范围收口。** 不提交、不改主构建或已有用户 dirty Main/LogBackend/MigrationUiChecks。

## 已核对的实现方向

- SecureCredentialStore 按 AiFraming/Ssh/ProtocolSync 服务域隔离，AI原命名空间保留；writeVerified验证后才允许调用者清旧字段，验证失败尝试恢复旧值并明确报错。系统存储继续由后台调用，不回显保存密码。
- SSH worker使用非阻塞socket/libssh2阶段及异步DNS，认证前先检查host+port的应用专用指纹记录；首次挑战绑定generation/requestId/fingerprint，变化/拒绝/不支持算法阻断密码及自动重连。shared buffer和store由worker拥有，析构不等待系统授权。
- SMB输入不含凭据，交系统处理登录，按系统真实smbfs来源映射挂载路径；未将Finder接手等同挂载成功。旧protocolSync密码以含username/password的JSON保全到源INI派生legacy-unbound profile，不绑定新目标或代填Finder；完成时复核原字段。
- 上传已改QSaveFile原子提交，下载保留XML结构/语义校验；已提交下载即使逻辑取消仍发重载通知，不把状态失效当作磁盘操作撤销。

## 审核发现与修复跟踪

| 编号 | 级别及事实 | 必须满足的修正/证据 | 当前状态 |
| --- | --- | --- | --- |
| S10-01 | P1：SSH迁移writeVerified后直接删除ssh/password，未重读身份/密码；缺host/user时仍派生默认身份 | 检查旧身份来源；系统等待后重读并逐字段匹配快照，变化保留新值；阻塞fake中修改INI回归 | 已修复，第二轮静态及SSH回归复核通过 |
| S10-02 | P1：Authenticating中阻塞Keychain返回后只查generation，15秒已超期仍可能发一次密码 | 认证调用前同时复查deadline，超时后零服务器密码事件 | 已修复，第二轮静态及SSH回归复核通过 |
| S10-03 | P1：clearSavedPassword未先失效正在等待的认证，删除排队前旧读取可先认证；身份刷新也可积压 | 清除前失效连接/重连；物理凭据操作状态明确，身份改变合并为最新刷新；旧目标变更事务报告真实结果而非暗示撤销 | 已修复，第二轮静态及SSH回归复核通过 |
| S10-04 | P2：Tab补全定时器未在断开/换身份时停止，旧回调可向新会话发Tab或更新补全结果 | 断开清理timer/状态或绑定generation；不重写正常补全逻辑 | 已修复，第二轮静态及SSH回归复核通过 |
| S10-05 | P1：同源挂载歧义最初按可访问子目录过滤；不可访问同源卷会被忽略 | 按系统不同mount.path先判歧义，再验证唯一目标；明确歧义直接返回而非吞成一般超时 | 已修复，静态及独立隔离回归通过 |
| S10-06 | P1：取消的挂载探测可能仍阻塞网络stat，可反复创建线程；下载未检查读取错误 | 保留物理单飞状态，不无限创建；readAll错误不提交；测试取消后20次重试不新增probe | 已修复，静态及独立隔离回归通过 |
| S10-07 | P1：原固定.tmp会删除既有文件；远端协议文件可为符号链接 | 主代理发现并交实施者改唯一QTemporaryFile、仅清理own temp，拒绝远端具体协议symlink；预存tmp/双实例/冻结后替换symlink回归 | 已修复，静态及独立隔离回归通过 |
| S10-08 | P2：fetchSyncStatus由完成bool改成受理bool，旧LogBackend自动验证caller未处理异步失败；非Mac同步合同也需保留 | 主代理只在受保护LogBackend作最小Mac结果接线；不启用当前关闭的安全管控；非Mac保持旧返回语义 | Mac接线及非Mac旧合同已复核，安全管控保持关闭 |

补充身份与结果边界：SSH密码profile应拒绝身份控制字符或使用无歧义结构序列化，不能靠未限制字段的换行拼接造成身份碰撞。异步保存旧目标实际完成后，不能仅因界面已换目标便向用户声称事务撤销；可以不更新当前缓存，但须保持实际结果及所属目标的可理解说明。

## 已读证据与未完成验证

已读取 `validation/stage10/ssh-security-local.log` 和 `ssh-security-asan.log`，均记录隔离真实sshd的信任、迁移、目标绑定、取消和析构PASS；结合源码确认首次确认前及变化拦截路径。该批日志早于本轮提出的迁移重读/超时/clear等修正，不能自动证明新负例通过。

共享store源码未见新增AI命名空间变化；还需正式安全存储/AI回归和随机三域原生测试日志核对。SMB当前主要是挂载、文件及系统启动替身测试，不能声明公司SMB连接已验证。QML仍由验证实施者接线，两套SSH/同步页面必须检查真实属性/信号及异步受理不假报成功。

最终退出应覆盖：上述全部负例、便携目录不可写及INI清理失败、同源卷歧义、系统连接拒绝/超时/取消晚返回、原子上传/下载失败旧文件保全、已提交下载重载、窗口关闭生命周期、原有AI与协议回归。真实系统授权不绕过；Windows只保留兼容边界，不声称本阶段完成Windows新功能或验收。

## 第二轮复核进展

SSH的S10-01至04已静态复核修复：迁移先验证旧身份来源，系统存储返回后sync并重读四字段；profile改规范JSON元组；读取返回复查generation及15秒deadline；清保存密码先失效旧认证；身份刷新合并；Tab断开清理及slot拒绝；过期显式事务保持实际结果；存储false空error补失败文本，信任锁/权限与普通配置保存错误明确区分。已重新读取普通与ASan隔离sshd日志PASS。真实sshd使用不存在用户，仅证明密码发送前的信任门槛，不能证明成功PTY/shell会话；主代理已安排合成账号的独立成功会话测试，不创建系统用户或读取真实密码。

SMB已静态看到按系统mount.path先判歧义、明确Ambiguous返回、取消后物理probe单飞、读取错误拒绝、唯一临时文件、原子提交及远端具体文件symlink拒绝；正式日志待整合。另发现getXmlDiffNow打开/读取失败仍当空侧并报成功，已交实施者补明确错误，不能把权限失败显示成有效差异。

两套QML最新隔离日志 `/private/tmp/loganalyqt-stage10-ui-tests.log` 为20 passed/0 failed，旧QML46项回归亦通过。已核对Mac不创建SMB账号密码组件、SSH不读已保存密码、异步pending/完成与hostkey确认；XML diff明确PlainText。root裁定非Mac保持原行为：实际usesSecureCredentials/usesSystemSmb常量与QML分支已匹配，SSH旧password显示/连接恢复，非Mac同步返回语义保留；不将替身分支覆盖等同Windows实机验收。

主代理在受保护LogBackend中仅新增Mac异步probe结果处理，statusUpdated才认可成功，失败不循环连接；安全管控仍关闭，用户APDU解析改动保留。若将来启用该管控，主动取消probe与其他status结果的所有权仍应单独复测，不在本阶段启用。

当前仍等待：SMB差异读取错误修复、成功SSH终端会话测试、最终正式构建/全量回归，以及授权的只读SMB原生验证。TCP445连通不等于认证/共享目录已验证。本审核未提交或修改用户脏改动。


## 第三轮复核与退出结论

S10-01至08及补充的差异读取问题均已逐项复核关闭。最后差异修复在Mac使用stat区分ENOENT与EACCES等错误，检查打开、QFile和QTextStream读取结果；失败只报告错误，不发送diffFinished或覆盖前一份结果。新增隔离测试覆盖本地/远端不可读、目录读取错误、目录不可遍历、单侧和双侧真正缺失。审核者独立执行实施者的隔离protocol_sync_tests二进制，exit 0、All protocol sync tests passed。

新增已读证据：

- validation/stage10/integrated-final-build.log：正式应用完成链接及Mac helper打包；validation/stage10/integrated-tests.log：21/21通过，33.59秒。上述整合证据早于最终差异读取小修，主代理仍需最新源定向补跑，不能用旧日志代替。
- /private/tmp/loganalyqt-ssh-success-session.log：37项检查、0失败；独立Paramiko服务端观察3次成功密码认证、3次PTY及shell请求、英文/中文回显、断流后自动重连和手动取消。已读fixture源码，确认仅127.0.0.1、合成账号/密码、临时pin文件及内存凭据，不调用系统shell、不创建系统用户、不使用真实Keychain密码。
- validation/stage10/native-acceptance.md：主代理记录原生应用已连接用户确认的公司SMB共享并展示7项真实协议版本/日期/比较状态，仅只读，无同步或上传下载。此证据支持公司共享连接与目录读取，不支持公司写入验收。

结论：源码审核可通过；此前列出的安全和生命周期阻断已闭环，未发现额外阻断。正式最新源定向回归及交付状态由主代理整合。公司SSH真实账户验收若继续进行，应单独记录；当前已有隔离真实SSH完整成功会话证据。原生三域凭据验证按10B实施者已报告通过，本轮审核未重跑系统Keychain测试。Windows保持旧行为的分支已静态核对，未进行Windows实机验证。


## 最终收口（最终源码与文档）

审核者已读取validation/stage10/final-build.log，确认最终应用链接及Mac helper打包完成；final-tests.log为21/21通过、31.39秒，覆盖最后差异读取小修；final-detailed-tests.log固化各测试结果及阶段10 QML20项、旧QML46项。原生凭据native-credential-tests.log确认AI/SSH/SMB随机合成条目域隔离、验证回读、替换和删除通过；主代理确认测试项已清理。成功SSH证据已固化至validation/stage10/ssh-success-session.log，37项、3次完整会话通过。

已审核user-guide.md、acceptance.md与README入口，核对实际SSH、公司SMB只读/写入、Windows及便携目录边界。execution.md和acceptance.md同步更新最终状态。用户明确“暂时先不做实际 SSH 验收”，故不再开展真实主机验收；该项明确待验证，不作为本轮收口阻断。公司SMB7项只读已验收，公司文件写入未验收。

最终结论：阶段10按本轮约定范围通过独立审核，未发现剩余阻断项。此前各轮“等待最终补跑”等语句属于审查过程记录，以上述最终日志与本节结论为准。源码本轮无新增修改；任务分组提交与用户脏改动隔离由主代理执行，不将本轮完成扩大为公司生产正式投用。
