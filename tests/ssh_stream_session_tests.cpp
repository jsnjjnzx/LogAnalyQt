#include "SshTerminal.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QTextStream>
#include <QThread>
#include <functional>
#include <stdexcept>

struct SshTerminalTestAccess {
  static qsizetype queuedBytes(const SshTerminal &terminal) {
    QMutexLocker lock(&terminal.m_buffer->mutex);
    return terminal.m_buffer->bytes;
  }
};
namespace {
int checks = 0;
void check(bool condition, const char *message) {
  ++checks; if (!condition) throw std::runtime_error(message);
}
bool waitFor(const std::function<bool()> &condition, int ms = 5000) {
  QElapsedTimer timer; timer.start();
  while (!condition() && timer.elapsed() < ms) { QCoreApplication::processEvents(); QThread::msleep(2); }
  return condition();
}
void settle(int ms) {
  QElapsedTimer timer; timer.start();
  while (timer.elapsed() < ms) { QCoreApplication::processEvents(); QThread::msleep(2); }
}
class MemoryCredentials : public SecureCredentialStore {
public:
  MemoryCredentials() : SecureCredentialStore(Domain::Ssh) {}
  bool contains(const QString &, bool *exists, QString *) const override { *exists = false; return true; }
};
}
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  if (argc != 4) return 2;
  try {
    const int port = QString::fromLocal8Bit(argv[1]).toInt();
    const QString fingerprint = QString::fromLocal8Bit(argv[2]);
    SshTerminal terminal(std::make_shared<MemoryCredentials>(), QString::fromLocal8Bit(argv[3]));
    terminal.setHost("127.0.0.1"); terminal.setPort(port); terminal.setUsername("loganalyqt-test");
    terminal.setPassword("synthetic-loopback-password"); terminal.setAutoReconnectEnabled(false);
    check(waitFor([&] { return !terminal.credentialBusy(); }), "isolated credentials ready");
    QObject::connect(&terminal, &SshTerminal::hostKeyConfirmationRequested, &app,
      [&](const QString &, int, const QString &, const QString &actual, const QString &id) {
        check(actual == fingerprint, "independent fingerprint match"); terminal.confirmHostKey(id, actual, true);
      });
    terminal.connectToHost();
    check(waitFor([&] { return terminal.isConnected(); }), "authenticated session");
    check(waitFor([&] { return terminal.getBufferText().contains("TEST_READY"); }), "ready output");
    terminal.sendCommand("TERMINAL");
    check(waitFor([&] { return terminal.liveLine() == "test$ "; }), "remote prompt appears before newline");
    const int historyRows = terminal.currentLineCount();
    terminal.sendRawBytes("x");
    check(waitFor([&] { return terminal.liveLine() == "test$ x"; }), "individual keystroke receives live remote echo");
    check(terminal.currentLineCount() == historyRows, "live echo does not manufacture completed log rows");
    terminal.sendRawBytes(QString(QChar(127)));
    check(waitFor([&] { return terminal.liveLine() == "test$  " && terminal.cursorColumn() == 6; }), "remote backspace redraw is applied");
    terminal.sendRawBytes("\x1b[A");
    check(waitFor([&] { return terminal.liveLine() == "test$ history"; }), "arrow key is passed unchanged and remote history redraw is applied");
    terminal.sendRawBytes("\t");
    check(waitFor([&] { return terminal.liveLine() == "test$ history-completed"; }), "raw Tab displays remote completion directly");
    terminal.sendRawBytes(QString::fromUtf8("中文"));
    check(waitFor([&] { return terminal.liveLine().endsWith(QString::fromUtf8("中文")); }), "Chinese commit round-trips through encrypted SSH before newline");
    terminal.sendRawBytes(QString(QChar(3)));
    check(waitFor([&] { return terminal.liveLine() == "test$ " && terminal.getBufferText().contains("^C"); }), "raw interrupt receives the remote prompt");
    terminal.sendRawBytes("\r");
    check(waitFor([&] { return terminal.getBufferText().contains("RAW_DONE"); }), "raw Return reaches the remote peer");
    terminal.sendCommand("SPLIT");
    check(waitFor([&] { return terminal.getBufferText().contains("END"); }), "fragmented stream completed");
    check(terminal.getBufferText().contains(QString::fromUtf8("分块中文😀\nRED\nEND")), "fragmented UTF-8/ANSI/CRLF content preserved");
    check(!terminal.getBufferText().contains(QChar::ReplacementCharacter), "no UTF-8 replacement corruption");
    terminal.sendCommand("GB");
    check(waitFor([&] { return terminal.getBufferText().contains("GB_READY"); }), "GB peer ready");
    terminal.setEncoding("GB2312"); terminal.sendCommand(QString::fromUtf8("echo 编码回显"));
    check(waitFor([&] { return terminal.getBufferText().contains(QString::fromUtf8("编码回显")); }), "GB receive and command encoding");
    terminal.sendCommand("UTF8");
    check(waitFor([&] { return terminal.getBufferText().contains("UTF8_READY"); }), "UTF-8 peer ready");
    terminal.setEncoding("UTF-8");

    {
      const QVariantList rules{
          QVariantMap{{"id", 1}, {"name", "loopback error"}, {"pattern", "MONITOR_(HIT|NEW)"}, {"ignoreCase", false}, {"color", "#f79a93"}},
          QVariantMap{{"id", 2}, {"name", "loopback offline"}, {"pattern", "deviceoffline"}, {"ignoreCase", true}, {"color", "#80b9ff"}},
          QVariantMap{{"id", 3}, {"name", "loopback mark"}, {"pattern", "MONITOR_MARK"}, {"ignoreCase", false}, {"color", "#7ed6a0"}, {"pauseOnMatch", false}}};
      int hits = 0, marks = 0; QVariantMap hit;
      QObject callbacks;
      QObject::connect(&terminal, &SshTerminal::monitorMatched, &callbacks, [&](const QVariantMap &value) {
        if (!value.value("pauseOnMatch", true).toBool()) { ++marks; return; }
        ++hits; hit = value;
        terminal.setMonitorRules(rules, false);
      });
      terminal.sendCommand("echo MONITOR_HIT_OLD");
      check(waitFor([&] { return terminal.getBufferText().contains("MONITOR_HIT_OLD"); }), "old matching log received before arming");
      check(terminal.setMonitorRules(rules, true), "real receiver accepts two selected rules");
      terminal.setPresentationActive(false);
      terminal.sendCommand("echo MONITOR_MARK_A"); terminal.sendCommand("echo MONITOR_MARK_B");
      check(waitFor([&] { return terminal.getBufferText().contains("MONITOR_MARK_B") && marks > 0; }) && hits == 0,
            "real SSH mark-only rule keeps monitoring subsequent output");
      int colored = 0;
      for (int row = 0; row < terminal.currentLineCount(); ++row)
        if (terminal.lineModel()->lineAt(row).contains("MONITOR_MARK")
            && terminal.lineModel()->data(terminal.lineModel()->index(row), TerminalLineModel::MonitorColorRole).toString() == "#7ed6a0") ++colored;
      check(colored == 2, "all mark-only matching SSH rows retain their configured color");
      terminal.sendCommand("echo NORMAL_AFTER_ARM");
      check(waitFor([&] { return terminal.getBufferText().contains("NORMAL_AFTER_ARM"); }) && hits == 0,
            "hidden monitor checks only new output, not a historical match");
      terminal.sendCommand("echo MONITOR_HIT_FIRST");
      check(waitFor([&] { return hits == 1; }), "real encrypted SSH output triggers a background monitor match");
      check(hit.value("color").toString() == "#f79a93" && hit.value("lineId").toLongLong() >= terminal.firstLineId(),
            "background hit retains configured color and stable line identity");
      terminal.sendCommand("echo MONITOR_HIT_WHILE_PAUSED");
      terminal.sendCommand("echo RECEIVING_WHILE_PAUSED");
      check(waitFor([&] { return terminal.getBufferText().contains("RECEIVING_WHILE_PAUSED"); }) && hits == 1 && terminal.isConnected(),
            "latched pause does not stop SSH reception or repeatedly trigger");
      terminal.setMonitorRules(rules, true);
      terminal.sendCommand("echo NORMAL_AFTER_RESUME");
      check(waitFor([&] { return terminal.getBufferText().contains("NORMAL_AFTER_RESUME"); }) && hits == 1,
            "resume does not trigger on logs received while paused");
      terminal.sendCommand("echo MONITOR_NEW_SECOND");
      check(waitFor([&] { return hits == 2; }), "resume detects newly arriving matching output");
      terminal.setMonitorRules(rules, true);
      terminal.sendCommand("echo deviceOFFLINE");
      check(waitFor([&] { return hits == 3; }) && hit.value("ruleId").toInt() == 2 && hit.value("color").toString() == "#80b9ff",
            "any selected expression can pause and carries its own color");
      terminal.setMonitorRules({}, false); terminal.setPresentationActive(true);
    }

    {
      terminal.sendCommand("PAUSE_STREAM");
      check(waitFor([&] { return terminal.getBufferText().contains("PAUSE_STREAM_READY"); }), "remote stream ready to test reception pause");
      terminal.setReceptionPaused(true);
      check(waitFor([&] { return terminal.receptionPaused(); }), "worker has stopped reading SSH data");
      const auto received = terminal.getBufferText();
      const int rows = terminal.currentLineCount();
      terminal.sendCommand("echo MUST_NOT_QUEUE_WHILE_STOPPED");
      settle(1300);
      check(terminal.isConnected() && terminal.getBufferText() == received && terminal.currentLineCount() == rows
                && SshTerminalTestAccess::queuedBytes(terminal) == 0,
            "stopped reception reads neither stdout nor stderr into local history or pending cache");
      terminal.setReceptionPaused(false);
      check(waitFor([&] { return !terminal.receptionPaused() && terminal.getBufferText().contains("PAUSED_STDOUT")
                          && terminal.getBufferText().contains("PAUSED_STDERR"); }),
            "explicit resume reads data waiting on the remote/system side");
      settle(100);
      check(!terminal.getBufferText().contains("MUST_NOT_QUEUE_WHILE_STOPPED"), "terminal commands entered while stopped are not silently deferred");
    }
    QString completion; QStringList candidates; int completions = 0;
    QObject completionCallbacks;
    QObject::connect(&terminal, &SshTerminal::tabCompletionReady, &completionCallbacks,
      [&](const QString &text, const QStringList &values) { completion = text; candidates = values; ++completions; });
    terminal.sendRawBytes(QString(QChar(0x15))); terminal.sendTabCompletion("tai");
    check(waitFor([&] { return completions == 1; }), "unique completion returns");
    check(completion == "tail" && candidates.isEmpty(), "unique completion content");
    terminal.sendRawBytes(QString(QChar(0x15))); terminal.sendTabCompletion("lo");
    check(waitFor([&] { return completions == 2; }), "second-tab candidates return");
    check(completion.isEmpty() && candidates == QStringList{"lock/", "log/"}, "candidate list preserved");
    terminal.sendRawBytes(QString(QChar(0x15))); terminal.sendTabCompletion("zz");
    check(waitFor([&] { return completions == 3; }), "completion timeout returns");
    check(completion.isEmpty() && candidates.isEmpty(), "no-match completion stays empty");
    terminal.sendCommand("echo AFTER_TAB");
    check(waitFor([&] { return terminal.getBufferText().contains("AFTER_TAB"); }), "normal output resumes after completion");
    terminal.sendRawBytes(QString(QChar(0x15))); terminal.sendTabCompletion("noisy");
    check(waitFor([&] { return completions == 4 && terminal.getBufferText().contains("NOISY_END"); }),
          "oversized completion falls back to bounded ordinary output");
    terminal.clearBuffer(); terminal.setMaxBufferLines(200000);
    const auto beforeBlank = terminal.firstLineId();
    terminal.sendRawBytes(QString(QChar(0x15))); terminal.sendTabCompletion("blank");
    QThread::msleep(1000);
    check(SshTerminalTestAccess::queuedBytes(terminal) <= SshLogStream::PendingBytes,
          "completion fallback respects queue budget even for one-byte empty records");
    check(waitFor([&] { return terminal.lineAt(terminal.currentLineCount() - 1) == "BLANK_END"; }, 10000), "blank completion output drains");
    check(terminal.firstLineId() + terminal.currentLineCount() - beforeBlank == 307201, "completion fallback loses no empty records");
    terminal.setMaxBufferLines(10000);

    terminal.clearBuffer();
    const QString largeCommand(200000, 'q');
    terminal.sendCommand("echo " + largeCommand);
    check(waitFor([&] { return terminal.lineIsComplete(0) && terminal.lineAt(0) == largeCommand; }),
          "partial nonblocking writes finish without waiting for unsolicited remote output");
    terminal.setPresentationActive(false); terminal.sendCommand("echo BACKGROUND_OUTPUT");
    check(waitFor([&] { return terminal.getBufferText().contains("BACKGROUND_OUTPUT"); }), "hidden presentation keeps receiving");
    terminal.setPresentationActive(true); terminal.sendCommand("echo FOREGROUND_OUTPUT");
    check(waitFor([&] { return terminal.getBufferText().contains("FOREGROUND_OUTPUT"); }), "foreground presentation resumes normally");

    terminal.clearBuffer(); terminal.sendCommand("LONG");
    check(waitFor([&] { return terminal.getBufferText().contains("LONG_END"); }), "long output completed");
    check(terminal.lineAt(1) == QString(40000, 'x') + " 68 04 07 00 00 00", "continued row maps to full logical line");
    check(terminal.lineIsComplete(1), "complete continued log can be parsed");
    terminal.copyLineToClipboard(1);
    check(terminal.clipboardText() == terminal.lineAt(1), "copy continued log is exact");
    terminal.copyBufferToClipboard();
    check(terminal.clipboardText() == terminal.getBufferText(), "copy buffer retains logical newlines");
    terminal.sendCommand("PARTIAL");
    check(waitFor([&] { return terminal.getBufferText().contains("OLD_PARTIAL"); }), "partial tail received");
    terminal.clearBuffer(); terminal.sendCommand("AFTER_CLEAR");
    check(waitFor([&] { return terminal.getBufferText().contains("NEW_AFTER_CLEAR"); }), "new output after clear");
    check(!terminal.getBufferText().contains("OLD_PARTIAL") && !terminal.lineIsComplete(0),
          "clear cannot resurrect old bytes and marks a newly received continuation as incomplete");
    terminal.sendCommand("STDERR");
    check(waitFor([&] { const auto text = terminal.getBufferText(); return text.contains("STDERR_CONTENT") && text.contains("STDOUT_CONTENT"); }), "both SSH streams are drained");

    terminal.clearBuffer(); terminal.setMaxBufferLines(200000);
    qint64 burstLast = -1, burstErrors = 0;
    QObject burstCallbacks;
    const auto burstObserver = QObject::connect(&terminal, &SshTerminal::lineCountChanged, &burstCallbacks, [&] {
      std::vector<QString> fresh;
      for (int row = terminal.currentLineCount() - 1; row >= 0; --row) {
        const auto line = terminal.lineAt(row);
        if (!line.startsWith("B:")) continue;
        if (QStringView(line).mid(2, 8).toLongLong() <= burstLast) break;
        fresh.push_back(line);
      }
      for (auto it = fresh.rbegin(); it != fresh.rend(); ++it) {
        const auto number = QStringView(*it).mid(2, 8).toLongLong();
        if (number != burstLast + 1 || *it != QString("B:%1:").arg(number, 8, 10, QChar('0')) + QString(116, 'b')) ++burstErrors;
        burstLast = number;
      }
    });
    terminal.sendCommand("BURST");
    QThread::msleep(1500); // Deliberately stall the consumer; the worker must bound its queue and pause reading.
    const auto queued = SshTerminalTestAccess::queuedBytes(terminal);
    check(queued > 0 && queued <= SshLogStream::PendingBytes, "backpressure bounds the decoded queue while GUI is blocked");
    const bool drained = waitFor([&] { return burstLast == 65535 && terminal.lineAt(terminal.currentLineCount() - 1) == "BURST_END"; }, 20000);
    if (!drained) QTextStream(stderr) << "burst summary rows=" << terminal.currentLineCount()
      << " history_bytes=" << terminal.lineModel()->byteSize() << " pending_bytes=" << SshTerminalTestAccess::queuedBytes(terminal)
      << " connected=" << terminal.isConnected() << " error=" << terminal.lastError() << '\n';
    check(drained, "all burst records drain after GUI resumes");
    check(burstErrors == 0, "every burst record verified before normal history eviction");
    QObject::disconnect(burstObserver);
    check(terminal.lineModel()->byteSize() <= SshLogStream::HistoryBytes, "history byte budget holds");
    const auto lastId = terminal.lineIdAt(terminal.currentLineCount() - 2);
    terminal.setMaxBufferLines(100);
    check(terminal.currentLineCount() == 100 && terminal.indexForLineId(lastId) == 98, "capacity change preserves remaining identities");
    terminal.clearBuffer(); terminal.sendCommand("EOF");
    check(waitFor([&] { return !terminal.isConnected(); }), "clean EOF disconnects");
    check(terminal.getBufferText() == "FINAL_WITHOUT_NEWLINE", "EOF flushes the unterminated tail once");
    terminal.connectToHost();
    check(waitFor([&] { return terminal.isConnected(); }), "reconnect after EOF");
    terminal.clearBuffer(); terminal.sendCommand("echo NEW_SESSION");
    check(waitFor([&] { return terminal.getBufferText().contains("NEW_SESSION"); }), "new session output arrives");
    check(!terminal.getBufferText().contains("FINAL_WITHOUT_NEWLINE"), "old session batch cannot reappear");
    terminal.disconnectFromHost(); settle(50);
    QTextStream(stdout) << "PASS: " << checks << " encrypted stream/completion/clipboard/backpressure assertions; payloads stayed in RAM\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1;
  }
}
