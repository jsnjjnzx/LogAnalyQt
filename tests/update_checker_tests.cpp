#include "core/UpdateChecker.h"
#include <QElapsedTimer>
#include <QThread>
#include <QTextStream>
#include <cstring>

class Reply : public QNetworkReply {
public:
  Reply(const QNetworkRequest &request, QByteArray data, QObject *parent)
      : QNetworkReply(parent), payload(std::move(data)) {
    setRequest(request);
    setUrl(request.url());
    open(QIODevice::ReadOnly);
    QTimer::singleShot(0, this, [this] {
      if (isFinished()) return;
      setFinished(true);
      emit readyRead();
      emit finished();
    });
  }
  void abort() override {
    setError(OperationCanceledError, "cancelled");
    setFinished(true);
    emit finished();
  }
  qint64 bytesAvailable() const override { return payload.size() - position + QNetworkReply::bytesAvailable(); }
protected:
  qint64 readData(char *data, qint64 maximum) override {
    const qint64 count = qMin(maximum, payload.size() - position);
    if (!count) return -1;
    std::memcpy(data, payload.constData() + position, count);
    position += count;
    return count;
  }
private:
  QByteArray payload;
  qint64 position = 0;
};

class Network : public QNetworkAccessManager {
public:
  QHash<QUrl, QByteArray> responses;
protected:
  QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
    return new Reply(request, responses.value(request.url()), this);
  }
};

class UpdateCheckerTest {
public:
  static int run() {
    int failures = 0;
    auto expect = [&](bool condition, const char *message) {
      if (!condition) { QTextStream(stderr) << "FAIL: " << message << '\n'; ++failures; }
    };
    auto settle = [] {
      for (int iteration = 0; iteration < 10; ++iteration) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
      }
    };
    UpdateChecker checker;
    delete checker.m_networkManager;
    auto *network = new Network;
    network->setParent(&checker);
    checker.m_networkManager = network;
    checker.m_currentVersion = "3.1.0";
#ifdef Q_OS_WIN
    const QString name = "TonglianWorkshop_v3.1.1_Windows_x64.exe";
#else
    const QString name = "TonglianWorkshop_v3.1.1_macOS_arm64_update.zip";
#endif
    const QByteArray payload = "downloaded release package";
    const QString url = "https://github.com/jsnjjnzx/LogAnalyQt/releases/download/v3.1.1/" + name;
    QJsonObject asset{{"name", name}, {"browser_download_url", url},
                      {"sha256", QString(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())}};
    QJsonObject metadata{{"tag_name", "v3.1.1"}, {"assets", QJsonArray{asset}}, {"body", "notes"}};
    int errors = 0;
    int results = 0;
    QObject::connect(&checker, &UpdateChecker::checkError, [&](const QString &) { ++errors; });
    QObject::connect(&checker, &UpdateChecker::updateResultReady, [&] { ++results; });
    auto check = [&](const QJsonObject &object) {
      network->responses[checker.m_manifestUrl] = QJsonDocument(object).toJson();
      checker.checkForUpdate();
      settle();
    };
    check(metadata);
    expect(results == 1 && errors == 0 && checker.isUpdateAvailable(), "recognizes GitHub update");
    expect(checker.downloadUrl() == url && checker.automaticInstallAvailable(), "exact platform package selected");
    checker.m_currentVersion = "3.1.1";
    check(metadata);
    expect(!checker.isUpdateAvailable(), "same version does not update");
    checker.m_currentVersion = "3.2.0";
    check(metadata);
    expect(!checker.isUpdateAvailable(), "older release cannot downgrade");
    checker.m_currentVersion = "3.1.0";
    for (const QJsonObject &invalid : QList<QJsonObject>{
             QJsonObject{{"tag_name", "v3.1.1-rc1"}, {"assets", QJsonArray{asset}}},
             QJsonObject{{"tag_name", "garbage"}, {"assets", QJsonArray{asset}}},
             QJsonObject{{"tag_name", "v3.1.1"}, {"assets", QJsonArray{}}},
             QJsonObject{{"tag_name", "v3.1.1"}, {"assets", QJsonArray{asset, asset}}}}) {
      const int previous = errors;
      check(invalid);
      expect(errors == previous + 1 && !checker.isUpdateAvailable(), "rejects invalid or incomplete metadata");
    }
    for (const QString &field : {QString("sha256"), QString("name"), QString("browser_download_url")}) {
      auto changed = asset;
      changed[field] = field == "name" ? "../other.exe" : "https://evil.example/payload";
      auto invalid = metadata;
      invalid["assets"] = QJsonArray{changed};
      check(invalid);
      expect(!checker.isUpdateAvailable() && checker.downloadUrl().isEmpty(), "rejects hash/path/source substitution");
    }
    check(metadata);
    network->responses[QUrl(url)] = payload;
    checker.downloadUpdate();
    settle();
    expect(checker.isDownloadComplete() && checker.isVerifySuccess(), "download and SHA256 verification succeed");
    QFile damaged(checker.m_downloadedFilePath);
    expect(damaged.open(QIODevice::Append), "download can be modified for tamper regression");
    damaged.write("tampered");
    damaged.close();
    expect(!checker.installUpdate(), "rechecks hash immediately before starting updater");
    checker.downloadUpdate();
    checker.cancelDownload();
    settle();
    expect(!checker.isDownloading() && !checker.isDownloadComplete(), "synchronous cancellation is safe");
    network->responses[QUrl(url)] = "corrupt download";
    checker.downloadUpdate();
    settle();
    expect(!checker.isDownloadComplete() && !checker.isVerifySuccess(), "corrupt download cannot install");
    expect(!QFile::exists(checker.m_downloadedFilePath), "corrupt temporary file is removed");
    QTextStream(stdout) << (failures ? "FAILED" : "PASS") << ": GitHub metadata, platform, downgrade, SHA256 and cancellation checks\n";
    return failures ? 1 : 0;
  }
};

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  return UpdateCheckerTest::run();
}
