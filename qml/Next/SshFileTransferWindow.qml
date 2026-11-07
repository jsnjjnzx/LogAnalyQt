pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import LogAnalyQt
import LogAnalyQt.Next as Next

ApplicationWindow {
    id: root
    objectName: "ssh-file-transfer-window"
    title: connectionName ? connectionName + " — 文件传输" : "文件传输"
    width: 1120; height: 700
    minimumWidth: 790; minimumHeight: 490
    visible: false
    modality: Qt.NonModal

    property string connectionName: ""
    property var theme
    color: theme ? theme.background : palette.window
    readonly property color panelColor: theme ? theme.surface : palette.window
    readonly property color wellColor: theme ? theme.terminalBackground : palette.base
    readonly property color edgeColor: theme ? theme.groupBorder : palette.mid
    readonly property color ink: theme ? theme.text : palette.text
    readonly property color mutedInk: theme ? theme.subText : palette.text
    readonly property color accent: theme ? theme.accent : palette.highlight
    readonly property color selectedFill: theme ? theme.monitorTabSelected : palette.highlight
    readonly property color success: theme ? theme.success : "#388458"
    readonly property color error: theme ? theme.error : "#d54747"

    property bool connected: false
    property bool closingWindow: false
    property string status: ""
    property bool failed: false
    property var trustClient: null
    property string pendingFingerprint: ""
    property string pendingEndpoint: ""
    property var editPane: null
    property string editPath: ""
    property string editName: ""
    property var jobs: []
    property int nextJobId: 1
    property int runningJobId: -1
    property bool failuresOnly: false
    property bool queueExpanded: true
    readonly property bool canBrowse: !browser.busy && !trustClient
    readonly property bool ftp: protocol.currentIndex === 1
    readonly property bool hasQueuedWork: runningJobId >= 0 || jobs.some(job => job.state === "waiting")
    readonly property bool confirmedPaths: localPane.path !== "" && remotePane.path !== "" &&
        localPane.path === localPane.pathInput.text && remotePane.path === remotePane.pathInput.text
    readonly property var shownJobs: failuresOnly ? jobs.filter(job => job.state === "failed") : jobs
    readonly property int failureCount: jobs.filter(job => job.state === "failed").length
    readonly property bool configurationValid: hostInput.text.trim() !== "" && portInput.acceptableInput &&
                                                userInput.text.trim() !== "" && (ftp || passwordInput.text !== "")

    FontLoader { id: fileIcons; source: "qrc:/qt/qml/LogAnalyQt/assets/materialdesignicons-webfont.ttf" }

    function present(window, target) {
        connectionName = target.name;
        if (!visible) {
            transientParent = window || null;
            hostInput.text = target.host;
            userInput.text = target.username;
            portInput.text = String(target.port);
            remotePane.pathInput.text = ".";
            if (!localPane.path.length && !browser.busy)
                browser.browseLocal(browser.homePath());
        }
        showNormal(); raise(); requestActivate();
    }

    function connectionOptions() {
        return {host: hostInput.text.trim(), port: Number(portInput.text), user: userInput.text.trim(),
                password: passwordInput.text, ftp: ftp};
    }
    function connectionKey() {
        return JSON.stringify([hostInput.text.trim().toLowerCase(), Number(portInput.text), userInput.text.trim(), ftp]);
    }
    function disconnectTransfer() {
        connected = false;
        remotePane.clearListing();
        passwordInput.clear();
        status = ""; failed = false;
    }
    function browse(pane, path) {
        if (closingWindow || !canBrowse) return;
        status = ""; failed = false;
        if (pane.local) {
            browser.browseLocal(path);
        } else {
            status = connected ? "正在读取远程目录…" : "正在连接…";
            browser.run("list", connectionOptions(), path, localPane.path, "");
        }
    }
    function askTrust(client, endpoint, fingerprint) {
        if (closingWindow || trustClient) { client.confirmHost(false); return; }
        trustClient = client;
        pendingEndpoint = endpoint; pendingFingerprint = fingerprint;
        trustDialog.open();
    }
    function answerTrust(accept) {
        const client = trustClient;
        trustClient = null;
        if (client) client.confirmHost(accept);
        Qt.callLater(root.processQueue);
    }
    function startEdit(pane, name) {
        if (!pane.canEdit) return;
        editPane = pane; editPath = pane.path; editName = name;
        nameInput.text = name;
        nameDialog.open();
    }
    function submitEdit() {
        const name = nameInput.text;
        if (!editPane || name === "" || name === editName) return;
        const action = editName ? "rename" : "mkdir";
        status = action === "rename" ? "正在重命名…" : "正在新建文件夹…";
        failed = false;
        if (editPane.local) {
            browser.run(editName ? "localRename" : "localMkdir", {}, "", editPath,
                        editName || name, editName ? name : "");
        } else {
            browser.run(action, connectionOptions(), editPath, localPane.path,
                        editName || name, editName ? name : "");
        }
    }
    function enqueue(pane) {
        if (!connected || !confirmedPaths || closingWindow) return;
        const key = connectionKey();
        const direction = pane.local ? "upload" : "download";
        const additions = [];
        const activeKeys = new Set(jobs.filter(job => job.state === "waiting" || job.state === "active" || job.state === "canceling")
            .map(job => JSON.stringify([job.key, job.direction, job.localPath, job.remotePath, job.name])));
        for (const row of pane.selectedFiles) {
            const identity = JSON.stringify([key, direction, localPane.path, remotePane.path, row.name]);
            if (activeKeys.has(identity)) continue;
            activeKeys.add(identity);
            additions.push({id: nextJobId++, key: key, direction: direction, localPath: localPane.path,
                            remotePath: remotePane.path, name: row.name, size: row.size, state: "waiting", detail: ""});
        }
        if (!additions.length) return;
        if (jobs.length + additions.length > 2000) {
            status = "队列最多 2,000 项，请清除历史记录后再添加。"; failed = true;
            return;
        }
        jobs = jobs.concat(additions);
        queueExpanded = true; failuresOnly = false;
        processQueue();
    }
    function setJob(id, state, detail) {
        jobs = jobs.map(job => job.id === id ? Object.assign({}, job, {state: state, detail: detail || ""}) : job);
    }
    function processQueue() {
        if (closingWindow || trustClient || worker.busy || runningJobId >= 0) return;
        const job = connected ? jobs.find(job => job.state === "waiting") : null;
        if (!job) {
            if (!canBrowse || nameDialog.visible || settingsDialog.visible || failed) return;
            if (localPane.refreshNeeded && localPane.path === localPane.pathInput.text) {
                localPane.refreshNeeded = false;
                browse(localPane, localPane.path);
            } else if (connected && remotePane.refreshNeeded && remotePane.path === remotePane.pathInput.text) {
                remotePane.refreshNeeded = false;
                browse(remotePane, remotePane.path);
            }
            return;
        }
        if (job.key !== connectionKey()) {
            setJob(job.id, "failed", "连接已更改");
            Qt.callLater(root.processQueue);
            return;
        }
        runningJobId = job.id;
        setJob(job.id, "active");
        worker.run(job.direction, connectionOptions(), job.remotePath, job.localPath, job.name);
    }
    function queueAction(job) {
        if (job.state === "waiting") setJob(job.id, "cancelled");
        else if (job.state === "active") {
            setJob(job.id, "canceling");
            worker.cancel();
        } else if ((job.state === "failed" || job.state === "cancelled") && connected && job.key === connectionKey()) {
            setJob(job.id, "waiting");
            processQueue();
        }
    }
    function bytesText(bytes) {
        if (bytes < 0) return "—";
        if (bytes < 1024) return bytes + " B";
        if (bytes < 1048576) return (bytes / 1024).toFixed(1) + " KiB";
        if (bytes < 1073741824) return (bytes / 1048576).toFixed(1) + " MiB";
        return (bytes / 1073741824).toFixed(1) + " GiB";
    }
    function jobStateText(job) {
        switch (job.state) {
        case "waiting": return "等待";
        case "active": return "传输中";
        case "done": return "已完成";
        case "failed": return "失败";
        case "canceling": return "正在取消";
        default: return "已取消";
        }
    }

    onClosing: {
        closingWindow = true;
        trustClient = null; trustDialog.close();
        browser.cancel(); worker.cancel();
        passwordInput.clear();
    }

    SshFileTransfer {
        id: browser
        objectName: "ssh-file-browser"
        onHostKeyRequired: (endpoint, fingerprint) => root.askTrust(browser, endpoint, fingerprint)
        onFinished: function(action, result) {
            if (root.closingWindow) return;
            if (result.error) {
                const pane = action === "local" ? localPane : action === "list" ? remotePane : null;
                if (pane && pane.path) pane.pathInput.text = pane.path;
                root.status = result.error; root.failed = true;
            } else if (action === "local" || action === "list") {
                (action === "local" ? localPane : remotePane).showListing(result);
                if (action === "list") root.connected = true;
                root.status = ""; root.failed = false;
            } else {
                const pane = root.editPane;
                if (pane && pane.path === root.editPath) pane.refreshNeeded = true;
                root.status = ""; root.failed = false;
            }
            Qt.callLater(root.processQueue);
        }
    }
    SshFileTransfer {
        id: worker
        objectName: "ssh-file-worker"
        onHostKeyRequired: (endpoint, fingerprint) => root.askTrust(worker, endpoint, fingerprint)
        onFinished: function(action, result) {
            if (root.closingWindow) return;
            const job = root.jobs.find(job => job.id === root.runningJobId);
            root.runningJobId = -1;
            if (job) {
                if (result.error) {
                    const cleanCancel = result.error === "已取消";
                    root.setJob(job.id, cleanCancel ? "cancelled" : "failed", result.error);
                } else {
                    root.setJob(job.id, "done");
                    const pane = job.direction === "upload" ? remotePane : localPane;
                    const path = pane.local ? job.localPath : job.remotePath;
                    if (pane.path === path) pane.refreshNeeded = true;
                }
            }
            Qt.callLater(root.processQueue);
        }
    }

    Dialogs.FolderDialog {
        id: chooseLocal
        title: "选择本地目录"
        onAccepted: root.browse(localPane, browser.localPath(selectedFolder))
    }
    Next.GlassDialog {
        id: settingsDialog
        title: "连接设置"
        parent: root.contentItem; anchors.centerIn: parent
        width: 420; modal: true
        property var previous: null
        onAboutToShow: previous = {protocol: protocol.currentIndex, host: hostInput.text,
                                  port: portInput.text, user: userInput.text, password: passwordInput.text,
                                  path: remotePane.pathInput.text}
        onRejected: {
            if (!previous) return;
            protocol.currentIndex = previous.protocol; hostInput.text = previous.host;
            portInput.text = previous.port; userInput.text = previous.user; passwordInput.text = previous.password;
            remotePane.pathInput.text = previous.path;
        }
        onClosed: { previous = null; Qt.callLater(root.processQueue); }
        onOpened: if (hostInput.text && userInput.text) passwordInput.forceActiveFocus()
        onAccepted: root.browse(remotePane, remotePane.pathInput.text)
        contentItem: GridLayout {
            columns: 2; rowSpacing: 8; columnSpacing: 12
            Label { text: "协议" }
            Next.GlassComboBox {
                id: protocol
                model: ["SFTP", "FTP（明文）"]
                Layout.preferredWidth: 150
                Accessible.name: "协议"
                onActivated: {
                    portInput.text = root.ftp ? "21" : "22";
                    remotePane.pathInput.text = root.ftp ? "/" : ".";
                }
            }
            Label { text: "主机" }
            Next.GlassTextField { id: hostInput; Layout.fillWidth: true; Accessible.name: "主机" }
            Label { text: "端口" }
            Next.GlassTextField {
                id: portInput; Layout.preferredWidth: 80; Accessible.name: "端口"
                validator: IntValidator { bottom: 1; top: 65535 }
            }
            Label { text: "账号" }
            Next.GlassTextField { id: userInput; Layout.fillWidth: true; Accessible.name: "账号" }
            Label { text: "密码" }
            Next.GlassTextField {
                id: passwordInput; Layout.fillWidth: true; Accessible.name: "密码"
                echoMode: TextInput.Password
                onAccepted: if (root.configurationValid) settingsDialog.accept()
            }
            Label { text: "远程目录" }
            Next.GlassTextField {
                text: remotePane.pathInput.text
                Layout.fillWidth: true; Accessible.name: "起始远程目录"
                onTextEdited: remotePane.pathInput.text = text
            }
        }
        footer: DialogButtonBox {
            Next.GlassButton { text: "取消"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Next.GlassButton {
                text: "连接"; highlighted: true; enabled: root.configurationValid
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
    }
    Next.GlassDialog {
        id: nameDialog
        title: root.editName ? "重命名" : "新建文件夹"
        parent: root.contentItem; anchors.centerIn: parent
        width: 380; modal: true
        readonly property bool nameValid: nameInput.text.trim().length > 0 &&
            nameInput.text !== "." && nameInput.text !== ".." && !/[\\/]/.test(nameInput.text) &&
            nameInput.text !== root.editName
        onOpened: { nameInput.forceActiveFocus(); nameInput.selectAll(); }
        onAccepted: root.submitEdit()
        onClosed: Qt.callLater(root.processQueue)
        contentItem: Next.GlassTextField {
            id: nameInput; maximumLength: 255; Accessible.name: "名称"; placeholderText: "名称"
            onAccepted: if (nameDialog.nameValid) nameDialog.accept()
        }
        footer: DialogButtonBox {
            Next.GlassButton { text: "取消"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Next.GlassButton {
                text: "确定"; highlighted: true; enabled: nameDialog.nameValid
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
    }
    Next.GlassDialog {
        id: trustDialog
        title: "确认 SFTP 主机指纹"
        parent: root.contentItem; anchors.centerIn: parent
        width: Math.min(600, root.width - 36); modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            standardButton(Dialog.Ok).text = "信任并连接";
            standardButton(Dialog.Cancel).text = "取消";
        }
        onAccepted: root.answerTrust(true)
        onRejected: root.answerTrust(false)
        contentItem: ColumnLayout {
            Label { text: root.pendingEndpoint; textFormat: Text.PlainText }
            Label {
                Layout.fillWidth: true; text: root.pendingFingerprint; textFormat: Text.PlainText
                font.family: Next.PlatformFonts.monospaceFamily; wrapMode: Text.WrapAnywhere
            }
            Label { text: "请先与服务器管理员核对指纹。"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12
        spacing: 9

        RowLayout {
            Layout.fillWidth: true; spacing: 10
            Rectangle {
                Layout.preferredWidth: 35; Layout.preferredHeight: 35; radius: 8
                color: root.selectedFill
                Glyph { anchors.centerIn: parent; text: Next.Icons.cloud; color: root.accent }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 1
                Label {
                    Layout.fillWidth: true; text: root.connectionName || "文件传输"; textFormat: Text.PlainText
                    elide: Text.ElideRight; color: root.ink; font.pixelSize: 15; font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    text: userInput.text + "@" + hostInput.text + ":" + portInput.text
                    textFormat: Text.PlainText; color: root.mutedInk; elide: Text.ElideMiddle
                    font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 11
                }
            }
            Rectangle {
                Layout.preferredWidth: protocolLabel.implicitWidth + 12
                Layout.preferredHeight: 21; radius: 4
                color: root.selectedFill
                Label { id: protocolLabel; anchors.centerIn: parent; text: root.ftp ? "FTP · 明文" : "SFTP"; color: root.accent; font.pixelSize: 11 }
            }
            Rectangle {
                Layout.preferredWidth: 6; Layout.preferredHeight: 6; radius: 3
                color: root.connected ? root.success : root.mutedInk
            }
            Label { text: root.connected ? "已连接" : "未连接"; color: root.connected ? root.success : root.mutedInk; font.pixelSize: 12 }
            CompactButton {
                text: root.connected ? "断开" : "连接…"
                highlighted: !root.connected
                enabled: root.canBrowse && !root.hasQueuedWork
                onClicked: root.connected ? root.disconnectTransfer() : settingsDialog.open()
            }
        }

        SplitView {
            Layout.fillWidth: true; Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Item {
                implicitWidth: 7
                Rectangle {
                    anchors.centerIn: parent; width: 1; height: parent.height - 12
                    color: parent.SplitHandle.pressed || parent.SplitHandle.hovered ? root.accent : "transparent"
                }
            }
            FilePane {
                id: localPane; local: true
                objectName: "ssh-file-local-pane"
                SplitView.minimumWidth: 334
                SplitView.preferredWidth: Math.floor((root.width - 31) / 2)
            }
            FilePane {
                id: remotePane
                objectName: "ssh-file-remote-pane"
                SplitView.minimumWidth: 334
                SplitView.fillWidth: true
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: queueLayout.implicitHeight + 2
            color: root.panelColor; border.color: root.edgeColor; radius: 8
            ColumnLayout {
                id: queueLayout
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 1
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 37
                    Layout.leftMargin: 8; Layout.rightMargin: 8; spacing: 6
                    IconButton {
                        text: root.queueExpanded ? "收起队列" : "展开队列"; glyph: root.queueExpanded ? "\u{F0140}" : "\u{F0142}"
                        onClicked: root.queueExpanded = !root.queueExpanded
                    }
                    Label { text: "传输队列"; color: root.ink; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        text: root.jobs.length ? String(root.jobs.length) : ""
                        color: root.mutedInk; font.pixelSize: 11
                    }
                    CompactButton {
                        text: "全部"; highlighted: !root.failuresOnly
                        onClicked: { root.failuresOnly = false; root.queueExpanded = true; }
                    }
                    CompactButton {
                        text: root.failureCount ? "失败 " + root.failureCount : "失败"
                        highlighted: root.failuresOnly
                        onClicked: { root.failuresOnly = true; root.queueExpanded = true; }
                    }
                    CompactButton {
                        text: "清除记录"
                        enabled: root.jobs.some(job => ["done", "failed", "cancelled"].includes(job.state))
                        ToolTip.visible: hovered
                        ToolTip.text: "清除已完成、失败和取消的任务"
                        onClicked: root.jobs = root.jobs.filter(job => !["done", "failed", "cancelled"].includes(job.state))
                    }
                }
                ListView {
                    id: queueView
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(134, Math.max(1, count) * 31)
                    visible: root.queueExpanded
                    clip: true; model: root.shownJobs
                    ScrollBar.vertical: ScrollBar {}
                    Label { anchors.centerIn: parent; visible: queueView.count === 0; color: root.mutedInk; text: root.failuresOnly ? "无失败任务" : "暂无传输任务" }
                    delegate: Rectangle {
                        id: queueRow
                        required property var modelData
                        width: queueView.width; height: 31
                        color: root.wellColor
                        readonly property bool active: modelData.state === "active" || modelData.state === "canceling"
                        readonly property real percent: modelData.size > 0 && worker.transferredBytes > 0
                            ? Math.min(99, Math.floor(100 * worker.transferredBytes / modelData.size)) : -1
                        Rectangle { width: parent.width; height: 1; color: root.edgeColor }
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 11; anchors.rightMargin: 11; spacing: 8
                            Glyph {
                                text: queueRow.modelData.direction === "upload" ? "\u{F0054}" : "\u{F004D}"
                                color: root.mutedInk
                            }
                            Label {
                                Layout.fillWidth: true; Layout.minimumWidth: 0
                                text: queueRow.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; color: root.ink
                                HoverHandler { id: taskHover }
                                ToolTip.visible: taskHover.hovered
                                ToolTip.text: queueRow.modelData.direction === "upload"
                                    ? queueRow.modelData.localPath + " → " + queueRow.modelData.remotePath
                                    : queueRow.modelData.remotePath + " → " + queueRow.modelData.localPath
                            }
                            Label { text: root.bytesText(queueRow.modelData.size); color: root.mutedInk; font.pixelSize: 11 }
                            ProgressBar {
                                Layout.preferredWidth: 104; Layout.preferredHeight: 5
                                visible: queueRow.active; from: 0; to: 100
                                value: Math.max(0, queueRow.percent); indeterminate: queueRow.percent < 0
                            }
                            Label {
                                Layout.preferredWidth: 88; horizontalAlignment: Text.AlignRight
                                text: queueRow.active && queueRow.percent >= 0
                                    ? queueRow.percent + "% · " + root.jobStateText(queueRow.modelData)
                                    : root.jobStateText(queueRow.modelData)
                                color: queueRow.modelData.state === "done" ? root.success
                                    : queueRow.modelData.state === "failed" ? root.error : root.mutedInk
                                font.pixelSize: 11
                                HoverHandler { id: stateHover }
                                ToolTip.visible: stateHover.hovered && queueRow.modelData.detail !== ""
                                ToolTip.text: queueRow.modelData.detail
                            }
                            Item {
                                Layout.preferredWidth: 48; Layout.preferredHeight: 25
                                CompactButton {
                                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                    visible: queueRow.modelData.state !== "done"
                                    text: queueRow.modelData.state === "failed" || queueRow.modelData.state === "cancelled" ? "重试" : "取消"
                                    enabled: queueRow.modelData.state !== "canceling" &&
                                        (text !== "重试" || root.connected && queueRow.modelData.key === root.connectionKey())
                                    onClicked: root.queueAction(queueRow.modelData)
                                }
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            visible: root.status !== "" || browser.busy
            Layout.fillWidth: true
            BusyIndicator { running: browser.busy; visible: running; implicitWidth: 19; implicitHeight: 19 }
            Label {
                Layout.fillWidth: true; text: root.status; textFormat: Text.PlainText
                color: root.failed ? root.error : root.mutedInk; elide: Text.ElideRight
                HoverHandler { id: statusHover }
                ToolTip.visible: truncated && statusHover.hovered; ToolTip.text: text
            }
            CompactButton { text: "取消"; visible: browser.busy; onClicked: browser.cancel() }
        }
    }

    component Glyph: Label {
        font.family: fileIcons.name || "Material Design Icons"
        font.pixelSize: 16
        color: root.mutedInk
        Accessible.ignored: true
    }
    component CompactButton: Next.GlassButton {
        implicitWidth: Math.max(34, implicitContentWidth + leftPadding + rightPadding)
        implicitHeight: 27
        leftPadding: 9; rightPadding: 9; topPadding: 2; bottomPadding: 2
        font.pixelSize: 12
    }
    component IconButton: Button {
        id: iconButton
        required property string glyph
        implicitWidth: 28; implicitHeight: 28
        padding: 0
        hoverEnabled: true
        Accessible.name: text
        contentItem: Glyph {
            text: iconButton.glyph
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            color: iconButton.enabled ? root.mutedInk : Qt.alpha(root.mutedInk, 0.5)
        }
        background: Rectangle {
            radius: 5
            color: iconButton.down || iconButton.hovered || iconButton.visualFocus ? root.selectedFill : "transparent"
            border.width: iconButton.visualFocus ? 1 : 0; border.color: root.accent
        }
        ToolTip.visible: hovered
        ToolTip.text: text
    }

    component FilePane: Rectangle {
        id: pane
        property bool local: false
        property string path: ""
        property var rows: []
        property var selection: []
        property string rangeStart: ""
        property string sortField: "name"
        property bool descending: false
        property bool refreshNeeded: false
        property alias pathInput: pathInput
        readonly property bool ready: local || root.connected
        readonly property bool canManage: ready && path !== "" && root.canBrowse
        readonly property bool canEdit: canManage && path === pathInput.text && !root.hasQueuedWork
        readonly property var visibleRows: sortedRows()
        readonly property var selectedNames: new Set(selection)
        readonly property var picked: visibleRows.filter(row => selectedNames.has(row.name))
        readonly property var selectedFiles: picked.filter(row => row.file)
        readonly property bool canRename: canEdit && picked.length === 1 && (picked[0].directory || picked[0].file)
        readonly property bool canTransfer: root.connected && root.confirmedPaths && selectedFiles.length > 0
        color: root.panelColor; border.color: root.edgeColor; radius: 8

        function sortedRows() {
            const needle = filterInput.text.trim().toLowerCase();
            return rows.filter(row => row.name.toLowerCase().includes(needle)).sort(function(a, b) {
                if (a.directory !== b.directory) return a.directory ? -1 : 1;
                const value = sortField === "name" ? a.name.localeCompare(b.name) : a[sortField] - b[sortField];
                return (descending ? -value : value) || a.name.localeCompare(b.name);
            });
        }
        function resetSelection() { selection = []; rangeStart = ""; }
        function clearListing() {
            path = ""; rows = []; resetSelection(); refreshNeeded = false;
        }
        function showListing(result) {
            if (path !== result.path) {
                refreshNeeded = false;
                filterInput.clear(); resetSelection(); files.positionViewAtBeginning();
            } else {
                const names = new Set(result.entries.map(row => row.name));
                selection = selection.filter(name => names.has(name));
                if (!names.has(rangeStart)) rangeStart = "";
            }
            path = result.path;
            pathInput.text = path; rows = result.entries;
        }
        function enterDirectory(entry) {
            if (entry.directory && canManage) root.browse(pane, path + (path.endsWith("/") ? "" : "/") + entry.name);
        }
        function selectRow(index, modifiers) {
            const entry = visibleRows[index];
            if (!entry) return;
            files.currentIndex = index;
            const anchor = modifiers & Qt.ShiftModifier ? visibleRows.findIndex(row => row.name === rangeStart) : -1;
            if (anchor >= 0) {
                selection = visibleRows.slice(Math.min(anchor, index), Math.max(anchor, index) + 1).map(row => row.name);
            } else {
                if (modifiers & (Qt.ControlModifier | Qt.MetaModifier)) {
                    selection = selection.includes(entry.name) ? selection.filter(name => name !== entry.name)
                                                                 : selection.concat([entry.name]);
                } else selection = [entry.name];
                rangeStart = entry.name;
            }
        }
        function sortBy(field) {
            descending = sortField === field && !descending;
            sortField = field;
        }
        function sortTitle(field, title) {
            return title + (sortField === field ? (descending ? " ↓" : " ↑") : "");
        }

        ColumnLayout {
            anchors.fill: parent; anchors.margins: 1; spacing: 0
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: 39
                Layout.leftMargin: 9; Layout.rightMargin: 8; spacing: 6
                Glyph { text: pane.local ? Next.Icons.file : Next.Icons.cloud }
                Label { Layout.fillWidth: true; text: pane.local ? "本地" : "远程"; color: root.ink; font.bold: true }
                IconButton { glyph: "\u{F0257}"; text: "新建文件夹"; enabled: pane.canEdit; onClicked: root.startEdit(pane, "") }
                IconButton { glyph: "\u{F0455}"; text: "重命名"; enabled: pane.canRename; onClicked: root.startEdit(pane, pane.picked[0].name) }
                CompactButton {
                    text: (pane.local ? "上传" : "下载") + (pane.selectedFiles.length ? " " + pane.selectedFiles.length : "")
                    highlighted: pane.local
                    enabled: pane.canTransfer
                    onClicked: root.enqueue(pane)
                }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.leftMargin: 7; Layout.rightMargin: 7; Layout.bottomMargin: 6
                spacing: 4
                IconButton {
                    glyph: "\u{F005D}"; text: "上一级目录"
                    enabled: pane.canManage && pane.path !== "/" && !/^[A-Za-z]:\/$/.test(pane.path)
                    onClicked: root.browse(pane, pane.local ? browser.parentPath(pane.path) : pane.path.slice(0, pane.path.lastIndexOf("/")) || "/")
                }
                IconButton { glyph: Next.Icons.refresh; text: "刷新"; enabled: pane.canManage; onClicked: root.browse(pane, pathInput.text) }
                IconButton { visible: pane.local; glyph: "\u{F0770}"; text: "选择本地目录"; enabled: root.canBrowse; onClicked: chooseLocal.open() }
                Next.GlassTextField {
                    id: pathInput
                    Layout.fillWidth: true; Layout.minimumWidth: 55; Layout.preferredHeight: 28
                    placeholderText: pane.local ? "本地路径" : "远程路径"
                    Accessible.name: placeholderText; font.family: Next.PlatformFonts.monospaceFamily; font.pixelSize: 11
                    enabled: root.canBrowse && pane.ready
                    onAccepted: root.browse(pane, text)
                }
                Next.GlassTextField {
                    id: filterInput
                    Layout.preferredWidth: 80; Layout.preferredHeight: 28
                    placeholderText: "筛选…"; Accessible.name: pane.local ? "筛选本地目录" : "筛选远程目录"
                    enabled: pane.ready
                    font.pixelSize: 11
                    onTextEdited: pane.resetSelection()
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 26
                color: root.selectedFill
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 6
                    HeaderButton { Layout.fillWidth: true; text: pane.sortTitle("name", "名称"); onClicked: pane.sortBy("name") }
                    HeaderButton { Layout.preferredWidth: 68; rightAligned: true; text: pane.sortTitle("size", "大小"); onClicked: pane.sortBy("size") }
                    HeaderButton { Layout.preferredWidth: 97; text: pane.sortTitle("modified", "修改时间"); onClicked: pane.sortBy("modified") }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                color: root.wellColor
                ListView {
                    id: files
                    anchors.fill: parent; clip: true
                    model: pane.visibleRows
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ItemDelegate {
                        id: fileRow
                        required property int index
                        required property var modelData
                        width: files.width - 4; height: 29
                        padding: 0; leftPadding: 8; rightPadding: 4
                        focusPolicy: Qt.StrongFocus
                        readonly property bool picked: pane.selectedNames.has(modelData.name)
                        Accessible.name: modelData.name + (modelData.directory ? "，文件夹" : "")
                        Accessible.description: picked ? "已选中" : ""
                        background: Rectangle {
                            color: fileRow.picked || fileRow.visualFocus ? root.selectedFill
                                 : pointer.containsMouse ? (root.theme ? root.theme.surfaceHover : root.panelColor) : "transparent"
                            Rectangle { visible: fileRow.picked; width: 2; height: parent.height; color: root.accent }
                        }
                        contentItem: RowLayout {
                            spacing: 6
                            Glyph {
                                text: fileRow.modelData.directory ? Next.Icons.folder : Next.Icons.fileDocument
                                color: fileRow.modelData.directory ? (root.theme ? root.theme.warning : "#926000") : root.mutedInk
                            }
                            Label {
                                Layout.fillWidth: true; Layout.minimumWidth: 0
                                text: fileRow.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; color: root.ink
                                ToolTip.visible: truncated && pointer.containsMouse; ToolTip.text: text
                            }
                            Label {
                                Layout.preferredWidth: 68; horizontalAlignment: Text.AlignRight
                                text: root.bytesText(fileRow.modelData.directory ? -1 : fileRow.modelData.size)
                                color: root.mutedInk; font.pixelSize: 11
                            }
                            Label {
                                Layout.preferredWidth: 97
                                text: fileRow.modelData.modified ? Qt.formatDateTime(new Date(fileRow.modelData.modified), "MM/dd HH:mm") : "—"
                                color: root.mutedInk; font.pixelSize: 11
                            }
                        }
                        onClicked: pane.selectRow(index, Qt.NoModifier)
                        Keys.onReturnPressed: pane.enterDirectory(modelData)
                        Keys.onEnterPressed: pane.enterDirectory(modelData)
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_A && (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier))) {
                                pane.selection = pane.visibleRows.map(row => row.name); event.accepted = true;
                            } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                                const next = Math.max(0, Math.min(files.count - 1, index + (event.key === Qt.Key_Up ? -1 : 1)));
                                pane.selectRow(next, event.modifiers);
                                files.positionViewAtIndex(next, ListView.Contain);
                                const item = files.itemAtIndex(next);
                                if (item) item.forceActiveFocus(Qt.TabFocusReason);
                                event.accepted = true;
                            } else if (event.key === Qt.Key_Menu) {
                                if (!fileRow.picked) pane.selectRow(index, Qt.NoModifier);
                                fileMenu.popup(); event.accepted = true;
                            }
                        }
                        MouseArea {
                            id: pointer
                            anchors.fill: parent; hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: function(mouse) {
                                fileRow.forceActiveFocus(Qt.MouseFocusReason);
                                if (mouse.button === Qt.RightButton) {
                                    if (!fileRow.picked) pane.selectRow(fileRow.index, Qt.NoModifier);
                                    fileMenu.popup();
                                } else pane.selectRow(fileRow.index, mouse.modifiers);
                            }
                            onDoubleClicked: function(mouse) {
                                if (mouse.button === Qt.LeftButton && mouse.modifiers === Qt.NoModifier)
                                    pane.enterDirectory(fileRow.modelData);
                                else mouse.accepted = false;
                            }
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent; visible: !files.count; color: root.mutedInk
                    text: !pane.ready ? "未连接" : filterInput.text ? "没有匹配项" : "此目录为空"
                }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: 27
                Layout.leftMargin: 9; Layout.rightMargin: 9
                Label { Layout.fillWidth: true; color: root.mutedInk; font.pixelSize: 11; text: pane.ready ? pane.visibleRows.length + " 项" : "" }
                Label { color: root.mutedInk; font.pixelSize: 11; text: pane.picked.length ? "已选 " + pane.picked.length + " 项" : "" }
            }
        }
        Next.GlassMenu {
            id: fileMenu
            Next.GlassMenuItem { text: pane.local ? "上传" : "下载"; enabled: pane.canTransfer; onTriggered: root.enqueue(pane) }
            Next.GlassMenuItem { text: "重命名…"; enabled: pane.canRename; onTriggered: root.startEdit(pane, pane.picked[0].name) }
            Next.GlassMenuItem { text: "新建文件夹…"; enabled: pane.canEdit; onTriggered: root.startEdit(pane, "") }
        }
    }
    component HeaderButton: Button {
        id: columnButton
        property bool rightAligned: false
        implicitHeight: 24; padding: 0
        contentItem: Label {
            text: columnButton.text; color: root.mutedInk; font.pixelSize: 11; elide: Text.ElideRight
            horizontalAlignment: columnButton.rightAligned ? Text.AlignRight : Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { color: columnButton.visualFocus ? root.panelColor : "transparent"; radius: 3 }
    }
}
