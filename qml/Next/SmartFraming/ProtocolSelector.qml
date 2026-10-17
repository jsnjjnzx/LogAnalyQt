import LogAnalyQt.Next as Next
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var theme
    property var backend
    property var protocols: []
    property string protocolCode: combo.currentValue || "698"
    property int protocolType: combo.currentIndex >= 0 && protocols.length > combo.currentIndex ? protocols[combo.currentIndex].protocolType : 1
    implicitHeight: 32

    function reload() {
        protocols = backend ? backend.listSmartFramingProtocols() : []
        combo.model = protocols
        if (combo.currentIndex < 0 && protocols.length > 0)
            combo.currentIndex = 0
    }

    function itemTextAt(itemIndex) {
        if (!protocols || itemIndex < 0 || itemIndex >= protocols.length)
            return ""
        var item = protocols[itemIndex]
        if (!item)
            return ""
        return item.name || item.text || ""
    }

    Component.onCompleted: reload()
    onBackendChanged: reload()

    ComboBox {
        id: combo
        anchors.fill: parent
        textRole: "name"
        valueRole: "code"

        indicator: Canvas {
            id: arrowCanvas
            x: combo.width - width - combo.rightPadding
            y: combo.topPadding + (combo.availableHeight - height) / 2
            width: 12
            height: 8
            contextType: "2d"

            Connections {
                target: combo
                function onPressedChanged() { arrowCanvas.requestPaint() }
            }

            onPaint: {
                context.reset()
                context.moveTo(0, 0)
                context.lineTo(width, 0)
                context.lineTo(width / 2, height)
                context.closePath()
                context.fillStyle = root.theme ? root.theme.text : "#333"
                context.fill()
            }
        }

        background: Rectangle { color: root.theme ? root.theme.controlBackground : "#fff"; border.color: root.theme ? root.theme.controlBorder : "#ccc"; radius: 8 }
        contentItem: Text { leftPadding: 8; rightPadding: combo.indicator.width + combo.spacing; text: combo.displayText; color: root.theme ? root.theme.text : "#333"; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        delegate: ItemDelegate {
            width: combo.width
            contentItem: Text { text: root.itemTextAt(index); color: root.theme ? root.theme.text : "#333"; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
            background: Rectangle { color: highlighted ? (root.theme ? root.theme.splitHandleHover : "#eee") : (root.theme ? root.theme.controlBackground : "#fff") }
        }

        popup: Popup {
            y: combo.height - 1
            width: combo.width
            implicitHeight: Math.min(contentItem.implicitHeight, 320)
            padding: 1
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            }
            background: Rectangle {
                color: root.theme ? root.theme.controlBackground : "#fff"
                border.color: root.theme ? root.theme.controlBorder : "#ccc"
                radius: 8
            }
        }
    }
}