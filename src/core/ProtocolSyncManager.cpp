#include "ProtocolSyncManager.h"
#include "config/PresetCodec.h"
#include "config/CredentialAccess.h"
#include "ai/AiPromptFile.h"
#include "../common/AppPaths.h"
#include "csg_upstream/CsgUpstreamConfigManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryFile>
#include "../common/AppSettings.h"
#include <QXmlStreamReader>
#include <QThread>
#include <QDesktopServices>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#ifdef Q_OS_MACOS
#include <cerrno>
#include <sys/stat.h>
#endif

SyncStatusModel::SyncStatusModel(QObject *parent)
    : QAbstractListModel(parent) {}

int SyncStatusModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return m_items.count();
}

QVariant SyncStatusModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= m_items.count())
    return QVariant();
  const auto &item = m_items[index.row()];
  switch (role) {
  case FileNameRole:
    return item.fileName;
  case LocalTimeRole:
    return item.localExists ? item.localTime.toString("MM-dd HH:mm")
                            : QString("--");
  case RemoteTimeRole:
    return item.remoteExists ? item.remoteTime.toString("MM-dd HH:mm")
                             : QString("--");
  case LocalVersionRole:
    return item.localVersion.isEmpty() ? QString("--") : item.localVersion;
  case LocalDateRole:
    return item.localDate.isEmpty() ? QString("--") : item.localDate;
  case RemoteVersionRole:
    return item.remoteVersion.isEmpty() ? QString("--") : item.remoteVersion;
  case RemoteDateRole:
    return item.remoteDate.isEmpty() ? QString("--") : item.remoteDate;
  case ActionRole:
    return item.action;
  case LocalExistsRole:
    return item.localExists;
  case RemoteExistsRole:
    return item.remoteExists;
  default:
    return QVariant();
  }
}

QHash<int, QByteArray> SyncStatusModel::roleNames() const {
  QHash<int, QByteArray> roles;
  roles[FileNameRole] = "fileName";
  roles[LocalTimeRole] = "localTime";
  roles[RemoteTimeRole] = "remoteTime";
  roles[LocalVersionRole] = "localVersion";
  roles[LocalDateRole] = "localDate";
  roles[RemoteVersionRole] = "remoteVersion";
  roles[RemoteDateRole] = "remoteDate";
  roles[ActionRole] = "action";
  roles[LocalExistsRole] = "localExists";
  roles[RemoteExistsRole] = "remoteExists";
  return roles;
}

void SyncStatusModel::setItems(const QList<SyncStatusItem> &items) {
  beginResetModel();
  m_items = items;
  endResetModel();
}

QList<SyncStatusItem> SyncStatusModel::items() const { return m_items; }

ProtocolSyncManager::ProtocolSyncManager(QObject *parent)
    : ProtocolSyncManager(Dependencies{}, parent) {}

ProtocolSyncManager::ProtocolSyncManager(const Dependencies &dependencies, QObject *parent)
    : QObject(parent), m_statusModel(new SyncStatusModel(this)), m_dependencies(dependencies) {
#ifdef Q_OS_MACOS
  m_sharePath = QStringLiteral("smb://10.10.1.210/研发一部/protocol_xml");
#else
  m_sharePath = QStringLiteral("\\\\10.10.1.210\\研发一部\\protocol_xml");
#endif
  if (!m_dependencies.credentials)
    m_dependencies.credentials = std::make_shared<SecureCredentialStore>(SecureCredentialStore::Domain::ProtocolSync);
  if (m_dependencies.settingsFile.isEmpty()) m_dependencies.settingsFile = appSettings().fileName();
  if (!m_dependencies.mounts) m_dependencies.mounts = SmbShare::mountedShares;
  if (!m_dependencies.openShare) m_dependencies.openShare = [](const QUrl &url) { return QDesktopServices::openUrl(url); };
  if (m_dependencies.promptFiles) {
    JsonSettings settings(m_dependencies.settingsFile);
    QString protocolPath = settings.value("protocolSync/sharePath", m_sharePath).toString();
    while (protocolPath.endsWith('/') || protocolPath.endsWith('\\')) protocolPath.chop(1);
    const int separator = qMax(protocolPath.lastIndexOf('/'), protocolPath.lastIndexOf('\\'));
    m_sharePath = protocolPath.left(separator + 1) + "prompts";
  }
  loadCredentials();
}

ProtocolSyncManager::ProtocolSyncManager(WorkerTag)
    : QObject(nullptr), m_statusModel(new SyncStatusModel(this)), m_isWorker(true) {}

ProtocolSyncManager::~ProtocolSyncManager() {
  if (m_connectionCancelled) *m_connectionCancelled = true;
  if (!m_isWorker && m_operationCancelled) *m_operationCancelled = true;
  cleanupConnectProcess();
}

SyncStatusModel *ProtocolSyncManager::statusModel() const {
  return m_statusModel;
}

void ProtocolSyncManager::clearStatus() { m_statusModel->setItems({}); }

QString ProtocolSyncManager::sharePath() const { return m_sharePath; }

void ProtocolSyncManager::setSharePath(const QString &path) {
  if (m_sharePath == path)
    return;
  cancelConnection();
  cancelPendingOperations();
  m_resolvedSharePath.clear();
  clearStatus();
  m_sharePath = path;
  emit sharePathChanged();
}

QString ProtocolSyncManager::username() const { return m_username; }

void ProtocolSyncManager::setUsername(const QString &name) {
  if (m_username == name)
    return;
  m_username = name;
  emit usernameChanged();
}

QString ProtocolSyncManager::password() const {
#ifdef Q_OS_MACOS
  return {};
#else
  return m_password;
#endif
}

void ProtocolSyncManager::setPassword(const QString &pwd) {
#ifdef Q_OS_MACOS
  Q_UNUSED(pwd);
  return;
#endif
  if (m_password == pwd)
    return;
  m_password = pwd;
  emit passwordChanged();
}

QString ProtocolSyncManager::lastError() const { return m_lastError; }
bool ProtocolSyncManager::connecting() const { return m_connecting; }

