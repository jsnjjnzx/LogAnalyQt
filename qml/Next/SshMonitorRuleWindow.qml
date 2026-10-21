import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt.Next as Next

ApplicationWindow {
    id: editor
    objectName: "ssh-rule-window"
    title: "规则库 — 通联工坊"
    width: 1060; height: 750
    minimumWidth: 760; minimumHeight: 560
    visible: false; modality: Qt.NonModal
    property var library
    property var theme
    property int selectedRuleId: -1
    property string savedDraft: '["","",false,"#e8b76a",true,0]'
    readonly property bool dirty: JSON.stringify([nameInput.text, patternInput.text, ignoreCase.checked, colorInput.text, pauseOnMatch.checked, highlightScope.currentIndex]) !== savedDraft
    property var validation: ({valid: false, error: "请输入正则表达式", matches: []})
    property var testResult: null
    property var pendingAction: null
    readonly property color textColor: theme ? theme.text : "#eceef2"
    readonly property color secondaryColor: theme ? theme.subText : "#a7adb7"
    color: theme ? theme.background : "#1d2025"
    palette: library ? library.palette : Qt.application.palette
    font.pixelSize: 14
    function loadRule(id) {
        const rule = library ? library.monitorRules.find(function(value) { return value.id === id; }) : null;
        selectedRuleId = rule ? rule.id : -1;
        nameInput.text = rule ? rule.name : "";
        patternInput.text = rule ? rule.pattern : "";
        ignoreCase.checked = rule ? rule.ignoreCase : false;
        colorInput.text = rule && rule.color ? rule.color : "#e8b76a";
        pauseOnMatch.checked = !rule || rule.pauseOnMatch !== false;
        highlightScope.currentIndex = rule && rule.highlightWholeLine === false ? 1 : 0;
        savedDraft = JSON.stringify([nameInput.text, patternInput.text, ignoreCase.checked, colorInput.text, pauseOnMatch.checked, highlightScope.currentIndex]);
        if (library) library.monitorRuleError = "";
        testResult = null;
    }
    function requestAction(action) {
        if (!dirty) { action(); return; }
        pendingAction = action; discardDialog.open();
    }
    function saveRule() {
        if (!library || !validation.valid) return;
        const id = library.saveMonitorRule(selectedRuleId, nameInput.text, patternInput.text, ignoreCase.checked, colorInput.text, pauseOnMatch.checked, highlightScope.currentIndex === 0);
        if (id > 0) loadRule(id);
    }
    function runTest() {
        testResult = library.backend.testMonitorExpression(patternInput.text, ignoreCase.checked, sampleInput.text);
        sampleInput.deselect();
        if (testResult.matches.length) sampleInput.select(testResult.matches[0].start, testResult.matches[0].end);
    }
    onVisibleChanged: if (visible && !dirty && library && !library.monitorRules.some(function(rule) { return rule.id === selectedRuleId; }))
        loadRule(library.monitorRules.length ? library.monitorRules[0].id : -1)
    onClosing: function(event) {
        if (dirty && library && !library.shuttingDown) {
            event.accepted = false;
            requestAction(function() { loadRule(selectedRuleId); editor.close(); });
        }
    }
    Timer {
        id: validationTimer; interval: 120
        onTriggered: {
            editor.validation = library.backend.testMonitorExpression(patternInput.text, ignoreCase.checked, "");
            editor.testResult = null;
        }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 12
        Label { text: "规则库"; color: textColor; font.pixelSize: 21; font.bold: true }
        Label { text: "规则库由全部会话共享，修改后会影响所有使用该规则的会话；各会话独立选择规则和启用监测。"; color: secondaryColor; font.pixelSize: 12 }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 16
            Rectangle {
                Layout.preferredWidth: Math.max(230, Math.min(290, editor.width * 0.28)); Layout.fillHeight: true
                color: theme ? theme.surface : "#282c33"; radius: 8; border.color: theme ? theme.groupBorder : "#444a53"
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Next.GlassTextField { id: ruleSearch; objectName: "ssh-library-search"; Layout.fillWidth: true; Layout.minimumWidth: 0; placeholderText: "搜索规则" }
                        Next.GlassButton { objectName: "ssh-rule-new"; text: "＋ 新建"; horizontalPadding: 10; onClicked: editor.requestAction(function() { editor.loadRule(-1); nameInput.forceActiveFocus(); }) }
                    }
                    ListView {
                        id: ruleList; objectName: "ssh-library-list"
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 6
                        model: library ? library.monitorRules.filter(function(rule) {
                            const keyword = ruleSearch.text.toLowerCase();
                            return rule.name.toLowerCase().indexOf(keyword) >= 0 || rule.pattern.toLowerCase().indexOf(keyword) >= 0;
                        }) : []
                        delegate: Next.GlassButton {
                            required property var modelData
                            objectName: "ssh-library-rule-" + modelData.id
                            width: ruleList.width; height: 64; checked: editor.selectedRuleId === modelData.id
                            onClicked: editor.requestAction(function() { editor.loadRule(modelData.id); })
                            contentItem: Item {
                                Rectangle { x: 0; y: 5; width: 9; height: 9; radius: 5; color: modelData.color || "#e8b76a" }
                                Column {
                                    x: 18; width: parent.width - 18; spacing: 6
                                    Label { textFormat: Text.PlainText; text: modelData.name; width: parent.width; elide: Text.ElideRight; color: textColor; font.pixelSize: 13 }
                                    Label { textFormat: Text.PlainText; text: modelData.pattern; width: parent.width; elide: Text.ElideRight; color: secondaryColor; font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 11 }
                                }
                            }
                        }
                        ScrollBar.vertical: ScrollBar {}
                        Label { anchors.centerIn: parent; visible: parent.count === 0; text: "暂无规则，点击「新建」"; color: secondaryColor; font.pixelSize: 12 }
                    }
                    Label { text: "共 " + (library ? library.monitorRules.length : 0) + " 条规则"; color: secondaryColor; font.pixelSize: 11 }
                }
            }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width; spacing: 9
                    Label { text: "规则名称"; color: textColor }
                    Next.GlassTextField { id: nameInput; objectName: "ssh-rule-name"; Layout.fillWidth: true; maximumLength: 100; placeholderText: "例如：设备通信异常" }
                    Label { text: "正则表达式"; color: textColor; Layout.topMargin: 5 }
                    ScrollView {
                        Layout.fillWidth: true; Layout.preferredHeight: 90
                        TextArea {
                            id: patternInput; objectName: "ssh-rule-pattern"
                            textFormat: TextEdit.PlainText; selectByMouse: true; wrapMode: TextEdit.Wrap
                            font.family: Next.PlatformFonts.monospaceFamily; color: textColor
                            placeholderText: "例如：设备.*(离线|超时)"
                            background: Rectangle { color: theme ? theme.controlBackground : "#191d23"; border.color: theme ? theme.controlBorder : "#444a53"; radius: 6 }
                            onTextChanged: validationTimer.restart()
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 12
                        Next.GlassCheckBox { id: ignoreCase; objectName: "ssh-rule-ignore-case"; text: "忽略大小写"; onToggled: validationTimer.restart() }
                        Next.GlassCheckBox { id: pauseOnMatch; objectName: "ssh-rule-pause-on-match"; text: "命中后暂停跟随"; checked: true }
                        Next.GlassComboBox {
                            id: highlightScope; objectName: "ssh-rule-highlight-scope"
                            model: ["整行着色", "仅正则匹配部分着色"]
                            implicitWidth: 190
                            Accessible.name: "着色范围"
                        }
                    }
                    Label { text: validation.valid ? "✓ 表达式有效" : "表达式无效"; color: validation.valid ? "#7ed6a0" : "#f79a93"; font.pixelSize: 11 }
                    Label { textFormat: Text.PlainText; Layout.fillWidth: true; visible: !validation.valid; text: validation.error || ""; color: "#f79a93"; wrapMode: Text.Wrap; font.pixelSize: 11 }
                    Label { text: "规则颜色"; color: textColor; Layout.topMargin: 5 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        Repeater {
                            model: ["#f79a93", "#e8b76a", "#d6d67f", "#7ed6a0", "#7ad3d7", "#80b9ff", "#bb9cf5", "#ee9acb"]
                            Next.GlassButton {
                                required property string modelData
                                objectName: "ssh-rule-color-" + modelData.slice(1)
                                implicitWidth: 24; implicitHeight: 24; padding: 0; horizontalPadding: 0
                                Accessible.name: "规则颜色 " + modelData
                                background: Rectangle { color: modelData; radius: 5; border.width: colorInput.text.toLowerCase() === modelData ? 2 : 0; border.color: "white" }
                                onClicked: colorInput.text = modelData
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Next.GlassTextField { id: colorInput; objectName: "ssh-rule-color"; text: "#e8b76a"; Layout.preferredWidth: 106; maximumLength: 7; validator: RegularExpressionValidator { regularExpression: /^#[0-9a-fA-F]{6}$/ } Accessible.name: "自定义规则颜色" }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: theme ? theme.groupBorder : "#444a53"; Layout.topMargin: 8; Layout.bottomMargin: 5 }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "测试表达式"; color: textColor; font.bold: true; Layout.fillWidth: true }
                        Next.GlassButton { objectName: "ssh-rule-test"; text: "测试匹配"; enabled: validation.valid && sampleInput.text.length > 0; onClicked: editor.runTest() }
                    }
                    ScrollView {
                        Layout.fillWidth: true; Layout.preferredHeight: 130
                        TextArea {
                            id: sampleInput; objectName: "ssh-rule-sample"
                            textFormat: TextEdit.PlainText; selectByMouse: true; persistentSelection: true
                            wrapMode: TextEdit.Wrap; font.family: Next.PlatformFonts.monospaceFamily; color: textColor
                            placeholderText: "粘贴日志测试，每条日志单独匹配，不跨行。"
                            onTextChanged: editor.testResult = null
                            background: Rectangle { color: theme ? theme.controlBackground : "#191d23"; border.color: theme ? theme.controlBorder : "#444a53"; radius: 6 }
                        }
                    }
                    Label {
                        objectName: "ssh-rule-test-result"; Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: !testResult ? "测试只验证表达式，不会触发监控暂停。" : testResult.error ? testResult.error
                            : "命中 " + testResult.matches.length + " 处" + (testResult.matches.length ? "，首处已高亮" : "")
                                + (testResult.truncated ? "（已达到测试文本或匹配数量上限）" : "")
                        color: testResult && testResult.error ? "#f79a93" : testResult && testResult.matches.length ? "#7ed6a0" : secondaryColor
                        font.pixelSize: 11
                    }
                    Label {
                        Layout.fillWidth: true; Layout.topMargin: 10; wrapMode: Text.Wrap
                        text: {
                            if (!library || selectedRuleId < 1) return "新规则保存后，可在各监控窗口勾选使用。";
                            const names = library.sessions.filter(function(page) { return page.monitorRuleIds.indexOf(selectedRuleId) >= 0; }).map(function(page) { return page.sessionName; });
                            return names.length ? "当前被这些会话选择：" + names.join("、") : "尚未被监控会话选择。";
                        }
                        color: secondaryColor; font.pixelSize: 11
                    }
                }
            }
        }
        Label { Layout.fillWidth: true; visible: library && library.monitorRuleError !== ""; text: library ? library.monitorRuleError : ""; color: "#f79a93"; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            Next.GlassButton { objectName: "ssh-rule-delete"; text: "删除规则…"; enabled: selectedRuleId > 0; onClicked: deleteDialog.open() }
            Label { text: dirty ? "有未保存修改" : ""; color: "#e8b76a"; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Next.GlassButton { text: "关闭"; onClicked: editor.close() }
            Next.GlassButton { objectName: "ssh-rule-save"; text: "保存修改"; highlighted: true; enabled: nameInput.text.trim().length > 0 && validation.valid && colorInput.acceptableInput; onClicked: editor.saveRule() }
        }
    }
    Next.GlassDialog {
        id: discardDialog; parent: Overlay.overlay; anchors.centerIn: parent
        title: "放弃未保存的修改？"; modal: true
        standardButtons: Dialog.NoButton
        ColumnLayout {
            Label { text: "当前规则的修改尚未保存。"; color: textColor }
            RowLayout {
                Next.GlassButton { text: "继续编辑"; onClicked: { editor.pendingAction = null; discardDialog.close(); } }
                Next.GlassButton { text: "放弃修改"; onClicked: { const action = editor.pendingAction; editor.pendingAction = null; discardDialog.close(); if (action) action(); } }
            }
        }
    }
    Next.GlassDialog {
        id: deleteDialog; objectName: "ssh-rule-delete-dialog"; parent: Overlay.overlay; anchors.centerIn: parent
        title: "删除监控规则？"; modal: true; standardButtons: Dialog.NoButton
        ColumnLayout {
            Label { text: "删除后，各会话会取消选择这条规则。"; color: textColor }
            RowLayout {
                Next.GlassButton { text: "取消"; onClicked: deleteDialog.close() }
                Next.GlassButton { objectName: "ssh-rule-delete-confirm"; text: "删除"; onClicked: { if (library.removeMonitorRule(selectedRuleId)) { deleteDialog.close(); editor.loadRule(-1); } } }
            }
        }
    }
}
