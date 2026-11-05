#include "UpdateChecker.h"
#include <QDebug>
#include <QSaveFile>
#include <QSysInfo>
#include <QVersionNumber>
#include "../common/AppPaths.h"

namespace {

int platformPackageScore(const QString &fileName) {
  const QString lower = fileName.toLower();

#ifdef Q_OS_WIN
  if (!lower.endsWith(QStringLiteral(".exe")))
    return -1;
  if (lower.contains(QStringLiteral("macos")) ||
      lower.contains(QStringLiteral("darwin")) ||
      lower.contains(QStringLiteral("linux")))
    return -1;
  return lower.contains(QStringLiteral("windows")) ||
                 lower.contains(QStringLiteral("win64"))
             ? 20
             : 10;
#elif defined(Q_OS_MACOS)
  if (!lower.endsWith(QStringLiteral(".zip")) ||
      (!lower.contains(QStringLiteral("macos")) &&
       !lower.contains(QStringLiteral("darwin"))))
    return -1;

  const QString arch = QSysInfo::currentCpuArchitecture().toLower();
  if (arch == QStringLiteral("arm64") || arch == QStringLiteral("aarch64")) {
    if (lower.contains(QStringLiteral("x86_64")) ||
        lower.contains(QStringLiteral("x64")))
      return -1;
    if (lower.contains(QStringLiteral("arm64")) ||
        lower.contains(QStringLiteral("aarch64")))
      return 30;
  } else if (arch == QStringLiteral("x86_64")) {
    if (lower.contains(QStringLiteral("arm64")) ||
        lower.contains(QStringLiteral("aarch64")))
      return -1;
    if (lower.contains(QStringLiteral("x86_64")) ||
        lower.contains(QStringLiteral("x64")))
      return 30;
  }
  if (lower.contains(QStringLiteral("universal")))
    return 20;
  return 10;
#else
  Q_UNUSED(lower);
  return -1;
#endif
}

QString currentUpdateTarget() {
#ifdef Q_OS_MACOS
  QDir appDir(QCoreApplication::applicationDirPath());
  if (!appDir.cdUp() || appDir.dirName() != QStringLiteral("Contents") ||
      !appDir.cdUp() || !appDir.dirName().endsWith(QStringLiteral(".app"),
                                                    Qt::CaseInsensitive))
    return {};
  return appDir.absolutePath();
#else
  return QCoreApplication::applicationFilePath();
#endif
}

} // namespace

// ============================================================
// 构造函数 & 基本访问器
// ============================================================

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent), m_checking(false), m_updateAvailable(false),
      m_downloading(false), m_downloadProgress(0.0),
      m_downloadComplete(false), m_verifying(false),
      m_verifySuccess(false), m_downloadResumeAvailable(false),
      m_cancelRequested(false), m_downloadReply(nullptr),
      m_downloadFile(nullptr), m_expectedDownloadSize(0),
      m_resumeOffset(0), m_downloadedBytes(0), m_lastReceivedBytes(0),
      m_downloadTimeoutTimer(nullptr), m_retryAttempts(0) {
  m_networkManager = new QNetworkAccessManager(this);
  m_downloadCacheRoot = QDir(AppPaths::writableDataRoot()).filePath(QStringLiteral("updates"));

#ifdef APP_VERSION
  m_currentVersion = QStringLiteral(APP_VERSION);
#else
  m_currentVersion = "1.0.0";
#endif
}

// --- 原有访问器 ---
bool UpdateChecker::isChecking() const { return m_checking; }
bool UpdateChecker::isUpdateAvailable() const { return m_updateAvailable; }
QString UpdateChecker::latestVersion() const { return m_latestVersion; }
QString UpdateChecker::releaseNotes() const { return m_releaseNotes; }
QString UpdateChecker::downloadUrl() const { return m_downloadUrl; }
bool UpdateChecker::automaticInstallAvailable() const {
  return platformPackageScore(m_downloadFileName) >= 0;
}
QString UpdateChecker::currentVersion() const { return m_currentVersion; }

