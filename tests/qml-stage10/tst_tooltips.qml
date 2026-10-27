import QtQuick
import QtQuick.Controls
import QtTest
import LogAnalyQt.Next as Next
import WorkshopStyle as WorkshopStyle

Item {
    id: scene
    width: 640; height: 480
    Next.GlassTheme { id: theme }
    Next.GlassButton {
        id: trigger
        x: 240; y: 200; width: 120; height: 36
        text: "操作"
        property string help: "清屏"
        ToolTip.visible: hovered
        ToolTip.delay: 80
        ToolTip.text: help
    }
    Next.GlassButton {
        id: underlyingButton
        visible: false
        width: 90; height: 30
        property int clicks: 0
        text: "相邻操作"
        onClicked: clicks++
    }
    Component {
        id: floatingComponent
        Window {
            width: 320; height: 240
            Next.GlassButton {
                objectName: "floating-trigger"
                x: 100; y: 100
                ToolTip.text: "独立窗口提示"
            }
        }
    }
    TestCase {
        name: "GlobalGlassToolTips"
        when: windowShown
        function init() {
            failOnWarning(/Binding loop|TypeError|ReferenceError|Unable to assign/)
            WorkshopStyle.Theme.appTheme = theme
            theme.reducedTransparency = false
            trigger.x = 240; trigger.y = 200; trigger.help = "清屏"
            mouseMove(scene, 600, 450); wait(20)
        }
        function cleanup() {
            underlyingButton.visible = false
            trigger.ToolTip.hide()
            mouseMove(scene, 600, 450); wait(20)
            WorkshopStyle.Theme.appTheme = null
        }
        function showTip(text) {
            trigger.help = text
            mouseMove(trigger, trigger.width / 2, trigger.height / 2)
            tryVerify(() => trigger.ToolTip.toolTip.visible)
            const tip = trigger.ToolTip.toolTip
            compare(tip.objectName, "workshop-tooltip")
            return tip
        }
        function test_sharedAttachedTooltipUsesTheGlobalGlassStyle() {
            const tip = showTip("只停止自动滚动，后台继续接收。")
            compare(tip.contentItem.text, trigger.help)
            compare(tip.font.pixelSize, 12)
            compare(tip.background.radius, 9)
            compare(tip.contentItem.textFormat, Text.PlainText)
            verify(tip.background.gradient.stops[0].color.a < 1)
            verify(tip.background.gradient.stops[1].color.a >= 0.9)
            verify(tip.implicitWidth < 384)
            compare(trigger.ToolTip.delay, 80)
        }
        function test_longTextAndProtocolCharactersAreBoundedPlainText() {
            const text = "<字段>&\"原文\"\n" + "C:/设备日志/".repeat(70)
            const tip = showTip(text)
            compare(tip.contentItem.text, text)
            compare(tip.contentItem.textFormat, Text.PlainText)
            verify(tip.width <= 384)
            verify(tip.height < 280)
            verify(tip.contentItem.implicitHeight > 30)
        }
        function test_reducedTransparencyUpdatesAnAlreadyVisibleTooltip() {
            const tip = showTip("透明度设置")
            theme.reducedTransparency = true; wait(20)
            verify(tip.opaqueSurface)
            compare(tip.background.gradient.stops[0].color.a, 1)
            compare(tip.background.gradient.stops[1].color.a, 1)
            theme.reducedTransparency = false; wait(20)
            verify(!tip.opaqueSurface)
        }
        function test_leavingTheTriggerAllowsAnUnderlyingButtonClick() {
            const tip = showTip("提示不会挡住其他操作")
            const point = tip.background.mapToItem(scene, 0, 0)
            underlyingButton.x = point.x + 10; underlyingButton.y = point.y + 2
            underlyingButton.visible = true; underlyingButton.clicks = 0
            wait(20)
            mouseMove(underlyingButton, underlyingButton.width / 2, underlyingButton.height / 2)
            tryVerify(() => !tip.visible)
            mouseClick(underlyingButton)
            compare(underlyingButton.clicks, 1)
        }
        function test_windowEdges_data() {
            return [{tag:"top-left",x:0,y:0}, {tag:"top-right",x:520,y:0},
                    {tag:"bottom-left",x:0,y:440}, {tag:"bottom-right",x:520,y:440}]
        }
        function test_windowEdges(data) {
            trigger.x = data.x; trigger.y = data.y
            const tip = showTip("窗口边缘的较长悬浮提示，应在窗口内完整显示。")
            wait(20)
            const window = scene.Window.window
            const point = tip.background.mapToItem(window.contentItem, 0, 0)
            verify(point.x >= 0 && point.y >= 0)
            verify(point.x + tip.width <= window.width + 1)
            verify(point.y + tip.height <= window.height + 1)
        }
        function test_sharedTooltipMovesToAnotherWindow() {
            const first = showTip("主窗口提示")
            trigger.ToolTip.hide()
            const floating = floatingComponent.createObject(scene, {visible:true})
            verify(floating !== null)
            const target = findChild(floating, "floating-trigger")
            target.ToolTip.show("独立窗口提示", -1)
            tryVerify(() => target.ToolTip.toolTip.visible)
            compare(target.ToolTip.toolTip, first)
            compare(first.text, "独立窗口提示")
            compare(first.parent.Window.window, floating)
            compare(first.objectName, "workshop-tooltip")
            target.ToolTip.hide(); floating.destroy(); wait(20)
        }
    }
}
