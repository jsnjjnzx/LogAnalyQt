import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.ComboBox {
    id: control
    implicitHeight: 36
    leftPadding: 12
    rightPadding: 12
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    indicator: Canvas {
        x: control.mirrored ? 12 : control.width - width - 12
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 14
        height: 14
        contextType: "2d"
        property color strokeColor: control.palette.text
        onStrokeColorChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            if (!ctx) return
            ctx.reset()
            ctx.strokeStyle = strokeColor
            ctx.lineWidth = 1.6
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.moveTo(3, 5)
            ctx.lineTo(7, 9)
            ctx.lineTo(11, 5)
            ctx.stroke()
        }
    }
    background: ControlSurface {
        implicitWidth: 120
        implicitHeight: 36
        inset: true
        baseColor: control.palette.base
        edgeColor: control.palette.mid
        focusColor: control.palette.highlight
        focused: control.visualFocus
        hovered: control.hovered
        pressed: control.down
    }
}
