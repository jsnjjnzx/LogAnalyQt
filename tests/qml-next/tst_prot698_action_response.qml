import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml/Next" as Next
import "../../qml/Next/SmartFraming" as Migrated

Item {
    id: scene
    width: 1100; height: 1000
    Next.GlassTheme { id: testTheme }
    QtObject {
        id: definitions
        function get698ServiceSchema(code) { return {} }
        function getNextPiid() { return 1 }
        function listAllOad(filter) { return [] }
        function getOadSchema(oi, member, method, index) {
            return {name: "synthetic method input", type: "integer", unit: "", scaler: 0, children: []}
        }
    }
    Component { id: migrated; Migrated.ServiceTreeEditor { theme: testTheme; backend: definitions } }
    SignalSpy { id: errorSignal; signalName: "statusMessage" }
    TestCase {
        name: "Prot698ActionResponseEditors"
        when: windowShown
        property var editor
        function init() { failOnWarning(/Binding loop|TypeError|ReferenceError|Unable to assign|Internal error/); }
        function cleanup() { errorSignal.target = null; if (editor) editor.destroy(); editor = null; wait(30); }
        function visibleNamed(node, name) {
            if (node.objectName === name && node.visible) return node
            if (node.children) for (let i = 0; i < node.children.length; ++i) {
                const found = visibleNamed(node.children[i], name)
                if (found) return found
            }
            return null
        }
        function item(name) { const found = visibleNamed(editor, name); verify(found !== null, name); return found }
        function input(node) {
            if (node.visible && typeof node.selectAll === "function" && typeof node.echoMode !== "undefined") return node
            if (node.children) for (let i = 0; i < node.children.length; ++i) {
                const found = input(node.children[i]); if (found) return found
            }
            return null
        }
        function enter(control, text) {
            verify(control !== null)
            control.forceActiveFocus(); control.selectAll(); keyClick(Qt.Key_Backspace)
            for (let i = 0; i < text.length; ++i) keyClick(text.charAt(i))
        }
        function result() {
            const tree = editor.getTreeValue()
            if (editor.serviceCode === "ACTION_RESPONSE_LIST") { compare(tree.actionResults.length, 1); return tree.actionResults[0] }
            return tree.actionResult
        }
        function selectType(type) {
            const picker = item("action-result-return-type")
            const position = picker.model.indexOf(type)
            verify(position >= 0, type + " selectable")
            picker.currentIndex = position; wait(30)
        }
        function test_typedReturn_data() {
            return [{tag:"normal", list:false}, {tag:"list", list:true}]
        }
        function test_typedReturn(data) {
            editor = migrated.createObject(scene, {
                width: 1060, serviceCode: data.list ? "ACTION_RESPONSE_LIST" : "ACTION_RESPONSE_NORMAL"})
            verify(editor !== null); wait(60)
            errorSignal.target = editor; errorSignal.clear()
            compare(result().hasData, false)
            verify(!Object.prototype.hasOwnProperty.call(result(), "dataValue"))
            mouseClick(item("action-result-has-data")); wait(30)
            compare(result().hasData, true)
            selectType("unsigned")
            enter(input(item("action-result-field-editor")), "255")
            compare(result().dataValue.type, "unsigned"); compare(result().dataValue.value, 255)
            // Method input is signed integer; return type must stay independently chosen.
            selectType("long64_unsigned")
            enter(item("action-result-return-64"), "18446744073709551615")
            compare(result().dataValue.type, "long64_unsigned")
            compare(result().dataValue.value, "18446744073709551615")
            selectType("structure")
            item("action-result-return-json").text = '[{"type":"unsigned","value":7},{"type":"null","value":null}]'
            const typed = result().dataValue
            compare(typed.type, "structure"); compare(typed.value.length, 2)
            compare(typed.value[0].type, "unsigned"); compare(typed.value[0].value, 7)
            compare(typed.value[1].type, "null"); compare(typed.value[1].value, null)
            selectType("null")
            compare(result().dataValue.type, "null"); compare(result().dataValue.value, null)
            mouseClick(item("action-result-has-data")); wait(30)
            compare(result().hasData, false)
            verify(!Object.prototype.hasOwnProperty.call(result(), "dataValue"))
            compare(errorSignal.count, 0)
        }
    }
}
