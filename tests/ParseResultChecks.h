#pragma once

#include "LogBackend.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QQmlEngine>
#include <QQmlIncubationController>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <functional>

inline void checkParseResultPresentation() {
  auto require = [](bool ok, const char *message) {
    if (!ok) throw QString::fromUtf8(message);
  };
  ParseResultModel model;
  auto add = [&](const QString &name, int indent, bool separator = false) {
    ParseResultItem item;
    item.name = name;
    item.indent = indent;
    item.description = "<原说明> & 完整文本";
    item.value = "01 02";
    item.offset = model.rowCount() * 2;
    item.length = 2;
    item.isFrameSeparator = separator;
    model.addItem(item);
  };
  auto role = [&](int row, int value) { return model.data(model.index(row), value); };
  add("根", 0);
  add("分组", 1);
  add("叶子", 2);
  add("同级", 1);
  add("下一根", 0);
  add("帧边界", 0, true);
  add("新帧", 0);
  add("子节点", 1);
  require(role(1, ParseResultModel::HasChildrenRole).toBool() &&
              !role(2, ParseResultModel::HasChildrenRole).toBool(), "tree parent/leaf metadata is incorrect");
  require(role(1, ParseResultModel::BranchContinuesRole).toBool() &&
              !role(2, ParseResultModel::BranchContinuesRole).toBool(), "tree sibling termination is incorrect");
  require(role(2, ParseResultModel::TreeGuidesRole).toList() == QVariantList{1},
          "ancestor continuation guide is missing");
  require(!role(5, ParseResultModel::HasChildrenRole).toBool() &&
              !role(6, ParseResultModel::HasParentRole).toBool() &&
              !role(4, ParseResultModel::BranchContinuesRole).toBool(),
          "tree lines must stop at frame separators");
  require(role(2, ParseResultModel::DescriptionSummaryRole) == role(2, ParseResultModel::DescriptionRole) &&
              role(2, ParseResultModel::DescriptionDetailRole).toString().isEmpty(),
          "unstructured descriptions must remain complete");
  model.toggleExpand(1);
  require(model.rowCount() == 7 && role(2, ParseResultModel::NameRole) == "同级", "collapse removed unrelated siblings");
  model.toggleExpand(1);
  require(model.rowCount() == 8 && role(2, ParseResultModel::TreeGuidesRole).toList() == QVariantList{1},
          "expand lost ancestor tree lines");
  model.toggleSubtree(0);
  require(model.rowCount() == 5, "subtree collapse crossed a frame boundary");
  model.toggleSubtree(0);
  require(model.rowCount() == 8, "subtree expand did not restore fields");
  model.clear();
  ParseResultItem item;
  item.name = "字段";
  item.value = "AA BB";
  item.description = "结果 (原始完整说明 <&>)";
  item.summary = "结果";
  item.detail = "类型 <&>";
  item.offset = 7;
  item.length = 2;
  model.addItem(item);
  require(role(0, ParseResultModel::DescriptionSummaryRole) == item.summary &&
              role(0, ParseResultModel::DescriptionDetailRole) == item.detail &&
              role(0, ParseResultModel::DescriptionRole) == item.description &&
              role(0, ParseResultModel::ValueRole) == item.value &&
              role(0, ParseResultModel::OffsetRole) == 7 && role(0, ParseResultModel::LengthRole) == 2,
          "presentation changed source data or byte locations");
  require(!role(0, ParseResultModel::HasParentRole).toBool() &&
              role(0, ParseResultModel::TreeGuidesRole).toList().isEmpty(), "clear retained old tree ancestry");
  model.clear();
  int treeRefreshes = 0;
  QObject::connect(&model, &QAbstractItemModel::dataChanged, &model,
                   [&](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
    if (roles.contains(ParseResultModel::TreeGuidesRole)) ++treeRefreshes;
  });
  add("大集合", 0);
  for (int row = 0; row < 1000; ++row) add(QString::number(row), 1);
  require(treeRefreshes == 0, "tree guides were refreshed for every appended field");
  QCoreApplication::processEvents();
  require(treeRefreshes == 1 && model.rowCount() == 1001 &&
              role(1, ParseResultModel::BranchContinuesRole).toBool() &&
              !role(1000, ParseResultModel::BranchContinuesRole).toBool(),
          "large collection tree updates were not batched correctly");
}

