#pragma once
#include <QList>
#include <QString>
#include <QUrl>

namespace SmbShare {
struct Target { QUrl url; QString host; int port = -1; QString share; QString subdirectory; };
struct Mount { QString source; QString path; };
enum class Resolution { Unavailable, Ready, Ambiguous };
bool parse(const QString &input, Target *target, QString *error);
QList<Mount> mountedShares();
QString resolve(const Target &target, const QList<Mount> &mounts, QString *error, Resolution *resolution = nullptr);
}
