# 固定深色原生毛玻璃与窗口布局验收

日期：2026-10-18。

## 最终范围

- 新版工作台采用 macOS 原生 `NSVisualEffectView`，以 behind-window 模式模糊窗口后方内容。材质位于 Qt 视图后方，不拦截输入，不替换 Qt 的 contentView。
- 使用固定 Dark Aqua 材质及深色 QML 配色；移除新版的深浅色/跟随系统开关、快捷键、配置读写、状态属性、变化监听、浅色颜色分支，以及日志着色中的浅色支持。
- 程序内“减少透明度”和 macOS 辅助功能偏好均可退回不透明深色背景；非 macOS 平台沿用应用内部背景效果。
- 品牌名称和版本移到左下角，按用户后续要求移除该处图标；导航直接从顶部开始。
- 标题栏图标由 21px 的通用字符改为 16px 的现有 Material Design Icons 字体图标，保留 36×32px 点击区域；最大化后显示还原图标与相应无障碍名称。
- 按用户确认，仅清理新版工作台，旧版兼容 QML 的深浅色功能保持原样。

实现参考：[Apple NSVisualEffectView 文档](https://developer.apple.com/documentation/appkit/nsvisualeffectview)，采用公开的材质、behind-window blending 和可拉伸 maskImage 接口。

## 已验证

| 检查 | 结果 | 本地证据 |
| --- | --- | --- |
| macOS Debug 编译 | 通过 | `build-dark-only-final.log` |
| 界面、AI 页面及窗口控制回归 | 62 项通过，0 失败 | `qml-tests.log` |
| 同步和实时终端界面回归 | 22 项通过，0 失败 | `sync-terminal-qml-tests.log` |
| AI 配置回归 | 通过 | `ai-settings-tests.log` |
| 原生材质检查 | 通过：挂载位置、输入穿透、固定深色、减少透明度、系统偏好读取、缩放、最大化圆角切换与清理 | `native-tests.log` |
| 完整应用原生回归 | 通过：旧浅色设置不生效、导航、解析、11 个隔离页签、重启恢复、损坏状态和保存失败保护 | `runtime-tests.log` |
| 最终原生窗口人工操作 | 已检查导航、最大化/还原、固定深色设置页及移除图标后的布局 | `final-dark-only-settings.png` |

完整应用回归预置旧的 `ui/next/appearance/dark=false` 和 `followSystem=true`，验证新版仍保持深色配色且不存在主题切换控件/状态属性。大小写不敏感的代码检查确认新版已无相关状态、切换监听、快捷键和颜色分支。

窗口级截图没有完整呈现桌面透光效果，不能仅凭截图证明背景采样。用户在本次对话中直接确认能看到背后的蓝色/橙色测试背景透入并被模糊；原生检查另外验证了 behind-window 材质的挂载与状态。测试背景窗口已关闭，最终隔离预览保留供查看。

早期 `final-light.png` 等截图属于移除模式切换前的过程证据，不代表最终仍提供浅色模式。最终界面以 `final-dark-only-settings.png` 为准。

## 边界

未进行 Windows 实机外观验证；真实桌面毛玻璃本轮只在 macOS 实现。未制作对外发布包，也未新增真实模型请求或设备收发验收。测试使用临时副本和独立数据目录。
