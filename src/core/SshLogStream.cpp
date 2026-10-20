#include "SshLogStream.h"
#include <utility>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <iconv.h>
#endif

SshLogStream::SshLogStream(const QString &encoding) { reset(encoding); }

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

void SshLogStream::reset(const QString &encoding, bool clearedPartialLine) {
  m_gb = encoding.startsWith("GB", Qt::CaseInsensitive);
  m_utf8 = QStringDecoder(QStringConverter::Utf8);
  m_pendingBytes.clear(); m_partial.clear();
  m_state = State::Text; m_afterCr = false; m_continuation = clearedPartialLine; m_pendingUtf8 = 0;
  ++m_logicalId;
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
  lines.push_back({std::exchange(m_partial, {}), m_logicalId, m_continuation, continues});
  m_continuation = continues;
  if (!continues) ++m_logicalId;
}

void SshLogStream::append(QStringView text, std::vector<SshLogLine> &lines) {
  while (!text.isEmpty()) {
    if (m_partial.size() == SegmentCharacters) {
      // Keep a UTF-16 surrogate pair together at the display-segment boundary.
      if (m_partial.back().isHighSurrogate() && text.front().isLowSurrogate()) {
        const auto high = m_partial.back(); m_partial.chop(1);
        emitLine(lines, true); m_partial.append(high);
      } else emitLine(lines, true);
    }
    const auto take = qMin(SegmentCharacters - m_partial.size(), text.size());
    if (m_partial.isEmpty()) m_partial = text.first(take).toString();
    else m_partial.append(text.first(take));
    text = text.sliced(take);
  }
}

void SshLogStream::consume(QStringView text, std::vector<SshLogLine> &lines) {
  qsizetype i = 0;
  while (i < text.size()) {
    const QChar c = text[i++];
    if (m_state == State::Text) {
      if (m_afterCr) { m_afterCr = false; if (c == '\n') continue; }
      if (c == '\r' || c == '\n') {
        emitLine(lines, false); m_afterCr = c == '\r';
      } else if (c == QChar(0x1b)) {
        m_state = State::Escape;
      } else if (c != QChar(7)) {
        const auto start = i - 1;
        while (i < text.size() && text[i] != '\r' && text[i] != '\n' &&
               text[i] != QChar(0x1b) && text[i] != QChar(7)) ++i;
        append(text.sliced(start, i - start), lines);
      }
    } else if (m_state == State::Escape) {
      if (c == '[') m_state = State::Csi;
      else if (c == ']') m_state = State::Osc;
      else if (c == 'P' || c == '^' || c == '_') m_state = State::String;
      else if (c.unicode() >= 0x20 && c.unicode() <= 0x2f) m_state = State::EscapeIntermediate;
      else m_state = State::Text;
    } else if (m_state == State::Csi) {
      if (c.unicode() >= 0x40 && c.unicode() <= 0x7e) m_state = State::Text;
      else if (c == QChar(0x1b)) m_state = State::Escape;
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
  m_state = State::Text; m_afterCr = false; m_pendingUtf8 = 0;
  return lines;
}
