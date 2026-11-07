#pragma once

#include <QByteArray>
#include <QString>
#include <QStringDecoder>
#include <QVariantList>
#include <numeric>
#include <utility>
#include <vector>
#ifndef Q_OS_WIN
#include <iconv.h>
#endif

struct SshMonitorMark {
  int ruleId = 0;
  QString ruleName;
  QString color;
  bool wholeLine = true;
  qsizetype offset = 0;
  QList<QPair<int, int>> ranges;

  qsizetype storageBytes() const {
    return (ruleName.capacity() + color.capacity()) * qsizetype(sizeof(QChar))
        + ranges.capacity() * qsizetype(sizeof(QPair<int, int>));
  }

  QVariantList visibleRanges(const QString &text) const {
    QVariantList result;
    for (const auto &range : ranges) {
      const auto start = qMax(offset, qsizetype(range.first));
      const auto end = qMin(offset + text.size(), qsizetype(range.first) + range.second);
      if (end > start) result.append(QVariantMap{{"start", start - offset}, {"length", end - start}});
    }
    return result;
  }
};

struct SshColorStyle {
  quint32 foreground = 0, background = 0;
  bool operator==(const SshColorStyle &other) const {
    return foreground == other.foreground && background == other.background;
  }
  bool operator!=(const SshColorStyle &other) const { return !(*this == other); }
};

struct SshColorRun {
  int start, length;
  SshColorStyle style;
  bool operator==(const SshColorRun &other) const {
    return start == other.start && length == other.length && style == other.style;
  }
};

struct SshLogLine {
  QString text;
  quint64 logicalId = 0;
  bool continuation = false;
  bool continues = false;
  QString monitorColor;
  QString monitorRuleName;
  bool monitorWholeLine = true;
  qsizetype monitorOffset = 0;
  QList<QPair<int, int>> monitorRanges;
  QList<SshMonitorMark> monitorMarks;
  QList<SshColorRun> colors;

  QVariantList colorRuns() const {
    QVariantList result;
    result.reserve(colors.size());
    for (const auto &run : colors)
      result.append(QVariantMap{{"start", run.start}, {"length", run.length},
                                {"fg", run.style.foreground}, {"bg", run.style.background}});
    return result;
  }

  QVariantList matchRanges() const {
    QVariantList result;
    for (const auto &range : monitorRanges) {
      const auto start = qMax(monitorOffset, qsizetype(range.first));
      const auto end = qMin(monitorOffset + text.size(), qsizetype(range.first) + range.second);
      if (end > start) result.append(QVariantMap{{"start", start - monitorOffset}, {"length", end - start}});
    }
    return result;
  }

  QVariantList monitorMarkList() const {
    QVariantList result;
    for (const auto &mark : monitorMarks) {
      QVariantMap value{{"ruleId", mark.ruleId}, {"ruleName", mark.ruleName},
                        {"color", mark.color}, {"wholeLine", mark.wholeLine}};
      if (!mark.wholeLine) value["ranges"] = mark.visibleRanges(text);
      result.append(value);
    }
    return result;
  }

  void setMonitorMarks(QList<SshMonitorMark> marks) {
    monitorMarks = std::move(marks);
    monitorColor.clear();
    monitorRuleName.clear();
    monitorWholeLine = true;
    monitorOffset = 0;
    monitorRanges.clear();
    if (monitorMarks.isEmpty()) return;
    const auto *summary = &monitorMarks.back();
    for (const auto &mark : monitorMarks)
      if (mark.wholeLine) summary = &mark;
    monitorColor = summary->color;
    monitorRuleName = summary->ruleName;
    monitorWholeLine = summary->wholeLine;
    monitorOffset = summary->offset;
    monitorRanges = summary->ranges;
  }

  qsizetype storageBytes() const { return sizeof(SshLogLine) + (text.capacity() + monitorColor.capacity() + monitorRuleName.capacity()) * qsizetype(sizeof(QChar))
      + monitorRanges.capacity() * qsizetype(sizeof(QPair<int, int>))
      + monitorMarks.capacity() * qsizetype(sizeof(SshMonitorMark))
      + colors.capacity() * qsizetype(sizeof(SshColorRun))
      + std::accumulate(monitorMarks.cbegin(), monitorMarks.cend(), qsizetype(0),
          [](qsizetype total, const SshMonitorMark &mark) { return total + mark.storageBytes(); }); }
};

// Worker-owned stream state; a socket read is not a character, escape sequence or line boundary.
class SshLogStream {
public:
  static constexpr int TerminalColumns = 80;
  static constexpr qsizetype SegmentCharacters = 8192;
  static constexpr qsizetype PendingBytes = 4 * 1024 * 1024;
  static constexpr qsizetype HistoryBytes = 32 * 1024 * 1024;

  explicit SshLogStream(const QString &encoding = QStringLiteral("UTF-8"));
  ~SshLogStream();
  SshLogStream(const SshLogStream &) = delete;
  SshLogStream &operator=(const SshLogStream &) = delete;

  void reset(const QString &encoding, bool clearedPartialLine = false, bool preserveColors = false);
  std::vector<SshLogLine> feed(QByteArrayView bytes);
  std::vector<SshLogLine> finish();
  const QString &partialLine() const { return m_partial; }
  QList<SshColorRun> colorRuns() const;
  int cursorColumn() const { return m_column; }
  int cursorPosition() const;
  int cursorPadding() const;
  quint64 logicalId() const { return m_logicalId; }
  qsizetype logicalOffset() const { return m_logicalOffset; }
  bool continuation() const { return m_continuation; }
  bool hasPendingInput() const {
    return !m_partial.isEmpty() || m_continuation || m_state != State::Text ||
           m_pendingUtf8 > 0 || !m_pendingBytes.isEmpty();
  }
  static QByteArray encode(const QString &text, const QString &encoding);
  bool encodingAvailable() const;

private:
  enum class State { Text, Escape, EscapeIntermediate, Csi, Osc, String, StringEscape };
  QString decode(QByteArrayView bytes, bool final = false);
  void consume(QStringView text, std::vector<SshLogLine> &lines);
  void append(QStringView text, std::vector<SshLogLine> &lines);
  void emitLine(std::vector<SshLogLine> &lines, bool continues);
  void splitLine(qsizetype split, std::vector<SshLogLine> &lines);
  void replace(qsizetype start, qsizetype length, QStringView text, SshColorStyle style);
  void applyCsi(QChar final);
  void applySgr();
  void syncCursorRow();
  void moveCursor(int column);
  void moveCursorRows(int rows);

  QStringDecoder m_utf8{QStringConverter::Utf8};
  bool m_gb = false;
#ifndef Q_OS_WIN
  iconv_t m_converter = nullptr;
#endif
  QByteArray m_pendingBytes;
  QString m_partial;
  QList<SshColorStyle> m_styles;
  SshColorStyle m_style;
  int m_basicForeground = -1;
  bool m_bright = false;
  quint64 m_logicalId = 0;
  qsizetype m_logicalOffset = 0;
  State m_state = State::Text;
  State m_stringState = State::Osc;
  QString m_csi;
  bool m_csiOverflow = false;
  int m_column = 0;
  int m_lineColumns = 0;
  int m_rowStart = 0; // May be negative when a display segment starts partway through a PTY row.
  bool m_continuation = false;
  int m_pendingUtf8 = 0;
};