void ProtocolSyncManager::connectToShare() {
#ifdef Q_OS_MACOS
  cancelConnection();
  if (m_connectionProbeRunning) {
    const QString error = QStringLiteral("前次系统共享检查仍在后台结束，请稍后重试");
    setLastError(error); emit connectionFinished(false, error); return;
  }
  cancelPendingOperations();
  clearStatus();
  m_resolvedSharePath.clear();
  setLastError({});
  SmbShare::Target target;
  QString error;
  if (!SmbShare::parse(m_sharePath, &target, &error)) {
    setLastError(error); emit connectionFinished(false, error); return;
  }
  saveCredentials();
  if (!m_lastError.isEmpty()) { emit connectionFinished(false, m_lastError); return; }
  if (!m_dependencies.openShare(target.url)) {
    error = QStringLiteral("无法请求 macOS 打开 SMB 连接");
    setLastError(error); emit connectionFinished(false, error); return;
  }
  m_connecting = true; m_connectionProbeRunning = true; emit connectingChanged();
  const auto cancelled = m_connectionCancelled = std::make_shared<std::atomic_bool>(false);
  const auto mounts = m_dependencies.mounts;
  struct ProbeResult { QString path; QString error; };
  const auto result = std::make_shared<ProbeResult>();
  auto *thread = QThread::create([target, mounts, cancelled, result] {
    while (!cancelled->load()) {
      SmbShare::Resolution resolution;
      result->path = SmbShare::resolve(target, mounts(), &result->error, &resolution);
      if (resolution != SmbShare::Resolution::Unavailable) break;
      QThread::msleep(250);
    }
  });
  connect(thread, &QThread::finished, this, [this, cancelled, result] {
    m_connectionProbeRunning = false;
    if (cancelled->load() || m_connectionCancelled != cancelled) return;
    if (m_connectTimer) m_connectTimer->stop();
    m_resolvedSharePath = result->path;
    m_connecting = false; emit connectingChanged();
    const bool success = !result->path.isEmpty();
    if (!success) setLastError(result->error);
    emit connectionFinished(success, success ? QStringLiteral("系统共享已挂载且目录可访问") : result->error);
  });
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  m_connectTimer = new QTimer(this);
  m_connectTimer->setSingleShot(true);
  connect(m_connectTimer, &QTimer::timeout, this, &ProtocolSyncManager::onConnectTimeout);
  m_connectTimer->start(m_dependencies.connectionTimeoutMs);
  thread->start();
  return;
#endif

  setLastError(QString());
  if (m_sharePath.isEmpty()) {
    setLastError("网盘路径为空");
    emit connectionFinished(false, "网盘路径为空");
    return;
  }

  // 如果上一次连接还在进行中，先清理
  if (m_connecting) {
    cleanupConnectProcess();
  }

  m_connecting = true;
  emit connectingChanged();

  QStringList args;
  args << "use" << m_sharePath;
  if (!m_username.isEmpty())
    args << QString("/user:%1").arg(m_username);
  if (!m_password.isEmpty())
    args << m_password;
  args << "/persistent:no";

  // 创建异步进程
  m_connectProcess = new QProcess(this);
  connect(m_connectProcess,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          &ProtocolSyncManager::onConnectProcessFinished);

  // 创建超时定时器，最多等待3秒
  m_connectTimer = new QTimer(this);
  m_connectTimer->setSingleShot(true);
  connect(m_connectTimer, &QTimer::timeout, this,
          &ProtocolSyncManager::onConnectTimeout);

  m_connectProcess->start("net", args);
  m_connectTimer->start(3000);
}

void ProtocolSyncManager::onConnectProcessFinished(
    int exitCode, QProcess::ExitStatus exitStatus) {
  // 如果已经被超时处理过，直接返回
  if (!m_connecting)
    return;

  // 停止超时定时器
  if (m_connectTimer)
    m_connectTimer->stop();

  bool success = false;
  QString message;

  if (exitStatus != QProcess::NormalExit || exitCode != 0) {
    QString errorText =
        QString::fromLocal8Bit(m_connectProcess->readAllStandardError())
            .trimmed();
    if (errorText.isEmpty()) {
      errorText =
          QString::fromLocal8Bit(m_connectProcess->readAllStandardOutput())
              .trimmed();
    }
    if (errorText.isEmpty())
      errorText = "连接失败，请检查账号密码或网络";
    setLastError(errorText);
    message = errorText;
  } else {
    QDir remoteDirectory(remoteDir());
    if (!remoteDirectory.exists()) {
      setLastError("网盘路径不可访问");
      message = "网盘路径不可访问";
    } else {
      success = true;
      message = "连接成功";
    }
  }

  cleanupConnectProcess();
  emit connectionFinished(success, message);
}

void ProtocolSyncManager::onConnectTimeout() {
#ifdef Q_OS_MACOS
  if (!m_connecting) return;
  cancelConnection();
  const QString error = QStringLiteral("等待系统 SMB 挂载超时，请确认 Finder 连接和共享目录");
  setLastError(error); emit connectionFinished(false, error); return;
#endif
  // 如果已经被进程完成处理过，直接返回
  if (!m_connecting)
    return;

  // 杀掉还在运行的进程
  if (m_connectProcess && m_connectProcess->state() != QProcess::NotRunning) {
    m_connectProcess->kill();
    m_connectProcess->waitForFinished(500);
  }

  QString errorMsg = "连接超时（3秒），请检查网络或网盘路径";
  setLastError(errorMsg);

  cleanupConnectProcess();
  emit connectionFinished(false, errorMsg);
}

void ProtocolSyncManager::cleanupConnectProcess() {
  m_connecting = false;
  emit connectingChanged();
  if (m_connectTimer) {
    m_connectTimer->stop();
    m_connectTimer->deleteLater();
    m_connectTimer = nullptr;
  }
  if (m_connectProcess) {
    // 断开信号防止重复触发
    m_connectProcess->disconnect(this);
    if (m_connectProcess->state() != QProcess::NotRunning) {
      m_connectProcess->kill();
      m_connectProcess->waitForFinished(500);
    }
    m_connectProcess->deleteLater();
    m_connectProcess = nullptr;
  }
}

