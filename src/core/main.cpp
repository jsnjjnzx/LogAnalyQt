#include "LogBackend.h"
#include "LogMerger.h"
#include "SshTerminal.h"
#include "UpdateChecker.h"
#include "mqtt/MqttController.h"
#include "config/ConfigManager.h"
#include <QEventLoop>
#include "../common/AppPaths.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMutex>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExtensionPlugin>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QTextStream>
#include <QSaveFile>
#include <QProcess>
#include <QTemporaryDir>
#include <memory>
#ifdef Q_OS_MACOS
#include "MacWindowBackdrop.h"
#endif
#ifdef LOGANALYQT_UI_TESTING
#include "../../tests/MigrationUiChecks.h"
#include "../../tests/ParseWorkspaceChecks.h"
#include "../../tests/MqttUiChecks.h"
#endif

Q_IMPORT_QML_PLUGIN(NextUiPlugin)

// 日志位置由平台路径规则决定，迁移完成后才开始写入。
static QMutex g_logMutex;
static void qtMessageToFile(QtMsgType type, const QMessageLogContext &context,
                            const QString &msg) {
  QMutexLocker locker(&g_logMutex);

  QDir dir(AppPaths::logDirectory());
  if (!dir.exists())
    dir.mkpath(QStringLiteral("."));
  QString logPath = dir.filePath(QStringLiteral("log.txt"));

  static QFile logFile;
  if (!logFile.isOpen()) {
    logFile.setFileName(logPath);
    if (!logFile.open(QIODevice::Append | QIODevice::Text)) {
      if (type == QtFatalMsg)
        abort();
      return;
    }
  }

  if (logFile.isOpen()) {
    QTextStream ts(&logFile);
    QString typeStr;
    switch (type) {
    case QtDebugMsg:
      typeStr = "DEBUG";
      break;
    case QtInfoMsg:
      typeStr = "INFO";
      break;
    case QtWarningMsg:
      typeStr = "WARN";
      break;
    case QtCriticalMsg:
      typeStr = "CRIT";
      break;
    case QtFatalMsg:
      typeStr = "FATAL";
      break;
    default:
      typeStr = "LOG";
    }

    QString time =
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    ts << time << " " << typeStr << " " << msg;
    // 额外输出位置信息，便于调试
    if (context.file && context.file[0] != '\0') {
      ts << " (" << context.file << ":" << context.line << ")";
    }
    ts << '\n';
    ts.flush();
  }

  if (type == QtFatalMsg) {
    abort();
  }
}

#ifdef LOGANALYQT_UI_TESTING
#include "../../tests/UpdateIntegration.h"
#endif

