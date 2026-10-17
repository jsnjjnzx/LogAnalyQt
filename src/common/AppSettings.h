#pragma once
#include "AppPaths.h"
#include <QSettings>

/// 各平台均使用程序同级目录中的便携式配置。
inline QSettings appSettings() {
    return QSettings(QDir(AppPaths::writableDataRoot())
                         .filePath(QStringLiteral("LogAnalyQt.ini")),
                     QSettings::IniFormat);
}
