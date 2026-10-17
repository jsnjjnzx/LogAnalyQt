#pragma once

#include "LogBackend.h"
#include "LogMerger.h"
#include "csg_upstream_test_support.h"
#include <QFile>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QQmlEngine>
#include <QQmlIncubationController>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QUrl>
#include <functional>

// Called only by --ui-smoke --ui-checks from the isolated runtime test runner.
inline QString checkMigratedUi(QQuickWindow *window) {
  auto require = [](bool ok, const char *message) {
    if (!ok) throw QString::fromUtf8(message);
  };
  try {
    for (auto *model : QList<QAbstractItemModel *>{new ParseResultModel, new GlobalPreviewModel}) {
      bool reset = false;
      QObject::connect(model, &QAbstractItemModel::modelReset, [&] { reset = true; });
      delete model;
      require(!reset, "destroying a model must not restart view delegates");
    }
    auto *backend = window->findChild<LogBackend *>("analysisBackend");
    require(backend != nullptr, "real LogBackend is missing");
    auto settle = [&] {
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
    auto navigate = [&](int page) {
      require(QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, page)), "navigation invocation failed");
      settle();
      require(window->property("currentPage").toInt() == page, "navigation did not reach requested page");
    };
    navigate(1);
    backend->setProtocolType(0);
    auto *manualInput = uiItem("parse-input-1");
    auto *parseButton = uiItem("parse-button-1");
    require(manualInput && parseButton, "manual parse controls missing");
    manualInput->setProperty("text", "68 04 07 00 00 00");
    QEventLoop ready;
    QTimer::singleShot(50, &ready, &QEventLoop::quit);
    ready.exec();
    auto clickParse = [&] {
      const QPointF click = parseButton->mapToScene(QPointF(parseButton->width() / 2, parseButton->height() / 2));
      QMouseEvent press(QEvent::MouseButtonPress, click, window->mapToGlobal(click.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QMouseEvent release(QEvent::MouseButtonRelease, click, window->mapToGlobal(click.toPoint()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QCoreApplication::sendEvent(window, &press);
      QCoreApplication::sendEvent(window, &release);
      settle();
    };
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
    for (int page = 0; page < 15; ++page) {
      navigate(page);
      if (page >= 4) {
        auto *view = window->findChild<QQuickItem *>(QString("page-%1").arg(page));
        require(view && view->isVisible(), "workspace is not visible");
        require(view->width() > 500 && view->height() > 300, "workspace has invalid geometry");
      }
    }

    const QSize originalSize = window->size();
    for (const QSize size : {QSize(1440, 860), QSize(1200, 740)}) {
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
          const auto resultTop = result->mapToItem(window->contentItem(), QPointF()).y();
          require((!preset->isVisible() || preset->mapToItem(window->contentItem(), QPointF(0, preset->height())).y() <= resultTop)
                      && input->mapToItem(window->contentItem(), QPointF(0, input->height())).y() <= resultTop,
                  "input cards overlap parsed results");
        }
      }
    }
    window->resize(originalSize);

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

    QTemporaryDir files;
    require(files.isValid(), "temporary log directory failed");
    const QString path = files.filePath(QString::fromUtf8("设备 日志.log"));
    const QByteArray log("10:24:16 [TX] 68 04 07 00 00 00\n10:24:17 [RX] 68 04 0B 00 00 00\n");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(log) == log.size(), "test log write failed");
    file.close();
    require(QMetaObject::invokeMethod(window, "openLogUrl", Q_ARG(QVariant, QUrl::fromLocalFile(path))), "log URL open failed");
    require(backend->currentFileContent().toUtf8() == log, "local file URL or Unicode path was lost");
    backend->applyFilter("RX");
    require(!backend->currentFileContent().contains("[TX]"), "filter not applied");
    const QString filtered = backend->currentFileContent();
    backend->applyFilter("[");
    require(backend->currentFileContent() == filtered, "invalid regex replaced the current log view");
    backend->applyFilter("(?i)rx");
    require(backend->currentFileContent() == filtered, "Qt regex syntax must remain supported");
    backend->applyFilter("");
    require(backend->currentFileContent().toUtf8() == log, "clearing filter lost unfiltered source text");
    backend->scanGlobalPreview();
    require(backend->globalPreviewModel()->rowCount() == 2, "global preview did not parse both frames");

    navigate(1);
    backend->setProtocolType(3);
    require(QMetaObject::invokeMethod(window, "performParse", Q_ARG(QVariant, "68 04 07 00 00 00"), Q_ARG(QVariant, 1)), "manual parse invocation failed");
    require(backend->resultModel()->rowCount() > 2, "manual parser returned no real fields");
    backend->parseHex("68 04 0B 00 00 00", "auto", 2);
    backend->compareModels();
    require(backend->findNextDiff(-1) >= 0, "diff model missing changed fields");
    backend->clearDiffs();
    require(backend->findNextDiff(-1) < 0, "diff state not cleared");

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
    framing->setProperty("frameHex", frame);
    require(QMetaObject::invokeMethod(framing, "verifyCurrent"), "verification popup invocation failed");
    settle();
    require(!framing->property("verifyFields").toList().isEmpty(), "verification popup has no rows");
    require(QMetaObject::invokeMethod(framing, "framingResult", Q_ARG(QString, frame), Q_ARG(int, 1)), "framing result signal failed");
    require(window->property("currentPage").toInt() == 1 && backend->resultModel()->rowCount() > 5, "framing result did not return to manual page");
    const QString appFrame = "01 00 00 00 00 63 6C 69 65 6E 74 00 73 65 72 76 65 72 00 10 00 00 00 00";
    require(QMetaObject::invokeMethod(framing, "framingResult", Q_ARG(QString, appFrame), Q_ARG(int, 4)), "APP return signal failed");
    require(backend->protocolType() == 4 && backend->resultModel()->rowCount() > 5, "APP return targeted the wrong parser/model");
    require(QMetaObject::invokeMethod(window, "inspectApdu", Q_ARG(QVariant, "05 01 01 40 00 02 00 00")), "APDU inspection failed");
    auto *apduWindow = window->findChild<QQuickWindow *>("apduInspection");
    require(apduWindow && apduWindow->isVisible() && backend->popupResultModel()->rowCount() > 0, "APDU window did not display parsed data");
    apduWindow->close();

    const QByteArray csg = BuildCsgApplicationFrame(0x88, 0x0A, 0x01,
        BuildCsgDataUnit(0, 0xE0000000, QByteArray::fromHex("00")));
    backend->setProtocolType(8);
    backend->parseHex(QString::fromLatin1(csg.toHex(' ')), "auto");
    require(backend->resultModel()->rowCount() > 5, "CSG parser unavailable in migrated UI");

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
    require(backend->getChangelogContent() != "CHANGELOG.md not found.", "bundled changelog is missing");
    navigate(1);
    backend->setProtocolType(0);
    manualInput->setProperty("text", "0501024000020000");
    clickParse();
    require(backend->resultModel()->rowCount() > 2, "returning to manual page lost APDU parsing");
    navigate(0);
    return {};
  } catch (const QString &error) {
    return error;
  }
}
