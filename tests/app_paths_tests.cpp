#include "common/AppPaths.h"
#include "common/AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QTextStream>

namespace {

int g_failureCount = 0;

void Expect(bool condition, const QString &message) {
  if (condition) {
    return;
  }
  QTextStream(stderr) << "FAIL: " << message << '\n';
  ++g_failureCount;
}

void TestMacBundleUsesAppSiblingDirectory() {
  const QString applicationDir =
      "/tmp/portable/appLogAnalyQt.app/Contents/MacOS";
  const QString root =
      AppPaths::portableDataRootForApplicationDir(applicationDir);
  Expect(root == "/tmp/portable",
         "macOS .app 应使用 app 同级目录作为数据根目录: " +
             root);
}

void TestNonBundleExecutableKeepsItsDirectory() {
  const QString applicationDir = "/tmp/portable/bin";
  const QString root =
      AppPaths::portableDataRootForApplicationDir(applicationDir);
  Expect(root == applicationDir,
         "非 .app 可执行程序应保持使用自身目录: " + root);
}

void TestAllWritablePathsUsePortableRoot() {
  const QString root = AppPaths::writableDataRoot();
  Expect(root == QCoreApplication::applicationDirPath(),
         "直接运行的测试程序应使用可执行文件同级目录: " + root);
  Expect(AppPaths::protocolDirectory() ==
             QDir(root).filePath("protocol_xml"),
         "协议 XML 应位于便携根目录的 protocol_xml");
  Expect(AppPaths::presetDirectory() == QDir(root).filePath("preset"),
         "预置数据应位于便携根目录的 preset");
  Expect(AppPaths::logDirectory() == QDir(root).filePath("log"),
         "日志应位于便携根目录的 log");

  auto settings = appSettings();
  Expect(settings.fileName() == QDir(root).filePath("LogAnalyQt.ini"),
         "应用设置应位于便携根目录: " +
             settings.fileName());
  Expect(!root.contains("Library/Application Support"),
         "便携数据根目录不应使用 Application Support: " + root);
}

} // namespace

int main(int argc, char *argv[]) {
  QCoreApplication application(argc, argv);
  TestMacBundleUsesAppSiblingDirectory();
  TestNonBundleExecutableKeepsItsDirectory();
  TestAllWritablePathsUsePortableRoot();

  if (g_failureCount == 0) {
    QTextStream(stdout) << "All portable app path tests passed.\n";
  }
  return g_failureCount == 0 ? 0 : 1;
}
