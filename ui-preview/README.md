# 日志解析首页 · 独立 QML 预览

以本次确认的毛玻璃设计图为依据，只实现日志解析首页的视觉和交互。
全部新增代码位于 `ui-preview/`，不修改原 `qml/`、C++ 解析引擎、主入口或根 CMake。
构建输出独立位于 `build/ui-preview/`。预览不读写原应用配置，也不联网。

## 启动

macOS 上双击本目录 `run-preview.command`，使用项目现有 Homebrew Qt、CMake 和 Ninja 构建并打开预览。
已编译程序位于 `../build/ui-preview/LogAnalyQtPreview.app`。

```sh
./ui-preview/run-preview.command
ctest --test-dir build/ui-preview --output-on-failure
```

其他平台使用 Qt 6.5+ 的 Quick / QuickControls2 / QtQuick.Effects：

```sh
cmake -S ui-preview -B build/ui-preview -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build/ui-preview
```

其他平台尚未进行实际编译和外观验收。

## 可以体验

- 打开日志：选择内置示例或空白状态；尚未读取磁盘日志。
- 点击日志行；点击字段联动高亮十六进制字节；展开/折叠字段树。
- 搜索、上一处/下一处、循环定位；Ctrl/Cmd+F、F3、Shift+F3。
- 正则过滤；无效正则保留上一视图并显示错误；清空过滤恢复完整日志。
- 全局预览：列出含十六进制字节的行，点击返回并定位。
- 清空并粘贴、复制报文或日志全文；空剪贴板不会清空现有示例。
- 拖动分栏和窗口边缘；标题栏移动/双击最大化；最小窗口 1100×680。
- 外观设置的减少透明度开关；其余业务入口仅提示后续接入。

## 边界

这是 UI 预览，不会执行真实协议解析。只有内置示例的第 3 行配置了字段树；
其他报文和粘贴文本只显示原始字节，不冒充解析结果。样例不保证帧结构或校验合法。
状态栏始终标识交互预览。日志文件、编码切换和真实协议识别留待引擎接入阶段。

毛玻璃使用内置背景图、Qt MultiEffect 模糊与半透明表面，是程序内部的材质效果，
不是 macOS/Windows 桌面穿透模糊。左上角保留项目现有品牌图标。

## 文件

- `PreviewMain.qml`：独立窗口、首页布局、示例交互。
- `GlassButton.qml` / `GlassField.qml` / `Glyph.qml`：本界面的通用视觉控件。
- `PreviewData.js`：明确标记的演示数据和搜索/过滤/高亮逻辑。
- `main.cpp` / `CMakeLists.txt`：独立启动和构建；不链接现有后台。
- `tests/tst_preview.qml`：Qt Quick Test 交互和边界检查。
- `evidence/`：参考图和实际 Qt 渲染截图；验收记录见 `design-qa.md`。

背景图由内置 ImageGen 生成，提示目标为低饱和蓝黑色柔焦纹理，无 UI/文字；
项目图标与主程序统一，来自 `../output/imagegen/log-parser-icon-v1.png`，字体直接复用原 `materialdesignicons-webfont.ttf`。
图标代码按 [Material Design Icons 字体定义](https://github.com/Templarian/MaterialDesign-Webfont/blob/master/css/materialdesignicons.css) 校对。