inline QString checkParseResultTableView(QQuickWindow *window, bool differenceMode) {
  try {
    auto require = [](bool ok, const char *message) { if (!ok) throw QString::fromUtf8(message); };
    auto *backend = window->findChild<LogBackend *>("analysisBackend");
    require(backend, "parse table backend is missing");
    std::function<QQuickItem *(QQuickItem *, const QString &)> find =
        [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
      if (parent->objectName() == name) return parent;
      for (auto *child : parent->childItems()) if (auto *found = find(child, name)) return found;
      return nullptr;
    };
    auto waitFor = [&](const std::function<bool()> &ready) {
      QElapsedTimer elapsed;
      elapsed.start();
      do {
        QCoreApplication::processEvents();
        if (auto *engine = qmlEngine(window))
          if (auto *controller = engine->incubationController()) controller->incubateFor(10);
        if (ready()) return true;
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
      } while (elapsed.elapsed() < 3000);
      return ready();
    };
    auto *bar = find(window->contentItem(), "parse-status-bar");
    auto *protocol = find(window->contentItem(), "parse-protocol-status");
    auto *status = find(window->contentItem(), "parseStatus");
    auto *address = find(window->contentItem(), "parse-address-controls");
    require(bar && protocol && status && address, "parse footer controls are missing");
    if (window->property("currentPage").toInt() > 0)
      require(status->property("text").toString() == backend->parseResult(), "footer must display the existing parse result in manual and comparison modes");
    const QSize originalSize = window->size();
    for (int width : {1200, originalSize.width()}) {
      window->resize(width, originalSize.height());
      require(waitFor([&] { return bar->width() > 0 && status->width() >= status->implicitWidth()
          && qAbs(protocol->mapToScene(QPointF(0, protocol->height() / 2)).y()
                  - address->mapToScene(QPointF(0, address->height() / 2)).y()) < 1; }),
              "protocol, parse status and address must fit one footer row");
      const auto protocolRect = protocol->mapRectToItem(bar, protocol->boundingRect());
      const auto statusRect = status->mapRectToItem(bar, status->boundingRect());
      const auto addressRect = address->mapRectToItem(bar, address->boundingRect());
      require(protocol->isVisible() && address->isVisible() && qAbs(protocolRect.left()) < 1
                  && protocolRect.right() <= statusRect.left() && statusRect.right() <= addressRect.left()
                  && qAbs(statusRect.center().y() - addressRect.center().y()) < 1
                  && qAbs(addressRect.right() - bar->width()) < 1 && bar->height() <= 40,
              "footer must place protocol then status on the left and address controls on the right");
      for (int id : {1, 2}) {
        auto *navigation = find(window->contentItem(), QString("parse-diff-navigation-%1").arg(id));
        require(navigation && navigation->isVisible() == differenceMode, "comparison header button visibility is incorrect");
        auto *previous = find(navigation, QString("parse-diff-prev-%1").arg(id));
        auto *next = find(navigation, QString("parse-diff-next-%1").arg(id));
        require(previous && next && previous->isVisible() == differenceMode && next->isVisible() == differenceMode,
                "each comparison side must have both previous and next buttons");
        if (!differenceMode) continue;
        auto *card = find(window->contentItem(), QString("result-card-%1").arg(id));
        auto *title = find(window->contentItem(), QString("result-card-title-%1").arg(id));
        require(title, "comparison result title is missing");
        const bool aligned = waitFor([&] {
          const auto buttonRect = navigation->mapRectToItem(card, navigation->boundingRect());
          const auto titleRect = title->mapRectToItem(card, QRectF(0, 0, title->width(), title->height()));
          return buttonRect.left() >= titleRect.right() && buttonRect.right() <= card->width()
                    && qAbs(buttonRect.center().y() - titleRect.center().y()) < 2
                    && previous->property("text").toString() == QString::fromUtf8("上一个差异")
                    && next->property("text").toString() == QString::fromUtf8("下一个差异")
                    && previous->x() + previous->width() <= next->x()
                    && next->x() + next->width() <= navigation->width();
        });
        if (!aligned)
          qInfo() << "UI_CHECK_DIFF_HEADER_LAYOUT" << width << id
                  << navigation->mapRectToItem(card, navigation->boundingRect())
                  << title->mapRectToItem(card, QRectF(0, 0, title->width(), title->height()))
                  << previous->x() << previous->width() << next->x() << next->width() << navigation->width()
                  << previous->property("text") << next->property("text");
        require(aligned,
                "each comparison result header must contain a complete navigation pair on the title row");
      }
    }
    if (differenceMode) {
      auto *leftTable = find(window->contentItem(), "parse-table-1");
      auto *rightTable = find(window->contentItem(), "parse-table-2");
      auto *leftList = leftTable ? leftTable->property("listView").value<QObject *>() : nullptr;
      auto *rightList = rightTable ? rightTable->property("listView").value<QObject *>() : nullptr;
      const int first = backend->findNextDiff(-1);
      require(leftList && rightList && first >= 0, "comparison fixture has no navigation target");
      for (int id : {1, 2}) {
        auto *source = id == 1 ? leftList : rightList;
        auto *other = id == 1 ? rightList : leftList;
        for (bool previous : {true, false}) {
          source->setProperty("currentIndex", previous ? first + 1 : -1);
          other->setProperty("currentIndex", previous ? -1 : first + 1);
          auto *button = find(window->contentItem(), QString(previous ? "parse-diff-prev-%1" : "parse-diff-next-%1").arg(id));
          require(QMetaObject::invokeMethod(button, "clicked") && leftList->property("currentIndex").toInt() == first
                      && rightList->property("currentIndex").toInt() == first,
                  "both buttons on each side must navigate from their own selected row and synchronize both panes");
          const int boundary = previous ? 0 : backend->resultModel()->rowCount() - 1;
          source->setProperty("currentIndex", boundary);
          other->setProperty("currentIndex", first);
          require(QMetaObject::invokeMethod(button, "clicked") && source->property("currentIndex").toInt() == boundary
                      && other->property("currentIndex").toInt() == first,
                  "navigation without a previous/next difference must preserve both selections");
        }
      }
    }
    for (int id = 1; id <= (differenceMode ? 2 : 1); ++id) {
      auto *table = find(window->contentItem(), QString("parse-table-%1").arg(id));
      auto *model = backend->parseModel(id);
      require(table && table->isVisible() && model && model->rowCount() > 20, "complex frame did not reach shared table");
      require(table->property("differenceMode").toBool() == differenceMode, "table mode is not connected to its host");
      require(QMetaObject::invokeMethod(table, "autoFit"), "shared automatic widths are unavailable");
      const int total = table->property("tableWidth").toInt();
      require(table->property("columnWidth3").toInt() >= total * 0.4, "long raw data squeezed the explanation column");
      int oadRow = -1;
      for (int row = 0; row < model->rowCount(); ++row)
        if (model->data(model->index(row), ParseResultModel::NameRole) == "OAD" &&
            !model->data(model->index(row), ParseResultModel::DescriptionDetailRole).toString().isEmpty()) { oadRow = row; break; }
      require(oadRow >= 0, "dictionary result and OAD details were not separated");
      auto *list = table->property("listView").value<QObject *>();
      require(list && waitFor([&] { return list->property("count").toInt() == model->rowCount(); }),
              "shared table did not finish loading its model");
      require(list && QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, oadRow), Q_ARG(int, 0)), "cannot reveal the selected field");
      QQuickItem *row = nullptr;
      require(waitFor([&] { row = find(table, QString("parse-%1-field-%2").arg(id).arg(oadRow)); return row != nullptr; }),
              "selected field delegate did not finish creation");
      require(row && QMetaObject::invokeMethod(row, "activate"), "field selection is unavailable");
      require(waitFor([&] { return row->height() == (differenceMode ? 26 : 48); }), "selected field height breaks detail/diff behavior");
      auto *description = find(row, row->objectName() + "-description");
      require(description && description->property("text").toString().contains("<font"), "secondary details are not styled");
      if (!differenceMode && window->property("currentPage").toInt() == 1) {
        auto *input = find(window->contentItem(), "parse-input-1");
        require(input && input->property("selectedText").toString().remove(' ') ==
                    model->data(model->index(oadRow), ParseResultModel::ValueRole).toString().remove(' '),
                "field click no longer selects the exact original bytes");
      }
    }
    return {};
  } catch (const QString &error) { return error; }
}
