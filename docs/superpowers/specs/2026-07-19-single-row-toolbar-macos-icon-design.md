# 单行工具栏与 macOS 应用图标设计

## 目标

恢复主窗口顶部工具区的单行布局，并让窗口最小宽度由完整可见的控件决定；同时为 macOS `.app` 配置现有项目 Logo。

## 已确认的界面行为

- 文件夹按钮、路径输入框、文件数量、解析按钮、协议选择和四个快捷按钮始终位于同一行。
- 路径输入框占用剩余空间，并在窗口缩小时优先收缩，最小宽度为 80 逻辑像素。
- 按钮图标和文字不得裁切、换行或隐藏。
- 当路径输入框达到最小宽度后，窗口不得继续缩窄。
- 最小窗口宽度根据当前可见控件的实际隐式宽度动态计算，不写死跨平台字体相关的固定宽度。

## 实现设计

`qml/Main.qml` 使用单个 `RowLayout` 承载顶部全部可视控件。窗口的 `minimumWidth` 绑定到该布局的 `implicitWidth` 加内外边距，四个自定义缩放区域统一比较 `mainWindow.minimumWidth`。路径输入框设置 80 像素的最小和首选宽度，并通过 `Layout.fillWidth` 获取其余空间。

macOS 图标复用现有 `assets/logo.ico`，生成标准 `assets/logo.icns`。`CMakeLists.txt` 仅在 Apple 平台把该文件复制到 `.app/Contents/Resources`，并设置 `MACOSX_BUNDLE_ICON_FILE`，Windows 继续使用现有 `assets/logo.rc` 和 `.ico`。

## 验证

- 修改前的结构检查应确认当前仍存在两行 `ColumnLayout`/`Flow`，且发布包 `CFBundleIconFile` 为空。
- 修改后完成 macOS Release 编译与打包。
- 检查工具栏为单个 `RowLayout`、路径框最小宽度为 80、窗口最小宽度绑定到布局内容。
- 检查 `.app` 内存在 `Contents/Resources/logo.icns`，且 `Info.plist` 的 `CFBundleIconFile` 为 `logo.icns`。
- 验证代码签名、ZIP SHA256，并用 `open` 实际启动发布目录中的 `.app`。
