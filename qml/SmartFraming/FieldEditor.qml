import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt

Item {
    id: root
    property var theme
    property var schema: null
    property bool controlsReady: false
    property bool compactDetailLayout: width < 480
    implicitHeight: body.implicitHeight

    onSchemaChanged: resetForSchema()
    onThemeChanged: if (controlsReady) refreshTimer.restart()

    function lowerType() {
        return schema && schema.type ? String(schema.type).toLowerCase().replace(/-/g, "_") : ""
    }

    function firstChildSchema() {
        return root.schema && root.schema.children && root.schema.children.length > 0 ? root.schema.children[0] : null
    }

    function selectedChoiceSchema() {
        return root.schema && root.schema.children && choiceCombo.currentIndex >= 0 ? root.schema.children[choiceCombo.currentIndex] : null
    }

    function schemaValue(key) {
        return root.schema && typeof root.schema[key] !== "undefined" ? root.schema[key] : undefined
    }

    function boolSchemaValue(key, fallback) {
        var value = schemaValue(key)
        if (typeof value === "undefined")
            return fallback
        if (typeof value === "string") {
            var normalized = value.trim().toLowerCase()
            if (normalized === "false" || normalized === "0" || normalized.length === 0)
                return false
            if (normalized === "true" || normalized === "1")
                return true
        }
        return Boolean(value)
    }

    function numberSchemaValue(key, fallback) {
        var value = schemaValue(key)
        if (typeof value === "undefined")
            return fallback
        var parsed = Number(value)
        return isNaN(parsed) ? fallback : parsed
    }

    function stringSchemaValue(key, fallback) {
        var value = schemaValue(key)
        return typeof value === "undefined" ? fallback : String(value)
    }

    function defaultArrayCount() {
        if (root.lowerType() !== "array")
            return 0
        var count = Math.floor(numberSchemaValue("defaultCount", 0))
        return count < 0 ? 0 : count
    }

    function defaultChoiceIndex() {
        if (choiceCombo.count <= 0)
            return -1
        var index = Math.floor(numberSchemaValue("defaultChoiceIndex", 0))
        if (index < 0 || index >= choiceCombo.count)
            return 0
        return index
    }

    function resetForSchema() {
        if (!controlsReady)
            return
        optionalCheck.checked = boolSchemaValue("defaultPresent", true)
        numberInput.text = stringSchemaValue("defaultValue", "0")
        boolInput.checked = boolSchemaValue("defaultValue", false)
        textInput.text = stringSchemaValue("defaultValue", "")
        var now = new Date()
        dateInput.text = Qt.formatDate(now, "yyyy-MM-dd")
        weekInput.text = "255"
        timeInput.text = Qt.formatTime(now, "HH:mm:ss")
        msInput.text = "0"
        tiUnit.currentIndex = 0
        tiValue.value = 0
        scalerInput.text = "0"
        unitInput.text = "0"
        arrayColumn.arrayCount = defaultArrayCount()
        refreshTimer.restart()
    }

    Timer {
        id: refreshTimer
        interval: 0
        onTriggered: {
            root.resetChoiceSelection()
            root.refreshChildEditors()
        }
    }

    Component.onCompleted: {
        controlsReady = true
        resetForSchema()
    }

    function resetChoiceSelection() {
        if (root.lowerType() !== "choice")
            return
        choiceCombo.currentIndex = defaultChoiceIndex()
        refreshChoiceEditor()
    }

    function refreshChoiceEditor() {
        if (!choiceLoader.item)
            return
        choiceLoader.item.theme = root.theme
        choiceLoader.item.width = choiceLoader.width
        choiceLoader.item.schema = selectedChoiceSchema()
    }

    function refreshChildEditors() {
        for (var structIndex = 0; structIndex < structureRepeater.count; ++structIndex) {
            var structItem = structureRepeater.itemAt(structIndex)
            if (structItem && structItem["applyChildSchema"])
                structItem["applyChildSchema"]()
        }
        for (var arrayIndex = 0; arrayIndex < arrayRepeater.count; ++arrayIndex) {
            var arrayItem = arrayRepeater.itemAt(arrayIndex)
            if (arrayItem && arrayItem["applyElementSchema"])
                arrayItem["applyElementSchema"]()
        }
        refreshChoiceEditor()
    }

    function getValue() {
        var value = coreValue()
        if (schema && schema.optional)
            return { present: optionalCheck.checked, value: value }
        return value
    }

    function coreValue() {
        var typeName = lowerType()
        if (!schema)
            return null
        if (typeName === "structure") {
            var structValues = []
            for (var structIndex = 0; structIndex < structureRepeater.count; ++structIndex) {
                var structItem = structureRepeater.itemAt(structIndex)
                structValues.push(structItem && structItem["getValue"] ? structItem["getValue"]() : null)
            }
            return structValues
        }
        if (typeName === "array") {
            var arrayValues = []
            for (var arrayIndex = 0; arrayIndex < arrayRepeater.count; ++arrayIndex) {
                var arrayItem = arrayRepeater.itemAt(arrayIndex)
                arrayValues.push(arrayItem && arrayItem["getValue"] ? arrayItem["getValue"]() : null)
            }
            return arrayValues
        }
        if (typeName === "choice") {
            var selected = selectedChoiceSchema()
            return {
                choiceValue: selected ? Number(selected.choiceValue) : -1,
                value: choiceLoader.item && choiceLoader.item["getValue"] ? choiceLoader.item["getValue"]() : null
            }
        }
        if (typeName === "bool" || typeName === "boolean")
            return boolInput.checked
        if (typeName === "enum")
            return enumInput.currentValue
        if (["integer", "long", "double_long", "long64", "unsigned", "long_unsigned", "double_long_unsigned", "long64_unsigned", "float32", "float64"].indexOf(typeName) >= 0)
            return Number(numberInput.text || 0)
        if (typeName === "date_time" || typeName === "date_time_s" || typeName === "date" || typeName === "time")
            return dateTimeValue()
        if (typeName === "ti")
            return { unit: tiUnit.currentIndex, value: Number(tiValue.value) }
        if (typeName === "scaler_unit" || typeName === "scalerunit")
            return { scaler: Number(scalerInput.text || 0), unit: Number(unitInput.text || 0) }
        if (typeName === "null")
            return null
        return textInput.text
    }

    function dateTimeValue() {
        var dateParts = dateInput.text.split("-")
        var timeParts = timeInput.text.split(":")
        return {
            year: Number(dateParts[0] || 0),
            month: Number(dateParts[1] || 0),
            day: Number(dateParts[2] || 0),
            week: Number(weekInput.text || 255),
            hour: Number(timeParts[0] || 0),
            minute: Number(timeParts[1] || 0),
            second: Number(timeParts[2] || 0),
            ms: Number(msInput.text || 0)
        }
    }

    ColumnLayout {
        id: body
        width: parent.width
        spacing: 6

        CheckBox {
            id: optionalCheck
            visible: !!(schema && schema.optional)
            checked: true
            text: schema ? ((schema.name || "字段") + " 存在") : "字段存在"
            palette.windowText: theme ? theme.text : "#333"
        }

        Rectangle {
            Layout.fillWidth: true
            visible: !!root.schema
            implicitHeight: leafRow.implicitHeight + 10
            color: root.lowerType() === "structure" || root.lowerType() === "array" || root.lowerType() === "choice" ? "transparent" : (theme ? theme.controlBackground : "#fff")
            border.color: root.lowerType() === "structure" || root.lowerType() === "array" || root.lowerType() === "choice" ? "transparent" : (theme ? theme.controlBorder : "#ccc")
            radius: 4

            RowLayout {
                id: leafRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 8

                Label {
                    text: schema ? ((schema.name || schema.type || "字段") + ":") : ""
                    color: theme ? theme.subText : "#666"
                    Layout.preferredWidth: 140
                    elide: Text.ElideRight
                    font.pixelSize: 12
                }

                TextField {
                    id: numberInput
                    visible: ["integer", "long", "double_long", "long64", "unsigned", "long_unsigned", "double_long_unsigned", "long64_unsigned", "float32", "float64"].indexOf(root.lowerType()) >= 0
                    Layout.fillWidth: visible
                    text: "0"
                    color: theme ? theme.text : "#333"
                    font.family: PlatformFonts.monospaceFamily
                    background: Rectangle { color: theme ? theme.background : "#fff"; border.color: theme ? theme.controlBorder : "#ccc"; radius: 4 }
                }

                CheckBox {
                    id: boolInput
                    visible: root.lowerType() === "bool" || root.lowerType() === "boolean"
                    text: "true"
                    palette.windowText: theme ? theme.text : "#333"
                }

                ComboBox {
                    id: enumInput
                    visible: root.lowerType() === "enum"
                    Layout.fillWidth: visible
                    model: schema && schema.enumValues ? schema.enumValues : []
                    textRole: "text"
                    valueRole: "value"
                    background: Rectangle { color: theme ? theme.background : "#fff"; border.color: theme ? theme.controlBorder : "#ccc"; radius: 4 }
                }

                TextField {
                    id: textInput
                    visible: ["octet_string", "visible_string", "utf8_string", "bit_string", "oi", "oad", "omd", "road", "tsa", "mac", "rn", "rsd", "csd", "ms", "rcsd", "comdcb", "region", "sid", "sid_mac", "vqds", "checksum"].indexOf(root.lowerType()) >= 0
                    Layout.fillWidth: visible
                    placeholderText: root.lowerType() === "visible_string" || root.lowerType() === "utf8_string" ? "文本" : "hex / 原始值"
                    color: theme ? theme.text : "#333"
                    placeholderTextColor: theme ? theme.placeholderText : "#888"
                    font.family: PlatformFonts.monospaceFamily
                    background: Rectangle { color: theme ? theme.background : "#fff"; border.color: theme ? theme.controlBorder : "#ccc"; radius: 4 }
                }

                Label {
                    visible: schema && (schema.unit || schema.fixedSize > 0) && (numberInput.visible || textInput.visible)
                    text: schema && schema.fixedSize > 0 ? (schema.fixedSize + "字节") : (schema ? schema.unit : "")
                    color: theme ? theme.subText : "#666"
                }
            }
        }

        Item {
            Layout.fillWidth: true
            visible: ["date_time", "date_time_s", "date", "time"].indexOf(root.lowerType()) >= 0
            implicitHeight: dateTimeFlow.implicitHeight

            Flow {
                id: dateTimeFlow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: root.compactDetailLayout ? 0 : 148
                spacing: 6

                TextField {
                    id: dateInput
                    visible: root.lowerType() !== "time"
                    width: 110
                    text: Qt.formatDate(new Date(), "yyyy-MM-dd")
                    color: theme ? theme.text : "#333"
                    background: Rectangle { color: theme ? theme.controlBackground : "#fff"; border.color: theme ? theme.controlBorder : "#ccc"; radius: 4 }
                }

                TextField {
                    id: weekInput
                    visible: root.lowerType() === "date_time" || root.lowerType() === "date"
                    width: 48
                    text: "255"
                    color: theme ? theme.text : "#333"
                    background: Rectangle { color: theme ? theme.controlBackground : "#fff"; border.color: theme ? theme.controlBorder : "#ccc"; radius: 4 }
                }

                TextField {
                    id: timeInput
                    visible: root.lowerType() !== "date"
                    width: 90
                    text: Qt.formatTime(new Date(), "HH:mm:ss")
                    color: theme ? theme.text : "#333"
                    background: Rectangle { color: theme ? theme.controlBackground : "#fff"; border.color: theme ? theme.controlBorder : "#ccc"; radius: 4 }
                }

                TextField {
                    id: msInput
                    visible: root.lowerType() === "date_time"
                    width: 54
                    text: "0"
                    color: theme ? theme.text : "#333"
                    background: Rectangle { color: theme ? theme.controlBackground : "#fff"; border.color: theme ? theme.controlBorder : "#ccc"; radius: 4 }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            visible: root.lowerType() === "ti"
            implicitHeight: tiFlow.implicitHeight

            Flow {
                id: tiFlow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: root.compactDetailLayout ? 0 : 148
                spacing: 6

                ComboBox {
                    id: tiUnit
                    width: 80
                    model: ["秒", "分", "时", "日", "月", "年"]
                }

                SpinBox {
                    id: tiValue
                    width: 100
                    from: 0
                    to: 65535
                    value: 0
                    editable: true
                }
            }
        }

        Item {
            Layout.fillWidth: true
            visible: root.lowerType() === "scaler_unit" || root.lowerType() === "scalerunit"
            implicitHeight: scalerUnitFlow.implicitHeight

            Flow {
                id: scalerUnitFlow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: root.compactDetailLayout ? 0 : 148
                spacing: 6

                Label { text: "倍率"; color: theme ? theme.subText : "#666" }

                TextField {
                    id: scalerInput
                    width: 60
                    text: "0"
                    color: theme ? theme.text : "#333"
                    background: Rectangle { color: theme ? theme.controlBackground : "#fff"; border.color: theme ? theme.controlBorder : "#ccc"; radius: 4 }
                }

                Label { text: "单位"; color: theme ? theme.subText : "#666" }

                TextField {
                    id: unitInput
                    width: 60
                    text: "0"
                    color: theme ? theme.text : "#333"
                    background: Rectangle { color: theme ? theme.controlBackground : "#fff"; border.color: theme ? theme.controlBorder : "#ccc"; radius: 4 }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.lowerType() === "structure"
            spacing: 6
            Label { text: schema ? ((schema.name || "structure") + " 结构") : ""; color: theme ? theme.text : "#333"; font.bold: true }
            Repeater {
                id: structureRepeater
                model: schema && schema.children ? schema.children : []
                Loader {
                    property var childSchema: modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: item ? item.implicitHeight : 0
                    active: root.lowerType() === "structure"
                    source: Qt.resolvedUrl("FieldEditor.qml")
                    function applyChildSchema() {
                        if (!item)
                            return
                        item.theme = root.theme
                        item.width = width
                        item.schema = childSchema
                    }
                    onLoaded: applyChildSchema()
                    onChildSchemaChanged: applyChildSchema()
                    onWidthChanged: applyChildSchema()
                    function getValue() { return item && item["getValue"] ? item["getValue"]() : null }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.lowerType() === "choice"
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Label { text: schema ? ((schema.name || "choice") + " 分支") : "分支"; color: theme ? theme.text : "#333"; font.bold: true }
                ComboBox {
                    id: choiceCombo
                    Layout.fillWidth: true
                    model: schema && schema.children ? schema.children : []
                    textRole: "name"
                    valueRole: "choiceValue"
                    onModelChanged: if (root.controlsReady) refreshTimer.restart()
                }
            }
            Loader {
                id: choiceLoader
                Layout.fillWidth: true
                Layout.preferredHeight: item ? item.implicitHeight : 0
                active: root.lowerType() === "choice" && choiceCombo.currentIndex >= 0
                source: Qt.resolvedUrl("FieldEditor.qml")
                onLoaded: root.refreshChoiceEditor()
                onWidthChanged: root.refreshChoiceEditor()
                Connections {
                    target: choiceCombo
                    function onCurrentIndexChanged() { root.refreshChoiceEditor() }
                }
            }
        }

        ColumnLayout {
            id: arrayColumn
            Layout.fillWidth: true
            visible: root.lowerType() === "array"
            spacing: 6
            property int arrayCount: 0
            RowLayout {
                Layout.fillWidth: true
                Label { text: schema ? ((schema.name || "array") + " 数组") : ""; color: theme ? theme.text : "#333"; font.bold: true }
                Item { Layout.fillWidth: true }
                Button { text: "+ 添加"; onClicked: arrayColumn.arrayCount += 1 }
                Button { text: "- 删除"; enabled: arrayColumn.arrayCount > 0; onClicked: arrayColumn.arrayCount -= 1 }
            }
            Label {
                Layout.fillWidth: true
                visible: arrayColumn.arrayCount === 0
                text: "暂无元素，可点击“+ 添加”创建一项"
                color: theme ? theme.placeholderText : "#888"
                horizontalAlignment: Text.AlignHCenter
            }
            Repeater {
                id: arrayRepeater
                model: arrayColumn.arrayCount
                Rectangle {
                    id: arrayDelegate
                    Layout.fillWidth: true
                    width: parent ? parent.width : 0
                    implicitHeight: (arrayFieldLoader.item ? arrayFieldLoader.item.implicitHeight : 0) + 8
                    color: theme ? theme.surface : "#fafafa"
                    border.color: theme ? theme.controlBorder : "#ccc"
                    radius: 4
                    function applyElementSchema() {
                        if (!arrayFieldLoader.item)
                            return
                        arrayFieldLoader.item.theme = root.theme
                        arrayFieldLoader.item.width = arrayFieldLoader.width
                        arrayFieldLoader.item.schema = root.firstChildSchema()
                    }
                    Loader {
                        id: arrayFieldLoader
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 4
                        active: root.lowerType() === "array"
                        source: Qt.resolvedUrl("FieldEditor.qml")
                        onLoaded: arrayDelegate.applyElementSchema()
                        onWidthChanged: arrayDelegate.applyElementSchema()
                    }
                    function getValue() { return arrayFieldLoader.item && arrayFieldLoader.item["getValue"] ? arrayFieldLoader.item["getValue"]() : null }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: !root.schema
            text: "选择 OAD/OMD 后会在这里生成字段树"
            color: theme ? theme.placeholderText : "#888"
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
