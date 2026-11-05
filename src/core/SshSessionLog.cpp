#include "SshSessionLog.h"

#include <QRegularExpression>

namespace {
constexpr int MaxNameAttempts = 1000;

QString defaultNameTemplate() {
  return QStringLiteral("{session}-{host}_{date}_{time}.log");
}

QString numberedName(const QString &name, int attempt) {
  if (attempt <= 1) return name;
  const QFileInfo info(name);
  const QString suffix = info.suffix();
  const QString base = suffix.isEmpty() ? name : name.left(name.size() - suffix.size() - 1);
  const QString number = QStringLiteral("%1").arg(attempt - 1, 3, 10, QChar('0'));
  return base + QLatin1Char('-') + number + (suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix);
}
}

QString SshSessionLog::sanitizedComponent(QString value, const QString &fallback) {
  static const QRegularExpression invalid(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
  value.replace(invalid, QStringLiteral("_"));
  value.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
  value = value.trimmed();
  if (value.isEmpty() || value == QStringLiteral(".") || value == QStringLiteral("..")) value = fallback;
  return value.left(80);
}

QString SshSessionLog::expandName(const QString &nameTemplate, const QString &sessionName,
                                  const QString &host, const QDateTime &now, int attempt) {
  QString name = nameTemplate.trimmed().isEmpty() ? defaultNameTemplate() : nameTemplate.trimmed();
  name.replace(QStringLiteral("{session}"), sanitizedComponent(sessionName, QStringLiteral("session")));
  name.replace(QStringLiteral("{host}"), sanitizedComponent(host, QStringLiteral("host")));
  name.replace(QStringLiteral("{date}"), now.toString(QStringLiteral("yyyyMMdd")));
  name.replace(QStringLiteral("{time}"), now.toString(QStringLiteral("HHmmss")));
  name.replace(QStringLiteral("{seq}"), QStringLiteral("%1").arg(attempt, 3, 10, QChar('0')));
  static const QRegularExpression invalid(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
  name.replace(invalid, QStringLiteral("_"));
  name = name.trimmed();
  if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
    name = QStringLiteral("ssh.log");
  name = name.left(180);
  if (!nameTemplate.contains(QStringLiteral("{seq}"))) name = numberedName(name, attempt);
  return name;
}

QString SshSessionLog::previewFolderPath(const QString &folder, const QString &nameTemplate,
                                         const QString &sessionName, const QString &host,
                                         const QDateTime &now) {
  const QString directory = QDir(folder).absolutePath();
  return QDir(directory).filePath(expandName(nameTemplate, sessionName, host, now, 1));
}

QString SshSessionLog::candidateName(const QDateTime &now, int attempt) const {
  return expandName(m_template, m_sessionName, m_host, now, attempt);
}

bool SshSessionLog::openNextFolderFile() {
  if (!QFileInfo(m_folder).isDir())
    return fail(QStringLiteral("日志文件夹不可用，请检查路径或挂载状态"));
  const auto now = QDateTime::currentDateTime();
  for (int attempt = 1; attempt <= MaxNameAttempts; ++attempt) {
    const QString path = QDir(m_folder).filePath(candidateName(now, attempt));
    if (QFileInfo::exists(path)) continue;
    auto lock = std::make_unique<QLockFile>(canonicalPath(path) + QStringLiteral(".loganalyqt-lock"));
    if (!lock->tryLock(0)) continue;
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::ReadWrite | QIODevice::NewOnly)) continue;
    m_file = std::move(file);
    m_lock = std::move(lock);
    m_path = path;
    m_tailOffset = 0;
    m_pending.clear();
    m_dirty = false;
    m_chunkStarted = false;
    m_chunkClock.invalidate();
    m_rotationRetry.invalidate();
    m_warning.clear();
    if (!m_file->seek(0)) return fail(m_file->errorString());
    return true;
  }
  return fail(QStringLiteral("无法创建唯一的日志文件，请检查文件夹权限或磁盘空间"));
}

bool SshSessionLog::rotationDue() const {
  if (!m_folderMode || !m_file || !m_file->isOpen()) return false;
  if (m_rotateBytes > 0 && m_file->size() >= m_rotateBytes) return true;
  return m_rotateMs > 0 && m_chunkStarted && m_chunkClock.isValid() && m_chunkClock.elapsed() >= m_rotateMs;
}

bool SshSessionLog::maybeRotate() {
  if (!rotationDue()) return true;
  if (m_rotationRetry.isValid() && m_rotationRetry.elapsed() < RotationRetryMs) return true;
  if (!flush()) return false;
  if (!m_file || m_tailOffset <= 0) return true;
  if (!QFileInfo(m_folder).isDir()) {
    setWarning(QStringLiteral("日志文件夹暂时不可用，继续写入当前卷"));
    m_rotationRetry.start();
    return true;
  }

  std::unique_ptr<QFile> nextFile;
  std::unique_ptr<QLockFile> nextLock;
  QString nextPath;
  const auto now = QDateTime::currentDateTime();
  for (int attempt = 1; attempt <= MaxNameAttempts; ++attempt) {
    const QString path = QDir(m_folder).filePath(candidateName(now, attempt));
    if (QFileInfo::exists(path)) continue;
    auto lock = std::make_unique<QLockFile>(canonicalPath(path) + QStringLiteral(".loganalyqt-lock"));
    if (!lock->tryLock(0)) continue;
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::ReadWrite | QIODevice::NewOnly)) continue;
    nextFile = std::move(file);
    nextLock = std::move(lock);
    nextPath = path;
    break;
  }
  if (!nextFile) {
    setWarning(QStringLiteral("无法创建下一日志卷，继续写入当前文件"));
    m_rotationRetry.start();
    return true;
  }

  const QByteArray tail = m_stream.partialLine().toUtf8();
  if (nextFile->write(tail) != tail.size() || !nextFile->flush()) {
    nextFile->close();
    QFile::remove(nextPath);
    nextLock.reset();
    setWarning(QStringLiteral("无法写入下一日志卷，继续写入当前文件"));
    m_rotationRetry.start();
    return true;
  }
  if (!m_file->resize(m_tailOffset)) {
    nextFile->close();
    QFile::remove(nextPath);
    nextLock.reset();
    setWarning(QStringLiteral("无法截断当前日志卷，继续写入当前文件"));
    m_rotationRetry.start();
    return true;
  }

  m_file->close();
  m_file.reset();
  m_lock.reset();
  m_file = std::move(nextFile);
  m_lock = std::move(nextLock);
  m_path = nextPath;
  m_tailOffset = 0;
  m_pending.clear();
  m_dirty = false;
  m_chunkStarted = true;
  m_chunkClock.restart();
  m_rotationRetry.invalidate();
  m_warning.clear();
  ++m_rotationRevision;
  return true;
}