bool ProtocolSyncManager::fetchSyncStatusNow() {
  setLastError(QString());
  if (m_sharePath.isEmpty()) {
    setLastError("网盘路径为空");
    return false;
  }

  if (!ensureLocalDir())
    return false;

  QDir remoteDirectory(remoteDir());
  if (!remoteDirectory.exists()) {
    setLastError("网盘路径不可访问");
    return false;
  }

  QList<SyncStatusItem> items;
  const QStringList files = protocolFiles();
  const QString localBase = localDir();
  const QString remoteBase = remoteDir();

  for (const QString &fileName : files) {
    SyncStatusItem item;
    item.fileName = fileName;

    const QString localPath = filePath(localBase, fileName);
    const QString remotePath = filePath(remoteBase, fileName);

    QFileInfo localInfo(localPath);
    QFileInfo remoteInfo(remotePath);
    if (remoteInfo.isSymbolicLink()) {
      setLastError(QStringLiteral("拒绝读取网盘协议符号链接：") + fileName); return false;
    }
    if (m_operationCancelled && m_operationCancelled->load()) return false;
    item.localExists = localInfo.exists();
    item.remoteExists = remoteInfo.exists();
    item.localTime = localInfo.lastModified();
    item.remoteTime = remoteInfo.lastModified();

    if (!m_dependencies.promptFiles && item.localExists) {
      readVersionInfo(localPath, &item.localVersion, &item.localDate);
    }
    if (!m_dependencies.promptFiles && item.remoteExists) {
      readVersionInfo(remotePath, &item.remoteVersion, &item.remoteDate);
    }

    if (!m_dependencies.promptFiles && fileName == "preset_messages.xml") {
      for (const auto &pair : {qMakePair(localPath, &item.localDigest), qMakePair(remotePath, &item.remoteDigest)}) {
        QFile file(pair.first);
        if (!file.exists()) continue;
        if (!file.open(QIODevice::ReadOnly) || file.size() > ConfigJson::MaxBytes) { setLastError(QStringLiteral("预置报文交换文件读取失败")); return false; }
        *pair.second = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
      }
    }
    if (m_dependencies.promptFiles) {
      item.localVersion = item.localTime.toString("HH:mm:ss");
      item.localDate = item.localTime.toString("yyyy-MM-dd");
      item.remoteVersion = item.remoteTime.toString("HH:mm:ss");
      item.remoteDate = item.remoteTime.toString("yyyy-MM-dd");
      QByteArray local, remote;
      QString error;
      if ((item.localExists && !AiPromptFile::readBytes(localPath, &local, &error)) ||
          (item.remoteExists && !AiPromptFile::readBytes(remotePath, &remote, &error))) {
        setLastError(error); return false;
      }
      if (item.localExists) item.localDigest = QCryptographicHash::hash(local, QCryptographicHash::Sha256);
      if (item.remoteExists) item.remoteDigest = QCryptographicHash::hash(remote, QCryptographicHash::Sha256);
      if (!item.localExists) item.action = item.remoteExists ? "download" : "missing";
      else if (!item.remoteExists) item.action = "upload";
      else if (local == remote) item.action = "equal";
      else if (item.localTime == item.remoteTime) item.action = "conflict";
      else item.action = item.localTime < item.remoteTime ? "download" : "upload";
    } else if (item.localExists && item.remoteExists) {
      int diff = compareByMetadata(item);
      if (diff > 0) {
        item.action = "download";
      } else if (diff < 0) {
        item.action = "upload";
      } else {
        item.action = "equal";
      }
    } else if (item.localExists && !item.remoteExists) {
      item.action = "upload";
    } else if (!item.localExists && item.remoteExists) {
      item.action = "download";
    } else {
      item.action = "missing";
    }

    items.append(item);
  }

  m_statusModel->setItems(items);
  emit statusUpdated();
  return true;
}

QVariantMap ProtocolSyncManager::syncAllNow() {
  QVariantMap result;
  QStringList uploaded;
  QStringList downloaded;
  QStringList skipped;
  QStringList failed;
  QVariantList errors;

  setLastError(QString());

  const QList<SyncStatusItem> items = m_statusModel->items();
  for (const SyncStatusItem &item : items) {
    if (m_operationCancelled && m_operationCancelled->load()) break;
    if (item.action == "upload") {
      QString errorMessage;
      if (uploadFile(item, &errorMessage)) {
        uploaded.append(item.fileName);
      } else {
        failed.append(item.fileName);
        QVariantMap err;
        err["file"] = item.fileName;
        err["error"] = errorMessage;
        errors.append(err);
      }
    } else if (item.action == "download") {
      QString errorMessage;
      if (downloadFile(item, &errorMessage)) {
        downloaded.append(item.fileName);
      } else {
        failed.append(item.fileName);
        QVariantMap err;
        err["file"] = item.fileName;
        err["error"] = errorMessage;
        errors.append(err);
      }
    } else if (item.action == "conflict") {
      failed.append(item.fileName);
      errors.append(QVariantMap{{"file", item.fileName}, {"error", QStringLiteral("内容不同但修改时间相同，请查看差异并保存希望保留的版本后刷新")}});
    } else if (item.action == "equal") {
      skipped.append(item.fileName);
    } else if (item.action == "missing") {
      skipped.append(item.fileName);
    }
  }

  result["uploaded"] = uploaded;
  result["downloaded"] = downloaded;
  result["skipped"] = skipped;
  result["failed"] = failed;
  result["errors"] = errors;

  if (!m_operationCancelled || !m_operationCancelled->load()) fetchSyncStatusNow();
  return result;
}

QVariantMap ProtocolSyncManager::syncFileNow(const QString &fileName) {
  QVariantMap result;
  QStringList uploaded;
  QStringList downloaded;
  QStringList skipped;
  QStringList failed;
  QVariantList errors;

  setLastError(QString());

  SyncStatusItem target;
  bool found = false;
  const QList<SyncStatusItem> items = m_statusModel->items();
  for (const SyncStatusItem &item : items) {
    if (item.fileName == fileName) {
      target = item;
      found = true;
      break;
    }
  }

  if (!found) {
    failed.append(fileName);
    QVariantMap err;
    err["file"] = fileName;
    err["error"] = "未找到同步项";
    errors.append(err);
  } else if (target.action == "conflict") {
    failed.append(fileName);
    errors.append(QVariantMap{{"file", fileName}, {"error", QStringLiteral("内容不同但修改时间相同，请查看差异并保存希望保留的版本后刷新")}});
  } else if (target.action == "upload") {
    QString errorMessage;
    if (uploadFile(target, &errorMessage)) {
      uploaded.append(target.fileName);
    } else {
      failed.append(target.fileName);
      QVariantMap err;
      err["file"] = target.fileName;
      err["error"] = errorMessage;
      errors.append(err);
    }
  } else if (target.action == "download") {
    QString errorMessage;
    if (downloadFile(target, &errorMessage)) {
      downloaded.append(target.fileName);
    } else {
      failed.append(target.fileName);
      QVariantMap err;
      err["file"] = target.fileName;
      err["error"] = errorMessage;
      errors.append(err);
    }
  } else {
    skipped.append(target.fileName);
  }

  result["uploaded"] = uploaded;
  result["downloaded"] = downloaded;
  result["skipped"] = skipped;
  result["failed"] = failed;
  result["errors"] = errors;

  if (!m_operationCancelled || !m_operationCancelled->load()) fetchSyncStatusNow();
  return result;
}

QString ProtocolSyncManager::localProtocolDir() const { return localDir(); }