// --- 新增访问器 ---
bool UpdateChecker::isDownloading() const { return m_downloading; }
double UpdateChecker::downloadProgress() const { return m_downloadProgress; }
bool UpdateChecker::isDownloadComplete() const { return m_downloadComplete; }
QString UpdateChecker::downloadErrorMsg() const { return m_downloadErrorMsg; }
bool UpdateChecker::isVerifying() const { return m_verifying; }
bool UpdateChecker::isVerifySuccess() const { return m_verifySuccess; }
bool UpdateChecker::isDownloadResumeAvailable() const {
  return m_downloadResumeAvailable;
}
qint64 UpdateChecker::downloadedBytes() const { return m_downloadedBytes; }
qint64 UpdateChecker::expectedDownloadBytes() const {
  return m_expectedDownloadSize;
}

// ============================================================
// 检查更新（原有逻辑，扩展：同时解析 .sha256 资产）
// ============================================================

void UpdateChecker::checkForUpdate() {
  if (m_checking || m_downloading)
    return;

  m_checking = true;
  // 重置下载状态
  m_downloading = false;
  m_downloadProgress = 0.0;
  m_downloadComplete = false;
  m_downloadErrorMsg.clear();
  m_verifying = false;
  m_verifySuccess = false;
  m_downloadResumeAvailable = false;
  m_cancelRequested = false;
  m_updateAvailable = false;
  m_downloadUrl.clear();
  m_latestVersion.clear();
  m_expectedSha256.clear();
  m_downloadedFilePath.clear();
  m_downloadFileName.clear();
  m_downloadEtag.clear();
  m_downloadedBytes = 0;
  m_expectedDownloadSize = 0;
  emit checkingChanged();
  emit downloadResumeChanged();
  emit downloadProgressChanged(0.0);

  QNetworkRequest request(m_manifestUrl);
  request.setTransferTimeout(15000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setHeader(QNetworkRequest::UserAgentHeader, "LogAnalyQt/" + m_currentVersion);

  QNetworkReply *reply = m_networkManager->get(request);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply]() { onReplyFinished(reply); });
}

void UpdateChecker::openDownloadUrl() {
  if (!m_downloadUrl.isEmpty()) {
    QDesktopServices::openUrl(QUrl(m_downloadUrl));
  }
}

void UpdateChecker::onReplyFinished(QNetworkReply *reply) {
  m_checking = false;
  emit checkingChanged();

  if (reply->error() != QNetworkReply::NoError) {
    emit checkError(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404
                        ? QStringLiteral("GitHub 尚无可用的正式发布版本")
                        : QString("检查 GitHub 更新失败: %1").arg(reply->errorString()));
    reply->deleteLater();
    return;
  }

  QByteArray data = reply->read(1024 * 1024 + 1);
  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
  QJsonObject obj = doc.object();
  reply->deleteLater();

  if (data.size() > 1024 * 1024 || parseError.error != QJsonParseError::NoError ||
      !QRegularExpression(QStringLiteral("^v[0-9]{1,6}\\.[0-9]{1,6}\\.[0-9]{1,6}$"))
           .match(obj["tag_name"].toString()).hasMatch() || !obj["assets"].isArray()) {
    emit checkError("获取版本信息失败: 无效的服务器响应");
    return;
  }

  m_latestVersion = obj["tag_name"].toString();
  // 去除可能的 'v' 前缀
  if (m_latestVersion.startsWith("v", Qt::CaseInsensitive)) {
    m_latestVersion = m_latestVersion.mid(1);
  }

  m_releaseNotes = obj["body"].toString();

  // 查找下载链接
  m_downloadUrl = "";
  m_expectedSha256 = "";
  m_downloadFileName = "";

  const QString releaseBase = QStringLiteral("https://github.com/jsnjjnzx/LogAnalyQt/releases/download/v%1/").arg(m_latestVersion);
#ifdef Q_OS_WIN
  const QString expectedName = QStringLiteral("TonglianWorkshop_v%1_Windows_x64.exe").arg(m_latestVersion);
#else
  const QString expectedName = QStringLiteral("TonglianWorkshop_v%1_macOS_arm64_update.zip").arg(m_latestVersion);
#endif
  int matches = 0;
  if (obj.contains("assets") && obj["assets"].isArray()) {
    const QJsonArray assets = obj["assets"].toArray();
    for (const auto &assetVal : assets) {
      const QJsonObject asset = assetVal.toObject();
      const QString name = asset["name"].toString();
      if (name == expectedName && platformPackageScore(name) >= 0) {
        ++matches;
        m_downloadUrl = asset["browser_download_url"].toString();
        m_downloadFileName = name;
        m_expectedSha256 = asset["sha256"].toString().toLower();
      }
    }

  }
  if (matches != 1 || m_downloadUrl != releaseBase + expectedName ||
      !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(m_expectedSha256).hasMatch()) {
    m_downloadUrl.clear();
    m_downloadFileName.clear();
    emit checkError(QStringLiteral("GitHub 更新信息缺少本平台的有效更新包或 SHA256 校验值"));
    return;
  }

  m_updateAvailable = isNewerVersion(m_latestVersion, m_currentVersion);
  if (m_updateAvailable) loadResumeState();
  else clearResumeFiles();
  emit updateResultReady();
}

