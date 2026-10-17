#include "LogBackend.h"
#include "LogMerger.h"
#include "SshTerminal.h"
#include "UpdateChecker.h"
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
#ifdef LOGANALYQT_UI_TESTING
#include "../../tests/MigrationUiChecks.h"
#include "../../tests/ParseWorkspaceChecks.h"
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
  QCoreApplication::setApplicationName(QStringLiteral("LogAnalyQt"));
  const bool legacyUi = app.arguments().contains(QStringLiteral("--legacy-ui"));
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
  if (!legacyUi)
    QQuickStyle::setStyle(QStringLiteral("Basic"));

  QString migrationError;
  if (!AppPaths::initialize(&migrationError)) {
    fprintf(stderr, "Data migration failed: %s\n", qPrintable(migrationError));
#ifdef Q_OS_MACOS
    if (!uiSmokeMode)
      QProcess::execute(QStringLiteral("/usr/bin/osascript"), {QStringLiteral("-e"),
          QStringLiteral("on run argv\ndisplay alert \"LogAnalyQt 数据迁移未完成\" message (item 1 of argv) as critical buttons {\"关闭\"}\nend run"),
          QStringLiteral("旧数据已保留，请处理以下问题后重试：\n") + migrationError});
#endif
    return 2;
  }
  // 迁移失败时不创建后端或写入默认配置。
  qInstallMessageHandler(qtMessageToFile);

  qmlRegisterType<LogBackend>("LogAnalyQt", 1, 2, "LogBackend");
  qmlRegisterUncreatableType<AiFramingController>("LogAnalyQt", 1, 2,
      "AiFramingController", QStringLiteral("由 LogBackend 提供"));
  qmlRegisterType<LogMerger>("LogAnalyQt", 1, 2, "LogMerger");
  qmlRegisterType<UpdateChecker>("LogAnalyQt", 1, 2, "UpdateChecker");
  qmlRegisterType<SshTerminal>("LogAnalyQt", 1, 2, "SshTerminal");

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("uiSmokeMode"), uiSmokeMode);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
  engine.loadFromModule(legacyUi ? "LogAnalyQt" : "LogAnalyQt.Next", "Main");

#ifdef Q_OS_MACOS
  if (app.platformName() == QStringLiteral("cocoa") && !engine.rootObjects().isEmpty()) {
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (window) {
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
    if (window && args.contains(QStringLiteral("--light"))) {
      if (auto *theme = window->property("uiTheme").value<QObject *>())
        theme->setProperty("isDark", false);
    }
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
    });
    QTimer::singleShot(1500, &app, [&, window, args, pageArg, screenshotArg] {
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
