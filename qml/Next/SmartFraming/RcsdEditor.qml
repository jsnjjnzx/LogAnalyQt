import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var theme
    property var backend
    implicitHeight: body.implicitHeight

    function collectRepeaterValues(repeater) {
        var values = []
        for (var itemIndex = 0; itemIndex < repeater.count; ++itemIndex) {
            var item = repeater.itemAt(itemIndex)
            if (item && item.getValue)
                values.push(item.getValue())
        }
        return values
    }

    function getValue() {
        return { items: collectRepeaterValues(csdRepeater) }
    }

    component SmallButton: Button {
        id: button
        implicitHeight: 26
        contentItem: Text { text: button.text; color: root.theme ? root.theme.buttonText : "#333"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; font.pixelSize: 12 }
        background: Rectangle { color: button.down ? (root.theme ? root.theme.buttonPressed : "#ddd") : (button.hovered ? (root.theme ? root.theme.buttonHover : "#eee") : (root.theme ? root.theme.buttonBackground : "#f5f5f5")); border.color: root.theme ? root.theme.controlBorder : "#ccc"; radius: 8 }
    }

    component OadLineEditor: Rectangle {
        id: oadLine
        property int itemNo: 1
        signal removeRequested()
        Layout.fillWidth: true
        implicitHeight: row.implicitHeight + 12
        color: root.theme ? root.theme.background : "#ffffff"
        border.color: root.theme ? root.theme.controlBorder : "#cccccc"
        radius: 8
        function getValue() { return selector.getValue() }
        RowLayout {
            id: row
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            Label { text: "#" + oadLine.itemNo; color: root.theme ? root.theme.subText : "#666"; Layout.preferredWidth: 34 }
            Next.OadSelector { id: selector; Layout.fillWidth: true; theme: root.theme; backend: root.backend; methodMode: false; opFilter: "GET" }
            SmallButton { text: "删除"; onClicked: oadLine.removeRequested() }
        }
    }

    component CsdEditor: Rectangle {
        id: csdRoot
        property int itemNo: 1
        signal removeRequested()
        Layout.fillWidth: true
        implicitHeight: csdColumn.implicitHeight + 12
        color: root.theme ? root.theme.controlBackground : "#ffffff"
        border.color: root.theme ? root.theme.controlBorder : "#cccccc"
        radius: 8

        ListModel { id: roadOadModel }
        Component.onCompleted: roadOadModel.append({})

        function getValue() {
            return {
                choice: choiceCombo.currentIndex,
                oad: mainOad.getValue(),
                roadOads: root.collectRepeaterValues(roadOadRepeater)
            }
        }

        ColumnLayout {
            id: csdColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Label { text: "CSD " + csdRoot.itemNo; color: root.theme ? root.theme.text : "#333"; font.bold: true }
                ComboBox { id: choiceCombo; model: ["OAD", "ROAD"]; Layout.preferredWidth: 110 }
                Item { Layout.fillWidth: true }
                SmallButton { text: "删除"; onClicked: csdRoot.removeRequested() }
            }

            Next.OadSelector { id: mainOad; Layout.fillWidth: true; theme: root.theme; backend: root.backend; methodMode: false; opFilter: "GET"; title: choiceCombo.currentIndex === 0 ? "OAD" : "主 OAD" }

            ColumnLayout {
                Layout.fillWidth: true
                visible: choiceCombo.currentIndex === 1
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "关联 OAD"; color: root.theme ? root.theme.subText : "#666" }
                    Item { Layout.fillWidth: true }
                    SmallButton { text: "+ 添加"; onClicked: roadOadModel.append({}) }
                }
                Repeater {
                    id: roadOadRepeater
                    model: roadOadModel
                    OadLineEditor { itemNo: index + 1; onRemoveRequested: roadOadModel.remove(index) }
                }
            }
        }
    }

    ListModel { id: csdModel }
    Component.onCompleted: csdModel.append({})

    ColumnLayout {
        id: body
        width: parent.width
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label { text: "记录列 CSD"; color: root.theme ? root.theme.text : "#333"; font.bold: true }
            Item { Layout.fillWidth: true }
            SmallButton { text: "+ 添加列"; onClicked: csdModel.append({}) }
        }

        Repeater {
            id: csdRepeater
            model: csdModel
            CsdEditor { itemNo: index + 1; onRemoveRequested: csdModel.remove(index) }
        }
    }
}