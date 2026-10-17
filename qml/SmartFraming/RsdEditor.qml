import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt

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
        var value = { type: selectorType.currentIndex }
        if (selectorType.currentIndex === 1)
            value.selector = selector1Editor.getValue()
        else if (selectorType.currentIndex === 2)
            value.selector = selector2Editor.getValue()
        else if (selectorType.currentIndex === 3)
            value.items = collectRepeaterValues(selector2Repeater)
        else if (selectorType.currentIndex === 4) {
            value.startTime = startTimeEditor.getValue()
            value.ms = msEditor4.getValue()
        } else if (selectorType.currentIndex === 5) {
            value.storageTime = storageTimeEditor.getValue()
            value.ms = msEditor5.getValue()
        } else if (selectorType.currentIndex === 6) {
            value.startTime = beginTimeEditor6.getValue()
            value.endTime = endTimeEditor6.getValue()
            value.ms = msEditor6.getValue()
        } else if (selectorType.currentIndex === 7) {
            value.startTime = beginTimeEditor7.getValue()
            value.endTime = endTimeEditor7.getValue()
            value.ti = tiEditor7.getValue()
            value.ms = msEditor7.getValue()
        } else if (selectorType.currentIndex === 8) {
            value.startTime = beginTimeEditor8.getValue()
            value.endTime = endTimeEditor8.getValue()
            value.ti = tiEditor8.getValue()
            value.ms = msEditor8.getValue()
        } else if (selectorType.currentIndex === 9) {
            value.n = Number(nSelector9.value)
        } else if (selectorType.currentIndex === 10) {
            value.n = Number(nSelector10.value)
            value.ms = msEditor10.getValue()
        } else if (selectorType.currentIndex === 11) {
            value.items = collectRepeaterValues(rsdRepeater)
        }
        return value
    }

    component SmallButton: Button {
        id: button
        implicitHeight: 26
        contentItem: Text {
            text: button.text
            color: root.theme ? root.theme.buttonText : "#333"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            font.pixelSize: 12
        }
        background: Rectangle {
            color: button.down ? (root.theme ? root.theme.buttonPressed : "#ddd") : (button.hovered ? (root.theme ? root.theme.buttonHover : "#eee") : (root.theme ? root.theme.buttonBackground : "#f5f5f5"))
            border.color: root.theme ? root.theme.controlBorder : "#ccc"
            radius: 4
        }
    }

    component StyledTextField: TextField {
        color: root.theme ? root.theme.text : "#333"
        background: Rectangle {
            color: root.theme ? root.theme.controlBackground : "#fff"
            border.color: root.theme ? root.theme.controlBorder : "#ccc"
            radius: 4
        }
    }

    component DateTimeSInput: RowLayout {
        id: dateTimeRow
        property string label: "时间"
        function getValue() {
            var dateParts = dateField.text.split("-")
            var timeParts = timeField.text.split(":")
            return {
                year: Number(dateParts[0] || 0),
                month: Number(dateParts[1] || 0),
                day: Number(dateParts[2] || 0),
                hour: Number(timeParts[0] || 0),
                minute: Number(timeParts[1] || 0),
                second: Number(timeParts[2] || 0)
            }
        }
        Layout.fillWidth: true
        Label { text: dateTimeRow.label; color: root.theme ? root.theme.subText : "#666"; Layout.preferredWidth: 84 }
        StyledTextField { id: dateField; text: Qt.formatDate(new Date(), "yyyy-MM-dd"); Layout.preferredWidth: 120 }
        StyledTextField { id: timeField; text: Qt.formatTime(new Date(), "HH:mm:ss"); Layout.preferredWidth: 100 }
        Item { Layout.fillWidth: true }
    }

    component TiInput: RowLayout {
        id: tiRow
        function getValue() { return { unit: tiUnit.currentIndex, value: Number(tiValue.value) } }
        Layout.fillWidth: true
        Label { text: "时间间隔 TI"; color: root.theme ? root.theme.subText : "#666"; Layout.preferredWidth: 84 }
        ComboBox { id: tiUnit; model: ["秒", "分", "时", "日", "月", "年"]; Layout.preferredWidth: 90 }
        SpinBox { id: tiValue; from: 0; to: 65535; value: 10; editable: true; Layout.preferredWidth: 110 }
        Item { Layout.fillWidth: true }
    }

    component MsInput: Rectangle {
        id: msRoot
        Layout.fillWidth: true
        implicitHeight: msColumn.implicitHeight + 12
        color: root.theme ? root.theme.controlBackground : "#ffffff"
        border.color: root.theme ? root.theme.controlBorder : "#cccccc"
        radius: 4

        function numberList(text) {
            var parts = text.split(/[,，\s]+/)
            var values = []
            for (var partIndex = 0; partIndex < parts.length; ++partIndex) {
                if (parts[partIndex].length > 0)
                    values.push(Number(parts[partIndex]))
            }
            return values
        }

        function hexLines(text) {
            var lines = text.split(/\n+/)
            var values = []
            for (var lineIndex = 0; lineIndex < lines.length; ++lineIndex) {
                var clean = lines[lineIndex].replace(/\s/g, "")
                if (clean.length > 0)
                    values.push(clean)
            }
            return values
        }

        function getValue() {
            return {
                type: msType.currentIndex,
                values: numberList(valuesField.text),
                tsaItems: hexLines(tsaField.text),
                rawHex: rawField.text.replace(/\s/g, ""),
                logicDevice: Number(logicDeviceSpin.value)
            }
        }

        ColumnLayout {
            id: msColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Label { text: "MS"; color: root.theme ? root.theme.text : "#333"; font.bold: true; Layout.preferredWidth: 36 }
                ComboBox {
                    id: msType
                    Layout.fillWidth: true
                    model: ["0 无表计", "1 全部用户地址", "2 一组用户类型", "3 一组用户地址 TSA", "4 一组配置序号", "5 用户类型区间", "6 用户地址区间", "7 配置序号区间", "8 一组设备类型", "9 特定逻辑设备"]
                }
            }

            StyledTextField {
                id: valuesField
                Layout.fillWidth: true
                visible: msType.currentIndex === 2 || msType.currentIndex === 4 || msType.currentIndex === 8
                placeholderText: "数值列表，用空格或逗号分隔"
            }

            TextArea {
                id: tsaField
                Layout.fillWidth: true
                Layout.preferredHeight: 58
                visible: msType.currentIndex === 3
                placeholderText: "每行一个 TSA 地址内容 hex"
                color: root.theme ? root.theme.text : "#333"
                background: Rectangle { color: root.theme ? root.theme.background : "#fff"; border.color: root.theme ? root.theme.controlBorder : "#ccc"; radius: 4 }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: msType.currentIndex === 9
                Label { text: "逻辑设备"; color: root.theme ? root.theme.subText : "#666" }
                SpinBox { id: logicDeviceSpin; from: 0; to: 255; value: 0; editable: true; Layout.preferredWidth: 100 }
                Item { Layout.fillWidth: true }
            }

            StyledTextField {
                id: rawField
                Layout.fillWidth: true
                visible: msType.currentIndex === 5 || msType.currentIndex === 6 || msType.currentIndex === 7
                placeholderText: "区间列表内容 hex"
                font.family: PlatformFonts.monospaceFamily
            }
        }
    }

    component DataSelectorInput: Rectangle {
        id: dataRoot
        property bool rangeMode: false
        property var dataSchema: null
        Layout.fillWidth: true
        implicitHeight: dataColumn.implicitHeight + 12
        color: root.theme ? root.theme.controlBackground : "#ffffff"
        border.color: root.theme ? root.theme.controlBorder : "#cccccc"
        radius: 4

        function loadSchema() {
            if (!root.backend)
                return
            var selected = selector.getValue()
            if (!selected.oi || selected.oi.length !== 4)
                return
            dataRoot.dataSchema = root.backend.getOadSchema(selected.oi, selected.attrId, false, selected.index)
        }

        function getValue() {
            if (!dataRoot.dataSchema)
                loadSchema()
            if (dataRoot.rangeMode) {
                return {
                    oad: selector.getValue(),
                    startValue: startValueEditor.getValue(),
                    endValue: endValueEditor.getValue(),
                    intervalValue: intervalValueEditor.getValue()
                }
            }
            return { oad: selector.getValue(), value: valueEditor.getValue() }
        }

        ColumnLayout {
            id: dataColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                OadSelector {
                    id: selector
                    Layout.fillWidth: true
                    theme: root.theme
                    backend: root.backend
                    methodMode: false
                    opFilter: "GET"
                    onSelectionChanged: dataRoot.dataSchema = null
                }
                SmallButton { text: "加载字段"; onClicked: dataRoot.loadSchema() }
            }

            FieldEditor {
                id: valueEditor
                Layout.fillWidth: true
                visible: !dataRoot.rangeMode
                theme: root.theme
                schema: dataRoot.dataSchema
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: dataRoot.rangeMode
                spacing: 6
                Label { text: "起始值"; color: root.theme ? root.theme.subText : "#666"; font.pixelSize: 12 }
                FieldEditor { id: startValueEditor; Layout.fillWidth: true; theme: root.theme; schema: dataRoot.dataSchema }
                Label { text: "结束值"; color: root.theme ? root.theme.subText : "#666"; font.pixelSize: 12 }
                FieldEditor { id: endValueEditor; Layout.fillWidth: true; theme: root.theme; schema: dataRoot.dataSchema }
                Label { text: "数据间隔"; color: root.theme ? root.theme.subText : "#666"; font.pixelSize: 12 }
                FieldEditor { id: intervalValueEditor; Layout.fillWidth: true; theme: root.theme; schema: dataRoot.dataSchema }
            }
        }
    }

    ListModel { id: selector2Model }
    ListModel { id: rsdModel }

    Component.onCompleted: {
        selector2Model.append({})
        rsdModel.append({})
    }

    ColumnLayout {
        id: body
        width: parent.width
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label { text: "选择方法"; color: root.theme ? root.theme.subText : "#666"; Layout.preferredWidth: 64 }
            ComboBox {
                id: selectorType
                Layout.fillWidth: true
                model: ["0 不选择", "1 OAD + Data", "2 OAD + 起止/间隔 Data", "3 多个 Selector2", "4 采集启动时间 + MS", "5 采集存储时间 + MS", "6 采集启动时间区间 + MS", "7 存储时间区间 + TI + MS", "8 成功时间区间 + TI + MS", "9 上第 n 次记录", "10 上 n 条记录 + MS", "11 多个 RSD"]
            }
        }

        DataSelectorInput { id: selector1Editor; visible: selectorType.currentIndex === 1; rangeMode: false }
        DataSelectorInput { id: selector2Editor; visible: selectorType.currentIndex === 2; rangeMode: true }

        ColumnLayout {
            Layout.fillWidth: true
            visible: selectorType.currentIndex === 3
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                SmallButton { text: "+ 添加 Selector2"; onClicked: selector2Model.append({}) }
            }
            Repeater {
                id: selector2Repeater
                model: selector2Model
                DataSelectorInput { rangeMode: true }
            }
        }

        DateTimeSInput { id: startTimeEditor; visible: selectorType.currentIndex === 4; label: "采集启动" }
        MsInput { id: msEditor4; visible: selectorType.currentIndex === 4 }
        DateTimeSInput { id: storageTimeEditor; visible: selectorType.currentIndex === 5; label: "采集存储" }
        MsInput { id: msEditor5; visible: selectorType.currentIndex === 5 }
        DateTimeSInput { id: beginTimeEditor6; visible: selectorType.currentIndex === 6; label: "起始时间" }
        DateTimeSInput { id: endTimeEditor6; visible: selectorType.currentIndex === 6; label: "结束时间" }
        MsInput { id: msEditor6; visible: selectorType.currentIndex === 6 }
        DateTimeSInput { id: beginTimeEditor7; visible: selectorType.currentIndex === 7; label: "存储起始" }
        DateTimeSInput { id: endTimeEditor7; visible: selectorType.currentIndex === 7; label: "存储结束" }
        TiInput { id: tiEditor7; visible: selectorType.currentIndex === 7 }
        MsInput { id: msEditor7; visible: selectorType.currentIndex === 7 }
        DateTimeSInput { id: beginTimeEditor8; visible: selectorType.currentIndex === 8; label: "成功起始" }
        DateTimeSInput { id: endTimeEditor8; visible: selectorType.currentIndex === 8; label: "成功结束" }
        TiInput { id: tiEditor8; visible: selectorType.currentIndex === 8 }
        MsInput { id: msEditor8; visible: selectorType.currentIndex === 8 }

        RowLayout {
            Layout.fillWidth: true
            visible: selectorType.currentIndex === 9
            Label { text: "上第 n 次记录"; color: root.theme ? root.theme.subText : "#666" }
            SpinBox { id: nSelector9; from: 0; to: 255; value: 1; editable: true; Layout.preferredWidth: 110 }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: selectorType.currentIndex === 10
            Label { text: "上 n 条记录"; color: root.theme ? root.theme.subText : "#666" }
            SpinBox { id: nSelector10; from: 0; to: 255; value: 1; editable: true; Layout.preferredWidth: 110 }
            Item { Layout.fillWidth: true }
        }
        MsInput { id: msEditor10; visible: selectorType.currentIndex === 10 }

        ColumnLayout {
            Layout.fillWidth: true
            visible: selectorType.currentIndex === 11
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                SmallButton { text: "+ 添加 RSD"; onClicked: rsdModel.append({}) }
            }
            Repeater {
                id: rsdRepeater
                model: rsdModel
                Loader {
                    Layout.fillWidth: true
                    Layout.preferredHeight: item ? item.implicitHeight : 0
                    active: selectorType.currentIndex === 11
                    source: Qt.resolvedUrl("RsdEditor.qml")
                    onLoaded: {
                        item.theme = root.theme
                        item.backend = root.backend
                        item.width = width
                    }
                    onWidthChanged: if (item) item.width = width
                    function getValue() { return item && item.getValue ? item.getValue() : { type: 0 } }
                }
            }
        }
    }
}
