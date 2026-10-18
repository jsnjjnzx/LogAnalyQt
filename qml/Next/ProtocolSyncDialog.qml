import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt

GlassPage {
    id: syncDialog
    visible: false
    title: promptsSelected ? "同步 AI 提示词" : "同步协议配置"


    property var syncManager
    property var promptSyncManager: null
    property bool promptsSelected: false
    readonly property var activeManager: promptsSelected ? promptSyncManager : syncManager
    function selectContent(index) {
        if (remoteBusy) return
        cancelOwnedWork()
        promptsSelected = index === 1
        pathField.text = activeManager ? activeManager.sharePath : ""
        statusText = ""; diffData = []
    }
    property string statusText: ""
    property bool statusIsError: false
    property var diffData: []
    readonly property bool systemSmb: !!(activeManager && activeManager.usesSystemSmb)
    readonly property bool canSync: !!activeManager && !remoteBusy && (!systemSmb || remoteReady)
    property bool remoteReady: false
    property bool ownsConnection: false
    property string pendingAction: ""
    property string pendingDiffFile: ""
    readonly property bool remoteBusy: pendingAction.length > 0 || !!(activeManager && (activeManager.busy || activeManager.connecting || activeManager.credentialBusy))
    function startConnection() {
        if (!activeManager || remoteBusy) return
        remoteReady = false; ownsConnection = true; statusText = systemSmb ? "等待 Finder 连接共享并确认目录可达…" : "连接中…"; statusIsError = false
        activeManager.sharePath = pathField.text.trim()
        if (!systemSmb) activeManager.saveCredentials()
        activeManager.connectToShare()
    }
    function cancelConnectionWait() {
        ownsConnection = false; remoteReady = false
        if (activeManager) activeManager.cancelConnection()
        statusText = "已停止等待连接；Finder 中的操作不会被强制终止。"; statusIsError = false
    }
    function cancelOwnedWork() {
        const hadConnection = ownsConnection
        const hadOperation = pendingAction.length > 0
        ownsConnection = false; pendingAction = ""; pendingDiffFile = ""; remoteReady = false
        if (activeManager && hadConnection) activeManager.cancelConnection()
        if (activeManager && hadOperation) activeManager.cancelPendingOperations()
    }
    function refreshStatus() {
        if (!canSync) return
        pendingAction = "status"; statusText = "正在检查文件状态…"; statusIsError = false
        const accepted = activeManager.fetchSyncStatus()
        if (!systemSmb || !accepted) {
            pendingAction = ""
            statusIsError = !accepted
            statusText = accepted ? "文件状态已刷新。" : (activeManager.lastError || "状态检查未受理")
        }
    }
    function requestSync(fileName) {
        if (!canSync) return
        pendingAction = "sync"; statusText = "正在同步文件，尚未确认结果…"; statusIsError = false
        const reply = fileName ? activeManager.syncFile(fileName) : activeManager.syncAll()
        if (!systemSmb) { pendingAction = ""; showSyncResult(reply); return }
        if (!reply || reply.pending !== true) {
            pendingAction = ""; statusText = reply && reply.error ? reply.error : "同步操作未受理"; statusIsError = true
        }
    }
    function showSyncResult(result) {
        resultDialog.resultText = buildResultText(result)
        resultDialog.hasErrors = !!((result.failed && result.failed.length) || (result.errors && result.errors.length) || result.error)
        statusIsError = resultDialog.hasErrors
        statusText = statusIsError ? "同步完成，部分文件失败；请查看详细结果。" : "同步操作完成。"
        centerResultDialog(); resultDialog.open()
    }
    function requestDiff(fileName) {
        if (!canSync) return
        pendingAction = "diff"; pendingDiffFile = fileName; diffData = []
        statusText = "正在读取差异，尚未确认结果…"; statusIsError = false
        const directDiff = activeManager.getXmlDiff(fileName)
        if (!systemSmb) {
            pendingAction = ""; pendingDiffFile = ""; diffData = directDiff
            diffDialog.diffFileName = fileName; diffDialog.show()
        }
    }
    onClosing: cancelOwnedWork()
    Component.onDestruction: cancelOwnedWork()

    color: theme ? theme.surface : "#ffffff"
    palette.highlight: theme ? theme.accentMuted : "#1976D2"




    // 窗口边框


    // 自定义标题栏
    Item { id: syncTitleBar; height: 0; width: parent.width }

    // 边缘缩放区域
    // 左边缘

    // 右边缘

    // 下边缘

    // 左下角

    // 右下角

    function centerResultDialog() {
        resultDialog.x = (syncDialog.width - resultDialog.width) / 2;
        resultDialog.y = (syncDialog.height - resultDialog.height) / 2;
    }

    function actionLabel(action) {
        if (action === "upload")
            return "↑";
        if (action === "download")
            return "↓";
        if (action === "equal")
            return "✓";
        if (action === "missing")
            return "-";
        return "!";
    }

    function syncStatusText(action, localExists, remoteExists) {
        if (action === "conflict") return "内容冲突"
        if (action === "upload" && localExists && !remoteExists)
            return "远端缺失，待上传";
        return actionLabel(action);
    }

    function actionColor(action) {
        if (!theme)
            return "#333333";
        if (action === "upload")
            return theme.error;
        if (action === "download")
            return theme.success;
        if (action === "equal")
            return theme.text;
        if (action === "missing")
            return theme.subText;
        return theme.error;
    }

    function buildResultText(result) {
        var lines = [];
        if (result.uploaded && result.uploaded.length > 0) {
            lines.push("✓ 上传成功:");
            for (var i = 0; i < result.uploaded.length; i++) {
                lines.push("  - " + result.uploaded[i]);
            }
            lines.push("");
        }
        if (result.downloaded && result.downloaded.length > 0) {
            lines.push("✓ 下载成功:");
            for (var j = 0; j < result.downloaded.length; j++) {
                lines.push("  - " + result.downloaded[j]);
            }
            lines.push("");
        }
        if (result.skipped && result.skipped.length > 0) {
            lines.push("✓ 无需同步:");
            for (var k = 0; k < result.skipped.length; k++) {
                lines.push("  - " + result.skipped[k]);
            }
            lines.push("");
        }
        if (result.failed && result.failed.length > 0) {
            lines.push("❌ 失败:");
            for (var m = 0; m < result.failed.length; m++) {
                lines.push("  - " + result.failed[m]);
            }
            lines.push("");
        }
        if (result.errors && result.errors.length > 0) {
            lines.push("错误详情:");
            for (var n = 0; n < result.errors.length; n++) {
                var err = result.errors[n];
                lines.push("  - " + err.file + ": " + err.error);
            }
        }
        if (result.error) lines.push(String(result.error))
        if (lines.length === 0) lines.push("没有需要同步的文件。")
        return lines.join("\n");
    }

    onVisibleChanged: {
        if (visible) {
            statusText = "";
        } else cancelOwnedWork()
    }

    component StyledButton: Next.GlassButton {
        id: control
        contentItem: Text {
            text: control.text
            font: control.font

            color: control.highlighted ? "white" : (theme ? theme.buttonText : "#333333")
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    ColumnLayout {
        anchors.top: syncTitleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            visible: !!syncDialog.promptSyncManager
            Label { text: "同步内容"; color: theme ? theme.text : "#333333" }
            Next.GlassComboBox {
                objectName: "sync-content-selector"
                model: ["协议 XML", "AI 提示词"]
                currentIndex: syncDialog.promptsSelected ? 1 : 0
                enabled: !syncDialog.remoteBusy
                onActivated: syncDialog.selectContent(currentIndex)
                Accessible.name: "同步内容"
            }
            Label {
                Layout.fillWidth: true
                visible: syncDialog.promptsSelected
                text: "共享目录放置 ai_698_framing.md；同步后从下一次 AI 组帧任务生效。"
                wrapMode: Text.Wrap
                color: theme ? theme.subText : "#666666"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: "网盘路径:"
                color: theme ? theme.text : "#333333"
                Layout.alignment: Qt.AlignVCenter
            }

            Next.GlassTextField {
                id: pathField
                objectName: "protocol-sync-path"
                Layout.fillWidth: true
                enabled: !syncDialog.remoteBusy
                placeholderText: "smb://server/share"
                onTextEdited: { syncDialog.remoteReady = false; if (activeManager) activeManager.sharePath = text }
                readOnly: !syncDialog.systemSmb && !syncDialog.promptsSelected
                text: activeManager ? activeManager.sharePath : ""
                color: theme ? theme.text : "#333333"
            }

            StyledButton {
                id: connectBtn
                objectName: "protocol-sync-connect"
                text: syncDialog.systemSmb && activeManager && activeManager.connecting ? "取消等待" : "连接"
                Layout.preferredWidth: 80
                enabled: !!activeManager && (syncDialog.systemSmb ? (activeManager.connecting || !syncDialog.remoteBusy) : !activeManager.connecting)
                onClicked: {
                    if (activeManager && activeManager.connecting) syncDialog.cancelConnectionWait()
                    else syncDialog.startConnection()
                }
            }
        }

        Connections {
            target: activeManager
            ignoreUnknownSignals: true
            function onSharePathChanged() { syncDialog.remoteReady = false }
            function onConnectionFinished(success, message) {
                if (!syncDialog.visible || !syncDialog.ownsConnection) return
                syncDialog.ownsConnection = false; syncDialog.remoteReady = success
                syncDialog.statusText = message; syncDialog.statusIsError = !success
                if (success) {
                    if (syncDialog.systemSmb) {
                        syncDialog.pendingAction = "status"
                        syncDialog.statusText = "共享已连接，正在检查文件状态…"
                        if (!activeManager.fetchSyncStatus()) { syncDialog.pendingAction = ""; syncDialog.statusIsError = true; syncDialog.statusText = "状态检查未受理" }
                    } else activeManager.fetchSyncStatus()
                }
            }
            function onStatusFinished(success, message) {
                if (!syncDialog.visible || (syncDialog.pendingAction !== "status" && syncDialog.pendingAction !== "diff")) return
                if (syncDialog.pendingAction === "diff" && success) return
                syncDialog.pendingAction = ""; syncDialog.pendingDiffFile = ""
                syncDialog.statusText = message; syncDialog.statusIsError = !success
                if (!success) syncDialog.remoteReady = false
            }
            function onSyncFinished(result) {
                if (!syncDialog.visible || syncDialog.pendingAction !== "sync") return
                syncDialog.pendingAction = ""; syncDialog.showSyncResult(result)
            }
            function onDiffFinished(fileName, diff) {
                if (!syncDialog.visible || syncDialog.pendingAction !== "diff" || syncDialog.pendingDiffFile !== fileName) return
                syncDialog.pendingAction = ""; syncDialog.pendingDiffFile = ""; syncDialog.diffData = diff
                diffDialog.diffFileName = fileName; diffDialog.show()
                syncDialog.statusText = "差异读取完成。"; syncDialog.statusIsError = false
            }
        }

        Loader {
            Layout.fillWidth: true
            active: !!activeManager && !syncDialog.systemSmb
            sourceComponent: Component {
                RowLayout {
                    spacing: 10
                    Label { text: "账号:"; color: theme ? theme.text : "#333333" }
                    Next.GlassTextField { objectName: "protocol-sync-legacy-user"; Layout.preferredWidth: 180; text: activeManager ? activeManager.username : ""; onTextEdited: if (activeManager) activeManager.username = text }
                    Label { text: "密码:"; color: theme ? theme.text : "#333333" }
                    Next.GlassTextField { objectName: "protocol-sync-legacy-password"; Layout.preferredWidth: 180; echoMode: TextInput.Password; text: activeManager ? activeManager.password : ""; onTextEdited: if (activeManager) activeManager.password = text }
                    Item { Layout.fillWidth: true }
                }
            }
        }
        Text { visible: syncDialog.systemSmb; text: "macOS 共享认证由 Finder 处理，本窗口不收集 SMB 用户名或密码。"; color: theme ? theme.subText : "#666666"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Text { objectName: "protocol-sync-credential-error"; text: activeManager ? activeManager.credentialError : ""; textFormat: Text.PlainText; visible: text.length > 0; color: theme ? theme.error : "#F44336"; Layout.fillWidth: true; wrapMode: Text.WordWrap }

        Text {
            objectName: "protocol-sync-status"
            text: statusText
            textFormat: Text.PlainText
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: syncDialog.remoteBusy ? (theme ? theme.subText : "#666666") : statusIsError ? (theme ? theme.error : "#F44336") : (theme ? theme.success : "#4CAF50")
            visible: statusText.length > 0
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: theme ? theme.controlBorder : "#cccccc"
        }

        Row {
            Layout.fillWidth: true
            height: 28

            Text {
                width: 200
                text: "文件名"
                font.bold: true
                color: theme ? theme.text : "#333333"
            }
            Text {
                width: 90
                text: syncDialog.promptsSelected ? "本地时间" : "本地版本"
                font.bold: true
                color: theme ? theme.text : "#333333"
            }
            Text {
                width: 90
                text: "本地日期"
                font.bold: true
                color: theme ? theme.text : "#333333"
            }
            Text {
                width: 90
                text: syncDialog.promptsSelected ? "网盘时间" : "网盘版本"
                font.bold: true
                color: theme ? theme.text : "#333333"
            }
            Text {
                width: 90
                text: "网盘日期"
                font.bold: true
                color: theme ? theme.text : "#333333"
            }
            Text {
                width: 120
                text: "操作"
                font.bold: true
                color: theme ? theme.text : "#333333"
            }
            Text {
                width: 70
                text: "同步"
                font.bold: true
                color: theme ? theme.text : "#333333"
            }
            Text {
                width: 70
                text: "差异"
                font.bold: true
                color: theme ? theme.text : "#333333"
            }
        }

        ListView {
            id: statusList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: activeManager ? activeManager.statusModel : null

            delegate: Rectangle {
                width: statusList.width
                height: 30
                color: index % 2 === 0 ? "transparent" : (theme ? theme.rowAlt : "#f5f5f5")

                Row {
                    anchors.fill: parent

                    Text {
                        width: 200
                        text: model.fileName
                        color: theme ? theme.text : "#333333"
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    Text {
                        width: 90
                        text: model.localVersion
                        color: (model.localVersion !== "--" && model.remoteVersion !== "--" && model.localVersion !== model.remoteVersion) ? (theme ? theme.error : "#F44336") : (theme ? theme.text : "#333333")
                        verticalAlignment: Text.AlignVCenter
                    }
                    Text {
                        width: 90
                        text: model.localDate
                        color: (model.localDate !== "--" && model.remoteDate !== "--" && model.localDate !== model.remoteDate) ? (theme ? theme.error : "#F44336") : (theme ? theme.text : "#333333")
                        verticalAlignment: Text.AlignVCenter
                    }
                    Text {
                        width: 90
                        text: model.remoteVersion
                        color: (model.localVersion !== "--" && model.remoteVersion !== "--" && model.localVersion !== model.remoteVersion) ? (theme ? theme.error : "#F44336") : (theme ? theme.text : "#333333")
                        verticalAlignment: Text.AlignVCenter
                    }
                    Text {
                        width: 90
                        text: model.remoteDate
                        color: (model.localDate !== "--" && model.remoteDate !== "--" && model.localDate !== model.remoteDate) ? (theme ? theme.error : "#F44336") : (theme ? theme.text : "#333333")
                        verticalAlignment: Text.AlignVCenter
                    }
                    Text {
                        width: 120
                        text: syncStatusText(model.action, model.localExists, model.remoteExists)
                        color: actionColor(model.action)
                        font.bold: true
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    StyledButton {
                        text: "同步"
                        width: 62
                        height: 24
                        enabled: syncDialog.canSync && (model.action === "upload" || model.action === "download")
                        onClicked: {
                            if (!activeManager)
                                return;
                            syncDialog.requestSync(model.fileName)
                        }
                    }
                    StyledButton {
                        text: "差异"
                        width: 62
                        height: 24
                        enabled: syncDialog.canSync && (model.action === "upload" || model.action === "download" || model.action === "conflict")
                        onClicked: {
                            if (!activeManager)
                                return;
                            syncDialog.requestDiff(model.fileName)
                        }
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {}
        }

        Text {
            text: "图例: ↑本地较新需上传  ↓网盘较新需下载  ✓一致  -缺失  远端缺失，待上传  |  差异视图: \u25a0红=本地独有  \u25a0绿=远程新增"
            color: theme ? theme.subText : "#666666"
            font.pixelSize: 12
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Item {
                Layout.fillWidth: true
            }

            StyledButton {
                objectName: "protocol-sync-refresh"
                text: "刷新状态"
                enabled: syncDialog.canSync
                onClicked: syncDialog.refreshStatus()
            }

            StyledButton {
                objectName: "protocol-sync-all"
                highlighted: true
                text: "一键同步"
                Layout.preferredWidth: 100
                enabled: syncDialog.canSync
                onClicked: {
                    if (!activeManager)
                        return;
                    syncDialog.requestSync("")
                }
            }

            StyledButton {
                text: "取消"
                Layout.preferredWidth: 80
                onClicked: syncDialog.close()
            }
        }
    }

    Next.GlassDialog {
        id: resultDialog
        objectName: "protocol-sync-result"
        parent: syncDialog.contentItem
        modal: true
        title: ""
        width: 500
        standardButtons: Dialog.NoButton

        property string resultText: ""
        property bool hasErrors: false

        onAccepted: close()
        onOpened: centerResultDialog()

        contentItem: ColumnLayout {
            width: 900
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                height: 36
                radius: 6
                color: theme ? theme.controlBackground : "#1e1e1e"
                border.color: theme ? theme.controlBorder : "#333333"

                Text {
                    text: "同步结果"
                    color: theme ? theme.text : "#ffffff"
                    font.bold: true
                    anchors.centerIn: parent
                }
            }

            TextArea {
                objectName: "protocol-sync-result-text"
                text: resultDialog.resultText
                textFormat: TextEdit.PlainText
                readOnly: true
                wrapMode: Text.WordWrap
                color: theme ? theme.text : "#333333"
                background: Rectangle {
                    color: theme ? theme.controlBackground : "#ffffff"
                    border.color: theme ? theme.controlBorder : "#cccccc"
                    radius: 8
                }
                Layout.preferredHeight: 260
                Layout.fillWidth: true
            }

            StyledButton {
                text: "打开本地目录"
                visible: resultDialog.hasErrors
                onClicked: {
                    if (activeManager) {
                        var path = activeManager.localProtocolDir();
                        if (path && path.length > 0) {
                            Qt.openUrlExternally("file:///" + path);
                        }
                    }
                }
            }
        }

        background: Rectangle {
            color: theme ? theme.popupBackground : "#ffffff"
            radius: 12
            border.color: theme ? theme.controlBorder : "#cccccc"
        }

        footer: Rectangle {
            color: theme ? theme.background : "#ffffff"
            border.color: theme ? theme.controlBorder : "#cccccc"
            height: 54

            StyledButton {
                text: "确定"
                width: 100
                height: 30
                anchors.centerIn: parent
                onClicked: resultDialog.close()
            }
        }
    }

    // ====== 差异查看窗口 ======
    Window {
        id: diffDialog
        objectName: "protocol-diff-window"
        palette: syncDialog.palette
        width: 1100
        height: 700
        minimumWidth: 600
        minimumHeight: 400
        visible: false
        title: "差异比较: " + diffFileName
        modality: Qt.WindowModal
        flags: Qt.Window | Qt.FramelessWindowHint

        property int resizeMargin: 6

        property string diffFileName: ""

        color: theme ? theme.background : "#ffffff"

        onVisibleChanged: {
            if (visible) {
                // 居中于父窗口
                diffDialog.x = ownerWindow.x + (ownerWindow.width - diffDialog.width) / 2;
                diffDialog.y = ownerWindow.y + (ownerWindow.height - diffDialog.height) / 2;
            }
        }

        onClosing: {
            // 关闭时清理差异数据，避免残留状态
            syncDialog.diffData = [];
        }

        // 窗口边框
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: theme ? theme.controlBorder : "#333333"
            border.width: 1
            z: 200
        }

        // 自定义标题栏
        Next.TitleBar {
            id: diffTitleBar
            z: 100
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            targetWindow: diffDialog
            title: diffDialog.title
            titleBarColor: theme ? theme.background : "#ffffff"
            titleTextColor: theme ? theme.text : "#333333"
            buttonHoverColor: theme ? theme.buttonHover : "#e0e0e0"
            closeButtonHoverColor: theme ? theme.error : "#ff0000"
        }

        // 边缘缩放区域
        // 左边缘
        MouseArea {
            width: diffDialog.resizeMargin
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: diffDialog.resizeMargin
            anchors.bottomMargin: diffDialog.resizeMargin
            cursorShape: Qt.SizeHorCursor
            z: 150

            property real startX: 0
            property real startWidth: 0
            property real startWindowX: 0

            onPressed: function (mouse) {
                startX = mouse.x;
                startWidth = diffDialog.width;
                startWindowX = diffDialog.x;
            }
            onPositionChanged: function (mouse) {
                if (pressed) {
                    var dx = mouse.x - startX;
                    var newWidth = startWidth - dx;
                    if (newWidth >= 600) {
                        diffDialog.width = newWidth;
                        diffDialog.x = startWindowX + dx;
                    }
                }
            }
        }

        // 右边缘
        MouseArea {
            width: diffDialog.resizeMargin
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: diffDialog.resizeMargin
            anchors.bottomMargin: diffDialog.resizeMargin
            cursorShape: Qt.SizeHorCursor
            z: 150

            property real startX: 0
            property real startWidth: 0

            onPressed: function (mouse) {
                startX = mapToGlobal(mouse.x, mouse.y).x;
                startWidth = diffDialog.width;
            }
            onPositionChanged: function (mouse) {
                if (pressed) {
                    var currentX = mapToGlobal(mouse.x, mouse.y).x;
                    var dx = currentX - startX;
                    var newWidth = startWidth + dx;
                    if (newWidth >= 600) {
                        diffDialog.width = newWidth;
                    }
                }
            }
        }

        // 下边缘
        MouseArea {
            height: diffDialog.resizeMargin
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: diffDialog.resizeMargin
            anchors.rightMargin: diffDialog.resizeMargin
            cursorShape: Qt.SizeVerCursor
            z: 150

            property real startY: 0
            property real startHeight: 0

            onPressed: function (mouse) {
                startY = mapToGlobal(mouse.x, mouse.y).y;
                startHeight = diffDialog.height;
            }
            onPositionChanged: function (mouse) {
                if (pressed) {
                    var currentY = mapToGlobal(mouse.x, mouse.y).y;
                    var dy = currentY - startY;
                    var newHeight = startHeight + dy;
                    if (newHeight >= 400) {
                        diffDialog.height = newHeight;
                    }
                }
            }
        }

        // 左下角
        MouseArea {
            width: diffDialog.resizeMargin
            height: diffDialog.resizeMargin
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            cursorShape: Qt.SizeBDiagCursor
            z: 150

            property point startPos: Qt.point(0, 0)
            property real startWidth: 0
            property real startHeight: 0
            property real startWindowX: 0

            onPressed: function (mouse) {
                startPos = mapToGlobal(mouse.x, mouse.y);
                startWidth = diffDialog.width;
                startHeight = diffDialog.height;
                startWindowX = diffDialog.x;
            }
            onPositionChanged: function (mouse) {
                if (pressed) {
                    var currentPos = mapToGlobal(mouse.x, mouse.y);
                    var dx = currentPos.x - startPos.x;
                    var dy = currentPos.y - startPos.y;

                    var newWidth = startWidth - dx;
                    if (newWidth >= 600) {
                        diffDialog.width = newWidth;
                        diffDialog.x = startWindowX + dx;
                    }

                    var newHeight = startHeight + dy;
                    if (newHeight >= 400) {
                        diffDialog.height = newHeight;
                    }
                }
            }
        }

        // 右下角
        MouseArea {
            width: diffDialog.resizeMargin
            height: diffDialog.resizeMargin
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            cursorShape: Qt.SizeFDiagCursor
            z: 150

            property point startPos: Qt.point(0, 0)
            property real startWidth: 0
            property real startHeight: 0

            onPressed: function (mouse) {
                startPos = mapToGlobal(mouse.x, mouse.y);
                startWidth = diffDialog.width;
                startHeight = diffDialog.height;
            }
            onPositionChanged: function (mouse) {
                if (pressed) {
                    var currentPos = mapToGlobal(mouse.x, mouse.y);
                    var dx = currentPos.x - startPos.x;
                    var dy = currentPos.y - startPos.y;

                    var newWidth = startWidth + dx;
                    if (newWidth >= 600) {
                        diffDialog.width = newWidth;
                    }

                    var newHeight = startHeight + dy;
                    if (newHeight >= 400) {
                        diffDialog.height = newHeight;
                    }
                }
            }
        }

        ColumnLayout {
            anchors.top: diffTitleBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 12
            spacing: 8

            // 左右列标题
            Row {
                Layout.fillWidth: true
                height: 28

                Rectangle {
                    width: parent.width / 2 - 1
                    height: parent.height
                    color: theme ? Qt.rgba(theme.error.r, theme.error.g, theme.error.b, 0.1) : "#1af44336"
                    radius: 8

                    Text {
                        text: "本地文件"
                        color: theme ? theme.text : "#333333"
                        font.bold: true
                        font.pixelSize: 13
                        anchors.centerIn: parent
                    }
                }

                Rectangle {
                    width: 2
                    height: parent.height
                    color: theme ? theme.controlBorder : "#cccccc"
                }

                Rectangle {
                    width: parent.width / 2 - 1
                    height: parent.height
                    color: theme ? Qt.rgba(theme.success.r, theme.success.g, theme.success.b, 0.1) : "#1a4caf50"
                    radius: 8

                    Text {
                        text: "远程文件"
                        color: theme ? theme.text : "#333333"
                        font.bold: true
                        font.pixelSize: 13
                        anchors.centerIn: parent
                    }
                }
            }

            // 左右并排差异内容
            ListView {
                id: diffListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: diffData

                delegate: Rectangle {
                    width: diffListView.width
                    height: Math.max(leftText.implicitHeight, rightText.implicitHeight) + 6

                    color: "transparent"

                    Row {
                        anchors.fill: parent

                        // ====== 左侧面板（本地） ======
                        Rectangle {
                            width: parent.width / 2 - 1
                            height: parent.height
                            color: {
                                var t = modelData.leftType;
                                if (t === "del")
                                    return theme ? Qt.rgba(theme.error.r, theme.error.g, theme.error.b, 0.1) : "#1af44336";
                                if (t === "fold")
                                    return theme ? Qt.rgba(0.5, 0.5, 0.5, 0.08) : "#08888888";
                                return "transparent";
                            }

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 4
                                spacing: 4

                                Text {
                                    width: 16
                                    height: parent.height
                                    text: {
                                        var t = modelData.leftType;
                                        if (t === "del")
                                            return "-";
                                        if (t === "fold")
                                            return "⋯";
                                        return " ";
                                    }
                                    color: {
                                        var t = modelData.leftType;
                                        if (t === "del")
                                            return theme ? theme.error : "#F44336";
                                        if (t === "fold")
                                            return theme ? theme.subText : "#888888";
                                        return theme ? theme.subText : "#999999";
                                    }
                                    font.family: Next.PlatformFonts.monospaceFamily
                                    font.pixelSize: 13
                                    font.bold: true
                                    verticalAlignment: Text.AlignVCenter
                                }

                                Text {
                                    id: leftText
                                    objectName: "protocol-diff-left-text"
                                    textFormat: Text.PlainText
                                    width: parent.width - 24
                                    text: modelData.leftText
                                    color: {
                                        var t = modelData.leftType;
                                        if (t === "del")
                                            return theme ? theme.error : "#F44336";
                                        if (t === "fold")
                                            return theme ? theme.subText : "#888888";
                                        if (t === "empty")
                                            return "transparent";
                                        return theme ? theme.text : "#333333";
                                    }
                                    font.family: Next.PlatformFonts.monospaceFamily
                                    font.pixelSize: 13
                                    font.italic: modelData.leftType === "fold"
                                    wrapMode: Text.NoWrap
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }

                        // ====== 分割线 ======
                        Rectangle {
                            width: 2
                            height: parent.height
                            color: theme ? theme.controlBorder : "#cccccc"
                        }

                        // ====== 右侧面板（远程） ======
                        Rectangle {
                            width: parent.width / 2 - 1
                            height: parent.height
                            color: {
                                var t = modelData.rightType;
                                if (t === "add")
                                    return theme ? Qt.rgba(theme.success.r, theme.success.g, theme.success.b, 0.1) : "#1a4caf50";
                                if (t === "fold")
                                    return theme ? Qt.rgba(0.5, 0.5, 0.5, 0.08) : "#08888888";
                                return "transparent";
                            }

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 4
                                spacing: 4

                                Text {
                                    width: 16
                                    height: parent.height
                                    text: {
                                        var t = modelData.rightType;
                                        if (t === "add")
                                            return "+";
                                        if (t === "fold")
                                            return "⋯";
                                        return " ";
                                    }
                                    color: {
                                        var t = modelData.rightType;
                                        if (t === "add")
                                            return theme ? theme.success : "#4CAF50";
                                        if (t === "fold")
                                            return theme ? theme.subText : "#888888";
                                        return theme ? theme.subText : "#999999";
                                    }
                                    font.family: Next.PlatformFonts.monospaceFamily
                                    font.pixelSize: 13
                                    font.bold: true
                                    verticalAlignment: Text.AlignVCenter
                                }

                                Text {
                                    id: rightText
                                    objectName: "protocol-diff-right-text"
                                    textFormat: Text.PlainText
                                    width: parent.width - 24
                                    text: modelData.rightText
                                    color: {
                                        var t = modelData.rightType;
                                        if (t === "add")
                                            return theme ? theme.success : "#4CAF50";
                                        if (t === "fold")
                                            return theme ? theme.subText : "#888888";
                                        if (t === "empty")
                                            return "transparent";
                                        return theme ? theme.text : "#333333";
                                    }
                                    font.family: Next.PlatformFonts.monospaceFamily
                                    font.pixelSize: 13
                                    font.italic: modelData.rightType === "fold"
                                    wrapMode: Text.NoWrap
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }
                }

                ScrollBar.vertical: ScrollBar {}
                ScrollBar.horizontal: ScrollBar {}
            }

            // 关闭按钮
            StyledButton {
                text: "关闭"
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 100
                Layout.preferredHeight: 30
                onClicked: diffDialog.close()
            }
        }
    }
}
