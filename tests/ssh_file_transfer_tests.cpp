#include "SshFileTransfer.h"
#include "SshTextColors.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextLayout>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool condition, const QString &message) {
  ++checks;
  if (!condition) throw std::runtime_error(message.toStdString());
}
QVariantMap result(QSignalSpy &spy) {
  check(!spy.isEmpty() || spy.wait(20000), "file job timed out");
  return spy.takeFirst().at(1).toMap();
}
QVariantMap success(QSignalSpy &spy) {
  auto value = result(spy);
  check(!value.contains("error"), value.value("error").toString());
  return value;
}
QVariantMap rowNamed(const QVariantMap &listing, const QString &name) {
  for (const auto &entry : listing.value("entries").toList()) {
    const auto row = entry.toMap();
    if (row.value("name").toString() == name) return row;
  }
  return {};
}
bool has(const QVariantMap &listing, const QString &name, const char *flag) {
  return rowNamed(listing, name).value(flag).toBool();
}
void writeFile(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), file.errorString());
}
void colorAndWordChecks() {
  QTextDocument document;
  document.setPlainText(QString::fromUtf8("alpha  beta_2 中文"));
  SshTextColors display;
  display.setDocument(&document);
  display.setRuns({QVariantMap{{"start", 7}, {"length", 6}, {"fg", 0xffff0000u}, {"bg", 0xff010203u}}});
  const auto formats = document.begin().layout()->formats();
  check(formats.size() == 1 && formats.front().start == 7 && formats.front().length == 6 &&
        formats.front().format.foreground().color() == QColor("#ff0000") &&
        formats.front().format.background().color() == QColor("#010203"), "formatting reaches the native document layout");
  check(document.toPlainText() == QString::fromUtf8("alpha  beta_2 中文"), "formatting preserves plain text");
  check(display.wordAt(8) == QVariantList{7, 13} && display.wordAt(2) == QVariantList{0, 5},
        "double-click selection finds the word under the cursor");
  check(display.wordAt(5).isEmpty() && display.wordAt(17).isEmpty() && !display.wordAt(14).isEmpty(),
        "whitespace/end produce no selection; CJK can be selected");
  display.setRuns({});
  check(document.begin().layout()->formats().isEmpty(), "clear ANSI styling when reusing a document");
}
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  try {
    const auto arguments = app.arguments();
    const bool ftp = arguments.value(1) == "ftp";
    const bool network = ftp || arguments.value(1) == "sftp";
    QTemporaryDir root;
    check(root.isValid(), "temporary test directory");
    const QDir directory(root.path());
    check(directory.mkdir("source") && directory.mkdir("download"), "create isolated directories");
    const auto source = directory.filePath("source"), download = directory.filePath("download");
    const auto name = QString::fromUtf8("测试 #+%.bin");
    QByteArray bytes;
    for (int i = 0; i < 400000; ++i) bytes.append(char(i % 251));
    writeFile(QDir(source).filePath(name), bytes);
    SshFileTransfer backend(root.path());
    QSignalSpy finished(&backend, &SshFileTransfer::finished);
    QSignalSpy trust(&backend, &SshFileTransfer::hostKeyRequired);
    backend.browseLocal(root.path());
    check(has(success(finished), "source", "directory"), "local directory listed");
    backend.browseLocal(source);
    const auto sourceRow = rowNamed(success(finished), name);
    check(sourceRow.value("file").toBool() && sourceRow.value("modified").toLongLong() > 0,
          "local Unicode file and timestamp listed");
    backend.run("localMkdir", {}, "", source, "new folder");
    success(finished);
    backend.run("localRename", {}, "", source, "new folder", "renamed folder");
    success(finished);
    backend.run("localRename", {}, "", source, "renamed folder", name);
    check(result(finished).value("error").toString().contains("已存在") &&
          QDir(source).exists("renamed folder") && QFileInfo(QDir(source).filePath(name)).size() == bytes.size(),
          "local rename refuses a collision and preserves both entries");
    backend.run("localMkdir", {}, "", source, "../escape");
    check(result(finished).contains("error") && !directory.exists("escape"), "invalid local name cannot escape directory");
    backend.browseLocal(directory.filePath("missing"));
    check(result(finished).contains("error"), "invalid local directory rejected");
    backend.run("list", {}, "/", source, "");
    check(result(finished).contains("error"), "empty remote connection rejected");
    colorAndWordChecks();

    if (network) {
      const QVariantMap connection{{"ftp", ftp}, {"host", "127.0.0.1"}, {"port", arguments.value(2).toInt()},
                                   {"user", "transfer-test"}, {"password", "p\"a\\ss:word"}};
      const QString subdir = "/sub folder";
      backend.run("upload", connection, subdir, source, "../bad");
      check(result(finished).contains("error"), "remote traversal rejected before connecting");
      backend.run("list", connection, ftp ? "/" : ".", source, "");
      if (!ftp) {
        check(trust.wait(20000), "unknown SSH key requires confirmation");
        check(trust.takeFirst().at(1).toString() == arguments.value(3), "reported fingerprint matches loopback peer");
        backend.confirmHost(false);
        check(result(finished).value("error").toString().contains("未发送密码"), "rejecting trust blocks login");
        backend.run("list", connection, ".", source, "");
        check(trust.wait(20000), "a rejected key is still unknown");
        trust.clear();
        backend.confirmHost(true);
      }
      auto rootListing = success(finished);
      check(rootListing.value("path") == "/" && has(rootListing, "sub folder", "directory"), "remote root lists subdirectory");
      backend.run("list", connection, subdir, source, "");
      const auto seed = rowNamed(success(finished), "seed.txt");
      check(seed.value("file").toBool(), "remote navigation lists existing file");
      check(arguments.value(3) == "legacy" ? seed.value("modified").toLongLong() == 0
                                          : seed.value("modified").toLongLong() > 0,
            "remote modified time is included when protocol listing supplies it");
      if (ftp) {
        writeFile(QDir(source).filePath(".hidden"), "replacement");
        backend.run("upload", connection, subdir, source, ".hidden");
        check(result(finished).value("error").toString().contains("已存在"), "FTP upload refuses an unlisted dotfile");
        backend.run("rename", connection, subdir, source, "seed.txt", ".hidden");
        check(result(finished).value("error").toString().contains("已存在"), "FTP rename refuses an unlisted dotfile");
        backend.run("rename", connection, subdir, source, "seed.txt", ".new-name");
        success(finished);
        backend.run("rename", connection, subdir, source, ".new-name", "seed.txt");
        success(finished);
      }
      const auto newFolder = QString::fromUtf8("新建 目录");
      const auto renamed = QString::fromUtf8("重命名 目录");
      backend.run("mkdir", connection, subdir, source, newFolder);
      success(finished);
      backend.run("rename", connection, subdir, source, newFolder, renamed);
      success(finished);
      backend.run("rename", connection, subdir, source, renamed, "seed.txt");
      check(result(finished).value("error").toString().contains("已存在"), "remote rename refuses a collision");
      backend.run("rename", connection, subdir, source, "seed.txt", "../escape");
      check(result(finished).contains("error"), "remote rename refuses traversal");
      backend.run("list", connection, subdir, source, "");
      const auto edited = success(finished);
      check(has(edited, renamed, "directory") && !has(edited, newFolder, "directory") &&
            has(edited, "seed.txt", "file"), "remote folder rename leaves other entries intact");
      backend.run("upload", connection, subdir, source, name);
      success(finished);
      if (!ftp) check(backend.transferredBytes() == bytes.size(), "SFTP upload reports actual transferred bytes");
      backend.run("list", connection, subdir, source, "");
      check(has(success(finished), name, "file"), "remote upload is visible");
      backend.run("upload", connection, subdir, source, name);
      check(result(finished).value("error").toString().contains("已存在"), "remote duplicate upload refused");
      backend.run("download", connection, subdir, download, name);
      success(finished);
      check(backend.transferredBytes() == bytes.size(), "download reports actual transferred bytes");
      QFile downloaded(QDir(download).filePath(name));
      check(downloaded.open(QIODevice::ReadOnly) && downloaded.readAll() == bytes, "download is byte-for-byte identical");
      downloaded.close();
      backend.run("download", connection, subdir, download, name);
      check(result(finished).value("error").toString().contains("已存在"), "local duplicate download refused");
      backend.run("download", connection, subdir, download, "missing-file");
      check(result(finished).contains("error") && QDir(download).entryList(QDir::Files | QDir::Hidden) == QStringList{name},
            "failed download leaves no partial local file");
      SshFileTransfer browser(root.path());
      QSignalSpy browsed(&browser, &SshFileTransfer::finished);
      QSignalSpy progress(&backend, &SshFileTransfer::transferredBytesChanged);
      backend.run("download", connection, subdir, download, "cancel.bin");
      browser.run("list", connection, subdir, source);
      check(has(success(browsed), name, "file") && backend.busy(), "remote browsing continues during a transfer");
      check(backend.transferredBytes() > 0 || progress.wait(5000), "slow transfer reports progress before finishing");
      check(backend.transferredBytes() > 0 && backend.transferredBytes() < 2000000, "reported progress is partial");
      backend.cancel();
      check(result(finished).value("error").toString().contains("已取消") &&
            QDir(download).entryList(QDir::Files | QDir::Hidden) == QStringList{name},
            "cancelled transfer leaves neither a final nor partial file");
      if (!ftp) {
        writeFile(directory.filePath("ssh-known-hosts.json"),
                  QStringLiteral("{\"[127.0.0.1]:%1\":\"SHA256:wrong\"}").arg(arguments.value(2)).toUtf8());
        backend.run("list", connection, subdir, download, "");
        check(result(finished).value("error").toString().contains("密钥已变化"), "changed SFTP host key rejected");
      }
    }
    std::cout << "PASS: " << checks << " native file transfer / document assertions\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
