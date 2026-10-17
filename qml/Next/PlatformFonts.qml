pragma Singleton

import QtQuick

QtObject {
    readonly property string monospaceFamily: Qt.platform.os === "windows"
                                               ? "Consolas"
                                               : (Qt.platform.os === "osx" ? "Menlo" : "monospace")
}
