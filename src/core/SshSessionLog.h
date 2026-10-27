#pragma once

#include "SshLogStream.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <memory>

// Owned by the SSH worker, independently of the bounded display history.
class SshSessionLog {
public:
  bool active() const { return m_file.isOpen(); }
  QString errorString() const { return m_error; }
  static QString canonicalPath(const QString &path) {
    const QFileInfo info(path);
    if (info.exists()) return info.canonicalFilePath();
    const auto directory = info.dir().canonicalPath();
    return directory.isEmpty() ? info.absoluteFilePath() : QDir(directory).filePath(info.fileName());
  }

  bool start(const QString &path, const QString &encoding, bool append, int bufferKiB = 256) {
    if (active()) { m_error = QStringLiteral("当前会话已在记录日志"); return false; }
    m_error.clear();
    if (bufferKiB < 16 || bufferKiB > 8192) return fail(QStringLiteral("日志缓存大小须为 16～8192 KiB"));
    m_bufferBytes = bufferKiB * 1024;
    m_pending.clear(); m_dirty = false;
    m_lock = std::make_unique<QLockFile>(canonicalPath(path) + QStringLiteral(".loganalyqt-lock"));
    if (!m_lock->tryLock(0)) return fail(QStringLiteral("日志文件正在使用或无法锁定，请选择其他文件"));
    m_stream.reset(encoding);
    if (!m_stream.encodingAvailable()) return fail(QStringLiteral("不支持当前日志字符编码"));
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadWrite | (append ? QIODevice::OpenMode{} : QIODevice::Truncate)))
      return fail(m_file.errorString());
    m_tailOffset = append ? m_file.size() : 0;
    if (!m_file.seek(m_tailOffset)) return fail(m_file.errorString());
    return true;
  }

  bool feed(QByteArrayView bytes) {
    if (!active()) return true;
    append(m_stream.feed(bytes));
    m_dirty = m_dirty || !bytes.isEmpty();
    return m_pending.size() + m_stream.partialLine().toUtf8().size() < m_bufferBytes || flush();
  }
  bool flush() {
    if (!active() || !m_dirty) return true;
    // Rewrite only the latest bounded live tail, once per batch rather than per read.
    const auto tail = m_stream.partialLine().toUtf8();
    if (!m_file.seek(m_tailOffset) || m_file.write(m_pending) != m_pending.size())
      return fail(m_file.errorString());
    m_tailOffset = m_file.pos();
    if (m_file.write(tail) != tail.size() ||
        (m_file.size() != m_file.pos() && !m_file.resize(m_file.pos())) || !m_file.flush())
      return fail(m_file.errorString());
    m_pending.clear(); m_dirty = false;
    return true;
  }
  bool boundary(const QString &encoding) {
    if (!active()) return true;
    append(m_stream.finish());
    if (!flush()) return false;
    m_stream.reset(encoding);
    return m_stream.encodingAvailable() || fail(QStringLiteral("不支持当前日志字符编码"));
  }
  bool stop() {
    if (!active()) return true;
    append(m_stream.finish());
    if (!flush()) return false;
    m_file.close();
    m_lock.reset();
    return true;
  }

  static QString exportSnapshot(const QString &path, const QByteArray &data) {
    QLockFile lock(canonicalPath(path) + QStringLiteral(".loganalyqt-lock"));
    if (!lock.tryLock(0)) return QStringLiteral("目标文件正在记录日志或无法锁定，请选择其他文件");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
      return file.errorString();
    return {};
  }

private:
  bool fail(const QString &error) {
    m_error = error;
    m_file.close();
    m_lock.reset();
    m_pending.clear(); m_dirty = false;
    return false;
  }
  void append(const std::vector<SshLogLine> &lines) {
    for (const auto &line : lines) {
      m_pending += line.text.toUtf8();
      if (!line.continues) m_pending += '\n';
    }
    m_dirty = m_dirty || !lines.empty();
  }

  QFile m_file;
  std::unique_ptr<QLockFile> m_lock;
  SshLogStream m_stream;
  qint64 m_tailOffset = 0;
  QByteArray m_pending;
  int m_bufferBytes = 256 * 1024;
  bool m_dirty = false;
  QString m_error;
};
