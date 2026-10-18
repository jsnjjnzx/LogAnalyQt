#include "SecurityStateStore.h"

#include "../common/AppSettings.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {

constexpr int kSecurityStateSchemaVersion = 1;
constexpr int kSecurityStateProofVersion = 1;
constexpr auto kSecurityStateFormatKey = "security/sealedStateFormat";
constexpr auto kSecurityStateBlobKey = "security/sealedStateBlobV1";
constexpr auto kSecurityStateFormatValue = "dpapi-current-user-v1";
constexpr auto kSecurityStateBindingScope = "windows-dpapi-current-user";
constexpr auto kSecurityStateLocalFileName = "security_state.ini";

struct SecurityStoreRecord {
  bool exists = false;
  bool valid = false;
  SecurityState state;
  QString error;
};

QSettings secondarySettings(const QString &filePath) {
  return QSettings(filePath, QSettings::IniFormat);
}

QByteArray canonicalPayload(const SecurityState &state) {
  QByteArray payload;
  payload += "schemaVersion=" + QByteArray::number(state.schemaVersion) + '\n';
  payload += "proofVersion=" + QByteArray::number(state.proofVersion) + '\n';
  payload += "bindingScope=" + state.bindingScope.toUtf8() + '\n';
  payload += "installId=" + state.installId.toUtf8() + '\n';
  payload += "lastVerifiedUtc=" +
             state.lastVerifiedUtc.toUTC().toString(Qt::ISODateWithMs).toUtf8() +
             '\n';
  payload += "maxSeenUtc=" +
             state.maxSeenUtc.toUTC().toString(Qt::ISODateWithMs).toUtf8() +
             '\n';
  payload += "tamperCount=" + QByteArray::number(state.tamperCount) + '\n';
  return payload;
}

QByteArray calculatePayloadHash(const SecurityState &state) {
  return QCryptographicHash::hash(canonicalPayload(state),
                                  QCryptographicHash::Sha256);
}

bool equivalentState(const SecurityState &left, const SecurityState &right) {
  return left.schemaVersion == right.schemaVersion &&
         left.proofVersion == right.proofVersion &&
         left.installId == right.installId &&
         left.lastVerifiedUtc == right.lastVerifiedUtc &&
         left.maxSeenUtc == right.maxSeenUtc &&
         left.tamperCount == right.tamperCount &&
         left.bindingScope == right.bindingScope &&
         left.payloadHash == right.payloadHash;
}

const SecurityState &preferredState(const SecurityState &left,
                                    const SecurityState &right) {
  if (left.lastVerifiedUtc != right.lastVerifiedUtc)
    return left.lastVerifiedUtc > right.lastVerifiedUtc ? left : right;
  if (left.maxSeenUtc != right.maxSeenUtc)
    return left.maxSeenUtc > right.maxSeenUtc ? left : right;
  return left.installId >= right.installId ? left : right;
}

QString baseErrorMessage(const QString &prefix, const QString &error) {
  return error.isEmpty() ? prefix : QString("%1%2").arg(prefix, error);
}

bool readIsoUtc(const QJsonObject &object, const char *key, QDateTime *value) {
  const QString text = object.value(QLatin1String(key)).toString().trimmed();
  if (text.isEmpty())
    return false;

  QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
  if (!parsed.isValid())
    parsed = QDateTime::fromString(text, Qt::ISODate);
  if (!parsed.isValid())
    return false;

  *value = parsed.toUTC();
  return true;
}

#ifdef Q_OS_WIN

DATA_BLOB toDataBlob(const QByteArray &bytes) {
  DATA_BLOB blob;
  blob.cbData = static_cast<DWORD>(bytes.size());
  blob.pbData = bytes.isEmpty()
                    ? nullptr
                    : reinterpret_cast<BYTE *>(
                          const_cast<char *>(bytes.constData()));
  return blob;
}

QString winErrorString(DWORD errorCode) {
  return QString("Windows API error %1").arg(errorCode);
}