// ============================================================
// 下载更新文件
// ============================================================

QString UpdateChecker::updateTempDir() const {
  return m_tempDirectory.path();
}

QString UpdateChecker::downloadCacheDir() const {
  return m_downloadCacheRoot;
}

QString UpdateChecker::partialDownloadPath() const {
  return QDir(downloadCacheDir()).filePath(m_downloadFileName + ".part");
}

QString UpdateChecker::finalDownloadPath() const {
  return QDir(downloadCacheDir()).filePath(m_downloadFileName);
}

QString UpdateChecker::downloadMetadataPath() const {
  return QDir(downloadCacheDir()).filePath(m_downloadFileName + ".meta.json");
}

bool UpdateChecker::saveResumeMetadata() {
  if (m_downloadFileName.isEmpty() || downloadCacheDir().isEmpty()) return false;
  if (!QDir().mkpath(downloadCacheDir())) return false;
  QSaveFile file(downloadMetadataPath());
  if (!file.open(QIODevice::WriteOnly)) return false;
  const QJsonObject metadata{
      {"name", m_downloadFileName},
      {"url", m_downloadUrl},
      {"sha256", m_expectedSha256},
      {"size", m_expectedDownloadSize},
      {"etag", m_downloadEtag},
  };
  if (file.write(QJsonDocument(metadata).toJson(QJsonDocument::Compact)) < 0 ||
      !file.commit())
    return false;
  return true;
}

