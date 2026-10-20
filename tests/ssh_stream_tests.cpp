#include "SshLogStream.h"
#include "SshTerminal.h"
#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QTextStream>
#include <stdexcept>

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
    check(result == QString::fromUtf8("中文😀\nred\nplain\ntail\n"), "UTF-8/ANSI/CRLF survive arbitrary read boundaries");
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

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    streamChecks(); modelChecks();
    QTextStream(stdout) << "PASS: " << checks << " SSH stream/model assertions; synthetic payloads stayed in RAM\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n'; return 1;
  }
}