QByteArray dpapiEntropy() {
  QString appId = QCoreApplication::applicationName().trimmed();
  if (appId.isEmpty())
    appId = QStringLiteral("LogAnalyQt");
  return QStringLiteral("LogAnalyQt::SecurityState::%1::v1")
      .arg(appId)
      .toUtf8();
}

#endif

class SecurityStateCodec {
public:
  static QByteArray seal(const SecurityState &state, QString *error) {
#ifndef Q_OS_WIN
    Q_UNUSED(state);
    if (error)
      *error = QStringLiteral("当前平台不支持 Windows DPAPI");
    return {};
#else
    SecurityState writableState = state;
    writableState.payloadHash = calculatePayloadHash(writableState);

    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), writableState.schemaVersion);
    object.insert(QStringLiteral("proofVersion"), writableState.proofVersion);
    object.insert(QStringLiteral("installId"), writableState.installId);
    object.insert(QStringLiteral("lastVerifiedUtc"),
                  writableState.lastVerifiedUtc.toUTC().toString(
                      Qt::ISODateWithMs));
    object.insert(QStringLiteral("maxSeenUtc"),
                  writableState.maxSeenUtc.toUTC().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("tamperCount"), writableState.tamperCount);
    object.insert(QStringLiteral("bindingScope"), writableState.bindingScope);
    object.insert(QStringLiteral("payloadHash"),
                  QString::fromLatin1(writableState.payloadHash.toHex()));

    const QByteArray plainBytes =
        QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray entropyBytes = dpapiEntropy();
    DATA_BLOB inputBlob = toDataBlob(plainBytes);
    DATA_BLOB entropyBlob = toDataBlob(entropyBytes);
    DATA_BLOB outputBlob;
    SecureZeroMemory(&outputBlob, sizeof(outputBlob));

    if (!CryptProtectData(&inputBlob, L"LogAnalyQt Security State",
                          &entropyBlob, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &outputBlob)) {
      if (error)
        *error = winErrorString(GetLastError());
      return {};
    }

    QByteArray sealedBytes(
        reinterpret_cast<const char *>(outputBlob.pbData),
        static_cast<int>(outputBlob.cbData));
    LocalFree(outputBlob.pbData);
    return sealedBytes;
#endif
  }

  static bool unseal(const QByteArray &sealedBytes, SecurityState *state,
                     QString *error) {
#ifndef Q_OS_WIN
    Q_UNUSED(sealedBytes);
    Q_UNUSED(state);
    if (error)
      *error = QStringLiteral("当前平台不支持 Windows DPAPI");
    return false;
#else
    if (sealedBytes.isEmpty()) {
      if (error)
        *error = QStringLiteral("密封状态为空");
      return false;
    }

    QByteArray entropyBytes = dpapiEntropy();
    DATA_BLOB inputBlob = toDataBlob(sealedBytes);
    DATA_BLOB entropyBlob = toDataBlob(entropyBytes);
    DATA_BLOB outputBlob;
    SecureZeroMemory(&outputBlob, sizeof(outputBlob));

    if (!CryptUnprotectData(&inputBlob, nullptr, &entropyBlob, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &outputBlob)) {
      if (error)
        *error = winErrorString(GetLastError());
      return false;
    }

    const QByteArray plainBytes(
        reinterpret_cast<const char *>(outputBlob.pbData),
        static_cast<int>(outputBlob.cbData));
    LocalFree(outputBlob.pbData);

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(plainBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
      if (error)
        *error = QStringLiteral("解封后的状态包不是有效 JSON");
      return false;
    }

    SecurityState parsedState;
    const QJsonObject object = document.object();
    parsedState.schemaVersion = object.value(QStringLiteral("schemaVersion"))
                                    .toInt(-1);
    parsedState.proofVersion = object.value(QStringLiteral("proofVersion"))
                                   .toInt(-1);
    parsedState.installId =
        object.value(QStringLiteral("installId")).toString().trimmed();
    parsedState.tamperCount =
        object.value(QStringLiteral("tamperCount")).toInt(-1);
    parsedState.bindingScope =
        object.value(QStringLiteral("bindingScope")).toString().trimmed();
    parsedState.payloadHash = QByteArray::fromHex(
        object.value(QStringLiteral("payloadHash")).toString().toLatin1());

    if (!readIsoUtc(object, "lastVerifiedUtc", &parsedState.lastVerifiedUtc) ||
        !readIsoUtc(object, "maxSeenUtc", &parsedState.maxSeenUtc)) {
      if (error)
        *error = QStringLiteral("状态包时间字段无效");
      return false;
    }

    if (!parsedState.isStructurallyValid()) {
      if (error)
        *error = QStringLiteral("状态包字段校验失败");
      return false;
    }

    const QByteArray expectedHash = calculatePayloadHash(parsedState);
    if (parsedState.payloadHash != expectedHash) {
      if (error)
        *error = QStringLiteral("状态包完整性摘要不匹配");
      return false;
    }

    *state = parsedState;
    return true;
#endif
  }
};

