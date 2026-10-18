#include "ProtocolSyncManager.h"
#include "core/ai/AiPromptFile.h"
#include "common/AppPaths.h"
#include "csg_upstream/CsgUpstreamConfigManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QElapsedTimer>
#include <QThread>
#include "common/JsonSettings.h"
#include "core/config/PresetCodec.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>

namespace {

int g_failureCount = 0;

void Expect(bool condition, const QString &message) {
  if (condition) {
    return;
  }
  QTextStream(stderr) << "FAIL: " << message << '\n';
  ++g_failureCount;
}

bool Wait(const std::function<bool()> &predicate, int timeout = 3000) {
  QElapsedTimer timer; timer.start();
  while (!predicate() && timer.elapsed() < timeout) {
    QCoreApplication::processEvents(); QThread::msleep(2);
  }
  return predicate();
}

bool Refresh(ProtocolSyncManager &manager) {
#ifndef Q_OS_MACOS
  return manager.fetchSyncStatus();
#else
  bool done = false, success = false;
  const auto connection = QObject::connect(&manager, &ProtocolSyncManager::statusFinished,
      &manager, [&](bool ok, const QString &) { done = true; success = ok; });
  const bool accepted = manager.fetchSyncStatus();
  const bool completed = accepted && Wait([&] { return done; });
  QObject::disconnect(connection);
  return completed && success;
#endif
}

QVariantMap Sync(ProtocolSyncManager &manager, const QString &name) {
#ifndef Q_OS_MACOS
  return manager.syncFile(name);
#else
  bool done = false; QVariantMap result;
  const auto connection = QObject::connect(&manager, &ProtocolSyncManager::syncFinished,
      &manager, [&](const QVariantMap &value) { result = value; done = true; });
  const auto pending = manager.syncFile(name);
  Expect(pending.value("pending").toBool(), "同步请求应返回 pending");
  Expect(Wait([&] { return done; }), "同步完成信号应到达");
  QObject::disconnect(connection);
  return result;
#endif
}

QByteArray ReadFile(const QString &path) {
  QFile file(path);
  Expect(file.open(QIODevice::ReadOnly), "应能读取文件: " + path);
  return file.isOpen() ? file.readAll() : QByteArray();
}

bool WriteFile(const QString &path, const QByteArray &content) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    Expect(false, "应能写入文件: " + path);
    return false;
  }
  const bool complete = file.write(content) == content.size();
  Expect(complete, "文件内容应完整写入: " + path);
  return complete;
}

bool CopyFile(const QString &source, const QString &destination) {
  QDir().mkpath(QFileInfo(destination).absolutePath());
  if (QFile::exists(destination)) {
    QFile::remove(destination);
  }
  const bool copied = QFile::copy(source, destination);
  Expect(copied, "应能复制文件到: " + destination);
  return copied;
}

SyncStatusItem FindSyncItem(const QList<SyncStatusItem> &items,
                            const QString &fileName) {
  for (const SyncStatusItem &item : items) {
    if (item.fileName == fileName) {
      return item;
    }
  }
  Expect(false, "同步状态应包含: " + fileName);
  return {};
}

QString ErrorText(const QVariantMap &result) {
  QStringList messages;
  const QVariantList errors = result.value("errors").toList();
  for (const QVariant &entry : errors) {
    messages.append(entry.toMap().value("error").toString());
  }
  return messages.join(" | ");
}

QByteArray InvalidCsgXml(const QString &version) {
  return QString(
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
             "<ProtCsgUpstreamConfig version=\"%1\" "
             "date=\"2026-08-05\"><AFNs>"
             "<AFN code=\"02\" name=\"链路接口检测\"/>"
             "</AFNs><DIs>"
             "<DI code=\"E0001000\" name=\"A\" type=\"None\"/>"
             "<DI code=\"E0001000\" name=\"B\" type=\"None\"/>"
             "</DIs></ProtCsgUpstreamConfig>")
      .arg(version)
      .toUtf8();
}

class FakeCredentials : public SecureCredentialStore {
public:
  FakeCredentials() : SecureCredentialStore(Domain::ProtocolSync) {}
  bool read(const QString &id, QByteArray *value, QString *error) const override {
    if (denyRead) { if (error) *error = "synthetic denied"; return false; }
    *value = values.value(id); return true;
  }
  bool write(const QString &id, const QByteArray &value, QString *error) override {
    if (writeDelay) QThread::msleep(writeDelay);
    if (denyWrite) { if (error) *error = "synthetic denied"; return false; }
    values[id] = corruptWrite ? QByteArray("corrupt") : value; return true;
  }
  bool remove(const QString &id, QString *) override { values.remove(id); return true; }
  QMap<QString, QByteArray> values;
  bool denyRead = false, denyWrite = false, corruptWrite = false;
  int writeDelay = 0;
};

