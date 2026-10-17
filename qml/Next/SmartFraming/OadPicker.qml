import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt

Item {
    id: root
    property var theme
    property var backend
    property bool methodMode: false
    property string opFilter: ""
    signal picked(string oi, int id, string name)

    property var oadList: []

    function reload() {
        if (backend)
            oadList = backend.listAllOad(searchField.text)
    }

    Component.onCompleted: reload()

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Next.GlassTextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: "搜索 OI / 名称 / 属性 / 方法"
            color: theme ? theme.text : "#333"
            placeholderTextColor: theme ? theme.placeholderText : "#888"

            onTextChanged: reload()
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.oadList
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: oiDelegate
                width: listView.width
                height: header.height + (expanded ? detail.implicitHeight : 0)
                color: index % 2 === 0 ? (theme ? theme.controlBackground : "#fff") : (theme ? theme.rowAlt : "#f9f9f9")
                property bool expanded: false
                property string delegateOi: modelData.oi

                Rectangle {
                    id: header
                    width: parent.width
                    height: 30
                    color: mouseArea.containsMouse ? (theme ? theme.surfaceHover : "#eee") : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        Label { text: modelData.oi; color: theme ? theme.accent : "#1976D2"; font.family: Next.PlatformFonts.monospaceFamily; font.bold: true; Layout.preferredWidth: 58 }
                        Label { text: modelData.name; color: theme ? theme.text : "#333"; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { text: expanded ? "▲" : "▼"; color: theme ? theme.subText : "#666" }
                    }
                    MouseArea {
                        id: mouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: expanded = !expanded
                    }
                }

                ColumnLayout {
                    id: detail
                    anchors.top: header.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 22
                    visible: expanded
                    spacing: 0

                    Repeater {
                        model: root.methodMode ? (modelData.methods || []) : (modelData.attrs || [])
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            height: 26
                            color: rowMouse.containsMouse ? (theme ? theme.accentMuted : "#1976D2") : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 4
                                anchors.rightMargin: 4
                                Label { text: (root.methodMode ? "方法" : "属性") + modelData.id; color: rowMouse.containsMouse ? "#fff" : (theme ? theme.subText : "#666"); Layout.preferredWidth: 58 }
                                Label { text: modelData.name; color: rowMouse.containsMouse ? "#fff" : (theme ? theme.text : "#333"); elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: modelData.op; color: rowMouse.containsMouse ? "#fff" : (theme ? theme.subText : "#666"); font.pixelSize: 11 }
                            }
                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: root.picked(oiDelegate.delegateOi, modelData.id, modelData.name)
                            }
                        }
                    }
                }
            }
        }
    }
}
