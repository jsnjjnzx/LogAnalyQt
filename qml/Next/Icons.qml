import QtQuick

pragma Singleton

Item {
    // 基础操作
    readonly property string plus: String.fromCodePoint(0xF0415)
    readonly property string minus: String.fromCodePoint(0xF01D8)
    readonly property string close: String.fromCodePoint(0xF0156)
    readonly property string check: String.fromCodePoint(0xF012C)
    readonly property string alert: String.fromCodePoint(0xF0026)
    readonly property string help: String.fromCodePoint(0xF02D7)
    readonly property string info: String.fromCodePoint(0xF02FC)
    readonly property string settings: String.fromCodePoint(0xF0493)
    readonly property string scriptText: String.fromCodePoint(0xF0BC2)
    readonly property string compareHorizontal: String.fromCodePoint(0xF1492)
    readonly property string vectorDifference: String.fromCodePoint(0xF055A)
    readonly property string sourceMerge: String.fromCodePoint(0xF062D)

    // 文件与文件夹
    readonly property string folder: String.fromCodePoint(0xF024B)
    readonly property string folderOpen: String.fromCodePoint(0xF024C)
    readonly property string file: String.fromCodePoint(0xF0214)
    readonly property string fileDocument: String.fromCodePoint(0xF0219)

    // 播放/解析相关
    readonly property string play: String.fromCodePoint(0xF040A)
    readonly property string stop: String.fromCodePoint(0xF04DB)
    readonly property string pause: String.fromCodePoint(0xF03E4)
    readonly property string refresh: String.fromCodePoint(0xF0450)
    readonly property string broom: String.fromCodePoint(0xF00D2) // 清理

    // 网络与同步
    readonly property string sync: String.fromCodePoint(0xF04E1)
    readonly property string database: String.fromCodePoint(0xF0187)
    readonly property string cloud: String.fromCodePoint(0xF0162)

    // 搜索与过滤
    readonly property string search: String.fromCodePoint(0xF0449)
    readonly property string filter: String.fromCodePoint(0xF0232)

    // 复制粘贴
    readonly property string contentCopy: String.fromCodePoint(0xF018F)
    readonly property string contentPaste: String.fromCodePoint(0xF0192)
}
