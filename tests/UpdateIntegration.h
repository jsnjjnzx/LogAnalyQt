#pragma once
#include "UpdateChecker.h"
#include <QQuickWindow>

class UpdateCheckerTest {
public:
  static void start(QQuickWindow *window) {
    auto *checker = window->findChild<UpdateChecker *>("updateChecker");
    if (!checker) { QCoreApplication::exit(2); return; }
    checker->m_currentVersion = QStringLiteral("3.1.0");
    const bool probe = QCoreApplication::arguments().contains(QStringLiteral("--update-probe"));
    QObject::connect(checker, &UpdateChecker::checkError, window, [probe](const QString &message) {
      if (probe && message == QStringLiteral("GitHub 尚无可用的正式发布版本")) {
        qInfo() << "UPDATE_TEST_TLS_OK_NO_RELEASE";
        QCoreApplication::exit(0);
        return;
      }
      qCritical() << "UPDATE_TEST_CHECK_FAILED" << message;
      QCoreApplication::exit(3);
    });
    QObject::connect(checker, &UpdateChecker::downloadErrorOccurred, window, [](const QString &message) {
      qCritical() << "UPDATE_TEST_DOWNLOAD_FAILED" << message;
      QCoreApplication::exit(4);
    });
    QObject::connect(checker, &UpdateChecker::updateResultReady, window, [checker, probe] {
      if (probe) { QCoreApplication::exit(0); return; }
      if (!checker->isUpdateAvailable()) { QCoreApplication::exit(5); return; }
      qInfo() << "UPDATE_TEST_DOWNLOAD" << checker->downloadUrl();
      checker->downloadUpdate();
    });
    QObject::connect(checker, &UpdateChecker::downloadFinished, window, [window] {
      qInfo() << "UPDATE_TEST_VERIFIED_AND_CLOSING";
      window->setProperty("installingUpdate", true);
      window->close();
      if (window->isVisible()) QCoreApplication::exit(6);
    });
    checker->checkForUpdate();
  }
};
