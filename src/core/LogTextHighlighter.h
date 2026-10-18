#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class LogTextHighlighter : public QSyntaxHighlighter {
public:
  explicit LogTextHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {
    setObjectName(QStringLiteral("logSyntaxHighlight"));
  }

protected:
  void highlightBlock(const QString &text) override {
    static const QRegularExpression markers(QStringLiteral("\\[(INFO|TX|RX|WARN|ERROR)\\]"));
    auto matches = markers.globalMatch(text);
    while (matches.hasNext()) {
      const auto match = matches.next();
      const QString marker = match.captured(1);
      const QColor color = marker == "TX" ? QColor("#79baff")
          : marker == "RX" || marker == "WARN" ? QColor("#e8be7d")
          : marker == "ERROR" ? QColor("#ff9c92")
          : QColor("#75d2b8");
      setFormat(match.capturedStart(), match.capturedLength(), color);
    }
  }

};
