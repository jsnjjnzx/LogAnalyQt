import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml/Next" as Next
import "../../qml/Next/SmartFraming" as Framing

Item {
    id: scene
    width: 800; height: 200
    Next.GlassTheme { id: testTheme }
    Component { id: selectorComponent; Framing.OadSelector { width: 760; theme: testTheme } }
    Component { id: headerComponent; Framing.FrameHeaderPanel { width: 760; theme: testTheme } }

    TestCase {
        name: "Prot698SelectorContract"
        when: windowShown

        function containsText(node, text) {
            if (node.text === text) return true
            if (node.children) for (let i = 0; i < node.children.length; ++i)
                if (containsText(node.children[i], text)) return true
            return false
        }

        function test_protocolFieldsOnly_data() {
            return [
                { tag: "manual-oad", method: false, name: "" },
                { tag: "named-oad", method: false, name: "正向有功电能" },
                { tag: "manual-omd", method: true, name: "" },
                { tag: "named-omd", method: true, name: "复位" }
            ]
        }

        function test_protocolFieldsOnly(data) {
            const selector = createTemporaryObject(selectorComponent, scene, { methodMode: data.method })
            verify(selector !== null)
            // Even an empty display name must stay outside the strict protocol object.
            compare(Object.keys(selector.getValue()).sort().join(","),
                    data.method ? "methodId,mode,oi" : "attrId,index,oi")
            const selection = data.method
                ? { oi: "fabc", methodId: 3, mode: 7, name: data.name }
                : { oi: "abcd", attrId: 2, index: 5, name: data.name }
            selector.setValue(selection)
            const value = selector.getValue()
            compare(Object.keys(value).sort().join(","),
                    data.method ? "methodId,mode,oi" : "attrId,index,oi")
            compare(value.oi, selection.oi.toUpperCase())
            compare(value[data.method ? "methodId" : "attrId"], data.method ? 3 : 2)
            compare(value[data.method ? "mode" : "index"], data.method ? 7 : 5)
            if (data.name) verify(containsText(selector, data.name), "Selection name remains on screen")
        }

        function test_optionalFrameAddress_data() {
            return [
                { tag: "empty", address: "", expected: "" },
                { tag: "whitespace", address: " \t ", expected: "" },
                { tag: "physical-address", address: "01 02 03 04 05 06", expected: "010203040506" },
                { tag: "invalid-address-keeps-validation", address: "GG", expected: "GG" }
            ]
        }

        function test_optionalFrameAddress(data) {
            const header = createTemporaryObject(headerComponent, scene, { defaultAddress: data.address })
            verify(header !== null)
            const options = header.getOptions()
            compare(Object.prototype.hasOwnProperty.call(options, "physicalAddressHex"), data.expected.length > 0)
            if (data.expected) compare(options.physicalAddressHex, data.expected)
            compare(options.dir, false)
            compare(options.prm, true)
            compare(options.funcCode, 3)
        }
    }
}