QString ProtocolSyncManager::legacyCredentialProfile(const QString &settingsFile, bool promptFiles) {
  const QFileInfo source(settingsFile);
  const QString canonical = source.canonicalFilePath().isEmpty() ? source.absoluteFilePath() : source.canonicalFilePath();
  return QString::fromLatin1(QCryptographicHash::hash((promptFiles ? "legacy-prompts-unbound\n" : "legacy-unbound\n") + canonical.toUtf8(), QCryptographicHash::Sha256).toHex());
}

void ProtocolSyncManager::saveCredentials() {
  JsonSettings settings(m_dependencies.settingsFile);
  const QString prefix = m_dependencies.promptFiles ? "promptSync/" : "protocolSync/";
  settings.setValue(prefix + "sharePath", m_sharePath);
  settings.setValue(prefix + "username", m_username);
#ifndef Q_OS_MACOS
  if (!m_password.isEmpty()) {
    const auto profile = CredentialAccess::profile((m_sharePath + "\n" + m_username).toUtf8());
    const auto payload = QJsonDocument(QJsonObject{{"username", m_username}, {"password", m_password}}).toJson(QJsonDocument::Compact);
    QString error;
    const auto credentials = m_dependencies.credentials;
    if (!CredentialAccess::run([credentials, profile, payload](QString *e) { return credentials->writeVerified(profile, payload, e); }, &error)) { setLastError(error); return; }
    settings.setValue(prefix + "credentialProfile", profile);
  }
#endif
  settings.sync();
  if (settings.status() != QSettings::NoError) setLastError(QStringLiteral("便携配置目录无法写入，SMB 地址未保存"));
}

void ProtocolSyncManager::loadCredentials() {
  JsonSettings settings(m_dependencies.settingsFile);
  const QString prefix = m_dependencies.promptFiles ? "promptSync/" : "protocolSync/";
  if (settings.contains(prefix + "sharePath")) m_sharePath = settings.value(prefix + "sharePath").toString();
  m_username = settings.value(prefix + "username").toString();
  const QString profile = settings.value(prefix + "credentialProfile").toString();
  if (profile.isEmpty()) return;
  m_credentialBusy = true; emit credentialStateChanged();
  auto bytes = std::make_shared<QByteArray>();
  auto error = std::make_shared<QString>();
  auto store = m_dependencies.credentials;
  auto *thread = QThread::create([store, profile, bytes, error] { store->read(profile, bytes.get(), error.get()); });
  connect(thread, &QThread::finished, this, [this, bytes, error] {
    m_credentialBusy = false; m_credentialError = *error;
    if (error->isEmpty() && !bytes->isEmpty()) {
      const auto record = QJsonDocument::fromJson(*bytes).object();
      m_username = record.value("username").toString(); m_password = record.value("password").toString();
    }
    bytes->fill(0); bytes->clear(); emit credentialStateChanged();
  });
  connect(thread, &QThread::finished, thread, &QObject::deleteLater); thread->start();
}

void ProtocolSyncManager::cancelConnection() {
  if (m_connectionCancelled) *m_connectionCancelled = true;
  cleanupConnectProcess();
}

void ProtocolSyncManager::cancelPendingOperations() {
  ++m_generation;
  if (m_operationCancelled) *m_operationCancelled = true;
}

QString ProtocolSyncManager::remoteDir() const {
#ifdef Q_OS_MACOS
  return m_resolvedSharePath;
#else
  return normalizePath(m_sharePath);
#endif
}

bool ProtocolSyncManager::fetchSyncStatus() {
#ifndef Q_OS_MACOS
  return fetchSyncStatusNow();
#else
  if (m_busy || connecting()) return false;
  startOperation("status"); return true;
#endif
}

QVariantMap ProtocolSyncManager::syncAll() {
#ifndef Q_OS_MACOS
  return syncAllNow();
#else
  if (m_busy || connecting()) return {{"pending", false}, {"error", QStringLiteral("同步操作尚未完成")}};
  startOperation("all"); return {{"pending", true}};
#endif
}

QVariantMap ProtocolSyncManager::syncFile(const QString &fileName) {
#ifndef Q_OS_MACOS
  return syncFileNow(fileName);
#else
  if (m_busy || connecting()) return {{"pending", false}, {"error", QStringLiteral("同步操作尚未完成")}};
  startOperation("file", fileName); return {{"pending", true}};
#endif
}

QVariantList ProtocolSyncManager::getXmlDiff(const QString &fileName) const {
#ifndef Q_OS_MACOS
  return getXmlDiffNow(fileName);
#else
  if (!m_busy && !connecting()) const_cast<ProtocolSyncManager *>(this)->startOperation("diff", fileName);
  return {};
#endif
}

