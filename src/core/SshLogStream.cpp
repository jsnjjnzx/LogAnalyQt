#include "SshLogStream.h"
#include <utility>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <iconv.h>
#endif

namespace {
int cellWidth(char32_t c) {
  const auto category = QChar::category(c);
  if (category == QChar::Mark_NonSpacing || category == QChar::Mark_Enclosing || c == 0x200d) return 0;
  return (c >= 0x1100 && c <= 0x115f) || (c >= 0x2e80 && c <= 0xa4cf)
      || (c >= 0xac00 && c <= 0xd7a3) || (c >= 0xf900 && c <= 0xfaff)
      || (c >= 0xfe10 && c <= 0xfe6f) || (c >= 0xff01 && c <= 0xff60)
      || (c >= 0xffe0 && c <= 0xffe6) || (c >= 0x1f300 && c <= 0x1faff)
      || (c >= 0x20000 && c <= 0x3ffff) ? 2 : 1;
}
char32_t codePoint(QStringView text, qsizetype index, qsizetype *length) {
  *length = text[index].isHighSurrogate() && index + 1 < text.size() && text[index + 1].isLowSurrogate() ? 2 : 1;
  return *length == 2 ? QChar::surrogateToUcs4(text[index], text[index + 1]) : text[index].unicode();
}
int columns(QStringView text) {
  int count = 0;
  for (qsizetype i = 0, length; i < text.size(); i += length) count += cellWidth(codePoint(text, i, &length));
  return count;
}
struct CellPosition { qsizetype index; int column; };
CellPosition cellPosition(QStringView text, int target, bool roundUp = false) {
  int column = 0;
  for (qsizetype i = 0, length; i < text.size(); i += length) {
    const int width = cellWidth(codePoint(text, i, &length));
    if (width && column >= target) return {i, column};
    if (column + width > target) return roundUp ? CellPosition{i + length, column + width} : CellPosition{i, column};
    column += width;
  }
  return {text.size(), column};
}
constexpr quint32 rgb(int r, int g, int b) {
  return 0xff000000u | (quint32(r) << 16) | (quint32(g) << 8) | quint32(b);
}
quint32 indexedColor(int index) {
  constexpr quint32 palette[] = {
    0xff000000, 0xffcd3131, 0xff0dbc79, 0xffe5b34f, 0xff4c86d8, 0xffbc3fbc, 0xff11a8cd, 0xffe5e5e5,
    0xff666666, 0xfff14c4c, 0xff23d18b, 0xfff5f543, 0xff6faeff, 0xffd670d6, 0xff29b8db, 0xffffffff
  };
  if (index < 16) return palette[index];
  if (index >= 232) {
    const int gray = 8 + (index - 232) * 10;
    return rgb(gray, gray, gray);
  }
  const auto component = [](int n) { return n ? 55 + n * 40 : 0; };
  index -= 16;
  return rgb(component(index / 36), component(index / 6 % 6), component(index % 6));
}
}

SshLogStream::SshLogStream(const QString &encoding) { reset(encoding); }
int SshLogStream::cursorPosition() const { return int(cellPosition(m_partial, m_column).index); }
int SshLogStream::cursorPadding() const { return qMax(0, m_column - cellPosition(m_partial, m_column).column); }

void SshLogStream::syncCursorRow() {
  while (m_column < m_rowStart) m_rowStart -= TerminalColumns;
  while (m_column >= m_rowStart + TerminalColumns) m_rowStart += TerminalColumns;
}

void SshLogStream::moveCursor(int column) {
  m_column = qBound(0, column, int(SegmentCharacters));
  syncCursorRow();
}

void SshLogStream::moveCursorRows(int rows) {
  moveCursor(m_column + rows * TerminalColumns);
}

SshLogStream::~SshLogStream() {
#ifndef Q_OS_WIN
  if (m_converter) iconv_close(m_converter);
#endif
}

bool SshLogStream::encodingAvailable() const {
#ifdef Q_OS_WIN
  return !m_gb || IsValidCodePage(936);
#else
  return !m_gb || m_converter;
#endif
}