void TestCredentials() {
  for (bool prompts : {false, true}) {
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    const QString prefix = prompts ? "promptSync/" : "protocolSync/";
    const QString profile(64, prompts ? 'a' : 'b');
    auto store = std::make_shared<FakeCredentials>();
    store->values[profile] = QJsonDocument(QJsonObject{{"username", "synthetic-user"}, {"password", "synthetic-legacy-secret"}}).toJson(QJsonDocument::Compact);
    JsonSettings settings(path);
    settings.setValue(prefix + "credentialProfile", profile); settings.setValue(prefix + "username", "synthetic-user"); settings.sync();
    ProtocolSyncManager::Dependencies dependencies;
    dependencies.promptFiles = prompts; dependencies.credentials = store; dependencies.settingsFile = path;
    ProtocolSyncManager manager(dependencies);
    Expect(Wait([&] { return !manager.credentialBusy(); }), "系统凭据读取完成");
    Expect(manager.password().isEmpty(), "SMB密码getter不回显明文");
    Expect(manager.credentialError().isEmpty(), "统一配置能找到迁移后的凭据引用");
    manager.setPassword("ignored-new-password"); manager.saveCredentials();
    QFile saved(path); Expect(saved.open(QIODevice::ReadOnly) && !saved.readAll().contains("synthetic-legacy-secret"), "JSON 不写入密码");
    const auto old = dir.filePath("LogAnalyQt.ini");
    QFile original(old); Expect(original.open(QIODevice::WriteOnly), "旧文件隔离样例"); original.write("[protocolSync]\npassword=unconfirmed-secret\n"); original.close();
    manager.loadCredentials(); Expect(Wait([&] { return !manager.credentialBusy(); }), "重复读取完成");
    Expect(QFile::exists(old), "业务构造器不迁移或清理未经确认的旧文件");
  }
}

void TestSmbPaths() {
  SmbShare::Target target; QString error;
  Expect(SmbShare::parse("smb://HOST/研发一部/protocol%20xml", &target, &error), "支持Unicode与编码空格");
  QTemporaryDir first, second, outside;
  QDir(first.path()).mkdir("protocol xml"); QDir(second.path()).mkdir("protocol xml");
  QList<SmbShare::Mount> mounts{{"//user@different/研发一部", first.path()}, {"//user@host/研发一部", second.path()}};
  Expect(SmbShare::resolve(target, mounts, &error) == QFileInfo(second.filePath("protocol xml")).canonicalFilePath(), "由host/share识别真实挂载，忽略重复卷名和系统用户名");
  Expect(SmbShare::resolve(target, {{"//host/wrong-share", second.path()}}, &error).isEmpty(), "共享不匹配不得使用挂载");
  Expect(SmbShare::resolve(target, {{"//host/研发一部", first.path()}, {"//host/研发一部", second.path()}}, &error).isEmpty(), "多个匹配实际挂载必须拒绝歧义");
  Expect(!SmbShare::resolve(target, {{"//host:445/研发一部", second.path()}}, &error).isEmpty(), "默认SMB端口与显式445等价");
  QDir(first.filePath("protocol xml")).removeRecursively();
  Expect(SmbShare::resolve(target, {{"//host/研发一部", first.path()}, {"//host/研发一部", second.path()}}, &error).isEmpty(), "另一个挂载缺少子目录也不能替用户选择挂载");
  SmbShare::Resolution resolution;
  Expect(SmbShare::resolve(target, {{"//host/研发一部", first.filePath("inaccessible-mount")}, {"//host/研发一部", second.path()}}, &error, &resolution).isEmpty() && resolution == SmbShare::Resolution::Ambiguous, "不可访问的同源挂载仍有身份，不能被跳过后消歧");
  const QStringList invalid{"smb://user:secret@host/share", "smb://@host/share", "smb://host/share?q=1", "smb://host/share#fragment", "smb://host/share/..", "smb://host/share/%2e%2e/x", "smb://host/share/a%2fb", "smb://host/share/%00", "smb://host//share", "smb://host", "file:///Volumes/share", "smb://host/share/a\\b", "smb://host:0/share", "smb://host:99999/share"};
  for (const auto &url : invalid) Expect(!SmbShare::parse(url, &target, &error), "拒绝歧义或越界地址: " + url);
  Expect(QFile::link(outside.path(), second.filePath("escape")), "创建隔离symlink测试");
  Expect(SmbShare::parse("smb://host/研发一部/escape", &target, &error), "语法允许普通目录名");
  Expect(SmbShare::resolve(target, mounts, &error).isEmpty(), "symlink逃逸共享根必须拒绝");
}

