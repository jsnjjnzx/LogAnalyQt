import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import LogAnalyQt.Next as Next

QQC2.AbstractButton {
    id: control
    required property var theme
    required property var options
    required property var excludedValues
    readonly property bool filterActive: excludedValues.length > 0
    signal excludedValuesEdited(var values)
    hoverEnabled: true
    Accessible.name: text + "筛选"
    QQC2.ToolTip.visible: hovered
    QQC2.ToolTip.text: "筛选" + text + (filterActive ? "（已启用）" : "")
    contentItem: Item {
        Row {
            anchors.centerIn: parent
            height: parent.height
            spacing: 3
            Text {
                width: Math.min(implicitWidth, Math.max(0, control.availableWidth - 12))
                height: parent.height
                text: control.text
                font.bold: true
                color: control.filterActive ? control.theme.accent : control.theme.text
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Text {
                width: 9; height: parent.height
                text: "▾"
                color: control.filterActive ? control.theme.accent : control.theme.text
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
    background: Rectangle {
        color: control.hovered || control.visualFocus ? control.theme.rowAlt : "transparent"
        border.width: control.visualFocus ? 1 : 0
        border.color: control.theme.accent
    }
    onClicked: picker.open()

    QQC2.Popup {
        id: picker
        objectName: control.objectName + "-popup"
        y: control.height
        width: 224
        padding: 10
        margins: 8
        focus: true
        closePolicy: QQC2.Popup.CloseOnEscape | QQC2.Popup.CloseOnPressOutside
        background: Next.ControlSurface {
            radius: 12
            baseColor: control.theme.popupBackground
            edgeColor: control.theme.controlBorder
        }
        onOpened: selectAll.forceActiveFocus()
        contentItem: ColumnLayout {
            spacing: 4
            Next.GlassCheckBox {
                id: selectAll
                objectName: control.objectName + "-all"
                Layout.fillWidth: true
                text: "全选（清除筛选）"
                checked: !control.filterActive
                onToggled: control.excludedValuesEdited(checked ? [] : control.options.slice())
            }
            Next.GlassMenuSeparator { Layout.fillWidth: true }
            Repeater {
                model: control.options
                Next.GlassCheckBox {
                    required property string modelData
                    required property int index
                    objectName: control.objectName + "-option-" + index
                    Layout.fillWidth: true
                    text: modelData
                    checked: control.excludedValues.indexOf(modelData) < 0
                    onToggled: {
                        const values = control.excludedValues.slice();
                        const position = values.indexOf(modelData);
                        if (checked && position >= 0) values.splice(position, 1);
                        else if (!checked && position < 0) values.push(modelData);
                        control.excludedValuesEdited(values);
                    }
                }
            }
        }
    }
}
