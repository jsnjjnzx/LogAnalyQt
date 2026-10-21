#include "SshLogStream.h"
#include "SshTerminal.h"
#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QTextStream>
#include <stdexcept>
#include <QTemporaryDir>
#include <QSignalSpy>

struct SshTerminalTestAccess {
  static void inputSchedulingChecks();
  static void publish(SshTerminal &terminal, std::vector<SshLogLine> lines, const SshLogLine &partial, SshMonitorHit hit = {}) {
    {
      QMutexLocker lock(&terminal.m_buffer->mutex);
      for (auto &line : lines) {
        terminal.m_buffer->bytes += line.storageBytes();
        terminal.m_buffer->lines.push_back(std::move(line));
      }
      terminal.m_buffer->partial = partial;
      if (!hit.data.isEmpty()) { hit.revision = terminal.m_buffer->monitorRevision; terminal.m_buffer->monitorHit = hit; }
    }
    terminal.flushPendingData();
  }
};

static int checks = 0;
static void check(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

static QString flattened(const std::vector<SshLogLine> &lines) {
  QString result;
  for (const auto &line : lines) { result += line.text; if (!line.continues) result += '\n'; }
  return result;
}

static void streamChecks() {
  const QByteArray input = QString::fromUtf8("中文😀\r\n\x1b[31mred\x1b[0m\rplain\n\x1b]0;title\x07tail").toUtf8();
  for (int block = 1; block <= 37; ++block) {
    SshLogStream stream;
    QString result;
    for (int offset = 0; offset < input.size(); offset += block)
      result += flattened(stream.feed(QByteArrayView(input).sliced(offset, qMin(qsizetype(block), input.size() - offset))));
    result += flattened(stream.finish());
    check(result == QString::fromUtf8("中文😀\nplain\ntail\n"), "UTF-8/ANSI/CRLF and carriage-return redraw survive arbitrary read boundaries");
  }
  const auto chinese = QString::fromUtf8("中文监控");
  const auto gb = QByteArray::fromHex("d6d0cec4bce0bfd8");
  check(SshLogStream::encode(chinese, "GB2312") == gb, "GB commands encode independently of system locale");
  SshLogStream gbStream("GB2312");
  check(gbStream.encodingAvailable(), "platform GB codec is available");
  QString decoded;
  for (const auto c : gb) decoded += flattened(gbStream.feed(QByteArrayView(&c, 1)));
  decoded += flattened(gbStream.finish());
  check(decoded == chinese + '\n', "GB lead bytes survive split reads");
  SshLogStream invalid;
  invalid.feed(QByteArray::fromHex("e4b8"));
  check(flattened(invalid.finish()).contains(QChar::ReplacementCharacter), "dangling UTF-8 is explicit at EOF");
  SshLogStream terminal;
  check(terminal.feed("test$ abc").empty() && terminal.partialLine() == "test$ abc" && terminal.cursorColumn() == 9,
        "prompt and echo are available without a newline");
  terminal.feed("\b \b");
  check(terminal.partialLine() == "test$ ab " && terminal.cursorColumn() == 8, "remote backspace redraw moves the cursor and erases the old glyph");
  terminal.feed("\rtest$ history\x1b[K");
  check(terminal.partialLine() == "test$ history" && terminal.cursorColumn() == 13, "CR and clear-to-end redraw remote history in place");
  check(flattened(terminal.feed("\r\n")) == "test$ history\n", "CRLF commits a remote line exactly once");
  terminal.feed(QString::fromUtf8("test$ 中文").toUtf8());
  check(terminal.cursorColumn() == 10, "Chinese echo occupies two terminal cells per character");
  terminal.feed("\b\b  \b\b");
  check(terminal.partialLine() == QString::fromUtf8("test$ 中  ") && terminal.cursorColumn() == 8, "remote deletion preserves preceding Chinese characters");
  terminal.feed("\r\x1b[2Ktest$ \x1b[2DXY");
  check(terminal.partialLine() == "testXY" && terminal.cursorColumn() == 6, "CSI cursor movement edits the current remote line");
  SshLogStream reset;
  reset.feed("old-part"); reset.reset("UTF-8", true);
  const auto afterClear = reset.feed("new-tail\nnew\n");
  check(flattened(afterClear) == "new-tail\nnew\n" && afterClear.front().continuation && !afterClear.back().continuation,
        "clear marks new tail as a fragment without silently discarding newly received bytes");
  SshLogStream colourReset;
  colourReset.feed("\x1b[31mold\n\x1b[0m");
  check(!colourReset.hasPendingInput(), "a complete colour reset after newline is not a partial log record");
  colourReset.reset("UTF-8", colourReset.hasPendingInput());
  const auto freshColourLine = colourReset.feed("68 04 07 00 00 00\n");
  check(!freshColourLine.front().continuation, "clearing after a colour reset keeps the next full record parseable");
  SshLogStream bom;
  bom.feed(QByteArray::fromHex("efbb")); check(bom.hasPendingInput(), "split BOM remains pending");
  bom.feed(QByteArray::fromHex("bf")); check(!bom.hasPendingInput(), "complete BOM is not a partial record");
  SshLogStream longStream;
  const auto longLine = QString(8191, 'x') + QString::fromUtf8("😀") + QString(25000, 'y');
  auto segments = longStream.feed((longLine + '\n').toUtf8());
  check(segments.size() > 1 && flattened(segments) == longLine + '\n', "long lines preserve exact logical content");
  for (const auto &segment : segments) {
    check(segment.text.size() <= SshLogStream::SegmentCharacters, "segment size is bounded");
    check(!segment.text.isEmpty() && !segment.text.front().isLowSurrogate() && !segment.text.back().isHighSurrogate(),
          "display segment does not split a surrogate pair");
  }
  TerminalLineModel model(100);
  model.appendRecords(std::move(segments));
  check(model.logicalLineAt(1) == longLine && model.completeLineAt(1), "clicking a continued row retrieves full logical line");
  check(model.segmentOffset(1) == 8191, "selection uses the segment offset");
  check(model.bufferText() == longLine + '\n', "copy history introduces no artificial newlines");
  check(model.textRange(model.lineId(0), 8189, model.lineId(1), 3) == longLine.mid(8189, 5),
        "drag selection across display segments adds no artificial newline");
  SshLogStream unbounded;
  TerminalLineModel bounded(100);
  for (int i = 0; i < 1000; ++i) bounded.appendRecords(unbounded.feed(QByteArray(16384, 'a')));
  check(unbounded.partialLine().size() <= SshLogStream::SegmentCharacters, "no-newline tail remains bounded");
  check(bounded.rowCount() == 100 && bounded.byteSize() <= SshLogStream::HistoryBytes, "no-newline history remains bounded");
  check(!bounded.completeLineAt(0), "evicted/incomplete long line cannot masquerade as complete");
}

static void modelChecks() {
  TerminalLineModel model(5000);
  QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
  int resets = 0;
  QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
  QStringList lines;
  for (int i = 0; i < 5500; ++i) lines.append(QString::number(i));
  model.appendLines(lines);
  check(model.rowCount() == 5000 && model.lineAt(0) == "500", "large append retains newest rows");
  const auto lastId = model.lineId(4999);
  check(model.firstLineId() == 500 && model.indexForId(lastId) == 4999, "stable IDs account for trimmed incoming rows");
  model.appendLines({"5500", "5501"});
  check(model.indexForId(lastId) == 4997 && model.lineAt(4997) == "5499", "eviction preserves identity");
  model.setMaxLines(100);
  check(model.rowCount() == 100 && model.lineAt(0) == "5402", "configured capacity affects existing history");
  check(resets == 0, "normal append and eviction do not reset the model");
  model.clear(); check(resets == 1 && model.rowCount() == 0 && model.indexForId(lastId) < 0, "clear invalidates old IDs");
  model.appendLines({"new"}); check(model.lineId(0) > lastId, "clear never reuses an old identity");
  const auto selectedId = model.lineId(0);
  model.appendLines({QString::fromUtf8("中文😀abcdef"), "last"});
  check(model.textRange(selectedId + 1, 0, selectedId + 1, 4) == QString::fromUtf8("中文😀"),
        "selected text preserves Unicode and UTF-16 cursor offsets");
  check(model.textRange(selectedId + 2, 2, selectedId, 1) == QString::fromUtf8("ew\n中文😀abcdef\nla"),
        "reverse multiline selection preserves real line boundaries");
  check(model.textRange(lastId, 0, selectedId, 1).isEmpty(), "evicted selections never copy unrelated rows");
  check(model.textRange(selectedId + 2, 1, selectedId + 3, 3, "live prompt") == "ast\nliv",
        "selection can include the unfinished live line");
  check(model.textRange(selectedId, -20, selectedId, 200) == "new", "selection offsets are bounded");
  TerminalLineModel byteBounded(200000);
  byteBounded.appendLines({"older-than-discarded-data"});
  QStringList large;
  for (int i = 0; i < 4096; ++i) large.append(QString(8192, 'b'));
  byteBounded.appendLines(large);
  check(byteBounded.byteSize() <= SshLogStream::HistoryBytes && byteBounded.rowCount() < large.size(),
        "byte budget applies even when row limit is large");
  check(byteBounded.lineAt(0) != "older-than-discarded-data" &&
        byteBounded.indexForId(byteBounded.firstLineId() + byteBounded.rowCount()) == -1,
        "byte-trimmed batches keep only the newest contiguous IDs");
}

static void monitorChecks() {
  const QVariantList rules{
      QVariantMap{{"id", 1}, {"name", "errors"}, {"pattern", "ERROR"}, {"ignoreCase", false}, {"color", "#f79a93"}},
      QVariantMap{{"id", 2}, {"name", "device"}, {"pattern", QString::fromUtf8("设备.*超时")}, {"ignoreCase", true}, {"color", "#80b9ff"}}};
  check(SshMonitorMatcher::validRules(rules), "valid rule library compiles with colors");
  check(!SshMonitorMatcher::test("(", false, "sample").value("valid").toBool(), "invalid expressions cannot be enabled");
  check(!SshMonitorMatcher::test("", false, "sample").value("valid").toBool(), "empty expressions are rejected");
  const auto preview = SshMonitorMatcher::test("error", true, "ERROR\nerror");
  check(preview.value("matches").toList().size() == 2, "test preview matches case-insensitively per line");
  check(SshMonitorMatcher::test("foo\\nbar", false, "foo\nbar").value("matches").toList().isEmpty(), "test preview does not invent cross-line matches");
  check(!SshMonitorMatcher::test("(*NO_JIT)(a+)+$", false, QString(100, 'a') + '!').value("error").toString().isEmpty(),
        "pathological backtracking returns a bounded error");
  SshLogStream stream;
  SshMonitorMatcher matcher;
  stream.feed("old ERROR");
  matcher.configure(rules, 1, stream.logicalId(), stream.partialLine(), stream.logicalOffset());
  auto feed = [&](const QByteArray &bytes) {
    auto lines = stream.feed(bytes);
    SshLogLine partial{stream.partialLine(), stream.logicalId(), stream.continuation(), stream.hasPendingInput()};
    matcher.feed(lines, partial);
  };
  feed(" unchanged");
  check(matcher.hit().data.isEmpty(), "arming never rematches existing partial output");
  feed("\nnormal\nnew ERROR\nafter\n");
  check(matcher.hit().data.value("ruleId").toInt() == 1 && matcher.hit().data.value("text").toString() == "new ERROR"
            && matcher.hit().data.value("color").toString() == "#f79a93", "first matching new line retains its rule and color");
  const auto oldHit = matcher.hit().logicalId;
  feed("ERROR again\n");
  check(matcher.hit().logicalId == oldHit, "a latched match is not replaced while paused");
  SshMonitorMatcher greedy;
  const QVariantList greedyRules{QVariantMap{{"id", 1}, {"name", "greedy"}, {"pattern", "ERROR.*"}, {"ignoreCase", false}}};
  greedy.configure(greedyRules, 4, 1, "ERROR old", 0);
  std::vector<SshLogLine> greedyLines;
  SshLogLine greedyPartial{"ERROR old appended after resume", 1};
  greedy.feed(greedyLines, greedyPartial);
  check(greedy.hit().data.isEmpty(), "a greedy old match cannot re-trigger when its partial line grows after resume");
  greedyLines = {{"ERROR old appended after resume", 1, false, false}, {"ERROR new line", 2, false, false}};
  greedyPartial = {"", 3}; greedy.feed(greedyLines, greedyPartial);
  check(greedy.hit().logicalId == 2, "a greedy rule resumes on newly starting log content");
  matcher.configure(rules, 2, stream.logicalId(), stream.partialLine(), stream.logicalOffset());
  feed("normal\n"); check(matcher.hit().data.isEmpty(), "resume ignores all previous logs");
  const auto payload = QString::fromUtf8("设备 12 超时").toUtf8();
  for (const char byte : payload) feed(QByteArray(1, byte));
  check(matcher.hit().data.value("ruleId").toInt() == 2 && matcher.hit().data.value("color").toString() == "#80b9ff",
        "fragmented UTF-8 and unfinished output match another selected rule");
  SshLogStream longStream;
  SshMonitorMatcher longMatcher;
  longMatcher.configure(rules, 3, longStream.logicalId(), {}, 0);
  auto lines = longStream.feed(QByteArray(8190, 'x') + "ERROR\n");
  SshLogLine longPartial{longStream.partialLine(), longStream.logicalId()};
  longMatcher.feed(lines, longPartial);
  check(longMatcher.hit().offset == 8190, "expressions cross display segment boundaries without data loss");
  TerminalLineModel model(100); model.appendRecords(lines);
  check(model.idForLogicalOffset(longMatcher.hit().logicalId, longMatcher.hit().offset) == model.lineId(0), "match offsets locate the original display row");
  QTemporaryDir temporary;
  SshTerminal terminal(std::make_shared<SecureCredentialStore>(SecureCredentialStore::Domain::Ssh), temporary.path());
  terminal.setUseSystemCredentials(false);
  QSignalSpy hits(&terminal, &SshTerminal::monitorMatched);
  bool hitBeforeRows = false;
  QObject::connect(&terminal, &SshTerminal::lineCountChanged, [&] { hitBeforeRows = !hits.isEmpty(); });
  check(terminal.setMonitorRules(rules, true), "terminal accepts compiled rules");
  SshTerminalTestAccess::publish(terminal, lines, {}, longMatcher.hit());
  check(hits.size() == 1 && hitBeforeRows, "matching is delivered before auto-follow's row notification");
  check(hits.at(0).at(0).toMap().value("lineId").toLongLong() == terminal.lineIdAt(0), "worker match maps to a stable view identity");
  terminal.setMonitorRules(rules, false);
  SshTerminalTestAccess::publish(terminal, {{"receiving while paused", 99, false, false}}, {});
  check(terminal.getBufferText().contains("receiving while paused") && hits.size() == 1, "pausing detection never pauses reception or commits old hits again");
  QVariantList mixedRules = rules;
  mixedRules.prepend(QVariantMap{{"id", 3}, {"name", "mark only"}, {"pattern", "WARN|ERROR"}, {"ignoreCase", false}, {"color", "#7ed6a0"}, {"pauseOnMatch", false}});
  SshLogStream markingStream; SshMonitorMatcher marking;
  marking.configure(mixedRules, 8, markingStream.logicalId(), {}, 0);
  auto markingLines = markingStream.feed("WARN first\nWARN second\n");
  SshLogLine markingPartial{markingStream.partialLine(), markingStream.logicalId()};
  marking.feed(markingLines, markingPartial);
  check(!marking.hit().data.value("pauseOnMatch", true).toBool() && markingLines[0].monitorColor == "#7ed6a0"
            && markingLines[1].monitorColor == "#7ed6a0", "mark-only rules color every matching row without latching");
  auto moreMarks = markingStream.feed("WARN third\nERROR must pause\n");
  markingPartial = {markingStream.partialLine(), markingStream.logicalId()}; marking.feed(moreMarks, markingPartial);
  check(marking.hit().data.value("pauseOnMatch").toBool() && marking.hit().data.value("ruleId").toInt() == 1
            && moreMarks[1].monitorColor == "#f79a93", "a pause rule takes precedence over mark-only matches on the same log");
  model.appendRecords(markingLines);
  check(model.data(model.index(model.rowCount() - 2), TerminalLineModel::MonitorColorRole).toString() == "#7ed6a0",
        "mark-only row colors persist in history independently of the latest notification");

  auto matchRule = rules[0].toMap();
  matchRule["highlightWholeLine"] = false; matchRule["pauseOnMatch"] = false; matchRule["ignoreCase"] = true;
  check(SshMonitorMatcher::validRules({matchRule}), "match-only highlighting is a valid optional rule setting");
  auto invalidScope = matchRule; invalidScope["highlightWholeLine"] = "false";
  check(!SshMonitorMatcher::validRules({invalidScope}), "non-boolean highlight scope is rejected");
  SshMonitorMatcher spans;
  spans.configure({matchRule}, 9, 0, {}, 0);
  std::vector<SshLogLine> spanLines{{QString::fromUtf8("设备😀 error / ERROR end"), 1}};
  SshLogLine spanPartial{"prefix ERROR tail ERROR", 2};
  spans.feed(spanLines, spanPartial);
  check(!spanLines[0].monitorWholeLine && spanLines[0].monitorRanges == QList<QPair<int, int>>{{5, 5}, {13, 5}},
        "match-only coloring retains all matches with Unicode UTF-16 offsets");
  check(spanPartial.monitorRanges == QList<QPair<int, int>>{{7, 5}, {18, 5}}
            && !spans.hit().data.value("highlightWholeLine").toBool(), "unfinished output and hit notifications keep match-only scope");
  TerminalLineModel spanModel(100); spanModel.appendRecords(spanLines);
  check(!spanModel.data(spanModel.index(0), TerminalLineModel::MonitorWholeLineRole).toBool()
            && spanModel.data(spanModel.index(0), TerminalLineModel::MonitorRangesRole).toList().size() == 2,
        "history exposes match ranges without changing original text");
  SshTerminalTestAccess::publish(terminal, {}, spanPartial);
  check(!terminal.liveMonitorWholeLine() && terminal.liveMonitorRanges() == spanPartial.matchRanges(),
        "live match ranges survive worker-to-GUI handoff");
  terminal.clearBuffer();
  check(terminal.liveMonitorWholeLine() && terminal.liveMonitorRanges().isEmpty(), "clear removes live match formatting");
  matchRule["pattern"] = "(?=ERROR)";
  spans.configure({matchRule}, 10, 0, {}, 0);
  spanLines = {{"prefix ERROR", 1}}; spanPartial = {};
  spans.feed(spanLines, spanPartial);
  check(!spanLines[0].monitorWholeLine && spanLines[0].monitorRanges.isEmpty(),
        "zero-width matches trigger rules without coloring unrelated characters");
  matchRule["pattern"] = "ERROR";
  spans.configure({matchRule}, 11, 0, {}, 0);
  spanLines = {{QString(8190, 'x') + "ER", 1, false, true}, {"ROR tail", 1, true, false}};
  spans.feed(spanLines, spanPartial);
  check(spanLines[1].matchRanges() == QVariantList{QVariantMap{{"start", 0}, {"length", 3}}}, "cross-segment matches use local display offsets");
  TerminalLineModel segmented(100);
  QAbstractItemModelTester segmentedTester(&segmented, QAbstractItemModelTester::FailureReportingMode::Fatal);
  segmented.appendRecords(spanLines);
  check(segmented.data(segmented.index(0), TerminalLineModel::MonitorRangesRole).toList()
            == QVariantList{QVariantMap{{"start", 8190}, {"length", 2}}}, "a cross-segment match colors its earlier display segment too");
  terminal.clearBuffer();
  SshTerminalTestAccess::publish(terminal, {spanLines[0]}, {});
  SshTerminalTestAccess::publish(terminal, {}, spanLines[1]);
  check(terminal.lineModel()->data(terminal.lineModel()->index(0), TerminalLineModel::MonitorRangesRole).toList()
            == QVariantList{QVariantMap{{"start", 8190}, {"length", 2}}} && terminal.liveMonitorRanges().size() == 1,
        "a live match updates an earlier segment delivered in a previous GUI batch");
  terminal.clearBuffer();
  std::vector<SshLogLine> history;
  for (int i = 0; i < 100; ++i) history.push_back({QString(8192, 'x'), 99, i > 0, true});
  SshTerminalTestAccess::publish(terminal, std::move(history), {});
  SshLogLine densePartial{"tail", 99, true, true};
  densePartial.monitorColor = "#7ed6a0"; densePartial.monitorWholeLine = false; densePartial.monitorOffset = 100 * 8192;
  densePartial.monitorRanges.reserve(65536);
  for (int i = 0; i < 65536; ++i) densePartial.monitorRanges.append({2 * i, 1});
  QSignalSpy countChanges(&terminal, &SshTerminal::lineCountChanged);
  SshTerminalTestAccess::publish(terminal, {}, densePartial);
  check(terminal.lineModel()->byteSize() <= SshLogStream::HistoryBytes && terminal.currentLineCount() < 100 && countChanges.size() == 1,
        "retroactive match metadata respects the history byte budget and notifies partial-only eviction");
}

void SshTerminalTestAccess::inputSchedulingChecks() {
  QTemporaryDir temporary;
  SshTerminal terminal(std::make_shared<SecureCredentialStore>(SecureCredentialStore::Domain::Ssh), temporary.path());
  terminal.setUseSystemCredentials(false);
  auto *timer = terminal.m_flushTimer;
  check(timer->interval() == 33, "normal foreground batches use a 33 ms interval");
  terminal.m_connected = true;
  QSignalSpy sent(&terminal, &SshTerminal::sigSendCommand);
  for (bool command : {false, true}) {
    const QString echo = command ? "command echo" : "return echo";
    {
      QMutexLocker lock(&terminal.m_buffer->mutex);
      terminal.m_buffer->partial = {echo, 1, false, true};
      terminal.m_buffer->notificationPending = true;
    }
    timer->start(180);
    QSignalSpy shown(&terminal, &SshTerminal::liveLineChanged);
    if (command) terminal.sendCommand("echo test");
    else terminal.sendRawBytes("\r");
    check(timer->interval() == 16 && terminal.m_lastInput.isValid(), "raw Return and quick commands advance an already pending batch");
    check(shown.wait(1000) && terminal.liveLine() == echo, "pending echo is committed without another worker notification");
  }
  check(sent.size() == 2 && sent[0][0].toString() == "\r" && !sent[0][1].toBool()
        && sent[1][0].toString() == QString(QChar(0x15)) + "echo test" && sent[1][1].toBool(),
        "input scheduling preserves raw Return and command clearing/newline semantics");
  timer->start(1); terminal.sendRawBytes("x");
  check(timer->interval() == 1, "input never postpones an earlier backlog deadline");
  terminal.setPresentationActive(false);
  terminal.sendCommand("background");
  check(timer->interval() == 500, "background commands retain the low-frequency display budget");
  terminal.setPresentationActive(true); timer->stop();
  terminal.sendRawBytes("x");
  check(!timer->isActive(), "idle input waits for actual data rather than manufacturing local echo");
  timer->start(33);
  const auto count = sent.size();
  terminal.m_buffer->receptionPauseRequested.store(true);
  terminal.sendRawBytes("\r"); terminal.sendCommand("blocked");
  check(sent.size() == count && timer->interval() == 33, "stopped reception neither sends nor accelerates input");
  terminal.m_buffer->receptionPauseRequested.store(false); terminal.m_connected = false;
  terminal.sendRawBytes("\r"); terminal.sendCommand("blocked");
  check(sent.size() == count && timer->interval() == 33, "disconnected input neither sends nor accelerates display");
  timer->stop();
}

static void receptionPauseChecks() {
  QTemporaryDir temporary;
  SshTerminal terminal(std::make_shared<SecureCredentialStore>(SecureCredentialStore::Domain::Ssh), temporary.path());
  terminal.setUseSystemCredentials(false);
  SshTerminalTestAccess::publish(terminal, {{"RX 680407000000", 1, false, false}}, {"old prompt", 2, false, true});
  auto *model = terminal.lineModel();
  const auto id = terminal.lineIdAt(0);
  QSignalSpy state(&terminal, &SshTerminal::receptionPausedChanged);
  terminal.setReceptionPaused(true);
  check(state.wait(1000) && terminal.receptionPaused(), "worker acknowledges stopped reception without blocking the GUI");
  check(terminal.lineModel() == model && terminal.currentLineCount() == 1 && terminal.liveLine() == "old prompt",
        "stopping reception keeps the actual history model and prompt, not a display snapshot");
  check(terminal.selectedText(id, 3, id, 15) == "680407000000" && terminal.lineIsComplete(0),
        "received logs remain selectable and parseable while reception is stopped");
  check(terminal.findLineIds("RX").size() == 1, "search still uses already received logs");
  terminal.clearBuffer();
  check(terminal.receptionPaused() && terminal.currentLineCount() == 0, "clearing received logs does not resume network reception");
  terminal.setReceptionPaused(false);
  check(state.wait(1000) && !terminal.receptionPaused(), "reception resumes only after an explicit resume request");
  terminal.setReceptionPaused(true); terminal.setReceptionPaused(false); terminal.setReceptionPaused(true);
  check(state.wait(1000) && terminal.receptionPaused(), "rapid stop/resume ignores stale worker acknowledgements");
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    streamChecks(); modelChecks(); monitorChecks(); receptionPauseChecks(); SshTerminalTestAccess::inputSchedulingChecks();
    QTextStream(stdout) << "PASS: " << checks << " SSH stream/model assertions; synthetic payloads stayed in RAM\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n'; return 1;
  }
}