void TestConnectionLifecycle() {
  QTemporaryDir dir;
  ProtocolSyncManager::Dependencies dependencies;
  dependencies.settingsFile = dir.filePath("isolated.ini");
  dependencies.connectionTimeoutMs = 60;
  int opens = 0, completions = 0; bool success = true;
  dependencies.openShare = [&](const QUrl &url) { ++opens; Expect(url.userInfo().isEmpty(), "系统打开URL不含凭据"); return true; };
  dependencies.mounts = [] { return QList<SmbShare::Mount>{}; };
  ProtocolSyncManager manager(dependencies);
  manager.setSharePath("smb://fake.invalid/share");
  QObject::connect(&manager, &ProtocolSyncManager::connectionFinished, &manager,
    [&](bool ok, const QString &) { ++completions; success = ok; });
  manager.connectToShare();
  Expect(opens == 1 && completions == 0 && manager.connecting(), "系统open成功仅代表已请求，不代表连接成功");
  Expect(Wait([&] { return completions == 1; }), "系统挂载等待应超时");
  Expect(!success && !manager.connecting(), "超时不能伪报成功");
  manager.connectToShare(); manager.cancelConnection();
  QElapsedTimer timer; timer.start();
  while (timer.elapsed() < 120) { QCoreApplication::processEvents(); QThread::msleep(2); }
  Expect(completions == 2 && opens == 1 && !manager.connecting(), "旧物理probe未返回期间拒绝重建，取消后无迟到成功");
  manager.setSharePath("smb://user:secret@fake.invalid/share"); manager.connectToShare();
  Expect(opens == 1 && completions == 3 && !success, "拒绝凭据URL且不能交给系统");
  Expect(!Refresh(manager), "未连接共享不得直接读远端");
  dependencies.settingsFile = dir.path();
  ProtocolSyncManager unwritable(dependencies);
  unwritable.setSharePath("smb://fake.invalid/share");
  bool rejected = false;
  QObject::connect(&unwritable, &ProtocolSyncManager::connectionFinished, &unwritable,
    [&](bool ok, const QString &message) { rejected = !ok && message.contains("便携配置"); });
  unwritable.connectToShare();
  Expect(rejected && opens == 1, "实际便携配置写失败需明确上报，不把系统请求伪作成功");
  dependencies.settingsFile = dir.filePath("ambiguous.ini");
  dependencies.connectionTimeoutMs = 3000;
  dependencies.mounts = [path = dir.path()] { return QList<SmbShare::Mount>{{"//fake.invalid/share", path}, {"//fake.invalid/share", path + "/inaccessible-mount"}}; };
  ProtocolSyncManager ambiguous(dependencies);
  ambiguous.setSharePath("smb://fake.invalid/share"); bool ambiguityReported = false;
  QObject::connect(&ambiguous, &ProtocolSyncManager::connectionFinished, &ambiguous,
    [&](bool ok, const QString &message) { ambiguityReported = !ok && message.contains("多个"); });
  ambiguous.connectToShare();
  Expect(Wait([&] { return ambiguityReported; }, 1000), "已确认多挂载歧义应立即明报，不能吞成普通超时");
}

void TestAsyncLifetime() {
  QTemporaryDir dir;
  auto release = std::make_shared<std::atomic_bool>(false);
  auto entered = std::make_shared<std::atomic_bool>(false);
  auto exited = std::make_shared<std::atomic_bool>(false);
  ProtocolSyncManager::Dependencies dependencies;
  dependencies.settingsFile = dir.filePath("isolated.ini");
  int openCount = 0;
  dependencies.openShare = [&](const QUrl &) { ++openCount; return true; };
  dependencies.mounts = [release, entered, exited] {
    *entered = true;
    while (!release->load()) QThread::msleep(2);
    *exited = true;
    return QList<SmbShare::Mount>{};
  };
  auto manager = std::make_unique<ProtocolSyncManager>(dependencies);
  manager->setSharePath("smb://fake.invalid/share"); manager->connectToShare();
  Expect(Wait([&] { return entered->load(); }), "后台mount检查已开始");
  manager->cancelConnection();
  for (int i = 0; i < 20; ++i) manager->connectToShare();
  Expect(openCount == 1, "阻塞probe未结束时连续重试不会积压线程或再次打开Finder");
  QElapsedTimer timer; timer.start(); manager.reset();
  Expect(timer.elapsed() < 100, "析构不得等待阻塞的远端I/O");
  *release = true;
  Expect(Wait([&] { return exited->load(); }), "被析构对象的后台任务安全退出");

  release = std::make_shared<std::atomic_bool>(false);
  entered = std::make_shared<std::atomic_bool>(false);
  auto calls = std::make_shared<std::atomic_int>(0);
  dependencies.mounts = [release, entered, calls, path = dir.path()] {
    if (calls->fetch_add(1) > 0) {
      *entered = true;
      while (!release->load()) QThread::msleep(2);
    }
    return QList<SmbShare::Mount>{{"//fake.invalid/share", path}};
  };
  ProtocolSyncManager current(dependencies);
  current.setSharePath("smb://fake.invalid/share");
  bool connected = false; int updates = 0;
  QObject::connect(&current, &ProtocolSyncManager::connectionFinished, &current, [&](bool ok, const QString &) { connected = ok; });
  QObject::connect(&current, &ProtocolSyncManager::statusUpdated, &current, [&] { ++updates; });
  current.connectToShare(); Expect(Wait([&] { return connected; }), "冻结操作测试连接完成");
  Expect(current.fetchSyncStatus(), "后台状态读取应被接受");
  Expect(Wait([&] { return entered->load(); }), "远端读取不会阻塞事件循环");
  current.cancelPendingOperations(); current.setSharePath("smb://new.invalid/other");
  *release = true;
  Expect(Wait([&] { return !current.busy(); }), "取消后台读取最终释放busy");
  Expect(updates == 0 && current.statusModel()->items().isEmpty(), "旧目标迟到结果不能覆盖新目标状态");
}

