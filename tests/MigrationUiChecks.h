#pragma once

#include "LogBackend.h"
#include "LogMerger.h"
#include "SshTerminal.h"
#include "csg_upstream_test_support.h"
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QImage>
#include <QQmlEngine>
#include <QQmlIncubationController>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUrl>
#include <functional>

// Called only by --ui-smoke --ui-checks from the isolated runtime test runner.
inline QString checkMigratedUi(QQuickWindow *window) {
  auto require = [](bool ok, const char *message) {
    if (!ok) throw QString::fromUtf8(message);
  };
  try {
    require(QGuiApplication::applicationDisplayName() == QString::fromUtf8("通联工坊"), "application display name is incorrect");
    require(QCoreApplication::applicationName() == "LogAnalyQt", "renaming the display must preserve the storage identity");
    require(window->title().startsWith(QString::fromUtf8("通联工坊")), "main window title is incorrect");
    require(!QFile::exists(":/qt/qml/LogAnalyQt/qml/Main.qml"), "legacy UI is still embedded in the app");
    for (auto *model : QList<QAbstractItemModel *>{new ParseResultModel, new GlobalPreviewModel}) {
      bool reset = false;
      QObject::connect(model, &QAbstractItemModel::modelReset, [&] { reset = true; });
      delete model;
      require(!reset, "destroying a model must not restart view delegates");
    }
    auto *backend = window->findChild<LogBackend *>("analysisBackend");
    require(backend != nullptr, "real LogBackend is missing");
    auto trace = [](const char *step) { qInfo() << "UI_CHECK_STEP" << step; };
    auto settle = [&] {
      QEventLoop frame;
      bool rendered = false;
      QObject::connect(window, &QQuickWindow::frameSwapped, &frame, [&] { rendered = true; frame.quit(); }, Qt::QueuedConnection);
      QTimer::singleShot(2000, &frame, &QEventLoop::quit);
      window->update();
      frame.exec();
      require(rendered, "UI frame did not finish rendering");
      auto *controller = qmlEngine(window)->incubationController();
      QElapsedTimer elapsed;
      elapsed.start();
      do {
        QCoreApplication::processEvents();
        if (controller) controller->incubateFor(10);
      } while (controller && controller->incubatingObjectCount() && elapsed.elapsed() < 2000);
      require(!controller || controller->incubatingObjectCount() == 0, "UI delegates did not finish creation");
    };
    std::function<QQuickItem *(QQuickItem *, const QString &)> findItem =
        [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
      if (parent->objectName() == name) return parent;
      for (auto *child : parent->childItems())
        if (auto *found = findItem(child, name)) return found;
      return nullptr;
    };
    auto uiItem = [&](const QString &name) { return findItem(window->contentItem(), name); };
    auto click = [&](QQuickItem *item) {
      require(item && item->isVisible(), "click target is missing or hidden");
      const QPointF point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
      QMouseEvent press(QEvent::MouseButtonPress, point, window->mapToGlobal(point.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QMouseEvent release(QEvent::MouseButtonRelease, point, window->mapToGlobal(point.toPoint()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QCoreApplication::sendEvent(window, &press);
      QCoreApplication::sendEvent(window, &release);
      settle();
    };
    auto navigate = [&](int page) {
      qInfo() << "UI_CHECK_PAGE" << page;
      require(QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, page)), "navigation invocation failed");
      settle();
      require(window->property("currentPage").toInt() == page, "navigation did not reach requested page");
    };
    trace("window corners");
    settle();
    const QImage windowImage = window->grabWindow();
    require(!windowImage.isNull(), "window frame capture failed");
    for (const QPoint corner : {QPoint(0, 0), QPoint(windowImage.width() - 1, 0),
                                QPoint(0, windowImage.height() - 1), QPoint(windowImage.width() - 1, windowImage.height() - 1)})
      require(windowImage.pixelColor(corner).alpha() < 32, "window corner is not transparent");
    auto *theme = window->property("uiTheme").value<QObject *>();
    require(theme, "window theme missing");
    require(theme->metaObject()->indexOfProperty("isDark") == -1 &&
            theme->metaObject()->indexOfProperty("followSystem") == -1,
            "new workspace must not expose theme switching state");
    require(theme->property("background").value<QColor>().lightnessF() < 0.2 &&
            theme->property("text").value<QColor>().lightnessF() > 0.8,
            "old light-mode settings must not change the fixed dark palette");
    require(!uiItem("settings-dark") && !uiItem("settings-follow-system"),
            "removed theme switches are still present");
    if (!theme->property("nativeGlass").toBool())
      require(windowImage.pixelColor(windowImage.width() / 2, 0).alpha() > 240,
              "fallback window edge must remain opaque between rounded corners");
    auto *brand = uiItem("nav-brand");
    auto *menu = uiItem("nav-menu");
    auto *footer = uiItem("nav-footer");
    require(brand && menu && footer, "navigation sections missing");
    require(brand->mapToScene(QPointF()).y() > window->height() - 90,
            "brand must be in the bottom-left footer");
    require(menu->mapToScene(QPointF(0, menu->height())).y() <= footer->mapToScene(QPointF()).y(),
            "navigation menu overlaps footer");
    trace("manual parsing");
    navigate(1);
    backend->setProtocolType(0);
    auto *manualInput = uiItem("parse-input-1");
    auto *parseButton = uiItem("parse-button-1");
    require(manualInput && parseButton, "manual parse controls missing");
    manualInput->setProperty("text", "68 04 07 00 00 00");
    QEventLoop ready;
    QTimer::singleShot(50, &ready, &QEventLoop::quit);
    ready.exec();
    auto clickParse = [&] { click(parseButton); };
    clickParse();
    require(backend->resultModel()->rowCount() > 2, "clicking manual parse did not produce fields");
    require(!uiItem("encodingCombo")->isVisible(), "manual parsing must not show file encoding");
    for (int protocol : {0, 1}) {
      backend->setProtocolType(protocol);
      manualInput->setProperty("text", "0501024000020000");
      clickParse();
      bool hasOad = false;
      for (int row = 0; row < backend->resultModel()->rowCount(); ++row)
        hasOad |= backend->resultModel()->data(backend->resultModel()->index(row), ParseResultModel::ValueRole).toString().remove(' ').contains("40000200");
      require(hasOad && backend->parseResult() == QString::fromUtf8("解析成功"), "698 APDU must parse in auto and explicit modes");
      require(uiItem("parseStatus")->property("text").toString() == backend->parseResult(), "parse status is not displayed");
    }
    backend->setProtocolType(0);
    manualInput->setProperty("text", "05010240000200");
    clickParse();
    require(backend->resultModel()->rowCount() == 0 && uiItem("parseStatus")->property("text").toString() == backend->parseResult()
                && backend->parseResult().contains(QString::fromUtf8("无法识别")), "truncated APDU must show a failure");
    trace("live log selection and real protocol parsing");
    navigate(5);
    auto *livePage = window->findChild<QQuickItem *>("page-5");
    auto *ssh = livePage ? livePage->findChild<SshTerminal *>("ssh-terminal-backend") : nullptr;
    require(ssh, "live log backend is missing");
    ssh->clearBuffer();
    ssh->lineModel()->appendLines({"RX 68 04 07 00 00 00", "RX 05 01 02 40 00 02 00 00"});
    ssh->lineCountChanged(); settle();
    for (int row = 0; row < 2; ++row) {
      auto *line = uiItem("ssh-log-line-" + QString::number(ssh->lineIdAt(row)));
      click(line);
      const auto hex = livePage->property("selectedHexText").toString().remove(' ');
      require(hex == (row == 0 ? "680407000000" : "0501024000020000"), "live selection extracted the wrong frame");
      const auto point = line->mapToScene(QPointF(line->width() / 2, line->height() / 2));
      QMouseEvent press(QEvent::MouseButtonPress, point, window->mapToGlobal(point.toPoint()), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
      QMouseEvent release(QEvent::MouseButtonRelease, point, window->mapToGlobal(point.toPoint()), Qt::RightButton, Qt::NoButton, Qt::NoModifier);
      QCoreApplication::sendEvent(window, &press); QCoreApplication::sendEvent(window, &release); settle();
      click(uiItem("ssh-parse-auto"));
      require(backend->popupResultModel()->rowCount() > 2 && livePage->property("parsePanelVisible").toBool(),
              "live context menu did not produce real parsed fields");
      if (row == 1) {
        bool hasOad = false;
        for (int field = 0; field < backend->popupResultModel()->rowCount(); ++field)
          hasOad |= backend->popupResultModel()->data(backend->popupResultModel()->index(field), ParseResultModel::ValueRole)
                        .toString().remove(' ').contains("40000200");
        require(hasOad, "live 698 APDU selection lost the actual OAD field");
        const auto screenshot = qEnvironmentVariable("LOGANALYQT_SSH_UI_SCREENSHOT");
        if (!screenshot.isEmpty()) require(window->grabWindow().save(screenshot), "live log UI capture failed");
      }
    }
    ssh->clearBuffer(); livePage->setProperty("parsePanelVisible", false);
    trace("live log paused viewport across coalesced batches");
    const int previousLimit = ssh->maxBufferLines();
    ssh->setMaxBufferLines(100);
    QStringList viewportRows;
    for (int i = 0; i < 100; ++i) viewportRows.append(QString("Viewport row %1").arg(i));
    ssh->lineModel()->appendLines(viewportRows); ssh->lineCountChanged(); settle();
    livePage->setProperty("autoScroll", false);
    auto *logView = uiItem("ssh-log-list");
    require(logView && QMetaObject::invokeMethod(logView, "positionViewAtIndex", Q_ARG(int, 50), Q_ARG(int, 0)), "cannot position live history");
    settle();
    QMetaObject::invokeMethod(livePage, "captureLogViewport");
    const auto anchor = livePage->property("viewAnchorId").toLongLong();
    require(anchor >= 0, "paused history anchor missing");
    for (int batch = 0; batch < 3; ++batch) {
      ssh->linesAboutToChange(); ssh->lineModel()->appendLines({QString("Incoming %1").arg(batch)}); ssh->lineCountChanged();
    }
    settle(); QMetaObject::invokeMethod(livePage, "captureLogViewport");
    require(livePage->property("viewAnchorId").toLongLong() == anchor, "coalesced updates moved the paused log record");
    livePage->setProperty("autoScroll", true); ssh->clearBuffer(); ssh->setMaxBufferLines(previousLimit);
    std::function<void(QQuickItem *)> checkBrand = [&](QQuickItem *item) {
      if (!item->isVisible()) return;
      require(!item->property("text").toString().contains("LogAnalyQt"), "visible UI still contains the old brand");
      for (auto *child : item->childItems()) checkBrand(child);
    };
    for (int page = 0; page < 15; ++page) {
      navigate(page);
      checkBrand(window->contentItem());
      if (page == 10) {
        require(uiItem("help-app-name") && uiItem("help-app-name")->property("text").toString() == QString::fromUtf8("通联工坊"), "help app name is incorrect");
        require(uiItem("help-app-subtitle") && uiItem("help-app-subtitle")->property("text").toString() == QString::fromUtf8("通信开发与调试工作台"), "help subtitle is incorrect");
      }
      if (page >= 4) {
        auto *view = window->findChild<QQuickItem *>(QString("page-%1").arg(page));
        require(view && view->isVisible(), "workspace is not visible");
        require(view->width() > 500 && view->height() > 300, "workspace has invalid geometry");
      }
    }

    const QSize originalSize = window->size();
    for (const QSize size : {QSize(1920, 1080), QSize(1440, 860), QSize(1200, 740)}) {
      window->resize(size);
      for (const int page : {1, 2, 3}) {
        navigate(page);
        QEventLoop layoutSettled;
        QTimer::singleShot(40, &layoutSettled, &QEventLoop::quit);
        layoutSettled.exec();
        const QList<int> models = page == 3 ? QList<int>{4} : (page == 2 ? QList<int>{1, 2} : QList<int>{1});
        for (int model : models) {
          auto item = [&](const char *name) {
            return uiItem(QString::fromLatin1(name) + QString::number(model));
          };
          auto *preset = item("preset-card-");
          auto *controls = item("preset-controls-");
          auto *input = item("input-card-");
          auto *actions = item("input-actions-");
          auto *result = item("result-card-");
          require(preset && controls && input && actions && result, "analysis layout objects missing");
          require(preset->isVisible() == (page == 1), "presets must only appear on the manual parse page");
          if (preset->isVisible())
            require(controls->mapToItem(preset, QPointF(0, controls->height())).y() <= preset->height() - 8,
                    "preset controls overflow their card");
          else
            require(qAbs(input->width() - input->parentItem()->width()) < 1,
                    "hidden presets leave unused input space");
          require(actions->mapToItem(input, QPointF(0, actions->height())).y() <= input->height() - 8,
                  "manual input actions overflow their card");
          auto *actionParse = item("parse-button-");
          require(actionParse, "input parse button missing");
          require(actionParse->width() <= 140 && actionParse->height() <= 36,
                  "parse action buttons must stay within their maximum size in wide windows");
          int actionCount = 0;
          for (auto *button : actions->childItems()) {
            if (!button->property("text").isValid()) continue;
            ++actionCount;
            require(qAbs(button->width() - actionParse->width()) < 1 &&
                        qAbs(button->height() - actionParse->height()) < 1,
                    "parse, paste and column-fit buttons must have matching width and height");
            auto *content = button->property("contentItem").value<QQuickItem *>();
            require(content && content->width() + 1 >= content->implicitWidth(),
                    "maximum button width must still leave room for its text and icon");
          }
          require(actionCount == 3, "input action group must contain three buttons");
          const auto resultTop = result->mapToItem(window->contentItem(), QPointF()).y();
          require((!preset->isVisible() || preset->mapToItem(window->contentItem(), QPointF(0, preset->height())).y() <= resultTop)
                      && input->mapToItem(window->contentItem(), QPointF(0, input->height())).y() <= resultTop,
                  "input cards overlap parsed results");
        }
      }
    }
    window->resize(originalSize);

    trace("multi-tab parsing");
    navigate(3);
    auto *tabs = uiItem("parseTabs");
    require(tabs && tabs->property("count").toInt() == 5, "multi-parse must start with five tabs");
    auto *sessionEncoding = uiItem("sessionEncoding");
    auto *sessionProtocol = uiItem("sessionProtocol");
    auto *toolbar = uiItem("multiParseToolbar");
    require(sessionEncoding && sessionProtocol && toolbar, "tab toolbar controls missing");
    for (auto *control : {sessionEncoding, sessionProtocol}) {
      const QPointF top = control->mapToItem(toolbar, QPointF());
      require(top.y() >= 0 && top.y() + control->height() <= toolbar->height(), "parser options are outside tab row");
    }
    const int originalProtocol = backend->protocolType();
    QList<QString> parsedValues;
    for (int tab = 0; tab < 11; ++tab) {
      if (tab >= 5)
        require(QMetaObject::invokeMethod(window, "addParseTab"), "adding parse tab failed");
      else
        tabs->setProperty("currentIndex", tab);
      settle();
      require(tabs->property("currentIndex").toInt() == tab, "new tab was not selected");
      const int modelId = tab + 4;
      auto *pane = uiItem(QString("session-pane-%1").arg(modelId));
      require(pane && pane->isVisible(), "new parse pane is missing");
      require(QMetaObject::invokeMethod(sessionProtocol, "activated", Q_ARG(int, tab % 2 ? 0 : 3)), "protocol selector failed");
      require(QMetaObject::invokeMethod(sessionEncoding, "activated", Q_ARG(int, tab % 4)), "encoding selector failed");
      const QString hex = tab % 2 ? "68 04 0B 00 00 00" : "68 04 07 00 00 00";
      require(QMetaObject::invokeMethod(window, "performParse", Q_ARG(QVariant, hex), Q_ARG(QVariant, modelId)), "tab parse failed");
      auto *model = backend->parseModel(modelId);
      require(model && model->rowCount() > 2, "tab has no parsed fields");
      QString values;
      for (int row = 0; row < model->rowCount(); ++row)
        values += model->data(model->index(row), ParseResultModel::ValueRole).toString() + "\n";
      parsedValues.append(values);
      auto *input = uiItem(QString("parse-input-%1").arg(modelId));
      require(input, "tab input is missing");
      input->setProperty("text", hex + QString("\n未解析草稿 %1").arg(tab));
    }
    require(tabs->property("count").toInt() == 11, "must support more than ten tabs");
    auto *firstTab = uiItem("parse-tab-0");
    require(firstTab && firstTab->mapToItem(tabs, QPointF()).y() >= 0
                && firstTab->mapToItem(tabs, QPointF(0, firstTab->height())).y() <= tabs->height(),
            "tab buttons are vertically clipped");
    require(QMetaObject::invokeMethod(uiItem("previousParseTab"), "clicked"), "previous tab button failed");
    require(tabs->property("currentIndex").toInt() == 9, "previous tab button did not select tab ten");
    require(QMetaObject::invokeMethod(uiItem("nextParseTab"), "clicked"), "next tab button failed");
    require(tabs->property("currentIndex").toInt() == 10, "next tab button did not select tab eleven");
    require(parsedValues[0] != parsedValues[1], "tab fixtures do not distinguish results");
    require(backend->protocolType() == originalProtocol, "tab protocol changed the shared parser setting");
    navigate(2);
    navigate(3);
    for (int tab = 10; tab >= 0; --tab) {
      tabs->setProperty("currentIndex", tab);
      settle();
      for (int other = 0; other < 11; ++other) {
        auto *pane = uiItem(QString("session-pane-%1").arg(other + 4));
        require(pane && pane->isVisible() == (other == tab), "tab switch must show exactly one pane");
      }
      const int modelId = tab + 4;
      auto *pane = uiItem(QString("session-pane-%1").arg(modelId));
      require(pane->property("protocolIndex").toInt() == (tab % 2 ? 0 : 3)
                  && pane->property("encodingIndex").toInt() == tab % 4, "tab parser options were overwritten");
      require(sessionProtocol->property("currentIndex").toInt() == (tab % 2 ? 0 : 3)
                  && sessionEncoding->property("currentIndex").toInt() == tab % 4, "toolbar does not follow selected tab");
      auto *input = uiItem(QString("parse-input-%1").arg(modelId));
      require(input->property("text").toString().endsWith(QString("未解析草稿 %1").arg(tab)), "tab draft was lost");
      auto *model = backend->parseModel(modelId);
      QString values;
      for (int row = 0; row < model->rowCount(); ++row)
        values += model->data(model->index(row), ParseResultModel::ValueRole).toString() + "\n";
      require(values == parsedValues[tab], "switching tabs changed parsed results");
      backend->handleLink("highlight:0,1", modelId);
      require(input->property("selectedText").toString() == "68", "tab field highlight targeted wrong input");
    }
    backend->parseHex("", "auto", 14, 0);
    require(backend->parseModel(14)->rowCount() == 0 && backend->parseModel(4)->rowCount() > 2,
            "clearing a tab affected another tab");

    trace("files and filters");
    QTemporaryDir files;
    require(files.isValid(), "temporary log directory failed");
    const QString path = files.filePath(QString::fromUtf8("设备 日志.log"));
    const QString preview698 = backend->buildFullFrame("05 01 01 40 00 02 00 00", {{"serverAddrHex", "000000000001"}});
    require(!preview698.isEmpty() && !preview698.startsWith("ERR:"), "preview 698 fixture failed");
    const QByteArray log = QByteArray("10:24:16 [TX] 68 04 07 00 00 00\n10:24:17 [RX] 68 04 0B 00 00 00\n")
        + "10:24:18 [TX] " + preview698.toUtf8() + "\n";
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(log) == log.size(), "test log write failed");
    const QDateTime modified(QDate(2026, 10, 18), QTime(9, 8, 7), QTimeZone::LocalTime);
    require(file.flush() && file.setFileTime(modified, QFileDevice::FileModificationTime), "test log timestamp failed");
    file.close();
    for (const QString &name : {QString("device10.log"), QString("device2.log")}) {
      QFile other(files.filePath(name));
      require(other.open(QIODevice::WriteOnly) && other.write(name.toUtf8()) > 0,
              "additional log fixture failed");
    }
    require(QMetaObject::invokeMethod(window, "openLogUrl", Q_ARG(QVariant, QUrl::fromLocalFile(path))), "log URL open failed");
    settle();
    require(backend->currentFileContent().toUtf8() == log, "local file URL or Unicode path was lost");
    auto *fileCombo = uiItem("fileCombo");
    require(fileCombo && backend->fileList().indexOf("device2.log") < backend->fileList().indexOf("device10.log"),
            "log list lost numeric filename ordering");
    require(fileCombo->property("currentText").toString() ==
                QString::fromUtf8("设备 日志.log  |  修改：2026-10-18 09:08:07  |  大小：%1 bytes").arg(log.size()),
            "log selector must display the filename, local modification time and file size");
    require(fileCombo->property("model").toStringList() == backend->fileDisplayList(),
            "log dropdown does not use timestamped labels");
    for (const QString &name : {QString("device10.log"), QFileInfo(path).fileName()}) {
      const int index = backend->fileList().indexOf(name);
      fileCombo->setProperty("currentIndex", index);
      require(QMetaObject::invokeMethod(fileCombo, "activated", Q_ARG(int, index)), "log selection failed");
      require(backend->currentFileContent().toUtf8() == (name == "device10.log" ? name.toUtf8() : log),
              "timestamped selection loaded the wrong file");
      backend->applyFilter("no matching lines");
      require(QMetaObject::invokeMethod(uiItem("encodingCombo"), "activated", Q_ARG(int, 0)), "encoding selection failed");
      require(backend->currentFileContent().toUtf8() == (name == "device10.log" ? name.toUtf8() : log),
              "changing encoding must reload the original filename without its timestamp");
    }
    require(file.open(QIODevice::ReadWrite) &&
                file.setFileTime(modified.addDays(1), QFileDevice::FileModificationTime), "timestamp refresh fixture failed");
    file.close();
    for (const auto size : {qint64(0), qint64(1024), qint64(1024 * 1024)}) {
      QFile sizedFile(files.filePath(QString("size%1.log").arg(size)));
      require(sizedFile.open(QIODevice::WriteOnly) && sizedFile.resize(size), "file size fixture failed");
    }
    backend->openFolder(files.path());
    settle();
    require(backend->fileDisplayList().at(backend->fileList().indexOf(QFileInfo(path).fileName())).contains("2026-10-19 09:08:07"),
            "refreshing the folder must update modification times");
    for (const auto &entry : {qMakePair(QString("size0.log"), QString("0 bytes")),
                              qMakePair(QString("size1024.log"), QString("1.0 KiB")),
                              qMakePair(QString("size1048576.log"), QString("1.0 MiB"))}) {
      require(backend->fileDisplayList().at(backend->fileList().indexOf(entry.first)).endsWith(entry.second),
              "file sizes must use readable units, including empty files");
    }
    QFile resizedFile(files.filePath("size1024.log"));
    require(resizedFile.open(QIODevice::ReadWrite) && resizedFile.resize(2048), "file size refresh fixture failed");
    resizedFile.close();
    backend->openFolder(files.path());
    require(backend->fileDisplayList().at(backend->fileList().indexOf("size1024.log")).endsWith("2.0 KiB"),
            "refreshing the folder must update file sizes");
    QTemporaryDir emptyFolder;
    require(emptyFolder.isValid(), "empty log directory failed");
    backend->openFolder(emptyFolder.path());
    settle();
    require(backend->fileList().isEmpty() && backend->fileDisplayList().isEmpty() &&
                fileCombo->property("count").toInt() == 0, "empty folder kept stale log labels");
    require(QMetaObject::invokeMethod(window, "openLogUrl", Q_ARG(QVariant, QUrl::fromLocalFile(path))), "log URL reopen failed");
    settle();
    backend->applyFilter("RX");
    require(!backend->currentFileContent().contains("[TX]"), "filter not applied");
    const QString filtered = backend->currentFileContent();
    backend->applyFilter("[");
    require(backend->currentFileContent() == filtered, "invalid regex replaced the current log view");
    backend->applyFilter("(?i)rx");
    require(backend->currentFileContent() == filtered, "Qt regex syntax must remain supported");
    backend->applyFilter("");
    require(backend->currentFileContent().toUtf8() == log, "clearing filter lost unfiltered source text");
    trace("global preview row clicks");
    backend->setProtocolType(1);
    click(uiItem("globalPreviewButton"));
    require(window->property("isGlobalPreviewMode").toBool() && backend->globalPreviewModel()->rowCount() == 3,
            "global preview did not scan the mixed-protocol log");
    for (int row : {1, 0, 2}) {
      backend->clearAllParses();
      settle();
      qInfo() << "UI_CHECK_PREVIEW_ROW" << row;
      click(uiItem(QString("global-preview-row-%1").arg(row)));
      const QString raw = backend->globalPreviewModel()->data(backend->globalPreviewModel()->index(row), GlobalPreviewModel::RawDataRole).toString();
      require(backend->resultModel()->rowCount() > 2 && backend->parseResult().contains(QString::fromUtf8("解析成功")),
              "clicking a preview row did not produce parsed fields");
      if (row < 2)
        require(backend->resultModel()->rowCount() == 3 &&
                    backend->resultModel()->data(backend->resultModel()->index(2), ParseResultModel::ValueRole).toString() == raw.mid(6),
                "104 preview fields did not follow the clicked row with 698 selected in the toolbar");
      else
        require(backend->resultModel()->rowCount() > 5, "698 preview fields are missing");
      require(uiItem("selected-frame-1")->property("text").toString() == raw,
              "selected frame text did not follow the preview row");
      backend->handleLink("highlight:0,1", 1);
      require(uiItem("selected-frame-1")->property("selectedText").toString() == "68" && backend->resultModel()->rowCount() > 2,
              "preview byte highlighting cleared the parsed fields");
    }
    click(uiItem("globalPreviewButton"));
    require(!window->property("isGlobalPreviewMode").toBool() && backend->currentFileContent().toUtf8() == log,
            "leaving global preview changed the log content");

    navigate(1);
    backend->setProtocolType(3);
    require(QMetaObject::invokeMethod(window, "performParse", Q_ARG(QVariant, "68 04 07 00 00 00"), Q_ARG(QVariant, 1)), "manual parse invocation failed");
    require(backend->resultModel()->rowCount() > 2, "manual parser returned no real fields");
    backend->parseHex("68 04 0B 00 00 00", "auto", 2);
    backend->compareModels();
    require(backend->findNextDiff(-1) >= 0, "diff model missing changed fields");
    backend->clearDiffs();
    require(backend->findNextDiff(-1) < 0, "diff state not cleared");

    trace("frame verification");
    const QString frame = backend->buildFullFrame("05 01 01 40 00 02 00 00", {{"serverAddrHex", "000000000001"}, {"prm", true}, {"funcCode", 3}});
    require(!frame.startsWith("ERR:") && !frame.isEmpty(), "698 frame build failed");
    for (const QString &badApdu : {QString(), QString("GG0501014000020000"), QString("050101400002000"), QString("05:01:01:40:00:02:00:00")})
      require(backend->buildFullFrame(badApdu, {{"serverAddrHex", "000000000001"}}).startsWith("ERR:"),
              "real buildFullFrame accepted invalid or incomplete APDU Hex");
    const QList<QVariantMap> invalidFrameOptions{
        {{"split", true}}, {{"scramble", true}},
        {{"serverAddrHex", "GG"}}, {{"serverAddrHex", "123"}}, {{"serverAddrHex", ""}},
        {{"physicalAddressHex", ""}}, {{"serverAddrHex", "000000000001"}, {"physicalAddressHex", "000000000001"}},
        {{"serverAddrHex", "000000000001"}, {"funcCode", true}},
        {{"serverAddrHex", "000000000001"}, {"funcCode", 8}},
        {{"serverAddrHex", "000000000001"}, {"dir", "false"}},
        {{"serverAddrHex", "000000000001"}, {"unknownOption", 1}}};
    for (const auto &options : invalidFrameOptions)
      require(backend->buildFullFrame("0501014000020000", options).startsWith("ERR:"),
              "real buildFullFrame silently fell back after explicit invalid options");
    require(backend->buildFullFrame("0501014000020000", {{"physicalAddressHex", "010203040506"}}) ==
                backend->buildFullFrame("0501014000020000", {{"serverAddrHex", "060504030201"}}),
            "real buildFullFrame display/wire address entry paths disagree");
    const QString directionFrame = backend->buildFullFrame("0501014000020000", {{"dir", true}, {"prm", false}});
    require(!directionFrame.startsWith("ERR:") && QByteArray::fromHex(directionFrame.toLatin1()).at(3) == char(0x83),
            "default address replaced explicitly selected control bits");
    backend->setProtocolType(1);
    require(QMetaObject::invokeMethod(window, "performParse", Q_ARG(QVariant, frame), Q_ARG(QVariant, 1)), "698 return-to-analysis failed");
    require(backend->resultModel()->rowCount() > 5, "698 fields missing");
    require(!backend->verifyHexData(frame).isEmpty(), "698 frame verification failed");
    backend->parseHex(frame + " " + frame, "auto", 2);
    int frameSeparators = 0;
    for (int row = 0; row < backend->resultModel2()->rowCount(); ++row)
      if (backend->resultModel2()->data(backend->resultModel2()->index(row, 0), ParseResultModel::FrameSeparatorRole).toBool()) ++frameSeparators;
    require(frameSeparators == 1, "second 698 pane lost multi-frame separators");
    navigate(4);
    auto *framing = window->findChild<QObject *>("page-4");
    trace("smart framing GET_NORMAL generation");
    auto *oadSelector = uiItem("get-normal-oad");
    require(oadSelector && oadSelector->isVisible(), "GET_NORMAL OAD selector is missing");
    for (const QString &name : {QString(), QString::fromUtf8("总及费率电能量数组")}) {
      const QVariant selection = QVariantMap{{"oi", "0010"}, {"attrId", 2}, {"index", 0}, {"name", name}};
      require(QMetaObject::invokeMethod(oadSelector, "setValue", Q_ARG(QVariant, selection)), "OAD selection failed");
      require(QMetaObject::invokeMethod(framing, "buildApdu"), "APDU generation invocation failed");
      const auto apdu = QByteArray::fromHex(framing->property("apduHex").toString().toLatin1());
      require(!framing->property("statusIsError").toBool() && apdu.size() == 8, "UI-generated OAD did not produce an APDU");
      require(apdu.left(2) == QByteArray::fromHex("0501") && apdu.mid(3) == QByteArray::fromHex("0010020000"),
              "UI-generated GET_NORMAL APDU bytes are incorrect");
      require(QMetaObject::invokeMethod(framing, "buildFrame"), "full frame generation invocation failed");
      require(!framing->property("statusIsError").toBool() && !framing->property("frameHex").toString().isEmpty(),
              qPrintable("UI-generated APDU did not produce a full frame: " + framing->property("statusText").toString()));
      require(QMetaObject::invokeMethod(framing, "verifyCurrent"), "generated frame verification invocation failed");
      settle();
      require(!framing->property("statusIsError").toBool() && !framing->property("verifyFields").toList().isEmpty(),
              "UI-generated frame did not parse");
    }
    framing->setProperty("frameHex", frame);
    require(QMetaObject::invokeMethod(framing, "verifyCurrent"), "verification popup invocation failed");
    settle();
    require(!framing->property("verifyFields").toList().isEmpty(), "verification popup has no rows");
    trace("frame result return");
    require(QMetaObject::invokeMethod(framing, "framingResult", Q_ARG(QString, frame), Q_ARG(int, 1)), "framing result signal failed");
    require(window->property("currentPage").toInt() == 1 && backend->resultModel()->rowCount() > 5, "framing result did not return to manual page");
    settle();
    trace("APP result return");
    const QString appFrame = "01 00 00 00 00 63 6C 69 65 6E 74 00 73 65 72 76 65 72 00 10 00 00 00 00";
    require(QMetaObject::invokeMethod(framing, "framingResult", Q_ARG(QString, appFrame), Q_ARG(int, 4)), "APP return signal failed");
    require(backend->protocolType() == 4 && backend->resultModel()->rowCount() > 5, "APP return targeted the wrong parser/model");
    settle();
    trace("APDU popup");
    require(QMetaObject::invokeMethod(window, "inspectApdu", Q_ARG(QVariant, "05 01 01 40 00 02 00 00")), "APDU inspection failed");
    auto *apduWindow = window->findChild<QQuickWindow *>("apduInspection");
    require(apduWindow && apduWindow->isVisible() && backend->popupResultModel()->rowCount() > 0, "APDU window did not display parsed data");
    settle();
    apduWindow->close();
    settle();

    trace("CSG parsing");
    const QByteArray csg = BuildCsgApplicationFrame(0x88, 0x0A, 0x01,
        BuildCsgDataUnit(0, 0xE0000000, QByteArray::fromHex("00")));
    backend->setProtocolType(8);
    backend->parseHex(QString::fromLatin1(csg.toHex(' ')), "auto");
    require(backend->resultModel()->rowCount() > 5, "CSG parser unavailable in migrated UI");

    trace("presets and merge");
    const QString summary = "__ui_migration_runtime_fixture__";
    require(backend->addPresetMessage("698", summary, frame), "preset create failed");
    require(backend->getPresetContent("698", summary) == frame, "preset persistence failed");
    require(backend->updatePresetMessage("698", summary, summary, frame + " "), "preset update failed");
    require(backend->removePresetMessage("698", summary), "preset remove failed");
    require(!backend->getPresetList("698").contains(summary), "deleted preset is still visible");

    auto *merger = window->findChild<LogMerger *>();
    require(merger != nullptr, "log merge workspace lost its backend");
    const QString later = files.filePath("later.log");
    const QString earlier = files.filePath("earlier.log");
    QFile first(later), second(earlier);
    require(first.open(QIODevice::WriteOnly) && second.open(QIODevice::WriteOnly), "merge fixtures unavailable");
    first.write("2026-10-14 10:00:02.000 later\n");
    second.write("2026-10-14 10:00:01.000 earlier\n");
    first.close(); second.close();
    merger->clearFiles();
    merger->addFiles({QUrl::fromLocalFile(later).toString(), QUrl::fromLocalFile(earlier).toString()});
    merger->setOutputPath(files.filePath("merged.log"));
    merger->mergeFiles();
    QFile merged(merger->outputPath());
    require(merged.open(QIODevice::ReadOnly), "merge produced no output");
    const QByteArray mergedText = merged.readAll();
    require(mergedText.indexOf("earlier") >= 0 && mergedText.indexOf("later") > mergedText.indexOf("earlier"), "merge timestamp order is incorrect");
    require(backend->getReadmeContent().contains(QString::fromUtf8("多协议")), "bundled help is missing");
    require(!backend->getReadmeContent().contains("LogAnalyQt"), "in-app help still contains the old brand");
    require(backend->getChangelogContent() != "CHANGELOG.md not found.", "bundled changelog is missing");
    navigate(1);
    backend->setProtocolType(0);
    manualInput->setProperty("text", "0501024000020000");
    clickParse();
    require(backend->resultModel()->rowCount() > 2, "returning to manual page lost APDU parsing");
    trace("personal configuration management");
    navigate(9);
    auto *settingsPage = uiItem("page-9");
    require(settingsPage && settingsPage->setProperty("section", 2), "personal configuration section missing");
    settle();
    require(uiItem("personalConfigPage") && uiItem("personalConfigPage")->isVisible(), "personal configuration page not visible");
    require(uiItem("personal-config-summary") && uiItem("personal-config-summary")->property("text").toString().contains("settings.json"), "personal configuration manager summary missing");
    require(uiItem("personal-config-export-full") && uiItem("personal-config-export-full")->isEnabled(), "backup entry disabled");
    QVariant saved;
    require(QMetaObject::invokeMethod(window, "flushPersonalConfig", Q_RETURN_ARG(QVariant, saved)) && saved.toBool(), "pending edits must flush before backup");
    trace("finish");
    navigate(0);
    return {};
  } catch (const QString &error) {
    return error;
  }
}
