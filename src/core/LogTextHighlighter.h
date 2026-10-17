#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class LogTextHighlighter : public QSyntaxHighlighter {
public:
  explicit LogTextHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {
    setObjectName(QStringLiteral("logSyntaxHighlight"));
  }
  void setDark(bool dark) { m_dark = dark; rehighlight(); }

protected:
  void highlightBlock(const QString &text) override {
    static const QRegularExpression markers(QStringLiteral("\\[(INFO|TX|RX|WARN|ERROR)\\]"));
    auto matches = markers.globalMatch(text);
    while (matches.hasNext()) {
      const auto match = matches.next();
      const QString marker = match.captured(1);
      const QColor color = marker == "TX" ? QColor(m_dark ? "#79baff" : "#1665ae")
          : marker == "RX" || marker == "WARN" ? QColor(m_dark ? "#e8be7d" : "#825308")
          : marker == "ERROR" ? QColor(m_dark ? "#ff9c92" : "#b7282c")
          : QColor(m_dark ? "#75d2b8" : "#16765c");
      setFormat(match.capturedStart(), match.capturedLength(), color);
    }
  }

private:
  bool m_dark = true;
};