void SshLogStream::reset(const QString &encoding, bool clearedPartialLine, bool preserveColors) {
  m_gb = encoding.startsWith("GB", Qt::CaseInsensitive);
  m_utf8 = QStringDecoder(QStringConverter::Utf8);
  m_pendingBytes.clear(); m_partial.clear();
  m_styles.clear();
  if (!preserveColors) { m_style = {}; m_basicForeground = -1; m_bright = false; }
  m_state = State::Text; m_csi.clear(); m_column = m_lineColumns = m_rowStart = 0; m_continuation = clearedPartialLine; m_pendingUtf8 = 0;
  ++m_logicalId;
  m_logicalOffset = 0;
#ifndef Q_OS_WIN
  if (m_converter) iconv_close(m_converter);
  m_converter = nullptr;
  if (m_gb) {
    const auto converter = iconv_open("UTF-16LE", "GBK");
    if (converter != reinterpret_cast<iconv_t>(-1)) m_converter = converter;
  }
#endif
}

QString SshLogStream::decode(QByteArrayView bytes, bool final) {
  if (!m_gb) {
    QString result = m_utf8(bytes);
    if (final) {
      char16_t tail[4]{};
      const auto end = m_utf8.finalize(tail, 4);
      result.append(QString::fromUtf16(tail, end.next - tail));
    }
    return result;
  }
  m_pendingBytes.append(bytes.data(), bytes.size());
#ifdef Q_OS_WIN
  qsizetype usable = 0;
  while (usable < m_pendingBytes.size()) {
    const auto c = uchar(m_pendingBytes[usable]);
    if (c >= 0x81 && c <= 0xfe) {
      if (usable + 1 == m_pendingBytes.size()) break;
      usable += 2;
    } else ++usable;
  }
  const int count = MultiByteToWideChar(936, 0, m_pendingBytes.constData(), int(usable), nullptr, 0);
  QString result(count, Qt::Uninitialized);
  if (count) MultiByteToWideChar(936, 0, m_pendingBytes.constData(), int(usable),
                               reinterpret_cast<wchar_t *>(result.data()), count);
  m_pendingBytes.remove(0, usable);
#else
  if (!m_converter) {
    // A missing platform codec must not silently decode GBK as UTF-8.
    QString result(m_pendingBytes.size(), QChar::ReplacementCharacter);
    m_pendingBytes.clear(); return result;
  }
  QByteArray converted(m_pendingBytes.size() * 2 + 8, Qt::Uninitialized);
  char *input = m_pendingBytes.data(), *output = converted.data();
  size_t remaining = size_t(m_pendingBytes.size()), available = size_t(converted.size());
  while (remaining) {
    if (iconv(m_converter, &input, &remaining, &output, &available) != size_t(-1)) break;
    if (errno == EINVAL) break;
    if (errno == E2BIG) break; // GBK produces at most two UTF-16 bytes per input byte.
    ++input; --remaining;
    *output++ = char(0xfd); *output++ = char(0xff); available -= 2;
  }
  const auto used = converted.size() - qsizetype(available);
  QStringDecoder utf16(QStringConverter::Utf16LE);
  QString result = utf16(QByteArrayView(converted.constData(), used));
  m_pendingBytes.remove(0, m_pendingBytes.size() - qsizetype(remaining));
#endif
  if (final && !m_pendingBytes.isEmpty()) {
    result.append(QChar::ReplacementCharacter); m_pendingBytes.clear();
  }
  return result;
}

QByteArray SshLogStream::encode(const QString &text, const QString &encoding) {
  if (!encoding.startsWith("GB", Qt::CaseInsensitive)) return text.toUtf8();
#ifdef Q_OS_WIN
  const int count = WideCharToMultiByte(936, 0, reinterpret_cast<const wchar_t *>(text.utf16()),
                                       int(text.size()), nullptr, 0, "?", nullptr);
  QByteArray result(count, Qt::Uninitialized);
  if (count) WideCharToMultiByte(936, 0, reinterpret_cast<const wchar_t *>(text.utf16()),
                               int(text.size()), result.data(), count, "?", nullptr);
  return result;
#else
  const auto converter = iconv_open("GBK", "UTF-8");
  if (converter == reinterpret_cast<iconv_t>(-1)) return {};
  auto utf8 = text.toUtf8();
  QByteArray result(utf8.size() * 2 + 8, Qt::Uninitialized);
  char *input = utf8.data(), *output = result.data();
  size_t remaining = size_t(utf8.size()), available = size_t(result.size());
  while (remaining) {
    if (iconv(converter, &input, &remaining, &output, &available) != size_t(-1)) break;
    if (errno == E2BIG) break;
    // Skip one unrepresentable UTF-8 code point, not one replacement per byte.
    ++input; --remaining;
    while (remaining && (uchar(*input) & 0xc0) == 0x80) { ++input; --remaining; }
    *output++ = '?'; --available;
  }
  iconv_close(converter);
  result.truncate(result.size() - qsizetype(available));
  return result;
#endif
}

