import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SpinBox {
    id: control
    implicitWidth: Math.max(100, numberMetrics.width + leftPadding + rightPadding + 8)
    implicitHeight: 34
    Layout.minimumWidth: implicitWidth
    leftPadding: 24
    rightPadding: 24
    font.pixelSize: 13
    opacity: enabled ? 1 : 0.45
    TextMetrics {
        id: numberMetrics
        font: control.font
        text: control.textFromValue(Math.abs(control.from) > Math.abs(control.to) ? control.from : control.to, control.locale)
    }
    background: ControlSurface { inset: true; baseColor: control.palette.base; edgeColor: control.palette.mid; focused: control.activeFocus; focusColor: control.palette.highlight }
    contentItem: TextInput {
        text: control.textFromValue(control.value, control.locale)
        color: control.palette.text
        selectionColor: control.palette.highlight
        selectedTextColor: control.palette.highlightedText
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        validator: control.validator
        readOnly: !control.editable
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        onEditingFinished: control.value = control.valueFromText(text, control.locale)
    }
    up.indicator: ControlSurface {
        x: control.width - width; width: 23; height: control.height; radius: 6
        opacity: control.wrap || (control.from <= control.to ? control.value < control.to : control.value > control.to) ? 1 : 0.35
        baseColor: control.up.pressed ? control.palette.highlight : control.palette.button
        edgeColor: control.palette.mid; pressed: control.up.pressed
        Text { anchors.centerIn: parent; text: "+"; color: control.palette.buttonText; font.pixelSize: 17 }
    }
    down.indicator: ControlSurface {
        width: 23; height: control.height; radius: 6
        opacity: control.wrap || (control.from <= control.to ? control.value > control.from : control.value < control.from) ? 1 : 0.35
        baseColor: control.down.pressed ? control.palette.highlight : control.palette.button
        edgeColor: control.palette.mid; pressed: control.down.pressed
        Text { anchors.centerIn: parent; text: "−"; color: control.palette.buttonText; font.pixelSize: 17 }
    }
}
