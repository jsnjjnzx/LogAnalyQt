#pragma once

#include <QByteArray>
#include <QString>

// Call on a serial background worker: native storage may show blocking system UI.
class SecureCredentialStore {
public:
  enum class Domain { AiFraming, Ssh, ProtocolSync, Mqtt };
  explicit SecureCredentialStore(Domain domain) : m_domain(domain) {}
  virtual ~SecureCredentialStore() = default;
  virtual bool isAvailable() const;
  virtual bool contains(const QString &profileId, bool *exists,
                        QString *error = nullptr) const;
  // A missing credential is a successful read with an empty value.
  virtual bool read(const QString &profileId, QByteArray *key,
                    QString *error = nullptr) const;
  virtual bool write(const QString &profileId, const QByteArray &key,
                     QString *error = nullptr);
  virtual bool remove(const QString &profileId, QString *error = nullptr);
  // Verified replacement restores the previous value on failure.
  // Legacy callers must reject conflicting stored values before calling this.
  bool writeVerified(const QString &profileId, const QByteArray &key,
                     QString *error = nullptr);
private:
  Domain m_domain;
};
