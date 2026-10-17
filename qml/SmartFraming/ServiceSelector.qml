import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var theme
    property var backend
    property var services: []
    property var categories: []
    property var filteredServices: []
    property string serviceCode: serviceCombo.currentValue || "GET_NORMAL"
    implicitHeight: row.implicitHeight

    function reload() {
        services = backend ? backend.list698ServiceTypes() : []
        rebuildCategories()
        updateFilteredServices()
    }

    function rebuildCategories() {
        var result = []
        for (var serviceIndex = 0; serviceIndex < services.length; ++serviceIndex) {
            var category = services[serviceIndex].category || services[serviceIndex].group || "服务"
            var exists = false
            for (var categoryIndex = 0; categoryIndex < result.length; ++categoryIndex) {
                if (result[categoryIndex].code === category) {
                    exists = true
                    break
                }
            }
            if (!exists)
                result.push({ code: category, name: category })
        }
        categories = result
        categoryCombo.model = categories
        if (categoryCombo.currentIndex < 0 && categories.length > 0)
            categoryCombo.currentIndex = 0
    }

    function updateFilteredServices() {
        var category = categoryCombo.currentValue || (categories.length > 0 ? categories[0].code : "")
        var result = []
        for (var serviceIndex = 0; serviceIndex < services.length; ++serviceIndex) {
            var service = services[serviceIndex]
            var serviceCategory = service.category || service.group || "服务"
            if (serviceCategory === category)
                result.push(service)
        }
        filteredServices = result
        serviceCombo.model = filteredServices
        if (serviceCombo.currentIndex < 0 && filteredServices.length > 0)
            serviceCombo.currentIndex = 0
    }

    function setService(code) {
        for (var serviceIndex = 0; serviceIndex < services.length; ++serviceIndex) {
            if (services[serviceIndex].code === code) {
                var category = services[serviceIndex].category || services[serviceIndex].group || "服务"
                for (var categoryIndex = 0; categoryIndex < categories.length; ++categoryIndex) {
                    if (categories[categoryIndex].code === category) {
                        categoryCombo.currentIndex = categoryIndex
                        break
                    }
                }
                updateFilteredServices()
                for (var itemIndex = 0; itemIndex < filteredServices.length; ++itemIndex) {
                    if (filteredServices[itemIndex].code === code) {
                        serviceCombo.currentIndex = itemIndex
                        break
                    }
                }
                return
            }
        }
    }

    Component.onCompleted: reload()

    function comboItemAt(listModel, itemIndex) {
        if (!listModel || itemIndex < 0 || itemIndex >= listModel.length)
            return null
        return listModel[itemIndex]
    }

    component StyledSelectorComboBox: ComboBox {
        id: control
        implicitHeight: 32

        indicator: Text {
            x: control.width - width - control.rightPadding - 2
            y: control.topPadding + (control.availableHeight - height) / 2
            text: "▼"
            color: root.theme ? root.theme.text : "#333"
            font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            color: root.theme ? root.theme.controlBackground : "#fff"
            border.color: root.theme ? root.theme.controlBorder : "#ccc"
            radius: 4
        }

        contentItem: Text {
            leftPadding: 8
            rightPadding: control.indicator.width + control.spacing + 8
            text: control.displayText
            color: root.theme ? root.theme.text : "#333"
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        popup: Popup {
            y: control.height - 1
            width: control.width
            implicitHeight: Math.min(contentItem.implicitHeight, 360)
            padding: 1
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            }
            background: Rectangle {
                color: root.theme ? root.theme.controlBackground : "#fff"
                border.color: root.theme ? root.theme.controlBorder : "#ccc"
                radius: 4
            }
        }
    }

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 8

        StyledSelectorComboBox {
            id: categoryCombo
            Layout.preferredWidth: 120
            textRole: "name"
            valueRole: "code"
            onCurrentIndexChanged: root.updateFilteredServices()
            delegate: ItemDelegate {
                width: categoryCombo.width
                contentItem: Text {
                    text: (root.comboItemAt(root.categories, index) || {}).name || ""
                    color: root.theme ? root.theme.text : "#333"
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle { color: highlighted ? (root.theme ? root.theme.splitHandleHover : "#eee") : (root.theme ? root.theme.controlBackground : "#fff") }
            }
        }

        StyledSelectorComboBox {
            id: serviceCombo
            Layout.fillWidth: true
            textRole: "name"
            valueRole: "code"
            delegate: ItemDelegate {
                width: serviceCombo.width
                contentItem: Text {
                    property var serviceItem: root.comboItemAt(root.filteredServices, index) || {}
                    text: (serviceItem.subCategory ? serviceItem.subCategory + " · " : "") + (serviceItem.name || "")
                    color: root.theme ? root.theme.text : "#333"
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle { color: highlighted ? (root.theme ? root.theme.splitHandleHover : "#eee") : (root.theme ? root.theme.controlBackground : "#fff") }
            }
        }
    }
}