import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt.Next as Next

Rectangle {
    id: root
    required property var workspace
    property var group: null
    readonly property var theme: workspace.theme
    readonly property var page: group ? group.active : null
    color: page ? page.terminalBg : theme ? theme.terminalBackground : "#1a1d21"; radius: 0
    readonly property color frameColor: group && group.container.activeGroup === group ? (theme ? theme.monitorFrame : "#465971") : (theme ? theme.monitorFrameInactive : "#343b46")
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: root.frameColor }
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: root.frameColor }
    Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 1; color: root.frameColor; visible: !root.group || root.group.container.groups.indexOf(root.group) === 0 }
    Rectangle { anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 1; color: root.frameColor; visible: !root.group || root.group.container.groups.indexOf(root.group) === root.group.container.groups.length - 1 }
    objectName: "ssh-group-" + (group ? group.groupId : 0)
    Binding { target: root.group; property: "contentHost"; value: content; when: !!root.group }
    Binding { target: root.group; property: "menu"; value: groupMenu.item; when: !!root.group }
    Binding { target: root.group; property: "visualHost"; value: root; when: !!root.group }
    Binding { target: root.group; property: "tabHost"; value: tabs; when: !!root.group }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 1; spacing: 0
        RowLayout {
            Layout.fillWidth: true; spacing: 0
            ListView {
                id: tabs
                objectName: "ssh-tab-bar-" + (root.group ? root.group.groupId : 0)
                Layout.fillWidth: true; Layout.preferredHeight: 26
                orientation: ListView.Horizontal; clip: true; spacing: 1
                model: root.group ? root.group.tabs : []
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                Rectangle {
                    parent: tabs
                    readonly property var drop: root.workspace.dragTarget
                    visible: drop !== null && drop.group === root.group && drop.inHeader && !root.workspace.dragCancelled
                    x: visible ? Math.max(0, Math.min(tabs.width - width, drop.edgeX - tabs.contentX - width / 2)) : 0
                    width: 2; height: tabs.height; z: 2
                    color: root.theme ? root.theme.accent : "#80b9ff"
                }
                function revealActiveTab() {
                    if (!root.group) return;
                    currentIndex = root.group.tabs.indexOf(root.group.active);
                    if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain);
                }
                onModelChanged: Qt.callLater(revealActiveTab)
                Connections {
                    target: root.group
                    function onActiveChanged() { tabs.revealActiveTab(); }
                }
                delegate: Rectangle {
                    id: tab
                    required property var modelData
                    required property int index
                    objectName: "ssh-session-row-" + modelData.sessionId
                    width: Math.max(110, Math.min(196, label.implicitWidth + 38)); height: 26
                    color: root.group && root.group.active === modelData ? (root.theme ? root.theme.monitorTabSelected : "#2b3c53") : tabHover.hovered ? (root.theme ? root.theme.monitorTabHover : "#29313c") : (root.theme ? root.theme.monitorTab : "#20262f")
                    Accessible.role: Accessible.PageTab
                    Accessible.name: modelData.sessionName
                    Accessible.description: "拖动可排序；双击新建同配置标签并连接；右键打开更多操作"
                    Accessible.onPressAction: root.workspace.activateSession(modelData)
                    activeFocusOnTab: true
                    function connectFromKeyboard(event) {
                        root.workspace.activateSession(modelData);
                        if (!event.isAutoRepeat) modelData.connectIfIdle();
                        modelData.focusTerminal();
                    }
                    Keys.onReturnPressed: function(event) { connectFromKeyboard(event); }
                    Keys.onEnterPressed: function(event) { connectFromKeyboard(event); }
                    Keys.onMenuPressed: tab.openMenu()
                    function openMenu() { root.workspace.openSessionMenu(modelData); }
                    Label {
                        id: label
                        anchors.left: parent.left; anchors.leftMargin: 10
                        anchors.right: close.left; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
                        text: tab.modelData.sessionName; textFormat: Text.PlainText; elide: Text.ElideRight
                        color: root.workspace.theme ? root.workspace.theme.text : "white"; font.pixelSize: 12
                    }
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: root.theme ? root.theme.accent : "#80b9ff"; visible: root.group && root.group.active === tab.modelData }
                    Item {
                        id: tabPointer
                        objectName: "ssh-tab-pointer-" + tab.modelData.sessionId
                        anchors.left: parent.left; anchors.right: close.left
                        anchors.top: parent.top; anchors.bottom: parent.bottom
                        HoverHandler { id: tabHover }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: {
                                root.workspace.activateSession(tab.modelData);
                                tab.modelData.focusTerminal();
                            }
                            onDoubleTapped: root.workspace.cloneSession(tab.modelData)
                        }
                        TapHandler { acceptedButtons: Qt.RightButton; onTapped: tab.openMenu() }
                        DragHandler {
                            id: tabDrag
                            target: null
                            acceptedButtons: Qt.LeftButton
                            cursorShape: Qt.ClosedHandCursor
                            function preview() {
                                if (!active || root.workspace.dragCancelled) return;
                                const point = tabPointer.mapToGlobal(centroid.position.x, centroid.position.y);
                                const local = root.mapFromGlobal(point.x, point.y);
                                dragProxy.x = Math.max(0, Math.min(root.width - dragProxy.width, local.x + 8));
                                dragProxy.y = Math.max(0, Math.min(root.height - dragProxy.height, local.y + 9));
                                root.workspace.dragTarget = root.workspace.tabDropTarget(point);
                            }
                            onActiveChanged: {
                                if (!active) return;
                                root.workspace.dragCancelled = false;
                                root.workspace.draggedSession = tab.modelData;
                                preview();
                            }
                            onCentroidChanged: preview()
                            onGrabChanged: function(transition, eventPoint) {
                                if (root.workspace.draggedSession !== tab.modelData) return;
                                if (transition === PointerDevice.UngrabExclusive) {
                                    const page = tab.modelData;
                                    const point = eventPoint.globalPosition;
                                    Qt.callLater(function() { root.workspace.finishTabDrag(page, point); });
                                } else if (transition === PointerDevice.CancelGrabExclusive) {
                                    root.workspace.draggedSession = null;
                                    root.workspace.dragTarget = null;
                                }
                            }
                        }
                        ToolTip.visible: tabHover.hovered && !root.workspace.draggedSession
                        ToolTip.text: tab.modelData.sessionName + "\n" + tab.modelData.connectionConfiguration().username + "@" + tab.modelData.connectionConfiguration().host + "\n拖动可排序"
                    }
                    Next.GlassButton {
                        id: close
                        objectName: "ssh-tab-close-" + tab.modelData.sessionId
                        anchors.right: parent.right; anchors.rightMargin: 3; anchors.verticalCenter: parent.verticalCenter
                        text: "×"; implicitWidth: 20; implicitHeight: 20; padding: 0; horizontalPadding: 0
                        font.pixelSize: 12
                        background: Rectangle { radius: 3; color: close.hovered || close.visualFocus ? (root.theme && (root.theme.lightMode || root.theme.monokaiMode) ? root.theme.buttonHover : "#394451") : "transparent" }
                        Accessible.name: "关闭 " + tab.modelData.sessionName
                        onClicked: root.workspace.requestCloseSession(tab.modelData)
                    }
                    Rectangle {
                        id: dragProxy
                        objectName: "ssh-tab-drag-preview-" + tab.modelData.sessionId
                        parent: root
                        visible: root.workspace.draggedSession === tab.modelData && !root.workspace.dragCancelled
                        width: tab.width; height: 26; radius: 5; color: root.theme && (root.theme.lightMode || root.theme.monokaiMode) ? root.theme.navigationSelected : "#34465e"; opacity: 0.9; z: 1000
                        Label { anchors.centerIn: parent; text: tab.modelData.sessionName; color: root.theme ? root.theme.text : "#eceef2"; font.pixelSize: 12 }
                    }
                }

            }
            Next.GlassButton {
                objectName: "ssh-group-add-" + (root.group ? root.group.groupId : 0)
                text: "+"; implicitWidth: 26; implicitHeight: 26; padding: 0; horizontalPadding: 0
                Accessible.name: "在当前组新建连接"
                onClicked: root.workspace.addSession(undefined, undefined, root.group)
            }
        }
        Item {
            id: content
            Layout.fillWidth: true; Layout.fillHeight: true
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: if (root.page) root.workspace.activateSession(root.page)
            }
            Label {
                anchors.centerIn: parent; visible: !root.page
                text: "双击左侧 SSH 连接开始监控"; color: root.workspace.theme ? root.workspace.theme.subText : "#a7adb7"; font.pixelSize: 12
            }
            Rectangle {
                objectName: "ssh-drop-preview-" + (root.group ? root.group.groupId : 0)
                z: 10
                visible: root.workspace.dragTarget !== null && !root.workspace.dragTarget.inHeader && root.workspace.dragTarget.group === root.group && !root.workspace.dragCancelled
                anchors.top: parent.top
                height: root.page ? Math.min(parent.height, root.page.monitorPreviewBottom) : parent.height
                x: root.workspace.dragTarget && root.workspace.dragTarget.side === "left" ? 0 : parent.width - width
                width: root.workspace.dragTarget && root.workspace.dragTarget.split ? parent.width / 2 : parent.width
                color: root.theme && root.theme.lightMode ? "#303478c5" : root.theme && root.theme.monokaiMode ? "#3066d9ef" : "#304c80b9"; border.color: root.theme ? root.theme.accent : "#80b9ff"; border.width: 1
                Label {
                    anchors.centerIn: parent
                    visible: root.workspace.dragTarget !== null && root.workspace.dragTarget.split
                    text: root.workspace.dragTarget && root.workspace.dragTarget.side === "left" ? "松开后在左侧分组" : "松开后在右侧分组"
                    color: root.theme ? root.theme.text : "#eceef2"; font.pixelSize: 12
                }
            }
        }
    }
    Loader {
        id: groupMenu
        active: root.page !== null
        sourceComponent: Next.SshSessionMenu { workspace: root.workspace; page: root.page }
    }
}
