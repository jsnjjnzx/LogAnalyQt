import QtQuick
import QtTest
import LogAnalyQt.Next as Next

Item {
    width: 940; height: 440
    Next.GlassTheme { id: mockTheme }
    QtObject {
        id: mockBackend
        property string lastLink: ""
        property int lastTarget: -1
        property string clipboard: ""
        signal highlightRange(int start, int end, int modelId)
        function handleLink(link, target) { lastLink = link; lastTarget = target; }
        function copyToClipboard(text) { clipboard = text; }
        function parseModel(target) { return fields; }
    }
    ListModel {
        id: fields
        property int lastToggle: -1
        property int lastSubtree: -1
        function getColumnWidths(fontWidth, indentWidth) { return [240, 2000]; }
        function toggleExpand(row) { lastToggle = row; }
        function toggleSubtree(row) { lastSubtree = row; }
    }
    Next.ParseResultTable {
        id: table
        width: 900; height: 400
        backend: mockBackend
        theme: mockTheme
        resultModel: fields
        targetModelId: 7
        namePrefix: "table"
    }
    Component {
        id: popupFactory
        Next.ParseResultWindow { backend: mockBackend; theme: mockTheme; parseSessionId: 7; parsedHex: "AA BB"; namePrefix: "test-popup" }
    }
    TestCase {
        name: "ParseResultPresentation"
        when: windowShown
        function field(row) { return findChild(table, "table-field-" + row); }
        function init() {
            table.width = 900;
            table.differenceMode = false;
            table.columnRatio1 = 0.34; table.columnRatio2 = 0.20;
            fields.clear();
            const common = {name: "父节点", value: "AA", description: "原始完整说明 <&>", descriptionSummary: "主要结果", descriptionDetail: "类型与索引 <&>", indent: 0, offset: 0, length: 1, isFrameSeparator: false, isDiff: false, highlighted: false, hasChildren: true, expanded: true, treeGuides: [], hasParent: false, branchContinues: false};
            fields.append(common);
            const child = Object.assign({}, common, {name: "叶子", value: "BB CC", indent: 1, offset: 1, length: 2, hasChildren: false, hasParent: true});
            fields.append(child);
            table.listView.currentIndex = -1;
            mockBackend.lastLink = ""; mockBackend.clipboard = "";
            fields.lastToggle = -1; fields.lastSubtree = -1;
            tryVerify(() => field(1) !== null);
        }
        function cleanup() {
            findChild(table, "table-row-menu").close();
        }
        function test_compactRowsAndByteLink() {
            compare(field(0).height, 26); compare(field(1).height, 26);
            verify(findChild(field(0), "table-field-0-expander").text.length > 0);
            compare(findChild(field(1), "table-field-1-expander").text, "");
            mouseClick(field(1));
            compare(mockBackend.lastLink, "highlight:1,2"); compare(mockBackend.lastTarget, 7);
            tryCompare(field(1), "height", 48);
        }
        function test_plainSourceAndEscapedStyledDescription() {
            const text = findChild(field(1), "table-field-1-description");
            verify(text.text.indexOf("&lt;&amp;&gt;") >= 0);
            verify(text.text.indexOf("<font") >= 0);
            compare(fields.get(1).description, "原始完整说明 <&>");
            mouseClick(field(1));
            verify(text.text.indexOf("<br>") >= 0);
        }
        function test_diffSelectionDoesNotChangeRowHeight() {
            table.differenceMode = true;
            mouseClick(field(1));
            compare(field(1).height, 26);
            compare(findChild(field(1), "table-field-1-description").maximumLineCount, 1);
        }
        function test_autoFitReservesExplanationSpace() {
            table.autoFit();
            verify(table.columnRatio1 + table.columnRatio2 <= 0.60001);
            verify(table.columnWidth3 >= table.tableWidth * 0.40);
            verify(table.columnRatio2 <= 0.26);
        }
        function test_narrowPaneKeepsUsableColumns() {
            table.width = 420;
            compare(table.tableWidth, 520);
            compare(table.listView.contentWidth, 520);
            verify(table.columnWidth3 >= 230);
        }
        function test_expandUsesParentOnly() {
            mouseClick(findChild(field(0), "table-field-0-expander"));
            compare(fields.lastToggle, 0);
            fields.lastToggle = -1;
            mouseClick(findChild(field(1), "table-field-1-expander"));
            compare(fields.lastToggle, -1);
        }
        function test_contextMenuRetainsCopyAndSubtree() {
            mouseClick(field(0), field(0).width / 2, field(0).height / 2, Qt.RightButton);
            const menu = findChild(table, "table-row-menu");
            tryCompare(menu, "visible", true);
            const action = findChild(table, "table-toggle-subtree");
            verify(action.visible); compare(action.text, "收起整棵子树");
            mouseClick(action); compare(fields.lastSubtree, 0);
            menu.close();
            mouseClick(field(1), field(1).width / 2, field(1).height / 2, Qt.RightButton);
            tryCompare(menu, "visible", true);
            verify(!action.visible);
            compare(menu.rowDescription, "原始完整说明 <&>");
        }
        function test_independentWindowUsesSharedTable() {
            const popup = popupFactory.createObject(null);
            verify(popup !== null);
            const shared = findChild(popup, "test-popup-table");
            verify(shared !== null);
            compare(shared.resultModel, fields);
            compare(shared.targetModelId, 7);
            compare(shared.indentWidth, table.indentWidth);
            popup.destroy();
        }
        function test_frameShowsEightLinesAndScrolls() {
            const popup = popupFactory.createObject(null, {parsedHex: Array(21).join("AA BB\n"), height: 360, visible: true});
            verify(popup !== null);
            const frame = findChild(popup, "test-popup-frame");
            const scroll = findChild(popup, "test-popup-frame-scroll");
            const metrics = Qt.createQmlObject('import QtQuick; FontMetrics {}', popup);
            metrics.font = frame.font;
            tryVerify(() => scroll.height >= Math.ceil(metrics.lineSpacing * 8) + frame.topPadding + frame.bottomPadding);
            tryVerify(() => scroll.contentHeight > scroll.height);
            mockBackend.highlightRange(0, 2, 7);
            compare(frame.selectedText, "AA");
            popup.destroy();
        }
        function test_resetWhileDetailsAreSelected() {
            mouseClick(field(1));
            compare(field(1).height, 48);
            fields.clear();
            tryCompare(table.listView, "count", 0);
            tryCompare(table.listView, "currentIndex", -1);
        }
    }
}
