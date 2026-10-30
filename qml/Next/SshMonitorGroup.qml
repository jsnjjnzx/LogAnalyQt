import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt.Next as Next

Rectangle {
    id: root
    required property var workspace
    property var group: null
    readonly property var page: group ? group.active : null
    color: page ? page.terminalBg : "#1a1d21"; radius: 0
    readonly property color frameColor: group && group.container.activeGroup === group ? "#465971" : "#343b46"
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: root.frameColor }
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: root.frameColor }
    Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 1; color: root.frameColor; visible: !root.group || root.group.container.groups.indexOf(root.group) === 0 }
    Rectangle { anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 1; color: root.frameColor; visible: !root.group || root.group.container.groups.indexOf(root.group) === root.group.container.groups.length - 1 }
    objectName: "ssh-group-" + (group ? group.groupId : 0)
    Binding { target: root.group; property: "contentHost"; value: content; when: !!root.group }
    Binding { target: root.group; property: "menu"; value: groupMenu.item; when: !!root.group }
    Binding { target: root.group; property: "visualHost"; value: root; when: !!root.group }
    Binding { target: root.group; property: "tabHost"; value: tabs; when: !!root.group }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.WithinBounds
        onTapped: if (root.page) root.workspace.activateSession(root.page)
    }
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
                onCountChanged: Qt.callLater(function() { if (root.group) { currentIndex = root.group.tabs.indexOf(root.group.active); positionViewAtIndex(currentIndex, ListView.Contain); } })
                Connections {
                    target: root.group
                    function onActiveChanged() { tabs.currentIndex = root.group.tabs.indexOf(root.group.active); tabs.positionViewAtIndex(tabs.currentIndex, ListView.Contain); }
                }
                delegate: Rectangle {
                    id: tab
                    required property var modelData
                    required property int index
                    objectName: "ssh-session-row-" + modelData.sessionId
                    width: Math.max(110, Math.min(196, label.implicitWidth + 38)); height: 26
                    color: root.group && root.group.active === modelData ? "#2b3c53" : tabMouse.containsMouse ? "#29313c" : "#20262f"
                    Accessible.role: Accessible.PageTab
                    Accessible.name: modelData.sessionName
                    Accessible.description: "双击新建同配置标签并连接；右键更多操作"
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
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: "#80b9ff"; visible: root.group && root.group.active === tab.modelData }
                    MouseArea {
                        id: tabMouse
                        objectName: "ssh-tab-pointer-" + tab.modelData.sessionId
                        anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton; hoverEnabled: true
                        preventStealing: true
                        drag.target: dragProxy
                        drag.minimumX: 0; drag.maximumX: Math.max(0, root.width - dragProxy.width)
                        drag.minimumY: 0; drag.maximumY: Math.max(0, root.height - dragProxy.height)
                        onPressed: function(mouse) {
                            root.workspace.activateSession(tab.modelData);
                            const point = mapToItem(dragProxy.parent, mouse.x, mouse.y);
                            dragProxy.x = point.x; dragProxy.y = point.y; root.workspace.dragCancelled = false;
                        }
                        onPositionChanged: function(mouse) {
                            if (drag.active) {
                                root.workspace.draggedSession = tab.modelData;
                                root.workspace.dragTarget = root.workspace.tabDropTarget(mapToGlobal(mouse.x, mouse.y));
                            }
                        }
                        onReleased: function(mouse) {
                            if (root.workspace.draggedSession === tab.modelData) {
                                const page = tab.modelData, point = mapToGlobal(mouse.x, mouse.y);
                                Qt.callLater(function() { root.workspace.finishTabDrag(page, point); });
                            }
                        }
                        onCanceled: { root.workspace.draggedSession = null; root.workspace.dragTarget = null; }
                        onClicked: function(mouse) {
                            if (mouse.button === Qt.RightButton) tab.openMenu();
                            else tab.modelData.focusTerminal();
                        }
                        onDoubleClicked: function(mouse) { if (mouse.button === Qt.LeftButton) root.workspace.cloneSession(tab.modelData); }
                        ToolTip.visible: containsMouse && !drag.active
                        ToolTip.text: tab.modelData.sessionName + "\n" + tab.modelData.connectionConfiguration().username + "@" + tab.modelData.connectionConfiguration().host
                    }
                    Next.GlassButton {
                        id: close
                        objectName: "ssh-tab-close-" + tab.modelData.sessionId
                        anchors.right: parent.right; anchors.rightMargin: 3; anchors.verticalCenter: parent.verticalCenter
                        text: "×"; implicitWidth: 20; implicitHeight: 20; padding: 0; horizontalPadding: 0
                        font.pixelSize: 12
                        background: Rectangle { radius: 3; color: close.hovered || close.visualFocus ? "#394451" : "transparent" }
                        Accessible.name: "关闭 " + tab.modelData.sessionName
                        onClicked: root.workspace.requestCloseSession(tab.modelData)
                    }
                    Rectangle {
                        id: dragProxy
                        objectName: "ssh-tab-drag-preview-" + tab.modelData.sessionId
                        parent: root
                        visible: tabMouse.drag.active
                        width: tab.width; height: 26; radius: 5; color: "#34465e"; opacity: 0.9; z: 1000
                        Label { anchors.centerIn: parent; text: tab.modelData.sessionName; color: "#eceef2"; font.pixelSize: 12 }
                    }
                    Rectangle {
                        visible: root.workspace.dragTarget !== null && root.workspace.dragTarget.group === root.group && root.workspace.dragTarget.index === tab.index
                        anchors.left: parent.left; width: 2; height: parent.height; color: "#80b9ff"
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
            Label {
                anchors.centerIn: parent; visible: !root.page
                text: "双击左侧 SSH 连接开始监控"; color: root.workspace.theme ? root.workspace.theme.subText : "#a7adb7"; font.pixelSize: 12
            }
            Rectangle {
                objectName: "ssh-drop-preview-" + (root.group ? root.group.groupId : 0)
                z: 10
                visible: root.workspace.dragTarget !== null && !root.workspace.dragTarget.inHeader && root.workspace.dragTarget.group === root.group
                anchors.top: parent.top
                height: root.page ? Math.min(parent.height, root.page.monitorPreviewBottom) : parent.height
                x: root.workspace.dragTarget && root.workspace.dragTarget.side === "left" ? 0 : parent.width - width
                width: root.workspace.dragTarget && root.workspace.dragTarget.split ? parent.width / 2 : parent.width
                color: "#304c80b9"; border.color: "#80b9ff"; border.width: 1
                Label {
                    anchors.centerIn: parent
                    visible: root.workspace.dragTarget !== null && root.workspace.dragTarget.split
                    text: root.workspace.dragTarget && root.workspace.dragTarget.side === "left" ? "松开后在左侧分组" : "松开后在右侧分组"
                    color: "#eceef2"; font.pixelSize: 12
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
