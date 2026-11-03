import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml/Next" as Next
import "DarkPaletteBaseline.js" as Baseline

Item {
    width: 600; height: 400
    Next.GlassTheme { id: theme }
    Next.GlassPage {
        id: page
        theme: theme
        width: 300; height: 200
        Next.GlassTextField { id: field; text: "中文 ABC 0123" }
    }
    TestCase {
        name: "GlobalAppearance"
        when: windowShown
        function init() {
            failOnWarning(/Binding loop|TypeError|ReferenceError|Unable to assign/)
            theme.lightMode = false; theme.nativeBackdropAvailable = false
            theme.reducedTransparency = false; theme.systemReducedTransparency = false
        }
        function test_darkPaletteUnchanged_data() { return Baseline.snapshots }
        function test_darkPaletteUnchanged(data) {
            theme.nativeBackdropAvailable = data.native
            theme.reducedTransparency = data.opaque
            for (let cycle = 0; cycle < 2; ++cycle) {
                theme.lightMode = true; theme.lightMode = false
                for (const key of Object.keys(data.palette))
                    verify(Qt.colorEqual(theme[key],data.palette[key]),key + " differs from the original dark palette")
            }
        }
        function luminance(color) {
            function linear(value) { return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055,2.4) }
            return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b)
        }
        function contrast(a,b) { const x = luminance(a), y = luminance(b); return (Math.max(x,y)+0.05)/(Math.min(x,y)+0.05) }
        function test_lightReadabilityAndSharedControls() {
            theme.lightMode = true
            for (const key of ["text","subText","accent","success","warning","error"])
                verify(contrast(theme[key],theme.background) >= 4.5,key + " needs readable contrast")
            verify(contrast(theme.terminalText,theme.terminalBackground) >= 4.5)
            for (const color of ["#ffffff","#60a5fa","#34d399","#fbbf24","#000000"])
                verify(contrast(theme.markerTextColor(color),theme.background) >= 4.5,"light marker text " + color)
            verify(Qt.colorEqual(field.color,theme.text))
            theme.lightMode = false
            verify(Qt.colorEqual(field.color,theme.text))
        }
        function test_lightGlassAndBothOpaqueFallbacks() {
            theme.lightMode = true; theme.nativeBackdropAvailable = true
            verify(theme.nativeGlass); verify(theme.surface.a >= 0.95); verify(theme.surface.a < 1)
            verify(theme.sidebar.a > 0 && theme.sidebar.a < 0.8)
            verify(Qt.colorEqual(theme.controlBackground,"white"))
            compare(theme.surface.r,1); compare(theme.surface.g,1); compare(theme.surface.b,1)
            compare(theme.sidebar.r,1); compare(theme.sidebar.g,1); compare(theme.sidebar.b,1)
            theme.reducedTransparency = true
            verify(!theme.nativeGlass); compare(theme.surface.a,1); compare(theme.sidebar.a,1); compare(theme.controlBackground.a,1)
            theme.reducedTransparency = false; theme.systemReducedTransparency = true
            verify(!theme.nativeGlass); compare(theme.surface.a,1); compare(theme.sidebar.a,1)
        }
    }
}
