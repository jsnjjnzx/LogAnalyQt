#pragma once

#include "LogBackend.h"
#include "../src/common/AppPaths.h"
#include <QDir>
#include <QFile>
#include <QQuickItem>
#include <QQuickWindow>
#include <functional>

inline QString checkParseWorkspace(QQuickWindow *window, const QString &phase) {
  auto require = [](bool ok, const char *message) {
    if (!ok) throw QString::fromUtf8(message);
  };
  try {
    std::function<QQuickItem *(QQuickItem *, const QString &)> find =
        [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
      if (parent->objectName() == name) return parent;
      for (auto *child : parent->childItems())
        if (auto *item = find(child, name)) return item;
      return nullptr;
    };
    auto item = [&](const QString &name) { return find(window->contentItem(), name); };
    auto call = [&](const char *method) { require(QMetaObject::invokeMethod(window, method), "workspace action failed"); };
    auto closeTab = [&](int index) {
      auto *button = item(QString("close-parse-tab-%1").arg(index));
      require(button && QMetaObject::invokeMethod(button, "clicked"), "tab close button failed");
      QCoreApplication::processEvents();
    };
    auto *backend = window->findChild<LogBackend *>("analysisBackend");
    auto *tabs = item("parseTabs");
    require(backend && tabs, "workspace is missing");
    const QString key = "next/multiParse/workspace";
    const QString draft = QString::fromUtf8("68 04 07 00 00 00\n未解析的中文草稿\n第二行");

    if (phase == "write") {
      require(tabs->property("count").toInt() == 5, "first launch must have five tabs");
      QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, 3));
      auto *pane = item("session-pane-4");
      pane->setProperty("protocolIndex", 3);
      require(QMetaObject::invokeMethod(window, "performParse", Q_ARG(QVariant, "68 04 07 00 00 00"), Q_ARG(QVariant, 4)), "fixture parse failed");
      pane->setProperty("protocolIndex", 1);
      pane->setProperty("encodingIndex", 2);
      pane->setProperty("autoFitLocked", true);
      pane->setProperty("columnRatio1", 0.25);
      pane->setProperty("columnRatio2", 0.35);
      item("parse-input-6")->setProperty("text", QString::fromUtf8("保留原解析 3"));
      tabs->setProperty("currentIndex", 3);
      closeTab(4);
      require(tabs->property("currentIndex").toInt() == 3, "closing a later tab changed selection");
      closeTab(1);
      require(tabs->property("currentIndex").toInt() == 2, "closing an earlier tab lost logical selection");
      require(!backend->parseModel(5) && !backend->parseModel(8), "closed models were retained");
      call("addParseTab");
      require(backend->parseModel(9) && backend->parseModel(7), "new tab reused a live model ID");
      item("parse-input-9")->setProperty("text", QString::fromUtf8("新建解析页"));
      tabs->setProperty("currentIndex", 0);
      // Close immediately after editing, before the debounce timer can save.
      item("parse-input-4")->setProperty("text", draft);
    } else if (phase == "read") {
      require(tabs->property("count").toInt() == 4, "restart did not restore the saved tab count");
      require(tabs->property("currentIndex").toInt() == 0 && window->property("currentPage").toInt() == 3,
              "restart lost the selected tab/workspace");
      require(item("parse-input-4")->property("text").toString() == draft, "restart lost the exact draft");
      require(item("parse-input-5")->property("text").toString() == QString::fromUtf8("保留原解析 3"), "restart changed tab order");
      require(item("parse-input-7")->property("text").toString() == QString::fromUtf8("新建解析页"), "new tab was not saved");
      auto *pane = item("session-pane-4");
      require(pane->property("protocolIndex").toInt() == 1 && pane->property("encodingIndex").toInt() == 2,
              "restart lost parser choices");
      require(item("sessionProtocol")->property("currentIndex").toInt() == 1
                  && item("sessionEncoding")->property("currentIndex").toInt() == 2, "restored toolbar does not show selected tab options");
      require(pane->property("autoFitLocked").toBool()
                  && qAbs(pane->property("columnRatio1").toDouble() - 0.25) < 0.001
                  && qAbs(pane->property("columnRatio2").toDouble() - 0.35) < 0.001, "restart lost column settings");
      auto *model = backend->parseModel(4);
      require(model->rowCount() == 3 && model->data(model->index(2), ParseResultModel::ValueRole).toString().contains("07"),
              "restart parsed the draft or used the new protocol instead of the last parsed state");
      while (tabs->property("count").toInt()) closeTab(0);
      require(tabs->property("currentIndex").toInt() == -1, "closing all tabs left a selected tab");
      require(!item("sessionProtocol")->isEnabled() && !item("sessionEncoding")->isEnabled(), "empty workspace selectors are enabled");
    } else if (phase == "empty") {
      require(tabs->property("count").toInt() == 0, "zero saved tabs were replaced with defaults");
      call("addParseTab");
      require(tabs->property("count").toInt() == 1 && item("session-pane-4")->isVisible(), "cannot add after restoring zero tabs");
      require(QMetaObject::invokeMethod(item("sessionProtocol"), "activated", Q_ARG(int, 3)), "new tab selector unavailable");
      require(item("session-pane-4")->property("protocolIndex").toInt() == 3, "toolbar did not reconnect after adding to empty workspace");
      closeTab(0);
    } else if (phase == "corrupt-write") {
      require(backend->setUiSetting(key, "{broken-json"), "corrupt fixture write failed");
      return {};
    } else if (phase == "corrupt-read") {
      require(tabs->property("count").toInt() == 5, "corrupt state did not recover to five tabs");
      require(backend->getUiSetting("next/multiParse/recovery").toString() == "{broken-json", "corrupt data was not backed up");
      require(!window->property("parseWorkspaceError").toString().isEmpty(), "corrupt state recovery was silent");
    } else if (phase == "write-failure") {
      const QString path = QDir(AppPaths::writableDataRoot()).filePath("LogAnalyQt.ini");
      QFile::remove(path);
      require(QDir().mkdir(path), "cannot create unwritable settings fixture");
      window->setProperty("installingUpdate", true);
      window->close();
      require(window->isVisible() && !window->property("parseWorkspaceError").toString().isEmpty(),
              "save failure allowed closing and losing edits");
      window->setProperty("installingUpdate", false);
      require(QDir().rmdir(path), "cannot remove settings fixture");
    } else {
      throw QString("unknown workspace test phase");
    }
    window->close();
    require(!window->isVisible(), "normal close did not save the workspace");
    return {};
  } catch (const QString &error) {
    return error;
  }
}
