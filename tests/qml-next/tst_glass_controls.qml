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
            theme.isDark = true; theme.reducedTransparency = false
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
        function test_appearancePaletteAndReducedTransparency() {
            verify(theme.surface.a < 1); verify(theme.sidebar.a < 1)
            const dark = view.field.background.baseColor.toString()
            theme.isDark = false
            tryVerify(() => view.field.background.baseColor.toString() !== dark)
            theme.reducedTransparency = true
            compare(theme.surface.a, 1); compare(theme.sidebar.a, 1)
            theme.isDark = true
            compare(theme.surface.a, 1); compare(theme.sidebar.a, 1)
        }
    }
}
