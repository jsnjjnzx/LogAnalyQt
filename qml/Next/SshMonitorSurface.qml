import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt.Next as Next

ColumnLayout {
    id: monitorSurface
    required property var workspace
    required property var container
    spacing: 0
    Flickable {
        id: viewport
        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
        contentWidth: Math.max(width, monitorSurface.container.groups.length > 1 ? 760 : 360)
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        interactive: contentWidth > width
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
        Rectangle {
            width: viewport.contentWidth; height: viewport.height
            color: first.page ? first.page.terminalBg : "#1a1d21"
        }
        SplitView {
            id: split
            width: viewport.contentWidth; height: viewport.height - (viewport.contentWidth > viewport.width ? 12 : 0)
            orientation: Qt.Horizontal
            onResizingChanged: {
                if (!resizing && monitorSurface.container.groups.length > 1 && width > 0) {
                    monitorSurface.container.splitRatio = Math.max(0.1, Math.min(0.9, first.width / width));
                    monitorSurface.workspace.scheduleSave();
                }
            }
            handle: Rectangle {
                id: splitHandle
                objectName: "ssh-split-handle-" + (monitorSurface.container ? monitorSurface.container.windowId : 0)
                implicitWidth: 6
                color: "transparent"
                Rectangle {
                    objectName: "ssh-split-line-" + (monitorSurface.container ? monitorSurface.container.windowId : 0)
                    x: Math.floor((parent.width - width) / 2)
                    width: 1; height: parent.height
                    color: splitHandle.SplitHandle.pressed ? "#80b9ff" : splitHandle.SplitHandle.hovered ? "#536071" : "#343b46"
                }
            }
            Next.SshMonitorGroup {
                id: first
                workspace: monitorSurface.workspace; group: monitorSurface.container.groups[0] || null
                SplitView.minimumWidth: 360
                SplitView.preferredWidth: split.width * monitorSurface.container.splitRatio
                SplitView.fillWidth: monitorSurface.container.groups.length < 2
            }
            Next.SshMonitorGroup {
                workspace: monitorSurface.workspace; group: monitorSurface.container.groups[1] || null
                visible: group !== null
                SplitView.minimumWidth: 360; SplitView.fillWidth: true
            }
        }
    }
}
