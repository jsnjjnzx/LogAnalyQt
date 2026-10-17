import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform
import LogAnalyQt

GlassPage {
    id: mergeDialog
    visible: false
    title: "日志合并"


    LogMerger {
        id: logMerger
        onMergeFinished: function (success, message) {
            statusText = message;
            statusIsError = !success;
        }
        onStatusMessage: function (message, timeout) {
            statusText = message;
            statusIsError = false;
        }
    }

    property string statusText: ""
    property bool statusIsError: false

    color: theme ? theme.background : "#ffffff"
    palette.highlight: theme ? theme.accentMuted : "#1976D2"




    // 窗口边框


    // 自定义标题栏
    Item { id: titleBar; height: 0; width: parent.width }

    // 边缘缩放 MouseAreas

    FileDialog {
        id: fileDialog
        title: "选择要合并的日志文件"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["日志文件 (*.txt *.log)", "所有文件 (*.*)"]
        onAccepted: {
            // Qt.labs.platform.FileDialog returns a list of URLs
            logMerger.addFiles(currentFiles);
        }
    }

    FileDialog {
        id: saveDialog
        title: "选择合并后的输出位置"
        fileMode: FileDialog.SaveFile
        nameFilters: ["文本文件 (*.txt)", "所有文件 (*.*)"]
        currentFile: "merged_log.txt"
        onAccepted: {
            var path = currentFile.toString();
            logMerger.outputPath = path;
        }
    }

    component StyledButton: Button {
        id: control
        contentItem: Text {
            text: control.text
            font: control.font
            opacity: enabled ? 1.0 : 0.3
            color: theme ? theme.buttonText : "#333333"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            implicitWidth: 100
            implicitHeight: 36
            opacity: enabled ? 1 : 0.3
            color: control.down ? (theme ? theme.buttonPressed : "#cccccc") : (control.hovered ? (theme ? theme.buttonHover : "#dddddd") : (theme ? theme.buttonBackground : "#eeeeee"))
            border.color: theme ? theme.controlBorder : "#cccccc"
            border.width: 1
            radius: 8
        }
    }

    ColumnLayout {
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        spacing: 12

        Text {
            text: "可添加多个带时间戳的日志文件，系统将按时间戳排序后合并为一个文件"
            color: theme ? theme.subText : "#666"
            font.pixelSize: 13
            Layout.fillWidth: true
        }

        Text {
            text: "说明：最终顺序由时间戳决定，与添加顺序无关"
            color: theme ? theme.placeholderText : "#999"
            font.pixelSize: 12
            Layout.fillWidth: true
        }

        // 文件列表区域
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: theme ? theme.controlBackground : "#ffffff"
            border.color: theme ? theme.controlBorder : "#cccccc"
            radius: 8

            ListView {
                id: fileListView
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: logMerger.fileList

                delegate: Rectangle {
                    width: fileListView.width
                    height: 36
                    color: index % 2 === 0 ? "transparent" : (theme ? theme.rowAlt : "#f5f5f5")

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 8

                        Text {
                            text: (index + 1) + ". " + modelData
                            color: theme ? theme.text : "#333"
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }

                        StyledButton {
                            text: "✕"
                            width: 30
                            height: 24
                            enabled: !logMerger.merging
                            onClicked: logMerger.removeFile(index)
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar {}
            }

            Text {
                text: "点击 [添加文件] 可添加多个带时间戳的日志文件"
                color: theme ? theme.placeholderText : "#999"
                anchors.centerIn: parent
                visible: fileListView.count === 0
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            StyledButton {
                text: "添加文件"
                enabled: !logMerger.merging
                onClicked: fileDialog.open()
            }

            StyledButton {
                text: "清空列表"
                enabled: fileListView.count > 0 && !logMerger.merging
                onClicked: logMerger.clearFiles()
            }
        }

        // 输出路径区域
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: "输出:"
                color: theme ? theme.text : "#333"
                Layout.alignment: Qt.AlignVCenter
            }

            TextField {
                id: pathField
                Layout.fillWidth: true
                readOnly: true
                text: {
                    var outPath = logMerger.outputPath;
                    if (outPath.startsWith("file:///")) {
                        // 在 Windows 上，通常需要去掉开头多余的斜杠
                        var cleanPath = outPath.substring(8);
                        return cleanPath;
                    }
                    return outPath;
                }
                color: theme ? theme.text : "#333333"
                background: Rectangle {
                    color: theme ? theme.controlBackground : "#ffffff"
                    border.color: theme ? theme.controlBorder : "#cccccc"
                    radius: 8
                }
            }

            StyledButton {
                text: "浏览..."
                enabled: !logMerger.merging
                onClicked: saveDialog.open()
            }
        }

        // 状态与进度条
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            ProgressBar {
                id: progressBar
                Layout.fillWidth: true
                visible: logMerger.merging || logMerger.progress > 0 || statusText !== ""
                from: 0
                to: 1.0
                value: logMerger.progress

                background: Rectangle {
                    implicitWidth: 200
                    implicitHeight: 8
                    radius: 8
                    color: theme ? theme.controlBackground : "#e0e0e0"
                    border.color: theme ? theme.controlBorder : "#cccccc"
                    border.width: 1
                }

                contentItem: Item {
                    implicitWidth: 200
                    implicitHeight: 8
                    Rectangle {
                        width: progressBar.visualPosition * parent.width
                        height: parent.height
                        radius: 8
                        color: theme ? theme.success : "#4caf50"
                        Behavior on width {
                            NumberAnimation {
                                duration: 200
                            }
                        }
                    }
                }
            }

            Text {
                text: statusText
                color: statusIsError ? (theme ? theme.error : "#cc0000") : (theme ? theme.subText : "#666")
                visible: statusText.length > 0
                Layout.fillWidth: true
            }
        }

        // 底部按钮
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 12

            StyledButton {
                text: logMerger.merging ? "取消中..." : "关闭"
                enabled: !logMerger.merging
                onClicked: mergeDialog.close()
            }

            StyledButton {
                text: "合并"
                enabled: fileListView.count > 0 && logMerger.outputPath !== "" && !logMerger.merging
                onClicked: {
                    logMerger.mergeFiles();
                }
            }
        }
    }
}