void SshLogStream::emitLine(std::vector<SshLogLine> &lines, bool continues) {
  m_rowStart = continues ? m_rowStart - columns(m_partial) : 0;
  m_logicalOffset = continues ? m_logicalOffset + m_partial.size() : 0;
  auto colors = colorRuns();
  lines.push_back({std::exchange(m_partial, {}), m_logicalId, m_continuation, continues});
  lines.back().colors = std::move(colors);
  m_styles.clear();
  m_column = m_lineColumns = 0;
  m_continuation = continues;
  if (!continues) ++m_logicalId;
}

QList<SshColorRun> SshLogStream::colorRuns() const {
  QList<SshColorRun> result;
  for (int start = 0, end; start < m_styles.size(); start = end) {
    const auto style = m_styles[start];
    for (end = start + 1; end < m_styles.size() && m_styles[end] == style; ++end) {}
    if (style != SshColorStyle{}) result.append({start, end - start, style});
  }
  return result;
}

void SshLogStream::replace(qsizetype start, qsizetype length, QStringView text, SshColorStyle style) {
  if (!m_styles.isEmpty() || (style != SshColorStyle{} && !text.isEmpty())) {
    if (m_styles.isEmpty()) m_styles.resize(m_partial.size());
    if (length == text.size()) {
      std::fill_n(m_styles.begin() + start, length, style);
    } else {
      m_styles.remove(start, length);
      m_styles.insert(start, text.size(), style);
    }
  }
  m_partial.replace(start, length, text.data(), text.size());
}

void SshLogStream::splitLine(qsizetype split, std::vector<SshLogLine> &lines) {
  auto tail = m_partial.mid(split);
  auto styles = m_styles.isEmpty() ? QList<SshColorStyle>{} : m_styles.sliced(split);
  replace(split, m_partial.size() - split, {}, {});
  emitLine(lines, true);
  m_partial = std::move(tail);
  m_styles = std::move(styles);
  m_column = m_lineColumns = columns(m_partial);
  m_rowStart += qMax(0, (m_column - m_rowStart - 1) / TerminalColumns) * TerminalColumns;
}

void SshLogStream::append(QStringView text, std::vector<SshLogLine> &lines) {
  while (!text.isEmpty()) {
    if (m_column != m_lineColumns) {
      qsizetype length;
      const int width = cellWidth(codePoint(text, 0, &length));
      const auto start = cellPosition(m_partial, m_column);
      const auto end = cellPosition(m_partial, m_column + width, true);
      const QString replacement = QString(qMax(0, m_column - start.column), ' ') + text.first(length)
          + QString(qMax(0, end.column - m_column - width), ' ');
      replace(start.index, end.index - start.index, replacement, m_style);
      m_column += width; m_lineColumns = columns(m_partial);
      m_rowStart += qMax(0, (m_column - m_rowStart - 1) / TerminalColumns) * TerminalColumns;
      text = text.sliced(length);
      if (m_partial.size() > SegmentCharacters) {
        auto split = SegmentCharacters;
        if (m_partial[split - 1].isHighSurrogate()) --split;
        splitLine(split, lines);
      }
      continue;
    }
    if (m_partial.size() == SegmentCharacters) {
      // Keep a UTF-16 surrogate pair together at the display-segment boundary.
      if (m_partial.back().isHighSurrogate() && text.front().isLowSurrogate()) {
        splitLine(m_partial.size() - 1, lines);
        m_column = m_lineColumns = 1;
      } else emitLine(lines, true);
    }
    const auto take = qMin(SegmentCharacters - m_partial.size(), text.size());
    if (!m_styles.isEmpty() || m_style != SshColorStyle{}) {
      if (m_styles.isEmpty()) m_styles.resize(m_partial.size());
      m_styles.insert(m_styles.size(), take, m_style);
    }
    if (m_partial.isEmpty()) m_partial = text.first(take).toString();
    else m_partial.append(text.first(take));
    m_lineColumns += columns(text.first(take)); m_column = m_lineColumns;
    // The last cell wraps only when another printable cell arrives.
    m_rowStart += qMax(0, (m_column - m_rowStart - 1) / TerminalColumns) * TerminalColumns;
    text = text.sliced(take);
  }
}

