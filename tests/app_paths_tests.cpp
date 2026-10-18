#include "common/AppPaths.h"
#include "common/AppSettings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

namespace {
int failures = 0;
void expect(bool ok, const QString &message) {
  if (!ok) { QTextStream(stderr) << "FAIL: " << message << '\n'; ++failures; }
}
void write(const QString &path, const QByteArray &bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  expect(file.open(QIODevice::WriteOnly), "open fixture: " + path);
  expect(file.write(bytes) == bytes.size(), "write fixture: " + path);
}
QByteArray read(const QString &path) {
  QFile file(path); expect(file.open(QIODevice::ReadOnly), "read fixture: " + path);
  return file.readAll();
}

void paths() {
  expect(AppPaths::portableDataRootForApplicationDir("/tmp/portable/LogAnalyQt.app/Contents/MacOS") == "/tmp/portable", "legacy bundle location remains discoverable");
  expect(AppPaths::portableDataRootForApplicationDir("/tmp/portable/bin") == "/tmp/portable/bin", "non-bundle portable location");
  const QString root = AppPaths::writableDataRoot();
#ifdef Q_OS_MACOS
  expect(root == QDir(AppPaths::testHomeDirectory).filePath("Library/Application Support/LogAnalyQt"), "Mac data uses isolated standard Application Support directory");
  expect(AppPaths::logDirectory() == QDir(AppPaths::testHomeDirectory).filePath("Library/Logs/LogAnalyQt"), "Mac log uses standard Logs directory");
#else
  expect(root == QCoreApplication::applicationDirPath(), "Windows/non-Mac executable remains portable");
  expect(AppPaths::logDirectory() == QDir(root).filePath("log"), "portable logs stay beside executable");
#endif
  expect(AppPaths::protocolDirectory() == QDir(root).filePath("protocol_xml"), "protocol directory");
  expect(AppPaths::promptDirectory() == QDir(root).filePath("prompts"), "prompt directory");
  expect(AppPaths::presetDirectory() == QDir(root).filePath("preset"), "preset directory");
  expect(appSettings().fileName() == QDir(root).filePath("settings.json"), "settings follow the platform data root");
}

#ifdef Q_OS_MACOS
namespace fs = std::filesystem;
fs::path path(const QString &value) { return fs::path(value.toStdString()); }
void migration() {
  QTemporaryDir directory;
  const QString old = directory.filePath("Applications"), data = directory.filePath("home/Library/Application Support/LogAnalyQt"), logs = directory.filePath("home/Library/Logs/LogAnalyQt");
  const QStringList names = {"LogAnalyQt.ini", "protocol_xml/用户协议.xml", "prompts/user.md", "preset/smart_framing/user.json", "ssh-known-hosts.json", "log/log.txt"};
  for (const auto &name : names) write(old + "/" + name, ("preserve " + name).toUtf8());
  QDir().mkpath(old + "/preset/empty");
  const auto timestamp = fs::file_time_type::clock::now() - std::chrono::hours(48);
  fs::last_write_time(path(old + "/protocol_xml/用户协议.xml"), timestamp);
  MacDataMigration::initialize({path(old)}, path(data), path(logs));
  for (const auto &name : names) {
    const QString destination = name.startsWith("log/") ? logs + name.mid(3) : data + "/" + name;
    expect(read(destination) == read(old + "/" + name), "migrated bytes and original preserved: " + name);
  }
  expect(QDir(data + "/preset/empty").exists(), "empty preset directories preserved");
  expect(fs::last_write_time(path(data + "/protocol_xml/用户协议.xml")) == timestamp, "protocol modification time preserved for synchronization");
  write(data + "/LogAnalyQt.ini", "user changed settings after migration");
  MacDataMigration::initialize({path(old)}, path(data), path(logs));
  expect(read(data + "/LogAnalyQt.ini") == "user changed settings after migration", "restart never reimports stale legacy settings");
  expect(read(old + "/LogAnalyQt.ini") == "preserve LogAnalyQt.ini", "old settings remain unchanged");
}

void failuresAndResume() {
  QTemporaryDir directory;
  const QString old = directory.filePath("old"), data = directory.filePath("data"), logs = directory.filePath("logs");
  write(old + "/LogAnalyQt.ini", "current settings");
  write(old + "/prompts/user.md", "edited prompt");
  write(data + "/LogAnalyQt.ini", "different prior settings");
  const auto initialize = [&] { MacDataMigration::initialize({path(old)}, path(data), path(logs)); };
  bool rejected = false;
  try { initialize(); } catch (const std::exception &) { rejected = true; }
  expect(rejected, "different existing settings block migration");
  expect(read(data + "/LogAnalyQt.ini") == "different prior settings" && read(old + "/LogAnalyQt.ini") == "current settings", "conflict preserves both copies");
  expect(!QFile::exists(data + "/prompts/user.md") && !QFile::exists(data + "/.legacy-data-imported-v1"), "conflict detected before copying other data or committing marker");
  write(data + "/LogAnalyQt.ini", "current settings");
  initialize();
  expect(read(data + "/prompts/user.md") == "edited prompt", "identical partial import resumes safely");

  QTemporaryDir linked;
  const QString source = linked.filePath("old"), target = linked.filePath("data");
  write(linked.filePath("outside"), "outside stays private");
  QDir().mkpath(source);
  expect(QFile::link(linked.filePath("outside"), source + "/LogAnalyQt.ini"), "create source symbolic link fixture");
  rejected = false;
  try { MacDataMigration::initialize({path(source)}, path(target), path(linked.filePath("logs"))); } catch (const std::exception &) { rejected = true; }
  expect(rejected && !QFile::exists(target + "/LogAnalyQt.ini"), "source symbolic links cannot escape migration scope");
  QFile::remove(source + "/LogAnalyQt.ini");
  write(source + "/LogAnalyQt.ini", "source");
  expect(QFile::link(linked.filePath("outside"), target + "/LogAnalyQt.ini"), "create destination symbolic link fixture");
  rejected = false;
  try { MacDataMigration::initialize({path(source)}, path(target), path(linked.filePath("logs"))); } catch (const std::exception &) { rejected = true; }
  expect(rejected && read(linked.filePath("outside")) == "outside stays private", "destination symbolic links never overwrite outside data");
}

void unreadableAndInvalidMarker() {
  QTemporaryDir directory;
  const QString old = directory.filePath("old"), data = directory.filePath("data"), logs = directory.filePath("logs");
  write(old + "/prompts/user.md", "keep edited prompt");
  fs::permissions(path(old + "/prompts"), fs::perms::none);
  bool rejected = false;
  try { MacDataMigration::initialize({path(old)}, path(data), path(logs)); } catch (const std::exception &) { rejected = true; }
  fs::permissions(path(old + "/prompts"), fs::perms::owner_all);
  expect(rejected && read(old + "/prompts/user.md") == "keep edited prompt", "unreadable source stops migration without deleting data");
  write(data + "/.legacy-data-imported-v1", "partial marker");
  rejected = false;
  try { MacDataMigration::initialize({path(old)}, path(data), path(logs)); } catch (const std::exception &) { rejected = true; }
  expect(rejected, "invalid marker cannot silently skip migration");

  QTemporaryDir wrongType;
  QDir().mkpath(wrongType.filePath("old/LogAnalyQt.ini"));
  rejected = false;
  try { MacDataMigration::initialize({path(wrongType.filePath("old"))}, path(wrongType.filePath("data")), path(wrongType.filePath("logs"))); }
  catch (const std::exception &) { rejected = true; }
  expect(rejected, "settings directory cannot be mistaken for a valid settings file");
}
#endif
} // namespace

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName("LogAnalyQt");
  QTemporaryDir home;
  if (!home.isValid()) return 1;
  AppPaths::testHomeDirectory = home.path();
  paths();
#ifdef Q_OS_MACOS
  try { migration(); failuresAndResume(); unreadableAndInvalidMarker(); }
  catch (const std::exception &error) { expect(false, QString::fromUtf8(error.what())); }
#endif
  if (!failures) QTextStream(stdout) << "PASS: platform paths, lossless migration, conflicts, retry, timestamps and source preservation\n";
  return failures ? 1 : 0;
}