bool UpdateChecker::loadResumeState() {
  const bool previous = m_downloadResumeAvailable;
  m_downloadResumeAvailable = false;
  m_downloadComplete = false;
  m_verifySuccess = false;
  m_downloadedBytes = 0;
  m_downloadProgress = 0.0;
  m_expectedDownloadSize = 0;
  m_resumeOffset = 0;
  m_downloadEtag.clear();
  if (!m_updateAvailable || m_downloadFileName.isEmpty() ||
      downloadCacheDir().isEmpty() || m_expectedSha256.isEmpty()) {
    if (previous) emit downloadResumeChanged();
    return false;
  }

  const QFileInfo finalInfo(finalDownloadPath());
  if (finalInfo.isFile() && finalInfo.size() > 0 &&
      verifySha256(finalInfo.absoluteFilePath(), m_expectedSha256)) {
    m_downloadedFilePath = finalInfo.absoluteFilePath();
    m_downloadedBytes = m_expectedDownloadSize = finalInfo.size();
    m_downloadProgress = 1.0;
    m_downloadComplete = true;
    m_verifySuccess = true;
    if (previous) emit downloadResumeChanged();
    emit downloadProgressChanged(1.0);
    emit verifyFinished(true);
    emit downloadFinished();
    return false;
  }
  if (finalInfo.exists() && !QFile::remove(finalInfo.absoluteFilePath()))
    return false;

  bool metadataValid = false;
  QFile metadataFile(downloadMetadataPath());
  if (metadataFile.open(QIODevice::ReadOnly)) {
    const auto metadata =
        QJsonDocument::fromJson(metadataFile.read(64 * 1024)).object();
    if (metadata.value("name").toString() == m_downloadFileName &&
        metadata.value("url").toString() == m_downloadUrl &&
        metadata.value("sha256").toString() == m_expectedSha256) {
      m_expectedDownloadSize =
          qMax<qint64>(0, metadata.value("size").toVariant().toLongLong());
      m_downloadEtag = metadata.value("etag").toString();
      metadataValid = true;
    } else {
      metadataFile.close();
      clearResumeFiles();
      return false;
    }
  }
  if (!metadataValid) {
    QFile::remove(partialDownloadPath());
    if (previous) emit downloadResumeChanged();
    return false;
  }

  const QFileInfo partialInfo(partialDownloadPath());
  if (!partialInfo.isFile() || partialInfo.size() <= 0) {
    QFile::remove(downloadMetadataPath());
    if (previous) emit downloadResumeChanged();
    return false;
  }
  m_resumeOffset = partialInfo.size();
  m_downloadedBytes = m_resumeOffset;
  if (m_expectedDownloadSize > 0 &&
      m_downloadedBytes > m_expectedDownloadSize) {
    QFile::remove(partialInfo.absoluteFilePath());
    QFile::remove(downloadMetadataPath());
    m_resumeOffset = m_downloadedBytes = 0;
    if (previous) emit downloadResumeChanged();
    return false;
  }
  m_downloadResumeAvailable = true;
  if (m_expectedDownloadSize > 0)
    m_downloadProgress =
        qMin(0.999, static_cast<double>(m_downloadedBytes) /
                        static_cast<double>(m_expectedDownloadSize));
  emit downloadProgressChanged(m_downloadProgress);
  emit downloadResumeChanged();
  return true;
}

void UpdateChecker::clearResumeFiles() {
  if (m_downloadFileName.isEmpty()) return;
  QFile::remove(partialDownloadPath());
  QFile::remove(downloadMetadataPath());
  QFile::remove(finalDownloadPath());
  m_downloadResumeAvailable = false;
  m_resumeOffset = 0;
  m_downloadedBytes = 0;
  m_downloadProgress = 0.0;
  m_downloadEtag.clear();
  emit downloadResumeChanged();
  emit downloadProgressChanged(0.0);
}

void UpdateChecker::downloadUpdate() {
  if (m_downloading || !m_updateAvailable || m_downloadUrl.isEmpty() ||
      m_downloadComplete)
    return;

  if (platformPackageScore(m_downloadFileName) < 0) {
    openDownloadUrl();
    return;
  }
  if (!QDir().mkpath(downloadCacheDir())) {
    m_downloadErrorMsg = QStringLiteral("无法创建更新下载目录");
    emit downloadErrorOccurred(m_downloadErrorMsg);
    return;
  }

  m_cancelRequested = false;
  m_retryAttempts = 0;
  loadResumeState();
  if (m_downloadComplete) return;
  m_downloading = true;
  m_downloadComplete = false;
  m_downloadErrorMsg.clear();
  m_verifying = false;
  m_verifySuccess = false;
  m_lastReceivedBytes = 0;
  emit downloadingChanged();
  emit downloadProgressChanged(m_downloadProgress);
  beginDownloadAttempt();
}

