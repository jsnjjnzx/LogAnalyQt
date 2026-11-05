#include "core/UpdateChecker.h"
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QTextStream>
#include <cstring>
#include <functional>

class Reply : public QNetworkReply {
public:
  Reply(const QNetworkRequest &request, QByteArray data, QObject *parent,
        int status = 200, const QByteArray &contentRange = {},
        qint64 declaredLength = -1,
        QNetworkReply::NetworkError finishError = QNetworkReply::NoError)
      : QNetworkReply(parent), payload(std::move(data)) {
    setRequest(request);
    setUrl(request.url());
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
    setRawHeader("content-length",
                 QByteArray::number(declaredLength >= 0 ? declaredLength
                                                        : payload.size()));
    if (!contentRange.isEmpty()) setRawHeader("content-range", contentRange);
    open(QIODevice::ReadOnly);
    QTimer::singleShot(0, this, [this, finishError] {
      if (isFinished()) return;
      emit metaDataChanged();
      if (isFinished()) return;
      if (finishError != QNetworkReply::NoError)
        setError(finishError, "synthetic interruption");
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
  QUrl resumableUrl;
  bool interruptFirstDownload = false;
  bool interrupted = false;
  bool rejectRange = false;
  QByteArray lastRange;
protected:
  QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
    const QByteArray payload = responses.value(request.url());
    const QByteArray range = request.rawHeader("Range");
    if (request.url() == resumableUrl) lastRange = range;
    if (request.url() == resumableUrl && interruptFirstDownload && !interrupted) {
      interrupted = true;
      return new Reply(request, payload.left(9), this, 200, {}, payload.size(),
                       QNetworkReply::RemoteHostClosedError);
    }
    if (request.url() == resumableUrl && range.startsWith("bytes=") &&
        range.endsWith("-")) {
      if (rejectRange) {
        return new Reply(request, "range rejected", this, 416, {}, -1,
                         QNetworkReply::ContentOperationNotPermittedError);
      }
      bool ok = false;
      const qint64 offset = range.mid(6).chopped(1).toLongLong(&ok);
      if (ok && offset >= 0 && offset < payload.size()) {
        return new Reply(request, payload.mid(offset), this, 206,
                         "bytes " + QByteArray::number(offset) + "-" +
                             QByteArray::number(payload.size() - 1) + "/" +
                             QByteArray::number(payload.size()));
      }
    }
    return new Reply(request, payload, this);
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
    auto waitUntil = [](const std::function<bool()> &condition, int timeoutMs = 4000) {
      QElapsedTimer timer;
      timer.start();
      while (!condition() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
      }
      return condition();
    };
    UpdateChecker checker;
    QTemporaryDir cacheDirectory;
    checker.m_downloadCacheRoot = cacheDirectory.path();
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

    QTemporaryDir resumeDirectory;
    const QByteArray prefix = payload.left(9);
    QFile partial(QDir(resumeDirectory.path()).filePath(name + ".part"));
    expect(partial.open(QIODevice::WriteOnly) && partial.write(prefix) == prefix.size(),
           "resume prefix fixture is writable");
    partial.close();
    QJsonObject resumeMetadata{{"name", name}, {"url", url},
                               {"sha256", QString(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())},
                               {"size", payload.size()}, {"etag", "resume-etag"}};
    QFile metadataFile(QDir(resumeDirectory.path()).filePath(name + ".meta.json"));
    expect(metadataFile.open(QIODevice::WriteOnly) &&
               metadataFile.write(QJsonDocument(resumeMetadata).toJson()) > 0,
           "resume metadata fixture is writable");
    metadataFile.close();

    UpdateChecker resumed;
    resumed.m_downloadCacheRoot = resumeDirectory.path();
    delete resumed.m_networkManager;
    auto *resumeNetwork = new Network;
    resumeNetwork->setParent(&resumed);
    resumed.m_networkManager = resumeNetwork;
    resumed.m_currentVersion = "3.1.0";
    resumeNetwork->resumableUrl = QUrl(url);
    resumeNetwork->responses[resumed.m_manifestUrl] = QJsonDocument(metadata).toJson();
    resumeNetwork->responses[QUrl(url)] = payload;
    resumed.checkForUpdate();
    settle();
    expect(resumed.isDownloadResumeAvailable() && resumed.downloadedBytes() == prefix.size(),
           "a restarted app discovers the saved partial download");
    resumed.downloadUpdate();
    settle();
    expect(resumeNetwork->lastRange == "bytes=" + QByteArray::number(prefix.size()) + "-",
           "resume requests only the missing byte range");
    expect(resumed.isDownloadComplete() && resumed.isVerifySuccess() &&
               QFile(resumed.m_downloadedFilePath).size() == payload.size(),
           "resumed download completes and verifies");
    QFile resumedFile(resumed.m_downloadedFilePath);
    expect(resumedFile.open(QIODevice::ReadOnly) && resumedFile.readAll() == payload,
           "resumed file contains the original bytes without duplication");

    QTemporaryDir interruptionDirectory;
    UpdateChecker interrupted;
    interrupted.m_downloadCacheRoot = interruptionDirectory.path();
    delete interrupted.m_networkManager;
    auto *interruptedNetwork = new Network;
    interruptedNetwork->setParent(&interrupted);
    interrupted.m_networkManager = interruptedNetwork;
    interrupted.m_currentVersion = "3.1.0";
    interruptedNetwork->resumableUrl = QUrl(url);
    interruptedNetwork->interruptFirstDownload = true;
    interruptedNetwork->responses[interrupted.m_manifestUrl] =
        QJsonDocument(metadata).toJson();
    interruptedNetwork->responses[QUrl(url)] = payload;
    interrupted.checkForUpdate();
    settle();
    interrupted.downloadUpdate();
    expect(waitUntil([&] { return interrupted.isDownloadComplete(); }),
           "an interrupted transfer automatically resumes from its byte range");
    expect(interruptedNetwork->lastRange == "bytes=9-",
           "automatic retry asks only for the missing suffix");

    interrupted.cancelDownload();
    interruptedNetwork->interrupted = false;
    interruptedNetwork->rejectRange = true;
    interrupted.downloadUpdate();
    expect(waitUntil([&] { return interrupted.isDownloadComplete(); }),
           "a rejected range restarts and verifies the full download");
    expect(interruptedNetwork->lastRange.isEmpty(),
           "the restarted transfer drops the rejected range");

    QTextStream(stdout) << (failures ? "FAILED" : "PASS") << ": GitHub metadata, platform, downgrade, SHA256, cancellation and resume checks\n";
    return failures ? 1 : 0;
  }
};

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  return UpdateCheckerTest::run();
}