void TestDiffReadFailures() {
  QTemporaryDir remote;
  ProtocolSyncManager::Dependencies dependencies;
  dependencies.settingsFile = remote.filePath("isolated.ini");
  dependencies.openShare = [](const QUrl &) { return true; };
  dependencies.mounts = [path = remote.path()] { return QList<SmbShare::Mount>{{"//fake.invalid/share", path}}; };
  ProtocolSyncManager manager(dependencies);
  manager.setSharePath("smb://fake.invalid/share"); bool connected = false;
  QObject::connect(&manager, &ProtocolSyncManager::connectionFinished, &manager, [&](bool ok, const QString &) { connected = ok; });
  manager.connectToShare(); Expect(Wait([&] { return connected; }), "差异错误测试fake挂载连接成功");
  QDir().mkpath(AppPaths::protocolDirectory());
  const QString local = QDir(AppPaths::protocolDirectory()).filePath("698_protocol.xml");
  const QString remoteFile = remote.filePath("698_protocol.xml");
  WriteFile(local, "local-line\n"); WriteFile(remoteFile, "remote-line\n");
  int successes = 0, failures = 0; QString lastError; QVariantList lastGoodDiff;
  QObject::connect(&manager, &ProtocolSyncManager::diffFinished, &manager, [&](const QString &, const QVariantList &diff) { ++successes; lastGoodDiff = diff; });
  QObject::connect(&manager, &ProtocolSyncManager::statusFinished, &manager, [&](bool ok, const QString &error) { if (!ok) { ++failures; lastError = error; } });
  const auto request = [&] { manager.getXmlDiff("698_protocol.xml"); Expect(Wait([&] { return !manager.busy(); }), "差异读取任务应完成"); };
  request(); Expect(successes == 1 && failures == 0 && !lastGoodDiff.isEmpty(), "正常差异成功完成");
  const QVariantList originalDiff = lastGoodDiff;
  const auto remotePermissions = QFile::permissions(remoteFile);
  Expect(QFile::setPermissions(remoteFile, {}), "隔离远端文件设为不可读");
  request();
  Expect(QFile::setPermissions(remoteFile, remotePermissions), "恢复远端文件权限");
  Expect(successes == 1 && failures == 1 && lastError.contains("无法打开") && lastGoodDiff == originalDiff, "存在但不可打开的远端不得假成功或替换旧diff");
  const auto localPermissions = QFile::permissions(local);
  Expect(QFile::setPermissions(local, {}), "隔离本地文件设为不可读");
  request();
  Expect(QFile::setPermissions(local, localPermissions), "恢复本地文件权限");
  Expect(successes == 1 && failures == 2 && lastError.contains("本地"), "本地存在但打不开也必须明确失败");
  QFile::remove(remoteFile); QDir().mkdir(remoteFile);
  request();
  Expect(successes == 1 && failures == 3 && !lastError.isEmpty() && lastGoodDiff == originalDiff, "目录导致读取失败不可当空文件成功");
  QDir(remoteFile).removeRecursively();
  const QString localDirectory = AppPaths::protocolDirectory();
  const auto directoryPermissions = QFile::permissions(localDirectory);
  Expect(QFile::setPermissions(localDirectory, QFileDevice::ReadOwner | QFileDevice::WriteOwner), "隔离本地目录移除遍历权限");
  request();
  Expect(QFile::setPermissions(localDirectory, directoryPermissions), "恢复本地目录遍历权限");
  Expect(successes == 1 && failures == 4 && lastError.contains("状态无法读取"), "无目录遍历权限不能假装目标不存在");
  request();
  Expect(successes == 2 && failures == 4, "真实不存在的一侧仍可按空侧比较");
  QFile::remove(local); request();
  Expect(successes == 3 && failures == 4 && !lastGoodDiff.isEmpty() && !lastGoodDiff.first().toMap().value("text").toString().contains("无法读取"), "两侧真实缺失可报告为空，不能与读取错误混同");
}

