#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace AppPaths {

inline QString portableDataRootForApplicationDir(
    const QString &applicationDirPath) {
  const QString cleanPath = QDir::cleanPath(applicationDirPath);
  const QFileInfo applicationDirectory(cleanPath);
  if (applicationDirectory.fileName() == QStringLiteral("MacOS")) {
    const QFileInfo contentsDirectory(
        applicationDirectory.dir().absolutePath());
    if (contentsDirectory.fileName() == QStringLiteral("Contents")) {
      const QFileInfo bundleDirectory(
          contentsDirectory.dir().absolutePath());
      if (bundleDirectory.fileName().endsWith(
              QStringLiteral(".app"), Qt::CaseInsensitive)) {
        return bundleDirectory.dir().absolutePath();
      }
    }
  }
  return QDir(cleanPath).absolutePath();
}

inline QString writableDataRoot() {
  const QString path = portableDataRootForApplicationDir(
      QCoreApplication::applicationDirPath());
  if (!path.isEmpty())
    QDir().mkpath(path);
  return path;
}

inline QString protocolDirectory() {
  return QDir(writableDataRoot()).filePath(QStringLiteral("protocol_xml"));
}

inline QString promptDirectory() {
  return QDir(writableDataRoot()).filePath(QStringLiteral("prompts"));
}

inline QString logDirectory() {
  return QDir(writableDataRoot()).filePath(QStringLiteral("log"));
}

inline QString presetDirectory() {
  return QDir(writableDataRoot()).filePath(QStringLiteral("preset"));
}

} // namespace AppPaths
