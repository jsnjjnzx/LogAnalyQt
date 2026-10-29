import QtQuick
import QtQuick.Window
import QtTest
import LogAnalyQt.Next as Next

Item {
    id: scene
    width: 280; height: 650
    Next.GlassTheme { id: testTheme }
    QtObject {
        id: target
        property bool nativeTitleBar: true
        property int visibility: Window.Windowed
        property int moves: 0
        function startSystemMove() { moves++ }
        function showMaximized() { visibility = Window.Maximized }
        function showNormal() { visibility = Window.Windowed }
    }
    Component {
        id: navigationComponent
        Next.Navigation { width: collapsed ? 88 : 244; height: 650; theme: testTheme; targetWindow: target }
    }
    Component {
        id: realWindowComponent
        Window {
            id: realWindow
            property bool nativeTitleBar: true
            width: 900; height: 600
            flags: Qt.Window | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint
            Next.Navigation {
                objectName: "real-navigation"
                width: 244; height: parent.height
                theme: testTheme; targetWindow: realWindow
            }
        }
    }
    TestCase {
        id: navigationTest
        name: "NativeNavigationChrome"
        when: windowShown
        property var navigation
        function init() {
            failOnWarning(/Binding loop|TypeError|ReferenceError|Unable to assign/)
            target.nativeTitleBar = true; target.visibility = Window.Windowed; target.moves = 0
            ignoreWarning(/.*FontLoader.*Cannot load font.*/)
            navigation = createTemporaryObject(navigationComponent, scene)
            verify(navigation !== null); wait(20)
        }
        function dragArea() { return findChild(navigation, "nav-window-drag") }
        SignalSpy { id: navigationSpy; target: navigationTest.navigation; signalName: "navigate" }
        function test_collapsePreservesIconsAndNavigation_data() {
            return [{tag:"native-glass", native:true, opaque:false},
                    {tag:"native-opaque", native:true, opaque:true},
                    {tag:"custom-opaque", native:false, opaque:true}]
        }
        function test_collapsePreservesIconsAndNavigation(data) {
            target.nativeTitleBar = data.native
            testTheme.nativeBackdropAvailable = data.native
            testTheme.reducedTransparency = data.opaque
            const toggle = findChild(navigation, "nav-collapse-toggle")
            verify(toggle !== null)
            compare(toggle.parent, navigation)
            compare(findChild(navigation, "nav--1"), null)
            compare(toggle.x + toggle.width / 2, navigation.width)
            compare(toggle.y + toggle.height / 2, navigation.height / 2)
            verify(toggle.width >= 24 && toggle.height >= 24)
            navigationSpy.clear()
            mouseClick(toggle, toggle.width - 2, toggle.height / 2); wait(20)
            compare(navigationSpy.count, 0)
            compare(navigation.collapsed, true); compare(navigation.width, 88)
            compare(toggle.x + toggle.width / 2, navigation.width)
            verify(!findChild(navigation, "nav-brand").visible)
            for (const page of [0, 1, 2, 3, 4, 14, 5, 6, 15, 7, 8, 9, 10]) {
                const icon = findChild(navigation, "nav-icon-" + page)
                verify(icon.visible)
                verify(!findChild(navigation, "nav-label-" + page).visible)
                verify(Math.abs(icon.mapToItem(navigation, icon.width / 2, 0).x - 44) < 1)
            }
            for (const index of [0, 7, 10, 12]) verify(!findChild(navigation, "nav-section-" + index).visible)
            verify(findChild(navigation, "nav-icon-0").text !== findChild(navigation, "nav-icon-7").text)
            const log = findChild(navigation, "nav-0")
            verify(log.mapToItem(navigation, 0, 0).y >= dragArea().height)
            navigationSpy.clear()
            mouseClick(log); compare(navigationSpy.count, 1); compare(navigationSpy.signalArguments[0][0], 0)
            mouseClick(findChild(navigation, "nav-9")); compare(navigationSpy.signalArguments[1][0], 9)
            mouseClick(findChild(navigation, "nav-10")); compare(navigationSpy.signalArguments[2][0], 10)
            toggle.forceActiveFocus(); keyClick(Qt.Key_Space); wait(20)
            compare(navigation.collapsed, false); compare(navigation.width, 244)
            verify(findChild(navigation, "nav-label-7").visible)
            verify(findChild(navigation, "nav-brand").visible)
        }
        function test_doubleClickMaximizesAndRestores() {
            for (let cycle = 0; cycle < 2; ++cycle) {
                mouseDoubleClickSequence(dragArea(), 200, 15)
                compare(target.visibility, Window.Maximized)
                mouseDoubleClickSequence(dragArea(), 200, 15)
                compare(target.visibility, Window.Windowed)
            }
        }
        function test_singlePressKeepsDragAndFullscreenStaysFullscreen() {
            mouseClick(dragArea(), 200, 15); compare(target.moves, 1)
            target.visibility = Window.FullScreen
            mouseDoubleClickSequence(dragArea(), 200, 15)
            compare(target.visibility, Window.FullScreen)
        }
        function test_otherPlatformsKeepTheExistingSidebarBehavior() {
            target.nativeTitleBar = false
            mouseDoubleClickSequence(dragArea(), 200, 10)
            compare(target.visibility, Window.Windowed)
        }
        function test_doubleClickOnARealWindowMaximizesAndRestores() {
            if (Qt.platform.os !== "osx") skip("macOS native titlebar regression")
            ignoreWarning(/.*FontLoader.*Cannot load font.*/)
            const real = createTemporaryObject(realWindowComponent, scene, {visible:true})
            verify(real !== null); wait(60)
            const area = findChild(real, "nav-window-drag")
            verify(area !== null)
            const normalWidth = real.width, normalHeight = real.height
            for (let cycle = 0; cycle < 2; ++cycle) {
                mouseDoubleClickSequence(area, 200, 15)
                tryCompare(real, "visibility", Window.Maximized)
                mouseDoubleClickSequence(area, 200, 15)
                tryCompare(real, "visibility", Window.Windowed)
                tryCompare(real, "width", normalWidth)
                tryCompare(real, "height", normalHeight)
            }
        }
    }
}
