#pragma once

#include "SshLogStream.h"
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <memory>

// Owned by the SSH worker, independently of the bounded display history.
class SshSessionLog {
public:
  static constexpr int RotationRetryMs = 30000;

  bool active() const { return m_file && m_file->isOpen(); }
  QString errorString() const { return m_error; }
  QString warningString() const { return m_warning; }
  QString currentPath() const { return m_path; }
  int rotationRevision() const { return m_rotationRevision; }
  static QString canonicalPath(const QString &path) {
    const QFileInfo info(path);
    if (info.exists()) return info.canonicalFilePath();
    const auto directory = info.dir().canonicalPath();
    return directory.isEmpty() ? info.absoluteFilePath() : QDir(directory).filePath(info.fileName());
  }
  static QString previewFolderPath(const QString &folder, const QString &nameTemplate,
                                   const QString &sessionName, const QString &host,
                                   const QDateTime &now = QDateTime::currentDateTime());

  bool start(const QString &path, const QString &encoding, bool append, int bufferKiB = 256) {
    if (active()) { m_error = QStringLiteral("当前会话已在记录日志"); return false; }
    resetState();
    if (bufferKiB < 16 || bufferKiB > 8192) return fail(QStringLiteral("日志缓存大小须为 16～8192 KiB"));
    m_bufferBytes = bufferKiB * 1024;
    m_lock = std::make_unique<QLockFile>(canonicalPath(path) + QStringLiteral(".loganalyqt-lock"));
    if (!m_lock->tryLock(0)) return fail(QStringLiteral("日志文件正在使用或无法锁定，请选择其他文件"));
    m_stream.reset(encoding);
    if (!m_stream.encodingAvailable()) return fail(QStringLiteral("不支持当前日志字符编码"));
    m_file = std::make_unique<QFile>(path);
    if (!m_file->open(QIODevice::ReadWrite | (append ? QIODevice::OpenMode{} : QIODevice::Truncate)))
      return fail(m_file->errorString());
    m_path = path;
    m_tailOffset = append ? m_file->size() : 0;
    if (!m_file->seek(m_tailOffset)) return fail(m_file->errorString());
    return true;
  }

  bool startFolder(const QString &folder, const QString &nameTemplate, const QString &encoding,
                   int bufferKiB, const QString &sessionName, const QString &host,
                   qint64 rotateBytes = 0, qint64 rotateMs = 0) {
    if (active()) { m_error = QStringLiteral("当前会话已在记录日志"); return false; }
    resetState();
    if (bufferKiB < 16 || bufferKiB > 8192) return fail(QStringLiteral("日志缓存大小须为 16～8192 KiB"));
    if (rotateBytes < 0 || rotateMs < 0) return fail(QStringLiteral("日志分卷参数无效"));
    if (folder.trimmed().isEmpty() || nameTemplate.trimmed().isEmpty())
      return fail(QStringLiteral("请选择日志文件夹并填写文件名模板"));
    if (!QDir().mkpath(folder)) return fail(QStringLiteral("无法创建日志文件夹，请检查路径和权限"));
    const QFileInfo folderInfo(folder);
    if (!folderInfo.isDir()) return fail(QStringLiteral("日志保存位置不是文件夹"));
    const auto canonical = folderInfo.canonicalFilePath();
    m_folder = canonical.isEmpty() ? QDir(folder).absolutePath() : canonical;
    m_folderMode = true;
    m_template = nameTemplate.trimmed();
    m_sessionName = sessionName;
    m_host = host;
    m_rotateBytes = rotateBytes;
    m_rotateMs = rotateMs;
    m_bufferBytes = bufferKiB * 1024;
    m_stream.reset(encoding);
    if (!m_stream.encodingAvailable()) return fail(QStringLiteral("不支持当前日志字符编码"));
    if (!openNextFolderFile()) return fail(m_error.isEmpty() ? QStringLiteral("无法创建日志文件") : m_error);
    return true;
  }

  bool feed(QByteArrayView bytes) {
    if (!active()) return true;
    if (!bytes.isEmpty()) {
      if (!m_chunkStarted) {
        m_chunkStarted = true;
        m_chunkClock.start();
      }
      if (!maybeRotate()) return false;
    }
    append(m_stream.feed(bytes));
    m_dirty = m_dirty || !bytes.isEmpty();
    return m_pending.size() + m_stream.partialLine().toUtf8().size() < m_bufferBytes || flush();
  }
  bool flush() {
    if (!active() || !m_dirty) return true;
    // Rewrite only the latest bounded live tail, once per batch rather than per read.
    const auto tail = m_stream.partialLine().toUtf8();
    if (!m_file->seek(m_tailOffset) || m_file->write(m_pending) != m_pending.size())
      return fail(m_file->errorString());
    m_tailOffset = m_file->pos();
    if (m_file->write(tail) != tail.size() ||
        (m_file->size() != m_file->pos() && !m_file->resize(m_file->pos())) || !m_file->flush())
      return fail(m_file->errorString());
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
    m_file->close();
    m_file.reset();
    m_lock.reset();
    m_chunkStarted = false;
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
  void resetState() {
    m_error.clear();
    m_warning.clear();
    m_folder.clear();
    m_path.clear();
    m_template.clear();
    m_sessionName.clear();
    m_host.clear();
    m_pending.clear();
    m_dirty = false;
    m_folderMode = false;
    m_rotateBytes = 0;
    m_rotateMs = 0;
    m_tailOffset = 0;
    m_chunkStarted = false;
    m_rotationRevision = 0;
    m_chunkClock.invalidate();
    m_rotationRetry.invalidate();
    m_file.reset();
    m_lock.reset();
  }
  static QString sanitizedComponent(QString value, const QString &fallback);
  static QString expandName(const QString &nameTemplate, const QString &sessionName,
                            const QString &host, const QDateTime &now, int attempt);
  QString candidateName(const QDateTime &now, int attempt) const;
  bool openNextFolderFile();
  bool maybeRotate();
  bool rotationDue() const;
  void setWarning(const QString &warning) { m_warning = warning; }
  bool fail(const QString &error) {
    m_error = error;
    m_warning.clear();
    if (m_file) m_file->close();
    m_file.reset();
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

  std::unique_ptr<QFile> m_file;
  std::unique_ptr<QLockFile> m_lock;
  SshLogStream m_stream;
  QString m_folder;
  QString m_path;
  QString m_template;
  QString m_sessionName;
  QString m_host;
  qint64 m_tailOffset = 0;
  qint64 m_rotateBytes = 0;
  qint64 m_rotateMs = 0;
  QByteArray m_pending;
  int m_bufferBytes = 256 * 1024;
  bool m_dirty = false;
  bool m_folderMode = false;
  bool m_chunkStarted = false;
  int m_rotationRevision = 0;
  QElapsedTimer m_chunkClock;
  QElapsedTimer m_rotationRetry;
  QString m_error;
  QString m_warning;
};
