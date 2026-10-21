#include "SshTerminal.h"
#include "SshHostKeyStore.h"
#include "common/JsonSettings.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QProcess>
#include <QJsonArray>
#include <functional>
#include <stdexcept>
#include <atomic>
#include <thread>

static void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
static bool waitFor(const std::function<bool()> &predicate, int ms = 3000) {
  QElapsedTimer timer; timer.start();
  while (!predicate() && timer.elapsed() < ms) {
    QCoreApplication::processEvents(); QThread::msleep(2);
  }
  return predicate();
}
class FakeSshCredentials : public SecureCredentialStore {
public:
  FakeSshCredentials() : SecureCredentialStore(Domain::Ssh) {}
  bool isAvailable() const override { return true; }
  bool contains(const QString &id, bool *exists, QString *) const override {
    ++containsCalls;
    if (silentContainsFailure) return false;
    *exists = values.contains(id); return true;
  }
  bool read(const QString &id, QByteArray *value, QString *error) const override {
    if (readFailure) { if (error) *error = "synthetic read failure"; return false; }
    if (blockRead.load()) {
      enteredRead.store(true);
      while (!releaseRead.load()) QThread::msleep(2);
    }
    *value = values.value(id); return true;
  }
  bool write(const QString &id, const QByteArray &value, QString *error) override {
    if (writeFailure) { if (error) *error = "synthetic write failure"; return false; }
    values[id] = value; return true;
  }
  bool remove(const QString &id, QString *) override {
    if (silentRemoveFailure) return false;
    values.remove(id); return true;
  }
  QMap<QString, QByteArray> values;
  bool writeFailure = false;
  bool readFailure = false;
  bool silentRemoveFailure = false;
  bool silentContainsFailure = false;
  mutable std::atomic<bool> blockRead{false}, enteredRead{false}, releaseRead{false};
  mutable std::atomic<int> containsCalls{0};
};
static QString profile(const QString &host, int port, const QString &user) {
  return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(QJsonArray{
    host.trimmed().toLower(), port, user}).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
static void trustChecks() {
  QTemporaryDir dir; check(dir.isValid(), "temporary directory");
  SshHostKeyStore store(dir.filePath("knownhosts.json"));
  QString error;
  check(store.check("HOST", 2222, "one") == SshHostKeyStore::Unknown, "unknown");
  check(store.trust("HOST", 2222, "one", &error), "trust first key");
  check(store.check("host", 2222, "one") == SshHostKeyStore::Trusted, "canonical host");
  check(store.check("host", 22, "one") == SshHostKeyStore::Unknown, "port isolation");
  check(store.check("host", 2222, "two") == SshHostKeyStore::Changed, "rotation blocked");
  check(!store.trust("host", 2222, "two", &error), "no silent replacement");
  check(store.check("host", 2222, "one") == SshHostKeyStore::Trusted, "original retained");
  QLockFile held(dir.filePath("knownhosts.json.lock"));
  check(held.tryLock(0), "hold concurrent trust writer lock");
  bool trusted = false;
  std::thread writer([&] {
    SshHostKeyStore concurrent(dir.filePath("knownhosts.json"));
    trusted = concurrent.trust("second-host", 22, "second-key");
  });
  QThread::msleep(80); held.unlock(); writer.join();
  check(trusted && store.check("second-host", 22, "second-key") == SshHostKeyStore::Trusted
            && store.check("host", 2222, "one") == SshHostKeyStore::Trusted, "concurrent trust writes wait and preserve both endpoints");
  QFile file(dir.filePath("knownhosts.json")); check(file.open(QIODevice::WriteOnly), "corrupt fixture");
  file.write("broken"); file.close();
  check(store.check("new", 22, "one", &error) == SshHostKeyStore::Error, "corrupt fail closed");
  SshHostKeyStore unwritable(dir.filePath("missing/knownhosts.json"));
  check(!unwritable.trust("host", 22, "one", &error), "write error blocks trust");
}
static void sessionSettingsChecks() {
  QTemporaryDir directory;
  const auto path = directory.filePath("settings.json");
  JsonSettings settings(path);
  settings.setValue("ssh/host", "original.invalid"); settings.setValue("ssh/encoding", "UTF-8"); settings.sync();
  QFile file(path); check(file.open(QIODevice::ReadOnly), "read original session settings");
  const auto before = file.readAll(); file.close();
  auto credentials = std::make_shared<FakeSshCredentials>();
  credentials->silentContainsFailure = true;
  SshTerminal first(credentials, directory.path()), second(credentials, directory.path());
  check(!first.usesSecureCredentials() && !second.usesSecureCredentials() && !first.credentialBusy()
            && first.credentialError().isEmpty() && credentials->containsCalls.load() == 0, "plain account/password sessions do not query the system credential store");
  check(first.setProperty("persistSettings", false) && second.setProperty("persistSettings", false), "workspace owns session configuration persistence");
  first.setHost("first.invalid"); second.setHost("second.invalid");
  first.setPassword("plain-a"); second.setPassword("plain-b");
  check(first.password() == "plain-a" && second.password() == "plain-b", "plain passwords are independently available to connection records");
  first.setEncoding("GB2312"); second.setEncoding("UTF-8");
  first.setAutoReconnectEnabled(false); second.setAutoReconnectEnabled(true);
  first.setReconnectMaxAttempts(2); second.setReconnectMaxAttempts(8);
  first.setReconnectIntervalMs(1000); second.setReconnectIntervalMs(4000);
  check(QMetaObject::invokeMethod(&first, "onWorkerConnected") && QMetaObject::invokeMethod(&second, "onWorkerConnected"), "exercise connected-setting writes without network");
  check(file.open(QIODevice::ReadOnly) && file.readAll() == before, "independent sessions cannot overwrite legacy/shared connection defaults");
  check(first.encoding() == "GB2312" && second.encoding() == "UTF-8" && first.reconnectMaxAttempts() == 2 && second.reconnectMaxAttempts() == 8,
        "session settings remain independent in memory");
}
static void credentialChecks() {
  QTemporaryDir dir;
  const auto oldPath = dir.filePath("LogAnalyQt.ini");
  { QSettings old(oldPath, QSettings::IniFormat); old.setValue("ssh/password", "legacy-secret"); old.sync(); }
  JsonSettings settings(dir.filePath("settings.json"));
  settings.setValue("ssh/host", "legacy.invalid"); settings.setValue("ssh/port", 2222);
  settings.setValue("ssh/username", "synthetic"); settings.sync();
  auto store = std::make_shared<FakeSshCredentials>();
  store->values[profile("legacy.invalid", 2222, "synthetic")] = "migrated-secret";
  SshTerminal terminal(store, dir.path());
  terminal.setUseSystemCredentials(true);
  check(waitFor([&] { return !terminal.credentialBusy(); }), "credential status completion");
  check(terminal.hasSavedPassword() && terminal.password().isEmpty(), "JSON identity loads saved state without echoing secret");
  QSettings old(oldPath, QSettings::IniFormat);
  check(old.value("ssh/password") == "legacy-secret", "runtime constructor cannot migrate or delete unconfirmed INI");
  terminal.setHost("other.invalid");
  check(waitFor([&] { return !terminal.credentialBusy(); }), "profile change completion");
  check(!terminal.hasSavedPassword(), "target binding");
  QString completed; bool succeeded = false;
  QObject::connect(&terminal, &SshTerminal::passwordOperationFinished, [&](const QString &id, bool ok, const QString &) { completed = id; succeeded = ok; });
  const auto save = terminal.savePassword("new-secret");
  check(!save.isEmpty() && completed.isEmpty(), "save enqueue is not completion");
  check(waitFor([&] { return !terminal.credentialBusy(); }), "save completion");
  check(completed == save && succeeded && terminal.hasSavedPassword(), "actual save success");
  const auto clear = terminal.clearSavedPassword();
  check(waitFor([&] { return !terminal.credentialBusy(); }), "clear completion");
  check(completed == clear && succeeded && !terminal.hasSavedPassword(), "actual clear success");
}
static void migrationSourceChecks() {
  QTemporaryDir dir;
  auto store = std::make_shared<FakeSshCredentials>();
  SshTerminal terminal(store, dir.path());
  terminal.setUseSystemCredentials(true);
  terminal.setHost("valid.invalid"); terminal.setUsername("synthetic");
  check(waitFor([&] { return !terminal.credentialBusy(); }), "silent failure fixture ready");
  QString completed; bool success = true;
  QObject::connect(&terminal, &SshTerminal::passwordOperationFinished,
    [&](const QString &id, bool ok, const QString &) { completed = id; success = ok; });
  store->silentRemoveFailure = true;
  const QString removal = terminal.clearSavedPassword();
  check(waitFor([&] { return completed == removal; }), "silent removal failure completion");
  check(!success && !terminal.credentialError().isEmpty(), "false-with-empty-error cannot succeed");
  store->silentRemoveFailure = false; store->silentContainsFailure = true;
  terminal.setUsername("another");
  check(waitFor([&] { return !terminal.credentialBusy(); }), "silent contains failure completion");
  check(!terminal.credentialError().isEmpty(), "false contains cannot claim known missing credential");
  terminal.setHost("host\n22");
  check(waitFor([&] { return !terminal.credentialBusy(); }), "invalid host fixture ready");
  terminal.connectToHost();
  check(terminal.connectionState() == SshTerminal::Failed, "control characters rejected");
  check(profile("foo\n22", 33, "bar") != profile("foo", 22, "33\nbar"), "canonical tuple avoids delimiter collision");
  store->silentContainsFailure = false;
  terminal.setHost("before.invalid"); terminal.setUsername("synthetic");
  check(waitFor([&] { return !terminal.credentialBusy(); }), "old-target operation ready");
  store->enteredRead = false; store->releaseRead = false; store->blockRead = true;
  completed.clear(); success = false;
  const QString oldTargetOperation = terminal.savePassword("old-target-secret");
  check(waitFor([&] { return store->enteredRead.load(); }), "save pending while target changes");
  terminal.setHost("after.invalid"); terminal.setPort(3333); terminal.setUsername("after-user");
  store->releaseRead = true;
  check(waitFor([&] { return !terminal.credentialBusy() && completed == oldTargetOperation; }), "old-target completion and coalesced refresh");
  check(success && !terminal.hasSavedPassword(), "actual old-target success does not update current target");
  check(store->values.value(profile("before.invalid", 22, "synthetic")) == "old-target-secret", "completed old-target transaction remains real");
}
static void cancellationChecks() {
  QTemporaryDir dir;
  auto store = std::make_shared<FakeSshCredentials>();
  QTcpServer server;
  check(server.listen(QHostAddress::LocalHost, 0), "local fake listener");
  SshTerminal terminal(store, dir.path());
  terminal.setUseSystemCredentials(true);
  check(waitFor([&] { return !terminal.credentialBusy(); }), "initial credentials");
  terminal.setHost("127.0.0.1"); terminal.setPort(server.serverPort()); terminal.setUsername("fake");
  check(waitFor([&] { return !terminal.credentialBusy(); }), "identity ready");
  terminal.setPassword("synthetic-password");
  terminal.connectToHost();
  check(waitFor([&] { return server.hasPendingConnections(); }), "TCP connected");
  auto *peer = server.nextPendingConnection();
  terminal.disconnectFromHost();
  check(waitFor([&] { return peer->state() == QAbstractSocket::UnconnectedState; }), "cancel closes stalled handshake");
  check(!terminal.isConnected() && terminal.connectionState() == SshTerminal::Disconnected, "stale state ignored");
  delete peer;
  QElapsedTimer elapsed; elapsed.start();
  {
    SshTerminal temporary(store, dir.path());
    temporary.setUseSystemCredentials(true);
    check(waitFor([&] { return !temporary.credentialBusy(); }), "temporary ready");
    temporary.setHost("127.0.0.1"); temporary.setPort(server.serverPort()); temporary.setUsername("fake");
    check(waitFor([&] { return !temporary.credentialBusy(); }), "temporary identity ready");
    temporary.setPassword("fake"); temporary.connectToHost();
    check(waitFor([&] { return server.hasPendingConnections(); }), "temporary TCP connected");
    elapsed.restart();
  }
  check(elapsed.elapsed() < 500, "destructor does not wait for network");
  peer = server.nextPendingConnection();
  check(waitFor([&] { return peer->state() == QAbstractSocket::UnconnectedState; }), "destroy closes transport");
  delete peer;
}
static void credentialDestructionChecks() {
  QTemporaryDir dir;
  auto store = std::make_shared<FakeSshCredentials>();
  auto *terminal = new SshTerminal(store, dir.path());
  terminal->setUseSystemCredentials(true);
  terminal->setHost("synthetic.invalid"); terminal->setUsername("synthetic");
  check(waitFor([&] { return !terminal->credentialBusy(); }), "slow store ready");
  store->blockRead = true;
  check(!terminal->savePassword("synthetic-only").isEmpty(), "slow operation queued");
  check(waitFor([&] { return store->enteredRead.load(); }), "slow native-like operation entered");
  QElapsedTimer elapsed; elapsed.start();
  delete terminal;
  check(elapsed.elapsed() < 500, "destructor does not join blocked credential UI");
  store->releaseRead = true;
  check(waitFor([&] { return store.use_count() == 1; }), "abandoned worker eventually releases owned credentials");
}
static void localSshdChecks() {
  QTemporaryDir dir;
  auto generateKey = [&](const QString &path) {
    QProcess keygen;
    keygen.start("/usr/bin/ssh-keygen", {"-q", "-t", "ed25519", "-N", "", "-f", path});
    check(keygen.waitForFinished(5000) && keygen.exitCode() == 0, "synthetic host key");
  };
  const QString firstKey = dir.filePath("first_host_key");
  const QString secondKey = dir.filePath("second_host_key");
  generateKey(firstKey); generateKey(secondKey);
  QTcpServer reservation;
  check(reservation.listen(QHostAddress::LocalHost, 0), "reserve SSH port");
  const int port = reservation.serverPort(); reservation.close();
  QProcess daemon;
  QByteArray log;
  QObject::connect(&daemon, &QProcess::readyReadStandardError, [&] {
    log += daemon.readAllStandardError();
  });
  auto startDaemon = [&](const QString &key) {
    log.clear();
    QFile config(dir.filePath("sshd_config")); check(config.open(QIODevice::WriteOnly), "sshd config");
    config.write(("ListenAddress 127.0.0.1\nPort " + QString::number(port) +
      "\nHostKey " + key + "\nPidFile " + dir.filePath("sshd.pid") +
      "\nPasswordAuthentication yes\nKbdInteractiveAuthentication no\nPubkeyAuthentication no\nUsePAM no\nStrictModes no\nLogLevel DEBUG3\n").toUtf8());
    config.close();
    daemon.start("/usr/sbin/sshd", {"-D", "-e", "-f", config.fileName()});
    check(daemon.waitForStarted(3000), "start isolated sshd");
    if (!waitFor([&] { return log.contains("Server listening on"); })) {
      QTextStream(stderr) << log;
      check(false, "isolated sshd ready");
    }
  };
  auto stopDaemon = [&] {
    daemon.terminate();
    if (!daemon.waitForFinished(3000)) { daemon.kill(); daemon.waitForFinished(); }
  };
  auto store = std::make_shared<FakeSshCredentials>();
  startDaemon(firstKey);
  {
    SshTerminal terminal(store, dir.path());
    terminal.setUseSystemCredentials(true);
    check(waitFor([&] { return !terminal.credentialBusy(); }), "SSH test credentials ready");
    terminal.setHost("127.0.0.1"); terminal.setPort(port); terminal.setUsername("stage10-synthetic-nonexistent");
    terminal.setReconnectIntervalMs(500); terminal.setReconnectMaxAttempts(2);
    check(waitFor([&] { return !terminal.credentialBusy(); }), "SSH identity ready");
    QString request, fingerprint;
    int challenges = 0;
    QObject::connect(&terminal, &SshTerminal::hostKeyConfirmationRequested,
      [&](const QString &, int challengePort, const QString &algorithm, const QString &fp, const QString &id) {
        check(challengePort == port && algorithm == "ssh-ed25519", "challenge frozen endpoint/key type");
        request = id; fingerprint = fp; ++challenges;
      });
    terminal.setPassword("synthetic-test-only-password");
    terminal.connectToHost();
    if (!waitFor([&] { return terminal.hostKeyPending(); }, 5000)) {
      QTextStream(stderr) << log << terminal.lastError(); check(false, "first host challenge");
    }
    auto noAuthFor = [&](int duration) {
      QElapsedTimer timer; timer.start();
      waitFor([&] { return timer.elapsed() >= duration; }, duration + 500);
      check(!log.contains("method password"), "no password before trust");
    };
    noAuthFor(100);
    terminal.confirmHostKey("stale-id", fingerprint, true);
    terminal.confirmHostKey(request, "SHA256:incorrect", true);
    noAuthFor(100);
    check(terminal.hostKeyPending(), "stale or wrong fingerprint cannot resolve challenge");
    const QString cancelled = request;
    terminal.disconnectFromHost();
    terminal.confirmHostKey(cancelled, fingerprint, true);
    noAuthFor(100);
    terminal.connectToHost();
    check(waitFor([&] { return terminal.hostKeyPending() && request != cancelled; }, 5000), "new request after cancellation");
    terminal.confirmHostKey(request, fingerprint, false);
    check(waitFor([&] { return terminal.connectionState() == SshTerminal::Failed; }), "reject is terminal");
    noAuthFor(700);
    check(challenges == 2 && terminal.reconnectAttempt() == 0, "rejected host never auto reconnects");
    terminal.connectToHost();
    check(waitFor([&] { return terminal.hostKeyPending(); }, 5000), "first trust can be explicitly retried");
    terminal.confirmHostKey(request, fingerprint, true);
    if (!waitFor([&] { return log.contains("method password"); }, 5000)) {
      QTextStream(stderr) << log << terminal.lastError(); check(false, "password only after accepted trust");
    }
    check(waitFor([&] { return terminal.connectionState() == SshTerminal::Failed; }), "synthetic password rejected");
    check(QFile::exists(dir.filePath("ssh-known-hosts.json")), "trust persisted");
    log.clear();
    terminal.connectToHost();
    check(waitFor([&] { return terminal.connectionState() == SshTerminal::Failed; }, 5000), "trusted reconnect reaches auth");
    check(challenges == 3 && log.contains("method password"), "trusted pin avoids repeated TOFU");
    // A native credential prompt can outlive a disconnect. Its late result
    // must not reach password authentication on the old connection.
    store->values[profile("127.0.0.1", port, "stage10-synthetic-nonexistent")] = "saved-synthetic-password";
    terminal.setPassword(QString());
    store->enteredRead = false; store->releaseRead = false; store->blockRead = true;
    log.clear(); terminal.connectToHost();
    check(waitFor([&] { return store->enteredRead.load(); }, 5000), "native-like credential read pending");
    terminal.disconnectFromHost();
    store->releaseRead = true;
    noAuthFor(200);
    store->blockRead = false;
    check(terminal.connectionState() == SshTerminal::Disconnected, "late credential result stays cancelled");
    store->enteredRead = false; store->releaseRead = false; store->blockRead = true;
    log.clear(); terminal.connectToHost();
    check(waitFor([&] { return store->enteredRead.load() && terminal.credentialBusy(); }, 5000), "clear fixture credential read pending");
    const QString clearing = terminal.clearSavedPassword();
    check(!clearing.isEmpty(), "clear accepted during authentication read");
    store->releaseRead = true;
    check(waitFor([&] { return !terminal.credentialBusy(); }), "clear waits for actual credential removal");
    noAuthFor(200);
    check(!terminal.hasSavedPassword(), "clear removes credential after cancelling old authentication");
    store->blockRead = false;
    store->values[profile("127.0.0.1", port, "stage10-synthetic-nonexistent")] = "saved-synthetic-password";
    store->enteredRead = false; store->releaseRead = false; store->blockRead = true;
    log.clear(); terminal.connectToHost();
    check(waitFor([&] { return store->enteredRead.load() && terminal.credentialBusy(); }, 5000), "timeout fixture credential read pending");
    noAuthFor(15100);
    store->releaseRead = true;
    check(waitFor([&] { return terminal.connectionState() == SshTerminal::Failed; }), "late read fails expired connection");
    noAuthFor(100);
    check(terminal.lastError().contains(QStringLiteral("超时")), "expired read explains timeout");
    store->blockRead = false;
    stopDaemon();
    startDaemon(secondKey);
    log.clear();
    terminal.connectToHost();
    check(waitFor([&] { return terminal.connectionState() == SshTerminal::Failed; }, 5000), "changed key rejected");
    noAuthFor(700);
    check(challenges == 3 && terminal.reconnectAttempt() == 0 &&
          terminal.lastError().contains(QStringLiteral("密钥已变化")), "rotation never prompts or reconnects");
  }
  stopDaemon();
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    trustChecks(); sessionSettingsChecks(); credentialChecks(); migrationSourceChecks(); cancellationChecks(); credentialDestructionChecks();
    if (app.arguments().contains("--local-sshd")) localSshdChecks();
    QTextStream(stdout) << "PASS: SSH trust, confirmed-config isolation, target binding, cancellation and destruction\n";
    return 0;
  } catch (const std::exception &e) {
    QTextStream(stderr) << "FAIL: " << e.what() << '\n'; return 1;
  }
}