void TestProtocolSync(const QString &sourceXml) {
  const QString localRoot = AppPaths::writableDataRoot();
  const QString localPath =
      QDir(AppPaths::protocolDirectory())
          .filePath("csg_upstream_protocol.xml");
  QTemporaryDir remote;
  Expect(remote.isValid(), "应能创建临时远端目录");
  if (!remote.isValid()) {
    return;
  }

  ProtocolSyncManager::Dependencies dependencies;
  dependencies.settingsFile = remote.filePath("isolated.ini");
  dependencies.openShare = [](const QUrl &) { return true; };
  dependencies.mounts = [path = remote.path()] { return QList<SmbShare::Mount>{{"//test.invalid/share", path}}; };
  ProtocolSyncManager manager(dependencies);
  #ifdef Q_OS_MACOS
  manager.setSharePath("smb://test.invalid/share");
  bool connected = false;
  QObject::connect(&manager, &ProtocolSyncManager::connectionFinished, &manager,
                   [&](bool ok, const QString &) { connected = ok; });
  manager.connectToShare();
  Expect(Wait([&] { return connected; }), "fake挂载匹配后连接成功");
#else
  manager.setSharePath(remote.path());
#endif
  CopyFile(sourceXml, localPath);
  Expect(Refresh(manager), "临时远端目录应可读取");

  SyncStatusItem item = FindSyncItem(
      manager.statusModel()->items(), "csg_upstream_protocol.xml");
  Expect(item.localExists && !item.remoteExists,
         "新 XML 应显示本地存在、远端缺失");
  Expect(item.action == "upload", "远端缺失仍使用 upload 动作");
  Expect(!FindSyncItem(manager.statusModel()->items(),
                       "698_protocol.xml")
              .fileName.isEmpty(),
         "状态模型应继续包含现有协议项");

  WriteFile(localPath, InvalidCsgXml("v2.0.0"));
  Expect(Refresh(manager), "非法本地 XML 不应阻断状态获取");
  QVariantMap result = Sync(manager, "csg_upstream_protocol.xml");
  Expect(result.value("failed").toStringList().contains(
             "csg_upstream_protocol.xml"),
         "语义非法的本地 XML 应上传失败");
  Expect(ErrorText(result).contains("协议配置语义错误"),
         "上传错误应说明协议配置语义错误");
  const QString remotePath =
      QDir(remote.path()).filePath("csg_upstream_protocol.xml");
  Expect(!QFile::exists(remotePath), "非法上传不得生成远端文件");

  CopyFile(sourceXml, localPath);
  const QByteArray originalLocal = ReadFile(localPath);
  WriteFile(remotePath, InvalidCsgXml("v2.0.1"));
  Expect(Refresh(manager), "非法远端 XML 不应阻断状态获取");
  item = FindSyncItem(manager.statusModel()->items(),
                      "csg_upstream_protocol.xml");
  Expect(item.action == "download", "远端高版本应进入下载动作");

  int downloadSignalCount = 0;
  QString downloadedFile;
  QString hotReloadError;
  QObject::connect(
      &manager, &ProtocolSyncManager::protocolFileDownloaded,
      [&downloadSignalCount, &downloadedFile, &hotReloadError,
       localPath](const QString &fileName) {
        ++downloadSignalCount;
        downloadedFile = fileName;
        CsgUpstreamConfigManager::instance().loadXml(localPath,
                                                      &hotReloadError);
      });

  result = Sync(manager, "csg_upstream_protocol.xml");
  Expect(result.value("failed").toStringList().contains(
             "csg_upstream_protocol.xml"),
         "语义非法的远端 XML 应下载失败");
  Expect(ErrorText(result).contains("协议配置语义错误"),
         "下载错误应说明协议配置语义错误");
  Expect(ReadFile(localPath) == originalLocal,
         "非法下载不得覆盖本地 XML");
  Expect(downloadSignalCount == 0, "非法下载不得发出成功信号");

  QByteArray validRemote = originalLocal;
  validRemote.replace("version=\"v2.0.0\"", "version=\"v2.0.1\"");
  validRemote.replace("name=\"终端登录\"", "name=\"同步后登录\"");
  WriteFile(remotePath, validRemote);
  QString loadError;
  Expect(CsgUpstreamConfigManager::instance().loadXml(localPath, &loadError),
         "合法下载测试应先加载旧配置: " + loadError);
  Expect(Refresh(manager), "合法远端 XML 应能刷新状态");
  result = Sync(manager, "csg_upstream_protocol.xml");
  Expect(result.value("downloaded").toStringList().contains(
             "csg_upstream_protocol.xml"),
         "合法高版本 XML 应下载成功");
  Expect(downloadSignalCount == 1 &&
             downloadedFile == "csg_upstream_protocol.xml",
         "合法下载应发出一次文件名信号");
  Expect(hotReloadError.isEmpty(),
         "下载信号触发的配置加载不应失败: " + hotReloadError);
  Expect(CsgUpstreamConfigManager::instance().getVersion() == "v2.0.1",
         "下载后内存配置版本应立即更新");
  const CsgUpstreamDiDef *login =
      CsgUpstreamConfigManager::instance().getDiDef("E0001000");
  Expect(login && login->name == "同步后登录",
         "下载后内存配置应立即使用新 DI 名称");

#ifdef Q_OS_MACOS
  CopyFile(sourceXml, localPath);
  QByteArray olderRemote = ReadFile(localPath);
  olderRemote.replace("version=\"v2.0.0\"", "version=\"v1.0.0\"");
  WriteFile(remotePath, olderRemote);
  Expect(Refresh(manager), "覆盖上传前读取旧远端");
  Expect(FindSyncItem(manager.statusModel()->items(), "csg_upstream_protocol.xml").action == "upload", "本地高版本触发上传");
  const auto permissions = QFile::permissions(remote.path());
  Expect(QFile::setPermissions(remote.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner), "设置隔离目录只读以模拟原子写失败");
  result = Sync(manager, "csg_upstream_protocol.xml");
  Expect(QFile::setPermissions(remote.path(), permissions), "恢复隔离目录权限");
  Expect(result.value("failed").toStringList().contains("csg_upstream_protocol.xml"), "远端临时写失败应报告失败");
  Expect(ReadFile(remotePath) == olderRemote, "原子上传失败保留旧远端完整内容");
  result = Sync(manager, "csg_upstream_protocol.xml");
  Expect(result.value("uploaded").toStringList().contains("csg_upstream_protocol.xml"), "恢复可写后应原子上传成功");
  Expect(ReadFile(remotePath) == ReadFile(localPath), "原子上传完整替换远端");

  const QString oldTemporary = localPath + ".tmp";
  WriteFile(oldTemporary, "preexisting-user-temporary-file");
  WriteFile(localPath, olderRemote);
  WriteFile(remotePath, originalLocal);
  Expect(Refresh(manager), "并发下载前本地为旧版本");
  ProtocolSyncManager peer(dependencies);
  peer.setSharePath("smb://test.invalid/share"); bool peerConnected = false;
  QObject::connect(&peer, &ProtocolSyncManager::connectionFinished, &peer, [&](bool ok, const QString &) { peerConnected = ok; });
  peer.connectToShare(); Expect(Wait([&] { return peerConnected; }), "第二个隔离实例连接完成");
  Expect(Refresh(peer), "第二实例读取下载状态");
  bool firstDone = false, peerDone = false;
  QVariantMap firstResult, peerResult;
  QObject::connect(&manager, &ProtocolSyncManager::syncFinished, &manager, [&](const QVariantMap &value) { firstResult = value; firstDone = true; });
  QObject::connect(&peer, &ProtocolSyncManager::syncFinished, &peer, [&](const QVariantMap &value) { peerResult = value; peerDone = true; });
  manager.syncFile("csg_upstream_protocol.xml"); peer.syncFile("csg_upstream_protocol.xml");
  Expect(Wait([&] { return firstDone && peerDone; }), "两个实例下载任务均完成");
  Expect(firstResult.value("downloaded").toStringList().contains("csg_upstream_protocol.xml") && peerResult.value("downloaded").toStringList().contains("csg_upstream_protocol.xml"), "两个实例使用独立临时文件并完成原子替换");
  Expect(ReadFile(localPath) == originalLocal && ReadFile(oldTemporary) == "preexisting-user-temporary-file", "唯一临时文件保全既有.tmp及完整本地配置");

  QTemporaryDir outside;
  const QString external = outside.filePath("external.xml");
  WriteFile(external, originalLocal);
  WriteFile(localPath, olderRemote);
  Expect(Refresh(manager), "在symlink切换前冻结下载动作");
  QFile::remove(remotePath);
  Expect(QFile::link(external, remotePath), "创建隔离远端文件symlink");
  Expect(!Refresh(manager), "拒绝从网盘协议symlink读取元数据");
  result = Sync(manager, "csg_upstream_protocol.xml");
  Expect(result.value("failed").toStringList().contains("csg_upstream_protocol.xml") && ReadFile(external) == originalLocal, "下载拒绝symlink且外部目标完整保留");
  bool diffRejected = false;
  QObject::connect(&manager, &ProtocolSyncManager::statusFinished, &manager, [&](bool ok, const QString &message) { diffRejected = !ok && message.contains("符号链接"); });
  manager.getXmlDiff("csg_upstream_protocol.xml");
  Expect(Wait([&] { return diffRejected; }), "差异比较不得读取网盘协议symlink");
  QFile::remove(remotePath); WriteFile(remotePath, olderRemote); WriteFile(localPath, originalLocal);
  Expect(Refresh(manager), "在symlink切换前冻结上传动作");
  QFile::remove(remotePath); Expect(QFile::link(external, remotePath), "创建上传symlink测试");
  result = Sync(manager, "csg_upstream_protocol.xml");
  Expect(result.value("failed").toStringList().contains("csg_upstream_protocol.xml") && ReadFile(external) == originalLocal, "上传拒绝symlink且不能原子替换外部目标");

#endif
  Expect(localRoot.contains("test-runtime/CsgProtocolSync"),
         "同步测试必须使用独立便携运行目录: " + localRoot);
  const QString localProtocolDir = AppPaths::protocolDirectory();
  if (localRoot.contains("test-runtime/CsgProtocolSync")) {
    Expect(QDir(localProtocolDir).removeRecursively(),
           "应能清理同步测试的协议目录");
  }
}