int main(int argc, char *argv[]) {
  QGuiApplication app(argc, argv);
  // Keep the storage and credential identity stable across the display-name change.
  QCoreApplication::setApplicationName(QStringLiteral("LogAnalyQt"));
  QGuiApplication::setApplicationDisplayName(QStringLiteral("通联工坊"));
#ifdef LOGANALYQT_UI_TESTING
  const bool updateTest = app.arguments().contains(QStringLiteral("--update-test")) ||
                          app.arguments().contains(QStringLiteral("--update-probe"));
  const bool updateRestart = qEnvironmentVariableIsSet("LOGANALYQT_UPDATE_TEST_RESULT") && !updateTest;
  const bool uiSmokeMode = app.arguments().contains(QStringLiteral("--ui-smoke")) || updateRestart;
#ifdef Q_OS_MACOS
  std::unique_ptr<QTemporaryDir> isolatedHome;
  if (uiSmokeMode) {
    AppPaths::testHomeDirectory = qEnvironmentVariable("LOGANALYQT_TEST_HOME");
    if (AppPaths::testHomeDirectory.isEmpty()) {
      isolatedHome = std::make_unique<QTemporaryDir>();
      if (!isolatedHome->isValid()) return 2;
      AppPaths::testHomeDirectory = isolatedHome->path();
    }
    if (!QFileInfo(AppPaths::testHomeDirectory).isAbsolute()) return 2;
  }
#endif
#else
  const bool uiSmokeMode = false;
#endif
  QQuickStyle::setStyle(QStringLiteral("Basic"));

  ConfigManager configManager(AppPaths::writableDataRoot());
  QStringList legacyRoots{AppPaths::writableDataRoot()};
#ifdef Q_OS_MACOS
  if (!uiSmokeMode && AppPaths::portableDataRootForApplicationDir(app.applicationDirPath()) == QStringLiteral("/Applications"))
    legacyRoots << QStringLiteral("/Applications") << app.applicationDirPath();
#endif
  configManager.inspectStartup(legacyRoots);
  if (configManager.pending() || !configManager.error().isEmpty()) {
    app.setQuitOnLastWindowClosed(false);
    QQmlApplicationEngine migrationEngine;
    migrationEngine.rootContext()->setContextProperty("configManager", &configManager);
    migrationEngine.loadFromModule("LogAnalyQt.Next", "ConfigMigrationWindow");
    if (migrationEngine.rootObjects().isEmpty()) return 2;
    QEventLoop gate;
    bool accepted = false;
    auto *dialog = migrationEngine.rootObjects().first();
    QObject::connect(dialog, SIGNAL(finished(bool)), &gate, SLOT(quit()));
    QObject::connect(&configManager, &ConfigManager::migrationFinished, &gate, [&accepted](bool success) { if (success) accepted = true; });
#ifdef LOGANALYQT_UI_TESTING
    const int migrationAction = app.arguments().indexOf("--migration-action");
    if (uiSmokeMode && migrationAction >= 0 && migrationAction + 1 < app.arguments().size()) {
      const auto action = app.arguments()[migrationAction + 1];
      if (action == "retry-conflict") {
        QObject::connect(&configManager, &ConfigManager::migrationFinished, dialog, [&configManager, dialog](bool success) {
          if (success) return;
          QVariantMap choices;
          for (const auto &key : configManager.conflicts()) choices.insert(key, "legacy");
          dialog->setProperty("choices", choices);
          QTimer::singleShot(0, dialog, [dialog] { if (auto *button = dialog->findChild<QObject *>("migration-confirm")) QMetaObject::invokeMethod(button, "clicked"); });
        }, Qt::SingleShotConnection);
      }
      const int captureArg = app.arguments().indexOf("--migration-screenshot");
      const auto capture = captureArg >= 0 && captureArg + 1 < app.arguments().size() ? app.arguments()[captureArg + 1] : QString();
      QTimer::singleShot(300, dialog, [dialog, action, capture] {
        if (!capture.isEmpty()) if (auto *window = qobject_cast<QQuickWindow *>(dialog)) window->grabWindow().save(capture);
        auto *button = dialog->findChild<QObject *>(action == "confirm" || action == "retry-conflict" ? "migration-confirm" : "migration-cancel");
        if (button && button->property("enabled").toBool()) QMetaObject::invokeMethod(button, "clicked");
        else QCoreApplication::exit(5);
      });
    }
#endif
    gate.exec();
    if (!accepted) return 0;
  }
  QString migrationError;
  if (!configManager.applyPending(&migrationError) || !AppPaths::initialize(&migrationError) ||
      !ConfigJson::update(QDir(AppPaths::writableDataRoot()).filePath("settings.json"), [](QJsonObject &root, QString *error) { return ConfigMigration::validate(root, error); }, &migrationError)) {
    fprintf(stderr, "Configuration startup failed: %s\n", qPrintable(migrationError));
    configManager.inspectStartup(legacyRoots);
    QQmlApplicationEngine errorEngine;
    errorEngine.rootContext()->setContextProperty("startupError", migrationError);
    errorEngine.loadData(R"QML(import QtQuick
import QtQuick.Controls
ApplicationWindow { visible: true; width: 660; height: 350; title: "通联工坊 配置未能加载"
Column { anchors.fill: parent; anchors.margins: 24; spacing: 20
Label { width: parent.width; text: startupError; textFormat: Text.PlainText; wrapMode: Text.Wrap }
Label { width: parent.width; text: "原文件及回退快照已保留。请处理后重新启动；取消待恢复任务可移走 restore-pending.json 或 import-pending.json。"; wrapMode: Text.Wrap }
Button { text: "关闭"; onClicked: Qt.quit() }
} })QML");
    return app.exec();
  }
  // 迁移失败时不创建后端或写入默认配置。
  qInstallMessageHandler(qtMessageToFile);

  qmlRegisterType<LogBackend>("LogAnalyQt", 1, 2, "LogBackend");
  qmlRegisterUncreatableType<AiFramingController>("LogAnalyQt", 1, 2,
      "AiFramingController", QStringLiteral("由 LogBackend 提供"));
  qmlRegisterType<LogMerger>("LogAnalyQt", 1, 2, "LogMerger");
  qmlRegisterType<UpdateChecker>("LogAnalyQt", 1, 2, "UpdateChecker");
  qmlRegisterType<SshTerminal>("LogAnalyQt", 1, 2, "SshTerminal");
  qmlRegisterType<MqttController>("LogAnalyQt", 1, 2, "MqttController");
  qmlRegisterUncreatableType<MqttSession>("LogAnalyQt", 1, 2, "MqttSession", QStringLiteral("由 MqttController 提供"));
  qmlRegisterUncreatableType<MqttBroker>("LogAnalyQt", 1, 2, "MqttBroker", QStringLiteral("由 MqttController 提供"));

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("uiSmokeMode"), uiSmokeMode);
  engine.rootContext()->setContextProperty(QStringLiteral("personalConfigManager"), &configManager);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
  engine.loadFromModule("LogAnalyQt.Next", "Main");
  app.setQuitOnLastWindowClosed(true);

