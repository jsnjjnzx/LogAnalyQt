#include "SshTerminal.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMutexLocker>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <functional>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool ok, const QString &message) {
  ++checks;
  if (!ok) throw std::runtime_error(message.toStdString());
}
bool waitFor(const std::function<bool()> &predicate, int milliseconds = 8000) {
  QElapsedTimer timer; timer.start();
  while (!predicate() && timer.elapsed() < milliseconds) {
    QCoreApplication::processEvents(); QThread::msleep(2);
  }
  return predicate();
}
void settle(int milliseconds) {
  QElapsedTimer timer; timer.start();
  while (timer.elapsed() < milliseconds) { QCoreApplication::processEvents(); QThread::msleep(2); }
}
class MemoryCredentials final : public SecureCredentialStore {
public:
  MemoryCredentials() : SecureCredentialStore(Domain::Ssh) {}
  bool isAvailable() const override { return true; }
  bool contains(const QString &id, bool *exists, QString *) const override {
    QMutexLocker lock(&mutex); *exists = values.contains(id); return true;
  }
  bool read(const QString &id, QByteArray *value, QString *) const override {
    QMutexLocker lock(&mutex); *value = values.value(id); return true;
  }
  bool write(const QString &id, const QByteArray &value, QString *) override {
    QMutexLocker lock(&mutex); values[id] = value; return true;
  }
  bool remove(const QString &id, QString *) override {
    QMutexLocker lock(&mutex); values.remove(id); return true;
  }
private:
  mutable QMutex mutex;
  QMap<QString,QByteArray> values;
};
int authCount(const QString &path) {
  QFile file(path); check(file.open(QIODevice::ReadOnly), "read isolated server counters");
  return QJsonDocument::fromJson(file.readAll()).object()["authAttempts"].toInt();
}
void configure(SshTerminal &terminal, int port, const QString &host = "127.0.0.1") {
  terminal.setUseSystemCredentials(true);
  terminal.setHost(host); terminal.setPort(port); terminal.setUsername("loganalyqt-test");
  terminal.setEncoding("UTF-8"); terminal.setAutoReconnectEnabled(false);
  check(waitFor([&]{return !terminal.credentialBusy();}), "isolated credential refresh completes");
  check(terminal.credentialError().isEmpty(), "isolated credential state: " + terminal.credentialError());
  terminal.setPassword("synthetic-loopback-password");
}
void echo(SshTerminal &terminal, const QString &text) {
  terminal.sendCommand("echo " + text);
  check(waitFor([&]{return terminal.getBufferText().contains("TEST_ECHO:" + text);}),
        "encrypted PTY/shell echo returned: " + text + " / " + terminal.statusMessage());
}
void reconnectNoticeChecks() {
  QTemporaryDir directory;
  SshTerminal terminal(std::make_shared<MemoryCredentials>(), directory.path());
  check(QMetaObject::invokeMethod(&terminal, "onWorkerConnected"), "initial connection callback");
  check(terminal.getBufferText().isEmpty(), "initial success does not add a reconnection notice");
  const QString failure = QStringLiteral("连接 SSH 服务器失败，请检查网络和端口");
  check(QMetaObject::invokeMethod(&terminal, "onWorkerError", Q_ARG(QString, failure)), "failed attempt callback");
  check(terminal.reconnectAttempt() > 0 && terminal.getBufferText() == failure,
        "failed attempt retains its error without prematurely reporting success");
  const auto before = QDateTime::currentDateTime().addSecs(-1);
  check(QMetaObject::invokeMethod(&terminal, "onWorkerConnected"), "reconnection success callback");
  const auto lines = terminal.lineModel()->lines();
  check(lines.size() == 2 && lines.first() == failure && lines.last().endsWith(
            QStringLiteral("] SSH 已重新连接成功")), "recovery notice is appended after the retained failure");
  const auto timestamp = QDateTime::fromString(lines.last().mid(1, 19), "yyyy-MM-dd HH:mm:ss");
  check(timestamp.isValid() && timestamp >= before && timestamp <= QDateTime::currentDateTime(),
        "recovery notice contains the current local date and time");
  check(terminal.isConnected() && terminal.lastError().isEmpty() && terminal.reconnectAttempt() == 0,
        "recovery retains normal connected state and clears the current error");
  check(QMetaObject::invokeMethod(&terminal, "onWorkerConnected") && terminal.currentLineCount() == 2,
        "a repeated success callback does not duplicate the recovery notice");
}
}
int main(int argc,char **argv) {
  QCoreApplication app(argc,argv);
  if (argc != 5) { QTextStream(stderr) << "usage: ssh_success_session_tests port stall-port expected-fingerprint stats-json\n"; return 2; }
  try {
    const int port = QString::fromLocal8Bit(argv[1]).toInt();
    const int stallPort = QString::fromLocal8Bit(argv[2]).toInt();
    const QString fingerprint = QString::fromLocal8Bit(argv[3]);
    const QString stats = QString::fromLocal8Bit(argv[4]);
    check(port > 0 && port <= 65535 && stallPort > 0 && stallPort <= 65535, "loopback random ports");
    reconnectNoticeChecks();
    QTemporaryDir directory; check(directory.isValid(), "isolated application data root");
    auto credentials = std::make_shared<MemoryCredentials>();
    SshTerminal terminal(credentials, directory.path());
    configure(terminal,port);
    QString request, observedFingerprint; int confirmations = 0, connectedCount = 0;
    QObject::connect(&terminal,&SshTerminal::connectedChanged,[&]{if(terminal.isConnected())++connectedCount;});
    QObject::connect(&terminal,&SshTerminal::hostKeyConfirmationRequested,
      [&](const QString &host,int targetPort,const QString &algorithm,const QString &value,const QString &id){
        check(host == "127.0.0.1" && targetPort == port && !algorithm.isEmpty(), "confirmation names the isolated endpoint and algorithm");
        request=id; observedFingerprint=value; ++confirmations;
      });
    terminal.connectToHost();
    check(waitFor([&]{return terminal.hostKeyPending();}), "first host key confirmation: " + terminal.statusMessage());
    check(!terminal.isConnected() && !request.isEmpty() && observedFingerprint == fingerprint, "no connection before matching independent fingerprint acceptance");
    settle(100);
    check(authCount(stats) == 0, "no password authentication attempt before first host-key decision");
    terminal.confirmHostKey(request, observedFingerprint, true);
    check(waitFor([&]{return terminal.isConnected();}), "first full authenticated session: " + terminal.statusMessage());
    echo(terminal,"LOOPBACK_FIRST"); echo(terminal,QString::fromUtf8("中文回显"));
    terminal.disconnectFromHost();
    check(waitFor([&]{return terminal.connectionState()==SshTerminal::Disconnected;}), "explicit disconnect");
    check(!terminal.isConnected() && !terminal.hostKeyPending(), "disconnect invalidates session and trust wait");
    QString savedOperation; bool saveOk = false;
    QObject::connect(&terminal,&SshTerminal::passwordOperationFinished,
      [&](const QString &id,bool ok,const QString &){savedOperation=id;saveOk=ok;});
    const auto operation=terminal.savePassword("synthetic-loopback-password");
    check(!operation.isEmpty() && waitFor([&]{return savedOperation==operation;} ) && saveOk, "password stored only in injected memory store");
    terminal.setPassword(""); terminal.connectToHost();
    check(waitFor([&]{return terminal.isConnected();}), "reconnect with pinned key and memory credential: " + terminal.statusMessage());
    check(confirmations==1, "same-key reconnect does not ask TOFU again"); echo(terminal,"LOOPBACK_SECOND");
    terminal.setAutoReconnectEnabled(true); terminal.setReconnectMaxAttempts(2); terminal.setReconnectIntervalMs(500);
    terminal.sendCommand("__DROP_TEST_SESSION__");
    check(waitFor([&]{return connectedCount>=3 && terminal.isConnected();}), "automatic reconnect after isolated peer closes");
    echo(terminal,"AUTO_RECONNECTED"); check(confirmations==1, "automatic reconnect retains original pin");
    check(terminal.getBufferText().count(QStringLiteral("] SSH 已重新连接成功")) == 1
              && terminal.getBufferText().contains("TEST_ECHO:LOOPBACK_FIRST"),
          "real automatic reconnect adds one recovery notice and preserves earlier history");
    terminal.sendCommand("__DROP_TEST_SESSION__");
    check(waitFor([&]{return terminal.connectionState()==SshTerminal::Reconnecting;}), "reconnect timer entered");
    terminal.disconnectFromHost(); settle(1000);
    check(!terminal.isConnected() && terminal.connectionState()==SshTerminal::Disconnected, "manual cancellation stops queued automatic reconnect");
    check(authCount(stats)==3, "cancelled reconnect made no extra password authentication");
    {
      QTemporaryDir cancelRoot; SshTerminal cancelled(credentials,cancelRoot.path()); configure(cancelled,port);
      QString cancelId, cancelFingerprint;
      QObject::connect(&cancelled,&SshTerminal::hostKeyConfirmationRequested,
        [&](const QString &,int,const QString &,const QString &value,const QString &id){cancelId=id;cancelFingerprint=value;});
      cancelled.connectToHost(); check(waitFor([&]{return cancelled.hostKeyPending();}), "second isolated trust root waits for confirmation");
      cancelled.disconnectFromHost(); cancelled.confirmHostKey(cancelId,cancelFingerprint,true); settle(300);
      check(!cancelled.isConnected() && !cancelled.hostKeyPending(), "late acceptance after cancellation does not connect");
      check(!QFile::exists(cancelRoot.filePath("ssh-known-hosts.json")), "late decision did not create trust record");
      check(authCount(stats)==3, "cancelled host-key wait did not authenticate");
    }
    {
      QTemporaryDir stallRoot; SshTerminal stalled(credentials,stallRoot.path()); configure(stalled,stallPort);
      stalled.connectToHost(); settle(50); QElapsedTimer cancelTime;cancelTime.start();stalled.disconnectFromHost();
      check(cancelTime.elapsed()<100, "cancel returns to main loop without waiting for remote SSH banner");settle(400);
      check(!stalled.isConnected() && !stalled.hostKeyPending(), "cancelled stalled handshake cannot later connect");
    }
    {
      QTemporaryDir hostRoot; SshTerminal named(credentials, hostRoot.path()); configure(named, port, "localhost");
      QObject::connect(&named, &SshTerminal::hostKeyConfirmationRequested,
        [&](const QString &host, int targetPort, const QString &, const QString &value, const QString &id) {
          check(host == "localhost" && targetPort == port && value == fingerprint, "hostname lookup preserves the requested trust identity");
          named.confirmHostKey(id, value, true);
        });
      named.connectToHost();
      check(waitFor([&] { return named.isConnected(); }), "hostnames still resolve and establish an authenticated session: " + named.statusMessage());
      echo(named, "HOSTNAME_RESOLVED");
      named.disconnectFromHost();
      check(authCount(stats) == 4, "IP and hostname paths authenticate only their intended sessions");
    }
    QFile config(directory.filePath("settings.json"));
    if(config.open(QIODevice::ReadOnly))check(!config.readAll().contains("synthetic-loopback-password"), "synthetic password not written to portable INI");
    QTextStream(stdout)<<"SSH loopback success checks: "<<checks<<", failures: 0; authenticated sessions: "<<connectedCount<<"\n";
    return 0;
  } catch(const std::exception &error) {
    QTextStream(stderr)<<"FAIL: "<<error.what()<<"\n"; return 1;
  }
}
