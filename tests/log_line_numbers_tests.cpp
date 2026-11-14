#include "core/LogLineNumbers.h"

#include <QtTest>

class LogLineNumbersTests : public QObject {
  Q_OBJECT
private slots:
  void longLogTail() {
    QTextDocument document;
    document.setPlainText(QStringLiteral("TX 报文\r\n").repeated(920) + "\r\n末行");
    const auto start = document.findBlockByNumber(919);
    const auto rows = visibleLogLineNumbers(document, start.position(), document.size().height());
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows.front().toMap().value("number").toInt(), 920);
    QCOMPARE(rows.at(1).toMap().value("number").toInt(), 921);
    QCOMPARE(rows.back().toMap().value("number").toInt(), 922);
  }

  void wrappedContinuationDoesNotConsumeNumbers() {
    QTextDocument document;
    document.setTextWidth(100);
    document.setPlainText(QString(160, 'A') + "\nNext");
    const auto second = document.findBlockByNumber(1);
    const auto *layout = document.documentLayout();
    const qreal secondTop = layout->blockBoundingRect(second).top();
    QVERIFY(document.firstBlock().layout()->lineCount() > 1);
    // The viewport may begin inside a continuation line.
    const int position = layout->hitTest(QPointF(5, secondTop / 2), Qt::FuzzyHit);
    QVERIFY(position > 0 && position < second.position());
    QCOMPARE(visibleLogLineNumbers(document, position, secondTop).size(), 1);
    const auto rows = visibleLogLineNumbers(document, position, document.size().height());
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.back().toMap().value("number").toInt(), 2);
    QCOMPARE(rows.back().toMap().value("baseline").toDouble(),
             secondTop + second.layout()->lineAt(0).ascent());
  }
};

QTEST_MAIN(LogLineNumbersTests)
#include "log_line_numbers_tests.moc"
