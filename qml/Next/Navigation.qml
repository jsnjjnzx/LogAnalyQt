import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: nav
    required property var theme
    property int currentPage: 0
    property string version: ""
    property var targetWindow: null
    readonly property bool nativeWindowControls: !!(targetWindow && targetWindow.nativeTitleBar)
    signal navigate(int page)
    color: theme.sidebar
    readonly property var items: [
        {section:"分析工作台"},
        {page:0,title:"日志解析",icon:0xF09EE},
        {page:1,title:"手动解析",icon:0xF0CB6},
        {page:2,title:"差异对比",icon:0xF1492},
        {page:3,title:"多窗口解析",icon:0xF0BCC},
        {page:4,title:"智能组帧",icon:0xF09FE},
        {page:14,title:"AI 助手",icon:0xF167A},
        {section:"日志工具"},
        {page:5,title:"实时监控",icon:0xF0430},
        {page:6,title:"日志合并",icon:0xF0219},
        {section:"通信调试"},
        {page:15,title:"MQTT 调试",icon:0xF0318},
        {section:"资源管理"},
        {page:7,title:"预置报文",icon:0xF09EE},
        {page:8,title:"协议同步",icon:0xF04E6}
    ]
    FontLoader { id: mdi; source: "qrc:/qt/qml/LogAnalyQt/assets/materialdesignicons-webfont.ttf" }
    Rectangle { width: 1; height: parent.height; anchors.right: parent.right; color: nav.theme.groupBorder }
    MouseArea {
        objectName: "nav-window-drag"
        z: 1
        width: parent.width; height: nav.nativeWindowControls ? 46 : 18
        onPressed: if (nav.targetWindow) nav.targetWindow.startSystemMove()
        onDoubleClicked: if (nav.nativeWindowControls && nav.targetWindow.visibility !== Window.FullScreen) {
            if (nav.targetWindow.visibility === Window.Maximized) nav.targetWindow.showNormal();
            else nav.targetWindow.showMaximized();
        }
    }
    Flickable {
        objectName: "nav-menu"
        x: 14; y: 22; width: parent.width - 28
        height: Math.max(0, footer.y - y - 14)
        clip: true; contentHeight: menu.height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        Column {
            id: menu; width: parent.width; spacing: 5
            Repeater {
                model: nav.items
                delegate: Item {
                    required property var modelData
                    required property int index
                    width: menu.width
                    height: modelData.section ? (index === 0 ? 24 : (nav.height < 800 ? 36 : 43)) : (nav.height < 800 ? 38 : 43)
                    Text { objectName: "nav-section-" + index; visible: !!modelData.section && !(index === 0 && nav.nativeWindowControls); x: 8; anchors.bottom: parent.bottom; anchors.bottomMargin: 8; text: modelData.section || ""; font.pixelSize: 12; font.weight: Font.DemiBold; color: nav.theme.subText }
                    Next.GlassButton {
                        objectName: modelData.section ? "" : "nav-" + modelData.page
                        anchors.fill: parent; visible: !modelData.section
                        hoverEnabled: true
                        Accessible.name: modelData.title || ""
                        onClicked: nav.navigate(modelData.page)
                        background: Rectangle {
                            radius: 10
                            color: nav.currentPage === modelData.page ? nav.theme.navigationSelected : parent.hovered ? nav.theme.surfaceHover : "transparent"
                            border.width: parent.visualFocus ? 1 : 0
                            border.color: parent.activeFocus ? nav.theme.accent : nav.theme.groupBorder
                        }
                        contentItem: RowLayout {
                            spacing: 15
                            Text { text: String.fromCodePoint(modelData.icon || 0); font.family: mdi.name; font.pixelSize: 20; color: nav.currentPage === modelData.page ? nav.theme.accent : nav.theme.subText; Layout.leftMargin: 7 }
                            Text { text: modelData.title || ""; font.pixelSize: 14; color: nav.theme.text; Layout.fillWidth: true }
                        }
                    }
                }
            }
        }
    }
    Column {
        id: footer
        objectName: "nav-footer"
        anchors.bottom: parent.bottom; anchors.bottomMargin: 16
        x: 18; width: parent.width - 36; spacing: 8
        Rectangle { width: parent.width; height: 1; color: nav.theme.groupBorder }
        Repeater {
            model: [{page:9,title:"系统设置",icon:0xF08BB},{page:10,title:"帮助与更新",icon:0xF0625}]
            delegate: Next.GlassButton {
                required property var modelData
                objectName: "nav-" + modelData.page
                width: parent.width; height: 39
                Accessible.name: modelData.title
                onClicked: nav.navigate(modelData.page)
                background: Rectangle { radius: 9; color: nav.currentPage === modelData.page ? nav.theme.navigationSelected : parent.hovered ? nav.theme.surfaceHover : "transparent"; border.color: parent.visualFocus ? nav.theme.accent : "transparent" }
                contentItem: RowLayout {
                    spacing: 15
                    Text { text: String.fromCodePoint(modelData.icon); font.family: mdi.name; font.pixelSize: 20; color: nav.theme.subText }
                    Text { text: modelData.title; color: nav.theme.text; font.pixelSize: 14; Layout.fillWidth: true }
                }
            }
        }
        Rectangle { width: parent.width; height: 1; color: nav.theme.separator }
        RowLayout {
            objectName: "nav-brand"
            width: parent.width; height: 44; spacing: 10
            ColumnLayout {
                Layout.fillWidth: true; spacing: 3
                Text { text: "通联工坊"; font.pixelSize: 13; font.weight: Font.DemiBold; color: nav.theme.text }
                RowLayout {
                    Layout.fillWidth: true; spacing: 4
                    Item { Layout.fillWidth: true }
                    Text { text: "v" + nav.version; font.pixelSize: 10; color: nav.theme.subText }
                }
            }
        }
    }
}
