#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

class UpdateChecker : public QObject {
  Q_OBJECT
  // --- 原有属性 ---
  Q_PROPERTY(bool checking READ isChecking NOTIFY checkingChanged)
  Q_PROPERTY(
      bool updateAvailable READ isUpdateAvailable NOTIFY updateResultReady)
  Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateResultReady)
  Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY updateResultReady)
  Q_PROPERTY(QString downloadUrl READ downloadUrl NOTIFY updateResultReady)
  Q_PROPERTY(bool automaticInstallAvailable READ automaticInstallAvailable
                 NOTIFY updateResultReady)
  Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)

  // --- 新增属性：下载状态 ---
  Q_PROPERTY(bool downloading READ isDownloading NOTIFY downloadingChanged)
  Q_PROPERTY(
      double downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)
  Q_PROPERTY(
      bool downloadComplete READ isDownloadComplete NOTIFY downloadFinished)
  Q_PROPERTY(QString downloadErrorMsg READ downloadErrorMsg NOTIFY
                 downloadErrorOccurred)
  Q_PROPERTY(bool verifying READ isVerifying NOTIFY verifyingChanged)
  Q_PROPERTY(
      bool verifySuccess READ isVerifySuccess NOTIFY verifyFinished)

public:
  explicit UpdateChecker(QObject *parent = nullptr);

  // --- 原有访问器 ---
  bool isChecking() const;
  bool isUpdateAvailable() const;
  QString latestVersion() const;
  QString releaseNotes() const;
  QString downloadUrl() const;
  bool automaticInstallAvailable() const;
  QString currentVersion() const;

  // --- 新增访问器 ---
  bool isDownloading() const;
  double downloadProgress() const;
  bool isDownloadComplete() const;
  QString downloadErrorMsg() const;
  bool isVerifying() const;
  bool isVerifySuccess() const;

  // --- 原有可调用方法 ---
  Q_INVOKABLE void checkForUpdate();
  Q_INVOKABLE void openDownloadUrl();

  // --- 新增可调用方法 ---
  Q_INVOKABLE void downloadUpdate();    // 开始下载更新文件
  Q_INVOKABLE void cancelDownload();    // 取消下载
  Q_INVOKABLE void installUpdate();     // 启动 updater 并退出主程序

signals:
  // --- 原有信号 ---
  void checkingChanged();
  void updateResultReady();
  void checkError(QString errorMessage);

  // --- 新增信号 ---
  void downloadingChanged();
  void downloadProgressChanged(double progress);
  void downloadFinished();
  void downloadErrorOccurred(QString errorMessage);
  void verifyingChanged();
  void verifyFinished(bool success);

private slots:
  void onReplyFinished(QNetworkReply *reply);

private:
  QNetworkAccessManager *m_networkManager;

  // --- 原有成员 ---
  bool m_checking;
  bool m_updateAvailable;
  QString m_latestVersion;
  QString m_releaseNotes;
  QString m_downloadUrl;
  QString m_currentVersion;

  // --- 新增成员 ---
  bool m_downloading;
  double m_downloadProgress;
  bool m_downloadComplete;
  QString m_downloadErrorMsg;
  bool m_verifying;
  bool m_verifySuccess;
  QString m_sha256Url;              // 当前平台更新包对应的 .sha256 下载地址
  QString m_expectedSha256;         // 当前平台更新包的 SHA256 哈希
  QString m_downloadedFilePath;     // 已下载的更新文件路径
  QString m_downloadFileName;       // 下载文件名（如 LogAnalyQt_v1.7.0.exe）
  QNetworkReply *m_downloadReply;   // 当前下载的 reply（用于取消）
  QFile *m_downloadFile;            // 下载时增量写入的文件句柄
  qint64 m_expectedDownloadSize;    // 预期的下载总大小（用于处理重定向后 Content-Length 丢失的情况）
  qint64 m_lastReceivedBytes;       // 上次进度更新时接收的字节数（用于超时检测）
  QTimer *m_downloadTimeoutTimer;   // 下载超时定时器

  // --- 原有方法 ---
  bool isNewerVersion(const QString &remote, const QString &local);

  // --- 新增方法 ---
  QString updateTempDir() const;              // 获取临时下载目录
  void downloadSha256AndVerify();             // 校验已下载文件的 SHA256
  void finishVerification(const QString &expectedHash);
  bool verifySha256(const QString &filePath,
                    const QString &expectedHash); // SHA256 校验
  QString findUpdaterPath() const;            // 查找 updater.exe 路径
};

#endif // UPDATECHECKER_H
