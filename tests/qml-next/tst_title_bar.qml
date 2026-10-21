import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/Next" as Next

Item {
    id: scene
    width: 600; height: 100
    QtObject {
        id: target
        property int visibility: Window.Windowed
        property int closes: 0
        property int moves: 0
        function showMinimized() { visibility = Window.Minimized }
        function showMaximized() { visibility = Window.Maximized }
        function showNormal() { visibility = Window.Windowed }
        function close() { closes++ }
        function startSystemMove() { moves++ }
    }
    Component { id: titleComponent; Next.TitleBar { width: 600; height: 36; titlePixelSize: 14; targetWindow: target; title: "通联工坊" } }
    TestCase {
        name: "WindowControls"
        when: windowShown
        property var bar
        function init() {
            failOnWarning(/Binding loop|TypeError|ReferenceError|Unable to assign/)
            target.visibility = Window.Windowed; target.closes = 0; target.moves = 0
            bar = createTemporaryObject(titleComponent,scene); verify(bar); wait(20)
        }
        function control(index) { const button = findChild(bar,"window-control-"+index); verify(button); return button }
        function test_maximizeRestoreAndHitAreas() {
            const button = control(1)
            for(let i=0;i<3;++i) {
                const node = control(i)
                verify(node.width >= 32 && node.height >= 32)
                verify(node.mapToItem(bar,node.width,0).x <= bar.width)
                verify(node.mapToItem(bar,0,0).y >= 0)
                verify(node.mapToItem(bar,0,node.height).y <= bar.height)
            }
            const maximizeIcon = button.contentItem.text
            mouseClick(button); compare(target.visibility,Window.Maximized)
            compare(button.Accessible.name,"还原"); verify(button.contentItem.text !== maximizeIcon)
            mouseClick(button); compare(target.visibility,Window.Windowed)
            compare(button.Accessible.name,"最大化"); compare(button.contentItem.text,maximizeIcon)
        }
        function test_minimizeAndKeyboardClose() {
            mouseClick(control(0)); compare(target.visibility,Window.Minimized)
            control(2).forceActiveFocus(); keyClick(Qt.Key_Space); compare(target.closes,1)
        }
        function test_dragAndDoubleClick() {
            mouseClick(bar, 20, 10); compare(target.moves, 1)
            mouseDoubleClickSequence(bar, 20, 10); compare(target.visibility, Window.Maximized)
            mouseDoubleClickSequence(bar, 20, 10); compare(target.visibility, Window.Windowed)
        }
    }
}
