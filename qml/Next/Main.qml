import LogAnalyQt.Next as Next
import WorkshopStyle as WorkshopStyle
import QtQuick
import QtQuick.Effects
import QtQuick.Controls
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import Qt.labs.platform
import LogAnalyQt

ApplicationWindow {
    id: mainWindow

    objectName: "nextMainWindow"
    property alias uiTheme: theme
    Binding { target: WorkshopStyle.Theme; property: "appTheme"; value: theme }
    property alias analysisBackend: backend
    property int currentPage: 0
    property bool reflectingHighlight: false
    property string selectedLogHex: ""
    property int selectedLogStart: 0
    function inspectApdu(text) {
        popupWindow.apduHex = text;
        backend.parsePopupApdu(text);
        popupWindow.show();
        popupPane.autoFitCols();
    }
    function navigate(page) {
        currentPage = page;
        if (page < 3) {
            theme.isManualMode = page !== 0;
            theme.isDiffMode = page === 2;
            enableDiffLogic = page === 2;
            backend.clearDiffs();
            backend.clearAllParses();
            if (page === 0 && selectedLogHex) backend.parseHex(selectedLogHex, encodingCombo.currentText, 1);
            if (page > 0 && pane1.manualParseInput.text) performParse(pane1.manualParseInput.text, 1);
            if (page > 1 && pane2.manualParseInput.text) performParse(pane2.manualParseInput.text, 2);
        }
        if (page === 11) helpWindow.readmeText = backend.getReadmeContent();
        if (page === 12) changelogWindow.changelogText = backend.getChangelogContent();
    }
    function filterLog() { backend.applyFilter(regexField.text); }
    function openLogUrl(url) {
        var path = url.toString();
        var split = path.lastIndexOf("/");
        if (split < 0) return;
        folderPathField.text = path.substring(0, split);
        backend.openFolder(folderPathField.text);
        var name = decodeURIComponent(path.substring(split + 1));
        backend.loadFile(name, encodingCombo.currentText);
        fileCombo.currentIndex = backend.fileList.indexOf(name);
        navigate(0);
    }
    TextEdit { id: clipboardBuffer; visible: false }
    FileDialog { id: logFileDialog; title: "打开日志"; nameFilters: ["日志文件 (*.log *.txt)", "所有文件 (*)"]; onAccepted: mainWindow.openLogUrl(file) }
    Shortcut { sequences: [StandardKey.Open]; onActivated: logFileDialog.open() }
    Shortcut { sequences: [StandardKey.Find]; enabled: currentPage === 0; onActivated: searchField.forceActiveFocus() }
    Shortcut { sequence: "F3"; enabled: currentPage === 0; onActivated: logContentLayout.findNext() }
    Shortcut { sequence: "Shift+F3"; enabled: currentPage === 0; onActivated: logContentLayout.findPrev() }
    Connections {
        target: backend
        function onHighlightRange(start, end, modelId) {
            if (mainWindow.currentPage === 0 && modelId === 1 && !mainWindow.isGlobalPreviewMode) {
                mainWindow.reflectingHighlight = true;
                logContentArea.select(mainWindow.selectedLogStart + start, mainWindow.selectedLogStart + end);
                mainWindow.reflectingHighlight = false;
            }
        }
    }

                Timer {
                    id: autoUpdateTimer
                    interval: 3000
                    repeat: false
                    running: !uiSmokeMode
                    onTriggered: {
                        mainWindow.isAutoCheck = true;
                        updateChecker.checkForUpdate();
                    }
                }
                UpdateChecker {
                    id: updateChecker
                    objectName: "updateChecker"
                    onUpdateResultReady: {
                        mainWindow.updateCheckStatus = updateAvailable ? "发现新版本 " + latestVersion : "当前已是最新版本";
                        updateWindow.latestVersion = latestVersion;
                        updateWindow.releaseNotes = releaseNotes;
                        updateWindow.downloadUrl = downloadUrl;
                        updateWindow.isUpdateAvailable = updateAvailable;
                        // 自动检查仅在有新版本时弹窗；手动检查始终弹窗
                        if (!mainWindow.isAutoCheck || updateAvailable) {
                            updateWindow.show();
                        } else if (mainWindow.isAutoCheck && !updateAvailable) {
                            // 自动检查无新版本时，在状态栏提示
                            backend.statusMessage("当前已是最新版本 v" + updateChecker.currentVersion, 3000);
                        }
                        mainWindow.isAutoCheck = false;
                    }
                    onCheckError: errorMessage => {
                        mainWindow.updateCheckStatus = "检查更新失败：" + errorMessage;
                        backend.statusMessage(errorMessage, 3000);
                        mainWindow.isAutoCheck = false;
                    }
                }
    width: 1440
    height: 860
    minimumWidth: 1200
    minimumHeight: 740
    visible: true
    font.family: Qt.platform.os === "osx" ? "PingFang SC" : "Microsoft YaHei UI"
    font.pixelSize: 14
    title: "通联工坊 v" + backend.appVersion
    color: "transparent"
    readonly property bool nativeTitleBar: Qt.platform.os === "osx"
    flags: nativeTitleBar ? Qt.Window | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint
                         : Qt.Window | Qt.FramelessWindowHint
    // Only the sidebar needs space for native controls; fullscreen keeps the system safe area.
    topPadding: nativeTitleBar && visibility !== Window.FullScreen ? 0 : SafeArea.margins.top
    readonly property real windowCornerRadius: nativeTitleBar || visibility === Window.Maximized || visibility === Window.FullScreen ? 0 : 18
    background: Rectangle { color: theme.nativeGlass ? "transparent" : theme.background; radius: mainWindow.windowCornerRadius }
    contentItem.layer.enabled: windowCornerRadius > 0
    contentItem.layer.effect: MultiEffect {
        maskEnabled: true
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
        maskSource: windowMask
    }
    Rectangle {
        id: windowMask
        width: mainWindow.width
        height: mainWindow.height
        radius: mainWindow.windowCornerRadius
        antialiasing: true
        layer.enabled: true
        visible: false
    }

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
        readonly property string folderOpen: String.fromCodePoint(0xF0DCF)
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

    // 设置窗口调色板，使标题栏跟随主题颜色
    palette.window: theme.background
    palette.windowText: theme.text
    palette.base: theme.controlBackground
    palette.text: theme.text
    palette.button: theme.buttonBackground
    palette.buttonText: theme.buttonText
    palette.mid: theme.controlBorder
    palette.dark: theme.groupBorder
    palette.light: theme.highlightEdge
    palette.placeholderText: theme.placeholderText
    palette.brightText: "white"
    palette.highlightedText: "white"
    palette.highlight: theme.accentMuted

    // 窗口初始化完成后设置无边框窗口
    Component.onCompleted: {
        loadMainUiSettings();
        restoreParseWorkspace();
        theme.reducedTransparency = Boolean(backend.getUiSetting("next/appearance/reducedTransparency", false));
        Qt.callLater(ensureMinimumWindowWidth);
        // 延迟调用确保窗口完全创建

    }



    // 主题变化时更新标题栏颜色


    readonly property string appVersion: backend.appVersion
    property bool enableDiffLogic: true
    property bool isGlobalPreviewMode: false
    // 标记本次更新检查是否为启动自动触发
    property bool isAutoCheck: false
    property string updateCheckStatus: ""

    // 用于精确测量字符宽度的组件
    Text {
        id: charMeasurer
        visible: false
        font.family: Next.PlatformFonts.monospaceFamily
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

    function flushPersonalConfig() {
        saveWindowGeometry()
        if (!saveParseWorkspace()) return false
        if (!mqttPage.saveDraft()) return false
        var reply = mqttPage.controller.saveNow()
        return reply && reply.ok
    }
    function saveWindowGeometry() {
        saveMainUiSetting("next/window/x", mainWindow.x);
        saveMainUiSetting("next/window/y", mainWindow.y);
        saveMainUiSetting("next/window/width", mainWindow.width);
        saveMainUiSetting("next/window/height", mainWindow.height);
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
        var savedX = Number(backend.getUiSetting("next/window/x", noSavedPos));
        var savedY = Number(backend.getUiSetting("next/window/y", noSavedPos));
        var savedWidth = Number(backend.getUiSetting("next/window/width", mainWindow.width));
        mainWindow.width = Math.max(savedWidth, mainWindow.minimumWidth);
        mainWindow.height = Number(backend.getUiSetting("next/window/height", mainWindow.height));

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
    property bool installingUpdate: false
    onClosing: close => {
        mqttPage.saveDraft();
        if (!saveParseWorkspace()) {
            close.accepted = false;
            navigate(3);
            return;
        }
        if (!liveLogDialog.saveWorkspace()) {
            close.accepted = false;
            navigate(5);
            return;
        }
        if (liveLogDialog.hasOpenLogFiles()) {
            close.accepted = false;
            liveLogDialog.finishLogsBeforeExit();
            return;
        }
        saveWindowGeometry();
        liveLogDialog.closing();
        if (installingUpdate) {
            if (!updateChecker.installUpdate()) {
                close.accepted = false;
                return;
            }
            Qt.callLater(Qt.quit);
        }
    }
    onCurrentPageChanged: scheduleParseWorkspaceSave()

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
        objectName: "analysisBackend"
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

    GlassTheme { id: theme }

    // 定义统一的颜色过渡动画


    FolderDialog {
        id: folderDialog
        title: "选择日志文件夹"
        onAccepted: {
            folderPathField.text = folderDialog.folder.toString();
            backend.openFolder(folderDialog.folder);
            if (backend.fileList.length) {
                fileCombo.currentIndex = 0;
                backend.loadFile(backend.fileList[0], encodingCombo.currentText);
            }
        }
    }


    // Define a common style for GroupBox to look cleaner
    component StyledGroupBox: GroupBox {
        id: control
        padding: 0
        topPadding: 0
        background: Rectangle { color: theme.surface; radius: 14; border.color: theme.groupBorder
            Rectangle { x: 14; y: 1; width: Math.max(0, parent.width - 28); height: 1; color: theme.highlightEdge }
        }
        label: Label { x: 14; y: 5; text: control.title; font.pixelSize: 15; font.bold: true; color: theme.text; padding: 3 }
    }

    component StyledComboBox: Next.GlassComboBox {
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
                radius: 8
                color: theme.popupBackground
            }
        }
    }

    component StyledButton: Next.GlassButton {
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
                    color: control.highlighted ? "white" : theme.buttonText
                    visible: text !== ""
                    verticalAlignment: Text.AlignVCenter
                }

                Text {
                    text: control.text
                    font: control.font

                    color: control.highlighted ? "white" : theme.buttonText
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }

    }

    component ParsePane: ColumnLayout {
        id: parsePane
        property string frameText: mainWindow.selectedLogHex
        property string titlePrefix: ""
        property string settingsPrefix: ""
        property var resultModel
        property int targetModelId
        property int protocolIndex: 0
        property int encodingIndex: 0
        property string lastParsedText: ""
        property int lastParsedProtocol: 0
        property int lastParsedEncoding: 0
        property alias columnRatio1: parseTable.columnRatio1
        property alias columnRatio2: parseTable.columnRatio2
        onProtocolIndexChanged: mainWindow.scheduleParseWorkspaceSave()
        onEncodingIndexChanged: mainWindow.scheduleParseWorkspaceSave()
        property bool showInput: true
        property alias listView: parseTable.listView
        property alias manualParseInput: manualParseInput
        // 是否锁定列宽（禁用自动调整）
        property bool autoFitLocked: false
        property bool paneSettingsReady: false

        function autoFitCols() {
            parseTable.autoFit();
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
            if (targetModelId >= 4) mainWindow.scheduleParseWorkspaceSave();
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

            var savedR1 = Number(backend.getUiSetting(paneSettingKey("colsR1"), parseTable.columnRatio1));
            var savedR2 = Number(backend.getUiSetting(paneSettingKey("colsR2"), parseTable.columnRatio2));
            if (!isNaN(savedR1) && !isNaN(savedR2) && savedR1 > 0.05 && savedR2 > 0.05 && (savedR1 + savedR2) < 0.95) {
                parseTable.columnRatio1 = savedR1;
                parseTable.columnRatio2 = savedR2;
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
            enabled: showInput && visible
            function onHighlightRange(start, end, modelId) {
                if (modelId === targetModelId) {
                    manualParseInput.select(start, end);
                    manualParseInput.forceActiveFocus();
                }
            }
        }

        spacing: 8

        // 单窗口手动解析支持预置报文，双栏和多窗口页面只显示输入区。
        GridLayout {
            columns: presetCard.visible && width >= 780 ? 2 : 1
            visible: showInput
            Layout.fillWidth: true
            Layout.minimumHeight: implicitHeight
            Layout.preferredHeight: implicitHeight
            Layout.maximumHeight: implicitHeight
            rowSpacing: 8
            columnSpacing: 8

            // 左侧：预置报文选择区
            StyledGroupBox {
                id: presetCard
                objectName: "preset-card-" + targetModelId
                visible: mainWindow.currentPage === 1
                title: titlePrefix + "预置报文选择区"
                implicitHeight: 42 + presetControls.implicitHeight + 12
                Layout.minimumHeight: implicitHeight
                Layout.preferredWidth: 1
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    id: presetControls
                    objectName: "preset-controls-" + targetModelId
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 42
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
                id: inputCard
                objectName: "input-card-" + targetModelId
                title: titlePrefix + "手动报文解析"
                implicitHeight: 42 + Math.max(inputActions.implicitHeight, 112) + 12
                Layout.minimumHeight: implicitHeight
                Layout.preferredWidth: 3
                Layout.fillWidth: true
                Layout.fillHeight: true

                RowLayout {
                    anchors.fill: parent
                    anchors.topMargin: 42
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    anchors.bottomMargin: 10
                    spacing: 5

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        TextArea {
                            id: manualParseInput
                            objectName: "parse-input-" + targetModelId
                            onTextChanged: if (targetModelId >= 4) mainWindow.scheduleParseWorkspaceSave()
                            placeholderText: "在此输入报文..."
                            placeholderTextColor: theme.placeholderText
                            font.family: Next.PlatformFonts.monospaceFamily
                            color: theme.text
                            wrapMode: TextArea.Wrap
                            selectByMouse: true
                            background: Rectangle {
                                color: theme.controlBackground
                                border.color: theme.controlBorder
                                radius: 6
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
                        id: inputActions
                        objectName: "input-actions-" + targetModelId
                        Layout.alignment: Qt.AlignTop
                        Layout.fillWidth: false
                        Layout.maximumWidth: 140
                        spacing: 5
                        StyledButton {
                            text: "清空并粘贴"
                            Layout.fillWidth: true
                            iconChar: mdi.contentPaste
                            onClicked: {
                                manualParseInput.text = "";
                                manualParseInput.paste();
                                performParse(manualParseInput.text, targetModelId);
                            }
                        }
                        StyledButton {
                            text: "解析"
                            Layout.fillWidth: true
                            highlighted: true
                            objectName: "parse-button-" + targetModelId
                            iconChar: mdi.play
                            onClicked: {
                                performParse(manualParseInput.text, targetModelId);
                            }
                        }
                        StyledButton {
                            text: autoFitLocked ? "🔒 列宽已锁定" : "自适应列宽"
                            Layout.fillWidth: true
                            iconChar: autoFitLocked ? "" : mdi.refresh
                            onClicked: {
                                if (autoFitLocked) {
                                    // 解锁并执行自适应
                                    autoFitLocked = false;
                                    parseTable.autoFit();
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
            id: resultCard
            objectName: "result-card-" + targetModelId
            title: titlePrefix + "解析结果"
            Layout.fillWidth: true
            Layout.fillHeight: true

            label: RowLayout {
                x: 14
                y: 5
                width: resultCard.width - 28
                height: 28
                spacing: 8
                Label {
                    objectName: "result-card-title-" + targetModelId
                    Layout.fillWidth: true
                    text: resultCard.title
                    font.pixelSize: 15
                    font.bold: true
                    color: theme.text
                    padding: 3
                }
                Row {
                    objectName: "parse-diff-navigation-" + targetModelId
                    visible: mainWindow.currentPage === 2 && mainWindow.enableDiffLogic && targetModelId <= 2
                    spacing: 6
                    Repeater {
                        model: 2
                        delegate: StyledButton {
                            required property int index
                            objectName: (index === 0 ? "parse-diff-prev-" : "parse-diff-next-") + targetModelId
                            height: 28
                            text: index === 0 ? "上一个差异" : "下一个差异"
                            onClicked: {
                                var currentIdx = parseTable.listView.currentIndex;
                                var diffIdx = index === 0 ? backend.findPrevDiff(currentIdx) : backend.findNextDiff(currentIdx);
                                if (diffIdx !== -1) {
                                    pane1.listView.positionViewAtIndex(diffIdx, ListView.Beginning);
                                    pane2.listView.positionViewAtIndex(diffIdx, ListView.Beginning);
                                    pane1.listView.currentIndex = diffIdx;
                                    pane2.listView.currentIndex = diffIdx;
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: selectedFrame
                x: 12; y: 37; width: parent.width - 24; height: showInput ? 0 : 84
                visible: !showInput; color: "transparent"
                Label { text: "当前选中报文"; color: theme.subText; font.pixelSize: 13 }
                ScrollView {
                    anchors.top: parent.top; anchors.topMargin: 23; width: parent.width; height: 54
                    TextArea { id: selectedFrameText; objectName: "selected-frame-" + targetModelId; text: frameText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 13; color: theme.accent; selectionColor: theme.tableHighlight; background: Rectangle { color: theme.controlBackground; radius: 6 } }
                }
                Connections { target: backend; function onHighlightRange(start, end, modelId) { if (!showInput && modelId === targetModelId) selectedFrameText.select(start,end); } }
            }
            Next.ParseResultTable {
                id: parseTable
                objectName: "parse-table-" + targetModelId
                anchors.top: showInput ? parent.top : selectedFrame.bottom
                anchors.topMargin: showInput ? 42 : 8
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.leftMargin: 10; anchors.rightMargin: 10; anchors.bottomMargin: 10
                backend: mainWindow.analysisBackend
                theme: mainWindow.uiTheme
                resultModel: parsePane.resultModel
                targetModelId: parsePane.targetModelId
                namePrefix: "parse-" + targetModelId
                differenceMode: mainWindow.currentPage === 2 && targetModelId <= 2
                onColumnRatio1Changed: savePaneSetting("colsR1", columnRatio1)
                onColumnRatio2Changed: savePaneSetting("colsR2", columnRatio2)
                onFieldDoubleClicked: (name, value) => {
                    if (name === "明文数据单元") mainWindow.inspectApdu(value);
                }
            }
        }
    }

    function performParse(text, modelId) {
        if (modelId >= 4) {
            for (var i = 0; i < sessionPanes.count; ++i) {
                var pane = sessionPanes.itemAt(i);
                if (pane.targetModelId !== modelId) continue;
                pane.manualParseInput.text = backend.formatHexString(text);
                pane.lastParsedText = pane.manualParseInput.text;
                pane.lastParsedProtocol = pane.protocolIndex;
                pane.lastParsedEncoding = pane.encodingIndex;
                backend.parseHex(pane.manualParseInput.text, ["auto", "UTF-8", "GBK", "ASCII"][pane.encodingIndex], modelId, pane.protocolIndex);
                if (!pane.autoFitLocked) pane.autoFitCols();
                scheduleParseWorkspaceSave();
                return;
            }
            return;
        }
        // 先格式化十六进制字符串
        var formattedText = backend.formatHexString(text);

        // 更新输入框显示为格式化后的文本
        if (modelId === 1 && pane1.manualParseInput) {
            pane1.manualParseInput.text = formattedText;
        } else if (modelId === 2 && pane2.manualParseInput) {
            pane2.manualParseInput.text = formattedText;
        }

        // 使用格式化后的文本进行解析
        backend.parseHex(formattedText, "auto", modelId);

        // 仅在未锁定时自动调整列宽
        if (modelId === 1 && !pane1.autoFitLocked)
            pane1.autoFitCols();
        if (modelId === 2 && !pane2.autoFitLocked)
            pane2.autoFitCols();

        if (mainWindow.currentPage === 2 && mainWindow.enableDiffLogic) {
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

    GlassSurface { anchors.fill: parent; theme: mainWindow.uiTheme }
    Rectangle {
        anchors.fill: parent; z: 1000
        visible: !mainWindow.nativeTitleBar
        radius: mainWindow.windowCornerRadius; color: "transparent"
        border.color: theme.groupBorder; antialiasing: true
    }
    Navigation {
        id: navigation
        targetWindow: mainWindow
        width: mainWindow.currentPage === 15 ? 190 : mainWindow.width < 1340 ? 226 : 244
        anchors.top: customTitleBar.bottom; anchors.bottom: parent.bottom
        theme: mainWindow.uiTheme; currentPage: mainWindow.currentPage; version: mainWindow.appVersion
        onNavigate: page => mainWindow.navigate(page)
    }
    Next.TitleBar {
        id: customTitleBar
        objectName: "main-title-bar"
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: mainWindow.nativeTitleBar ? 0 : 36
        visible: !mainWindow.nativeTitleBar
        titlePixelSize: 14
        targetWindow: mainWindow
        title: mainWindow.title
        titleBarColor: theme.sidebar; titleTextColor: theme.text
        buttonHoverColor: theme.buttonHover
    }
    Item {
        id: pageDeck
        objectName: "page-deck"
        anchors.top: customTitleBar.bottom; anchors.left: navigation.right
        anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: mainWindow.currentPage === 15 ? 12 : 18
    }
    MouseArea {
        objectName: "mac-titlebar-drag"
        visible: mainWindow.nativeTitleBar && mainWindow.visibility !== Window.FullScreen
        anchors.top: parent.top; anchors.left: navigation.right; anchors.right: parent.right
        height: pageDeck.anchors.topMargin
        onPressed: mainWindow.startSystemMove()
        onDoubleClicked: {
            if (mainWindow.visibility === Window.Maximized) mainWindow.showNormal();
            else mainWindow.showMaximized();
        }
    }
    Next.MqttPage {
        id: mqttPage
        backend: mainWindow.analysisBackend
        onRequestConfigManagement: { mainWindow.navigate(9); systemSettingsPage.section = 2 }
        objectName: "page-15"
        parent: pageDeck; anchors.fill: parent
        visible: mainWindow.currentPage === 15
        theme: mainWindow.uiTheme
    }
    Next.AiAssistantPage {
        objectName: "page-14"
        parent: pageDeck
        anchors.fill: parent
        visible: mainWindow.currentPage === 14
        theme: mainWindow.uiTheme
        iconFont: mdiFont.name
        backend: mainWindow.analysisBackend
        onRequestSettings: { mainWindow.navigate(9); systemSettingsPage.showModelSettings(); }
        onFramingResult: function(hexStr, protocolType) {
            backend.protocolType = protocolType
            protocolComboMain.currentIndex = protocolType
            mainWindow.navigate(1)
            performParse(hexStr, 1)
        }
    }
    MouseArea { enabled: !mainWindow.nativeTitleBar; width: 5; anchors.left: parent.left; height: parent.height; cursorShape: Qt.SizeHorCursor; onPressed: mainWindow.startSystemResize(Qt.LeftEdge) }
    MouseArea { enabled: !mainWindow.nativeTitleBar; width: 5; anchors.right: parent.right; height: parent.height; cursorShape: Qt.SizeHorCursor; onPressed: mainWindow.startSystemResize(Qt.RightEdge) }
    MouseArea { enabled: !mainWindow.nativeTitleBar; height: 5; anchors.bottom: parent.bottom; width: parent.width; cursorShape: Qt.SizeVerCursor; onPressed: mainWindow.startSystemResize(Qt.BottomEdge) }

    ColumnLayout {
        id: mainContentLayout
        anchors.fill: pageDeck
        visible: mainWindow.currentPage < 3
        anchors.margins: 0
        spacing: 8

        // --- Top Section: Folder Selection ---


        // --- Control Options ---


        RowLayout {
            Layout.fillWidth: true; spacing: 12
            StyledButton { text: "打开日志"; highlighted: true; iconChar: mdi.folderOpen; visible: !theme.isManualMode; onClicked: logFileDialog.open() }
            StyledButton { text: "文件夹"; visible: !theme.isManualMode; onClicked: folderDialog.open() }
            StyledComboBox {
                id: fileCombo
                objectName: "fileCombo"
                visible: !theme.isManualMode
                Layout.fillWidth: true
                Layout.minimumWidth: 110
                model: backend.fileDisplayList
                onActivated: backend.loadFile(backend.fileList[currentIndex] || "", encodingCombo.currentText)
            }
            Next.GlassTextField { id: folderPathField; visible: false }
            StyledButton { text: "刷新"; visible: !theme.isManualMode; onClicked: backend.openFolder(folderPathField.text) }
            StyledComboBox { id: encodingCombo; objectName: "encodingCombo"; visible: mainWindow.currentPage !== 1; model: ["auto", "UTF-8", "GBK", "ASCII"]; Layout.preferredWidth: 100; onActivated: { saveMainUiSetting("encoding/current", currentText); if (!theme.isManualMode) backend.loadFile(backend.fileList[fileCombo.currentIndex] || "", currentText); } }
            StyledComboBox {
                id: protocolComboMain
                objectName: "protocolCombo"
                model: ["自动检测", "DL/T 698", "IEC 101", "IEC 104", "应用层消息", "模块消息", "Q/GDW 376.2", "DL/T 645", "南网计量上行规约"]
                currentIndex: backend.protocolType
                Layout.preferredWidth: 180
                onActivated: backend.protocolType = currentIndex
                Connections { target: backend; function onProtocolTypeChanged() { protocolComboMain.currentIndex = backend.protocolType; } }
            }
            Item { Layout.fillWidth: true; visible: theme.isManualMode }
            StyledButton { objectName: "globalPreviewButton"; text: isGlobalPreviewMode ? "返回日志" : "全局预览"; visible: !theme.isManualMode; onClicked: { isGlobalPreviewMode = !isGlobalPreviewMode; if (isGlobalPreviewMode) backend.scanGlobalPreview(); } }
        }
        RowLayout {
            Layout.fillWidth: true; visible: !theme.isManualMode; spacing: 10
            Next.GlassTextField { id: searchField; objectName: "logSearch"; Layout.fillWidth: true; placeholderText: "搜索日志内容"; color: theme.text; placeholderTextColor: theme.placeholderText;  onAccepted: logContentLayout.findNext() }
            StyledButton { text: "上一处"; onClicked: logContentLayout.findPrev() }
            StyledButton { text: "下一处"; onClicked: logContentLayout.findNext() }
            Next.GlassTextField { id: regexField; objectName: "logFilter"; Layout.fillWidth: true; placeholderText: "正则过滤"; color: theme.text; placeholderTextColor: theme.placeholderText;  onAccepted: mainWindow.filterLog() }
            StyledButton { text: "过滤"; onClicked: mainWindow.filterLog() }
        }
        // --- Main Content Area (Split View) ---
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 10
                radius: 8
                color: SplitHandle.pressed ? theme.splitHandlePressed : (SplitHandle.hovered ? theme.splitHandleHover : theme.splitHandle)
            }

            // Left Side: Log Content
            StyledGroupBox {
                title: mainWindow.isGlobalPreviewMode ? "概要预览" : "日志内容"
                visible: !theme.isManualMode
                SplitView.preferredWidth: parent.width * 0.53
                SplitView.fillHeight: true

                Row { anchors.top: parent.top; anchors.topMargin: 4; anchors.right: parent.right; anchors.rightMargin: 12; spacing: 6; StyledButton { text: "清空并粘贴"; iconChar: mdi.contentPaste; onClicked: { clipboardBuffer.clear(); clipboardBuffer.paste(); if (clipboardBuffer.text.length) logContentArea.text = clipboardBuffer.text; } } StyledButton { text: "复制全文"; onClicked: backend.copyToClipboard(logContentArea.text) } }
                ColumnLayout {
                    id: logContentLayout
                    anchors.fill: parent
                    anchors.topMargin: 42
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


                    // Log Text Area
                    ScrollView {
                        id: logScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        FontMetrics { id: logFontMetrics; font: logContentArea.font }
                        Item {
                            parent: logScroll
                            x: 1; y: 1; width: 44; height: logScroll.height - 16
                            clip: true; z: 2
                            Rectangle { anchors.fill: parent; color: theme.controlBackground }
                            Repeater {
                                model: Math.ceil(logScroll.height / logFontMetrics.height) + 1
                                delegate: Text {
                                    required property int index
                                    readonly property real scrollY: logScroll.contentItem.contentY || 0
                                    readonly property int line: Math.floor(scrollY / logFontMetrics.height) + index
                                    x: 5; y: line * logFontMetrics.height - scrollY + logContentArea.topPadding
                                    width: 32; height: logFontMetrics.height
                                    text: line < logContentArea.lineCount ? line + 1 : ""
                                    color: theme.subText; font: logContentArea.font
                                    horizontalAlignment: Text.AlignRight
                                }
                            }
                        }
                        TextArea {
                            id: logContentArea
                            objectName: "logText"
                            textFormat: TextEdit.PlainText
                            leftPadding: 54
                            wrapMode: TextEdit.NoWrap
                            font.pixelSize: 14
                            Component.onCompleted: { backend.disableTextAreaUndo(textDocument); backend.styleLogDocument(textDocument); }

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
                                        mainWindow.openLogUrl(drop.urls[0]);
                                    }
                                }

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 10
                                    visible: logContentArea.text.length === 0

                                    Text {
                                        text: mdi.scriptText
                                        font.family: mdiFont.name
                                        font.pixelSize: 52
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
                                        text: "也可粘贴日志文本，双击报文即可解析"
                                        font.pixelSize: 14
                                        color: "#aaaaaa"
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                }
                            }
                            text: ""
                            readOnly: false
                            selectByMouse: true
                            font.family: Next.PlatformFonts.monospaceFamily
                            color: theme.text
                            background: Rectangle {
                                color: theme.controlBackground
                                border.color: theme.controlBorder
                                radius: 6
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
                                if (!mainWindow.reflectingHighlight && !receivingBackend) { mainWindow.selectedLogHex = selectedText; mainWindow.selectedLogStart = selectionStart; backend.parseHex(selectedText, encodingCombo.currentText); }
                            }

                            property bool receivingBackend: false
                            onTextChanged: { if (!receivingBackend) backend.loadRawText(text); }

                            Connections {
                                target: backend
                                function onCurrentFileContentChanged() {
                                    logContentArea.receivingBackend = true;
                                    mainWindow.selectedLogHex = "";
                                    mainWindow.selectedLogStart = 0;
                                    backend.clearAllParses();
                                    logContentArea.text = backend.currentFileContent;
                                    logContentArea.receivingBackend = false;
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
                    anchors.topMargin: 42
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
                        objectName: "globalPreviewList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: backend.globalPreviewModel

                        delegate: Rectangle {
                            objectName: "global-preview-row-" + index
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
                                        mainWindow.selectedLogHex = model.rawData;
                                        backend.parseHex(model.rawData, "auto", 1, 0);
                                    }
                                }
                            }

                            Next.GlassMenu {
                                id: contextMenu
                                Next.GlassMenuItem {
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
                                    font.family: Next.PlatformFonts.monospaceFamily
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
                                    font.family: Next.PlatformFonts.monospaceFamily
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
                                    font.family: Next.PlatformFonts.monospaceFamily
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
                                    font.family: Next.PlatformFonts.monospaceFamily
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
                                    font.family: Next.PlatformFonts.monospaceFamily
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
                                    font.family: Next.PlatformFonts.monospaceFamily
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
                SplitView.preferredWidth: parent.width * 0.47
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
            Layout.preferredHeight: statusRows.implicitHeight + 8
            color: "transparent"
            RowLayout {
                id: statusRows
                objectName: "parse-status-bar"
                anchors.left: parent.left; anchors.right: parent.right
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
                    function onParseResultChanged() {
                        if (mainWindow.currentPage !== 1 && mainWindow.currentPage !== 2) return;
                        statusTimer.stop();
                        statusLabel.text = backend.parseResult || "就绪";
                    }
                    function onStatusMessage(message, timeout) {
                        statusLabel.text = message;
                        statusLabel.color = theme.text; // Highlight warning messages
                        statusTimer.interval = timeout;
                        statusTimer.restart();
                    }
                }

                // 协议状态显示
                Label {
                    objectName: "parse-protocol-status"
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: mainContentLayout.width * 0.4
                    elide: Text.ElideRight
                    text: backend.protocolType === 0 ? ("检测协议: " + backend.currentProtocolName) : ("当前协议: " + backend.currentProtocolName)
                    font.pixelSize: 12
                    color: backend.currentProtocolName === "自动检测" ? theme.subText : theme.accent
                    visible: backend.currentProtocolName !== "自动检测"
                }
                Label {
                    id: statusLabel
                    objectName: "parseStatus"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                    text: "就绪"
                    font.pixelSize: 12
                    color: theme.text
                }

                RowLayout {
                    objectName: "parse-address-controls"
                    spacing: 10
                    visible: backend.currentProtocolName.indexOf("698") !== -1

                    Label {
                        text: "地址:"
                        font.pixelSize: 12
                        color: theme.text
                    }

                    Next.GlassTextField {
                        id: addrEdit698
                        placeholderText: ""
                        Layout.preferredWidth: 140
                        font.pixelSize: 12
                        color: theme.text
                    }

                    Label {
                        text: "逻辑地址:"
                        font.pixelSize: 12
                        color: theme.text
                    }

                    Next.GlassTextField {
                        id: logicAddrEdit698
                        placeholderText: ""
                        text: ""
                        Layout.preferredWidth: 52
                        font.pixelSize: 12
                        color: theme.text
                    }

                    StyledButton {
                        text: "更改"
                        Layout.preferredHeight: 30
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
            }
        }
    }

    ListModel { id: parseSessions }
    readonly property var activeParsePane: parseTabs.currentIndex >= 0 && sessionPanes.count > parseTabs.currentIndex
        ? sessionPanes.itemAt(parseTabs.currentIndex) : null
    property bool parseWorkspaceReady: false
    property bool parseWorkspaceWritable: true
    property string parseWorkspaceError: ""
    Timer { id: parseSaveTimer; interval: 500; onTriggered: mainWindow.saveParseWorkspace() }
    function scheduleParseWorkspaceSave() {
        if (parseWorkspaceReady) parseSaveTimer.restart();
    }
    function saveParseWorkspace() {
        if (!parseWorkspaceReady) return true;
        if (!parseWorkspaceWritable) return false;
        parseSaveTimer.stop();
        var tabs = [];
        for (var i = 0; i < sessionPanes.count; ++i) {
            var pane = sessionPanes.itemAt(i);
            tabs.push({text: pane.manualParseInput.text, parsedText: pane.lastParsedText,
                protocol: pane.protocolIndex, encoding: pane.encodingIndex,
                parsedProtocol: pane.lastParsedProtocol, parsedEncoding: pane.lastParsedEncoding,
                locked: pane.autoFitLocked, r1: pane.columnRatio1, r2: pane.columnRatio2});
        }
        var saved = backend.setUiSetting("next/multiParse/workspace", JSON.stringify({version: 1,
            tabs: tabs, currentIndex: parseTabs.currentIndex, open: currentPage === 3}));
        parseWorkspaceError = saved ? "" : "解析页保存失败，请检查程序目录是否可写、磁盘是否有剩余空间。内容尚在当前窗口，请勿强制退出。";
        return saved;
    }
    function restoreParseWorkspace() {
        var state = null;
        var raw = backend.getUiSetting("next/multiParse/workspace", "");
        if (raw !== "") {
            try {
                state = JSON.parse(raw);
                if (!state || state.version !== 1 || !Array.isArray(state.tabs)
                        || !Number.isInteger(state.currentIndex) || typeof state.open !== "boolean")
                    throw new Error("Invalid workspace");
                for (var tab of state.tabs) {
                    if (!tab || typeof tab.text !== "string" || typeof tab.parsedText !== "string"
                            || !Number.isInteger(tab.protocol) || tab.protocol < 0 || tab.protocol > 8
                            || !Number.isInteger(tab.parsedProtocol) || tab.parsedProtocol < 0 || tab.parsedProtocol > 8
                            || !Number.isInteger(tab.encoding) || tab.encoding < 0 || tab.encoding > 3
                            || !Number.isInteger(tab.parsedEncoding) || tab.parsedEncoding < 0 || tab.parsedEncoding > 3
                            || typeof tab.locked !== "boolean" || !Number.isFinite(tab.r1) || !Number.isFinite(tab.r2)
                            || tab.r1 <= 0 || tab.r2 <= 0 || tab.r1 + tab.r2 >= 1)
                        throw new Error("Invalid parse tab");
                }
            } catch (error) {
                state = null;
                parseWorkspaceWritable = backend.setUiSetting("next/multiParse/recovery", raw);
                parseWorkspaceError = parseWorkspaceWritable
                    ? "上次的解析页配置无法读取，已保留原始备份，本次打开 5 个空白页。"
                    : "解析页配置无法读取且备份失败。为保护原始内容，已停止保存，请检查程序目录权限。";
            }
        }
        var count = state ? state.tabs.length : 5;
        for (var i = 0; i < count; ++i) {
            addParseTab();
            if (!state) continue;
            var pane = sessionPanes.itemAt(i);
            var saved = state.tabs[i];
            pane.protocolIndex = saved.protocol;
            pane.encodingIndex = saved.encoding;
            pane.lastParsedText = saved.parsedText;
            pane.lastParsedProtocol = saved.parsedProtocol;
            pane.lastParsedEncoding = saved.parsedEncoding;
            backend.parseHex(saved.parsedText, ["auto", "UTF-8", "GBK", "ASCII"][saved.parsedEncoding], pane.targetModelId, saved.parsedProtocol);
            pane.manualParseInput.text = saved.text;
            pane.autoFitLocked = saved.locked;
            pane.columnRatio1 = saved.r1;
            pane.columnRatio2 = saved.r2;
        }
        parseTabs.currentIndex = count ? Math.max(0, Math.min(state ? state.currentIndex : 0, count - 1)) : -1;
        if (state && state.open) navigate(3);
        parseWorkspaceReady = true;
    }
    function addParseTab() {
        parseSessions.append({parseId: backend.createParseSession()});
        parseTabs.currentIndex = parseSessions.count - 1;
        scheduleParseWorkspaceSave();
    }
    function closeParseTab(index) {
        if (index < 0 || index >= parseSessions.count) return;
        var modelId = parseSessions.get(index).parseId;
        var current = parseTabs.currentIndex;
        parseSessions.remove(index);
        backend.closeParseSession(modelId);
        parseTabs.currentIndex = parseSessions.count ? Math.max(0, Math.min(current - (index < current ? 1 : 0), parseSessions.count - 1)) : -1;
        saveParseWorkspace();
    }
    ColumnLayout {
        id: multiParseWorkspace
        objectName: "multiParseWorkspace"
        anchors.fill: pageDeck
        visible: mainWindow.currentPage === 3
        spacing: 8
        Rectangle {
            objectName: "multiParseToolbar"
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            radius: 10
            color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.08)
            border.color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.18)
        RowLayout {
            anchors.fill: parent
            anchors.margins: 6
            StyledButton {
                objectName: "previousParseTab"
                text: "‹"
                Layout.preferredWidth: 36
                visible: parseTabs.contentWidth > parseTabs.width
                enabled: parseTabs.currentIndex > 0
                Accessible.name: "上一个解析页"
                onClicked: parseTabs.currentIndex -= 1
            }
            TabBar {
                id: parseTabs
                objectName: "parseTabs"
                Layout.fillWidth: true
                clip: true
                onCurrentIndexChanged: mainWindow.scheduleParseWorkspaceSave()
                background: Rectangle { color: "transparent" }
                Repeater {
                    model: parseSessions
                    TabButton {
                        id: tabButton
                        required property int index
                        objectName: "parse-tab-" + index
                        text: "解析 " + (index + 1)
                        width: 116
                        implicitHeight: 38
                        rightPadding: 24
                        contentItem: Text {
                            text: parent.text
                            color: parent.checked ? theme.text : theme.subText
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 6
                            color: parent.checked ? theme.tableHighlight : theme.controlBackground
                            border.color: parent.checked ? theme.accent : theme.controlBorder
                        }
                        ToolButton {
                            objectName: "close-parse-tab-" + tabButton.index
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 2
                            width: 20
                            height: 20
                            Accessible.name: "关闭" + tabButton.text
                            contentItem: Text { text: "×"; font.pixelSize: 16; color: theme.subText; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { radius: 4; color: parent.hovered ? theme.buttonHover : "transparent" }
                            onClicked: mainWindow.closeParseTab(tabButton.index)
                        }
                    }
                }
            }
            StyledButton {
                objectName: "nextParseTab"
                text: "›"
                Layout.preferredWidth: 36
                visible: parseTabs.contentWidth > parseTabs.width
                enabled: parseTabs.currentIndex < parseTabs.count - 1
                Accessible.name: "下一个解析页"
                onClicked: parseTabs.currentIndex += 1
            }
            StyledButton {
                objectName: "addParseTab"
                text: "+"
                Layout.preferredWidth: 40
                Accessible.name: "新建解析窗口"
                ToolTip.visible: hovered
                ToolTip.text: "新建解析窗口"
                onClicked: mainWindow.addParseTab()
            }
            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 24
                Layout.leftMargin: 4
                Layout.rightMargin: 4
                color: theme.controlBorder
            }
            StyledComboBox {
                objectName: "sessionEncoding"
                model: ["auto", "UTF-8", "GBK", "ASCII"]
                Layout.preferredWidth: 90
                enabled: mainWindow.activeParsePane !== null
                currentIndex: mainWindow.activeParsePane ? mainWindow.activeParsePane.encodingIndex : 0
                onActivated: index => { if (mainWindow.activeParsePane) mainWindow.activeParsePane.encodingIndex = index; }
                Accessible.name: "当前解析页编码"
            }
            StyledComboBox {
                objectName: "sessionProtocol"
                model: protocolComboMain.model
                Layout.preferredWidth: 160
                enabled: mainWindow.activeParsePane !== null
                currentIndex: mainWindow.activeParsePane ? mainWindow.activeParsePane.protocolIndex : 0
                onActivated: index => { if (mainWindow.activeParsePane) mainWindow.activeParsePane.protocolIndex = index; }
                Accessible.name: "当前解析页协议"
            }
        }
        }
        Label {
            Layout.fillWidth: true
            visible: mainWindow.parseWorkspaceError !== ""
            text: mainWindow.parseWorkspaceError
            color: theme.error
            wrapMode: Text.Wrap
        }
        Label {
            visible: parseSessions.count === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: "暂无解析页，点击上方 ＋ 新建"
            color: theme.subText
        }
        StackLayout {
            visible: parseSessions.count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: parseTabs.currentIndex
            Repeater {
                id: sessionPanes
                model: parseSessions
                ParsePane {
                    required property int parseId
                    objectName: "session-pane-" + parseId
                    targetModelId: parseId
                    resultModel: backend.parseModel(parseId)
                }
            }
        }
    }

    GlassPage {
        id: helpWindow
        objectName: "page-11"
        title: "功能说明 - 通联工坊 v" + mainWindow.appVersion
        parent: pageDeck
        anchors.fill: parent
        theme: mainWindow.uiTheme
        visible: mainWindow.currentPage === 11
        onRequested: mainWindow.navigate(11)
        onClosed: mainWindow.navigate(10)
        color: theme.surface

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

    GlassPage {
        id: changelogWindow
        objectName: "page-12"
        title: "升级日志 - 通联工坊 v" + mainWindow.appVersion
        parent: pageDeck
        anchors.fill: parent
        theme: mainWindow.uiTheme
        visible: mainWindow.currentPage === 12
        onRequested: mainWindow.navigate(12)
        onClosed: mainWindow.navigate(10)
        color: theme.surface

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
        objectName: "apduInspection"
        palette: mainWindow.palette
        property string apduHex: ""
        width: 800
        height: 600
        flags: Qt.Window | Qt.FramelessWindowHint
        title: "明文 APDU 解析 - 通联工坊 v" + mainWindow.appVersion
        visible: false
        color: theme.background
        Next.TitleBar {
            id: apduTitle
            width: parent.width
            title: "明文 APDU 解析"
            targetWindow: popupWindow
            titleBarColor: theme.background
            titleTextColor: theme.text
        }

        ParsePane {
            id: popupPane
            anchors.top: apduTitle.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10
            frameText: popupWindow.apduHex
            titlePrefix: "APDU-"
            targetModelId: 3
            resultModel: backend.popupResultModel
            showInput: false
        }
    }

    // 预置报文管理窗口
    GlassPage {
        id: presetManagerWindow
        objectName: "page-7"
        title: "预置报文管理"
        parent: pageDeck
        anchors.fill: parent
        theme: mainWindow.uiTheme
        visible: mainWindow.currentPage === 7
        onRequested: mainWindow.navigate(7)
        onClosed: mainWindow.navigate(10)
        color: theme.surface

        // 窗口边框
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: theme.controlBorder // 使用主题边框色
            border.width: 1
            z: 200 // 确保在最上层
        }


        // 自定义标题栏
        Item { id: presetTitleBar; width: parent.width; height: 0 }

        // 边缘缩放区域 - 预置报文管理窗口
        // 左边缘

        // 右边缘

        // 下边缘

        // 左下角

        // 右下角

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
                refreshExistingList();
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
                    anchors.topMargin: 42
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

                        Next.GlassTextField {
                            id: newSummaryInput
                            Layout.fillWidth: true
                            placeholderText: "输入报文名称/概述..."
                            placeholderTextColor: theme.placeholderText
                            color: theme.text
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
                                font.family: Next.PlatformFonts.monospaceFamily
                                wrapMode: TextArea.Wrap
                                background: Rectangle {
                                    color: theme.controlBackground
                                    border.color: theme.controlBorder
                                    radius: 8
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
                    anchors.topMargin: 42
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
                            height: 48
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
                                    Layout.preferredHeight: 42
                                    onClicked: {
                                        presetManagerWindow.enterEditMode(modelData);
                                    }
                                }

                                StyledButton {
                                    text: "删除"
                                    Layout.preferredWidth: 60
                                    Layout.preferredHeight: 42
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

    Next.ProtocolSyncDialog {
        id: protocolSyncDialog
        objectName: "page-8"
        parent: pageDeck
        anchors.fill: parent
        visible: mainWindow.currentPage === 8
        onRequested: mainWindow.navigate(8)
        onClosed: mainWindow.navigate(0)
        ownerWindow: mainWindow
        theme: mainWindow.uiTheme
        syncManager: backend.syncManager
        promptSyncManager: backend.promptSyncManager
    }

    Next.GlassDialog {
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
                color: "#2b1d12"
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
            color: theme.popupBackground
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

    Next.LogMergeDialog {
        id: logMergeDialog
        objectName: "page-6"
        parent: pageDeck
        anchors.fill: parent
        visible: mainWindow.currentPage === 6
        onRequested: mainWindow.navigate(6)
        onClosed: mainWindow.navigate(0)
        ownerWindow: mainWindow
        theme: mainWindow.uiTheme
    }

    Next.SshMonitorPage {
        id: liveLogDialog
        objectName: "page-5"
        parent: pageDeck
        anchors.fill: parent
        visible: mainWindow.currentPage === 5
        onRequested: mainWindow.navigate(5)
        onClosed: mainWindow.navigate(0)
        onLogShutdownReady: function(success) {
            if (success) Qt.callLater(mainWindow.close);
            else mainWindow.navigate(5);
        }
        ownerWindow: mainWindow
        theme: mainWindow.uiTheme
        backend: backend
    }

    Next.SmartFramingDialog {
        id: smartFramingDialog
        objectName: "page-4"
        parent: pageDeck
        anchors.fill: parent
        visible: mainWindow.currentPage === 4
        onRequested: mainWindow.navigate(4)
        onClosed: mainWindow.navigate(0)
        ownerWindow: mainWindow
        theme: mainWindow.uiTheme
        backend: backend
        onFramingResult: function(hexStr, protocolType) {
            backend.protocolType = protocolType
            protocolComboMain.currentIndex = protocolType
            mainWindow.navigate(1)
            performParse(hexStr, 1)
        }
    }

    // 更新结果窗口（支持程序内下载与自动更新）
    GlassPage {
        id: updateWindow
        objectName: "page-13"
        parent: pageDeck
        anchors.fill: parent
        theme: mainWindow.uiTheme
        visible: mainWindow.currentPage === 13
        onRequested: mainWindow.navigate(13)
        onClosed: mainWindow.navigate(10)
        title: isUpdateAvailable ? "发现新版本" : "检查更新"

        property string latestVersion: ""
        property string releaseNotes: ""
        property string downloadUrl: ""
        property bool isUpdateAvailable: false

        // UI 状态: "idle" | "downloading" | "verifying" | "ready" | "error"
        property string updateState: "idle"
        property string errorMsg: ""

        color: "transparent" // Set to transparent to allow resizing handles interaction if outside


        // 窗口边框
        Rectangle {
            anchors.fill: parent
            color: theme.surface
            radius: 14
            border.color: theme.groupBorder
            border.width: 1
            z: 0 // Background at bottom
        }

        // 自定义标题栏
        Item { id: updateTitleBar; width: parent.width; height: 0 }

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

        // === 边缘缩放 MouseAreas ===
        // 左边
        // 右边
        // 上边
        // 下边
        // 左上角
        // 右上角
        // 左下角
        // 右下角

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
                    text: updateWindow.isUpdateAvailable ? "发现新版本 " + updateWindow.latestVersion : updateWindow.latestVersion.length ? "当前已是最新版本" : "检查版本更新"
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
                color: theme.controlBorder
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
                        radius: 8
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
                            radius: 8
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
                            radius: 8
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
                text: "下载完成，校验通过。更新时会保存工作区，并自动重新打开软件。"
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
                    text: "立即更新并重启"
                    visible: updateWindow.updateState === "ready"
                    onClicked: {
                        mainWindow.installingUpdate = true;
                        mainWindow.close();
                        mainWindow.installingUpdate = false;
                    }
                }
            }
        }
    }

    Next.SystemSettingsPage {
        id: systemSettingsPage
        flushPersonalConfig: function() { return mainWindow.flushPersonalConfig() }
        objectName: "page-9"
        parent: pageDeck; anchors.fill: parent
        theme: mainWindow.uiTheme
        backend: mainWindow.analysisBackend
        visible: mainWindow.currentPage === 9
    }
    GlassPage {
        objectName: "page-10"
        parent: pageDeck; anchors.fill: parent
        theme: mainWindow.uiTheme
        visible: mainWindow.currentPage === 10
        ColumnLayout {
            anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 28; spacing: 22
            RowLayout {
                spacing: 20
                Image { source: "assets/app-logo.png"; Layout.preferredWidth: 76; Layout.preferredHeight: 76 }
                Column { spacing: 8; Label { objectName: "help-app-name"; text: "通联工坊"; color: theme.text; font.pixelSize: 25; font.bold: true } Label { text: "v" + mainWindow.appVersion; color: theme.subText; font.pixelSize: 16 } }
            }
            Label { objectName: "help-app-subtitle"; text: "通信开发与调试工作台"; color: theme.subText; font.pixelSize: 16 }
            RowLayout {
                spacing: 16
                StyledButton { text: "功能说明"; onClicked: mainWindow.navigate(11) }
                StyledButton { text: "升级日志"; onClicked: mainWindow.navigate(12) }
                StyledButton { text: updateChecker.checking ? "正在检查…" : "检查更新"; enabled: !updateChecker.checking; onClicked: { mainWindow.updateCheckStatus = "正在检查更新…"; mainWindow.isAutoCheck = false; updateChecker.checkForUpdate(); } }
            }
            Label { text: mainWindow.updateCheckStatus; visible: text.length > 0; color: theme.subText; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
}
