#include "AiPromptFile.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>
#include <QTemporaryFile>

namespace AiPromptFile {
bool ensure(const QString &path, QString *error) {
  if (QFileInfo::exists(path) || QFileInfo(path).isSymbolicLink()) return true;
  const QString directory = QFileInfo(path).absolutePath();
  QFile bundled(QStringLiteral(":/prompts/") + FileName);
  if (!QDir().mkpath(directory) || !bundled.open(QIODevice::ReadOnly)) {
    if (error) *error = QStringLiteral("无法读取默认提示词或创建目录：") + path;
    return false;
  }
  const QByteArray bytes = bundled.readAll();
  QTemporaryFile temporary(QDir(directory).filePath(".prompt-default-XXXXXX"));
  if (bundled.error() != QFile::NoError || !temporary.open() ||
      temporary.write(bytes) != bytes.size() || !temporary.flush()) {
    if (error) *error = QStringLiteral("无法准备默认提示词文件：") + path;
    return false;
  }
  temporary.close();
  // Rename refuses an existing destination, including a concurrent user save.
  if (!QFile::rename(temporary.fileName(), path) && !QFileInfo::exists(path)) {
    if (error) *error = QStringLiteral("无法创建默认提示词文件：") + path;
    return false;
  }
  return true;
}

bool readBytes(const QString &path, QByteArray *bytes, QString *error) {
  const QFileInfo info(path);
  QFile file(path);
  if (info.isSymbolicLink() || !info.isFile() || !file.open(QIODevice::ReadOnly)) {
    if (error) *error = QStringLiteral("提示词必须是可读取的普通文件：") + path;
    return false;
  }
  *bytes = file.read(MaxBytes + 1);
  if (file.error() != QFile::NoError || bytes->size() > MaxBytes) {
    if (error) *error = QStringLiteral("提示词读取失败或超过 64 KiB：") + path;
    return false;
  }
  return true;
}

bool decode(const QByteArray &bytes, QString *text, QString *error) {
  QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
  *text = decoder(bytes);
  if (bytes.size() > MaxBytes || decoder.hasError() || text->contains(QChar::Null) || text->trimmed().isEmpty()) {
    if (error) *error = QStringLiteral("提示词须为非空 UTF-8 文本，不能含空字符，大小不能超过 64 KiB");
    return false;
  }
  return true;
}

bool read(const QString &path, QString *text, QString *error) {
  QByteArray bytes;
  return readBytes(path, &bytes, error) && decode(bytes, text, error);
}
}
