import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt

Item {
    id: root
    property var theme
    property var backend
    property bool methodMode: false
    property string opFilter: ""
    property string title: methodMode ? "OMD" : "OAD"
    signal selectionChanged()
    implicitHeight: row.implicitHeight

    function getValue() {
        if (methodMode) {
            return { oi: oiField.text.toUpperCase(), methodId: Number(idSpin.value), mode: Number(indexSpin.value), name: nameLabel.text }
        }
        return { oi: oiField.text.toUpperCase(), attrId: Number(idSpin.value), index: Number(indexSpin.value), name: nameLabel.text }
    }

    function setValue(value) {
        if (!value) return
        oiField.text = value.oi || ""
        idSpin.value = methodMode ? Number(value.methodId || 1) : Number(value.attrId || 2)
        indexSpin.value = Number(value.index || value.mode || 0)
        nameLabel.text = value.name || ""
        selectionChanged()
    }

    RowLayout {
        id: row
        width: parent.width
        spacing: 6
        Label { text: root.title; color: theme ? theme.subText : "#666"; Layout.preferredWidth: 36 }
        TextField {
            id: oiField
            Layout.preferredWidth: 78
            text: ""
            placeholderText: "0010"
            color: theme ? theme.text : "#333"
            placeholderTextColor: theme ? theme.placeholderText : "#888"
            font.family: PlatformFonts.monospaceFamily
            background: Rectangle { color: theme ? theme.controlBackground : "#fff"; border.color: theme ? theme.controlBorder : "#ccc"; radius: 4 }
            onTextChanged: root.selectionChanged()
        }
        Label { text: methodMode ? "方法" : "属性"; color: theme ? theme.subText : "#666" }
        SpinBox { id: idSpin; from: 1; to: 255; value: methodMode ? 1 : 2; editable: true; Layout.preferredWidth: 76; onValueChanged: root.selectionChanged() }
        Label { text: methodMode ? "模式" : "索引"; color: theme ? theme.subText : "#666" }
        SpinBox { id: indexSpin; from: 0; to: 255; value: 0; editable: true; Layout.preferredWidth: 70; onValueChanged: root.selectionChanged() }
        Button {
            text: "选择"
            Layout.preferredWidth: 58
            onClicked: pickerPopup.open()
        }
        Label { id: nameLabel; text: ""; color: theme ? theme.subText : "#666"; elide: Text.ElideRight; Layout.fillWidth: true }
    }

    Popup {
        id: pickerPopup
        modal: true
        width: 560
        height: 460
        x: Math.round((Overlay.overlay.width - width) / 2)
        y: Math.round((Overlay.overlay.height - height) / 2)
        parent: Overlay.overlay
        padding: 10
        background: Rectangle { color: theme ? theme.background : "#fff"; border.color: theme ? theme.controlBorder : "#ccc"; radius: 6 }
        OadPicker {
            anchors.fill: parent
            theme: root.theme
            backend: root.backend
            methodMode: root.methodMode
            opFilter: root.opFilter
            onPicked: function(oi, id, name) {
                oiField.text = oi
                idSpin.value = id
                nameLabel.text = name
                pickerPopup.close()
                root.selectionChanged()
            }
        }
    }
}