void UpdateChecker::beginDownloadAttempt() {
  if (!m_downloading || m_cancelRequested || m_downloadUrl.isEmpty()) return;

  if (m_downloadReply) {
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;
    reply->abort();
    reply->deleteLater();
  }
  if (m_downloadFile) {
    m_downloadFile->close();
    delete m_downloadFile;
    m_downloadFile = nullptr;
  }

  const QString partialPath = partialDownloadPath();
  qint64 offset = QFileInfo(partialPath).size();
  if (offset < 0) offset = 0;
  if (m_expectedDownloadSize > 0 && offset > m_expectedDownloadSize) {
    QFile::remove(partialPath);
    offset = 0;
  }
  m_resumeOffset = offset;
  m_downloadedBytes = offset;
  m_lastReceivedBytes = 0;
  m_downloadFile = new QFile(partialPath, this);
  const auto mode = offset > 0 ? QIODevice::WriteOnly | QIODevice::Append
                               : QIODevice::WriteOnly | QIODevice::Truncate;
  if (!m_downloadFile->open(mode)) {
    delete m_downloadFile;
    m_downloadFile = nullptr;
    failDownload(QStringLiteral("无法写入更新临时文件，请检查磁盘空间和权限"), false);
    return;
  }
  saveResumeMetadata();

  QNetworkRequest request{QUrl(m_downloadUrl)};
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    "LogAnalyQt/" + m_currentVersion);
  request.setRawHeader("Accept", "application/octet-stream");
  if (offset > 0) {
    request.setRawHeader("Range", "bytes=" + QByteArray::number(offset) + "-");
    if (!m_downloadEtag.isEmpty())
      request.setRawHeader("If-Range", m_downloadEtag.toUtf8());
  }
  m_downloadReply = m_networkManager->get(request);
  m_downloadReply->setReadBufferSize(0);

  // A hidden page must not turn a temporary network stall into a full restart.
  if (!m_downloadTimeoutTimer) {
    m_downloadTimeoutTimer = new QTimer(this);
    m_downloadTimeoutTimer->setSingleShot(true);
    connect(m_downloadTimeoutTimer, &QTimer::timeout, this, [this]() {
      if (!m_downloading || m_cancelRequested) return;
      if (m_downloadReply)
        failDownload(QStringLiteral("下载连接长时间无响应，正在尝试续传"), true);
      else
        beginDownloadAttempt();
    });
  }
  m_downloadTimeoutTimer->start(30000);

  connect(m_downloadReply, &QNetworkReply::redirected, this, [this]() {
    if (m_downloadTimeoutTimer) m_downloadTimeoutTimer->start(30000);
  });

  // 监控元数据（收到 HTTP 响应头）
  connect(m_downloadReply, &QNetworkReply::metaDataChanged, this, [this]() {
    if (!m_downloadReply || !m_downloadFile) return;
    const int status = m_downloadReply
                           ->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                           .toInt();
    if (status >= 200 && status < 300 && status != 206 && m_resumeOffset > 0) {
      m_downloadFile->close();
      if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        failDownload(QStringLiteral("无法重置更新临时文件"), false);
        return;
      }
      m_resumeOffset = 0;
      m_downloadedBytes = 0;
      m_downloadProgress = 0.0;
      emit downloadProgressChanged(0.0);
    }
    const QByteArray contentRange =
        m_downloadReply->rawHeader("content-range");
    qint64 rangeStart = -1, total = 0;
    if (status == 206) {
      const QRegularExpressionMatch match =
          QRegularExpression(QStringLiteral("^bytes\\s+(\\d+)-\\d+/(\\d+)$"),
                             QRegularExpression::CaseInsensitiveOption)
              .match(QString::fromLatin1(contentRange));
      if (match.hasMatch()) {
        rangeStart = match.captured(1).toLongLong();
        total = match.captured(2).toLongLong();
      }
      if (rangeStart != m_resumeOffset || total <= 0) {
        failDownload(QStringLiteral("服务器返回的续传位置不一致"), false);
        return;
      }
      m_expectedDownloadSize = total;
    } else if (status >= 200 && status < 300) {
      const qint64 length =
          m_downloadReply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
      m_expectedDownloadSize = qMax<qint64>(0, length);
    } else if (status == 416) {
      m_downloadFile->close();
      if (!QFile::remove(partialDownloadPath())) {
        failDownload(QStringLiteral("无法重置更新临时文件"), false);
        return;
      }
      m_expectedDownloadSize = 0;
      m_downloadEtag.clear();
      m_resumeOffset = m_downloadedBytes = 0;
      m_downloadProgress = 0.0;
      failDownload(QStringLiteral("服务器拒绝了续传范围，正在重新下载"), true);
      return;
    }
    m_downloadEtag =
        QString::fromLatin1(m_downloadReply->rawHeader("etag")).trimmed();
    saveResumeMetadata();
  });

  // 连接下载进度信号
  // 重定向后可能不返回 Content-Length。
  connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
          [this](qint64 received, qint64) {
            if (received > m_lastReceivedBytes && m_downloadTimeoutTimer) {
              m_downloadTimeoutTimer->start(30000);
              m_lastReceivedBytes = received;
            }
            m_downloadedBytes = m_resumeOffset + received;
            if (m_expectedDownloadSize > 0) {
              m_downloadProgress =
                  qMin(1.0, static_cast<double>(m_downloadedBytes) /
                                static_cast<double>(m_expectedDownloadSize));
              emit downloadProgressChanged(m_downloadProgress);
            }
          });

  // 监控网络错误（不等 finished，立即记录）
  connect(m_downloadReply, &QNetworkReply::errorOccurred, this,
          [](QNetworkReply::NetworkError code) {
            qDebug() << "[UpdateChecker] 网络错误:" << code;
          });

  // 收到数据时增量写入文件，避免内部缓冲区堆积导致下载停滞
  connect(m_downloadReply, &QIODevice::readyRead, this, [this]() {
    if (!m_downloadFile || !m_downloadReply) return;
    const int status = m_downloadReply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200 && status != 206) return;
    const QByteArray chunk = m_downloadReply->readAll();
    if (!chunk.isEmpty() && m_retryAttempts != 0) m_retryAttempts = 0;
    if (m_downloadFile->write(chunk) != chunk.size() ||
        !m_downloadFile->flush())
      failDownload(QStringLiteral("写入更新包失败，请检查磁盘空间和权限"), false);
  });

  // 连接下载完成信号
  connect(m_downloadReply, &QNetworkReply::finished, this, [this]() {
    if (!m_downloadReply) return;
    if (m_downloadTimeoutTimer) m_downloadTimeoutTimer->stop();
    const auto error = m_downloadReply->error();
    const int status =
        m_downloadReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (error != QNetworkReply::NoError || (status != 200 && status != 206)) {
      const QString message =
          error == QNetworkReply::OperationCanceledError
              ? QStringLiteral("下载连接被中断，正在尝试续传")
              : QStringLiteral("下载失败：%1").arg(m_downloadReply->errorString());
      failDownload(message, true);
      return;
    }

    if (m_downloadFile) {
      const QByteArray remaining = m_downloadReply->readAll();
      const bool written =
          m_downloadFile->write(remaining) == remaining.size() &&
          m_downloadFile->flush();
      const qint64 actualSize = m_downloadFile->size();
      m_downloadFile->close();
      delete m_downloadFile;
      m_downloadFile = nullptr;
      if (!written) {
        failDownload(QStringLiteral("写入更新包失败，请检查磁盘空间和权限"), false);
        return;
      }
      if (m_expectedDownloadSize > 0 &&
          actualSize != m_expectedDownloadSize) {
        failDownload(QStringLiteral("更新包下载不完整，正在继续下载"), true);
        return;
      }
    }

    m_downloadReply->deleteLater();
    m_downloadReply = nullptr;
    const QString partialPath = partialDownloadPath();
    const QString finalPath = finalDownloadPath();
    QFile::remove(finalPath);
    if (!QFile::rename(partialPath, finalPath)) {
      failDownload(QStringLiteral("无法保存完整更新包"), false);
      return;
    }
    QFile::remove(downloadMetadataPath());
    m_downloadedFilePath = finalPath;
    m_downloadedBytes = m_expectedDownloadSize = QFileInfo(finalPath).size();
    m_downloadProgress = 1.0;
    m_downloading = false;
    m_downloadResumeAvailable = false;
    emit downloadingChanged();
    emit downloadResumeChanged();
    emit downloadProgressChanged(1.0);
    downloadSha256AndVerify();
  });
}