SecurityStoreRecord readRecord(QSettings &settings) {
  SecurityStoreRecord record;
  const QString format =
        settings.value(QString::fromLatin1(kSecurityStateFormatKey))
          .toString()
          .trimmed();
  const QString blobBase64 =
        settings.value(QString::fromLatin1(kSecurityStateBlobKey))
          .toString()
          .trimmed();

  if (format.isEmpty() && blobBase64.isEmpty())
    return record;

  record.exists = true;
  if (format != QString::fromLatin1(kSecurityStateFormatValue) ||
      blobBase64.isEmpty()) {
    record.error = QStringLiteral("缺少有效的状态格式标记或密封内容");
    return record;
  }

  const QByteArray sealedBytes = QByteArray::fromBase64(blobBase64.toLatin1());
  if (sealedBytes.isEmpty()) {
    record.error = QStringLiteral("密封状态不是有效 Base64");
    return record;
  }

  QString error;
  if (!SecurityStateCodec::unseal(sealedBytes, &record.state, &error)) {
    record.error = error;
    return record;
  }

  record.valid = true;
  return record;
}

bool writeRecord(QSettings &settings, const QByteArray &sealedBytes,
                 QString *error) {
  settings.setValue(QString::fromLatin1(kSecurityStateFormatKey),
                    QString::fromLatin1(kSecurityStateFormatValue));
  settings.setValue(QString::fromLatin1(kSecurityStateBlobKey),
                    QString::fromLatin1(sealedBytes.toBase64()));
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    if (error)
      *error = QStringLiteral("QSettings 写入失败");
    return false;
  }
  return true;
}

} // namespace

bool SecurityState::isStructurallyValid() const {
  return schemaVersion == kSecurityStateSchemaVersion &&
         proofVersion == kSecurityStateProofVersion &&
         !installId.trimmed().isEmpty() && lastVerifiedUtc.isValid() &&
         maxSeenUtc.isValid() && tamperCount >= 0 &&
         bindingScope == QString::fromLatin1(kSecurityStateBindingScope) &&
         payloadHash.size() == QCryptographicHash::hashLength(
                                   QCryptographicHash::Sha256) &&
         lastVerifiedUtc.toUTC() <= maxSeenUtc.toUTC();
}

QString SecurityStateStore::secondaryStorePath() const {
  QString localPath =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (localPath.isEmpty()) {
    localPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  }
  if (localPath.isEmpty())
    return {};

  QDir dir(localPath);
  if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
    return {};

  return dir.filePath(QString::fromLatin1(kSecurityStateLocalFileName));
}

