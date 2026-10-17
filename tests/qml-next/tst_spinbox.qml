import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import "../../qml/Next" as Next

Item {
    width: 640; height: 200
    RowLayout {
        id: row
        width: 460
        Next.CompactSpinBox { id: small; from: 0; to: 255; value: 1; editable: true; Layout.preferredWidth: 70 }
        Next.CompactSpinBox { id: large; from: 0; to: 65535; value: 65535; editable: true; Layout.preferredWidth: 86 }
        Next.CompactSpinBox { id: off; enabled: false; from: 0; to: 255; value: 20; editable: true; Layout.preferredWidth: 70 }
    }
    TestCase {
        name: "MigratedNumericControls"
        when: windowShown
        function init() { small.value = 1; large.value = 65535; small.focus = false; }
        function test_numberHasDedicatedSpace() {
            verify(small.width >= small.implicitWidth);
            verify(large.width >= large.implicitWidth);
            verify(small.contentItem.width > 40);
            verify(large.contentItem.contentWidth <= large.contentItem.width);
            compare(small.height, 34);
        }
        function test_incrementDecrementAndBounds() {
            mouseClick(small.up.indicator);
            compare(small.value, 2);
            mouseClick(small.down.indicator);
            compare(small.value, 1);
            small.value = 0;
            mouseClick(small.down.indicator);
            compare(small.value, 0);
            compare(small.down.indicator.opacity, 0.35);
            compare(small.up.indicator.opacity, 1);
            small.value = 255;
            mouseClick(small.up.indicator);
            compare(small.value, 255);
            compare(small.up.indicator.opacity, 0.35);
            compare(small.down.indicator.opacity, 1);
        }
        function test_directNumericEntry() {
            mouseClick(small.contentItem);
            small.contentItem.selectAll();
            keyClick(Qt.Key_4);
            keyClick(Qt.Key_2);
            keyClick(Qt.Key_Return);
            compare(small.value, 42);
        }
        function test_disabledButtonsDoNotChangeValue() {
            mouseClick(off.up.indicator);
            compare(off.value, 20);
        }
    }
}
