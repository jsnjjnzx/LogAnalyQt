import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import LogAnalyQt.Next as Next

ColumnLayout {
    id: root
    objectName: "personalConfigPage"
    required property var theme
    property var flushAction: null
    property var manager: typeof personalConfigManager !== "undefined" ? personalConfigManager : null
    property string notice: ""
    property bool failed: false
    property bool restoring: false
    property bool importing: false
    property var groups: []
    property var choices: ({})
    property var selectedGroups: ({})
    property bool fullBackup: false
    Layout.fillWidth: true; spacing: 14
    function show(reply) { failed = !reply || !reply.ok; notice = reply && reply.message ? reply.message : failed ? "操作失败" : "操作完成。" }
    function flushEdits() {
        if (root.flushAction && !root.flushAction()) { root.failed = true; root.notice = "当前编辑内容尚未保存，请处理保存错误后重试。"; return false }
        return true
    }
    Label { text: "个人配置"; color: root.theme.text; font.pixelSize: 23; font.bold: true }
    Label { objectName: "personal-config-summary"; text: root.manager ? root.manager.summary : "尚未连接配置管理器"; textFormat: Text.PlainText; color: root.theme.subText; Layout.fillWidth: true; wrapMode: Text.Wrap }
    Label { text: "备份可能包含报文和自定义命令等私人内容。密码、私钥及设备安全状态不导出。"; color: root.theme.subText; Layout.fillWidth: true; wrapMode: Text.Wrap }
    CheckBox { id: includeWorkspace; text: "包含解析内容、MQTT 草稿和发送历史"; checked: true; palette.windowText: root.theme.text }
    CheckBox { id: includeTrust; text: "完整备份中包含 SSH 主机信任记录"; checked: false; palette.windowText: root.theme.text }
    Flow {
        Layout.fillWidth: true; spacing: 10
        Next.GlassButton { text: "导出配置 JSON"; enabled: !!root.manager; onClicked: { if (!root.flushEdits()) return; root.fullBackup = false; saveDialog.nameFilters = ["JSON 配置 (*.json)"]; saveDialog.open() } }
        Next.GlassButton { objectName: "personal-config-export-full"; text: "导出完整备份包"; enabled: !!root.manager; onClicked: { if (!root.flushEdits()) return; root.fullBackup = true; saveDialog.nameFilters = ["LogAnalyQt 备份 (*.laqbackup)"]; saveDialog.open() } }
        Next.GlassButton { text: "恢复备份…"; enabled: !!root.manager; onClicked: { if (root.flushEdits()) restoreDialog.open() } }
        Next.GlassButton { text: "导入旧配置…"; enabled: !!root.manager; onClicked: legacyDialog.open() }
        Next.GlassButton { text: "打开数据目录"; enabled: !!root.manager; onClicked: root.manager.openDirectory() }
        Next.GlassButton { text: "取消待应用操作"; enabled: !!root.manager; onClicked: root.show(root.manager.cancelPending()) }
        Next.GlassButton { text: "刷新状态"; enabled: !!root.manager; onClicked: root.manager.refresh() }
    }
    ColumnLayout {
        visible: root.restoring
        Layout.fillWidth: true
        Label { text: "选择要替换的配置分组"; color: root.theme.text }
        Repeater {
            model: root.groups
            delegate: CheckBox {
                required property string modelData
                text: modelData; checked: true; palette.windowText: root.theme.text
                onToggled: { var next = Object.assign({}, root.selectedGroups); next[modelData] = checked; root.selectedGroups = next }
            }
        }
        CheckBox { id: restoreResources; text: "恢复包内协议和提示词"; checked: true; palette.windowText: root.theme.text }
        CheckBox { id: restoreTrust; text: "恢复 SSH 主机信任记录（同一主机指纹冲突将停止）"; checked: false; palette.windowText: root.theme.text }
        Next.GlassButton { text: "确认恢复，关闭软件后下次启动生效"; onClicked: {
            var selected = root.groups.filter(function(group) { return root.selectedGroups[group] !== false })
            root.show(root.manager.stageRestore(selected, restoreResources.checked, restoreTrust.checked))
            if (!root.failed) root.restoring = false
        } }
    }
    ColumnLayout {
        visible: root.importing; Layout.fillWidth: true
        Label { text: "所选目录的原件全部保留；导入在下次启动执行。"; color: root.theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Repeater {
            model: root.manager ? root.manager.conflictDetails : []
            delegate: RowLayout {
                required property string modelData
                Label { text: modelData.key + "\n当前：" + modelData.current + "\n旧配置：" + modelData.legacy; textFormat: Text.PlainText; color: root.theme.warning; Layout.fillWidth: true; wrapMode: Text.Wrap }
                ComboBox { model: ["请选择来源", "保留当前", "使用旧配置"]; onActivated: { var next = Object.assign({}, root.choices); next[modelData.key] = currentIndex === 1 ? "current" : currentIndex === 2 ? "legacy" : ""; root.choices = next } }
            }
        }
        Next.GlassButton { text: "确认导入，关闭软件后下次启动生效"; onClicked: { root.show(root.manager.stageLegacy(root.choices)); if (!root.failed) root.importing = false } }
    }
    Label { text: root.notice; textFormat: Text.PlainText; visible: text.length > 0; color: root.failed ? root.theme.error : root.theme.success; Layout.fillWidth: true; wrapMode: Text.Wrap }
    Dialogs.FileDialog {
        id: saveDialog; fileMode: Dialogs.FileDialog.SaveFile; title: "保存个人配置备份"
        onAccepted: { if (root.flushEdits()) root.show(root.manager.exportBackup(selectedFile, root.fullBackup, includeWorkspace.checked, includeTrust.checked)) }
    }
    Dialogs.FileDialog {
        id: restoreDialog; fileMode: Dialogs.FileDialog.OpenFile; title: "选择配置或完整备份"; nameFilters: ["LogAnalyQt 配置与备份 (*.json *.laqbackup)"]
        onAccepted: {
            if (!root.flushEdits()) return
            var reply = root.manager.previewRestore(selectedFile); root.show(reply)
            root.restoring = !!reply.ok; root.importing = false; root.groups = reply.groups ? reply.groups.filter(function(group) { return group !== "schemaVersion" }) : []; root.selectedGroups = ({})
        }
    }
    Dialogs.FolderDialog {
        id: legacyDialog; title: "选择旧配置目录（原件保留，缺少文件会跳过）"
        onAccepted: { root.show(root.manager.previewLegacy(selectedFolder)); root.importing = !root.failed; root.restoring = false; root.choices = ({}) }
    }
}