SecurityStateReadResult SecurityStateStore::readState(
    const QDateTime &nowUtc) const {
  SecurityStateReadResult result;

#ifndef Q_OS_WIN
  Q_UNUSED(nowUtc);
  result.status = SecurityStateReadStatus::UnsupportedPlatform;
  result.detail = QStringLiteral("当前平台不支持 Windows 密封状态校验。");
  return result;
#else
  auto primarySettings = deviceStateSettings();
  const SecurityStoreRecord primaryRecord = readRecord(primarySettings);

  const QString secondaryPath = secondaryStorePath();
  SecurityStoreRecord secondaryRecord;
  if (secondaryPath.isEmpty()) {
    secondaryRecord.exists = true;
    secondaryRecord.error = QStringLiteral("无法定位用户本地数据目录");
  } else {
    auto secondary = secondarySettings(secondaryPath);
    secondaryRecord = readRecord(secondary);
  }

  const bool primaryExists = primaryRecord.exists;
  const bool secondaryExists = secondaryRecord.exists;

  if (!primaryExists && !secondaryExists) {
    result.status = SecurityStateReadStatus::NoState;
    result.detail = QStringLiteral("未找到本地安全状态记录。");
    return result;
  }

  if (primaryRecord.valid && !secondaryExists) {
    result.status = SecurityStateReadStatus::CopyMissing;
    result.state = primaryRecord.state;
    result.hasTrustedState = true;
    result.detail = QStringLiteral("检测到用户本地副本缺失。");
    result.nextTamperCount = primaryRecord.state.tamperCount + 1;
    return result;
  }

  if (secondaryRecord.valid && !primaryExists) {
    result.status = SecurityStateReadStatus::CopyMissing;
    result.state = secondaryRecord.state;
    result.hasTrustedState = true;
    result.detail = QStringLiteral("检测到程序目录主记录缺失。");
    result.nextTamperCount = secondaryRecord.state.tamperCount + 1;
    return result;
  }

  if (primaryExists && secondaryExists && primaryRecord.valid &&
      secondaryRecord.valid) {
    if (!equivalentState(primaryRecord.state, secondaryRecord.state)) {
      result.status = SecurityStateReadStatus::StateMismatch;
      result.state = preferredState(primaryRecord.state, secondaryRecord.state);
      result.hasTrustedState = true;
      const int highestTamperCount =
          primaryRecord.state.tamperCount > secondaryRecord.state.tamperCount
              ? primaryRecord.state.tamperCount
              : secondaryRecord.state.tamperCount;
      result.nextTamperCount = highestTamperCount + 1;
      result.detail =
          QStringLiteral("程序目录主记录与用户本地副本内容不一致。");
      return result;
    }

    result.status = SecurityStateReadStatus::Valid;
    result.state = primaryRecord.state;
    result.hasTrustedState = true;
    result.nextTamperCount = primaryRecord.state.tamperCount;

    if (nowUtc.isValid() && nowUtc.toUTC() < result.state.maxSeenUtc.toUTC()) {
      result.status = SecurityStateReadStatus::TimeRollback;
      result.nextTamperCount = result.state.tamperCount + 1;
      result.detail = QStringLiteral(
          "检测到系统时间回拨，当前时间早于最近一次可信启动时间。");
    }
    return result;
  }

  if (primaryRecord.valid) {
    result.state = primaryRecord.state;
    result.hasTrustedState = true;
    result.nextTamperCount = primaryRecord.state.tamperCount + 1;
  } else if (secondaryRecord.valid) {
    result.state = secondaryRecord.state;
    result.hasTrustedState = true;
    result.nextTamperCount = secondaryRecord.state.tamperCount + 1;
  }

  result.status = SecurityStateReadStatus::StateCorrupted;
  if (!primaryExists) {
    result.detail = QStringLiteral("程序目录主记录缺失。");
  } else if (!secondaryExists) {
    result.detail = QStringLiteral("用户本地副本缺失。");
  } else if (!primaryRecord.valid && !secondaryRecord.valid) {
    result.detail = baseErrorMessage(
        QStringLiteral("程序目录主记录和用户本地副本都无法通过校验："),
        QStringLiteral("%1；%2")
            .arg(primaryRecord.error, secondaryRecord.error));
  } else if (!primaryRecord.valid) {
    result.detail = baseErrorMessage(
        QStringLiteral("程序目录主记录无法通过校验："), primaryRecord.error);
  } else {
    result.detail = baseErrorMessage(
        QStringLiteral("用户本地副本无法通过校验："), secondaryRecord.error);
  }
  return result;
#endif
}

