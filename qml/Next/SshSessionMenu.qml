import QtQuick
import QtQuick.Controls
import QtQml.Models
import LogAnalyQt.Next as Next

Next.GlassMenu {
    id: root
    required property var workspace
    property var page: null
    readonly property var otherWindows: page ? workspace.containers.filter(function(container) {
        return container.window && container.window.visible && container !== root.page.group.container;
    }) : []
    parent: page
    palette: workspace.palette
    objectName: "ssh-session-options-" + (page ? page.sessionId : 0)
    onCurrentIndexChanged: { const option = itemAt(currentIndex); if (option instanceof CheckDelegate) option.forceActiveFocus(Qt.TabFocusReason); }
    component SessionOption: CheckDelegate {
        id: option
        required property var menu
        signal keyboardToggleRequested()
        implicitHeight: 30
        implicitWidth: Math.max(212, implicitContentWidth + leftPadding + rightPadding)
        leftPadding: 32; rightPadding: 12
        font.pixelSize: 13
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        highlighted: hovered || visualFocus
        function selectInMenu() {
            for (let i = 0; i < menu.count; ++i)
                if (menu.itemAt(i) === option) { menu.currentIndex = i; return }
        }
        onHoveredChanged: if (hovered) selectInMenu()
        onPressed: selectInMenu()
        Keys.onReturnPressed: keyboardToggleRequested()
        Keys.onEnterPressed: keyboardToggleRequested()
        indicator: Rectangle {
            x: 12; y: (option.height - height) / 2
            width: 14; height: 14; radius: 3
            color: "transparent"; border.width: 1
            border.color: option.highlighted ? option.palette.highlightedText : option.palette.text
            opacity: option.enabled ? 1 : 0.38
            Text {
                objectName: "menu-check-mark"
                anchors.centerIn: parent
                visible: option.checked; text: "✓"; font.pixelSize: 11
                color: parent.border.color
            }
        }
        contentItem: Text {
            text: option.text; textFormat: Text.PlainText; font: option.font
            color: option.highlighted ? option.palette.highlightedText : option.palette.text
            opacity: option.enabled ? 1 : 0.38
            verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 7
            color: option.highlighted && option.enabled ? option.palette.highlight : "transparent"
        }
    }

    SessionOption {
        objectName: "ssh-pause"; menu: root; text: "自动跟随"
        checked: root.page.autoScroll
        onToggled: root.page.toggleFollowing()
        onKeyboardToggleRequested: root.page.toggleFollowing()
        ToolTip.visible: hovered
        ToolTip.text: "只控制自动滚动；取消后仍接收日志。重新勾选恢复跟随，并监测新的输出。"
    }
    SessionOption {
        objectName: "ssh-stop-reception"; menu: root; text: "停止刷屏"
        checked: root.page.terminal.receptionPaused
        onToggled: root.page.terminal.receptionPaused = checked
        onKeyboardToggleRequested: root.page.terminal.receptionPaused = !root.page.terminal.receptionPaused
        ToolTip.visible: hovered
        ToolTip.text: "勾选后停止接收，保留 SSH 连接；取消勾选恢复接收，不改变跟随状态。"
    }
    SessionOption {
        objectName: "ssh-copy-on-select"
        menu: root
        text: "选中即复制"
        checked: root.page.copyOnSelect
        onToggled: root.page.copyOnSelect = checked
        onKeyboardToggleRequested: root.page.copyOnSelect = !root.page.copyOnSelect
    }
    SessionOption {
        objectName: "ssh-wrap-lines"
        menu: root
        text: "自动换行"
        checked: root.page.wrapLogLines
        onToggled: root.page.wrapLogLines = checked
        onKeyboardToggleRequested: root.page.wrapLogLines = !root.page.wrapLogLines
        ToolTip.visible: hovered
        ToolTip.text: "勾选后按窗口宽度换行；关闭时通过底部横向滚动条查看长行。"
    }
    Next.GlassMenuItem { objectName: "ssh-clear"; text: "清屏"; onTriggered: root.page.clearTerminal() }
    Next.GlassMenuSeparator {}
    SessionOption {
        objectName: "ssh-monitor-enabled"; menu: root; text: "启用规则监测"
        checked: root.page.monitorEnabled
        onToggled: root.page.monitorEnabled = checked
        onKeyboardToggleRequested: root.page.monitorEnabled = !root.page.monitorEnabled
    }
    Next.GlassMenu {
        id: rulesMenu
        onCurrentIndexChanged: { const option = itemAt(currentIndex); if (option instanceof CheckDelegate) option.forceActiveFocus(Qt.TabFocusReason); }
        objectName: "ssh-rule-select"
        title: "选择规则（" + root.page.selectedMonitorRules.length + "）"
        Instantiator {
            model: root.page.monitorRuleLibrary ? root.page.monitorRuleLibrary.monitorRules : []
            delegate: SessionOption {
                required property var modelData
                objectName: "ssh-menu-rule-" + modelData.id
                menu: rulesMenu; text: modelData.name
                checked: root.page.monitorRuleIds.indexOf(modelData.id) >= 0
                onToggled: root.page.toggleMonitorRule(modelData.id, checked)
                onKeyboardToggleRequested: root.page.toggleMonitorRule(modelData.id, !checked)
                ToolTip.visible: hovered
                ToolTip.text: modelData.pattern + (modelData.pauseOnMatch === false ? " · 仅标记" : " · 暂停跟随")
            }
            onObjectAdded: function(index, object) { rulesMenu.insertItem(index, object); }
            onObjectRemoved: function(index, object) { rulesMenu.removeItem(object); }
        }
        Next.GlassMenuSeparator {}
        Next.GlassMenuItem {
            objectName: "ssh-menu-rule-search"; text: "搜索规则…"
            onTriggered: { const page = root.page; root.close(); Qt.callLater(function() { page.openRulePicker(); }); }
        }
        Next.GlassMenuItem {
            objectName: "ssh-menu-rule-manage"; text: "管理规则库…"
            onTriggered: root.workspace.openMonitorRules(root.page.ownerWindow)
        }
    }
    Next.GlassMenuItem {
        objectName: "ssh-menu-match-details"; text: "查看最近命中…"; enabled: !!root.page.monitorEvent
        onTriggered: root.page.showMatchDetails()
    }
    Next.GlassMenuSeparator {}
    Next.GlassMenuItem {
        objectName: "ssh-session-connect-" + (root.page ? root.page.sessionId : 0)
        text: !root.page ? "连接" : root.page.terminal.connected ? "断开" : root.page.sessionActive || root.page.pendingSshAction ? "取消" : "连接"
        enabled: !!root.page && !root.page.passwordOperationId
        onTriggered: root.workspace.connectSession(root.page)
    }
    Next.GlassMenuItem {
        objectName: "ssh-session-config-" + root.page.sessionId
        text: "连接配置"
        onTriggered: {
            root.workspace.activateSession(root.page);
            root.page.connectionConfigExpanded = true;
            if (root.page.detached) root.workspace.locateSession(root.page);
        }
    }
    Next.GlassMenuSeparator {}
    Next.GlassMenuItem { text: "新建同配置标签"; onTriggered: root.workspace.cloneSession(root.page) }
    Next.GlassMenu {
        id: layoutMenu; objectName: "ssh-layout-menu"; title: "布局与窗口"
        Next.GlassMenuItem { text: "移到另一分组"; enabled: !!root.page && (root.page.group.tabs.length > 1 || root.page.group.container.groups.length > 1); onTriggered: root.workspace.splitSession(root.page) }
        Next.GlassMenuItem { objectName: "ssh-session-detach-" + (root.page ? root.page.sessionId : 0); text: "移到新窗口"; onTriggered: root.workspace.detachSession(root.page) }
        Next.GlassMenuItem { objectName: "ssh-session-dock-" + (root.page ? root.page.sessionId : 0); text: "收回主窗口"; visible: !!root.page && root.page.detached; height: visible ? implicitHeight : 0; enabled: visible; onTriggered: root.workspace.dockSession(root.page) }
        Next.GlassMenu {
            id: windowsMenu
            title: "移到已有窗口"; enabled: root.otherWindows.length > 0
            Instantiator {
                model: root.otherWindows
                delegate: Next.GlassMenuItem {
                    required property var modelData
                    objectName: "ssh-window-target-" + modelData.windowId
                    text: "窗口 " + modelData.windowId + (modelData.activeGroup.active ? " · " + modelData.activeGroup.active.sessionName : "")
                    onTriggered: {
                        const page = root.page;
                        root.workspace.moveSession(page, modelData.activeGroup || modelData.groups[0]);
                        root.workspace.locateSession(page);
                    }
                }
                onObjectAdded: function(index, object) { windowsMenu.insertItem(index, object); }
                onObjectRemoved: function(index, object) { windowsMenu.removeItem(object); }
            }
        }
        Next.GlassMenuSeparator {}
        Next.GlassMenuItem {
            objectName: "ssh-session-merge"; text: "合并全部标签"
            enabled: root.page.group.container.groups.length > 1
            onTriggered: root.workspace.mergeGroups(root.page.group.container)
        }
    }
    Next.GlassMenu {
        id: logMenu; objectName: "ssh-log-menu"; title: "日志"
        Next.GlassMenuItem {
            objectName: "ssh-session-log-settings-" + root.page.sessionId
            text: "日志写入设置…"
            onTriggered: root.page.openLogSettings()
        }
        Next.GlassMenuItem {
            objectName: "ssh-session-log-start-" + root.page.sessionId
            text: "开始记录日志…"
            enabled: !root.page.terminal.logging && !root.page.terminal.logBusy
            ToolTip.visible: hovered
            ToolTip.text: "选择文件后记录该会话的新输出，保存为 UTF-8 文本；清屏和暂停跟随不影响记录。"
            onTriggered: root.page.requestLogFile(false)
        }
        Next.GlassMenuItem {
            objectName: "ssh-session-log-stop-" + root.page.sessionId
            text: "停止记录日志"
            enabled: root.page.terminal.logging && !root.page.terminal.logBusy
            onTriggered: root.page.terminal.stopLogging()
        }
        Next.GlassMenuItem {
            objectName: "ssh-session-log-export-" + root.page.sessionId
            text: "导出当前缓冲日志…"
            enabled: !root.page.terminal.logBusy
            ToolTip.visible: hovered
            ToolTip.text: "只导出该会话当前保留的日志及未换行尾部，已淘汰的内容无法恢复。"
            onTriggered: root.page.requestLogFile(true)
        }
    }
    Next.GlassMenuSeparator {}
    Next.GlassMenuItem { text: "重命名"; onTriggered: root.workspace.requestRename(root.page) }
    Next.GlassMenuItem {
        objectName: "ssh-session-remove-" + root.page.sessionId
        text: "关闭标签…"
        onTriggered: root.workspace.requestCloseSession(root.page)
    }

}
