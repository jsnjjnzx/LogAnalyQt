#pragma once

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QVariant>

inline QVariantList visibleLogLineNumbers(const QTextDocument &document, int position,
                                         qreal bottom) {
  QVariantList rows;
  if (document.isEmpty()) return rows;
  const auto *layout = document.documentLayout();
  for (auto block = document.findBlock(position); block.isValid(); block = block.next()) {
    const qreal top = layout->blockBoundingRect(block).top();
    if (top >= bottom) break;
    if (!block.layout()->lineCount()) continue;
    const auto first = block.layout()->lineAt(0);
    rows.append(QVariantMap{{"number", block.blockNumber() + 1},
                           {"baseline", top + first.y() + first.ascent()}});
  }
  return rows;
}