void SshLogStream::applyCsi(QChar final) {
  if (final == 'm') { applySgr(); return; }
  // shortcut: current-line terminal editing only; use a screen emulator for full-screen TUIs.
  bool valid = false;
  const int value = m_csi.section(';', 0, 0).toInt(&valid);
  const int count = valid && value > 0 ? qMin(value, int(SegmentCharacters)) : 1;
  if (final == 'C' || final == 'a') moveCursor(m_column + count);
  else if (final == 'D') moveCursor(m_column - count);
  else if (final == 'A') moveCursorRows(-count);
  else if (final == 'B') moveCursorRows(count);
  else if (final == 'G' || final == '`') moveCursor(m_rowStart + count - 1);
  else if (final == 'K') {
    const int mode = valid ? value : 0;
    if (mode == 0) {
      const auto start = cellPosition(m_partial, m_column).index;
      replace(start, m_partial.size() - start, {}, {});
    } else if (mode == 1) replace(0, cellPosition(m_partial, m_column + 1, true).index, QString(m_column + 1, ' '), m_style);
    else if (mode == 2) { m_partial.clear(); m_styles.clear(); }
    m_lineColumns = columns(m_partial);
  } else if (final == 'P' || final == '@' || final == 'X') {
    const auto start = cellPosition(m_partial, m_column);
    const auto end = cellPosition(m_partial, m_column + count, true);
    if (final == '@') replace(start.index, 0, QString(qMax(0, qMin(count, int(SegmentCharacters - m_partial.size()))), ' '), m_style);
    else replace(start.index, end.index - start.index, final == 'X' ? QString(qMin(count, int(end.column - start.column)), ' ') : QString(), m_style);
    m_lineColumns = columns(m_partial);
  }
}

void SshLogStream::applySgr() {
  for (const auto c : m_csi)
    if ((c < '0' || c > '9') && c != ';' && c != ':') return;
  const auto parameters = m_csi.split(';');
  const auto byte = [](const QString &value) {
    bool ok = false;
    const int n = value.toInt(&ok);
    return ok && n >= 0 && n <= 255 ? n : -1;
  };
  const auto extended = [&](int mode, const QStringList &values, qsizetype offset) -> quint32 {
    if (mode == 5 && values.size() == offset + 1) {
      const int i = byte(values[offset]);
      return i >= 0 ? indexedColor(i) : 0;
    }
    if (mode == 2 && (values.size() == offset + 3 || values.size() == offset + 4)) {
      const int r = byte(values[values.size() - 3]), g = byte(values[values.size() - 2]), b = byte(values.back());
      if (r >= 0 && g >= 0 && b >= 0) return rgb(r, g, b);
    }
    return 0;
  };
  for (qsizetype i = 0; i < parameters.size(); ++i) {
    const auto &parameter = parameters[i];
    const auto sub = parameter.contains(':') ? parameter.split(':') : QStringList{};
    const int code = parameter.isEmpty() ? 0 : byte(sub.isEmpty() ? parameter : sub.front());
    if (!sub.isEmpty() && code != 38 && code != 48) continue;
    if (code == 0) { m_style = {}; m_basicForeground = -1; m_bright = false; }
    else if (code == 1 || code == 22) {
      m_bright = code == 1;
      if (m_basicForeground >= 0) m_style.foreground = indexedColor(m_basicForeground + (m_bright ? 8 : 0));
    } else if (code >= 30 && code <= 37) {
      m_basicForeground = code - 30;
      m_style.foreground = indexedColor(m_basicForeground + (m_bright ? 8 : 0));
    } else if (code == 39 || (code >= 90 && code <= 97)) {
      m_basicForeground = -1;
      m_style.foreground = code == 39 ? 0 : indexedColor(code - 82);
    } else if (code >= 40 && code <= 47) m_style.background = indexedColor(code - 40);
    else if (code >= 100 && code <= 107) m_style.background = indexedColor(code - 92);
    else if (code == 49) m_style.background = 0;
    else if (code == 38 || code == 48 || code == 58) {
      quint32 color = 0;
      if (sub.size() > 1) color = extended(byte(sub[1]), sub, 2);
      else if (i + 1 < parameters.size()) {
        const int mode = byte(parameters[++i]);
        const int count = mode == 5 ? 1 : mode == 2 ? 3 : 0;
        if (!count || i + count >= parameters.size()) break;
        color = extended(mode, parameters.mid(i + 1, count), 0);
        i += count;
      }
      if (code == 38 && color) { m_style.foreground = color; m_basicForeground = -1; }
      if (code == 48 && color) m_style.background = color;
    }
  }
}

