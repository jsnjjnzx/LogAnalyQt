#pragma once
#include "AppPaths.h"
#include "JsonSettings.h"

inline JsonSettings appSettings() {
  return JsonSettings(QDir(AppPaths::writableDataRoot()).filePath(QStringLiteral("settings.json")));
}
// Device-bound verification state is deliberately excluded from portable preferences.
inline QSettings deviceStateSettings() {
  return QSettings(QDir(AppPaths::writableDataRoot()).filePath(QStringLiteral("device-state.ini")), QSettings::IniFormat);
}
