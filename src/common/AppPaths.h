#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#ifdef Q_OS_MACOS
#include "MacDataMigration.h"
#endif

namespace AppPaths {

#ifdef LOGANALYQT_UI_TESTING
inline QString testHomeDirectory;
#endif

#ifdef Q_OS_MACOS
inline QString standardLocation(QStandardPaths::StandardLocation location) {
  QString path = QStandardPaths::writableLocation(location);
#ifdef LOGANALYQT_UI_TESTING
  if (!testHomeDirectory.isEmpty()) {
    const QString home = QDir::homePath() + "/";
    if (!path.startsWith(home)) return {};
    path = QDir(testHomeDirectory).filePath(path.mid(home.size()));
  }
#endif
  return path;
}
#endif

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
#ifdef Q_OS_MACOS
  return standardLocation(QStandardPaths::AppLocalDataLocation);
#else
  const QString path = portableDataRootForApplicationDir(
      QCoreApplication::applicationDirPath());
  if (!path.isEmpty())
    QDir().mkpath(path);
  return path;
#endif
}

inline QString protocolDirectory() {
  return QDir(writableDataRoot()).filePath(QStringLiteral("protocol_xml"));
}

inline QString promptDirectory() {
  return QDir(writableDataRoot()).filePath(QStringLiteral("prompts"));
}

inline QString logDirectory() {
#ifdef Q_OS_MACOS
  const QString support = standardLocation(QStandardPaths::GenericDataLocation);
  if (support.isEmpty()) return {};
  return QDir(QFileInfo(support).absolutePath())
      .filePath("Logs/" + QCoreApplication::applicationName());
#else
  return QDir(writableDataRoot()).filePath(QStringLiteral("log"));
#endif
}

inline QString presetDirectory() {
  return QDir(writableDataRoot()).filePath(QStringLiteral("preset"));
}

inline bool initialize(QString *error) {
#ifdef Q_OS_MACOS
  try {
    const auto path = [](const QString &value) { return std::filesystem::path(value.toStdString()); };
    std::vector<std::filesystem::path> legacy;
    const auto installation = portableDataRootForApplicationDir(QCoreApplication::applicationDirPath());
    // Development builds never import configuration or resources from their build tree.
    if (installation == QStringLiteral("/Applications")) {
      legacy = {path(installation), path(QCoreApplication::applicationDirPath())};
    }
    MacDataMigration::initialize(legacy, path(writableDataRoot()), path(logDirectory()), true);
  } catch (const std::exception &failure) {
    if (error) *error = QString::fromUtf8(failure.what());
    return false;
  }
#else
  Q_UNUSED(error)
#endif
  return true;
}

} // namespace AppPaths
