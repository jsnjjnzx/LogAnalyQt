import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LogAnalyQt

Item {
    id: root
    property var theme
    property var backend
    property string serviceCode: "GET_NORMAL"
    property var schema: backend ? backend.get698ServiceSchema(serviceCode) : null
    signal statusMessage(string message, bool isError)
    implicitHeight: mainColumn.implicitHeight

    onServiceCodeChanged: {
        schema = backend ? backend.get698ServiceSchema(serviceCode) : null
        Qt.callLater(reset)
    }

    Component.onCompleted: reset()

    function reset() {
        getListModel.clear()
        setListModel.clear()
        actionListModel.clear()
        proxyServerModel.clear()
        getResponseListModel.clear()
        getResponseNextNormalModel.clear()
        getResponseNextRecordModel.clear()
        setResponseListModel.clear()
        actionResponseListModel.clear()
        proxyResponseServerModel.clear()
        getListModel.append({})
        setListModel.append({})
        actionListModel.append({})
        proxyServerModel.append({})
        getResponseListModel.append({})
        getResponseNextNormalModel.append({})
        getResponseNextRecordModel.append({})
        setResponseListModel.append({})
        actionResponseListModel.append({})
        proxyResponseServerModel.append({})
    }

    function usesAcd() {
        return serviceCode === "GET_RESPONSE_NORMAL"
            || serviceCode === "GET_RESPONSE_LIST"
            || serviceCode === "GET_RESPONSE_RECORD"
            || serviceCode === "GET_RESPONSE_NEXT"
            || serviceCode === "ACTION_RESPONSE_NORMAL"
            || serviceCode === "ACTION_RESPONSE_LIST"
            || serviceCode === "PROXY_GET_RESPONSE_LIST"
    }

    function piidValue() {
        return { seqNo: Number(piidSpin.value), highPriority: highPriorityCheck.checked, acd: acdCheck.checked }
    }

    function timeTagValue() {
        var dateParts = timeDate.text.split("-")
        var timeParts = timeClock.text.split(":")
        return {
            hasTimeTag: timeTagCheck.checked,
            year: Number(dateParts[0] || 0),
            month: Number(dateParts[1] || 0),
            day: Number(dateParts[2] || 0),
            hour: Number(timeParts[0] || 0),
            minute: Number(timeParts[1] || 0),
            second: Number(timeParts[2] || 0),
            tiUnit: tiUnitCombo.currentIndex,
            tiValue: Number(tiValueSpin.value)
        }
    }

    function collectRepeaterValues(repeater) {
        var values = []
        for (var itemIndex = 0; itemIndex < repeater.count; ++itemIndex) {
            var item = repeater.itemAt(itemIndex)
            if (item && item.getValue)
                values.push(item.getValue())
        }
        return values
    }

    function getTreeValue() {
        var value = { piid: piidValue(), timeTag: timeTagValue() }
        if (serviceCode === "GET_NORMAL") {
            value.oad = getNormalOad.getValue()
        } else if (serviceCode === "GET_LIST") {
            value.oads = collectRepeaterValues(getListRepeater)
        } else if (serviceCode === "GET_RECORD") {
            value.oad = getRecordOad.getValue()
            value.rsd = rsdEditor.getValue()
            value.rcsd = rcsdEditor.getValue()
        } else if (serviceCode === "GET_NEXT") {
            value.lastBlockNo = Number(lastBlockSpin.value)
        } else if (serviceCode === "SET_NORMAL") {
            value.setItem = setNormalItem.getValue()
        } else if (serviceCode === "SET_LIST") {
            value.setItems = collectRepeaterValues(setListRepeater)
        } else if (serviceCode === "ACTION_NORMAL") {
            value.actionItem = actionNormalItem.getValue()
        } else if (serviceCode === "ACTION_LIST") {
            value.actionItems = collectRepeaterValues(actionListRepeater)
        } else if (serviceCode === "PROXY_GET_LIST") {
            value.totalTimeout = Number(proxyTotalTimeout.value)
            value.servers = collectRepeaterValues(proxyServerRepeater)
        } else if (serviceCode === "GET_RESPONSE_NORMAL") {
            value.result = getResponseNormalItem.getValue()
        } else if (serviceCode === "GET_RESPONSE_LIST") {
            value.results = collectRepeaterValues(getResponseListRepeater)
        } else if (serviceCode === "GET_RESPONSE_RECORD") {
            value.recordResult = getResponseRecordItem.getValue()
        } else if (serviceCode === "GET_RESPONSE_NEXT") {
            value.nextResult = {
                end: getResponseNextEnd.checked,
                blockNo: Number(getResponseNextBlockNo.value),
                choice: getResponseNextChoice.currentIndex,
                dar: Number(getResponseNextDar.value),
                normalResults: collectRepeaterValues(getResponseNextNormalRepeater),
                recordResults: collectRepeaterValues(getResponseNextRecordRepeater)
            }
        } else if (serviceCode === "SET_RESPONSE_NORMAL") {
            value.setResult = setResponseNormalItem.getValue()
        } else if (serviceCode === "SET_RESPONSE_LIST") {
            value.setResults = collectRepeaterValues(setResponseListRepeater)
        } else if (serviceCode === "ACTION_RESPONSE_NORMAL") {
            value.actionResult = actionResponseNormalItem.getValue()
        } else if (serviceCode === "ACTION_RESPONSE_LIST") {
            value.actionResults = collectRepeaterValues(actionResponseListRepeater)
        } else if (serviceCode === "PROXY_GET_RESPONSE_LIST") {
            value.servers = collectRepeaterValues(proxyResponseServerRepeater)
        }
        return value
    }

    component SmallButton: Next.GlassButton {
        id: button
        implicitHeight: 28
        contentItem: Text { text: button.text; color: theme ? theme.buttonText : "#333"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
    }

    component TreeSection: Rectangle {
        id: section
        property string title: ""
        property string subtitle: ""
        property bool expanded: true
        default property alias content: sectionBody.data
        Layout.fillWidth: true
        implicitHeight: header.height + (expanded ? sectionBody.implicitHeight + 10 : 0)
        color: theme ? theme.surface : "#fafafa"
        border.color: theme ? theme.controlBorder : "#cccccc"
        radius: 8

        Rectangle {
            id: header
            width: parent.width
            height: 34
            color: headerMouse.containsMouse ? (theme ? theme.surfaceHover : "#eeeeee") : "transparent"
            radius: 8
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                Text { text: section.expanded ? "▼" : "▶"; color: theme ? theme.subText : "#666"; font.pixelSize: 12 }
                Label { text: section.title; color: theme ? theme.text : "#333"; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                Label { text: section.subtitle; color: theme ? theme.subText : "#666"; font.pixelSize: 11; elide: Text.ElideRight }
            }
            MouseArea { id: headerMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: section.expanded = !section.expanded }
        }

        ColumnLayout {
            id: sectionBody
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 8
            visible: section.expanded
            spacing: 8
        }
    }

    component DataItemEditor: Rectangle {
        id: itemEditor
        property bool methodMode: false
        property string label: methodMode ? "方法项" : "写入项"
        property var dataSchema: null
        property int itemNo: 1
        signal removeRequested()
        Layout.fillWidth: true
        implicitHeight: itemColumn.implicitHeight + 12
        color: theme ? theme.controlBackground : "#ffffff"
        border.color: theme ? theme.controlBorder : "#cccccc"
        radius: 8

        function loadSchema() {
            if (!backend)
                return
            var selected = selector.getValue()
            var idValue = methodMode ? selected.methodId : selected.attrId
            if (!selected.oi || selected.oi.length !== 4) {
                root.statusMessage("请先填写 4 位 OI", true)
                return
            }
            var loaded = backend.getOadSchema(selected.oi, idValue, methodMode, methodMode ? 0 : selected.index)
            if (!loaded || Object.keys(loaded).length === 0) {
                dataSchema = null
                root.statusMessage("找不到 " + selected.oi + "-" + idValue + " 的 XML 定义", true)
                return
            }
            dataSchema = loaded
            root.statusMessage("已加载字段树：" + (loaded.name || loaded.type || "Data"), false)
        }

        function getValue() {
            if (!dataSchema)
                loadSchema()
            if (methodMode)
                return { omd: selector.getValue(), dataValue: fieldEditor.getValue() }
            return { oad: selector.getValue(), dataValue: fieldEditor.getValue() }
        }

        ColumnLayout {
            id: itemColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Label { text: itemEditor.label + " " + itemEditor.itemNo; color: theme ? theme.text : "#333"; font.bold: true }
                Item { Layout.fillWidth: true }
                SmallButton { text: "加载字段"; onClicked: itemEditor.loadSchema() }
                SmallButton { text: "删除"; visible: itemEditor.itemNo > 0; onClicked: itemEditor.removeRequested() }
            }

            Next.OadSelector {
                id: selector
                Layout.fillWidth: true
                theme: root.theme
                backend: root.backend
                methodMode: itemEditor.methodMode
                opFilter: itemEditor.methodMode ? "ACTION" : "SET"
                onSelectionChanged: itemEditor.dataSchema = null
            }

            Next.FieldEditor {
                id: fieldEditor
                Layout.fillWidth: true
                theme: root.theme
                schema: itemEditor.dataSchema
            }
        }
    }

    component OadRowEditor: Rectangle {
        id: oadRow
        property int itemNo: 1
        signal removeRequested()
        Layout.fillWidth: true
        implicitHeight: oadSelector.implicitHeight + 12
        color: theme ? theme.controlBackground : "#ffffff"
        border.color: theme ? theme.controlBorder : "#cccccc"
        radius: 8
        function getValue() { return oadSelector.getValue() }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 8
            Label { text: "#" + itemNo; color: theme ? theme.subText : "#666"; Layout.preferredWidth: 34 }
            Next.OadSelector { id: oadSelector; Layout.fillWidth: true; theme: root.theme; backend: root.backend; methodMode: false; opFilter: "GET" }
            SmallButton { text: "删除"; onClicked: oadRow.removeRequested() }
        }
    }

    component ProxyServerEditor: Rectangle {
        id: serverEditor
        property int itemNo: 1
        signal removeRequested()
        Layout.fillWidth: true
        implicitHeight: serverColumn.implicitHeight + 12
        color: theme ? theme.controlBackground : "#ffffff"
        border.color: theme ? theme.controlBorder : "#cccccc"
        radius: 8
        ListModel { id: proxyOadModel }
        Component.onCompleted: proxyOadModel.append({})

        function getValue() {
            var oads = []
            for (var oadIndex = 0; oadIndex < proxyOadRepeater.count; ++oadIndex) {
                var row = proxyOadRepeater.itemAt(oadIndex)
                if (row && row.getValue)
                    oads.push(row.getValue())
            }
            return { tsaHex: tsaField.text.replace(/\s/g, ""), timeout: Number(serverTimeout.value), oads: oads }
        }

        ColumnLayout {
            id: serverColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label { text: "目标服务器 " + itemNo; color: theme ? theme.text : "#333"; font.bold: true }
                Item { Layout.fillWidth: true }
                SmallButton { text: "删除服务器"; onClicked: serverEditor.removeRequested() }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "TSA"; color: theme ? theme.subText : "#666"; Layout.preferredWidth: 42 }
                Next.GlassTextField { id: tsaField; Layout.fillWidth: true; placeholderText: "目标服务器地址内容 hex（不含 0x55 Tag）"; color: theme ? theme.text : "#333"; placeholderTextColor: theme ? theme.placeholderText : "#888"; font.family: Next.PlatformFonts.monospaceFamily;  }
                Label { text: "超时"; color: theme ? theme.subText : "#666" }
                Next.CompactSpinBox { id: serverTimeout; from: 0; to: 65535; value: 5; editable: true; Layout.preferredWidth: 86 }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "OAD 列表"; color: theme ? theme.subText : "#666" }
                Item { Layout.fillWidth: true }
                SmallButton { text: "+ 添加 OAD"; onClicked: proxyOadModel.append({}) }
            }
            Repeater { id: proxyOadRepeater; model: proxyOadModel; OadRowEditor { itemNo: index + 1; onRemoveRequested: proxyOadModel.remove(index) } }
        }
    }

    component ResultNormalEditor: Rectangle {
        id: resultEditor
        property int itemNo: 1
        property string label: "读取结果"
        property var dataSchema: null
        signal removeRequested()
        Layout.fillWidth: true
        implicitHeight: resultColumn.implicitHeight + 12
        color: theme ? theme.controlBackground : "#ffffff"
        border.color: theme ? theme.controlBorder : "#cccccc"
        radius: 8

        function loadSchema() {
            if (!backend)
                return
            var selected = selector.getValue()
            if (!selected.oi || selected.oi.length !== 4) {
                root.statusMessage("请先填写 4 位 OI", true)
                return
            }
            var loaded = backend.getOadSchema(selected.oi, selected.attrId, false, selected.index)
            if (!loaded || Object.keys(loaded).length === 0) {
                dataSchema = null
                root.statusMessage("找不到 " + selected.oi + "-" + selected.attrId + " 的 XML 定义", true)
                return
            }
            dataSchema = loaded
            root.statusMessage("已加载字段树：" + (loaded.name || loaded.type || "Data"), false)
        }

        function getValue() {
            if (resultChoice.currentIndex === 0 && !dataSchema)
                loadSchema()
            return {
                oad: selector.getValue(),
                choice: resultChoice.currentIndex === 0 ? 1 : 0,
                dar: Number(darSpin.value),
                dataValue: fieldEditor.getValue()
            }
        }

        ColumnLayout {
            id: resultColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Label { text: resultEditor.label + " " + resultEditor.itemNo; color: theme ? theme.text : "#333"; font.bold: true }
                Item { Layout.fillWidth: true }
                SmallButton { text: "加载字段"; visible: resultChoice.currentIndex === 0; onClicked: resultEditor.loadSchema() }
                SmallButton { text: "删除"; visible: resultEditor.itemNo > 0; onClicked: resultEditor.removeRequested() }
            }

            Next.OadSelector {
                id: selector
                Layout.fillWidth: true
                theme: root.theme
                backend: root.backend
                methodMode: false
                opFilter: "GET"
                onSelectionChanged: resultEditor.dataSchema = null
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: "结果"; color: theme ? theme.subText : "#666" }
                Next.GlassComboBox { id: resultChoice; model: ["Data", "DAR"]; Layout.preferredWidth: 120; onCurrentIndexChanged: resultEditor.dataSchema = null }
                Label { text: "DAR"; visible: resultChoice.currentIndex === 1; color: theme ? theme.subText : "#666" }
                Next.CompactSpinBox { id: darSpin; visible: resultChoice.currentIndex === 1; from: 0; to: 255; value: 0; editable: true; Layout.preferredWidth: 92 }
                Item { Layout.fillWidth: true }
            }

            Next.FieldEditor {
                id: fieldEditor
                visible: resultChoice.currentIndex === 0
                Layout.fillWidth: true
                theme: root.theme
                schema: resultEditor.dataSchema
            }
        }
    }

    component RecordResultEditor: Rectangle {
        id: recordEditor
        property int itemNo: 1
        property string label: "记录结果"
        signal removeRequested()
        Layout.fillWidth: true
        implicitHeight: recordColumn.implicitHeight + 12
        color: theme ? theme.controlBackground : "#ffffff"
        border.color: theme ? theme.controlBorder : "#cccccc"
        radius: 8

        function getValue() {
            return {
                oad: selector.getValue(),
                rcsd: rcsdEditor.getValue(),
                choice: recordChoice.currentIndex,
                dar: Number(recordDar.value),
                rowCount: Number(rowCountSpin.value),
                rowsHex: rowsHexArea.text.replace(/\s/g, "")
            }
        }

        ColumnLayout {
            id: recordColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Label { text: recordEditor.label + " " + recordEditor.itemNo; color: theme ? theme.text : "#333"; font.bold: true }
                Item { Layout.fillWidth: true }
                SmallButton { text: "删除"; visible: recordEditor.itemNo > 0; onClicked: recordEditor.removeRequested() }
            }

            Next.OadSelector { id: selector; Layout.fillWidth: true; theme: root.theme; backend: root.backend; methodMode: false; opFilter: "GET" }
            Next.RcsdEditor { id: rcsdEditor; Layout.fillWidth: true; theme: root.theme; backend: root.backend }

            RowLayout {
                Layout.fillWidth: true
                Label { text: "结果"; color: theme ? theme.subText : "#666" }
                Next.GlassComboBox { id: recordChoice; model: ["DAR", "记录行数据"]; Layout.preferredWidth: 140 }
                Label { text: "DAR"; visible: recordChoice.currentIndex === 0; color: theme ? theme.subText : "#666" }
                Next.CompactSpinBox { id: recordDar; visible: recordChoice.currentIndex === 0; from: 0; to: 255; value: 0; editable: true; Layout.preferredWidth: 92 }
                Label { text: "行数"; visible: recordChoice.currentIndex === 1; color: theme ? theme.subText : "#666" }
                Next.CompactSpinBox { id: rowCountSpin; visible: recordChoice.currentIndex === 1; from: 0; to: 255; value: 1; editable: true; Layout.preferredWidth: 92 }
                Item { Layout.fillWidth: true }
            }

            TextArea {
                id: rowsHexArea
                visible: recordChoice.currentIndex === 1
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                placeholderText: "按 RCSD 列顺序填写每行 Data 编码 hex"
                color: theme ? theme.text : "#333"
                placeholderTextColor: theme ? theme.placeholderText : "#888"
                font.family: Next.PlatformFonts.monospaceFamily
                wrapMode: TextArea.Wrap
                background: Rectangle { color: theme ? theme.background : "#fff"; border.color: theme ? theme.controlBorder : "#ccc"; radius: 8 }
            }
        }
    }

    component SetResultEditor: Rectangle {
        id: setResultEditor
        property int itemNo: 1
        signal removeRequested()
        Layout.fillWidth: true
        implicitHeight: setResultRow.implicitHeight + 12
        color: theme ? theme.controlBackground : "#ffffff"
        border.color: theme ? theme.controlBorder : "#cccccc"
        radius: 8
        function getValue() { return { oad: selector.getValue(), dar: Number(darSpin.value) } }
        RowLayout {
            id: setResultRow
            anchors.fill: parent
            anchors.margins: 6
            spacing: 8
            Label { text: "#" + itemNo; color: theme ? theme.subText : "#666"; Layout.preferredWidth: 34 }
            Next.OadSelector { id: selector; Layout.fillWidth: true; theme: root.theme; backend: root.backend; methodMode: false; opFilter: "SET" }
            Label { text: "DAR"; color: theme ? theme.subText : "#666" }
            Next.CompactSpinBox { id: darSpin; from: 0; to: 255; value: 0; editable: true; Layout.preferredWidth: 92 }
            SmallButton { text: "删除"; visible: setResultEditor.itemNo > 0; onClicked: setResultEditor.removeRequested() }
        }
    }

    component ActionResultEditor: Rectangle {
        id: actionResultEditor
        objectName: "action-result-editor"
        property int itemNo: 1
        property var dataSchema: null
        signal removeRequested()
        Layout.fillWidth: true
        implicitHeight: actionResultColumn.implicitHeight + 12
        color: theme ? theme.controlBackground : "#ffffff"
        border.color: theme ? theme.controlBorder : "#cccccc"
        radius: 8

        function loadSchema() {
            dataSchema = { name: "返回 Data", optional: false, type: returnType.currentText === "enum" ? "unsigned" : returnType.currentText,
                unit: "", scaler: 0, children: [] }
        }

        function getValue() {
            var result = { omd: selector.getValue(), dar: Number(darSpin.value), hasData: hasDataCheck.checked }
            if (!hasDataCheck.checked)
                return result
            if (!dataSchema)
                loadSchema()
            var value = fieldEditor.getValue()
            if (returnType.currentText === "long64" || returnType.currentText === "long64_unsigned")
                value = exactInteger.text
            if (returnType.currentText === "array" || returnType.currentText === "structure") {
                try { value = JSON.parse(compoundValue.text) }
                catch (error) { root.statusMessage("返回 Data 数组不是有效 JSON", true); value = null }
            }
            result.dataValue = { type: returnType.currentText, value: value }
            return result
        }

        ColumnLayout {
            id: actionResultColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Label { text: "方法结果 " + actionResultEditor.itemNo; color: theme ? theme.text : "#333"; font.bold: true }
                Item { Layout.fillWidth: true }
                SmallButton { text: "删除"; visible: actionResultEditor.itemNo > 0; onClicked: actionResultEditor.removeRequested() }
            }

            Next.OadSelector {
                id: selector
                Layout.fillWidth: true
                theme: root.theme
                backend: root.backend
                methodMode: true
                opFilter: "ACTION"
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: "DAR"; color: theme ? theme.subText : "#666" }
                Next.CompactSpinBox { id: darSpin; from: 0; to: 255; value: 0; editable: true; Layout.preferredWidth: 92 }
                Next.GlassCheckBox { id: hasDataCheck; objectName: "action-result-has-data"; text: "携带返回 Data"; palette.windowText: theme ? theme.text : "#333"; onCheckedChanged: if (checked) actionResultEditor.loadSchema() }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                visible: hasDataCheck.checked
                Layout.fillWidth: true
                Label { text: "返回类型"; color: theme ? theme.subText : "#666" }
                Next.GlassComboBox {
                    id: returnType
                    objectName: "action-result-return-type"
                    Layout.fillWidth: true
                    model: ["unsigned", "integer", "long", "long_unsigned", "double_long", "double_long_unsigned",
                        "long64", "long64_unsigned", "enum", "bool", "null", "float32", "float64", "bit_string",
                        "octet_string", "visible_string", "utf8_string", "date_time_s", "date_time", "date", "time",
                        "oi", "oad", "omd", "ti", "tsa", "scaler_unit", "mac", "rn", "checksum", "array", "structure",
                        "comdcb", "csd", "ms", "rcsd", "region", "sid", "sid_mac", "rsd", "vqds"]
                    onCurrentTextChanged: actionResultEditor.loadSchema()
                }
            }

            Next.GlassTextField {
                id: exactInteger
                objectName: "action-result-return-64"
                visible: hasDataCheck.checked && (returnType.currentText === "long64" || returnType.currentText === "long64_unsigned")
                Layout.fillWidth: true
                text: "0"
                placeholderText: "64 位十进制整数"
            }

            TextArea {
                id: compoundValue
                objectName: "action-result-return-json"
                visible: hasDataCheck.checked && (returnType.currentText === "array" || returnType.currentText === "structure")
                Layout.fillWidth: true
                text: "[]"
                placeholderText: '[{"type":"unsigned","value":1}]'
                wrapMode: TextEdit.Wrap
            }

            Next.FieldEditor {
                id: fieldEditor
                objectName: "action-result-field-editor"
                visible: hasDataCheck.checked && ["long64", "long64_unsigned", "array", "structure"].indexOf(returnType.currentText) < 0
                Layout.fillWidth: true
                theme: root.theme
                schema: actionResultEditor.dataSchema
            }
        }
    }

    component ProxyResponseServerEditor: Rectangle {
        id: proxyResponseEditor
        property int itemNo: 1
        signal removeRequested()
        Layout.fillWidth: true
        implicitHeight: proxyResponseColumn.implicitHeight + 12
        color: theme ? theme.controlBackground : "#ffffff"
        border.color: theme ? theme.controlBorder : "#cccccc"
        radius: 8
        ListModel { id: proxyResultModel }
        Component.onCompleted: proxyResultModel.append({})

        function getValue() {
            return { tsaHex: tsaField.text.replace(/\s/g, ""), results: root.collectRepeaterValues(proxyResultRepeater) }
        }

        ColumnLayout {
            id: proxyResponseColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label { text: "目标服务器结果 " + itemNo; color: theme ? theme.text : "#333"; font.bold: true }
                Item { Layout.fillWidth: true }
                SmallButton { text: "删除服务器"; onClicked: proxyResponseEditor.removeRequested() }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "TSA"; color: theme ? theme.subText : "#666"; Layout.preferredWidth: 42 }
                Next.GlassTextField { id: tsaField; Layout.fillWidth: true; placeholderText: "目标服务器地址内容 hex（不含 0x55 Tag）"; color: theme ? theme.text : "#333"; placeholderTextColor: theme ? theme.placeholderText : "#888"; font.family: Next.PlatformFonts.monospaceFamily;  }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "读取结果列表"; color: theme ? theme.subText : "#666" }
                Item { Layout.fillWidth: true }
                SmallButton { text: "+ 添加结果"; onClicked: proxyResultModel.append({}) }
            }
            Repeater { id: proxyResultRepeater; model: proxyResultModel; ResultNormalEditor { itemNo: index + 1; label: "读取结果"; onRemoveRequested: proxyResultModel.remove(index) } }
        }
    }

    ListModel { id: getListModel }
    ListModel { id: setListModel }
    ListModel { id: actionListModel }
    ListModel { id: proxyServerModel }
    ListModel { id: getResponseListModel }
    ListModel { id: getResponseNextNormalModel }
    ListModel { id: getResponseNextRecordModel }
    ListModel { id: setResponseListModel }
    ListModel { id: actionResponseListModel }
    ListModel { id: proxyResponseServerModel }

    ColumnLayout {
        id: mainColumn
        width: parent.width
        spacing: 8

        TreeSection {
            title: "PIID"
            subtitle: "服务序号与优先级"
            RowLayout {
                Layout.fillWidth: true
                Label { text: "服务序号"; color: theme ? theme.subText : "#666" }
                Next.CompactSpinBox { id: piidSpin; from: 0; to: 63; value: backend ? backend.getNextPiid() : 1; editable: true; Layout.preferredWidth: 90 }
                Next.GlassCheckBox { id: highPriorityCheck; text: "高优先级"; palette.windowText: theme ? theme.text : "#333" }
                Next.GlassCheckBox { id: acdCheck; visible: root.usesAcd(); text: "ACD"; palette.windowText: theme ? theme.text : "#333" }
                Item { Layout.fillWidth: true }
            }
        }

        TreeSection {
            title: "GET-NORMAL"
            subtitle: "OAD"
            visible: serviceCode === "GET_NORMAL"
            Next.OadSelector { id: getNormalOad; Layout.fillWidth: true; theme: root.theme; backend: root.backend; methodMode: false; opFilter: "GET" }
        }

        TreeSection {
            title: "GET-LIST"
            subtitle: "OAD 列表"
            visible: serviceCode === "GET_LIST"
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                SmallButton { text: "+ 添加 OAD"; onClicked: getListModel.append({}) }
            }
            Repeater { id: getListRepeater; model: getListModel; OadRowEditor { itemNo: index + 1; onRemoveRequested: getListModel.remove(index) } }
        }

        TreeSection {
            title: "GET-RECORD"
            subtitle: "OAD + RSD + RCSD"
            visible: serviceCode === "GET_RECORD"
            Next.OadSelector { id: getRecordOad; Layout.fillWidth: true; theme: root.theme; backend: root.backend; methodMode: false; opFilter: "GET" }
            Next.RsdEditor { id: rsdEditor; Layout.fillWidth: true; theme: root.theme; backend: root.backend }
            Next.RcsdEditor { id: rcsdEditor; Layout.fillWidth: true; theme: root.theme; backend: root.backend }
        }

        TreeSection {
            title: "GET-NEXT"
            subtitle: "上一块序号"
            visible: serviceCode === "GET_NEXT"
            RowLayout {
                Layout.fillWidth: true
                Label { text: "上一块序号"; color: theme ? theme.subText : "#666" }
                Next.CompactSpinBox { id: lastBlockSpin; from: 0; to: 65535; value: 0; editable: true; Layout.preferredWidth: 120 }
                Item { Layout.fillWidth: true }
            }
        }

        TreeSection {
            title: "SET-NORMAL"
            subtitle: "OAD → Data"
            visible: serviceCode === "SET_NORMAL"
            DataItemEditor { id: setNormalItem; methodMode: false; label: "写入项"; itemNo: 0 }
        }

        TreeSection {
            title: "SET-LIST"
            subtitle: "写入项列表"
            visible: serviceCode === "SET_LIST"
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                SmallButton { text: "+ 添加写入项"; onClicked: setListModel.append({}) }
            }
            Repeater { id: setListRepeater; model: setListModel; DataItemEditor { methodMode: false; label: "写入项"; itemNo: index + 1; onRemoveRequested: setListModel.remove(index) } }
        }

        TreeSection {
            title: "ACTION-NORMAL"
            subtitle: "OMD → MethodParam"
            visible: serviceCode === "ACTION_NORMAL"
            DataItemEditor { id: actionNormalItem; methodMode: true; label: "方法项"; itemNo: 0 }
        }

        TreeSection {
            title: "ACTION-LIST"
            subtitle: "方法项列表"
            visible: serviceCode === "ACTION_LIST"
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                SmallButton { text: "+ 添加方法项"; onClicked: actionListModel.append({}) }
            }
            Repeater { id: actionListRepeater; model: actionListModel; DataItemEditor { methodMode: true; label: "方法项"; itemNo: index + 1; onRemoveRequested: actionListModel.remove(index) } }
        }

        TreeSection {
            title: "PROXY-GET-LIST"
            subtitle: "目标服务器 → OAD 列表"
            visible: serviceCode === "PROXY_GET_LIST"
            RowLayout {
                Layout.fillWidth: true
                Label { text: "总超时"; color: theme ? theme.subText : "#666" }
                Next.CompactSpinBox { id: proxyTotalTimeout; from: 0; to: 65535; value: 10; editable: true; Layout.preferredWidth: 100 }
                Item { Layout.fillWidth: true }
                SmallButton { text: "+ 添加服务器"; onClicked: proxyServerModel.append({}) }
            }
            Repeater { id: proxyServerRepeater; model: proxyServerModel; ProxyServerEditor { itemNo: index + 1; onRemoveRequested: proxyServerModel.remove(index) } }
        }

        TreeSection {
            title: "GET-RESPONSE-NORMAL"
            subtitle: "A-ResultNormal"
            visible: serviceCode === "GET_RESPONSE_NORMAL"
            ResultNormalEditor { id: getResponseNormalItem; itemNo: 0; label: "读取结果" }
        }

        TreeSection {
            title: "GET-RESPONSE-LIST"
            subtitle: "A-ResultNormal 列表"
            visible: serviceCode === "GET_RESPONSE_LIST"
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                SmallButton { text: "+ 添加结果"; onClicked: getResponseListModel.append({}) }
            }
            Repeater { id: getResponseListRepeater; model: getResponseListModel; ResultNormalEditor { itemNo: index + 1; label: "读取结果"; onRemoveRequested: getResponseListModel.remove(index) } }
        }

        TreeSection {
            title: "GET-RESPONSE-RECORD"
            subtitle: "A-ResultRecord"
            visible: serviceCode === "GET_RESPONSE_RECORD"
            RecordResultEditor { id: getResponseRecordItem; itemNo: 0; label: "记录结果" }
        }

        TreeSection {
            title: "GET-RESPONSE-NEXT"
            subtitle: "分帧结果"
            visible: serviceCode === "GET_RESPONSE_NEXT"
            RowLayout {
                Layout.fillWidth: true
                Next.GlassCheckBox { id: getResponseNextEnd; text: "末帧"; checked: true; palette.windowText: theme ? theme.text : "#333" }
                Label { text: "分帧序号"; color: theme ? theme.subText : "#666" }
                Next.CompactSpinBox { id: getResponseNextBlockNo; from: 0; to: 65535; value: 0; editable: true; Layout.preferredWidth: 110 }
                Label { text: "结果"; color: theme ? theme.subText : "#666" }
                Next.GlassComboBox { id: getResponseNextChoice; model: ["DAR", "普通结果列表", "记录结果列表"]; Layout.preferredWidth: 150 }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: getResponseNextChoice.currentIndex === 0
                Label { text: "DAR"; color: theme ? theme.subText : "#666" }
                Next.CompactSpinBox { id: getResponseNextDar; from: 0; to: 255; value: 0; editable: true; Layout.preferredWidth: 92 }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: getResponseNextChoice.currentIndex === 1
                Item { Layout.fillWidth: true }
                SmallButton { text: "+ 添加普通结果"; onClicked: getResponseNextNormalModel.append({}) }
            }
            Repeater { id: getResponseNextNormalRepeater; model: getResponseNextNormalModel; ResultNormalEditor { visible: getResponseNextChoice.currentIndex === 1; itemNo: index + 1; label: "普通结果"; onRemoveRequested: getResponseNextNormalModel.remove(index) } }
            RowLayout {
                Layout.fillWidth: true
                visible: getResponseNextChoice.currentIndex === 2
                Item { Layout.fillWidth: true }
                SmallButton { text: "+ 添加记录结果"; onClicked: getResponseNextRecordModel.append({}) }
            }
            Repeater { id: getResponseNextRecordRepeater; model: getResponseNextRecordModel; RecordResultEditor { visible: getResponseNextChoice.currentIndex === 2; itemNo: index + 1; label: "记录结果"; onRemoveRequested: getResponseNextRecordModel.remove(index) } }
        }

        TreeSection {
            title: "SET-RESPONSE-NORMAL"
            subtitle: "OAD + DAR"
            visible: serviceCode === "SET_RESPONSE_NORMAL"
            SetResultEditor { id: setResponseNormalItem; itemNo: 0 }
        }

        TreeSection {
            title: "SET-RESPONSE-LIST"
            subtitle: "OAD + DAR 列表"
            visible: serviceCode === "SET_RESPONSE_LIST"
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                SmallButton { text: "+ 添加结果"; onClicked: setResponseListModel.append({}) }
            }
            Repeater { id: setResponseListRepeater; model: setResponseListModel; SetResultEditor { itemNo: index + 1; onRemoveRequested: setResponseListModel.remove(index) } }
        }

        TreeSection {
            title: "ACTION-RESPONSE-NORMAL"
            subtitle: "OMD + DAR + 可选 Data"
            visible: serviceCode === "ACTION_RESPONSE_NORMAL"
            ActionResultEditor { id: actionResponseNormalItem; itemNo: 0 }
        }

        TreeSection {
            title: "ACTION-RESPONSE-LIST"
            subtitle: "方法结果列表"
            visible: serviceCode === "ACTION_RESPONSE_LIST"
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                SmallButton { text: "+ 添加结果"; onClicked: actionResponseListModel.append({}) }
            }
            Repeater { id: actionResponseListRepeater; model: actionResponseListModel; ActionResultEditor { itemNo: index + 1; onRemoveRequested: actionResponseListModel.remove(index) } }
        }

        TreeSection {
            title: "PROXY-GET-RESPONSE-LIST"
            subtitle: "目标服务器 → A-ResultNormal 列表"
            visible: serviceCode === "PROXY_GET_RESPONSE_LIST"
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                SmallButton { text: "+ 添加服务器结果"; onClicked: proxyResponseServerModel.append({}) }
            }
            Repeater { id: proxyResponseServerRepeater; model: proxyResponseServerModel; ProxyResponseServerEditor { itemNo: index + 1; onRemoveRequested: proxyResponseServerModel.remove(index) } }
        }

        TreeSection {
            title: "TimeTag"
            subtitle: "可选时间标签"
            expanded: false
            Next.GlassCheckBox { id: timeTagCheck; text: "携带时间标签"; palette.windowText: theme ? theme.text : "#333" }
            GridLayout {
                Layout.fillWidth: true
                columns: 4
                enabled: timeTagCheck.checked
                Label { text: "日期"; color: theme ? theme.subText : "#666" }
                Next.GlassTextField { id: timeDate; text: Qt.formatDate(new Date(), "yyyy-MM-dd"); Layout.fillWidth: true; color: theme ? theme.text : "#333";  }
                Label { text: "时间"; color: theme ? theme.subText : "#666" }
                Next.GlassTextField { id: timeClock; text: Qt.formatTime(new Date(), "HH:mm:ss"); Layout.fillWidth: true; color: theme ? theme.text : "#333";  }
                Label { text: "允许延时"; color: theme ? theme.subText : "#666" }
                Next.CompactSpinBox { id: tiValueSpin; from: 0; to: 65535; value: 10; editable: true; Layout.fillWidth: true }
                Label { text: "单位"; color: theme ? theme.subText : "#666" }
                Next.GlassComboBox { id: tiUnitCombo; model: ["秒", "分", "时", "日", "月", "年"]; Layout.fillWidth: true }
            }
        }
    }
}