#ifdef Q_OS_MACOS
  if (app.platformName() == QStringLiteral("cocoa") && !engine.rootObjects().isEmpty()) {
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (window) {
      new MacWindowBackdrop(window, window->property("uiTheme").value<QObject *>());
#ifdef LOGANALYQT_UI_TESTING
      const int stateArg = app.arguments().indexOf(QStringLiteral("--ui-start-visibility"));
      if (uiSmokeMode && stateArg >= 0 && stateArg + 1 < app.arguments().size()) {
        if (app.arguments()[stateArg + 1] == QStringLiteral("hidden")) window->hide();
        if (app.arguments()[stateArg + 1] == QStringLiteral("minimized")) window->showMinimized();
      }
#endif
      // Older updaters pass no launch flag; restore the window on every native Mac launch.
      QTimer::singleShot(0, window, [window] {
        if (!window->isVisible() || window->visibility() == QWindow::Minimized)
          window->showNormal();
        window->raise();
        window->requestActivate();
      });
    }
  }
#endif

#ifdef LOGANALYQT_UI_TESTING
  if (!engine.rootObjects().isEmpty() && updateRestart) {
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto rendered = std::make_shared<bool>(false);
    const bool nativeMac = app.platformName() == QStringLiteral("cocoa");
    const auto recordRestart = [window, rendered, nativeMac] {
      if (!*rendered || !window->isVisible() || (nativeMac && !window->isActive())) return;
      QSaveFile marker(qEnvironmentVariable("LOGANALYQT_UPDATE_TEST_RESULT"));
      if (!marker.open(QIODevice::WriteOnly)) { QCoreApplication::exit(7); return; }
      marker.write(QJsonDocument(QJsonObject{{"version", QStringLiteral(APP_VERSION)},
          {"executable", QCoreApplication::applicationFilePath()},
          {"pid", QCoreApplication::applicationPid()},
          {"visible", window->isVisible()},
          {"active", window->isActive()}}).toJson());
      if (!marker.commit()) QCoreApplication::exit(7);
    };
    QObject::connect(window, &QQuickWindow::frameSwapped, &app, [rendered, recordRestart] {
      *rendered = true;
      recordRestart();
    }, Qt::SingleShotConnection);
    QObject::connect(window, &QQuickWindow::activeChanged, &app, recordRestart);
  }
  if (updateTest && !engine.rootObjects().isEmpty()) {
    QTimer::singleShot(0, &app, [&engine] {
      UpdateCheckerTest::start(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));
    });
  }
  // Smoke captures use an isolated build directory and never check for updates.
  if (uiSmokeMode && !updateTest && !app.arguments().contains(QStringLiteral("--interactive")) && !engine.rootObjects().isEmpty()) {
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    const QStringList args = app.arguments();
    const int sizeArg = args.indexOf(QStringLiteral("--size"));
    if (window && sizeArg >= 0 && sizeArg + 2 < args.size())
      window->resize(args[sizeArg + 1].toInt(), args[sizeArg + 2].toInt());
    const int pageArg = args.indexOf(QStringLiteral("--page"));
    const int screenshotArg = args.indexOf(QStringLiteral("--screenshot"));
    QTimer::singleShot(250, &app, [window, args, pageArg] {
      if (window && args.contains(QStringLiteral("--with-fixture"))) {
        auto *backend = window->findChild<LogBackend *>("analysisBackend");
        if (backend) {
          const QString frame = backend->buildFullFrame("05 01 01 40 00 02 00 00", {{"serverAddrHex", "000000000001"}, {"prm", true}, {"funcCode", 3}});
          const QString log = "10:24:16.182  [INFO]  本地界面验证日志\n10:24:16.204  [TX]    " + frame + "\n10:24:17.002  [INFO]  报文已加载\n";
          auto *editor = window->findChild<QObject *>("logText");
          if (editor) editor->setProperty("text", log);
          window->setProperty("selectedLogHex", frame);
          window->setProperty("selectedLogStart", log.indexOf(frame));
          backend->setProtocolType(1);
          QMetaObject::invokeMethod(window, "performParse", Q_ARG(QVariant, frame), Q_ARG(QVariant, 1));
          const QString otherFrame = backend->buildFullFrame("05 01 01 40 00 02 00 00", {{"serverAddrHex", "000000000002"}, {"prm", true}, {"funcCode", 3}});
          QMetaObject::invokeMethod(window, "performParse", Q_ARG(QVariant, otherFrame), Q_ARG(QVariant, 2));
        }
      }
      if (window && pageArg >= 0 && pageArg + 1 < args.size())
        QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, args[pageArg + 1].toInt()));
      if (window && args.contains("--personal-config")) {
        QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, 9));
        if (auto *settings = window->findChild<QObject *>("page-9")) settings->setProperty("section", 2);
      }
    });
    QTimer::singleShot(1500, &app, [&, window, args, pageArg, screenshotArg] {
      if (args.contains(QStringLiteral("--mqtt-checks"))) {
        const QString path = screenshotArg >= 0 && screenshotArg + 1 < args.size() ? args[screenshotArg + 1] : QString();
        const QString error = checkMqttUi(window, path, args.contains(QStringLiteral("--mqtt-broker-view")));
        if (!error.isEmpty()) fprintf(stderr, "MQTT UI CHECK FAILED: %s\n", qPrintable(error));
        app.exit(error.isEmpty() ? 0 : 4);
        return;
      }
      const int workspacePhase = args.indexOf(QStringLiteral("--workspace-phase"));
      if (workspacePhase >= 0 && workspacePhase + 1 < args.size()) {
        const QString error = checkParseWorkspace(window, args[workspacePhase + 1]);
        if (!error.isEmpty()) fprintf(stderr, "WORKSPACE CHECK FAILED: %s\n", qPrintable(error));
        app.exit(error.isEmpty() ? 0 : 4);
        return;
      }
      if (args.contains(QStringLiteral("--ui-checks"))) {
        const QString error = checkMigratedUi(window);
        if (!error.isEmpty()) {
          fprintf(stderr, "UI CHECK FAILED: %s\n", qPrintable(error));
          app.exit(4);
          return;
        }
        if (pageArg >= 0 && pageArg + 1 < args.size())
          QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, args[pageArg + 1].toInt()));
      }
      if (window && screenshotArg >= 0 && screenshotArg + 1 < args.size()) {
        const QImage image = window->grabWindow();
        app.exit(!image.isNull() && image.save(args[screenshotArg + 1]) ? 0 : 3);
      } else {
        app.exit(0);
      }
    });
  }
#endif

  return app.exec();
}
