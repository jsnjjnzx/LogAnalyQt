#include "UpdateChecker.h"
#include <QDebug>
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
      m_verifySuccess(false), m_downloadReply(nullptr),
      m_downloadFile(nullptr), m_expectedDownloadSize(0),
      m_lastReceivedBytes(0), m_downloadTimeoutTimer(nullptr) {
  m_networkManager = new QNetworkAccessManager(this);

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
  m_updateAvailable = false;
  m_downloadUrl.clear();
  m_latestVersion.clear();
  m_expectedSha256.clear();
  m_downloadedFilePath.clear();
  m_downloadFileName.clear();
  emit checkingChanged();

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
  const QString expectedName = QStringLiteral("LogAnalyQt_v%1_Windows_x64.exe").arg(m_latestVersion);
#else
  const QString expectedName = QStringLiteral("LogAnalyQt_v%1_macOS_arm64_update.zip").arg(m_latestVersion);
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
  emit updateResultReady();
}

// ============================================================
// 下载更新文件
// ============================================================

QString UpdateChecker::updateTempDir() const {
  return m_tempDirectory.path();
}

void UpdateChecker::downloadUpdate() {
  if (m_downloading || !m_updateAvailable || m_downloadUrl.isEmpty())
    return;

  // 当前平台没有可自动安装的资产时，降级为浏览器打开 Release 页面。
  if (platformPackageScore(m_downloadFileName) < 0) {
    openDownloadUrl();
    return;
  }

  m_downloading = true;
  m_downloadProgress = 0.0;
  m_downloadComplete = false;
  m_downloadErrorMsg.clear();
  m_verifying = false;
  m_verifySuccess = false;
  m_lastReceivedBytes = 0;
  emit downloadingChanged();
  emit downloadProgressChanged(0.0);

  // 确定下载保存路径
  QString downloadDir = updateTempDir();
  if (downloadDir.isEmpty()) {
    m_downloading = false;
    emit downloadingChanged();
    emit downloadErrorOccurred(QStringLiteral("无法创建安全的更新临时目录"));
    return;
  }
  QString savePath = downloadDir + "/" + m_downloadFileName;

  // 检查 download 目录中是否有无法删除的旧文件
  QDir dir(downloadDir);
  if (dir.exists()) {
    QStringList oldFiles = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &oldFile : oldFiles) {
      QString oldFilePath = downloadDir + "/" + oldFile;
      // 尝试检查文件是否可删除（通过检查写权限）
      QFile testFile(oldFilePath);
      if (!testFile.open(QIODevice::ReadWrite)) {
        qWarning() << "[UpdateChecker] 警告：发现无法覆盖的文件:" << oldFilePath
                   << "错误:" << testFile.errorString();
        m_downloadErrorMsg = QString("download 目录中存在被占用的文件，可能被杀毒软件锁定。\n"
                                      "请检查并手动删除: %1\n\n"
                                      "提示：如果刚下载失败，请等待几秒后重试。").arg(oldFilePath);
        emit downloadErrorOccurred(m_downloadErrorMsg);
        m_downloading = false;
        emit downloadingChanged();
        return;
      }
      testFile.close();
    }
  }

  // 如果已存在旧的临时文件，先删除
  if (QFile::exists(savePath)) {
    qDebug() << "[UpdateChecker] 删除旧的临时文件:" << savePath;
    if (!QFile::remove(savePath)) {
      m_downloadErrorMsg = QString("无法删除旧的临时文件，请手动删除后重试: %1").arg(savePath);
      emit downloadErrorOccurred(m_downloadErrorMsg);
      m_downloading = false;
      emit downloadingChanged();
      return;
    }
  }

  qDebug() << "[UpdateChecker] 开始下载:" << m_downloadUrl;
  qDebug() << "[UpdateChecker] 保存路径:" << savePath;

  QNetworkRequest request{QUrl(m_downloadUrl)};
  // GitHub 附件会重定向到下载服务器。
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  // 设置 User-Agent，部分 CDN 会拒绝无 UA 的请求或限速
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    "LogAnalyQt/" + m_currentVersion);
  // 明确请求二进制流，避免服务端返回网页内容
  request.setRawHeader("Accept", "application/octet-stream");

  // 提前打开文件，后续通过 readyRead 增量写入，避免大文件内存缓冲导致卡死
  m_downloadFile = new QFile(savePath, this);
  if (!m_downloadFile->open(QIODevice::WriteOnly)) {
    m_downloadErrorMsg = QString("无法写入临时文件: %1").arg(savePath);
    emit downloadErrorOccurred(m_downloadErrorMsg);
    m_downloading = false;
    emit downloadingChanged();
    delete m_downloadFile;
    m_downloadFile = nullptr;
    return;
  }

  m_downloadReply = m_networkManager->get(request);
  // 不限制内部读缓冲区大小，确保数据不会因缓冲区满而停滞
  m_downloadReply->setReadBufferSize(0);
  qDebug() << "[UpdateChecker] QNetworkReply 已创建";

  // 重要：在连接任何信号之前重置状态，避免竞态条件
  m_expectedDownloadSize = 0;
  m_lastReceivedBytes = 0;

  // 创建下载超时定时器（30秒无数据则超时）
  if (!m_downloadTimeoutTimer) {
    m_downloadTimeoutTimer = new QTimer(this);
    m_downloadTimeoutTimer->setSingleShot(true);
    connect(m_downloadTimeoutTimer, &QTimer::timeout, this, [this]() {
      if (m_downloading && m_downloadReply) {
        qDebug() << "[UpdateChecker] 下载超时：30秒内无数据传输";
        m_downloadErrorMsg = "下载超时，服务器响应过慢，请稍后重试";
        emit downloadErrorOccurred(m_downloadErrorMsg);
        cancelDownload();
      }
    });
  }
  m_downloadTimeoutTimer->start(30000);  // 30秒超时

  // 监控重定向
  connect(m_downloadReply, &QNetworkReply::redirected, this,
          [this](const QUrl &url) {
            qDebug() << "[UpdateChecker] 重定向到:" << url.toString();
            // 重定向时重置超时计时器
            if (m_downloadTimeoutTimer) {
              m_downloadTimeoutTimer->start(30000);
            }
          });

  // 监控元数据（收到 HTTP 响应头）
  connect(m_downloadReply, &QNetworkReply::metaDataChanged, this, [this]() {
    if (m_downloadReply) {
      int statusCode = m_downloadReply->attribute(
          QNetworkRequest::HttpStatusCodeAttribute).toInt();
      qint64 contentLength = m_downloadReply->header(
          QNetworkRequest::ContentLengthHeader).toLongLong();
      QString contentType = m_downloadReply->header(
          QNetworkRequest::ContentTypeHeader).toString();
      qDebug() << "[UpdateChecker] HTTP状态码:" << statusCode
               << "Content-Length:" << contentLength
               << "Content-Type:" << contentType;

      // 记录 Content-Length，用于处理后续重定向可能丢失该头的情况
      if (contentLength > 0) {
        m_expectedDownloadSize = contentLength;
        qDebug() << "[UpdateChecker] 记录文件总大小:" << contentLength << "字节";
      }

      // 列出所有响应头
      for (const auto &header : m_downloadReply->rawHeaderList()) {
        qDebug() << "[UpdateChecker]   Header:" << header << "="
                 << m_downloadReply->rawHeader(header);
      }
    }
  });

  // 连接下载进度信号
  // 重定向后可能不返回 Content-Length。
  connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
          [this](qint64 received, qint64 total) {
            // 只要有数据传输，就重置超时计时器
            if (received > m_lastReceivedBytes && m_downloadTimeoutTimer) {
              m_downloadTimeoutTimer->start(30000);
              m_lastReceivedBytes = received;
            }

            qDebug() << "[UpdateChecker] 进度:" << received << "/" << total
                     << (total > 0 ? QString("%1%").arg(received * 100 / total) : "unknown");

            if (total > 0) {
              // 正常情况：有 Content-Length
              m_expectedDownloadSize = total;
              m_downloadProgress =
                  static_cast<double>(received) / static_cast<double>(total);
              emit downloadProgressChanged(m_downloadProgress);
            } else if (m_expectedDownloadSize > 0) {
              // 重定向后 Content-Length 丢失，使用之前记录的大小估算进度
              // 但进度最多显示到 95%，避免误导用户
              double estimatedProgress = qMin(0.95, static_cast<double>(received) / static_cast<double>(m_expectedDownloadSize));
              m_downloadProgress = estimatedProgress;
              emit downloadProgressChanged(m_downloadProgress);
            } else {
              // 完全不知道总大小时，显示已下载的 MB 数（但不更新进度条，避免跳跃）
              qDebug() << "[UpdateChecker] 已下载:" << (received / 1024.0 / 1024.0) << "MB (总大小未知)";
            }
          });

  // 监控网络错误（不等 finished，立即记录）
  connect(m_downloadReply, &QNetworkReply::errorOccurred, this,
          [](QNetworkReply::NetworkError code) {
            qDebug() << "[UpdateChecker] 网络错误:" << code;
          });

  // 收到数据时增量写入文件，避免内部缓冲区堆积导致下载停滞
  connect(m_downloadReply, &QIODevice::readyRead, this, [this]() {
    if (m_downloadFile && m_downloadReply) {
      QByteArray chunk = m_downloadReply->readAll();
      qDebug() << "[UpdateChecker] readyRead: 收到" << chunk.size() << "字节,"
               << "已写入文件:" << m_downloadFile->size();
      if (m_downloadFile->write(chunk) != chunk.size() || !m_downloadFile->flush()) {
        m_downloadReply->abort();
        emit downloadErrorOccurred(QStringLiteral("写入更新包失败，请检查磁盘空间和权限"));
      }
    }
  });

  // 连接下载完成信号
  connect(m_downloadReply, &QNetworkReply::finished, this, [this, savePath]() {
    qDebug() << "[UpdateChecker] finished 信号触发";

    // 停止超时定时器
    if (m_downloadTimeoutTimer) {
      m_downloadTimeoutTimer->stop();
    }

    if (!m_downloadReply) {
      qDebug() << "[UpdateChecker] m_downloadReply 为空（已被取消）";
      return; // 已被取消
    }

    qDebug() << "[UpdateChecker] 完成状态: error=" << m_downloadReply->error()
             << "errorString=" << m_downloadReply->errorString()
             << "HTTP状态码=" << m_downloadReply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt()
             << "最终URL=" << m_downloadReply->url().toString();

    if (m_downloadReply->error() != QNetworkReply::NoError) {
      // 若是用户主动取消（OperationCanceledError），不报错
      if (m_downloadReply->error() !=
          QNetworkReply::OperationCanceledError) {
        m_downloadErrorMsg =
            QString("下载失败: %1").arg(m_downloadReply->errorString());
        emit downloadErrorOccurred(m_downloadErrorMsg);
      }
      m_downloading = false;
      emit downloadingChanged();
      if (m_downloadFile) {
        m_downloadFile->close();
        m_downloadFile->remove();
        delete m_downloadFile;
        m_downloadFile = nullptr;
      }
      m_downloadReply->deleteLater();
      m_downloadReply = nullptr;
      return;
    }

    // 写入剩余数据并关闭文件
    if (m_downloadFile) {
      const QByteArray remaining = m_downloadReply->readAll();
      const bool written = m_downloadFile->write(remaining) == remaining.size() && m_downloadFile->flush();
      m_downloadFile->close();
      delete m_downloadFile;
      m_downloadFile = nullptr;
      if (!written) {
        m_downloading = false;
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
        QFile::remove(savePath);
        emit downloadingChanged();
        emit downloadErrorOccurred(QStringLiteral("写入更新包失败，请检查磁盘空间和权限"));
        return;
      }
    }

    m_downloadedFilePath = savePath;
    m_downloading = false;
    emit downloadingChanged();
    m_downloadReply->deleteLater();
    m_downloadReply = nullptr;

    // 下载完成后自动进行 SHA256 校验
    downloadSha256AndVerify();
  });
}

void UpdateChecker::cancelDownload() {
  // 停止超时定时器
  if (m_downloadTimeoutTimer) {
    m_downloadTimeoutTimer->stop();
  }

  if (m_downloadReply) {
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;
    reply->abort();
    reply->deleteLater();
  }
  if (m_downloadFile) {
    m_downloadFile->close();
    m_downloadFile->remove();
    delete m_downloadFile;
    m_downloadFile = nullptr;
  }
  m_downloading = false;
  m_downloadProgress = 0.0;
  m_downloadComplete = false;
  emit downloadingChanged();
  emit downloadProgressChanged(0.0);

  // 清理已下载的临时文件
  if (!m_downloadedFilePath.isEmpty()) {
    QFile::remove(m_downloadedFilePath);
    m_downloadedFilePath.clear();
  }
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
