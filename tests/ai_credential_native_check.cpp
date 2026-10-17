#include "AiCredentialStore.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QUuid>
#include <QTextStream>
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  AiCredentialStore store;
  const QString profile = QString::fromLatin1(QCryptographicHash::hash(QUuid::createUuid().toByteArray(), QCryptographicHash::Sha256).toHex());
  const QByteArray first = "local-test-only-" + QUuid::createUuid().toByteArray();
  const QByteArray second = "replacement-test-only-" + QUuid::createUuid().toByteArray();
  QString error; bool exists = false; QByteArray read;
  if (!store.isAvailable() || !store.contains(profile, &exists, &error) || exists) {
    QTextStream(stderr) << "Native storage unavailable before test: " << error << '\n'; return 1;
  }
  bool ok = store.write(profile, first, &error)
    && store.contains(profile, &exists, &error) && exists
    && store.read(profile, &read, &error) && read == first
    && store.write(profile, second, &error)
    && store.read(profile, &read, &error) && read == second;
  const bool removed = store.remove(profile, &error);
  ok = ok && removed && store.contains(profile, &exists, &error) && !exists;
  if (!removed) QTextStream(stderr) << "Cleanup required for test profile: " << profile << '\n';
  QTextStream(ok ? stdout : stderr) << (ok ? "PASS: native credential write/exists/read/replace/delete, synthetic key only" : "FAIL: native credential lifecycle") << " " << error << '\n';
  return ok ? 0 : 2;
}