void SshLogStream::consume(QStringView text, std::vector<SshLogLine> &lines) {
  qsizetype i = 0;
  while (i < text.size()) {
    const QChar c = text[i++];
    if (m_state == State::Text) {
      if (c == '\n') {
        emitLine(lines, false);
      } else if (c == '\r') {
        moveCursor(qMax(0, m_rowStart));
      } else if (c == '\b') {
        moveCursor(m_column - 1);
      } else if (c == '\t') {
        append(QString(8 - m_column % 8, ' '), lines);
      } else if (c == QChar(0x1b)) {
        m_state = State::Escape;
      } else if (c.unicode() >= 0x20 && c != QChar(0x7f)) {
        const auto start = i - 1;
        while (i < text.size() && text[i] != '\r' && text[i] != '\n' &&
               text[i].unicode() >= 0x20 && text[i] != QChar(0x7f)) ++i;
        append(text.sliced(start, i - start), lines);
      }
    } else if (m_state == State::Escape) {
      if (c == '[') { m_csi.clear(); m_csiOverflow = false; m_state = State::Csi; }
      else if (c == ']') m_state = State::Osc;
      else if (c == 'P' || c == '^' || c == '_') m_state = State::String;
      else if (c.unicode() >= 0x20 && c.unicode() <= 0x2f) m_state = State::EscapeIntermediate;
      else m_state = State::Text;
    } else if (m_state == State::Csi) {
      if (c.unicode() >= 0x40 && c.unicode() <= 0x7e) {
        if (!m_csiOverflow) applyCsi(c);
        m_csi.clear(); m_state = State::Text;
      } else if (c == QChar(0x1b)) m_state = State::Escape;
      else if (m_csi.size() < 64) m_csi.append(c);
      else m_csiOverflow = true;
    } else if (m_state == State::EscapeIntermediate) {
      if (c.unicode() >= 0x30 && c.unicode() <= 0x7e) m_state = State::Text;
    } else if (m_state == State::StringEscape) {
      m_state = c == '\\' ? State::Text : (c == QChar(0x1b) ? State::StringEscape : m_stringState);
    } else {
      if (c == QChar(7) && m_state == State::Osc) m_state = State::Text;
      else if (c == QChar(0x1b)) { m_stringState = m_state; m_state = State::StringEscape; }
    }
  }
}

std::vector<SshLogLine> SshLogStream::feed(QByteArrayView bytes) {
  std::vector<SshLogLine> lines;
  const auto text = decode(bytes);
  consume(text, lines);
  if (!m_gb && !bytes.isEmpty()) {
    // Only a trailing incomplete code point can remain in the decoder, not an ASCII ANSI terminator.
    qsizetype lead = bytes.size() - 1;
    int continuationBytes = 0;
    while (lead >= 0 && (uchar(bytes[lead]) & 0xc0) == 0x80 && continuationBytes < 3) { --lead; ++continuationBytes; }
    if (lead < 0) m_pendingUtf8 = qMax(0, m_pendingUtf8 - continuationBytes);
    else {
      const auto c = uchar(bytes[lead]);
      const int needed = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
      m_pendingUtf8 = qMax(0, needed - continuationBytes);
    }
  }
  return lines;
}

std::vector<SshLogLine> SshLogStream::finish() {
  std::vector<SshLogLine> lines;
  const auto tail = decode({}, true);
  consume(tail, lines);
  if (!m_partial.isEmpty()) emitLine(lines, false);
  m_state = State::Text; m_csi.clear(); m_pendingUtf8 = 0;
  return lines;
}