void ProtocolSyncManager::startOperation(const QString &kind, const QString &fileName) {
  struct Result { bool success = false; QString error; QList<SyncStatusItem> items; QVariantMap sync; QVariantList diff; QStringList downloaded; };
  const auto result = std::make_shared<Result>();
  const auto cancelled = m_operationCancelled = std::make_shared<std::atomic_bool>(false);
  const quint64 generation = m_generation;
  const QString remote = remoteDir();
  const QString shareUrl = m_sharePath;
  const auto mounts = m_dependencies.mounts;
  const auto items = m_statusModel->items();
  const bool promptFiles = m_dependencies.promptFiles;
  const QString configFile = m_dependencies.settingsFile;
  m_busy = true; setLastError({}); emit busyChanged();
  auto *thread = QThread::create([result, cancelled, remote, shareUrl, mounts, items, kind, fileName, promptFiles, configFile] {
    QString currentRemote = remote;
#ifdef Q_OS_MACOS
    SmbShare::Target target;
    if (remote.isEmpty() || !SmbShare::parse(shareUrl, &target, &result->error)) {
      result->error = QStringLiteral("请先连接可访问的系统 SMB 共享"); return;
    }
    currentRemote = SmbShare::resolve(target, mounts(), &result->error);
#endif
    ProtocolSyncManager worker(WorkerTag{});
    worker.m_dependencies.promptFiles = promptFiles;
    worker.m_dependencies.settingsFile = configFile;
    worker.m_sharePath = currentRemote; worker.m_resolvedSharePath = currentRemote;
    worker.m_operationCancelled = cancelled;
    worker.m_statusModel->setItems(items);
    QObject::connect(&worker, &ProtocolSyncManager::protocolFileDownloaded, &worker,
                     [result](const QString &name) { result->downloaded.append(name); });
    if (currentRemote.isEmpty() || !QDir(currentRemote).exists()) {
      if (result->error.isEmpty()) result->error = QStringLiteral("请先连接可访问的系统 SMB 共享"); return;
    }
    if (cancelled->load()) return;
    if (kind == "status") result->success = worker.fetchSyncStatusNow();
    else if (kind == "diff") {
      if (!worker.protocolFiles().contains(fileName)) { result->error = QStringLiteral("未知协议文件"); return; }
      if (QFileInfo(worker.filePath(currentRemote, fileName)).isSymbolicLink()) {
        result->error = QStringLiteral("拒绝比较网盘协议符号链接"); return;
      }
      if (!worker.ensureLocalDir()) { result->error = worker.lastError(); return; }
      result->diff = worker.getXmlDiffNow(fileName, &result->error);
      result->success = result->error.isEmpty();
      if (!result->success) return;
    } else { result->sync = kind == "all" ? worker.syncAllNow() : worker.syncFileNow(fileName); result->success = true; }
    result->error = worker.lastError(); result->items = worker.m_statusModel->items();
  });
  connect(thread, &QThread::finished, this, [this, result, cancelled, generation, kind, fileName] {
    m_busy = false; emit busyChanged();
    // A committed download still needs a reload even when its dialog was closed.
    for (const auto &name : result->downloaded) emit protocolFileDownloaded(name);
    if (generation != m_generation || cancelled->load()) return;
    setLastError(result->error);
    if (kind == "status") {
      if (result->success) { m_statusModel->setItems(result->items); emit statusUpdated(); }
      emit statusFinished(result->success, result->error);
    } else if (kind == "diff") {
      if (result->success) emit diffFinished(fileName, result->diff);
      else emit statusFinished(false, result->error);
    }
    else {
      if (!result->success) result->sync = {{"failed", QStringList{fileName.isEmpty() ? QStringLiteral("全部文件") : fileName}},
        {"errors", QVariantList{QVariantMap{{"file", fileName}, {"error", result->error}}}}};
      else { m_statusModel->setItems(result->items); emit statusUpdated(); }
      emit syncFinished(result->sync);
    }
  });
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start();
}

QString ProtocolSyncManager::localDir() const {
  return m_dependencies.promptFiles ? AppPaths::promptDirectory() : AppPaths::protocolDirectory();
}

QString ProtocolSyncManager::normalizePath(const QString &path) const {
  return QDir::fromNativeSeparators(path);
}

QString ProtocolSyncManager::filePath(const QString &base,
                                      const QString &fileName) const {
  if (base == localDir() && !m_dependencies.promptFiles && fileName == "preset_messages.xml") {
    const auto directory = QFileInfo(m_dependencies.settingsFile).dir().filePath("cache/preset-exchange");
    QDir().mkpath(directory);
    return QDir(directory).filePath(fileName);
  }
  QDir dir(base);
  return dir.filePath(fileName);
}

QString
ProtocolSyncManager::expectedRootForFile(const QString &fileName) const {
  if (fileName == "698_protocol.xml")
    return "OIConfig";
  if (fileName == "app_msg_protocol.xml")
    return "app_msg_protocol";
  if (fileName == "3762_protocol.xml")
    return "Prot3762Config";
  if (fileName == "645_protocol.xml")
    return "Prot645Config";
  if (fileName == "module_msg_protocol.xml")
    return "module_msg_protocol";
  if (fileName == "csg_upstream_protocol.xml")
    return "ProtCsgUpstreamConfig";
  if (fileName == "preset_messages.xml")
    return "presets";
  return QString();
}

QStringList ProtocolSyncManager::protocolFiles() const {
  if (m_dependencies.promptFiles) return {AiPromptFile::FileName};
  return {"698_protocol.xml", "app_msg_protocol.xml", "3762_protocol.xml",
          "645_protocol.xml", "module_msg_protocol.xml",
          "csg_upstream_protocol.xml", "preset_messages.xml"};
}

bool ProtocolSyncManager::ensureLocalDir() {
  if (!m_dependencies.promptFiles) {
    QJsonObject root; QString error;
    if (!ConfigJson::read(m_dependencies.settingsFile, &root, &error)) { setLastError(error); return false; }
    const auto path = filePath(localDir(), "preset_messages.xml");
    const auto data = PresetCodec::encode(root.value("messages").toObject());
    QFile previous(path);
    const bool unchanged = previous.open(QIODevice::ReadOnly) && previous.readAll() == data;
    previous.close();
    if (!unchanged) {
      QSaveFile output(path);
      if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit()) { setLastError(QStringLiteral("无法生成预置报文交换视图")); return false; }
    }
  }
  QDir dir(localDir());
  if (dir.exists())
    return true;
  if (dir.mkpath("."))
    return true;
  setLastError("无法创建本地协议目录");
  return false;
}

bool ProtocolSyncManager::validateXml(const QString &filePath,
                                      QString *errorMessage, int *line,
                                      int *column, QString *rootName) const {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (errorMessage)
      *errorMessage = "无法打开文件";
    return false;
  }

  QXmlStreamReader reader(&file);
  QString firstRoot;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement() && firstRoot.isEmpty()) {
      firstRoot = reader.name().toString();
    }
  }

  if (reader.hasError()) {
    if (errorMessage)
      *errorMessage = reader.errorString();
    if (line)
      *line = reader.lineNumber();
    if (column)
      *column = reader.columnNumber();
    return false;
  }

  if (rootName)
    *rootName = firstRoot;
  return true;
}

bool ProtocolSyncManager::readVersionInfo(const QString &filePath,
                                          QString *version,
                                          QString *date) const {
  if (version)
    version->clear();
  if (date)
    date->clear();

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return false;

  QXmlStreamReader reader(&file);
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement()) {
      const auto attrs = reader.attributes();
      if (version && attrs.hasAttribute("version"))
        *version = attrs.value("version").toString();
      if (date && attrs.hasAttribute("date"))
        *date = attrs.value("date").toString();
      return true;
    }
  }

  return false;
}

int ProtocolSyncManager::compareByMetadata(const SyncStatusItem &item) const {
  if (!item.localDate.isEmpty() && !item.remoteDate.isEmpty()) {
    QDate localDate = QDate::fromString(item.localDate, "yyyy-MM-dd");
    QDate remoteDate = QDate::fromString(item.remoteDate, "yyyy-MM-dd");
    if (localDate.isValid() && remoteDate.isValid()) {
      if (localDate < remoteDate)
        return 1;
      if (localDate > remoteDate)
        return -1;
      int verDiff = compareVersion(item.localVersion, item.remoteVersion);
      if (verDiff != 0)
        return verDiff;
      return 0;
    }
  }

  qint64 diffSeconds = item.localTime.secsTo(item.remoteTime);
  if (diffSeconds > 1)
    return 1;
  if (diffSeconds < -1)
    return -1;
  return 0;
}

