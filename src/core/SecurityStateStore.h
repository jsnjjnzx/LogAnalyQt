#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

struct SecurityState {
  int schemaVersion = 1;
  int proofVersion = 1;
  QString installId;
  QDateTime lastVerifiedUtc;
  QDateTime maxSeenUtc;
  int tamperCount = 0;
  QString bindingScope;
  QByteArray payloadHash;

  bool isStructurallyValid() const;
};

enum class SecurityStateReadStatus {
  Valid,
  NoState,
  CopyMissing,
  StateCorrupted,
  StateMismatch,
  TimeRollback,
  UnsupportedPlatform,
};

struct SecurityStateReadResult {
  SecurityStateReadStatus status = SecurityStateReadStatus::NoState;
  SecurityState state;
  bool hasTrustedState = false;
  QString detail;
  int nextTamperCount = 0;
};

class SecurityStateStore {
public:
  SecurityStateReadResult readState(const QDateTime &nowUtc) const;
  bool persistVerifiedState(const QDateTime &verifiedUtc,
                            const QDateTime &nowUtc, int tamperCount,
                            const QString &installIdHint,
                            QString *error = nullptr) const;
  bool touchState(const SecurityState &state, const QDateTime &nowUtc,
                  QString *error = nullptr) const;

private:
  QString secondaryStorePath() const;
};