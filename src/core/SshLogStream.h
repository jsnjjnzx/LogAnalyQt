#pragma once

#include <QByteArray>
#include <QString>
#include <QStringDecoder>
#include <QVariantList>
#include <vector>
#ifndef Q_OS_WIN
#include <iconv.h>
#endif

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

  QVariantList matchRanges() const {
    QVariantList result;
    for (const auto &range : monitorRanges) {
      const auto start = qMax(monitorOffset, qsizetype(range.first));
      const auto end = qMin(monitorOffset + text.size(), qsizetype(range.first) + range.second);
      if (end > start) result.append(QVariantMap{{"start", start - monitorOffset}, {"length", end - start}});
    }
    return result;
  }
  qsizetype storageBytes() const { return sizeof(SshLogLine) + (text.capacity() + monitorColor.capacity() + monitorRuleName.capacity()) * qsizetype(sizeof(QChar))
      + monitorRanges.capacity() * qsizetype(sizeof(QPair<int, int>)); }
};

// Worker-owned stream state; a socket read is not a character, escape sequence or line boundary.
class SshLogStream {
public:
  static constexpr qsizetype SegmentCharacters = 8192;
  static constexpr qsizetype PendingBytes = 4 * 1024 * 1024;
  static constexpr qsizetype HistoryBytes = 32 * 1024 * 1024;

  explicit SshLogStream(const QString &encoding = QStringLiteral("UTF-8"));
  ~SshLogStream();
  SshLogStream(const SshLogStream &) = delete;
  SshLogStream &operator=(const SshLogStream &) = delete;

  void reset(const QString &encoding, bool clearedPartialLine = false);
  std::vector<SshLogLine> feed(QByteArrayView bytes);
  std::vector<SshLogLine> finish();
  const QString &partialLine() const { return m_partial; }
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
  void applyCsi(QChar final);

  QStringDecoder m_utf8{QStringConverter::Utf8};
  bool m_gb = false;
#ifndef Q_OS_WIN
  iconv_t m_converter = nullptr;
#endif
  QByteArray m_pendingBytes;
  QString m_partial;
  quint64 m_logicalId = 0;
  qsizetype m_logicalOffset = 0;
  State m_state = State::Text;
  State m_stringState = State::Osc;
  QString m_csi;
  int m_column = 0;
  int m_lineColumns = 0;
  bool m_continuation = false;
  int m_pendingUtf8 = 0;
};
