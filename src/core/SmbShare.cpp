#include "SmbShare.h"
#include <QDir>
#include <QFileInfo>
#include <vector>
#ifdef Q_OS_MACOS
#include <sys/mount.h>
#endif

namespace SmbShare {
bool parse(const QString &input, Target *target, QString *error) {
  auto fail = [&] { if (error) *error = QStringLiteral("请输入不含账号、密码、查询或片段的 smb://主机/共享/目录 地址"); return false; };
  const QUrl url(input, QUrl::StrictMode);
  if (!url.isValid() || url.scheme().compare("smb", Qt::CaseInsensitive) ||
      url.host().isEmpty() || !url.userInfo().isEmpty() ||
      url.authority().contains('@') || url.hasQuery() || url.hasFragment() ||
      input.contains('\\') || input.contains(QChar::Null) || url.port() == 0) return fail();
  for (const auto character : input) if (character.unicode() < 32 || character.unicode() == 127) return fail();
  QStringList parts;
  for (const auto &part : url.path(QUrl::FullyEncoded).split('/', Qt::KeepEmptyParts))
    parts.append(QUrl::fromPercentEncoding(part.toUtf8()));
  if (parts.size() < 2 || parts[0] != "" || parts[1].isEmpty()) return fail();
  for (int i = 1; i < parts.size(); ++i) {
    const auto &part = parts[i];
    if (part == "." || part == ".." || part.contains('\\') || part.contains('/') || part.contains(QChar::Null) ||
        (part.isEmpty() && i != parts.size() - 1)) return fail();
  }
  target->url = url; target->host = url.host().toLower(); target->port = url.port(445);
  target->share = parts[1]; target->subdirectory = parts.mid(2).join('/');
  return true;
}

QList<Mount> mountedShares() {
  QList<Mount> result;
#ifdef Q_OS_MACOS
  const int capacity = getfsstat(nullptr, 0, MNT_NOWAIT);
  if (capacity <= 0) return result;
  std::vector<struct statfs> mounts(capacity);
  const int count = qMin(capacity, getfsstat(mounts.data(), static_cast<int>(mounts.size() * sizeof(struct statfs)), MNT_NOWAIT));
  for (int i = 0; i < count; ++i) {
    if (QByteArray(mounts[i].f_fstypename) == "smbfs")
      result.append({QString::fromUtf8(mounts[i].f_mntfromname),
                     QString::fromUtf8(mounts[i].f_mntonname)});
  }
#endif
  return result;
}

QString resolve(const Target &target, const QList<Mount> &mounts, QString *error, Resolution *resolution) {
  if (resolution) *resolution = Resolution::Unavailable;
  QString mountedPath;
  for (const auto &mount : mounts) {
    const QUrl source("smb:" + mount.source, QUrl::StrictMode);
    const auto parts = source.path(QUrl::FullyDecoded).split('/', Qt::SkipEmptyParts);
    if (!source.isValid() || source.host().compare(target.host, Qt::CaseInsensitive) ||
        source.port(445) != target.port || parts.size() != 1 || parts[0] != target.share) continue;
    const QString candidate = QDir::cleanPath(mount.path);
    if (candidate.isEmpty() || !QDir::isAbsolutePath(candidate)) continue;
    if (!mountedPath.isEmpty() && mountedPath != candidate) {
      if (resolution) *resolution = Resolution::Ambiguous;
      if (error) *error = QStringLiteral("同一 SMB 主机和共享存在多个系统挂载，无法确定同步目标");
      return {};
    }
    mountedPath = candidate;
  }
  const QString root = mountedPath.isEmpty() ? QString() : QFileInfo(mountedPath).canonicalFilePath();
  if (!root.isEmpty()) {
    const QFileInfo directory(QDir(root).filePath(target.subdirectory));
    const QString resolved = directory.canonicalFilePath();
    if (directory.isDir() && !resolved.isEmpty() &&
        (resolved == root || resolved.startsWith(root + '/'))) {
      if (resolution) *resolution = Resolution::Ready;
      if (error) error->clear();
      return resolved;
    }
  }
  if (error) *error = QStringLiteral("尚未找到与 SMB 主机、共享匹配且目录可访问的系统挂载");
  return {};
}
}
