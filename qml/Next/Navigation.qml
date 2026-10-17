import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: nav
    required property var theme
    property int currentPage: 0
    property string version: ""
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
        {section:"资源管理"},
        {page:7,title:"预置报文",icon:0xF09EE},
        {page:8,title:"协议同步",icon:0xF04E6}
    ]
    FontLoader { id: mdi; source: "qrc:/qt/qml/LogAnalyQt/assets/materialdesignicons-webfont.ttf" }
    Rectangle { width: 1; height: parent.height; anchors.right: parent.right; color: nav.theme.groupBorder }
    RowLayout {
        x: 20; y: 24; width: parent.width - 36; spacing: 12
        Image { source: "assets/app-logo.png"; Layout.preferredWidth: 44; Layout.preferredHeight: 44; smooth: true }
        Column {
            spacing: 4
            Text { text: "多协议解析器"; font.pixelSize: 18; font.bold: true; color: nav.theme.text }
            Text { text: "LogAnalyQt"; font.pixelSize: 13; color: nav.theme.subText }
        }
    }
    Flickable {
        x: 14; y: 105; width: parent.width - 28; height: parent.height - 242
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
                    Text { visible: !!modelData.section; x: 8; anchors.bottom: parent.bottom; anchors.bottomMargin: 8; text: modelData.section || ""; font.pixelSize: 13; color: nav.theme.subText }
                    Next.GlassButton {
                        objectName: modelData.section ? "" : "nav-" + modelData.page
                        anchors.fill: parent; visible: !modelData.section
                        hoverEnabled: true
                        Accessible.name: modelData.title || ""
                        onClicked: nav.navigate(modelData.page)
                        background: Rectangle {
                            radius: 9
                            color: nav.currentPage === modelData.page ? nav.theme.navigationSelected : parent.hovered ? nav.theme.surfaceHover : "transparent"
                            border.width: nav.currentPage === modelData.page || parent.activeFocus ? 1 : 0
                            border.color: parent.activeFocus ? nav.theme.accent : nav.theme.groupBorder
                        }
                        contentItem: RowLayout {
                            spacing: 15
                            Text { text: String.fromCodePoint(modelData.icon || 0); font.family: mdi.name; font.pixelSize: 22; color: nav.currentPage === modelData.page ? nav.theme.accent : nav.theme.subText; Layout.leftMargin: 7 }
                            Text { text: modelData.title || ""; font.pixelSize: 15; color: nav.theme.text; Layout.fillWidth: true }
                        }
                    }
                }
            }
        }
    }
    Column {
        anchors.bottom: parent.bottom; anchors.bottomMargin: 14
        x: 20; width: parent.width - 40; spacing: 6
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
                    Text { text: String.fromCodePoint(modelData.icon); font.family: mdi.name; font.pixelSize: 23; color: nav.theme.subText }
                    Text { text: modelData.title; color: nav.theme.text; font.pixelSize: 15; Layout.fillWidth: true }
                }
            }
        }
        Text { anchors.right: parent.right; text: "v" + nav.version; color: nav.theme.subText; font.pixelSize: 12 }
    }
}