void TestPromptSync() {
  QTemporaryDir remote;
  const QString directory = AppPaths::promptDirectory();
  Expect(directory.contains("test-runtime/CsgProtocolSync"), "提示词同步测试目录隔离");
  if (!directory.contains("test-runtime/CsgProtocolSync")) return;
  QDir().mkpath(directory);
  const QString name = AiPromptFile::FileName;
  const QString localPath = QDir(directory).filePath(name), remotePath = remote.filePath(name);
  ProtocolSyncManager::Dependencies deps;
  deps.promptFiles = true; deps.settingsFile = remote.filePath("settings.ini");
  deps.openShare = [](const QUrl &) { return true; };
  deps.mounts = [path = remote.path()] { return QList<SmbShare::Mount>{{"//prompt.invalid/share", path}}; };
  ProtocolSyncManager manager(deps);
#ifdef Q_OS_MACOS
  manager.setSharePath("smb://prompt.invalid/share");
  bool connected = false;
  QObject::connect(&manager, &ProtocolSyncManager::connectionFinished, &manager, [&](bool ok, const QString &) { connected = ok; });
  manager.connectToShare(); Expect(Wait([&] { return connected; }), "提示词挂载连接完成");
#else
  manager.setSharePath(remote.path());
#endif
  manager.saveCredentials();
  JsonSettings settings(deps.settingsFile);
  Expect(settings.value("promptSync/sharePath") == manager.sharePath() && !settings.contains("protocolSync/sharePath"), "提示词共享配置独立保存");
  QString defaultError;
  const auto beforeCreation = QDateTime::currentDateTimeUtc().addSecs(-2);
  Expect(AiPromptFile::ensure(localPath, &defaultError), "本地缺失时释放默认提示词");
  const QByteArray defaults = ReadFile(localPath);
  const auto creationTime = QFileInfo(localPath).lastModified();
  Expect(creationTime >= beforeCreation, "默认文件保留正常生成时间，不设置特殊历史时间");
  Expect(AiPromptFile::ensure(localPath, &defaultError) && ReadFile(localPath) == defaults && QFileInfo(localPath).lastModified() == creationTime, "本地已有文件时不重新释放，也不修改时间");
  WriteFile(remotePath, "shared customization");
  { QFile file(remotePath); Expect(file.open(QIODevice::ReadWrite) && file.setFileTime(QDateTime::currentDateTimeUtc().addYears(-1), QFileDevice::FileModificationTime), "模拟较早的共享文件"); }
  Expect(Refresh(manager) && FindSyncItem(manager.statusModel()->items(), name).action == "upload", "默认文件与其他文件采用相同同步规则，不优先下载");
  Expect(ReadFile(localPath) == defaults && ReadFile(remotePath) == "shared customization", "释放默认和刷新状态均不执行同步");
  auto manualResult = Sync(manager, name);
  Expect(manualResult.value("uploaded").toStringList().contains(name) && ReadFile(remotePath) == defaults, "只有人工触发同步才按状态执行上传");
  Expect(QFile::remove(remotePath), "清理临时共享文件以测试初次上传");
  WriteFile(localPath, "prompt A");
  Expect(Refresh(manager) && manager.statusModel()->items().size() == 1, "提示词清单不混入XML");
  auto result = Sync(manager, name);
  Expect(result.value("uploaded").toStringList().contains(name) && ReadFile(remotePath) == "prompt A", "上传提示词并原样保存");
  Expect(FindSyncItem(manager.statusModel()->items(), name).action == "equal", "内容相同忽略修改时间，避免重复同步");
  const auto stamp = QDateTime::currentDateTimeUtc().addSecs(-100);
  const auto time = [&](const QString &path, int offset) {
    QFile file(path); Expect(file.open(QIODevice::ReadWrite) && file.setFileTime(stamp.addSecs(offset), QFileDevice::FileModificationTime), "设置隔离文件修改时间");
  };
  WriteFile(remotePath, "prompt B"); time(localPath, 0); time(remotePath, 10);
  Expect(Refresh(manager) && FindSyncItem(manager.statusModel()->items(), name).action == "download", "较新的共享提示词待下载");
  result = Sync(manager, name);
  Expect(result.value("downloaded").toStringList().contains(name) && ReadFile(localPath) == "prompt B", "合法下载原子替换本地提示词");
  for (const auto &invalid : QList<QByteArray>{QByteArray(" "), QByteArray::fromHex("ff"), QByteArray("x\0y", 3)}) {
    WriteFile(remotePath, invalid); time(localPath, 0); time(remotePath, 10);
    Expect(Refresh(manager), "非法文本仍可列出状态"); result = Sync(manager, name);
    Expect(result.value("failed").toStringList().contains(name) && ReadFile(localPath) == "prompt B", "非法下载保留本地原文");
  }
  WriteFile(remotePath, "remote version"); WriteFile(localPath, "local version"); time(localPath, 0); time(remotePath, 0);
  Expect(Refresh(manager) && FindSyncItem(manager.statusModel()->items(), name).action == "conflict", "同时间不同内容显式冲突");
  result = Sync(manager, name);
  Expect(result.value("failed").toStringList().contains(name) && ReadFile(localPath) == "local version" && ReadFile(remotePath) == "remote version", "冲突保留双方文件");
#ifdef Q_OS_MACOS
  bool diffDone = false;
  QObject::connect(&manager, &ProtocolSyncManager::diffFinished, &manager, [&](const QString &, const QVariantList &diff) { diffDone = !diff.isEmpty(); });
  manager.getXmlDiff(name); Expect(Wait([&] { return diffDone; }), "提示词复用逐行差异查看");
#else
  Expect(!manager.getXmlDiff(name).isEmpty(), "提示词复用逐行差异查看");
#endif
  time(localPath, 0); time(remotePath, 10); Expect(Refresh(manager), "下载前记录快照");
  WriteFile(localPath, "edited after status"); result = Sync(manager, name);
  Expect(result.value("failed").toStringList().contains(name) && ReadFile(localPath) == "edited after status", "状态过期不覆盖新编辑");
  WriteFile(localPath, QByteArray::fromHex("ff")); time(localPath, 20); time(remotePath, 0); Expect(Refresh(manager), "非法上传前读取状态");
  result = Sync(manager, name);
  Expect(result.value("failed").toStringList().contains(name) && ReadFile(remotePath) == "remote version", "非法上传保留远端原文");
  WriteFile(localPath, "valid local"); time(localPath, 20); time(remotePath, 0); Expect(Refresh(manager), "上传前记录快照");
  WriteFile(remotePath, "edited remote after status"); result = Sync(manager, name);
  Expect(result.value("failed").toStringList().contains(name) && ReadFile(remotePath) == "edited remote after status", "状态过期不覆盖共享端新编辑");
  WriteFile(localPath, "valid local"); WriteFile(remotePath, "remote version"); time(localPath, 20); time(remotePath, 0); Expect(Refresh(manager), "有效上传快照");
  result = Sync(manager, name);
  Expect(result.value("uploaded").toStringList().contains(name) && ReadFile(remotePath) == "valid local", "有效修改可重新上传");
  Expect(QDir(directory).removeRecursively(), "清理隔离提示词目录");
}