int ProtocolSyncManager::compareVersion(const QString &localVersion,
                                        const QString &remoteVersion) const {
  QString local = localVersion;
  QString remote = remoteVersion;
  if (local.startsWith("v") || local.startsWith("V"))
    local = local.mid(1);
  if (remote.startsWith("v") || remote.startsWith("V"))
    remote = remote.mid(1);

  QStringList localParts = local.split('.', Qt::SkipEmptyParts);
  QStringList remoteParts = remote.split('.', Qt::SkipEmptyParts);
  int count = qMax(localParts.size(), remoteParts.size());
  for (int i = 0; i < count; ++i) {
    int localVal = i < localParts.size() ? localParts[i].toInt() : 0;
    int remoteVal = i < remoteParts.size() ? remoteParts[i].toInt() : 0;
    if (localVal < remoteVal)
      return 1;
    if (localVal > remoteVal)
      return -1;
  }
  return 0;
}

bool ProtocolSyncManager::validateProtocolStructure(
    const QString &fileName, const QString &rootName,
    QString *errorMessage) const {
  const QString expected = expectedRootForFile(fileName);
  if (expected.isEmpty())
    return true;
  if (rootName != expected) {
    if (errorMessage)
      *errorMessage = QString("根节点应为 <%1>").arg(expected);
    return false;
  }
  return true;
}

bool ProtocolSyncManager::validateProtocolSemantics(
    const QString &fileName, const QString &filePath,
    QString *errorMessage) const {
  if (fileName != "csg_upstream_protocol.xml")
    return true;
  return CsgUpstreamConfigManager::instance().validateXml(
      filePath, errorMessage);
}

bool ProtocolSyncManager::promptSnapshotUnchanged(const SyncStatusItem &item, QString *error) const {
  const auto unchanged = [error](const QString &path, bool existed, const QByteArray &digest) {
    const QFileInfo info(path);
    if (info.isSymbolicLink() || info.exists() != existed) return false;
    if (!existed) return true;
    QByteArray bytes;
    return AiPromptFile::readBytes(path, &bytes, error) &&
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) == digest;
  };
  if (!unchanged(filePath(localDir(), item.fileName), item.localExists, item.localDigest) ||
      !unchanged(filePath(remoteDir(), item.fileName), item.remoteExists, item.remoteDigest)) {
    if (error) *error = QStringLiteral("提示词在状态检查后发生变化，请刷新后再同步；原文件已保留");
    return false;
  }
  return true;
}

bool ProtocolSyncManager::uploadFile(const SyncStatusItem &item,
                                     QString *errorMessage) {
  if (!ensureLocalDir()) { if (errorMessage) *errorMessage = m_lastError; return false; }
  const QString localPath = filePath(localDir(), item.fileName);
  const QString remoteBase = remoteDir();
  const QString remotePath = filePath(remoteBase, item.fileName);
  if (QFileInfo(remotePath).isSymbolicLink()) {
    if (errorMessage) *errorMessage = QStringLiteral("拒绝同步网盘协议符号链接");
    return false;
  }

  if (!QFile::exists(localPath)) {
    if (errorMessage)
      *errorMessage = "本地文件不存在";
    return false;
  }

  if (m_dependencies.promptFiles) {
    if (!promptSnapshotUnchanged(item, errorMessage)) return false;
  } else {
    QString xmlError;
    int line = 0;
    int column = 0;
    QString rootName;
    if (!validateXml(localPath, &xmlError, &line, &column, &rootName)) {
      if (errorMessage) {
        *errorMessage = QString("XML语法错误 (行 %1, 列 %2): %3")
                            .arg(line)
                            .arg(column)
                            .arg(xmlError);
      }
      return false;
    }

    if (!validateProtocolStructure(item.fileName, rootName, &xmlError)) {
      if (errorMessage)
        *errorMessage = xmlError;
      return false;
    }

    if (!validateProtocolSemantics(item.fileName, localPath, &xmlError)) {
      if (errorMessage)
        *errorMessage = "协议配置语义错误：" + xmlError;
      return false;
    }

  }

  QFile source(localPath);
  if (!source.open(QIODevice::ReadOnly)) { if (errorMessage) *errorMessage = "无法读取本地文件"; return false; }
  const QByteArray data = m_dependencies.promptFiles ? source.read(AiPromptFile::MaxBytes + 1) : source.readAll();
  if (source.error() != QFile::NoError) { if (errorMessage) *errorMessage = "读取本地文件失败"; return false; }
  if (m_dependencies.promptFiles) {
    QString text;
    if (!AiPromptFile::decode(data, &text, errorMessage)) return false;
    if (QCryptographicHash::hash(data, QCryptographicHash::Sha256) != item.localDigest) {
      if (errorMessage) *errorMessage = QStringLiteral("本地提示词读取期间发生变化，请刷新后重试");
      return false;
    }
  }
  if (!m_dependencies.promptFiles && item.fileName == "preset_messages.xml") {
    QFile remote(remotePath);
    if (QCryptographicHash::hash(data, QCryptographicHash::Sha256) != item.localDigest || remote.exists() != item.remoteExists ||
        (item.remoteExists && (!remote.open(QIODevice::ReadOnly) || QCryptographicHash::hash(remote.readAll(), QCryptographicHash::Sha256) != item.remoteDigest))) {
      if (errorMessage) *errorMessage = QStringLiteral("预置报文在状态检查后已变化，请刷新后重试"); return false;
    }
  }
  QSaveFile destination(remotePath);
  destination.setDirectWriteFallback(false);
  if (!destination.open(QIODevice::WriteOnly) || destination.write(data) != data.size() ||
      (m_operationCancelled && m_operationCancelled->load()) ||
      (m_dependencies.promptFiles && !promptSnapshotUnchanged(item, errorMessage)) || !destination.commit()) {
    destination.cancelWriting();
    if (errorMessage && errorMessage->isEmpty()) *errorMessage = "原子上传失败或提交前已取消，远端原文件已保留";
    return false;
  }

  return true;
}

