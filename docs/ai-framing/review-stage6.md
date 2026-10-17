# 阶段 6 界面接线独立审核

审核者：GPT-6 Astra，2026-10-14。对象：Next AI 助手、系统设置、Main 接线及 LogBackend facade。**阶段 6 本地接线验收通过；真实模型端到端仍待阶段 8。** 已核对七组正式集成通过日志和真实控制器属性，未把 QML 替身测试当作真实 DeepSeek 成功。

## 已核对路径

LogBackend 拥有只读 aiFraming 控制器属性；控制器在 QML 注册为不可自行创建类型。Main 将实际 backend 传入 AI 页及设置页，模型设置入口跳 page 9，成功回填走既有手动解析函数。AI 页保留当前会话内终端列表，无预置真实地址或终端持久化；提交产生独立快照。

结果展示要求 succeeded、当前 requestId、合法 Hex，复制及回填只接受本地结果。终端编辑后旧结果仍显示原冻结目标，没有冒充当前编辑目标。报文解释继续明确不可用，不走网络。设置页普通配置与 password 输入分开，保存/清除成功清空输入；生成/澄清/连接期间不能改配置，连接测试计费提示可见。

## 修复复核与证据边界

- AI 页销毁仅取消本页持有的匹配 requestId 活动请求已修，手动取消也保护所有权；普通隐藏不取消。设置页同样按 ownedConnectionId 检查；真实 controller 的 connectionRequestId 只读属性已落地并有实际 property 断言。
- 本地 inputTreeText/normalizedTreeText 已用于可展开 PlainText 参数详情，避免 JS 大整数转换；已核对 physical/日期/uint64 展示测试，具体值可审阅。
- warnings 对象显示问题已修：按本地 message 和重复对象/成员计数组合 PlainText，不再显示 `[object Object]`；真实对象形状断言通过。
- 主程序接线需真实控制器+本地工具+网络替身贯穿提交、澄清、取消、失败、成功、复制与回填，不能以 QML 假控制器通过单独证明整条链可用。

本地用例覆盖页面卸载、请求中切换/编辑终端、迟到回复、清 key 和未实现解释模式；最终目标平台体验仍需覆盖中文输入、焦点、实际设置保存重启等原生操作，不以替身结果代替。当前 macOS 基础凭据实测已在阶段 3 单独通过；真实 DeepSeek、Windows 和设备行为不计入本阶段已验证声明。

已实际读取 `/private/tmp/loganalyqt-stage2-tests/ui-review-tests.log` 的最新版本：35 项 QML 测试通过、0 失败；包括参数精确文本、生命周期所有权和 warning 对象。该批使用控制器替身；随后已单独核对真实 controller.connectionRequestId 属性/信号及主程序回归，不能仅以替身具备属性作为真实类实现证据。

最终复核：真实 controller.connectionRequestId 的 Q_PROPERTY/getter 已补，实际 QObject property 及取消后清空有 C++ 断言；`stage4-6-integrated-tests.log` 的七项目标全部通过，包含主应用迁移运行、35 项 QML、工具、控制器、协议和设置/网络。上述代码接线阻断全部关闭。

另已目视检查 `settings-live-dark.png` 与 `assistant-live-compact.png`：系统设置导航及紧凑 AI 页关键控件可见，空地址/未配置状态没有假成功。截图为配置前界面，不能证明成功结果或真实模型；最终 v3 构建的界面与模型联调仍由主代理补充证据。
