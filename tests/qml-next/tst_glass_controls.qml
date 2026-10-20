import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import "../../qml/Next" as Next

Pane {
    id: scene
    width: 560; height: 360
    Next.GlassTheme { id: theme }
    palette.window: theme.background
    palette.button: theme.buttonBackground
    palette.buttonText: theme.text
    palette.text: theme.text
    palette.base: theme.controlBackground
    palette.mid: theme.controlBorder
    palette.highlight: theme.accentMuted
    palette.highlightedText: "white"
    Component {
        id: controls
        ColumnLayout {
            property alias button: button
            property alias field: field
            property alias choice: choice
            property alias check: check
            property alias toggle: toggle
            property int clicks: 0
            Next.GlassButton { id: button; text: "解析"; onClicked: parent.clicks++ }
            Next.GlassTextField { id: field; validator: IntValidator { bottom: 0; top: 255 } text: "12" }
            Next.GlassComboBox { id: choice; model: ["698", "645", "104"] }
            Next.GlassCheckBox { id: check; text: "携带数据" }
            Next.GlassSwitch { id: toggle; text: "自动滚动" }
        }
    }
    TestCase {
        name: "V4GlassControls"
        when: windowShown
        property var view
        function init() {
            failOnWarning(/Binding loop|TypeError|ReferenceError|Unable to assign/)
            theme.reducedTransparency = false
            theme.nativeBackdropAvailable = false; theme.systemReducedTransparency = false
            view = createTemporaryObject(controls, scene)
            verify(view); wait(20)
        }
        function test_keyboardActivationAndDisabled() {
            view.button.forceActiveFocus()
            keyClick(Qt.Key_Space)
            compare(view.clicks, 1)
            view.button.enabled = false
            mouseClick(view.button)
            compare(view.clicks, 1)
            verify(view.button.opacity < 1)
        }
        function test_numericValidationAndSelection() {
            view.field.forceActiveFocus(); view.field.selectAll()
            keyClick(Qt.Key_4); keyClick(Qt.Key_2)
            compare(view.field.text, "42"); verify(view.field.acceptableInput)
            keyClick(Qt.Key_X); compare(view.field.text, "42")
            verify(view.field.background.focused)
            view.choice.forceActiveFocus(); keyClick(Qt.Key_Down)
            compare(view.choice.currentIndex, 1)
            mouseClick(view.choice); tryCompare(view.choice.popup, "visible", true)
            keyClick(Qt.Key_Escape); tryCompare(view.choice.popup, "visible", false)
            compare(view.choice.currentIndex, 1)
        }
        function test_checksAndSwitches() {
            mouseClick(view.check); verify(view.check.checked)
            view.check.tristate = true; view.check.checkState = Qt.PartiallyChecked
            view.check.forceActiveFocus(); keyClick(Qt.Key_Space)
            compare(view.check.checkState, Qt.Checked)
            view.toggle.forceActiveFocus(); keyClick(Qt.Key_Space)
            verify(view.toggle.checked)
            view.toggle.enabled = false; mouseClick(view.toggle)
            verify(view.toggle.checked)
        }
        function test_comboArrowAlignment() {
            view.choice.model = ["协议 XML", "AI 提示词"]
            for (const height of [28, 36, 44]) {
                view.choice.Layout.preferredHeight = height
                wait(20)
                const arrow = view.choice.indicator
                compare(arrow.width, 14)
                compare(arrow.height, 14)
                compare(arrow.y + arrow.height / 2, view.choice.height / 2)
                compare(view.choice.width - arrow.x - arrow.width, 12)
            }
            mouseClick(view.choice)
            tryCompare(view.choice.popup, "visible", true)
            keyClick(Qt.Key_Down); keyClick(Qt.Key_Return)
            compare(view.choice.currentIndex, 1)
        }
        function test_reducedTransparency() {
            verify(theme.surface.a < 1); verify(theme.sidebar.a < 1)
            theme.reducedTransparency = true
            compare(theme.surface.a, 1); compare(theme.sidebar.a, 1)
        }
        function test_nativeGlassAndAccessibilityFallback() {
            const fallbackAlpha = theme.surface.a
            theme.nativeBackdropAvailable = true
            verify(theme.nativeGlass); verify(theme.surface.a < fallbackAlpha)
            verify(theme.controlBackground.a > theme.surface.a)
            theme.systemReducedTransparency = true
            verify(!theme.nativeGlass); compare(theme.surface.a, 1); compare(theme.sidebar.a, 1)
            theme.systemReducedTransparency = false
            verify(theme.nativeGlass)
            theme.reducedTransparency = true
            verify(!theme.nativeGlass); compare(theme.surface.a, 1); compare(theme.sidebar.a, 1)
            theme.reducedTransparency = false
            verify(theme.nativeGlass)
        }
    }
}
