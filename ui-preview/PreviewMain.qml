import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import "PreviewData.js" as Data

ApplicationWindow {
    id: window
    objectName: "previewWindow"
    width: 1440
    height: 800
    minimumWidth: 1100
    minimumHeight: 680
    visible: true
    title: "多协议解析器 · 界面预览"
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint
    font.family: Qt.platform.os === "osx" ? "PingFang SC" : "Microsoft YaHei UI"
    font.pixelSize: 16

    property var lines: Data.sampleLines.slice()
    property var shownLines: Data.entries(lines, "")
    property int selectedLine: 2
    property string filename: "Terminal3.log"
    property bool isSample: true
    property string searchText: ""
    property string filterText: ""
    property string filterError: ""
    property bool overview: false
    property bool solidSurfaces: false
    property int byteOffset: 6
    property int byteLength: 4
    property string selectedField: "OAD"
    property var collapsed: ({})
    property string status: "就绪"
    readonly property string mono: Qt.platform.os === "osx" ? "Menlo" : "Consolas"
    readonly property var bytes: selectedLine >= 0 ? Data.hex(lines[selectedLine] || "") : []
    readonly property var resultRows: Data.visibleFields(Data.resultFields(lines[selectedLine] || "", isSample), collapsed)
    readonly property var overviewRows: shownLines.filter(function(entry) { return Data.hex(entry.text).length > 0; })

    function selectLine(line) {
        selectedLine = line;
        byteOffset = -1;
        byteLength = 0;
        selectedField = "";
        collapsed = ({});
    }
    function applyFilter(pattern) {
        try {
            var next = Data.entries(lines, pattern);
            filterError = "";
            filterText = pattern;
            shownLines = next;
            if (!next.some(function(entry) { return entry.line === selectedLine; })) selectLine(-1);
            status = pattern ? "筛选后 " + next.length + " 行" : "就绪";
        } catch (error) {
            filterError = "正则表达式无效，请检查后重试";
        }
    }
    function find(backwards) {
        var line = Data.find(shownLines, searchText, selectedLine, backwards);
        if (line < 0) { status = "未找到匹配内容"; return; }
        selectLine(line);
        var row = shownLines.findIndex(function(entry) { return entry.line === line; });
        logList.positionViewAtIndex(row, ListView.Contain);
        status = "已定位至第 " + (line + 1) + " 行";
    }
    function loadSample(empty) {
        lines = empty ? [] : Data.sampleLines.slice();
        filename = empty ? "未打开日志" : "Terminal3.log";
        isSample = true;
        selectLine(empty ? -1 : 2);
        searchInput.clear();
        filterInput.clear();
        applyFilter("");
        overview = false;
    }
    function loadPasted(text) {
        lines = text.replace(/\r\n?/g, "\n").split("\n");
        filename = "剪贴板文本";
        isSample = false;
        selectLine(-1);
        filterInput.clear();
        searchInput.clear();
        applyFilter("");
        overview = false;
        status = "已粘贴 · 仅查看原文，尚未接入解析引擎";
    }
    function selectField(row) {
        selectedField = row.name;
        byteOffset = row.offset;
        byteLength = row.length;
    }
    function toggleGroup(group) {
        var next = Object.assign({}, collapsed);
        next[group] = !next[group];
        collapsed = next;
    }
    function copyText(value) {
        clipboardBuffer.text = value;
        clipboardBuffer.selectAll();
        clipboardBuffer.copy();
        status = "已复制";
    }
    function maximize() {
        if (visibility === Window.Maximized) showNormal(); else showMaximized();
    }

    TextEdit { id: clipboardBuffer; visible: false }
    FontMetrics { id: logMetrics; font.family: window.mono; font.pixelSize: 15 }
    Shortcut { sequences: [StandardKey.Find]; onActivated: searchInput.forceActiveFocus() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: samples.open() }
    Shortcut { sequence: "F3"; onActivated: window.find(false) }
    Shortcut { sequence: "Shift+F3"; onActivated: window.find(true) }

    Item {
        id: canvas
        readonly property real uiScale: Math.max(0.88, Math.min(1.0, window.width / 1632))
        width: window.width / uiScale
        height: window.height / uiScale
        scale: uiScale
        transformOrigin: Item.TopLeft
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
        }
        Rectangle {
            id: mask
            width: canvas.width
            height: canvas.height
            radius: 20
            layer.enabled: true
            visible: false
        }
        Image {
            id: backdrop
            anchors.fill: parent
            source: "assets/glass-background.png"
            fillMode: Image.PreserveAspectCrop
            visible: false
        }
        MultiEffect { anchors.fill: parent; source: backdrop; blurEnabled: true; blurMax: 48; blur: 0.75; brightness: 0.045; saturation: -0.08 }
        Rectangle { anchors.fill: parent; color: window.solidSurfaces ? "#1b222b" : "#4c1c2125" }

        Rectangle {
            id: sidebar
            width: canvas.width < 1400 ? 254 : 316
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            color: window.solidSurfaces ? "#263748" : "#643e4854"
            Rectangle { width: 1; color: "#55758ba4"; anchors.right: parent.right; height: parent.height }
            Row {
                x: 23; y: 22; spacing: 13
                Repeater {
                    model: ["#ff615a", "#ffbd44", "#29ca42"]
                    delegate: Button {
                        required property string modelData
                        required property int index
                        width: 16; height: 16
                        Accessible.name: ["关闭预览", "最小化", "最大化"][index]
                        background: Rectangle { radius: 8; color: parent.modelData; opacity: parent.down ? 0.7 : 1 }
                        onClicked: { if (index === 0) window.close(); else if (index === 1) window.showMinimized(); else window.maximize(); }
                    }
                }
            }
            MouseArea { x: 120; y: 0; width: parent.width - 120; height: 56; onPressed: window.startSystemMove(); onDoubleClicked: window.maximize() }
            RowLayout {
                x: 23; y: 65; spacing: 16
                Image { source: "assets/app-logo.png"; Layout.preferredWidth: 55; Layout.preferredHeight: 55; smooth: true }
                Column {
                    spacing: 4
                    Text { text: "多协议解析器"; font.pixelSize: 22; font.weight: Font.Bold; color: "#f1f5fc" }
                    Text { text: "LogAnalyQt"; font.pixelSize: 15; color: "#aebed0" }
                }
            }
            Flickable {
                x: 16; y: 145
                width: parent.width - 32; height: parent.height - y - 150
                contentHeight: navColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                Column {
                    id: navColumn
                    width: parent.width
                    spacing: 4
                    Repeater {
                        model: [
                            {title:"分析工作台"},
                            {label:"日志解析",icon:0xF09EE,active:true},
                            {label:"手动解析",icon:0xF0CB6},
                            {label:"差异对比",icon:0xF1492},
                            {label:"双窗口解析",icon:0xF0BCC},
                            {label:"智能组帧",icon:0xF09FE},
                            {title:"日志工具"},
                            {label:"实时监控",icon:0xF0430},
                            {label:"日志合并",icon:0xF09EE},
                            {title:"资源管理"},
                            {label:"预置报文",icon:0xF09EE},
                            {label:"协议同步",icon:0xF04E1}
                        ]
                        delegate: Item {
                            required property var modelData
                            required property int index
                            width: navColumn.width
                            height: modelData.title ? (index === 0 ? 28 : 58) : 47
                            Text {
                                visible: !!modelData.title
                                x: 7; anchors.bottom: parent.bottom; anchors.bottomMargin: 9
                                text: modelData.title || ""; color: "#a9b8cb"; font.pixelSize: 16
                            }
                            Button {
                                visible: !modelData.title
                                anchors.fill: parent
                                hoverEnabled: true
                                Accessible.name: modelData.label || ""
                                background: Rectangle {
                                    radius: 10
                                    color: modelData.active ? "#a4356fac" : parent.hovered ? "#304f657e" : "transparent"
                                    border.width: modelData.active ? 1 : 0
                                    border.color: "#a45c9dea"
                                    Rectangle { visible: !!modelData.active; x: 0; y: 13; height: 21; width: 3; radius: 2; color: "#68adff" }
                                }
                                contentItem: RowLayout {
                                    spacing: 18
                                    Glyph { code: modelData.icon || 0; Layout.leftMargin: 9; color: modelData.active ? "#ffffff" : "#c5d5e8"; font.pixelSize: 26 }
                                    Text { text: modelData.label || ""; font.pixelSize: 18; font.weight: modelData.active ? Font.DemiBold : Font.Normal; color: "#dce6f4"; Layout.fillWidth: true }
                                }
                                onClicked: { if (modelData.active) window.overview = false; else { notice.text = modelData.label + "将在后续阶段接入。本次可体验日志解析首页。"; notice.open(); } }
                                ToolTip.visible: hovered && !modelData.active
                                ToolTip.text: "后续接入"
                                ToolTip.delay: 500
                            }
                        }
                    }
                }
            }
            Column {
                anchors.bottom: parent.bottom; anchors.bottomMargin: 24
                x: 18; width: parent.width - 36; spacing: 7
                Rectangle { width: parent.width; height: 1; color: "#3c8da1b7" }
                GlassButton { text: "外观设置"; quiet: true; iconCode: 0xF08BB; width: parent.width; onClicked: appearance.open() }
                GlassButton { text: "帮助与更新"; quiet: true; iconCode: 0xF0625; width: parent.width; onClicked: { notice.text = "日志解析交互预览\n\n选择日志行查看字段，点击字段高亮对应字节。\n⌘/Ctrl+F 搜索，F3 查找下一个。\n本预览不联网、不检查更新、不修改原应用配置。"; notice.open(); } }
                Text { anchors.right: parent.right; text: "v3.1.0"; color: "#93a6bf"; font.pixelSize: 14 }
            }
        }

        Item {
            id: workspace
            anchors.left: sidebar.right; anchors.right: parent.right
            anchors.top: parent.top; anchors.bottom: parent.bottom
            Rectangle {
                id: titlebar
                height: 79; width: parent.width
                gradient: Gradient { GradientStop { position: 0; color: "#8a3c4b5f" } GradientStop { position: 1; color: "#5a293645" } }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#424e6175" }
                MouseArea { anchors.fill: parent; onPressed: window.startSystemMove(); onDoubleClicked: window.maximize() }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 23; anchors.rightMargin: 20; spacing: 14
                    Rectangle { Layout.preferredWidth: 50; Layout.preferredHeight: 50; radius: 12; color: "#75466f9f"; border.color: "#486c91b8"; Glyph { anchors.centerIn: parent; code: 0xF09EE; font.pixelSize: 33; color: "#d6e7ff" } }
                    Text { text: "日志解析"; color: "#f3f6fc"; font.pixelSize: 27; font.weight: Font.Bold }
                    Text { text: "从日志定位报文，逐字段查看协议含义"; color: "#b8c5d7"; font.pixelSize: 15; visible: canvas.width > 1480; Layout.fillWidth: true }
                    Item { Layout.fillWidth: true; visible: canvas.width <= 1480 }
                    GlassButton { objectName: "openLogButton"; text: "打开日志"; iconCode: 0xF0DCF; primary: true; onClicked: samples.open() }
                    GlassButton { text: ""; iconCode: 0xF01D8; hint: "更多操作"; onClicked: moreMenu.popup() }
                }
            }
            ColumnLayout {
                anchors.top: titlebar.bottom; anchors.bottom: statusbar.top
                anchors.left: parent.left; anchors.right: parent.right
                anchors.margins: 20; anchors.bottomMargin: 12; spacing: 14
                RowLayout {
                    Layout.fillWidth: true; spacing: 16
                    GlassButton { text: window.filename; iconCode: 0xF0DCF; trailingChevron: true; Layout.preferredWidth: 286; onClicked: samples.open() }
                    GlassButton { text: "UTF-8"; trailingChevron: true; Layout.preferredWidth: 155; onClicked: { notice.text = "示例日志使用 UTF-8。其他编码将在接入文件读取时启用。"; notice.open(); } }
                    GlassButton { text: "自动识别"; trailingChevron: true; Layout.preferredWidth: 170; onClicked: { notice.text = "当前使用示例字段，协议自动识别将在接入解析引擎时启用。"; notice.open(); } }
                    Item { Layout.fillWidth: true }
                    GlassButton { objectName: "overviewButton"; text: window.overview ? "返回日志" : "全局预览"; iconCode: 0xF06D0; quiet: true; onClicked: window.overview = !window.overview }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 12
                    GlassField { id: searchInput; objectName: "searchInput"; placeholderText: "搜索日志内容"; Layout.fillWidth: true; onTextChanged: window.searchText = text; onAccepted: window.find(false) }
                    GlassButton { iconCode: 0xF0143; hint: "上一个匹配 · Shift+F3"; onClicked: window.find(true) }
                    GlassButton { iconCode: 0xF0140; hint: "下一个匹配 · F3"; onClicked: window.find(false) }
                    GlassField { id: filterInput; objectName: "filterInput"; placeholderText: "正则过滤"; iconCode: 0xF0232; Layout.fillWidth: true; invalid: !!window.filterError; onTextChanged: filterDelay.restart(); onAccepted: { filterDelay.stop(); window.applyFilter(text); } }
                    Timer { id: filterDelay; interval: 220; onTriggered: window.applyFilter(filterInput.text) }
                }
                Text { visible: !!window.filterError; text: window.filterError; color: "#ffb2a4"; font.pixelSize: 14; Layout.fillWidth: true }

                SplitView {
                    id: panels
                    objectName: "panels"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    orientation: Qt.Horizontal
                    handle: Item {
                        implicitWidth: 14
                        Rectangle { width: 3; height: 38; radius: 2; anchors.centerIn: parent; color: SplitHandle.pressed ? "#79b4ff" : SplitHandle.hovered ? "#68829e" : "#304b5b70" }
                    }
                    Rectangle {
                        id: logPanel
                        objectName: "logPanel"
                        SplitView.preferredWidth: panels.width * 0.535
                        SplitView.minimumWidth: 340
                        SplitView.fillHeight: true
                        radius: 10; color: "#c2192028"; border.color: "#71556374"
                        clip: true
                        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 61; radius: 9; gradient: Gradient { GradientStop { position: 0; color: "#90313b47" } GradientStop { position: 1; color: "#50313b47" } } }
                        Rectangle { y: 62; width: parent.width; height: 1; color: "#485b6b7d" }
                        RowLayout {
                            x: 18; y: 8; width: parent.width - 36; height: 45
                            Text { text: window.overview ? "报文概览" : "日志内容"; color: "#eff4fc"; font.pixelSize: 23; font.weight: Font.Bold; Layout.fillWidth: true }
                            GlassButton { text: "清空并粘贴"; quiet: true; iconCode: 0xF018F; onClicked: { clipboardBuffer.clear(); clipboardBuffer.paste(); if (clipboardBuffer.text.trim()) window.loadPasted(clipboardBuffer.text); else window.status = "剪贴板没有文本，当前日志已保留"; } }
                        }
                        Flickable {
                            id: logHorizontal
                            x: 1; y: 70; width: parent.width - 2; height: parent.height - 112
                            visible: !window.overview
                            clip: true
                            contentWidth: Math.max(width, window.shownLines.reduce(function(w, entry) { return Math.max(w, logMetrics.advanceWidth(entry.text) + 80); }, 0))
                            contentHeight: height
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                            ListView {
                                id: logList
                                objectName: "logList"
                                width: logHorizontal.contentWidth; height: parent.height
                                model: window.shownLines
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar { x: logHorizontal.width - width + logHorizontal.contentX; policy: ScrollBar.AsNeeded }
                                delegate: ItemDelegate {
                                    required property var modelData
                                    width: logList.width; height: 27
                                    hoverEnabled: true
                                    Accessible.name: "第 " + (modelData.line+1) + " 行 " + modelData.text
                                    background: Rectangle { color: window.selectedLine === modelData.line ? "#ad254a7b" : parent.hovered ? "#30445569" : "transparent" }
                                    contentItem: Row {
                                        spacing: 20
                                        Text { width: 40; horizontalAlignment: Text.AlignRight; text: modelData.line + 1; color: "#a4b1c3"; font.family: window.mono; font.pixelSize: 15; height: 27; verticalAlignment: Text.AlignVCenter }
                                        Text {
                                            text: Data.richLine(modelData.text, window.searchText, window.selectedLine === modelData.line ? window.byteOffset : -1, window.byteLength)
                                            textFormat: Text.RichText
                                            color: "#c4d0e0"; font.family: window.mono; font.pixelSize: 15; height: 27; verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                    padding: 0
                                    onClicked: window.selectLine(modelData.line)
                                }
                            }
                        }
                        ListView {
                            id: overviewList
                            x: 14; y: 76; width: parent.width - 28; height: parent.height - 123
                            visible: window.overview
                            model: window.overviewRows
                            clip: true
                            spacing: 6
                            ScrollBar.vertical: ScrollBar {}
                            delegate: ItemDelegate {
                                required property var modelData
                                width: overviewList.width; height: 62
                                background: Rectangle { radius: 6; color: parent.hovered ? "#35475f" : "#282f3a" }
                                contentItem: Column {
                                    spacing: 6
                                    Text { text: "第 " + (modelData.line+1) + " 行  ·  " + modelData.text.slice(0, 19); color: "#d5e3f5"; font.pixelSize: 14 }
                                    Text { text: Data.hex(modelData.text).join(" "); font.family: window.mono; font.pixelSize: 12; color: "#aebed1"; width: parent.width; elide: Text.ElideRight }
                                }
                                onClicked: { window.selectLine(modelData.line); window.overview=false; logList.positionViewAtIndex(window.shownLines.findIndex(function(e) { return e.line === modelData.line; }), ListView.Contain); }
                            }
                        }
                        Column {
                            anchors.centerIn: parent; width: parent.width - 60; spacing: 15
                            visible: window.shownLines.length === 0
                            Glyph { anchors.horizontalCenter: parent.horizontalCenter; code: 0xF09EE; font.pixelSize: 44; color: "#7796b8" }
                            Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: window.lines.length ? "没有匹配的日志" : "打开日志，开始分析"; font.pixelSize: 20; color: "#d2deef" }
                            Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: window.lines.length ? "修改或清空正则过滤条件" : "可加载示例日志，或粘贴文本查看"; font.pixelSize: 14; color: "#92a5be" }
                            GlassButton { anchors.horizontalCenter: parent.horizontalCenter; text: "加载示例日志"; primary: true; visible: !window.lines.length; onClicked: window.loadSample(false) }
                        }
                        Rectangle { y: parent.height - 43; width: parent.width; height: 1; color: "#384e6072" }
                        Text { x: 18; anchors.bottom: parent.bottom; anchors.bottomMargin: 15; width: parent.width - 36; elide: Text.ElideRight; text: window.overview ? "选择报文，返回日志并查看对应字段。" : "选择日志中的报文，查看右侧解析结果。"; color: "#a5b3c6"; font.pixelSize: 14 }
                    }
                    Rectangle {
                        id: resultPanel
                        objectName: "resultPanel"
                        SplitView.fillWidth: true; SplitView.minimumWidth: 370
                        radius: 10; color: "#c2192028"; border.color: "#71556374"
                        clip: true
                        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 68; radius: 9; gradient: Gradient { GradientStop { position: 0; color: "#90313b47" } GradientStop { position: 1; color: "#50313b47" } } }
                        RowLayout {
                            x: 18; y: 14; width: parent.width - 36; height: 42; spacing: 20
                            Text { text: "解析结果"; color: "#f1f5fc"; font.pixelSize: 23; font.weight: Font.Bold }
                            Rectangle { Layout.preferredWidth: protocolLabel.implicitWidth + 24; Layout.preferredHeight: 34; color: "#a23c4a5b"; radius: 7; border.color: "#6659697b"; Text { id: protocolLabel; anchors.centerIn: parent; text: window.isSample && window.selectedLine === 2 ? "DL/T 698.45" : "原始数据"; color: "#e1e8f3"; font.pixelSize: 14 } }
                            Item { Layout.fillWidth: true }
                            GlassButton { iconCode: 0xF018F; hint: "复制当前报文"; enabled: window.bytes.length > 0; opacity: enabled ? 1 : 0.4; onClicked: window.copyText(window.bytes.join(" ")) }
                        }
                        Rectangle { y: 69; width: parent.width; height: 1; color: "#485b6b7d" }
                        Text { x: 18; y: 88; text: "当前选中报文"; color: "#b9c7da"; font.pixelSize: 16 }
                        Flickable {
                            id: byteFlick
                            x: 18; y: 114; width: parent.width - 36; height: Math.min(byteFlow.height, 108) + 8
                            contentHeight: byteFlow.height; clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar {}
                            Flow {
                                id: byteFlow
                                width: parent.width; spacing: 4
                                Repeater {
                                    model: window.bytes
                                    delegate: Rectangle {
                                        required property string modelData
                                        required property int index
                                        readonly property bool highlighted: index >= window.byteOffset && index < window.byteOffset + window.byteLength
                                        width: 31; height: 31; radius: 5
                                        color: highlighted ? "#296bc4" : index === window.bytes.length - 1 ? "#345749" : "#293a50"
                                        border.color: highlighted ? "#71adff" : "#53637a"
                                        Text { anchors.centerIn: parent; text: parent.modelData; font.family: window.mono; font.pixelSize: 14; color: "#e9f1fb" }
                                    }
                                }
                            }
                        }
                        Rectangle {
                            id: tableHeader
                            y: Math.max(166, byteFlick.y + byteFlick.height + 18)
                            width: parent.width; height: 41; color: "#7534404b"
                            Row {
                                anchors.fill: parent
                                Repeater {
                                    model: ["域 / 位域", "数据", "说明"]
                                    delegate: Item { required property string modelData; required property int index; width: resultPanel.width * [0.315,0.255,0.43][index]; height: 41; Text { x: 18; anchors.verticalCenter: parent.verticalCenter; text: parent.modelData; color: "#e7edf6"; font.pixelSize: 15 } Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#40596a7f" } }
                                }
                            }
                        }
                        ListView {
                            id: fieldList
                            objectName: "fieldList"
                            anchors.top: tableHeader.bottom; anchors.bottom: parent.bottom; anchors.bottomMargin: 12
                            width: parent.width; clip: true
                            model: window.resultRows
                            ScrollBar.vertical: ScrollBar {}
                            delegate: ItemDelegate {
                                required property var modelData
                                id: fieldRow
                                width: fieldList.width; height: 37
                                padding: 0
                                hoverEnabled: true
                                Accessible.name: modelData.name + " " + modelData.value + " " + modelData.description
                                background: Rectangle { color: window.selectedField === fieldRow.modelData.name ? "#bc2d5998" : fieldRow.hovered ? "#36475b" : "transparent"; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#304f6276" } }
                                contentItem: Row {
                                    Item {
                                        width: resultPanel.width * 0.315; height: parent.height
                                        Button { x: 8; width: 27; height: parent.height; visible: !!fieldRow.modelData.group; background: Item {} contentItem: Glyph { code: window.collapsed[fieldRow.modelData.group] ? 0xF0142 : 0xF0140; font.pixelSize: 20 } Accessible.name: "展开或折叠" + fieldRow.modelData.name; onClicked: window.toggleGroup(fieldRow.modelData.group) }
                                        Text { x: fieldRow.modelData.depth ? 65 : 43; width: parent.width - x - 8; anchors.verticalCenter: parent.verticalCenter; text: fieldRow.modelData.name; elide: Text.ElideRight; color: "#dce7f5"; font.pixelSize: 15 }
                                        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#304f6276" }
                                    }
                                    Item {
                                        width: resultPanel.width * 0.255; height: parent.height
                                        Text { x: 16; width: parent.width - 25; anchors.verticalCenter: parent.verticalCenter; text: fieldRow.modelData.value; elide: Text.ElideRight; color: "#cce0f9"; font.pixelSize: 14; font.family: window.mono }
                                        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#304f6276" }
                                    }
                                    Item {
                                        width: resultPanel.width * 0.43; height: parent.height
                                        Text { x: 16; width: parent.width - 25; anchors.verticalCenter: parent.verticalCenter; text: fieldRow.modelData.description; elide: Text.ElideRight; color: "#c2cede"; font.pixelSize: 14 }
                                    }
                                }
                                onClicked: window.selectField(modelData)
                                ToolTip.visible: hovered
                                ToolTip.delay: 800
                                ToolTip.text: modelData.name + "\n" + modelData.value + "\n" + modelData.description
                            }
                        }
                        Text { anchors.centerIn: fieldList; width: parent.width - 40; horizontalAlignment: Text.AlignHCenter; visible: !window.resultRows.length; text: "选择一条报文查看字段"; color: "#92a5be"; font.pixelSize: 17 }
                    }
                }
            }
            Rectangle {
                id: statusbar
                anchors.bottom: parent.bottom; width: parent.width; height: 49; color: "#66313e4f"
                Rectangle { height: 1; width: parent.width; color: "#4861768d" }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 24; anchors.rightMargin: 20; spacing: 22
                    Rectangle { width: 12; height: 12; radius: 6; color: "transparent"; border.color: "#7ce696"; border.width: 2 }
                    Text { text: window.status; font.pixelSize: 14; color: "#c4d1e3"; Layout.maximumWidth: 420; elide: Text.ElideRight }
                    Rectangle { width: 1; height: 20; color: "#49647a" }
                    Text { text: "UTF-8"; color: "#b6c4d6"; font.pixelSize: 14 }
                    Text { text: "自动识别 · " + (window.isSample && window.selectedLine === 2 ? "DL/T 698.45" : "待解析"); color: "#b6c4d6"; font.pixelSize: 14; visible: canvas.width > 1400 }
                    Item { Layout.fillWidth: true }
                    Text { text: "交互预览 · " + (window.isSample ? "示例数据" : "原始文本"); color: "#9baec5"; font.pixelSize: 13 }
                }
            }
        }
        Rectangle { anchors.fill: parent; color: "transparent"; border.color: "#8b7993ad"; radius: 20 }
    }

    // Window borders provide native system resizing without touching the original window helper.
    Repeater {
        model: [Qt.LeftEdge, Qt.RightEdge, Qt.TopEdge, Qt.BottomEdge]
        delegate: MouseArea {
            required property int modelData
            x: modelData === Qt.RightEdge ? window.width - 5 : 0
            y: modelData === Qt.BottomEdge ? window.height - 5 : 0
            width: modelData === Qt.LeftEdge || modelData === Qt.RightEdge ? 5 : window.width
            height: modelData === Qt.TopEdge || modelData === Qt.BottomEdge ? 5 : window.height
            cursorShape: modelData === Qt.LeftEdge || modelData === Qt.RightEdge ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: window.startSystemResize(modelData)
        }
    }
    component PreviewDialog: Dialog {
        id: dialog
        anchors.centerIn: Overlay.overlay
        width: Math.min(500, window.width - 60)
        modal: true
        padding: 24
        font.pixelSize: 16
        palette.windowText: "#e9f1fb"
        palette.text: "#e9f1fb"
        palette.buttonText: "#e9f1fb"
        palette.button: "#34465e"
        palette.highlight: "#2779de"
        header: Label {
            text: dialog.title
            color: "#e9f1fb"
            font.pixelSize: 20
            font.weight: Font.Bold
            padding: 24
            bottomPadding: 8
            background: Item {}
        }
        footer: Item {
            implicitHeight: 66
            GlassButton {
                anchors.right: parent.right
                anchors.rightMargin: 24
                text: dialog.standardButtons === Dialog.Ok ? "知道了" : "关闭"
                onClicked: dialog.close()
            }
        }
        background: Rectangle { radius: 16; color: "#273749"; border.color: "#617c9b" }
        Overlay.modal: Rectangle { color: "#77070e18" }
    }
    PreviewDialog {
        id: samples
        objectName: "samplesDialog"
        title: "打开示例日志"
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            spacing: 16
            Text { text: "选择示例体验首页交互"; color: "#aebed3"; font.pixelSize: 15 }
            GlassButton { text: "Terminal3.log"; iconCode: 0xF09EE; Layout.fillWidth: true; onClicked: { window.loadSample(false); samples.close(); } }
            GlassButton { text: "空白日志"; iconCode: 0xF0214; Layout.fillWidth: true; onClicked: { window.loadSample(true); samples.close(); } }
            Text { text: "此版本使用示例字段，不执行真实协议解析。"; color: "#aebed3"; font.pixelSize: 14; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
    PreviewDialog {
        id: notice
        property string text: ""
        title: "提示"
        standardButtons: Dialog.Ok
        contentItem: Text { text: notice.text; color: "#d5e2f3"; font.pixelSize: 16; wrapMode: Text.Wrap; lineHeight: 1.5 }
    }
    PreviewDialog {
        id: appearance
        title: "外观设置"
        standardButtons: Dialog.Close
        contentItem: Switch { text: "减少透明度"; checked: window.solidSurfaces; onToggled: window.solidSurfaces = checked; palette.windowText: "#e1ebf9" }
    }
    Menu {
        id: moreMenu
        palette.window: "#27384c"; palette.text: "#e3edfa"; palette.windowText: "#e3edfa"; palette.highlight: "#326ab0"
        MenuItem { text: "重新加载示例"; onTriggered: window.loadSample(false) }
        MenuItem { text: "清空日志"; onTriggered: window.loadSample(true) }
        MenuItem { text: "复制日志全文"; onTriggered: window.copyText(window.lines.join("\n")) }
    }
}