#ifdef Q_OS_MACOS
void TestPresetExchange() {
  QTemporaryDir remote;
  const auto config = remote.filePath("settings.json");
  QJsonObject messages{{"version", "v1.0.1"}, {"date", "2026-10-18"}, {"presets", QJsonArray{QJsonObject{{"protocol", "698"}, {"summary", "local"}, {"content", "6816"}}}}};
  QString error;
  Expect(ConfigJson::write(config, {{"schemaVersion", 1}, {"messages", messages}}, &error), "预置报文 JSON 样例");
  ProtocolSyncManager::Dependencies dependencies;
  dependencies.settingsFile = config; dependencies.openShare = [](const QUrl &) { return true; };
  dependencies.mounts = [path = remote.path()] { return QList<SmbShare::Mount>{{"//fake.invalid/share", path}}; };
  ProtocolSyncManager manager(dependencies); manager.setSharePath("smb://fake.invalid/share"); bool connected = false;
  QObject::connect(&manager, &ProtocolSyncManager::connectionFinished, &manager, [&](bool ok, const QString &) { connected = ok; });
  manager.connectToShare(); Expect(Wait([&] { return connected; }), "预置报文假共享连接");
  Expect(Refresh(manager), "生成 XML 交换视图");
  auto result = Sync(manager, "preset_messages.xml");
  Expect(result.value("uploaded").toStringList().contains("preset_messages.xml"), "JSON 预置报文可上传 XML");
  QJsonObject decoded;
  Expect(PresetCodec::decode(ReadFile(remote.filePath("preset_messages.xml")), &decoded, &error) && decoded == messages, "交换 XML 保留报文版本和内容");
  auto incoming = messages; incoming.insert("version", "v1.0.2");
  auto rows = incoming.value("presets").toArray(); auto row = rows.first().toObject(); row.insert("summary", "remote"); rows[0] = row; incoming.insert("presets", rows);
  WriteFile(remote.filePath("preset_messages.xml"), PresetCodec::encode(incoming));
  Expect(Refresh(manager), "预置报文下载快照");
  auto edited = messages; edited.insert("version", "v1.0.9");
  ConfigJson::update(config, [&](QJsonObject &root, QString *) { root.insert("messages", edited); return true; });
  result = Sync(manager, "preset_messages.xml");
  Expect(result.value("failed").toStringList().contains("preset_messages.xml"), "JSON 新编辑不能被状态过期的下载覆盖");
  ConfigJson::update(config, [&](QJsonObject &root, QString *) { root.insert("messages", messages); return true; });
  Expect(Refresh(manager), "重新读取预置报文状态"); result = Sync(manager, "preset_messages.xml");
  QJsonObject saved; ConfigJson::read(config, &saved);
  Expect(result.value("downloaded").toStringList().contains("preset_messages.xml") && saved.value("messages").toObject() == incoming, "XML 下载提交到统一 JSON");
  Expect(!QFile::exists(QDir(AppPaths::protocolDirectory()).filePath("preset_messages.xml")), "不会在旧配置位置重新创建 XML");
}
#endif

} // namespace

int main(int argc, char *argv[]) {
  QCoreApplication application(argc, argv);
#ifdef Q_OS_MACOS
  QTemporaryDir isolatedHome(QDir(QCoreApplication::applicationDirPath()).filePath("test-home-XXXXXX"));
  if (!isolatedHome.isValid()) return 1;
  AppPaths::testHomeDirectory = isolatedHome.path();
#endif

  const QString sourceXml =
      QStringLiteral(CSG_TEST_SOURCE_DIR "/protocol_xml/"
                                     "csg_upstream_protocol.xml");
#ifdef Q_OS_MACOS
  TestSmbPaths();
  TestCredentials();
  TestConnectionLifecycle();
  TestAsyncLifetime();
  TestDiffReadFailures();
#endif
  TestProtocolSync(sourceXml);
  TestPromptSync();
#ifdef Q_OS_MACOS
  TestPresetExchange();
#endif

  if (g_failureCount == 0) {
    QTextStream(stdout) << "All protocol sync tests passed.\n";
  }
  return g_failureCount == 0 ? 0 : 1;
}
