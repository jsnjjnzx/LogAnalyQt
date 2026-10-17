import QtQuick
import QtTest
import ".." as Preview
import "../PreviewData.js" as Data

TestCase {
    id: suite
    name: "LogHomePreview"
    when: windowShown
    Preview.PreviewMain { id: app }

    function init() {
        app.width = 1440;
        app.height = 800;
        app.loadSample(false);
        wait(30);
    }
    function test_searchWrapsAndHonorsFilter() {
        app.searchText = "[RX]";
        app.selectLine(17);
        app.find(false);
        compare(app.selectedLine, 2);
        app.find(true);
        compare(app.selectedLine, 17);
        app.applyFilter("10:24:20");
        app.find(false);
        compare(app.selectedLine, 11);
        app.searchText = "missing";
        app.find(false);
        compare(app.status, "未找到匹配内容");
    }
    function test_invalidRegexPreservesView() {
        app.applyFilter("\\[RX\\]");
        compare(app.shownLines.length, 6);
        app.applyFilter("[");
        verify(app.filterError.length > 0);
        compare(app.shownLines.length, 6);
        app.applyFilter("");
        compare(app.filterError, "");
        compare(app.shownLines.length, 19);
    }
    function test_filteredSelectionClearsResult() {
        app.selectLine(2);
        verify(app.resultRows.length > 1);
        app.applyFilter("连接已建立");
        compare(app.selectedLine, -1);
        compare(app.bytes.length, 0);
        compare(app.resultRows.length, 0);
    }
    function test_fieldSelectionAndExpansion() {
        app.selectField(app.resultRows[6]);
        compare(app.byteOffset, 6);
        compare(app.byteLength, 4);
        compare(app.bytes.slice(6,10).join(" "), "20 00 02 00");
        var count = app.resultRows.length;
        app.toggleGroup("control");
        compare(app.resultRows.length, count - 1);
        app.toggleGroup("control");
        compare(app.resultRows.length, count);
        app.selectLine(0);
        compare(app.resultRows.length, 0);
        compare(app.byteOffset, -1);
    }
    function test_pasteDoesNotInventParsedFields() {
        app.loadPasted("<script>bad</script>\r\n68 01 02 03 16");
        compare(app.isSample, false);
        compare(app.lines.length, 2);
        app.selectLine(1);
        compare(app.resultRows.length, 1);
        compare(app.resultRows[0].description, "尚未接入解析引擎");
        verify(Data.richLine(app.lines[0], "", -1, 0).indexOf("<script>") === -1);
    }
    function test_emptyAndReset() {
        app.loadSample(true);
        compare(app.shownLines.length, 0);
        compare(app.resultRows.length, 0);
        app.loadSample(false);
        compare(app.shownLines.length, 19);
        compare(app.selectedLine, 2);
    }
    function test_visualControls() {
        var overview = findChild(app, "overviewButton");
        verify(overview);
        mouseClick(overview);
        compare(app.overview, true);
        compare(app.overviewRows.length, 12);
        mouseClick(overview);
        compare(app.overview, false);
        var open = findChild(app, "openLogButton");
        mouseClick(open);
        var dialog = findChild(app, "samplesDialog");
        tryCompare(dialog, "opened", true);
        dialog.close();
    }
    function test_keyboardSearch() {
        var input = findChild(app, "searchInput");
        app.requestActivate();
        mouseClick(input);
        input.forceActiveFocus();
        tryCompare(input, "activeFocus", true);
        input.text = "周期";
        keyClick(Qt.Key_Return);
        compare(app.selectedLine, 12);
    }
    function test_smallWindowKeepsPanels() {
        app.width = 1100;
        app.height = 680;
        wait(50);
        var left = findChild(app, "logPanel");
        var right = findChild(app, "resultPanel");
        verify(left.width >= 340);
        verify(right.width >= 370);
        var rightEdge = right.mapToItem(app.contentItem, right.width, 0);
        verify(rightEdge.x < app.width);
        verify(left.height > 300);
    }
    function test_splitterDrag() {
        var panels = findChild(app, "panels");
        var left = findChild(app, "logPanel");
        var before = left.width;
        mouseDrag(panels, before + 7, panels.height / 2, -65, 0);
        verify(left.width < before - 30);
    }
    function test_richTextRetainsSpacingAndEscapesInput() {
        var rich = Data.richLine("<img>  [RX]  68 01 02 03", "[RX]", -1, 0);
        verify(rich.indexOf("&lt;img&gt;") >= 0);
        verify(rich.indexOf("&nbsp;&nbsp;") >= 0);
        compare(Data.hex("no hex here").length, 0);
        compare(Data.hex("[RX] 68\t01 02 03").length, 4);
    }
}
