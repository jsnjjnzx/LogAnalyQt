#pragma once
#include "AppPaths.h"
#include <QSettings>

/// Mac 使用用户数据目录；Windows 保持便携式配置。
inline QSettings appSettings() {
    return QSettings(QDir(AppPaths::writableDataRoot())
                         .filePath(QStringLiteral("LogAnalyQt.ini")),
                     QSettings::IniFormat);
}
