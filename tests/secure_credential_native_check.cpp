#include "core/SecureCredentialStore.h"
#include "core/ai/AiCredentialStore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTextStream>
#include <QUuid>

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  AiCredentialStore ai;
  SecureCredentialStore ssh(SecureCredentialStore::Domain::Ssh);
  SecureCredentialStore smb(SecureCredentialStore::Domain::ProtocolSync);
  SecureCredentialStore *stores[] = {&ai, &ssh, &smb};
  const QString profile = QString::fromLatin1(QCryptographicHash::hash(
      QUuid::createUuid().toByteArray(), QCryptographicHash::Sha256).toHex());
  const QByteArray prefix = "synthetic-only-" + QUuid::createUuid().toByteArray();
  QString error;
  bool ok = true;
  // The only queried accounts are this run's random synthetic profile.
  for (auto *store : stores) {
    bool exists = false;
    if (!store->isAvailable() || !store->contains(profile, &exists, &error) || exists) {
      QTextStream(stderr) << "FAIL: isolated native profile unavailable\n";
      return 1;
    }
  }
  for (int i = 0; i < 3; ++i)
    ok = stores[i]->writeVerified(profile, prefix + QByteArray::number(i), &error) && ok;
  for (int i = 0; i < 3; ++i) {
    QByteArray read;
    bool exists = false;
    ok = stores[i]->contains(profile, &exists, &error) && exists &&
         stores[i]->read(profile, &read, &error) &&
         read == prefix + QByteArray::number(i) && ok;
  }
  // Same account in each domain proves no service can overwrite another domain.
  ok = ssh.writeVerified(profile, prefix + "replacement", &error) && ok;
  QByteArray aiRead;
  ok = ai.read(profile, &aiRead, &error) && aiRead == prefix + "0" && ok;
  for (auto *store : stores) {
    const bool removed = store->remove(profile, &error);
    if (!removed)
      QTextStream(stderr) << "Cleanup required for synthetic profile: " << profile << '\n';
    bool exists = true;
    ok = removed && store->contains(profile, &exists, &error) && !exists && ok;
  }
  QTextStream(ok ? stdout : stderr)
      << (ok ? "PASS: AI/SSH/SMB native domain isolation, verified roundtrip, replace/delete; synthetic only"
             : "FAIL: isolated native credential lifecycle") << '\n';
  return ok ? 0 : 2;
}