void UpdateChecker::failDownload(const QString &message, bool retryable) {
  if (m_downloadTimeoutTimer) m_downloadTimeoutTimer->stop();
  if (m_downloadReply) {
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;
    reply->abort();
    reply->deleteLater();
  }
  if (m_downloadFile) {
    m_downloadFile->flush();
    m_downloadFile->close();
    delete m_downloadFile;
    m_downloadFile = nullptr;
  }
  m_downloadedBytes = qMax<qint64>(0, QFileInfo(partialDownloadPath()).size());
  m_downloadResumeAvailable = m_downloadedBytes > 0 && !m_cancelRequested;
  if (m_expectedDownloadSize > 0)
    m_downloadProgress =
        qMin(0.999, static_cast<double>(m_downloadedBytes) /
                        static_cast<double>(m_expectedDownloadSize));
  saveResumeMetadata();
  emit downloadResumeChanged();
  emit downloadProgressChanged(m_downloadProgress);

  if (retryable && !m_cancelRequested && m_retryAttempts < 3) {
    ++m_retryAttempts;
    m_downloadTimeoutTimer->start(1000);
    return;
  }

  m_downloading = false;
  m_downloadErrorMsg =
      m_downloadResumeAvailable
          ? message + QStringLiteral("；已保留已下载内容，可稍后继续")
          : message;
  emit downloadingChanged();
  emit downloadErrorOccurred(m_downloadErrorMsg);
}

