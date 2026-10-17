import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * @brief 自定义标题栏组件
 * 用于无边框窗口的标题栏，支持拖拽移动、最大化/最小化/关闭等功能
 */
Rectangle {
    id: titleBar
    height: 42
    color: titleBarColor

    // 外部属性
    property Window targetWindow: null            // 目标窗口
    property string title: ""                     // 标题文本
    property color titleBarColor: "#ffffff"       // 标题栏背景色（兜底）
    property color titleTextColor: "#333333"      // 标题文本颜色（兜底）
    property color buttonHoverColor: "#e0e0e0"    // 按钮悬停颜色（兜底）
    property color closeButtonHoverColor: "#f44336" // 关闭按钮悬停颜色（兜底）
    property color closeButtonIconHoverColor: "#ffffff" // 关闭按钮悬停时图标色
    property bool isDarkTheme: true               // 是否深色主题
    property bool showThemeToggle: true           // 是否显示主题切换按钮

    // 内部状态
    property bool isMaximized: targetWindow ? (targetWindow.visibility === Window.Maximized) : false

    // 信号
    signal themeToggleClicked()

    // 窗口拖拽区域
    MouseArea {
        id: dragArea
        anchors.fill: parent
        anchors.rightMargin: buttonRow.width
        property point clickPos: Qt.point(0, 0)
        property bool isDragging: false

        onPressed: function(mouse) {
            if (targetWindow && !isMaximized) {
                clickPos = Qt.point(mouse.x, mouse.y)
                isDragging = true
            }
        }

        onPositionChanged: function(mouse) {
            if (isDragging && targetWindow && !isMaximized) {
                var delta = Qt.point(mouse.x - clickPos.x, mouse.y - clickPos.y)
                targetWindow.x += delta.x
                targetWindow.y += delta.y
            }
        }

        onReleased: {
            isDragging = false
        }

        // 双击最大化/还原
        onDoubleClicked: {
            if (targetWindow) {
                if (isMaximized) {
                    targetWindow.showNormal()
                } else {
                    targetWindow.showMaximized()
                }
            }
        }
    }

    // 标题栏内容布局
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 0
        spacing: 12

        // 应用图标
        Image {
            source: "../assets/logo.ico"
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            visible: status === Image.Ready
        }

        // 标题文本
        Label {
            Layout.fillWidth: true
            text: titleBar.title
            color: titleTextColor
            font.pixelSize: 14
            font.bold: true
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        // 按钮区域
        Row {
            id: buttonRow
            Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
            spacing: 0

            // 通用按钮组件
            component TitleButton: Rectangle {
                id: btnRoot
                width: 46
                height: titleBar.height
                color: "transparent"

                property alias hoverColor: btnMouseArea.hoverColor
                property alias tooltipText: btnToolTip.text
                property bool isHovered: btnMouseArea.containsMouse

                signal clicked()

                default property alias content: container.data

                Item {
                    id: container
                    anchors.fill: parent
                }

                MouseArea {
                    id: btnMouseArea
                    anchors.fill: parent
                    hoverEnabled: true

                    property color hoverColor: titleBar.buttonHoverColor

                    onEntered: parent.color = hoverColor
                    onExited: parent.color = "transparent"
                    onClicked: btnRoot.clicked()
                }

                ToolTip {
                    id: btnToolTip
                    visible: btnRoot.isHovered
                    delay: 500
                }
            }

            // 主题切换按钮
            TitleButton {
                visible: showThemeToggle
                tooltipText: isDarkTheme ? "切换到浅色主题" : "切换到深色主题"

                Canvas {
                    id: themeCanvas
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    contextType: "2d"

                    property color currentColor: titleTextColor
                    property bool currentIsDark: isDarkTheme

                    onCurrentColorChanged: requestPaint()
                    onCurrentIsDarkChanged: requestPaint()

                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.reset();
                        ctx.strokeStyle = currentColor;
                        ctx.fillStyle = currentColor;
                        ctx.lineWidth = 1.5;

                        var w = width;
                        var h = height;
                        var cx = w / 2;
                        var cy = h / 2;

                        if (currentIsDark) {
                            // 太阳图标
                            ctx.beginPath();
                            ctx.arc(cx, cy, 4, 0, Math.PI * 2);
                            ctx.stroke();

                            // 光芒
                            for (var i = 0; i < 8; i++) {
                                ctx.save();
                                ctx.translate(cx, cy);
                                ctx.rotate(i * Math.PI / 4);
                                ctx.beginPath();
                                ctx.moveTo(0, -6);
                                ctx.lineTo(0, -8);
                                ctx.stroke();
                                ctx.restore();
                            }
                        } else {
                            // 月亮图标 - 使用剪裁方式绘制更标准的月牙
                            ctx.save();
                            // 1. 画底圆 (满月)
                            ctx.beginPath();
                            ctx.arc(cx, cy, 6, 0, Math.PI * 2);
                            ctx.fill();

                            // 2. 剪裁掉左上部分
                            ctx.globalCompositeOperation = 'destination-out';
                            ctx.beginPath();
                            ctx.arc(cx - 3, cy - 2, 5, 0, Math.PI * 2);
                            ctx.fill();
                            ctx.restore();
                        }
                    }
                }

                onClicked: themeToggleClicked()
            }

            // 最小化按钮
            TitleButton {
                tooltipText: "最小化"

                Canvas {
                    id: minCanvas
                    anchors.centerIn: parent
                    width: 10
                    height: 10
                    contextType: "2d"

                    property color currentColor: titleTextColor
                    onCurrentColorChanged: requestPaint()

                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.reset();
                        ctx.strokeStyle = currentColor;
                        ctx.lineWidth = 1;

                        ctx.beginPath();
                        ctx.moveTo(0, height / 2);
                        ctx.lineTo(width, height / 2);
                        ctx.stroke();
                    }
                }

                onClicked: {
                    if (targetWindow) targetWindow.showMinimized()
                }
            }

            // 最大化/还原按钮
            TitleButton {
                tooltipText: isMaximized ? "向下还原" : "最大化"

                Canvas {
                    id: maxCanvas
                    anchors.centerIn: parent
                    width: 10
                    height: 10
                    contextType: "2d"

                    property color currentColor: titleTextColor
                    property color currentBarColor: titleBarColor
                    property bool currentIsMaximized: isMaximized

                    onCurrentColorChanged: requestPaint()
                    onCurrentBarColorChanged: requestPaint()
                    onCurrentIsMaximizedChanged: requestPaint()

                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.reset();
                        ctx.strokeStyle = currentColor;
                        ctx.lineWidth = 1;

                        if (currentIsMaximized) {
                            // 还原图标 (两个重叠矩形)
                            ctx.strokeRect(2, 0, 8, 8); // 后面的矩形
                            ctx.fillStyle = currentBarColor;
                            ctx.fillRect(0, 2, 8, 8); // 遮挡部分
                            ctx.strokeRect(0, 2, 8, 8); // 前面的矩形
                        } else {
                            // 最大化图标 (一个矩形)
                            ctx.strokeRect(0, 0, 10, 10);
                        }
                    }
                }

                onClicked: {
                    if (targetWindow) {
                        if (isMaximized) targetWindow.showNormal()
                        else targetWindow.showMaximized()
                    }
                }
            }

            // 关闭按钮
            TitleButton {
                tooltipText: "关闭"
                hoverColor: closeButtonHoverColor

                Canvas {
                    id: closeCanvas
                    anchors.centerIn: parent
                    width: 10
                    height: 10
                    contextType: "2d"

                    property color currentColor: titleTextColor
                    property bool parentHovered: parent.parent.isHovered // TitleButton.isHovered

                    onCurrentColorChanged: requestPaint()
                    onParentHoveredChanged: requestPaint()

                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.reset();
                        // 悬停时白色，否则跟随主题文本色
                        ctx.strokeStyle = parentHovered ? titleBar.closeButtonIconHoverColor : currentColor;
                        ctx.lineWidth = 1.2;

                        ctx.beginPath();
                        ctx.moveTo(0, 0);
                        ctx.lineTo(10, 10);
                        ctx.moveTo(10, 0);
                        ctx.lineTo(0, 10);
                        ctx.stroke();
                    }
                }

                onClicked: {
                     if (targetWindow) targetWindow.close()
                }
            }
        }
    }
}