bool ProtocolSyncManager::downloadFile(const SyncStatusItem &item,
                                       QString *errorMessage) {
  const QString localBase = localDir();
  const QString localPath = filePath(localBase, item.fileName);
  const QString remoteBase = remoteDir();
  const QString remotePath = filePath(remoteBase, item.fileName);
  if (QFileInfo(remotePath).isSymbolicLink()) {
    if (errorMessage) *errorMessage = QStringLiteral("拒绝同步网盘协议符号链接");
    return false;
  }

  if (!QFile::exists(remotePath)) {
    if (errorMessage)
      *errorMessage = "网盘文件不存在";
    return false;
  }

  QTemporaryFile temporary(QDir(localBase).filePath("." + item.fileName + ".XXXXXX"));
  QFile remoteFile(remotePath);
  if (!remoteFile.open(QIODevice::ReadOnly) || !temporary.open()) {
    if (errorMessage) *errorMessage = QStringLiteral("无法读取网盘文件或创建本地临时文件");
    return false;
  }
  const QByteArray remoteData = m_dependencies.promptFiles ? remoteFile.read(AiPromptFile::MaxBytes + 1) : remoteFile.readAll();
  if (remoteFile.error() != QFile::NoError || temporary.write(remoteData) != remoteData.size() || !temporary.flush()) {
    if (errorMessage) *errorMessage = QStringLiteral("下载读取或临时写入失败，原配置已保留");
    return false;
  }
  const QString tempPath = temporary.fileName();
  temporary.close();

  if (m_dependencies.promptFiles) {
    QString text;
    if (QCryptographicHash::hash(remoteData, QCryptographicHash::Sha256) != item.remoteDigest) {
      if (errorMessage) *errorMessage = QStringLiteral("共享提示词读取期间发生变化，请刷新后重试");
      return false;
    }
    if (!AiPromptFile::decode(remoteData, &text, errorMessage) ||
        !promptSnapshotUnchanged(item, errorMessage)) return false;
  } else {
    QString xmlError;
    int line = 0;
    int column = 0;
    QString rootName;
    if (!validateXml(tempPath, &xmlError, &line, &column, &rootName)) {
      if (errorMessage) {
        *errorMessage = QString("XML语法错误 (行 %1, 列 %2): %3")
                            .arg(line)
                            .arg(column)
                            .arg(xmlError);
      }
      return false;
    }

    if (!validateProtocolStructure(item.fileName, rootName, &xmlError)) {
      if (errorMessage)
        *errorMessage = xmlError;
      return false;
    }

    if (!validateProtocolSemantics(item.fileName, tempPath, &xmlError)) {
      if (errorMessage)
        *errorMessage = "协议配置语义错误：" + xmlError;
      return false;
    }

  }

  QFile validatedFile(tempPath);
  if (!validatedFile.open(QIODevice::ReadOnly)) {
    if (errorMessage)
      *errorMessage = "无法读取已校验的临时文件";
    return false;
  }

  const QByteArray validatedData = validatedFile.readAll();
  if (validatedFile.error() != QFile::NoError) {
    if (errorMessage) *errorMessage = QStringLiteral("读取已校验的临时文件失败，原配置已保留");
    return false;
  }
  if (!m_dependencies.promptFiles && item.fileName == "preset_messages.xml") {
    QJsonObject messages;
    if (!PresetCodec::decode(validatedData, &messages, errorMessage)) return false;
    if (m_operationCancelled && m_operationCancelled->load()) return false;
    if (QCryptographicHash::hash(validatedData, QCryptographicHash::Sha256) != item.remoteDigest) { if (errorMessage) *errorMessage = QStringLiteral("共享预置报文已变化，请刷新"); return false; }
    if (!ConfigJson::update(m_dependencies.settingsFile, [&](QJsonObject &root, QString *error) {
      const auto current = PresetCodec::encode(root.value("messages").toObject());
      if (QCryptographicHash::hash(current, QCryptographicHash::Sha256) != item.localDigest) return ConfigJson::fail(error, QStringLiteral("本地预置报文已变化，未覆盖新编辑"));
      root.insert("messages", messages); return true;
    }, errorMessage)) return false;
    emit protocolFileDownloaded(item.fileName); return true;
  }
  QSaveFile destination(localPath);
  destination.setDirectWriteFallback(false);
  if (!destination.open(QIODevice::WriteOnly) ||
      destination.write(validatedData) != validatedData.size() ||
      (m_operationCancelled && m_operationCancelled->load()) ||
      (m_dependencies.promptFiles && !promptSnapshotUnchanged(item, errorMessage)) || !destination.commit()) {
    destination.cancelWriting();
    if (errorMessage && errorMessage->isEmpty())
      *errorMessage = "原子替换本地文件失败，原配置已保留";
    return false;
  }
  validatedFile.close();

  emit protocolFileDownloaded(item.fileName);
  return true;
}

void ProtocolSyncManager::setLastError(const QString &message) {
  if (m_lastError == message)
    return;
  m_lastError = message;
  emit lastErrorChanged();
}

// ======== XML 差异比较 ========

QVariantList ProtocolSyncManager::getXmlDiffNow(const QString &fileName, QString *error) const {
  if (error) error->clear();
  if (!protocolFiles().contains(fileName)) {
    if (error) *error = QStringLiteral("未知同步文件");
    return {};
  }
  const auto readLines = [this, error, &fileName](const QString &path, const QString &side, QStringList *lines) {
#ifdef Q_OS_MACOS
    struct stat metadata;
    if (::stat(QFile::encodeName(path).constData(), &metadata) != 0) {
      if (errno == ENOENT) return true;
      if (error) *error = side + QStringLiteral("协议文件状态无法读取：") + fileName;
      return false;
    }
#else
    if (!QFileInfo::exists(path)) return true;
#endif
    if (m_dependencies.promptFiles) {
      QString text;
      if (!AiPromptFile::read(path, &text, error)) return false;
      *lines = text.split('\n');
      return true;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      if (error) *error = side + QStringLiteral("协议文件无法打开：") + fileName;
      return false;
    }
    QTextStream input(&file);
    while (!input.atEnd()) lines->append(input.readLine());
    if (file.error() != QFile::NoError || input.status() != QTextStream::Ok) {
      if (error) *error = side + QStringLiteral("协议文件读取失败：") + fileName;
      return false;
    }
    return true;
  };
  QStringList localLines, remoteLines;
  if (!readLines(filePath(localDir(), fileName), QStringLiteral("本地"), &localLines) ||
      !readLines(filePath(remoteDir(), fileName), QStringLiteral("网盘"), &remoteLines)) return {};

  // 如果两边都为空，返回提示
  if (localLines.isEmpty() && remoteLines.isEmpty()) {
    QVariantList result;
    QVariantMap entry;
    entry["type"] = "same";
    entry["text"] = "（两个文件均为空或不存在）";
    result.append(entry);
    return result;
  }

  return buildLcsDiff(localLines, remoteLines);
}