void UpdateChecker::cancelDownload() {
  m_cancelRequested = true;
  if (m_downloadTimeoutTimer) m_downloadTimeoutTimer->stop();
  if (m_downloadReply) {
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;
    reply->abort();
    reply->deleteLater();
  }
  if (m_downloadFile) {
    m_downloadFile->close();
    delete m_downloadFile;
    m_downloadFile = nullptr;
  }
  clearResumeFiles();
  if (!m_downloadedFilePath.isEmpty()) {
    QFile::remove(m_downloadedFilePath);
    m_downloadedFilePath.clear();
  }
  m_downloading = false;
  m_downloadComplete = false;
  m_downloadErrorMsg.clear();
  emit downloadingChanged();
}

// ============================================================
// SHA256 校验
// ============================================================

void UpdateChecker::downloadSha256AndVerify() {
  m_verifying = true;
  emit verifyingChanged();

  if (m_expectedSha256.isEmpty()) {
    m_verifying = false;
    m_verifySuccess = false;
    m_downloadComplete = false;
    m_downloadErrorMsg =
        "发布包缺少 SHA256 校验值，已拒绝自动安装。请前往发布页面手动下载。";
    emit verifyingChanged();
    emit verifyFinished(false);
    emit downloadErrorOccurred(m_downloadErrorMsg);
    return;
  }

  finishVerification(m_expectedSha256);
}

void UpdateChecker::finishVerification(const QString &expectedHash) {
  const bool ok = verifySha256(m_downloadedFilePath, expectedHash);
  m_verifySuccess = ok;
  m_downloadComplete = ok;
  m_verifying = false;

  emit verifyingChanged();
  emit verifyFinished(ok);

  if (ok) {
    emit downloadFinished();
  } else {
    m_downloadErrorMsg = "SHA256 校验失败，文件可能已损坏或被篡改";
    emit downloadErrorOccurred(m_downloadErrorMsg);
    // 删除损坏的文件
    QFile::remove(m_downloadedFilePath);
    m_downloadedFilePath.clear();
  }
}

