#pragma once

#include <QQuickTextDocument>
#include <QPointer>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QVariantList>

class SshTextColors : public QSyntaxHighlighter {
  Q_OBJECT
  Q_PROPERTY(QQuickTextDocument *source READ source WRITE setSource)
  Q_PROPERTY(QVariantList runs READ runs WRITE setRuns)

public:
  explicit SshTextColors(QObject *parent = nullptr) : QSyntaxHighlighter(parent) {}
  QQuickTextDocument *source() const { return m_source; }
  void setSource(QQuickTextDocument *source) {
    m_source = source;
    setDocument(source ? source->textDocument() : nullptr);
  }
  QVariantList runs() const { return m_runs; }
  void setRuns(const QVariantList &runs) {
    if (m_runs == runs) return;
    m_runs = runs;
    rehighlight();
  }

  Q_INVOKABLE QVariantList wordAt(int position) const {
    if (!document() || position < 0 || position >= document()->characterCount() - 1 ||
        document()->characterAt(position).isSpace()) return {};
    QTextCursor cursor(document());
    cursor.setPosition(position);
    cursor.select(QTextCursor::WordUnderCursor);
    return cursor.hasSelection() ? QVariantList{cursor.selectionStart(), cursor.selectionEnd()} : QVariantList{};
  }

protected:
  void highlightBlock(const QString &text) override {
    const int offset = currentBlock().position();
    for (const auto &entry : m_runs) {
      const auto run = entry.toMap();
      const int start = run.value("start").toInt() - offset;
      const int end = qMin(start + run.value("length").toInt(), int(text.size()));
      if (end <= qMax(0, start)) continue;
      QTextCharFormat style;
      if (const auto fg = run.value("fg").toUInt()) style.setForeground(QColor::fromRgb(fg));
      if (const auto bg = run.value("bg").toUInt()) style.setBackground(QColor::fromRgb(bg));
      setFormat(qMax(0, start), end - qMax(0, start), style);
    }
  }

private:
  QPointer<QQuickTextDocument> m_source;
  QVariantList m_runs;
};