QVariantList
ProtocolSyncManager::buildLcsDiff(const QStringList &localLines,
                                  const QStringList &remoteLines) const {
  const int totalLocal = localLines.size();
  const int totalRemote = remoteLines.size();

  // ========== 第一步：裁剪相同的前缀和后缀 ==========
  int prefixLen = 0;
  const int minLen = qMin(totalLocal, totalRemote);
  while (prefixLen < minLen &&
         localLines[prefixLen] == remoteLines[prefixLen]) {
    ++prefixLen;
  }

  int suffixLen = 0;
  while (suffixLen < (minLen - prefixLen) &&
         localLines[totalLocal - 1 - suffixLen] ==
             remoteLines[totalRemote - 1 - suffixLen]) {
    ++suffixLen;
  }

  // 提取中间差异部分
  QStringList midLocal =
      localLines.mid(prefixLen, totalLocal - prefixLen - suffixLen);
  QStringList midRemote =
      remoteLines.mid(prefixLen, totalRemote - prefixLen - suffixLen);

  const int m = midLocal.size();
  const int n = midRemote.size();

  // ========== 辅助 lambda：添加一对左右行 ==========
  // 数据格式: {leftType, leftText, rightType, rightText}
  QVariantList result;
  auto addRow = [&result](const QString &lt, const QString &ltext,
                          const QString &rt, const QString &rtext) {
    QVariantMap row;
    row["leftType"] = lt;
    row["leftText"] = ltext;
    row["rightType"] = rt;
    row["rightText"] = rtext;
    result.append(row);
  };

  // ========== 第二步：添加公共前缀（折叠） ==========
  const int contextLines = 3;
  if (prefixLen > 0) {
    if (prefixLen <= contextLines * 2) {
      for (int i = 0; i < prefixLen; ++i)
        addRow("same", localLines[i], "same", remoteLines[i]);
    } else {
      addRow(
          "fold",
          QString("... 省略 %1 行相同内容 ...").arg(prefixLen - contextLines),
          "fold",
          QString("... 省略 %1 行相同内容 ...").arg(prefixLen - contextLines));
      for (int i = prefixLen - contextLines; i < prefixLen; ++i)
        addRow("same", localLines[i], "same", remoteLines[i]);
    }
  }

  // ========== 第三步：差异区段 ==========
  // 先构建统一 diff 列表，再转为左右配对
  QList<QPair<QString, QString>> diffEntries; // <type, text>

  // Hirschberg LCS keeps exact alignment with linear workspace, including large XML files.
  const auto lengths = [&](int lBegin, int lEnd, int rBegin, int rEnd, bool reverse) {
    const int width = rEnd - rBegin;
    QVector<int> scores(width + 1, 0);
    for (int i = 0; i < lEnd - lBegin; ++i) {
      int diagonal = 0;
      const auto &line = midLocal[reverse ? lEnd - 1 - i : lBegin + i];
      for (int j = 1; j <= width; ++j) {
        const int previous = scores[j];
        if (line == midRemote[reverse ? rEnd - j : rBegin + j - 1])
          scores[j] = diagonal + 1;
        else
          scores[j] = qMax(scores[j], scores[j - 1]);
        diagonal = previous;
      }
    }
    return scores;
  };
  std::function<void(int, int, int, int)> align;
  align = [&](int lBegin, int lEnd, int rBegin, int rEnd) {
    if (lBegin == lEnd) {
      for (int j = rBegin; j < rEnd; ++j)
        diffEntries.append(qMakePair(QString("add"), midRemote[j]));
    } else if (rBegin == rEnd) {
      for (int i = lBegin; i < lEnd; ++i)
        diffEntries.append(qMakePair(QString("del"), midLocal[i]));
    } else if (lEnd - lBegin == 1) {
      int match = midRemote.indexOf(midLocal[lBegin], rBegin);
      if (match < 0 || match >= rEnd) {
        diffEntries.append(qMakePair(QString("del"), midLocal[lBegin]));
        match = -1;
      }
      for (int j = rBegin; j < rEnd; ++j)
        diffEntries.append(qMakePair(QString(j == match ? "same" : "add"), midRemote[j]));
    } else {
      const int middle = lBegin + (lEnd - lBegin) / 2;
      int split = 0;
      {
        const auto forward = lengths(lBegin, middle, rBegin, rEnd, false);
        const auto backward = lengths(middle, lEnd, rBegin, rEnd, true);
        const int width = rEnd - rBegin;
        for (int j = 1; j <= width; ++j)
          if (forward[j] + backward[width - j] > forward[split] + backward[width - split])
            split = j;
      }
      align(lBegin, middle, rBegin, rBegin + split);
      align(middle, lEnd, rBegin + split, rEnd);
    }
  };
  align(0, m, 0, n);

  // 将统一 diff 转换为左右并排格式
  // 策略：连续的 del 和 add 配对显示（类似 VS Code 的 side-by-side diff）
  int idx = 0;
  const int total = diffEntries.size();
  while (idx < total) {
    const QString &type = diffEntries[idx].first;
    if (type == "same") {
      addRow("same", diffEntries[idx].second, "same", diffEntries[idx].second);
      ++idx;
    } else {
      // 收集连续的 del 和 add 块
      QStringList dels, adds;
      while (idx < total && diffEntries[idx].first == "del") {
        dels.append(diffEntries[idx].second);
        ++idx;
      }
      while (idx < total && diffEntries[idx].first == "add") {
        adds.append(diffEntries[idx].second);
        ++idx;
      }
      // 并排配对输出
      const int maxPairs = qMax(dels.size(), adds.size());
      for (int k = 0; k < maxPairs; ++k) {
        QString lt = (k < dels.size()) ? "del" : "empty";
        QString ltext = (k < dels.size()) ? dels[k] : "";
        QString rt = (k < adds.size()) ? "add" : "empty";
        QString rtext = (k < adds.size()) ? adds[k] : "";
        addRow(lt, ltext, rt, rtext);
      }
    }
  }

  // ========== 第四步：添加公共后缀 ==========
  if (suffixLen > 0) {
    if (suffixLen <= contextLines * 2) {
      for (int i = totalLocal - suffixLen; i < totalLocal; ++i)
        addRow("same", localLines[i], "same", localLines[i]);
    } else {
      for (int i = totalLocal - suffixLen;
           i < totalLocal - suffixLen + contextLines; ++i)
        addRow("same", localLines[i], "same", localLines[i]);
      addRow(
          "fold",
          QString("... 省略 %1 行相同内容 ...").arg(suffixLen - contextLines),
          "fold",
          QString("... 省略 %1 行相同内容 ...").arg(suffixLen - contextLines));
    }
  }

  return result;
}
