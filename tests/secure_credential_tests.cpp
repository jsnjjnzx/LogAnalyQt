#include "core/SecureCredentialStore.h"

#include <QCoreApplication>
#include <QMap>
#include <QTextStream>

namespace {
int failures = 0;
void expect(bool ok, const QString &message) {
  if (!ok) {
    ++failures;
    QTextStream(stderr) << "FAIL: " << message << '\n';
  }
}
class MemoryCredentials : public SecureCredentialStore {
public:
  MemoryCredentials() : SecureCredentialStore(Domain::Ssh) {}
  bool writeFails = false;
  bool removeFails = false;
  int failReadNumber = -1;
  int corruptWriteNumber = -1;
  mutable int reads = 0;
  int writes = 0;
  QMap<QString, QByteArray> values;
  bool isAvailable() const override { return true; }
  bool contains(const QString &profile, bool *exists, QString *) const override {
    *exists = values.contains(profile);
    return true;
  }
  bool read(const QString &profile, QByteArray *value, QString *error) const override {
    ++reads;
    value->clear();
    if (reads == failReadNumber) {
      if (error) *error = QStringLiteral("synthetic read failure");
      return false;
    }
    *value = values.value(profile);
    return true;
  }
  bool write(const QString &profile, const QByteArray &value, QString *error) override {
    ++writes;
    if (writeFails) {
      if (error) *error = QStringLiteral("synthetic write failure");
      return false;
    }
    values.insert(profile, writes == corruptWriteNumber ? QByteArray("synthetic-corruption") : value);
    return true;
  }
  bool remove(const QString &profile, QString *) override {
    if (removeFails) return false;
    values.remove(profile);
    return true;
  }
};
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  const QString profile(64, QLatin1Char('a'));
  const QByteArray secret("synthetic-password-only");
  MemoryCredentials fake;
  QString error;
  expect(fake.writeVerified(profile, secret, &error) && fake.reads == 2 &&
             fake.values.value(profile) == secret,
         QStringLiteral("verified save snapshots and reads its write back"));
  const QByteArray replacement("synthetic-replacement");
  const int writes = fake.writes;
  expect(fake.writeVerified(profile, secret, &error) && fake.writes == writes,
         QStringLiteral("existing identical secret needs no replacement"));
  fake.failReadNumber = fake.reads + 1;
  expect(!fake.writeVerified(profile, replacement, &error) && fake.writes == writes,
         QStringLiteral("unreadable prior secret stops before any write"));
  fake.writeFails = true;
  expect(!fake.writeVerified(profile, replacement, &error) && fake.values.value(profile) == secret,
         QStringLiteral("write failure retains prior credential"));
  fake.writeFails = false;
  fake.failReadNumber = fake.reads + 2;
  expect(!fake.writeVerified(profile, replacement, &error) && fake.values.value(profile) == secret,
         QStringLiteral("readback failure restores prior credential"));
  fake.corruptWriteNumber = fake.writes + 1;
  expect(!fake.writeVerified(profile, replacement, &error) && fake.values.value(profile) == secret &&
             !error.contains(QString::fromUtf8(secret)) && !error.contains(QString::fromUtf8(replacement)),
         QStringLiteral("readback mismatch restores prior credential without disclosing either value"));
  MemoryCredentials empty;
  empty.corruptWriteNumber = 1;
  expect(!empty.writeVerified(profile, secret, &error) && !empty.values.contains(profile),
         QStringLiteral("failed initial save removes its unverified value"));
  MemoryCredentials cleanupFailure;
  cleanupFailure.corruptWriteNumber = 1;
  cleanupFailure.removeFails = true;
  expect(!cleanupFailure.writeVerified(profile, secret, &error) && error.contains(QStringLiteral("恢复未确认")),
         QStringLiteral("cleanup failure remains an explicit recoverable failure"));

  // Invalid input returns before touching any native credential service.
  SecureCredentialStore native(SecureCredentialStore::Domain::Ssh);
  bool exists = true;
  QByteArray value("stale");
  expect(!native.contains(QStringLiteral("invalid"), &exists, &error) && !exists,
         QStringLiteral("invalid profile cannot query native storage"));
  expect(!native.read(QStringLiteral("invalid"), &value, &error) && value.isEmpty(),
         QStringLiteral("failed read clears output"));
  expect(!native.write(profile, QByteArray(), &error) &&
             !native.write(profile, QByteArray(2049, 'x'), &error),
         QStringLiteral("empty and oversized secrets rejected before native call"));
  expect(!native.remove(QStringLiteral("invalid"), &error) &&
             !native.contains(profile, nullptr, &error) && !native.read(profile, nullptr, &error),
         QStringLiteral("invalid mutation and output rejected before native call"));
  if (!failures)
    QTextStream(stdout) << "PASS: shared credential validation and verified writes (synthetic fake only)\n";
  return failures ? 1 : 0;
}
