import QtQuick
import QtQuick.Controls
import LogAnalyQt.Next as Next

ApplicationWindow {
    id: window
    width: 1280; height: 900; visible: true
    title: "SSH 监控性能验收（隔离测试）"
    Next.GlassTheme { id: testTheme }
    QtObject {
        id: mockBackend
        property var popupResultModel: []
        property int protocolType: 0
        function getUiSetting(key, fallback) { return fallback }
        function setUiSetting(key, value) { return true }
    }
    Next.LiveLogDialog {
        objectName: "benchmark-page"
        anchors.fill: parent; visible: true
        theme: testTheme; ownerWindow: window
        backend: mockBackend
    }
}
