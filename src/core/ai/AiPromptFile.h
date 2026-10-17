#pragma once

#include <QString>
#include <QByteArray>

namespace AiPromptFile {
inline constexpr auto FileName = "ai_698_framing.md";
inline constexpr int MaxBytes = 64 * 1024;
bool ensure(const QString &path, QString *error);
bool readBytes(const QString &path, QByteArray *bytes, QString *error);
bool decode(const QByteArray &bytes, QString *text, QString *error);
bool read(const QString &path, QString *text, QString *error);
}
