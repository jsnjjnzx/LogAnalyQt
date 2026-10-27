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
        Next.Navigation { width: 244; height: 650; theme: testTheme; targetWindow: target }
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