bool UpdateChecker::verifySha256(const QString &filePath,
                                 const QString &expectedHash) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }

  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file))
    return false;
  file.close();

  QString actualHash = hash.result().toHex().toLower();
  return actualHash == expectedHash.toLower();
}

// ============================================================
// 启动 updater 并退出主程序
// ============================================================

// 嵌入的 updater.exe 数据（由构建系统自动生成 updater_resource.cpp）
#ifdef Q_OS_WIN
extern const unsigned char updater_exe_data[];
extern const size_t updater_exe_size;
#endif

QString UpdateChecker::findUpdaterPath() const {
#ifdef Q_OS_WIN
  // 从嵌入资源释放到临时目录
  QString tempDir = updateTempDir();
  QString extractedPath = tempDir + "/updater.exe";

  // 释放嵌入的 updater.exe
  QFile file(extractedPath);
  if (!file.open(QIODevice::WriteOnly)) {
    return QString();
  }
  if (file.write(reinterpret_cast<const char *>(updater_exe_data),
                 static_cast<qint64>(updater_exe_size)) != static_cast<qint64>(updater_exe_size) || !file.flush())
    return {};
  file.close();

  return extractedPath;
#elif defined(Q_OS_MACOS)
  QDir contentsDir(QCoreApplication::applicationDirPath());
  if (!contentsDir.cdUp())
    return {};
  const QString updaterPath =
      contentsDir.filePath(QStringLiteral("Helpers/LogAnalyQtUpdater"));
  const QFileInfo updaterInfo(updaterPath);
  return updaterInfo.isFile() && updaterInfo.isExecutable() ? updaterPath
                                                             : QString();
#else
  return {};
#endif
}

bool UpdateChecker::installUpdate() {
  if (m_downloadedFilePath.isEmpty() || !m_downloadComplete || !m_verifySuccess ||
      !verifySha256(m_downloadedFilePath, m_expectedSha256)) {
    m_downloadErrorMsg = "尚未完成下载或校验，无法安装";
    emit downloadErrorOccurred(m_downloadErrorMsg);
    return false;
  }

  QString updaterPath = findUpdaterPath();

  if (updaterPath.isEmpty()) {
    // 如果找不到平台更新器，降级为打开文件所在目录
    m_downloadErrorMsg = "未找到自动更新器，无法自动更新。"
                         "已下载的文件位于: " +
                         m_downloadedFilePath;
    emit downloadErrorOccurred(m_downloadErrorMsg);
    // 打开临时目录让用户手动替换
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(m_downloadedFilePath).absolutePath()));
    return false;
  }

  // 构造 updater 命令行参数
  QStringList args;
  args << "--pid"
       << QString::number(QCoreApplication::applicationPid());
  args << "--source" << QDir::toNativeSeparators(m_downloadedFilePath);
  const QString targetPath = currentUpdateTarget();
  if (targetPath.isEmpty()) {
    m_downloadErrorMsg = "无法确定当前程序的安装位置，已取消自动更新";
    emit downloadErrorOccurred(m_downloadErrorMsg);
    return false;
  }
  args << "--target" << QDir::toNativeSeparators(targetPath);
#ifdef Q_OS_MACOS
  args << "--data-root" << AppPaths::writableDataRoot()
       << "--log-root" << AppPaths::logDirectory();
#endif
  args << "--launch";

  // 启动 updater 进程（detached，不随主程序退出而终止）
  bool started = QProcess::startDetached(updaterPath, args);
  if (!started) {
    m_downloadErrorMsg = "启动自动更新器失败";
    emit downloadErrorOccurred(m_downloadErrorMsg);
    return false;
  }

  m_tempDirectory.setAutoRemove(false);
  return true;
}

// ============================================================
// 版本比较
// ============================================================

bool UpdateChecker::isNewerVersion(const QString &remote,
                                   const QString &local) {
  return QVersionNumber::compare(QVersionNumber::fromString(remote),
                                 QVersionNumber::fromString(local)) > 0;
}
