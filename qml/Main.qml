import QtQuick
import QtQuick.Controls
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import Qt.labs.platform
import LogAnalyQt

Window {
    id: mainWindow
    width: 1280
    height: 800
    readonly property real toolbarHorizontalChrome:
        mainContentLayout.anchors.leftMargin + mainContentLayout.anchors.rightMargin
        + folderGroupBox.leftPadding + folderGroupBox.rightPadding
        + folderLayout.anchors.leftMargin + folderLayout.anchors.rightMargin
    minimumWidth: Math.max(800, Math.ceil(folderLayout.implicitWidth + toolbarHorizontalChrome))
    visible: true
    title: "多协议解析器 v" + backend.appVersion
    color: theme.background
    flags: Qt.Window | Qt.FramelessWindowHint

    FontLoader {
        id: mdiFont
        source: "qrc:/qt/qml/LogAnalyQt/assets/materialdesignicons-webfont.ttf"
    }

    // Material Design Icons 图标编码映射表
    QtObject {
        id: mdi
        // 基础操作
        readonly property string plus: String.fromCodePoint(0xF0415)
        readonly property string minus: String.fromCodePoint(0xF01D8)
        readonly property string close: String.fromCodePoint(0xF0156)
        readonly property string check: String.fromCodePoint(0xF012C)
        readonly property string alert: String.fromCodePoint(0xF0026)
        readonly property string settings: String.fromCodePoint(0xF0493)
        readonly property string refresh: String.fromCodePoint(0xF0450)
        readonly property string scriptText: String.fromCodePoint(0xF0BC2)
        readonly property string compareHorizontal: String.fromCodePoint(0xF1492)
        readonly property string vectorDifference: String.fromCodePoint(0xF055A)
        readonly property string sourceMerge: String.fromCodePoint(0xF062D)
        // 文件与文件夹
        readonly property string folder: String.fromCodePoint(0xF024B)
        readonly property string folderOpen: String.fromCodePoint(0xF024C)
        // 播放/解析相关
        readonly property string play: String.fromCodePoint(0xF040A)
        readonly property string stop: String.fromCodePoint(0xF04DB)
        readonly property string broom: String.fromCodePoint(0xF00D2)
        // 网络与同步
        readonly property string sync: String.fromCodePoint(0xF04E1)
        // 复制粘贴
        readonly property string contentCopy: String.fromCodePoint(0xF018F)
        readonly property string contentPaste: String.fromCodePoint(0xF0192)
    }

    // 标题栏高度常量
    readonly property int titleBarHeight: 42

    // 设置窗口调色板，使标题栏跟随主题颜色
    palette.window: theme.background
    palette.windowText: theme.text
    palette.base: theme.controlBackground
    palette.text: theme.text
    palette.button: theme.buttonBackground
    palette.buttonText: theme.buttonText
    palette.highlight: theme.accentMuted

    // 窗口初始化完成后设置无边框窗口
    Component.onCompleted: {
        loadMainUiSettings();
        Qt.callLater(ensureMinimumWindowWidth);
        // 延迟调用确保窗口完全创建
        titleBarTimer.start();
    }

    Timer {
        id: titleBarTimer
        interval: 100
        repeat: false
        onTriggered: {
            WindowHelper.setTitleBarColor(mainWindow, theme.background);
            WindowHelper.setDarkTitleBar(mainWindow, theme.isDark);
        }
    }

    // 主题变化时更新标题栏颜色
    Connections {
        target: theme
        function onIsDarkChanged() {
            WindowHelper.setTitleBarColor(mainWindow, theme.background);
            WindowHelper.setDarkTitleBar(mainWindow, theme.isDark);
        }
    }

    readonly property string appVersion: backend.appVersion
    property bool startInDiffMode: false
    property bool enableDiffLogic: true
    property bool isGlobalPreviewMode: false
    // 标记本次更新检查是否为启动自动触发
    property bool isAutoCheck: false

    // 用于精确测量字符宽度的组件
    Text {
        id: charMeasurer
        visible: false
        font.family: PlatformFonts.monospaceFamily
        text: "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    }
    readonly property real charWidth: charMeasurer.contentWidth / charMeasurer.text.length

    // 全局预览列宽
    property real colWidthTime: 120
    property real colWidthProt: 100
    property real colWidthAddr: 120
    property real colWidthDir: 60
    property real colWidthSeq: 80
    property bool mainUiSettingsReady: false

    Timer {
        id: windowGeometrySaveTimer
        interval: 300
        repeat: false
        onTriggered: saveWindowGeometry()
    }

    function saveWindowGeometry() {
        saveMainUiSetting("window/x", mainWindow.x);
        saveMainUiSetting("window/y", mainWindow.y);
        saveMainUiSetting("window/width", mainWindow.width);
        saveMainUiSetting("window/height", mainWindow.height);
    }

    function requestSaveWindowGeometry() {
        if (!mainUiSettingsReady)
            return;
        windowGeometrySaveTimer.restart();
    }

    function ensureMinimumWindowWidth() {
        if (mainWindow.width < mainWindow.minimumWidth)
            mainWindow.width = Math.ceil(mainWindow.minimumWidth);
    }

    function loadMainUiSettings() {
        var noSavedPos = -99999;
        var savedX = Number(backend.getUiSetting("window/x", noSavedPos));
        var savedY = Number(backend.getUiSetting("window/y", noSavedPos));
        var savedWidth = Number(backend.getUiSetting("window/width", mainWindow.width));
        mainWindow.width = Math.max(savedWidth, mainWindow.minimumWidth);
        mainWindow.height = Number(backend.getUiSetting("window/height", mainWindow.height));

        // 校验保存的位置是否在任意已连接屏幕的可见范围内
        var posValid = false;
        if (savedX !== noSavedPos && savedY !== noSavedPos) {
            var screens = Qt.application.screens;
            for (var s = 0; s < screens.length; s++) {
                var sc = screens[s];
                // 窗口左上角至少有一部分（100x50）在屏幕内即认为可见
                if (savedX + mainWindow.width  > sc.virtualX + 100 &&
                    savedX                     < sc.virtualX + sc.width  - 100 &&
                    savedY + mainWindow.height  > sc.virtualY + 50 &&
                    savedY                     < sc.virtualY + sc.height - 50) {
                    posValid = true;
                    break;
                }
            }
        }

        if (posValid) {
            mainWindow.x = savedX;
            mainWindow.y = savedY;
        } else {
            // 没有有效位置时，居中于主屏幕
            var primary = Qt.application.screens[0];
            mainWindow.x = primary.virtualX + Math.round((primary.width  - mainWindow.width)  / 2);
            mainWindow.y = primary.virtualY + Math.round((primary.height - mainWindow.height) / 2);
        }

        colWidthTime = Number(backend.getUiSetting("globalPreview/colWidthTime", colWidthTime));
        colWidthProt = Number(backend.getUiSetting("globalPreview/colWidthProt", colWidthProt));
        colWidthAddr = Number(backend.getUiSetting("globalPreview/colWidthAddr", colWidthAddr));
        colWidthDir = Number(backend.getUiSetting("globalPreview/colWidthDir", colWidthDir));
        colWidthSeq = Number(backend.getUiSetting("globalPreview/colWidthSeq", colWidthSeq));

        var savedEncoding = String(backend.getUiSetting("encoding/current", "auto"));
        for (var i = 0; i < encodingCombo.model.length; i++) {
            if (encodingCombo.model[i] === savedEncoding) {
                encodingCombo.currentIndex = i;
                break;
            }
        }
        mainUiSettingsReady = true;
    }

    function saveMainUiSetting(key, value) {
        if (!mainUiSettingsReady)
            return;
        backend.setUiSetting(key, value);
    }

    onColWidthTimeChanged: saveMainUiSetting("globalPreview/colWidthTime", colWidthTime)
    onColWidthProtChanged: saveMainUiSetting("globalPreview/colWidthProt", colWidthProt)
    onColWidthAddrChanged: saveMainUiSetting("globalPreview/colWidthAddr", colWidthAddr)
    onColWidthDirChanged: saveMainUiSetting("globalPreview/colWidthDir", colWidthDir)
    onColWidthSeqChanged: saveMainUiSetting("globalPreview/colWidthSeq", colWidthSeq)
    onXChanged: requestSaveWindowGeometry()
    onYChanged: requestSaveWindowGeometry()
    onWidthChanged: requestSaveWindowGeometry()
    onHeightChanged: requestSaveWindowGeometry()
    onMinimumWidthChanged: ensureMinimumWindowWidth()
    onClosing: saveWindowGeometry()

    // 智能选择十六进制字节串函数 - 基于行的智能提取
    function selectHexString(textArea) {
        var text = textArea.text;
        var pos = textArea.cursorPosition;

        if (text.length === 0 || pos < 0 || pos > text.length) {
            return false;
        }

        // 找到光标所在行的起始和结束位置
        var lineStart = pos;
        while (lineStart > 0 && text.charAt(lineStart - 1) !== '\n') {
            lineStart--;
        }

        var lineEnd = pos;
        while (lineEnd < text.length && text.charAt(lineEnd) !== '\n') {
            lineEnd++;
        }

        if (lineStart >= lineEnd) {
            return false;
        }

        var line = text.substring(lineStart, lineEnd);

        // 使用正则表达式从该行中提取所有可能的16进制字节串
        // 匹配连续的16进制字符和空格组合
        var hexPattern = /[0-9A-Fa-f]+(?:[ \t]+[0-9A-Fa-f]+)*/g;
        var matches = [];
        var match;

        while ((match = hexPattern.exec(line)) !== null) {
            var hexStr = match[0];
            var hexOnly = hexStr.replace(/[ \t]/g, '');

            // 检查是否为有效的十六进制字节串（每2个字符为1个字节）
            if (hexOnly.length >= 2 && hexOnly.length % 2 === 0) {
                matches.push({
                    text: hexStr,
                    start: lineStart + match.index,
                    end: lineStart + match.index + hexStr.length,
                    hexOnly: hexOnly
                });
            }
        }

        // 如果没有找到符合条件的匹配，返回 false
        if (matches.length === 0) {
            return false;
        }

        // 找到包含光标位置的匹配项，或者距离光标最近的匹配项
        var bestMatch = null;
        var minDistance = Number.MAX_VALUE;

        for (var i = 0; i < matches.length; i++) {
            var m = matches[i];
            // 如果光标在匹配范围内，直接选中
            if (pos >= m.start && pos <= m.end) {
                bestMatch = m;
                break;
            }

            // 计算距离
            var distance = 0;
            if (pos < m.start) {
                distance = m.start - pos;
            } else if (pos > m.end) {
                distance = pos - m.end;
            }

            if (distance < minDistance) {
                minDistance = distance;
                bestMatch = m;
            }
        }

        if (bestMatch) {
            textArea.select(bestMatch.start, bestMatch.end);
            return true;
        }

        return false;
    }

    LogBackend {
        id: backend
    }

    Shortcut {
        sequence: "Ctrl+Shift+S"
        onActivated: protocolSyncDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+Shift+F"
        onActivated: smartFramingDialog.open()
    }

    // 接收698地址信号，自动填充底部地址输入框
    Connections {
        target: backend
        function onUpdate698Address(physicalAddress, logicAddress) {
            if (physicalAddress !== "") {
                addrEdit698.text = physicalAddress;
            }
            if (logicAddress !== "") {
                logicAddrEdit698.text = logicAddress;
            }
        }
    }

    Connections {
        target: backend
        function onSecurityControlStateChanged() {
            if (backend.securityControlActive) {
                if (!securityControlDialog.visible)
                    securityControlDialog.open();
            } else if (securityControlDialog.visible) {
                securityControlDialog.close();
            }
        }
    }

    Item {
        id: theme
        visible: false // Only used for properties, not rendering
        // 启动时跟随系统深色/浅色模式，后续可手动切换覆盖
        property bool followSystem: true  // 是否跟随系统主题
        property bool isDark: followSystem ? (Qt.styleHints.colorScheme === Qt.ColorScheme.Dark) : false
        property bool isManualMode: true
        property bool isDiffMode: mainWindow.startInDiffMode

        // 监听系统主题变化，当 followSystem 为 true 时自动更新
        Connections {
            target: Qt.styleHints
            function onColorSchemeChanged() {
                if (theme.followSystem) {
                    theme.isDark = (Qt.styleHints.colorScheme === Qt.ColorScheme.Dark);
                }
            }
        }

        // 现代深色主题配色 - 采用深蓝灰色调，带有微妙的蓝紫色层次
        property color background: isDark ? "#0d1117" : "#f5f5f5"  // GitHub Dark 风格深色背景
        Behavior on background {
            ThemeTransition {}
        }

        property color text: isDark ? "#e6edf3" : "#333333"  // 柔和的白色文字
        Behavior on text {
            ThemeTransition {}
        }

        property color subText: isDark ? "#8b949e" : "#666666"  // 淡蓝灰色次要文字
        Behavior on subText {
            ThemeTransition {}
        }

        property color controlBackground: isDark ? "#161b22" : "#ffffff"  // 略带蓝调的控件背景
        Behavior on controlBackground {
            ThemeTransition {}
        }

        property color controlBorder: isDark ? "#30363d" : "#cccccc"  // 柔和的边框色
        Behavior on controlBorder {
            ThemeTransition {}
        }

        property color groupBorder: isDark ? "#30363d" : "#d0d0d0"  // 组边框
        Behavior on groupBorder {
            ThemeTransition {}
        }

        property color readOnlyBackground: isDark ? "#0d1117" : "#ffffff"  // 只读背景
        Behavior on readOnlyBackground {
            ThemeTransition {}
        }

        property color splitHandle: isDark ? "#30363d" : "#cccccc"  // 分割条
        Behavior on splitHandle {
            ThemeTransition {}
        }

        property color splitHandleHover: isDark ? "#484f58" : "#aaaaaa"  // 分割条悬停
        Behavior on splitHandleHover {
            ThemeTransition {}
        }

        property color splitHandlePressed: isDark ? "#6e7681" : "#888888"  // 分割条按下
        Behavior on splitHandlePressed {
            ThemeTransition {}
        }

        property color placeholderText: isDark ? "#6e7681" : "#888888"  // 占位符文字
        Behavior on placeholderText {
            ThemeTransition {}
        }

        property color buttonBackground: isDark ? "#21262d" : "#f0f0f0"  // 按钮背景
        Behavior on buttonBackground {
            ThemeTransition {}
        }

        property color buttonHover: isDark ? "#30363d" : "#e0e0e0"  // 按钮悬停
        Behavior on buttonHover {
            ThemeTransition {}
        }

        property color buttonPressed: accentMuted  // 按钮按下统一使用强调蓝
        Behavior on buttonPressed {
            ThemeTransition {}
        }

        property color buttonText: isDark ? "#e6edf3" : "#333333"  // 按钮文字
        Behavior on buttonText {
            ThemeTransition {}
        }

        property color tableHighlight: isDark ? "#1f3a5f" : "#e6f3ff"  // 表格高亮 - 蓝色调
        Behavior on tableHighlight {
            ThemeTransition {}
        }

        // 新增强调色属性，用于更丰富的视觉层次
        property color accent: isDark ? "#58a6ff" : "#2196F3"  // 主强调色 - 亮蓝色
        Behavior on accent {
            ThemeTransition {}
        }

        property color accentMuted: isDark ? "#388bfd" : "#1976D2"  // 次强调色
        Behavior on accentMuted {
            ThemeTransition {}
        }

        property color surface: isDark ? "#1c2128" : "#fafafa"  // 卡片/面板表面色
        Behavior on surface {
            ThemeTransition {}
        }

        property color surfaceHover: isDark ? "#262c36" : "#f0f0f0"  // 表面悬停色
        Behavior on surfaceHover {
            ThemeTransition {}
        }

        property color success: isDark ? "#3fb950" : "#4caf50"  // 成功色 - 绿色
        Behavior on success {
            ThemeTransition {}
        }

        property color warning: isDark ? "#d29922" : "#ff9800"  // 警告色 - 橙色
        Behavior on warning {
            ThemeTransition {}
        }

        property color error: isDark ? "#f85149" : "#f44336"  // 错误色 - 红色
        Behavior on error {
            ThemeTransition {}
        }

        property color rowAlt: isDark ? "#161b22" : "#f9f9f9"  // 交替行背景
        Behavior on rowAlt {
            ThemeTransition {}
        }
    }

    // 定义统一的颜色过渡动画
    component ThemeTransition: ColorAnimation {
        duration: 400
        easing.type: Easing.InOutQuad
    }

    FolderDialog {
        id: folderDialog
        title: "选择日志文件夹"
        onAccepted: {
            var path = folderDialog.folder.toString();
            // Remove file:/// prefix if present
            if (path.startsWith("file:///")) {
                path = path.substring(8);
            } else if (path.startsWith("file://")) {
                path = path.substring(7);
            }
            folderPathField.text = path;
            backend.openFolder(folderDialog.folder);
        }
    }

    // Define a common style for GroupBox to look cleaner
    component StyledGroupBox: GroupBox {
        id: control
        background: Rectangle {
            color: "transparent"
            border.color: theme.groupBorder
            radius: 4
        }
        label: Label {
            x: 8
            y: -height / 2
            text: control.title
            font.bold: true
            color: theme.text
            background: Rectangle {
                color: theme.background
                height: 2
                width: parent.width
            }
            padding: 4
        }
    }

    component StyledComboBox: ComboBox {
        id: control

        delegate: ItemDelegate {
            width: control.width
            contentItem: Text {
                text: modelData
                color: theme.text
                font: control.font
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: control.highlightedIndex === index ? theme.splitHandleHover : "transparent"
            }
            highlighted: control.highlightedIndex === index
        }

        indicator: Canvas {
            id: canvas
            x: control.width - width - control.rightPadding
            y: control.topPadding + (control.availableHeight - height) / 2
            width: 12
            height: 8
            contextType: "2d"

            Connections {
                target: control
                function onPressedChanged() {
                    canvas.requestPaint();
                }
            }

            Connections {
                target: theme
                function onIsDarkChanged() {
                    canvas.requestPaint();
                }
            }

            onPaint: {
                context.reset();
                context.moveTo(0, 0);
                context.lineTo(width, 0);
                context.lineTo(width / 2, height);
                context.closePath();
                context.fillStyle = theme.text;
                context.fill();
            }
        }

        contentItem: Text {
            leftPadding: 10
            rightPadding: control.indicator.width + control.spacing
            text: control.displayText
            font: control.font
            color: theme.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            implicitWidth: 100
            implicitHeight: 30
            color: theme.controlBackground
            border.color: theme.controlBorder
            border.width: control.visualFocus ? 2 : 1
            radius: 4
        }

        popup: Popup {
            y: control.height - 1
            width: control.width
            implicitHeight: Math.min(contentItem.implicitHeight, 400)
            padding: 1

            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
            }
            background: Rectangle {
                border.color: theme.controlBorder
                radius: 4
                color: theme.controlBackground
            }
        }
    }

    component StyledSpinBox: SpinBox {
        id: control
        editable: true
        implicitHeight: 30

        // Explicitly set palette to ensure native style doesn't force white background
        palette.base: theme.controlBackground
        palette.text: theme.text
        palette.button: theme.controlBackground
        palette.window: theme.background

        // Reserve space for buttons so TextInput doesn't overlap them
        topPadding: 0
        bottomPadding: 0
        leftPadding: 30
        rightPadding: 30

        contentItem: TextInput {
            z: 2
            text: control.textFromValue(control.value, control.locale)
            font: control.font
            color: theme.text
            selectionColor: theme.splitHandleHover
            selectedTextColor: theme.text
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            readOnly: !control.editable
            validator: control.validator
            inputMethodHints: Qt.ImhDigitsOnly

            onTextEdited: {
                control.value = control.valueFromText(text, control.locale);
            }
        }

        up.indicator: Rectangle {
            x: control.mirrored ? 0 : parent.width - width
            height: parent.height
            width: 30
            implicitWidth: 30
            implicitHeight: 30
            color: control.up.pressed ? theme.splitHandlePressed : (control.up.hovered ? theme.splitHandleHover : "transparent")

            Text {
                text: "+"
                font.pixelSize: control.font.pixelSize * 1.5
                color: theme.text
                anchors.centerIn: parent
            }
        }

        down.indicator: Rectangle {
            x: control.mirrored ? parent.width - width : 0
            height: parent.height
            width: 30
            implicitWidth: 30
            implicitHeight: 30
            color: control.down.pressed ? theme.splitHandlePressed : (control.down.hovered ? theme.splitHandleHover : "transparent")

            Text {
                text: "-"
                font.pixelSize: control.font.pixelSize * 1.5
                color: theme.text
                anchors.centerIn: parent
            }
        }

        background: Rectangle {
            implicitWidth: 100
            implicitHeight: 30
            border.color: theme.controlBorder
            color: theme.controlBackground
            radius: 4
        }
    }

    component StyledButton: Button {
        id: control
        property string iconChar: ""
        property int iconSize: 16

        leftPadding: 10
        rightPadding: 10
        topPadding: 6
        bottomPadding: 6

        contentItem: Item {
            implicitWidth: contentRow.implicitWidth
            implicitHeight: contentRow.implicitHeight

            Row {
                id: contentRow
                spacing: 6
                anchors.centerIn: parent

                Text {
                    text: control.iconChar
                    font.family: mdiFont.name !== "" ? mdiFont.name : "Material Design Icons"
                    font.pixelSize: control.iconSize
                    color: control.highlighted ? theme.background : theme.buttonText
                    visible: text !== ""
                    verticalAlignment: Text.AlignVCenter
                }

                Text {
                    text: control.text
                    font: control.font
                    opacity: enabled ? 1.0 : 0.3
                    color: control.highlighted ? theme.background : theme.buttonText
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }

        background: Rectangle {
            implicitWidth: 100
            implicitHeight: 30
            opacity: enabled ? 1 : 0.3
            color: control.highlighted ? theme.text : (control.down ? theme.buttonPressed : (control.hovered ? theme.buttonHover : theme.buttonBackground))
            border.color: theme.controlBorder
            border.width: 1
            radius: 4
        }
    }

    component ParsePane: ColumnLayout {
        property string titlePrefix: ""
        property string settingsPrefix: ""
        property var resultModel
        property int targetModelId
        property bool showInput: true
        property alias listView: resultListView
        property alias manualParseInput: manualParseInput
        // 是否锁定列宽（禁用自动调整）
        property bool autoFitLocked: false
        property bool paneSettingsReady: false

        function autoFitCols() {
            cols.autoFit();
        }

        function clearInput() {
            manualParseInput.text = "";
        }

        function paneSettingKey(suffix) {
            if (settingsPrefix === "")
                return "";
            return settingsPrefix + "/" + suffix;
        }

        function savePaneSetting(suffix, value) {
            if (!paneSettingsReady)
                return;
            var key = paneSettingKey(suffix);
            if (key !== "")
                backend.setUiSetting(key, value);
        }

        function loadPaneSettings() {
            if (settingsPrefix === "") {
                paneSettingsReady = true;
                return;
            }

            var savedProtocolIndex = Number(backend.getUiSetting(paneSettingKey("presetProtocolIndex"), 0));
            if (!isNaN(savedProtocolIndex) && savedProtocolIndex >= 0 && savedProtocolIndex < presetCategoryCombo.model.length)
                presetCategoryCombo.currentIndex = savedProtocolIndex;

            autoFitLocked = !!backend.getUiSetting(paneSettingKey("autoFitLocked"), false);

            var savedR1 = Number(backend.getUiSetting(paneSettingKey("colsR1"), cols.r1));
            var savedR2 = Number(backend.getUiSetting(paneSettingKey("colsR2"), cols.r2));
            if (!isNaN(savedR1) && !isNaN(savedR2) && savedR1 > 0.05 && savedR2 > 0.05 && (savedR1 + savedR2) < 0.95) {
                cols.r1 = savedR1;
                cols.r2 = savedR2;
            }

            presetCategoryCombo.loadPresetMessages();
            paneSettingsReady = true;
        }

        onAutoFitLockedChanged: savePaneSetting("autoFitLocked", autoFitLocked)

        Component.onCompleted: {
            loadPaneSettings();
        }

        // 处理高亮信号
        Connections {
            target: backend
            enabled: showInput
            function onHighlightRange(start, end, modelId) {
                if (modelId === targetModelId) {
                    manualParseInput.select(start, end);
                    manualParseInput.forceActiveFocus();
                }
            }
        }

        spacing: 8

        // 预置报文选择区 和 手动报文解析区 并排布局
        RowLayout {
            visible: showInput
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            Layout.maximumHeight: 140
            spacing: 8

            // 左侧：预置报文选择区
            StyledGroupBox {
                title: titlePrefix + "预置报文选择区"
                Layout.preferredWidth: 1
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 25
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 5

                    // 协议选择行
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            text: "协议"
                            color: theme.text
                            font.pixelSize: 12
                            Layout.preferredWidth: 30
                        }

                        StyledComboBox {
                            id: presetCategoryCombo
                            Layout.fillWidth: true
                            model: ["DL/T 698", "IEC 101", "IEC 104", "应用层消息", "模块消息", "Q/GDW 376.2", "DL/T 645", "南网计量上行规约"]
                            currentIndex: 0

                            // 加载对应协议的预置报文列表
                            function loadPresetMessages() {
                                var list = backend.getPresetList(currentText);
                                if (list.length > 0) {
                                    presetMessageCombo.model = ["请选择预置报文..."].concat(list);
                                } else {
                                    presetMessageCombo.model = ["请选择预置报文..."];
                                }
                                presetMessageCombo.currentIndex = 0;
                            }

                            onActivated: {
                                loadPresetMessages();
                                savePaneSetting("presetProtocolIndex", currentIndex);
                            }

                            Component.onCompleted: {
                                loadPresetMessages();
                            }
                        }
                    }

                    // 报文选择行
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            text: "报文"
                            color: theme.text
                            font.pixelSize: 12
                            Layout.preferredWidth: 30
                        }

                        StyledComboBox {
                            id: presetMessageCombo
                            Layout.fillWidth: true
                            model: ["请选择预置报文..."]
                            currentIndex: 0

                            onActivated: {
                                if (currentIndex > 0) {
                                    // 获取选中的报文内容
                                    var content = backend.getPresetContent(presetCategoryCombo.currentText, currentText);
                                    if (content !== "") {
                                        // 填充到手动解析输入框
                                        manualParseInput.text = content;
                                        // 自动触发解析
                                        performParse(content, targetModelId);
                                    }
                                }
                            }
                        }
                    }

                    // 管理按钮行
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Item {
                            Layout.preferredWidth: 30
                        }  // 与上面标签对齐

                        StyledButton {
                            text: "管理预置报文"
                            iconChar: mdi.settings
                            Layout.fillWidth: true
                            onClicked: {
                                // 同步管理窗口的协议选择
                                presetManagerWindow.currentProtocol = presetCategoryCombo.currentText;
                                // 找到对应的索引
                                for (var i = 0; i < managerProtocolCombo.model.length; i++) {
                                    if (managerProtocolCombo.model[i] === presetCategoryCombo.currentText) {
                                        managerProtocolCombo.currentIndex = i;
                                        break;
                                    }
                                }
                                presetManagerWindow.show();
                            }
                        }

                        StyledButton {
                            text: "刷新"
                            Layout.preferredWidth: 50
                            ToolTip.visible: hovered
                            ToolTip.text: "刷新预置报文列表"
                            onClicked: {
                                presetCategoryCombo.loadPresetMessages();
                            }
                        }
                    }
                }
            }

            // 右侧：手动报文解析
            StyledGroupBox {
                title: titlePrefix + "手动报文解析"
                Layout.preferredWidth: 3
                Layout.fillWidth: true
                Layout.fillHeight: true

                RowLayout {
                    anchors.fill: parent
                    anchors.topMargin: 25
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    anchors.bottomMargin: 10
                    spacing: 5

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        TextArea {
                            id: manualParseInput
                            placeholderText: "在此输入报文..."
                            placeholderTextColor: theme.placeholderText
                            font.family: PlatformFonts.monospaceFamily
                            color: theme.text
                            wrapMode: TextArea.Wrap
                            selectByMouse: true
                            background: Rectangle {
                                color: theme.controlBackground
                                border.color: theme.controlBorder
                                radius: 2
                            }

                            // 使用 TapHandler 处理双击事件
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onDoubleTapped: function (eventPoint, button) {
                                    var pos = manualParseInput.positionAt(eventPoint.position.x, eventPoint.position.y);
                                    manualParseInput.cursorPosition = pos;

                                    if (selectHexString(manualParseInput)) {
                                        // 选中成功后自动解析选中的文本
                                        performParse(manualParseInput.selectedText, targetModelId);
                                    }
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        spacing: 5
                        StyledButton {
                            text: "清空并粘贴"
                            iconChar: mdi.contentPaste
                            onClicked: {
                                manualParseInput.text = "";
                                manualParseInput.paste();
                                performParse(manualParseInput.text, targetModelId);
                            }
                        }
                        StyledButton {
                            text: "解析"
                            iconChar: mdi.play
                            onClicked: {
                                performParse(manualParseInput.text, targetModelId);
                            }
                        }
                        StyledButton {
                            text: autoFitLocked ? "🔒 列宽已锁定" : "自适应列宽"
                            iconChar: autoFitLocked ? "" : mdi.refresh
                            onClicked: {
                                if (autoFitLocked) {
                                    // 解锁并执行自适应
                                    autoFitLocked = false;
                                    cols.autoFit();
                                } else {
                                    // 锁定当前列宽
                                    autoFitLocked = true;
                                }
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: autoFitLocked ? "点击解锁并自适应列宽" : "点击锁定当前列宽"
                        }
                    }
                }
            }
        }

        // Parse Results
        StyledGroupBox {
            title: titlePrefix + "解析结果"
            Layout.fillWidth: true
            Layout.fillHeight: true

            QtObject {
                id: cols
                property real r1: 0.4
                property real r2: 0.3
                onR1Changed: savePaneSetting("colsR1", r1)
                onR2Changed: savePaneSetting("colsR2", r2)
                // r3 is remainder

                property real minTotal: 400 // Reduced min width for split view
                property real availableW: Math.max(headerContainer.width, minTotal)

                property int w1: Math.floor(availableW * r1)
                property int w2: Math.floor(availableW * r2)
                property int w3: availableW - w1 - w2
                property int total: availableW

                function autoFit() {
                    // 使用动态测量的字符宽度，20px 为缩进宽度
                    if (resultModel && resultModel.getColumnWidths) {
                        var widths = resultModel.getColumnWidths(mainWindow.charWidth, 20);
                        if (widths.length >= 2) {
                            var w1 = widths[0];
                            var w2 = widths[1];

                            var total = cols.availableW;
                            if (total <= 0)
                                return;

                            var newR1 = w1 / total;
                            var newR2 = w2 / total;

                            // Constraints
                            if (newR1 < 0.1)
                                newR1 = 0.1;
                            if (newR1 > 0.6)
                                newR1 = 0.6;

                            if (newR2 < 0.1)
                                newR2 = 0.1;
                            if (newR2 > 0.8)
                                newR2 = 0.8;

                            if (newR1 + newR2 > 0.9) {
                                var factor = 0.9 / (newR1 + newR2);
                                newR1 *= factor;
                                newR2 *= factor;
                            }

                            cols.r1 = newR1;
                            cols.r2 = newR2;
                        }
                    }
                }
            }

            // Header Container
            Item {
                id: headerContainer
                anchors.top: parent.top
                anchors.topMargin: 25
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: parent.right
                anchors.rightMargin: 10
                height: 30
                clip: true
                z: 2

                Row {
                    x: -resultListView.contentX
                    height: parent.height

                    // Col 1
                    Rectangle {
                        width: cols.w1
                        height: parent.height
                        color: theme.controlBackground
                        border.color: theme.controlBorder
                        Text {
                            anchors.centerIn: parent
                            text: "域/位域"
                            font.bold: true
                            color: theme.text
                        }
                        // Resizer
                        Rectangle {
                            width: 4
                            height: parent.height
                            anchors.right: parent.right
                            color: Qt.rgba(0, 0, 0, 0)
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.SplitHCursor
                                property point lastPos
                                onPressed: mouse => {
                                    lastPos = mapToItem(headerContainer, mouse.x, mouse.y);
                                }
                                onPositionChanged: mouse => {
                                    if (pressed) {
                                        var curPos = mapToItem(headerContainer, mouse.x, mouse.y);
                                        var delta = curPos.x - lastPos.x;
                                        var deltaR = delta / cols.availableW;
                                        var minR = 50 / cols.availableW;

                                        if (cols.r1 + deltaR > minR && cols.r2 - deltaR > minR) {
                                            cols.r1 += deltaR;
                                            cols.r2 -= deltaR;
                                            lastPos = curPos;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Col 2
                    Rectangle {
                        width: cols.w2
                        height: parent.height
                        color: theme.controlBackground
                        border.color: theme.controlBorder
                        Text {
                            anchors.centerIn: parent
                            text: "数据"
                            font.bold: true
                            color: theme.text
                        }
                        // Resizer
                        Rectangle {
                            width: 4
                            height: parent.height
                            anchors.right: parent.right
                            color: Qt.rgba(0, 0, 0, 0)
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.SplitHCursor
                                property point lastPos
                                onPressed: mouse => {
                                    lastPos = mapToItem(headerContainer, mouse.x, mouse.y);
                                }
                                onPositionChanged: mouse => {
                                    if (pressed) {
                                        var curPos = mapToItem(headerContainer, mouse.x, mouse.y);
                                        var delta = curPos.x - lastPos.x;
                                        var deltaR = delta / cols.availableW;
                                        var minR = 50 / cols.availableW;
                                        var currentR3 = 1.0 - cols.r1 - cols.r2;

                                        if (cols.r2 + deltaR > minR && currentR3 - deltaR > minR) {
                                            cols.r2 += deltaR;
                                            lastPos = curPos;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Col 3
                    Rectangle {
                        width: cols.w3
                        height: parent.height
                        color: theme.controlBackground
                        border.color: theme.controlBorder
                        Text {
                            anchors.centerIn: parent
                            text: "说明"
                            font.bold: true
                            color: theme.text
                        }
                    }
                }
            }

            ListView {
                id: resultListView
                anchors.top: headerContainer.bottom
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                clip: true

                contentWidth: cols.total
                flickableDirection: Flickable.HorizontalAndVerticalFlick

                model: resultModel

                delegate: Rectangle {
                    width: cols.total
                    height: model.isFrameSeparator ? 20 : 30
                    color: model.isFrameSeparator ? theme.tableHighlight : (model.isDiff ? Qt.rgba(theme.error.r, theme.error.g, theme.error.b, theme.isDark ? 0.28 : 0.20) : (model.highlighted ? theme.tableHighlight : (index % 2 == 0 ? Qt.rgba(0, 0, 0, 0) : theme.rowAlt)))

                    // 分隔符显示
                    Item {
                        anchors.fill: parent
                        visible: model.isFrameSeparator

                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width - 40
                            height: 2
                            color: theme.accent
                            opacity: 0.5
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            color: theme.background
                            width: separatorText.width + 10
                            height: separatorText.height + 4

                            Text {
                                id: separatorText
                                anchors.centerIn: parent
                                text: model.name
                                font.family: PlatformFonts.monospaceFamily
                                font.bold: true
                                font.pixelSize: 11
                                color: theme.accent
                            }
                        }
                    }

                    // 常规字段显示
                    MouseArea {
                        anchors.fill: parent
                        visible: !model.isFrameSeparator
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: mouse => {
                            if (mouse.button === Qt.RightButton) {
                                rowContextMenu.rowName = model.name;
                                rowContextMenu.rowValue = model.value;
                                rowContextMenu.rowDescription = model.description;
                                rowContextMenu.popup();
                            } else {
                                var link = "highlight:" + model.offset + "," + model.length;
                                backend.handleLink(link, targetModelId);
                            }
                        }
                        onDoubleClicked: {
                            if (model.name === "明文数据单元") {
                                backend.parsePopupApdu(model.value);
                                popupWindow.show();
                                popupPane.autoFitCols();
                            }
                        }
                    }

                    QQC2.Menu {
                        id: rowContextMenu
                        property string rowName: ""
                        property string rowValue: ""
                        property string rowDescription: ""

                        QQC2.MenuItem {
                            text: "复制域/位域"
                            onTriggered: backend.copyToClipboard(rowContextMenu.rowName)
                        }
                        QQC2.MenuItem {
                            text: "复制数据"
                            onTriggered: backend.copyToClipboard(rowContextMenu.rowValue)
                        }
                        QQC2.MenuItem {
                            text: "复制说明"
                            onTriggered: backend.copyToClipboard(rowContextMenu.rowDescription)
                        }
                    }

                    Row {
                        height: parent.height
                        visible: !model.isFrameSeparator

                        // Field Name
                        Item {
                            width: cols.w1
                            height: parent.height

                            Text {
                                id: expander
                                x: 5 + (model.indent * 20)
                                width: 16
                                height: parent.height
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: Text.AlignHCenter
                                text: model.hasChildren ? (model.expanded ? "▼" : "▶") : ""
                                color: theme.subText
                                font.pixelSize: 10

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: model.hasChildren
                                    cursorShape: model.hasChildren ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: resultModel.toggleExpand(index)
                                }
                            }

                            Rectangle {
                                id: subtreeToggle
                                anchors.left: expander.right
                                anchors.leftMargin: model.hasChildren ? 2 : 0
                                anchors.verticalCenter: parent.verticalCenter
                                width: model.hasChildren ? 8 : 0
                                height: model.hasChildren ? 8 : 0
                                radius: width / 2
                                color: model.subtreeState === 0 ? Qt.rgba(0, 0.63, 0, 1) : (model.subtreeState === 2 ? Qt.rgba(0.75, 0, 0, 1) : Qt.rgba(0.55, 0.55, 0.55, 1))

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: model.hasChildren
                                    preventStealing: true
                                    propagateComposedEvents: false
                                    cursorShape: model.hasChildren ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onPressed: function (mouse) {
                                        mouse.accepted = true;
                                    }
                                    onReleased: function (mouse) {
                                        mouse.accepted = true;
                                    }
                                    onClicked: {
                                        resultModel.toggleSubtree(index);
                                    }
                                }
                            }

                            Text {
                                anchors.left: subtreeToggle.right
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.leftMargin: 4
                                verticalAlignment: Text.AlignVCenter
                                text: model.name
                                font.family: PlatformFonts.monospaceFamily
                                color: theme.text
                                elide: Text.ElideRight
                            }

                            Rectangle {
                                width: 1
                                height: parent.height
                                color: theme.controlBorder
                                anchors.right: parent.right
                            }
                        }

                        // Value
                        Item {
                            width: cols.w2
                            height: parent.height

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 5
                                verticalAlignment: Text.AlignVCenter
                                text: model.value
                                font.family: PlatformFonts.monospaceFamily
                                color: theme.text
                                elide: Text.ElideRight
                            }

                            Rectangle {
                                width: 1
                                height: parent.height
                                color: theme.controlBorder
                                anchors.right: parent.right
                            }
                        }

                        // Description
                        Item {
                            width: cols.w3
                            height: parent.height

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 5
                                verticalAlignment: Text.AlignVCenter
                                text: model.description
                                font.family: PlatformFonts.monospaceFamily
                                color: theme.text
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: theme.controlBorder
                        anchors.bottom: parent.bottom
                        visible: true
                    }
                }

                ScrollBar.vertical: ScrollBar {}
                ScrollBar.horizontal: ScrollBar {}
            }
        }
    }

    function performParse(text, modelId) {
        // 先格式化十六进制字符串
        var formattedText = backend.formatHexString(text);

        // 更新输入框显示为格式化后的文本
        if (modelId === 1 && pane1.manualParseInput) {
            pane1.manualParseInput.text = formattedText;
        } else if (modelId === 2 && pane2.manualParseInput) {
            pane2.manualParseInput.text = formattedText;
        }

        // 使用格式化后的文本进行解析
        backend.parseHex(formattedText, encodingCombo.currentText, modelId);

        // 仅在未锁定时自动调整列宽
        if (modelId === 1 && !pane1.autoFitLocked)
            pane1.autoFitCols();
        if (modelId === 2 && !pane2.autoFitLocked)
            pane2.autoFitCols();

        if (theme.isDiffMode && mainWindow.enableDiffLogic) {
            backend.compareModels();
            var firstDiff = backend.findNextDiff(-1);
            if (firstDiff !== -1) {
                pane1.listView.positionViewAtIndex(firstDiff, ListView.Beginning);
                pane2.listView.positionViewAtIndex(firstDiff, ListView.Beginning);
                pane1.listView.currentIndex = firstDiff;
                pane2.listView.currentIndex = firstDiff;
            }
        }
    }

    // 自定义标题栏 (设置高z值确保接收鼠标事件)
    TitleBar {
        id: customTitleBar
        z: 100
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        targetWindow: mainWindow
        title: mainWindow.title
        titleBarColor: theme.background  // 与窗口背景颜色一致
        titleTextColor: theme.text
        buttonHoverColor: theme.buttonHover
        closeButtonHoverColor: theme.error
        isDarkTheme: theme.isDark
        showThemeToggle: true

        onThemeToggleClicked: {
            var nextIsDark = !theme.isDark;
            theme.isDark = nextIsDark;
            theme.followSystem = false;
        }
    }

    // 边缘缩放区域 - 使用 QML 直接实现窗口缩放
    // 左边缘
    MouseArea {
        id: leftResizeArea
        width: 6
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        cursorShape: Qt.SizeHorCursor

        property real startX: 0
        property real startWidth: 0
        property real startWindowX: 0

        onPressed: function (mouse) {
            startX = mouse.x;
            startWidth = mainWindow.width;
            startWindowX = mainWindow.x;
        }
        onPositionChanged: function (mouse) {
            if (pressed) {
                var dx = mouse.x - startX;
                var newWidth = startWidth - dx;
                if (newWidth >= mainWindow.minimumWidth) {
                    mainWindow.width = newWidth;
                    mainWindow.x = startWindowX + dx;
                }
            }
        }
    }

    // 右边缘
    MouseArea {
        id: rightResizeArea
        width: 6
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        cursorShape: Qt.SizeHorCursor

        property real startX: 0
        property real startWidth: 0

        onPressed: function (mouse) {
            startX = mapToGlobal(mouse.x, mouse.y).x;
            startWidth = mainWindow.width;
        }
        onPositionChanged: function (mouse) {
            if (pressed) {
                var currentX = mapToGlobal(mouse.x, mouse.y).x;
                var dx = currentX - startX;
                var newWidth = startWidth + dx;
                if (newWidth >= mainWindow.minimumWidth) {
                    mainWindow.width = newWidth;
                }
            }
        }
    }

    // 下边缘
    MouseArea {
        id: bottomResizeArea
        height: 6
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        cursorShape: Qt.SizeVerCursor

        property real startY: 0
        property real startHeight: 0

        onPressed: function (mouse) {
            startY = mapToGlobal(mouse.x, mouse.y).y;
            startHeight = mainWindow.height;
        }
        onPositionChanged: function (mouse) {
            if (pressed) {
                var currentY = mapToGlobal(mouse.x, mouse.y).y;
                var dy = currentY - startY;
                var newHeight = startHeight + dy;
                if (newHeight >= 600) {
                    mainWindow.height = newHeight;
                }
            }
        }
    }

    // 左下角
    MouseArea {
        width: 6
        height: 6
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        cursorShape: Qt.SizeBDiagCursor

        property point startPos: Qt.point(0, 0)
        property real startWidth: 0
        property real startHeight: 0
        property real startWindowX: 0

        onPressed: function (mouse) {
            startPos = mapToGlobal(mouse.x, mouse.y);
            startWidth = mainWindow.width;
            startHeight = mainWindow.height;
            startWindowX = mainWindow.x;
        }
        onPositionChanged: function (mouse) {
            if (pressed) {
                var currentPos = mapToGlobal(mouse.x, mouse.y);
                var dx = currentPos.x - startPos.x;
                var dy = currentPos.y - startPos.y;

                var newWidth = startWidth - dx;
                if (newWidth >= mainWindow.minimumWidth) {
                    mainWindow.width = newWidth;
                    mainWindow.x = startWindowX + dx;
                }

                var newHeight = startHeight + dy;
                if (newHeight >= 600) {
                    mainWindow.height = newHeight;
                }
            }
        }
    }

    // 右下角
    MouseArea {
        width: 6
        height: 6
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        cursorShape: Qt.SizeFDiagCursor

        property point startPos: Qt.point(0, 0)
        property real startWidth: 0
        property real startHeight: 0

        onPressed: function (mouse) {
            startPos = mapToGlobal(mouse.x, mouse.y);
            startWidth = mainWindow.width;
            startHeight = mainWindow.height;
        }
        onPositionChanged: function (mouse) {
            if (pressed) {
                var currentPos = mapToGlobal(mouse.x, mouse.y);
                var dx = currentPos.x - startPos.x;
                var dy = currentPos.y - startPos.y;

                var newWidth = startWidth + dx;
                if (newWidth >= mainWindow.minimumWidth) {
                    mainWindow.width = newWidth;
                }

                var newHeight = startHeight + dy;
                if (newHeight >= 600) {
                    mainWindow.height = newHeight;
                }
            }
        }
    }

    ColumnLayout {
        id: mainContentLayout
        anchors.top: customTitleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        spacing: 8

        // --- Top Section: Folder Selection ---
        StyledGroupBox {
            id: folderGroupBox
            title: "文件夹选择 (支持 .txt 和 .log 文件)"
            Layout.fillWidth: true
            implicitHeight: folderLayout.implicitHeight + 40

            RowLayout {
                id: folderLayout
                anchors.fill: parent
                anchors.topMargin: 25
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.bottomMargin: 10
                spacing: 10

                StyledButton {
                    text: "选择文件夹"
                    iconChar: mdi.folderOpen
                    onClicked: folderDialog.open()
                }

                TextField {
                    id: folderPathField
                    Layout.fillWidth: true
                    Layout.minimumWidth: 80
                    Layout.preferredWidth: 80
                    placeholderText: "请选择日志文件夹..."
                    placeholderTextColor: theme.placeholderText
                    readOnly: true
                    background: Rectangle {
                        color: theme.readOnlyBackground
                        radius: 4
                        border.color: theme.controlBorder
                    }
                    color: theme.text
                }

                Label {
                    text: "已加载 " + backend.fileList.length + " 个文件"
                    color: theme.subText
                }

                StyledButton {
                        id: manualModeButton
                        text: "手动解析模式"
                        iconChar: mdi.scriptText
                        highlighted: theme.isManualMode
                        onClicked: {
                            theme.isManualMode = !theme.isManualMode;
                            if (!theme.isManualMode) {
                                backend.clearAllParses();
                                pane1.clearInput();
                                pane2.clearInput();
                                if (theme.isDiffMode) {
                                    theme.isDiffMode = false;
                                    backend.clearDiffs();
                                }
                            }
                        }
                    }

                    StyledButton {
                        id: diffModeButton
                        text: (theme.isDiffMode && mainWindow.enableDiffLogic) ? "退出对比" : "差异对比解析"
                        iconChar: mdi.vectorDifference
                        visible: theme.isManualMode
                        highlighted: theme.isDiffMode && mainWindow.enableDiffLogic
                        onClicked: {
                            if (theme.isDiffMode && mainWindow.enableDiffLogic) {
                                theme.isDiffMode = false;
                                backend.clearDiffs();
                            } else {
                                theme.isDiffMode = true;
                                mainWindow.enableDiffLogic = true;
                                // 重新触发解析以显示对比结果
                                if (pane1.manualParseInput.text !== "")
                                    performParse(pane1.manualParseInput.text, 1);
                                if (pane2.manualParseInput.text !== "")
                                    performParse(pane2.manualParseInput.text, 2);
                            }
                        }
                    }

                    StyledButton {
                        id: dualPaneButton
                        text: (theme.isDiffMode && !mainWindow.enableDiffLogic) ? "退出双窗口" : "双窗口解析"
                        iconChar: mdi.contentCopy
                        visible: theme.isManualMode
                        highlighted: theme.isDiffMode && !mainWindow.enableDiffLogic
                        onClicked: {
                            if (theme.isDiffMode && !mainWindow.enableDiffLogic) {
                                theme.isDiffMode = false;
                            } else {
                                theme.isDiffMode = true;
                                mainWindow.enableDiffLogic = false;
                                backend.clearDiffs();
                            }
                        }
                    }

                    StyledButton {
                        text: "日志合并"
                        iconChar: mdi.sourceMerge
                        visible: theme.isManualMode
                        onClicked: {
                            logMergeDialog.open();
                        }
                    }

                    StyledButton {
                        text: "实时监控"
                        iconChar: mdi.play
                        visible: theme.isManualMode
                        onClicked: {
                            liveLogDialog.openDialog();
                        }
                    }

                    StyledButton {
                        text: "智能组帧"
                        iconChar: mdi.compareHorizontal
                        visible: theme.isManualMode
                        onClicked: {
                            smartFramingDialog.open();
                        }
                    }

                    Label {
                        text: "协议:"
                        color: theme.text
                    }
                    StyledComboBox {
                        id: protocolComboMain
                        model: ["自动检测", "DL/T 698", "IEC 101", "IEC 104", "应用层消息", "模块消息", "Q/GDW 376.2", "DL/T 645", "南网计量上行规约"]
                        currentIndex: backend.protocolType
                        Layout.preferredWidth: 170
                        onActivated: {
                            backend.protocolType = currentIndex;
                        }

                        // 监听后端的协议类型变化，同步UI
                        Connections {
                            target: backend
                            function onProtocolTypeChanged() {
                                protocolComboMain.currentIndex = backend.protocolType;
                            }
                        }
                    }

                    StyledButton {
                        text: "📖"
                        Layout.preferredWidth: 30
                        leftPadding: 0
                        rightPadding: 0
                        ToolTip.visible: hovered
                        ToolTip.text: "功能说明"
                        onClicked: {
                            helpWindow.readmeText = backend.getReadmeContent();
                            helpWindow.show();
                        }
                    }

                    StyledButton {
                        text: "📝"
                        Layout.preferredWidth: 30
                        leftPadding: 0
                        rightPadding: 0
                        ToolTip.visible: hovered
                        ToolTip.text: "升级日志"
                        onClicked: {
                            changelogWindow.changelogText = backend.getChangelogContent();
                            changelogWindow.show();
                        }
                    }

                    StyledButton {
                        text: "🔁"
                        Layout.preferredWidth: 30
                        leftPadding: 0
                        rightPadding: 0
                        ToolTip.visible: hovered
                        ToolTip.text: "同步配置XML"
                        onClicked: {
                            protocolSyncDialog.open();
                        }
                    }

                    // 检查更新按钮
                    StyledButton {
                        text: updateChecker.checking ? "⏳" : "🔄"
                        Layout.preferredWidth: 30
                        leftPadding: 0
                        rightPadding: 0
                        enabled: !updateChecker.checking
                        ToolTip.visible: hovered
                        ToolTip.text: updateChecker.checking ? "正在检查..." : "检查更新"
                        onClicked: {
                            mainWindow.isAutoCheck = false;
                            updateChecker.checkForUpdate();
                        }
                    }

                // 启动后延迟 3 秒自动检查更新
                Timer {
                    id: autoUpdateTimer
                    interval: 3000
                    repeat: false
                    running: true
                    onTriggered: {
                        mainWindow.isAutoCheck = true;
                        updateChecker.checkForUpdate();
                    }
                }

                // 更新检查器实例
                UpdateChecker {
                    id: updateChecker
                    onUpdateResultReady: {
                        updateWindow.latestVersion = latestVersion;
                        updateWindow.releaseNotes = releaseNotes;
                        updateWindow.downloadUrl = downloadUrl;
                        updateWindow.isUpdateAvailable = updateAvailable;
                        // 自动检查仅在有新版本时弹窗；手动检查始终弹窗
                        if (!mainWindow.isAutoCheck || updateAvailable) {
                            updateWindow.x = mainWindow.x + (mainWindow.width - updateWindow.width) / 2;
                            updateWindow.y = mainWindow.y + (mainWindow.height - updateWindow.height) / 2;
                            updateWindow.show();
                        } else if (mainWindow.isAutoCheck && !updateAvailable) {
                            // 自动检查无新版本时，在状态栏提示
                            backend.statusMessage("当前已是最新版本 v" + updateChecker.currentVersion, 3000);
                        }
                        mainWindow.isAutoCheck = false;
                    }
                    onCheckError: errorMessage => {
                        backend.statusMessage(errorMessage, 3000);
                        mainWindow.isAutoCheck = false;
                    }
                }

            }
        }

        // --- Control Options ---
        StyledGroupBox {
            title: "控制选项"
            Layout.fillWidth: true
            implicitHeight: controlLayout.implicitHeight + 40
            visible: !theme.isManualMode

            RowLayout {
                id: controlLayout
                anchors.fill: parent
                anchors.topMargin: 25
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.bottomMargin: 10
                spacing: 10

                Label {
                    text: "日志文件:"
                    color: theme.text
                }
                StyledComboBox {
                    id: fileCombo
                    Layout.preferredWidth: 200
                    Layout.fillWidth: true
                    model: backend.fileList
                    onActivated: backend.loadFile(currentText, encodingCombo.currentText)
                }
                StyledButton {
                    text: "⟳" // Refresh icon
                    Layout.preferredWidth: 30
                    onClicked: backend.openFolder(folderPathField.text)
                }

                Label {
                    text: "正则:"
                    color: theme.text
                }
                TextField {
                    id: regexField
                    Layout.preferredWidth: 150
                    Layout.fillWidth: true
                    placeholderText: "正则过滤"
                    placeholderTextColor: theme.placeholderText
                    color: theme.text
                    background: Rectangle {
                        color: theme.controlBackground
                        border.color: theme.controlBorder
                        radius: 4
                    }
                    onAccepted: backend.applyFilter(text)
                }
                StyledButton {
                    text: "过滤"
                    onClicked: backend.applyFilter(regexField.text)
                }

                Label {
                    text: "编码:"
                    color: theme.text
                }
                StyledComboBox {
                    id: encodingCombo
                    model: ["auto", "UTF-8", "GBK", "ASCII"]
                    currentIndex: 0
                    Layout.preferredWidth: 90
                    onActivated: {
                        saveMainUiSetting("encoding/current", currentText);
                        backend.loadFile(fileCombo.currentText, currentText);
                    }
                }

                StyledButton {
                    text: isGlobalPreviewMode ? "返回日志" : "全局预览"
                    onClicked: {
                        isGlobalPreviewMode = !isGlobalPreviewMode;
                        if (isGlobalPreviewMode) {
                            backend.scanGlobalPreview();
                        }
                    }
                }
            }
        }

        // --- Main Content Area (Split View) ---
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 4
                color: SplitHandle.pressed ? theme.splitHandlePressed : (SplitHandle.hovered ? theme.splitHandleHover : theme.splitHandle)
            }

            // Left Side: Log Content
            StyledGroupBox {
                title: mainWindow.isGlobalPreviewMode ? "概要预览" : "日志内容"
                visible: !theme.isManualMode
                SplitView.preferredWidth: parent.width * 0.45
                SplitView.fillHeight: true

                ColumnLayout {
                    id: logContentLayout
                    anchors.fill: parent
                    anchors.topMargin: 25
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    anchors.bottomMargin: 10
                    spacing: 5
                    visible: !mainWindow.isGlobalPreviewMode

                    function findNext() {
                        var searchText = searchField.text;
                        if (searchText === "")
                            return;
                        var text = logContentArea.text;
                        var startPos = logContentArea.cursorPosition;

                        // If currently selected, start searching after the selection
                        if (logContentArea.selectedText.length > 0) {
                            startPos = logContentArea.selectionEnd;
                        }

                        var index = text.toLowerCase().indexOf(searchText.toLowerCase(), startPos);

                        if (index === -1) {
                            index = text.toLowerCase().indexOf(searchText.toLowerCase(), 0);
                        }

                        if (index !== -1) {
                            logContentArea.forceActiveFocus();
                            logContentArea.cursorPosition = index;
                            logContentArea.select(index, index + searchText.length);
                        }
                    }

                    function findPrev() {
                        var searchText = searchField.text;
                        if (searchText === "")
                            return;
                        var text = logContentArea.text;
                        var startPos = logContentArea.cursorPosition;

                        // If currently selected, start searching before the selection
                        if (logContentArea.selectedText.length > 0) {
                            startPos = logContentArea.selectionStart;
                        }

                        var index = text.toLowerCase().lastIndexOf(searchText.toLowerCase(), startPos - 1);

                        if (index === -1) {
                            index = text.toLowerCase().lastIndexOf(searchText.toLowerCase());
                        }

                        if (index !== -1) {
                            logContentArea.forceActiveFocus();
                            logContentArea.cursorPosition = index;
                            logContentArea.select(index, index + searchText.length);
                        }
                    }

                    // Search Bar
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "搜索:"
                            color: theme.text
                        }
                        TextField {
                            id: searchField
                            Layout.fillWidth: true
                            placeholderText: "查找内容..."
                            placeholderTextColor: theme.placeholderText
                            color: theme.text
                            background: Rectangle {
                                color: theme.controlBackground
                                border.color: theme.controlBorder
                                radius: 4
                            }
                            onAccepted: logContentLayout.findNext()
                        }
                        StyledButton {
                            text: "↑"
                            Layout.preferredWidth: 30
                            onClicked: logContentLayout.findPrev()
                        }
                        StyledButton {
                            text: "↓"
                            Layout.preferredWidth: 30
                            onClicked: logContentLayout.findNext()
                        }
                        StyledButton {
                            text: "清空并粘贴"
                            iconChar: mdi.contentPaste
                            onClicked: {
                                logContentArea.clear();
                                logContentArea.paste();
                            }
                        }
                    }

                    // Log Text Area
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        TextArea {
                            id: logContentArea

                            DropArea {
                                anchors.fill: parent

                                onEntered: drag => {
                                    if (drag.hasUrls || drag.hasText)
                                        drag.accept(Qt.CopyAction);
                                }

                                onDropped: drop => {
                                    if (drop.hasText && !drop.hasUrls) {
                                        // 直接拖入纯文本内容
                                        logContentArea.text = drop.text;
                                        logContentArea.forceActiveFocus();
                                    } else if (drop.hasUrls) {
                                        for (var i = 0; i < drop.urls.length; i++) {
                                            var url = drop.urls[i].toString();
                                            if (url.toLowerCase().endsWith(".txt") || url.toLowerCase().endsWith(".log")) {
                                                var path = url;
                                                // Remove file:/// prefix
                                                if (path.startsWith("file:///")) {
                                                    path = path.substring(8);
                                                } else if (path.startsWith("file://")) {
                                                    path = path.substring(7);
                                                }

                                                // Determine directory and filename
                                                var separator = path.lastIndexOf("/");
                                                if (separator === -1)
                                                    separator = path.lastIndexOf("\\");

                                                if (separator !== -1) {
                                                    var folder = path.substring(0, separator);
                                                    var fileName = path.substring(separator + 1);

                                                    // Update UI and Backend
                                                    folderPathField.text = folder;
                                                    backend.openFolder(folder); // This updates the file list
                                                    backend.loadFile(fileName, encodingCombo.currentText);

                                                    // Try to sync ComboBox
                                                    fileCombo.currentIndex = fileCombo.find(fileName);
                                                }
                                                break; // Only load the first valid file
                                            }
                                        }
                                    }
                                }

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 10
                                    visible: logContentArea.text.length === 0

                                    Text {
                                        text: "📄" // Document icon
                                        font.pixelSize: 64
                                        color: "#cccccc"
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }

                                    Text {
                                        text: "可以拖入文件直接解析"
                                        font.pixelSize: 24
                                        color: "#cccccc"
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        font.bold: true
                                    }

                                    Text {
                                        text: "也可粘贴报文文本 (Ctrl+V)，再点击「解析全文」"
                                        font.pixelSize: 14
                                        color: "#aaaaaa"
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                }
                            }
                            text: backend.currentFileContent
                            readOnly: false
                            selectByMouse: true
                            font.family: PlatformFonts.monospaceFamily
                            color: theme.text
                            background: Rectangle {
                                color: theme.controlBackground
                                border.color: theme.controlBorder
                                radius: 2
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.IBeamCursor
                                acceptedButtons: Qt.LeftButton
                                propagateComposedEvents: true

                                onPressed: mouse => {
                                    mouse.accepted = false;
                                }

                                onDoubleClicked: mouse => {
                                    var pos = logContentArea.positionAt(mouse.x, mouse.y);
                                    var text = logContentArea.text;
                                    var len = text.length;

                                    function isHexOrSpace(c) {
                                        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c === ' ';
                                    }

                                    // Expand left
                                    var start = pos;
                                    while (start > 0 && isHexOrSpace(text.charAt(start - 1))) {
                                        start--;
                                    }

                                    // Expand right
                                    var end = pos;
                                    while (end < len && isHexOrSpace(text.charAt(end))) {
                                        end++;
                                    }

                                    // Trim spaces
                                    while (start < end && text.charAt(start) === ' ')
                                        start++;
                                    while (end > start && text.charAt(end - 1) === ' ')
                                        end--;

                                    if (end > start) {
                                        logContentArea.select(start, end);
                                        mouse.accepted = true;
                                    } else {
                                        mouse.accepted = false;
                                    }
                                }
                            }

                            onSelectedTextChanged: {
                                backend.parseHex(selectedText, encodingCombo.currentText);
                            }

                            onTextChanged: {
                                // 同步到后端，使「全局预览」能扫描粘贴/拖入的文本
                                backend.loadRawText(logContentArea.text);
                            }

                            Connections {
                                target: backend
                                function onCurrentFileContentChanged() {
                                    logContentArea.text = backend.currentFileContent;
                                }
                            }

                            // 智能双击选择十六进制字节串
                            // 使用 TapHandler 处理双击事件
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onDoubleTapped: function (eventPoint, button) {
                                    var pos = logContentArea.positionAt(eventPoint.position.x, eventPoint.position.y);
                                    logContentArea.cursorPosition = pos;

                                    if (selectHexString(logContentArea)) {
                                        // 选中成功后自动解析选中的文本
                                        performParse(logContentArea.selectedText, 1);
                                    }
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    id: globalPreviewLayout
                    anchors.fill: parent
                    anchors.topMargin: 25
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    anchors.bottomMargin: 10
                    spacing: 0
                    visible: mainWindow.isGlobalPreviewMode

                    // 表头
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        color: theme.controlBackground
                        border.color: theme.controlBorder

                        Row {
                            anchors.fill: parent

                            Rectangle {
                                width: mainWindow.colWidthTime
                                height: parent.height
                                color: "transparent"
                                border.color: theme.controlBorder
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "时间"
                                    font.bold: true
                                    color: theme.text
                                }
                                MouseArea {
                                    width: 5
                                    height: parent.height
                                    anchors.right: parent.right
                                    cursorShape: Qt.SizeHorCursor
                                    onPositionChanged: mouse => {
                                        if (pressed) {
                                            mainWindow.colWidthTime = Math.max(50, mainWindow.colWidthTime + mouse.x);
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                width: mainWindow.colWidthProt
                                height: parent.height
                                color: "transparent"
                                border.color: theme.controlBorder
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "规约"
                                    font.bold: true
                                    color: theme.text
                                }
                                MouseArea {
                                    width: 5
                                    height: parent.height
                                    anchors.right: parent.right
                                    cursorShape: Qt.SizeHorCursor
                                    onPositionChanged: mouse => {
                                        if (pressed) {
                                            mainWindow.colWidthProt = Math.max(50, mainWindow.colWidthProt + mouse.x);
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                width: mainWindow.colWidthAddr
                                height: parent.height
                                color: "transparent"
                                border.color: theme.controlBorder
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "地址"
                                    font.bold: true
                                    color: theme.text
                                }
                                MouseArea {
                                    width: 5
                                    height: parent.height
                                    anchors.right: parent.right
                                    cursorShape: Qt.SizeHorCursor
                                    onPositionChanged: mouse => {
                                        if (pressed) {
                                            mainWindow.colWidthAddr = Math.max(50, mainWindow.colWidthAddr + mouse.x);
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                width: mainWindow.colWidthDir
                                height: parent.height
                                color: "transparent"
                                border.color: theme.controlBorder
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "方向"
                                    font.bold: true
                                    color: theme.text
                                }
                                MouseArea {
                                    width: 5
                                    height: parent.height
                                    anchors.right: parent.right
                                    cursorShape: Qt.SizeHorCursor
                                    onPositionChanged: mouse => {
                                        if (pressed) {
                                            mainWindow.colWidthDir = Math.max(30, mainWindow.colWidthDir + mouse.x);
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                width: mainWindow.colWidthSeq
                                height: parent.height
                                color: "transparent"
                                border.color: theme.controlBorder
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "帧序号"
                                    font.bold: true
                                    color: theme.text
                                }
                                MouseArea {
                                    width: 5
                                    height: parent.height
                                    anchors.right: parent.right
                                    cursorShape: Qt.SizeHorCursor
                                    onPositionChanged: mouse => {
                                        if (pressed) {
                                            mainWindow.colWidthSeq = Math.max(40, mainWindow.colWidthSeq + mouse.x);
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                width: parent.width - (mainWindow.colWidthTime + mainWindow.colWidthProt + mainWindow.colWidthAddr + mainWindow.colWidthDir + mainWindow.colWidthSeq)
                                height: parent.height
                                color: "transparent"
                                border.color: theme.controlBorder
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "报文类型"
                                    font.bold: true
                                    color: theme.text
                                }
                            }
                        }
                    }

                    // 表格内容
                    ListView {
                        id: globalPreviewList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: backend.globalPreviewModel

                        delegate: Rectangle {
                            width: globalPreviewList.width
                            height: 30
                            color: ListView.isCurrentItem ? theme.tableHighlight : (index % 2 == 0 ? Qt.rgba(0, 0, 0, 0) : theme.rowAlt)

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: mouse => {
                                    globalPreviewList.currentIndex = index;
                                    if (mouse.button === Qt.RightButton) {
                                        contextMenu.popup();
                                    } else {
                                        // 点击后在右侧显示该报文的详细解析
                                        backend.parseHex(model.rawData, 1, "auto", 1);
                                    }
                                }
                            }

                            QQC2.Menu {
                                id: contextMenu
                                QQC2.MenuItem {
                                    text: "返回日志并高亮"
                                    onTriggered: {
                                        var range = backend.getLineRange(model.lineNumber);
                                        if (range.length === 2 && range[0] !== -1) {
                                            mainWindow.isGlobalPreviewMode = false;
                                            logContentArea.cursorPosition = range[0];
                                            logContentArea.select(range[0], range[1]);
                                            logContentArea.forceActiveFocus();
                                        }
                                    }
                                }
                            }

                            Row {
                                anchors.fill: parent

                                Text {
                                    width: mainWindow.colWidthTime
                                    height: parent.height
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignLeft
                                    leftPadding: 5
                                    text: model.timestamp
                                    font.family: PlatformFonts.monospaceFamily
                                    font.pixelSize: 12
                                    color: theme.text
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: mainWindow.colWidthProt
                                    height: parent.height
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignLeft
                                    leftPadding: 5
                                    text: model.protocol
                                    font.family: PlatformFonts.monospaceFamily
                                    font.pixelSize: 12
                                    color: theme.text
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: mainWindow.colWidthAddr
                                    height: parent.height
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignLeft
                                    leftPadding: 5
                                    text: model.address
                                    font.family: PlatformFonts.monospaceFamily
                                    font.pixelSize: 12
                                    color: theme.text
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: mainWindow.colWidthDir
                                    height: parent.height
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignHCenter
                                    text: {
                                        var d = model.direction;
                                        // S->C (Server to Client) mapped to Up Arrow (Uplink/Response)
                                        if (d.indexOf("S->C") !== -1 || d.indexOf("1") !== -1 || d.toLowerCase().indexOf("down") !== -1)
                                            return "↑";
                                        // C->S (Client to Server) mapped to Down Arrow (Downlink/Request)
                                        if (d.indexOf("C->S") !== -1 || d.indexOf("0") !== -1 || d.toLowerCase().indexOf("up") !== -1)
                                            return "↓";
                                        return d;
                                    }
                                    font.family: PlatformFonts.monospaceFamily
                                    font.pixelSize: (text === "↓" || text === "↑") ? 18 : 12
                                    font.bold: true
                                    color: {
                                        if (text === "↓")
                                            return theme.success;
                                        if (text === "↑")
                                            return theme.error;
                                        return theme.text;
                                    }
                                }

                                Text {
                                    width: mainWindow.colWidthSeq
                                    height: parent.height
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignLeft
                                    leftPadding: 5
                                    text: model.frameSeq
                                    font.family: PlatformFonts.monospaceFamily
                                    font.pixelSize: 12
                                    color: theme.text
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: parent.width - (mainWindow.colWidthTime + mainWindow.colWidthProt + mainWindow.colWidthAddr + mainWindow.colWidthDir + mainWindow.colWidthSeq)
                                    height: parent.height
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignLeft
                                    leftPadding: 5
                                    text: model.messageType
                                    font.family: PlatformFonts.monospaceFamily
                                    font.pixelSize: 12
                                    color: theme.text
                                    elide: Text.ElideRight
                                }
                            }

                            Rectangle {
                                width: parent.width
                                height: 1
                                color: theme.controlBorder
                                anchors.bottom: parent.bottom
                            }
                        }

                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }
                    }
                }
            }

            // Right Side: Parsing Area
            SplitView {
                SplitView.preferredWidth: parent.width * 0.55
                SplitView.fillHeight: true
                orientation: Qt.Horizontal

                ParsePane {
                    id: pane1
                    SplitView.fillWidth: true
                    SplitView.fillHeight: true
                    titlePrefix: theme.isDiffMode ? "左侧-" : ""
                    settingsPrefix: "pane1"
                    targetModelId: 1
                    resultModel: backend.resultModel
                    showInput: theme.isManualMode

                    listView.onContentYChanged: {
                        if (theme.isDiffMode && (listView.moving || listView.flicking)) {
                            pane2.listView.contentY = listView.contentY;
                        }
                    }
                }

                ParsePane {
                    id: pane2
                    visible: theme.isDiffMode
                    SplitView.preferredWidth: parent.width * 0.5
                    SplitView.fillHeight: true
                    titlePrefix: "右侧-"
                    settingsPrefix: "pane2"
                    targetModelId: 2
                    resultModel: backend.resultModel2
                    showInput: theme.isManualMode

                    listView.onContentYChanged: {
                        if (theme.isDiffMode && (listView.moving || listView.flicking)) {
                            pane1.listView.contentY = listView.contentY;
                        }
                    }
                }
            }
        }

        // --- Status Bar ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 25
            color: "transparent"

            RowLayout {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: 15

                Timer {
                    id: statusTimer
                    onTriggered: {
                        statusLabel.text = "就绪";
                        statusLabel.color = theme.text;
                    }
                }

                Connections {
                    target: backend
                    function onStatusMessage(message, timeout) {
                        statusLabel.text = message;
                        statusLabel.color = "red"; // Highlight warning messages
                        statusTimer.interval = timeout;
                        statusTimer.restart();
                    }
                }

                Label {
                    id: statusLabel
                    text: "就绪"
                    font.pixelSize: 12
                    color: theme.text
                }

                // 协议状态显示
                Label {
                    text: backend.protocolType === 0 ? ("| 检测协议: " + backend.currentProtocolName) : ("| 当前协议: " + backend.currentProtocolName)
                    font.pixelSize: 12
                    color: backend.currentProtocolName === "自动检测" ? theme.subText : theme.accent
                    visible: backend.currentProtocolName !== "自动检测"
                }
            }

            RowLayout {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                visible: backend.currentProtocolName.indexOf("698") !== -1

                Label {
                    text: "地址:"
                    font.pixelSize: 12
                    color: theme.text
                }

                TextField {
                    id: addrEdit698
                    placeholderText: ""
                    Layout.preferredWidth: 140
                    font.pixelSize: 12
                    color: theme.text
                    background: Rectangle {
                        color: theme.controlBackground
                        border.color: theme.controlBorder
                        radius: 2
                    }
                }

                Label {
                    text: "逻辑地址:"
                    font.pixelSize: 12
                    color: theme.text
                }

                TextField {
                    id: logicAddrEdit698
                    placeholderText: ""
                    text: ""
                    Layout.preferredWidth: 30
                    font.pixelSize: 12
                    color: theme.text
                    background: Rectangle {
                        color: theme.controlBackground
                        border.color: theme.controlBorder
                        radius: 2
                    }
                }

                StyledButton {
                    text: "更改"
                    Layout.preferredHeight: 20
                    Layout.preferredWidth: 50
                    onClicked: {
                        if (pane1.manualParseInput.text !== "") {
                            var newHex = backend.replace698Address(pane1.manualParseInput.text, addrEdit698.text, logicAddrEdit698.text);
                            pane1.manualParseInput.text = newHex;
                            performParse(newHex, 1);
                        }
                    }
                }
            }

            RowLayout {
                anchors.centerIn: parent
                visible: theme.isDiffMode
                spacing: 10

                StyledButton {
                    text: "上一个差异"
                    Layout.preferredHeight: 20
                    onClicked: {
                        var currentIdx = pane1.listView.currentIndex;
                        var prevDiff = backend.findPrevDiff(currentIdx);
                        if (prevDiff !== -1) {
                            pane1.listView.positionViewAtIndex(prevDiff, ListView.Beginning);
                            pane2.listView.positionViewAtIndex(prevDiff, ListView.Beginning);
                            pane1.listView.currentIndex = prevDiff;
                            pane2.listView.currentIndex = prevDiff;
                        }
                    }
                }

                StyledButton {
                    text: "下一个差异"
                    Layout.preferredHeight: 20
                    onClicked: {
                        var currentIdx = pane1.listView.currentIndex;
                        var nextDiff = backend.findNextDiff(currentIdx);
                        if (nextDiff !== -1) {
                            pane1.listView.positionViewAtIndex(nextDiff, ListView.Beginning);
                            pane2.listView.positionViewAtIndex(nextDiff, ListView.Beginning);
                            pane1.listView.currentIndex = nextDiff;
                            pane2.listView.currentIndex = nextDiff;
                        }
                    }
                }
            }
        }
    }

    Window {
        id: helpWindow
        width: 800
        height: 600
        title: "功能说明 - 多协议解析器 v" + mainWindow.appVersion
        visible: false
        color: theme.background

        property string readmeText: ""

        ScrollView {
            anchors.fill: parent
            anchors.margins: 10

            TextArea {
                text: helpWindow.readmeText
                readOnly: true
                textFormat: Text.MarkdownText
                color: theme.text
                background: null
                wrapMode: Text.Wrap
                selectByMouse: true
            }
        }
    }

    Window {
        id: changelogWindow
        width: 800
        height: 600
        title: "升级日志 - 多协议解析器 v" + mainWindow.appVersion
        visible: false
        color: theme.background

        property string changelogText: ""

        ScrollView {
            anchors.fill: parent
            anchors.margins: 10

            TextArea {
                text: changelogWindow.changelogText
                readOnly: true
                textFormat: Text.MarkdownText
                color: theme.text
                background: null
                wrapMode: Text.Wrap
                selectByMouse: true
            }
        }
    }

    Window {
        id: popupWindow
        width: 800
        height: 600
        title: "明文 APDU 解析 - 多协议解析器 v" + mainWindow.appVersion
        visible: false
        color: theme.background

        ParsePane {
            id: popupPane
            anchors.fill: parent
            anchors.margins: 10
            titlePrefix: "APDU-"
            targetModelId: 3
            resultModel: backend.popupResultModel
            showInput: false
        }
    }

    // 预置报文管理窗口
    Window {
        id: presetManagerWindow
        width: 700
        height: 500
        title: "预置报文管理"
        visible: false
        color: theme.background

        property int minResizeWidth: 500
        property int minResizeHeight: 350
        property int resizeMargin: 6

        // 窗口边框
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: theme.controlBorder // 使用主题边框色
            border.width: 1
            z: 200 // 确保在最上层
        }

        flags: Qt.Window | Qt.FramelessWindowHint

        // 自定义标题栏
        TitleBar {
            id: presetTitleBar
            z: 100
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            targetWindow: presetManagerWindow
            title: presetManagerWindow.title
            titleBarColor: theme.background
            titleTextColor: theme.text
            buttonHoverColor: theme.buttonHover
            closeButtonHoverColor: theme.error
            isDarkTheme: theme.isDark
            showThemeToggle: false
        }

        // 边缘缩放区域 - 预置报文管理窗口
        // 左边缘
        MouseArea {
            width: presetManagerWindow.resizeMargin
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: presetManagerWindow.resizeMargin
            anchors.bottomMargin: presetManagerWindow.resizeMargin
            cursorShape: Qt.SizeHorCursor
            z: 300

            property real startX: 0
            property real startWidth: 0
            property real startWindowX: 0

            onPressed: function (mouse) {
                startX = mouse.x;
                startWidth = presetManagerWindow.width;
                startWindowX = presetManagerWindow.x;
            }
            onPositionChanged: function (mouse) {
                if (pressed) {
                    var dx = mouse.x - startX;
                    var newWidth = startWidth - dx;
                    if (newWidth >= presetManagerWindow.minResizeWidth) {
                        presetManagerWindow.width = newWidth;
                        presetManagerWindow.x = startWindowX + dx;
                    }
                }
            }
        }

        // 右边缘
        MouseArea {
            width: presetManagerWindow.resizeMargin
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: presetManagerWindow.resizeMargin
            anchors.bottomMargin: presetManagerWindow.resizeMargin
            cursorShape: Qt.SizeHorCursor
            z: 300

            property real startX: 0
            property real startWidth: 0

            onPressed: function (mouse) {
                startX = mapToGlobal(mouse.x, mouse.y).x;
                startWidth = presetManagerWindow.width;
            }
            onPositionChanged: function (mouse) {
                if (pressed) {
                    var currentX = mapToGlobal(mouse.x, mouse.y).x;
                    var dx = currentX - startX;
                    var newWidth = startWidth + dx;
                    if (newWidth >= presetManagerWindow.minResizeWidth) {
                        presetManagerWindow.width = newWidth;
                    }
                }
            }
        }

        // 下边缘
        MouseArea {
            height: presetManagerWindow.resizeMargin
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: presetManagerWindow.resizeMargin
            anchors.rightMargin: presetManagerWindow.resizeMargin
            cursorShape: Qt.SizeVerCursor
            z: 300

            property real startY: 0
            property real startHeight: 0

            onPressed: function (mouse) {
                startY = mapToGlobal(mouse.x, mouse.y).y;
                startHeight = presetManagerWindow.height;
            }
            onPositionChanged: function (mouse) {
                if (pressed) {
                    var currentY = mapToGlobal(mouse.x, mouse.y).y;
                    var dy = currentY - startY;
                    var newHeight = startHeight + dy;
                    if (newHeight >= presetManagerWindow.minResizeHeight) {
                        presetManagerWindow.height = newHeight;
                    }
                }
            }
        }

        // 左下角
        MouseArea {
            width: presetManagerWindow.resizeMargin
            height: presetManagerWindow.resizeMargin
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            cursorShape: Qt.SizeBDiagCursor
            z: 300

            property point startPos: Qt.point(0, 0)
            property real startWidth: 0
            property real startHeight: 0
            property real startWindowX: 0

            onPressed: function (mouse) {
                startPos = mapToGlobal(mouse.x, mouse.y);
                startWidth = presetManagerWindow.width;
                startHeight = presetManagerWindow.height;
                startWindowX = presetManagerWindow.x;
            }
            onPositionChanged: function (mouse) {
                if (pressed) {
                    var currentPos = mapToGlobal(mouse.x, mouse.y);
                    var dx = currentPos.x - startPos.x;
                    var dy = currentPos.y - startPos.y;

                    var newWidth = startWidth - dx;
                    if (newWidth >= presetManagerWindow.minResizeWidth) {
                        presetManagerWindow.width = newWidth;
                        presetManagerWindow.x = startWindowX + dx;
                    }

                    var newHeight = startHeight + dy;
                    if (newHeight >= presetManagerWindow.minResizeHeight) {
                        presetManagerWindow.height = newHeight;
                    }
                }
            }
        }

        // 右下角
        MouseArea {
            width: presetManagerWindow.resizeMargin
            height: presetManagerWindow.resizeMargin
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            cursorShape: Qt.SizeFDiagCursor
            z: 300

            property point startPos: Qt.point(0, 0)
            property real startWidth: 0
            property real startHeight: 0

            onPressed: function (mouse) {
                startPos = mapToGlobal(mouse.x, mouse.y);
                startWidth = presetManagerWindow.width;
                startHeight = presetManagerWindow.height;
            }
            onPositionChanged: function (mouse) {
                if (pressed) {
                    var currentPos = mapToGlobal(mouse.x, mouse.y);
                    var dx = currentPos.x - startPos.x;
                    var dy = currentPos.y - startPos.y;

                    var newWidth = startWidth + dx;
                    if (newWidth >= presetManagerWindow.minResizeWidth) {
                        presetManagerWindow.width = newWidth;
                    }

                    var newHeight = startHeight + dy;
                    if (newHeight >= presetManagerWindow.minResizeHeight) {
                        presetManagerWindow.height = newHeight;
                    }
                }
            }
        }

        // 当前管理窗口选择的协议
        property string currentProtocol: "DL/T 698"
        // 已有报文列表
        property var existingMessages: []
        // 编辑模式相关属性
        property bool isEditMode: false
        property string editingOldSummary: ""

        // 刷新已有报文列表
        function refreshExistingList() {
            existingMessages = backend.getPresetList(currentProtocol);
        }

        // 进入编辑模式
        function enterEditMode(summary) {
            var content = backend.getPresetContent(currentProtocol, summary);
            newSummaryInput.text = summary;
            newContentInput.text = content;
            editingOldSummary = summary;
            isEditMode = true;
        }

        // 退出编辑模式
        function exitEditMode() {
            newSummaryInput.text = "";
            newContentInput.text = "";
            editingOldSummary = "";
            isEditMode = false;
        }

        // 窗口显示时刷新列表并居中到主窗口
        onVisibleChanged: {
            if (visible) {
                // 将窗口居中到主窗口
                x = mainWindow.x + (mainWindow.width - width) / 2;
                y = mainWindow.y + (mainWindow.height - height) / 2;
                refreshExistingList();
                exitEditMode(); // 重置编辑模式
            }
        }

        ColumnLayout {
            anchors.top: presetTitleBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 15
            spacing: 15

            // 协议选择行
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Label {
                    text: "协议类型:"
                    color: theme.text
                    font.pixelSize: 14
                }

                StyledComboBox {
                    id: managerProtocolCombo
                    Layout.preferredWidth: 150
                    model: ["DL/T 698", "IEC 101", "IEC 104", "应用层消息", "模块消息", "Q/GDW 376.2", "DL/T 645", "南网计量上行规约"]
                    currentIndex: 0
                    onActivated: {
                        presetManagerWindow.currentProtocol = currentText;
                        presetManagerWindow.refreshExistingList();
                    }
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            // 分隔线
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: theme.controlBorder
            }

            // 添加/编辑报文区域
            StyledGroupBox {
                title: presetManagerWindow.isEditMode ? "编辑报文" : "添加新报文"
                Layout.fillWidth: true
                Layout.preferredHeight: 180

                ColumnLayout {
                    anchors.fill: parent
                    anchors.topMargin: 25
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    anchors.bottomMargin: 10
                    spacing: 10

                    // 报文概述
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            text: "报文概述:"
                            color: theme.text
                            font.pixelSize: 12
                            Layout.preferredWidth: 70
                        }

                        TextField {
                            id: newSummaryInput
                            Layout.fillWidth: true
                            placeholderText: "输入报文名称/概述..."
                            placeholderTextColor: theme.placeholderText
                            color: theme.text
                            background: Rectangle {
                                color: theme.controlBackground
                                border.color: theme.controlBorder
                                radius: 4
                            }
                        }
                    }

                    // 报文内容
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 10

                        Label {
                            text: "报文内容:"
                            color: theme.text
                            font.pixelSize: 12
                            Layout.preferredWidth: 70
                            Layout.alignment: Qt.AlignTop
                        }

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            TextArea {
                                id: newContentInput
                                placeholderText: "输入十六进制报文内容，如: 68 17 00 43 05..."
                                placeholderTextColor: theme.placeholderText
                                color: theme.text
                                font.family: PlatformFonts.monospaceFamily
                                wrapMode: TextArea.Wrap
                                background: Rectangle {
                                    color: theme.controlBackground
                                    border.color: theme.controlBorder
                                    radius: 4
                                }
                            }
                        }
                    }

                    // 按钮行
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Item {
                            Layout.fillWidth: true
                        }

                        // 取消编辑按钮（仅在编辑模式下显示）
                        StyledButton {
                            text: "取消编辑"
                            iconChar: mdi.close
                            visible: presetManagerWindow.isEditMode
                            onClicked: {
                                presetManagerWindow.exitEditMode();
                            }
                        }

                        // 添加/保存按钮
                        StyledButton {
                            text: presetManagerWindow.isEditMode ? "保存修改" : "添加报文"
                            iconChar: presetManagerWindow.isEditMode ? mdi.check : mdi.plus
                            onClicked: {
                                var summary = newSummaryInput.text.trim();
                                var content = newContentInput.text.trim();

                                if (summary === "") {
                                    backend.statusMessage("请输入报文概述", 2000);
                                    return;
                                }
                                if (content === "") {
                                    backend.statusMessage("请输入报文内容", 2000);
                                    return;
                                }

                                var result;
                                if (presetManagerWindow.isEditMode) {
                                    // 编辑模式：调用更新方法
                                    result = backend.updatePresetMessage(presetManagerWindow.currentProtocol, presetManagerWindow.editingOldSummary, summary, content);
                                } else {
                                    // 添加模式
                                    result = backend.addPresetMessage(presetManagerWindow.currentProtocol, summary, content);
                                }

                                if (result) {
                                    // 清空输入并退出编辑模式
                                    presetManagerWindow.exitEditMode();
                                    // 刷新列表
                                    presetManagerWindow.refreshExistingList();
                                }
                            }
                        }
                    }
                }
            }

            // 分隔线
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: theme.controlBorder
            }

            // 已有报文列表
            StyledGroupBox {
                title: "已有报文列表 (" + presetManagerWindow.existingMessages.length + " 条)"
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.topMargin: 25
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    anchors.bottomMargin: 10
                    spacing: 10

                    // 报文列表
                    ListView {
                        id: existingMessagesList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: presetManagerWindow.existingMessages

                        delegate: Rectangle {
                            width: existingMessagesList.width
                            height: 35
                            color: existingMessagesList.currentIndex === index ? theme.tableHighlight : (index % 2 === 0 ? "transparent" : theme.rowAlt)

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 10

                                Text {
                                    text: modelData
                                    color: theme.text
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                StyledButton {
                                    text: "修改"
                                    Layout.preferredWidth: 60
                                    Layout.preferredHeight: 25
                                    onClicked: {
                                        presetManagerWindow.enterEditMode(modelData);
                                    }
                                }

                                StyledButton {
                                    text: "删除"
                                    Layout.preferredWidth: 60
                                    Layout.preferredHeight: 25
                                    onClicked: {
                                        backend.removePresetMessage(presetManagerWindow.currentProtocol, modelData);
                                        presetManagerWindow.refreshExistingList();
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                z: -1
                                onClicked: {
                                    existingMessagesList.currentIndex = index;
                                }
                            }
                        }

                        ScrollBar.vertical: ScrollBar {}
                    }

                    // 提示文字
                    Text {
                        visible: presetManagerWindow.existingMessages.length === 0
                        text: "暂无预置报文，请在上方添加"
                        color: theme.subText
                        font.pixelSize: 12
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            // 底部按钮
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item {
                    Layout.fillWidth: true
                }

                StyledButton {
                    text: "关闭"
                    onClicked: presetManagerWindow.close()
                }
            }
        }
    } // End of PresetManager Window

    ProtocolSyncDialog {
        id: protocolSyncDialog
        ownerWindow: mainWindow
        transientParent: null
        theme: theme
        syncManager: backend.syncManager
        promptSyncManager: backend.promptSyncManager
    }

    QQC2.Dialog {
        id: securityControlDialog
        width: 540
        modal: false
        focus: false
        x: Math.round((mainWindow.width - width) / 2)
        y: Math.round((mainWindow.height - height) / 2)
        closePolicy: QQC2.Popup.CloseOnEscape | QQC2.Popup.CloseOnPressOutside
        standardButtons: QQC2.Dialog.NoButton

        Component.onCompleted: {
            if (backend.securityControlActive)
                open()
        }

        contentItem: ColumnLayout {
            width: 500
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                height: 42
                radius: 6
                color: theme.warning

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10

                    Text {
                        text: mdi.alert
                        font.family: mdiFont.name !== "" ? mdiFont.name : "Material Design Icons"
                        font.pixelSize: 20
                        color: "white"
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Text {
                        text: "安全管控提示"
                        font.pixelSize: 16
                        font.bold: true
                        color: "white"
                        Layout.alignment: Qt.AlignVCenter
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: backend.securityControlMessage
                wrapMode: Text.WordWrap
                color: theme.text
                font.pixelSize: 13
            }

            Rectangle {
                Layout.fillWidth: true
                radius: 6
                color: theme.isDark ? "#2b1d12" : "#fff1df"
                border.color: theme.warning
                border.width: 1

                Text {
                    anchors.fill: parent
                    anchors.margins: 12
                    text: "剩余可运行时间：" + backend.securityControlRemainingSeconds + " 秒"
                    color: theme.text
                    font.pixelSize: 14
                    font.bold: true
                }
            }

            Text {
                Layout.fillWidth: true
                text: "在倒计时结束前，只要成功连接内网规约服务器并获取到 XML 列表，限制会自动解除。"
                wrapMode: Text.WordWrap
                color: theme.subText
                font.pixelSize: 12
            }
        }

        background: Rectangle {
            color: theme.background
            radius: 8
            border.color: theme.controlBorder
            border.width: 1
        }

        footer: Rectangle {
            color: theme.background
            border.color: theme.controlBorder
            height: 58

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 10

                Item {
                    Layout.fillWidth: true
                }

                StyledButton {
                    text: "打开协议同步"
                    iconChar: mdi.sync
                    Layout.preferredWidth: 130
                    onClicked: {
                        protocolSyncDialog.open()
                        securityControlDialog.close()
                    }
                }

                StyledButton {
                    text: "知道了"
                    Layout.preferredWidth: 90
                    onClicked: securityControlDialog.close()
                }
            }
        }
    }

    LogMergeDialog {
        id: logMergeDialog
        ownerWindow: mainWindow
        transientParent: null
        theme: theme
    }

    LiveLogDialog {
        id: liveLogDialog
        ownerWindow: mainWindow
        transientParent: null
        theme: theme
        backend: backend
    }

    SmartFramingDialog {
        id: smartFramingDialog
        ownerWindow: mainWindow
        transientParent: mainWindow
        theme: theme
        backend: backend
        onFramingResult: function(hexStr, protocolType) {
            backend.protocolType = protocolType
            protocolComboMain.currentIndex = protocolType
            performParse(hexStr, protocolType)
        }
    }

    // 更新结果窗口（支持程序内下载与自动更新）
    Window {
        id: updateWindow
        width: 800
        height: 460
        visible: false
        title: isUpdateAvailable ? "发现新版本" : "检查更新"
        flags: Qt.Window | Qt.FramelessWindowHint
        modality: Qt.NonModal
        transientParent: null

        property string latestVersion: ""
        property string releaseNotes: ""
        property string downloadUrl: ""
        property bool isUpdateAvailable: false

        // UI 状态: "idle" | "downloading" | "verifying" | "ready" | "error"
        property string updateState: "idle"
        property string errorMsg: ""
        property int resizeMargin: 6

        color: "transparent" // Set to transparent to allow resizing handles interaction if outside

        Component.onCompleted: {
            // 断开与主窗口的父子关系，避免主窗口移动时弹窗跟随
            transientParent = null;
        }

        // 窗口边框
        Rectangle {
            anchors.fill: parent
            color: theme.background
            border.color: theme.controlBorder // 使用主题边框色
            border.width: 1
            z: 0 // Background at bottom
        }

        // 自定义标题栏
        TitleBar {
            id: updateTitleBar
            z: 100
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            targetWindow: updateWindow
            title: updateWindow.title

            // 绑定主题颜色
            titleBarColor: theme.background
            titleTextColor: theme.text
            buttonHoverColor: theme.buttonHover
            closeButtonHoverColor: theme.error
        }

        // 监听 UpdateChecker 信号以驱动状态
        Connections {
            target: updateChecker

            function onDownloadingChanged() {
                if (updateChecker.downloading) {
                    updateWindow.updateState = "downloading";
                }
            }

            function onDownloadProgressChanged(progress) {
                // 进度条自动绑定，无需额外处理
            }

            function onVerifyingChanged() {
                if (updateChecker.verifying) {
                    updateWindow.updateState = "verifying";
                }
            }

            function onDownloadFinished() {
                updateWindow.updateState = "ready";
            }

            function onDownloadErrorOccurred(errorMessage) {
                updateWindow.errorMsg = errorMessage;
                updateWindow.updateState = "error";
            }

            function onVerifyFinished(success) {
                if (!success) {
                    updateWindow.updateState = "error";
                }
            }
        }

        onVisibleChanged: {
            if (visible) {
                // 将窗口居中于主窗口
                updateWindow.x = mainWindow.x + (mainWindow.width - updateWindow.width) / 2;
                updateWindow.y = mainWindow.y + (mainWindow.height - updateWindow.height) / 2;
            } else {
                // 窗口关闭时重置状态（但不取消已完成的下载）
                if (updateWindow.updateState !== "ready") {
                    updateWindow.updateState = "idle";
                    updateWindow.errorMsg = "";
                }
            }
        }

        // === 边缘缩放 MouseAreas ===
        // 左边
        MouseArea {
            width: updateWindow.resizeMargin
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: updateWindow.resizeMargin
            anchors.bottomMargin: updateWindow.resizeMargin
            cursorShape: Qt.SizeHorCursor
            z: 200
            onPressed: mouse => updateWindow.startSystemResize(Qt.LeftEdge)
        }
        // 右边
        MouseArea {
            width: updateWindow.resizeMargin
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: updateWindow.resizeMargin
            anchors.bottomMargin: updateWindow.resizeMargin
            cursorShape: Qt.SizeHorCursor
            z: 200
            onPressed: mouse => updateWindow.startSystemResize(Qt.RightEdge)
        }
        // 上边
        MouseArea {
            height: updateWindow.resizeMargin
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: updateWindow.resizeMargin
            anchors.rightMargin: updateWindow.resizeMargin
            cursorShape: Qt.SizeVerCursor
            z: 200
            onPressed: mouse => updateWindow.startSystemResize(Qt.TopEdge)
        }
        // 下边
        MouseArea {
            height: updateWindow.resizeMargin
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: updateWindow.resizeMargin
            anchors.rightMargin: updateWindow.resizeMargin
            cursorShape: Qt.SizeVerCursor
            z: 200
            onPressed: mouse => updateWindow.startSystemResize(Qt.BottomEdge)
        }
        // 左上角
        MouseArea {
            width: updateWindow.resizeMargin
            height: updateWindow.resizeMargin
            anchors.left: parent.left
            anchors.top: parent.top
            cursorShape: Qt.SizeFDiagCursor
            z: 200
            onPressed: mouse => updateWindow.startSystemResize(Qt.LeftEdge | Qt.TopEdge)
        }
        // 右上角
        MouseArea {
            width: updateWindow.resizeMargin
            height: updateWindow.resizeMargin
            anchors.right: parent.right
            anchors.top: parent.top
            cursorShape: Qt.SizeBDiagCursor
            z: 200
            onPressed: mouse => updateWindow.startSystemResize(Qt.RightEdge | Qt.TopEdge)
        }
        // 左下角
        MouseArea {
            width: updateWindow.resizeMargin
            height: updateWindow.resizeMargin
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            cursorShape: Qt.SizeBDiagCursor
            z: 200
            onPressed: mouse => updateWindow.startSystemResize(Qt.LeftEdge | Qt.BottomEdge)
        }
        // 右下角
        MouseArea {
            width: updateWindow.resizeMargin
            height: updateWindow.resizeMargin
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            cursorShape: Qt.SizeFDiagCursor
            z: 200
            onPressed: mouse => updateWindow.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
        }

        ColumnLayout {
            anchors.top: updateTitleBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 20
            spacing: 12

            // 标题区域
            RowLayout {
                spacing: 15
                Text {
                    text: updateWindow.isUpdateAvailable ? "🎉 发现新版本 " + updateWindow.latestVersion : "✅ 当前已是最新版本"
                    color: updateWindow.isUpdateAvailable ? theme.success : theme.text
                    font.pixelSize: 18
                    font.bold: true
                    Layout.fillWidth: true
                }
            }

            // 版本对比
            Text {
                text: "当前版本: " + updateChecker.currentVersion + (updateWindow.isUpdateAvailable ? "  ➜  最新版本: " + updateWindow.latestVersion : "")
                color: theme.subText
                font.pixelSize: 13
                visible: updateWindow.isUpdateAvailable
            }

            // 分隔线
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: theme.border
            }

            // 更新日志标题
            Text {
                text: "更新内容："
                color: theme.text
                font.pixelSize: 14
                font.bold: true
                visible: updateWindow.isUpdateAvailable
            }

            // 更新日志内容区
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: updateWindow.isUpdateAvailable
                clip: true
                // 注意：某些 Qt 版本的 ScrollView 不支持 `horizontalScrollBarPolicy`，
                // 我们通过让内部 TextArea 填充容器并启用折行来避免横向滚动。

                TextArea {
                    text: updateWindow.releaseNotes
                    color: theme.text
                    font.pixelSize: 13
                    readOnly: true
                    wrapMode: Text.WordWrap
                    anchors.fill: parent
                    background: Rectangle {
                        color: "transparent"
                    }
                    selectByMouse: true
                }
            }

            // 无更新时的占位
            Item {
                Layout.fillHeight: true
                Layout.fillWidth: true
                visible: !updateWindow.isUpdateAvailable
            }

            // === 下载进度区域 ===
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                // 仅在真正开始下载或校验时显示，避免在展示更新窗口时出现突兀的白色进度条
                visible: updateChecker.downloading || updateChecker.verifying

                // 进度条 - 自定义样式以匹配应用主题
                ProgressBar {
                    id: downloadProgressBar
                    Layout.fillWidth: true
                    // 直接根据 UpdateChecker 的状态控制可见性，避免父容器与状态不同步
                    visible: updateChecker.downloading || updateChecker.verifying
                    from: 0
                    to: 1.0
                    value: updateChecker.downloadProgress
                    indeterminate: updateWindow.updateState === "verifying"

                    // 进度条轨道背景
                    background: Rectangle {
                        implicitWidth: 200
                        implicitHeight: 8
                        radius: 4
                        color: theme.controlBackground
                        border.color: theme.controlBorder
                        border.width: 1
                    }

                    // 进度条填充内容
                    contentItem: Item {
                        implicitWidth: 200
                        implicitHeight: 8

                        // 确定进度模式 - 从左到右填充
                        Rectangle {
                            visible: !downloadProgressBar.indeterminate
                            width: downloadProgressBar.visualPosition * parent.width
                            height: parent.height
                            radius: 4
                            color: theme.accent

                            Behavior on width {
                                NumberAnimation {
                                    duration: 200
                                    easing.type: Easing.OutQuad
                                }
                            }
                        }

                        // 不确定模式 - 滑块来回动画
                        Rectangle {
                            id: indeterminateBar
                            visible: downloadProgressBar.indeterminate
                            width: parent.width * 0.35
                            height: parent.height
                            radius: 4
                            color: theme.accent

                            SequentialAnimation on x {
                                running: downloadProgressBar.indeterminate && downloadProgressBar.visible
                                loops: Animation.Infinite
                                NumberAnimation {
                                    from: 0
                                    to: downloadProgressBar.width - indeterminateBar.width
                                    duration: 1200
                                    easing.type: Easing.InOutQuad
                                }
                                NumberAnimation {
                                    from: downloadProgressBar.width - indeterminateBar.width
                                    to: 0
                                    duration: 1200
                                    easing.type: Easing.InOutQuad
                                }
                            }
                        }
                    }
                }

                // 状态文本
                Text {
                    text: {
                        if (updateWindow.updateState === "downloading") {
                            return "正在下载... " + Math.round(updateChecker.downloadProgress * 100) + "%";
                        } else if (updateWindow.updateState === "verifying") {
                            return "正在校验文件完整性...";
                        }
                        return "";
                    }
                    color: theme.subText
                    font.pixelSize: 12
                }
            }

            // === 下载完成提示 ===
            Text {
                text: "✅ 下载完成，校验通过！可以立即安装更新。"
                color: theme.success
                font.pixelSize: 13
                font.bold: true
                visible: updateWindow.updateState === "ready"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            // === 错误提示 ===
            Text {
                text: "❌ " + updateWindow.errorMsg
                color: theme.error
                font.pixelSize: 12
                visible: updateWindow.updateState === "error"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            // === 底部按钮区域 ===
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 10

                // 关闭 / 稍后按钮（始终可见）
                StyledButton {
                    text: updateWindow.updateState === "ready" ? "稍后" : "关闭"
                    onClicked: updateWindow.close()
                }

                // 取消下载按钮
                StyledButton {
                    text: "取消下载"
                    visible: updateWindow.updateState === "downloading"
                    onClicked: {
                        updateChecker.cancelDownload();
                        updateWindow.updateState = "idle";
                    }
                }

                // 重试按钮（错误状态）
                StyledButton {
                    text: "重试 🔄"
                    visible: updateWindow.updateState === "error"
                    onClicked: {
                        updateWindow.updateState = "idle";
                        updateWindow.errorMsg = "";
                        updateChecker.downloadUpdate();
                    }
                }

                // 下载更新按钮（初始状态）
                StyledButton {
                    text: "下载更新 ⬇"
                    visible: updateWindow.isUpdateAvailable && updateWindow.updateState === "idle" &&
                             updateChecker.automaticInstallAvailable
                    onClicked: {
                        updateChecker.downloadUpdate();
                    }
                }

                // 前往下载按钮（降级：当下载 URL 不是直接文件时）
                StyledButton {
                    text: "前往下载 🚀"
                    visible: updateWindow.isUpdateAvailable && updateWindow.updateState === "idle" &&
                             updateWindow.downloadUrl !== "" &&
                             !updateChecker.automaticInstallAvailable
                    onClicked: {
                        updateChecker.openDownloadUrl();
                        updateWindow.close();
                    }
                }

                // 立即更新按钮（下载完成状态）
                StyledButton {
                    text: "立即更新 🚀"
                    visible: updateWindow.updateState === "ready"
                    onClicked: {
                        updateChecker.installUpdate();
                    }
                }
            }
        }
    }
}