bool SecurityStateStore::persistVerifiedState(const QDateTime &verifiedUtc,
                                             const QDateTime &nowUtc,
                                             int tamperCount,
                                             const QString &installIdHint,
                                             QString *error) const {
#ifndef Q_OS_WIN
  Q_UNUSED(verifiedUtc);
  Q_UNUSED(nowUtc);
  Q_UNUSED(tamperCount);
  Q_UNUSED(installIdHint);
  if (error)
    *error = QStringLiteral("当前平台不支持 Windows DPAPI");
  return false;
#else
  if (!verifiedUtc.isValid() || !nowUtc.isValid()) {
    if (error)
      *error = QStringLiteral("安全状态写入时间无效");
    return false;
  }

  SecurityState state;
  state.schemaVersion = kSecurityStateSchemaVersion;
  state.proofVersion = kSecurityStateProofVersion;
  state.installId = installIdHint.trimmed().isEmpty()
                        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                        : installIdHint.trimmed();
  state.lastVerifiedUtc = verifiedUtc.toUTC();
  state.maxSeenUtc = verifiedUtc.toUTC() > nowUtc.toUTC() ? verifiedUtc.toUTC()
                                                          : nowUtc.toUTC();
  state.tamperCount = tamperCount < 0 ? 0 : tamperCount;
  state.bindingScope = QString::fromLatin1(kSecurityStateBindingScope);
  state.payloadHash = calculatePayloadHash(state);

  const QByteArray sealedBytes = SecurityStateCodec::seal(state, error);
  if (sealedBytes.isEmpty())
    return false;

  auto primarySettings = deviceStateSettings();
  if (!writeRecord(primarySettings, sealedBytes, error)) {
    if (error)
      *error = QStringLiteral("主记录写入失败：%1").arg(*error);
    return false;
  }

  const QString secondaryPath = secondaryStorePath();
  if (secondaryPath.isEmpty()) {
    if (error)
      *error = QStringLiteral("无法定位用户本地数据目录");
    return false;
  }

  auto secondary = secondarySettings(secondaryPath);
  if (!writeRecord(secondary, sealedBytes, error)) {
    if (error)
      *error = QStringLiteral("副本写入失败：%1").arg(*error);
    return false;
  }
  return true;
#endif
}

bool SecurityStateStore::touchState(const SecurityState &state,
                                    const QDateTime &nowUtc,
                                    QString *error) const {
#ifndef Q_OS_WIN
  Q_UNUSED(state);
  Q_UNUSED(nowUtc);
  if (error)
    *error = QStringLiteral("当前平台不支持 Windows DPAPI");
  return false;
#else
  if (!state.isStructurallyValid() || !nowUtc.isValid()) {
    if (error)
      *error = QStringLiteral("当前安全状态无法刷新");
    return false;
  }

  const QDateTime nowUtcNormalized = nowUtc.toUTC();
  if (nowUtcNormalized < state.maxSeenUtc.toUTC()) {
    if (error)
      *error = QStringLiteral("当前时间早于可信时间上界");
    return false;
  }

  if (nowUtcNormalized == state.maxSeenUtc.toUTC())
    return true;

  SecurityState refreshedState = state;
  refreshedState.maxSeenUtc = nowUtcNormalized;
  return persistVerifiedState(refreshedState.lastVerifiedUtc,
                              refreshedState.maxSeenUtc,
                              refreshedState.tamperCount,
                              refreshedState.installId, error);
#endif
}