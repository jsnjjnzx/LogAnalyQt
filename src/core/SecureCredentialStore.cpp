#include "SecureCredentialStore.h"

#include <QRegularExpression>

#ifdef Q_OS_MACOS
#include <Security/Security.h>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#endif

namespace {
bool validProfile(const QString &profileId, QString *error) {
  static const QRegularExpression pattern(QStringLiteral("^[a-f0-9]{64}$"));
  if (pattern.match(profileId).hasMatch())
    return true;
  if (error)
    *error = QStringLiteral("凭据配置标识无效");
  return false;
}

bool storeError(QString *error, const QString &operation) {
  if (error)
    *error = operation;
  return false;
}

#ifdef Q_OS_MACOS
CFMutableDictionaryRef credentialQuery(const QString &profileId, SecureCredentialStore::Domain domain) {
  auto query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                        &kCFTypeDictionaryKeyCallBacks,
                                        &kCFTypeDictionaryValueCallBacks);
  const QByteArray account = profileId.toUtf8();
  auto accountString = CFStringCreateWithBytes(
      kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(account.constData()),
      account.size(), kCFStringEncodingUTF8, false);
  CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
  CFStringRef service = CFSTR("LogAnalyQt.AiFraming");
  if (domain == SecureCredentialStore::Domain::Ssh)
    service = CFSTR("LogAnalyQt.Ssh");
  else if (domain == SecureCredentialStore::Domain::ProtocolSync)
    service = CFSTR("LogAnalyQt.ProtocolSync");
  CFDictionarySetValue(query, kSecAttrService, service);
  CFDictionarySetValue(query, kSecAttrAccount, accountString);
  CFDictionarySetValue(query, kSecAttrSynchronizable, kCFBooleanFalse);
  CFRelease(accountString);
  return query;
}
#elif defined(Q_OS_WIN)
std::wstring credentialTarget(const QString &profileId) {
  return (QStringLiteral("LogAnalyQt/AiFraming/") + profileId).toStdWString();
}
#endif
} // namespace

bool SecureCredentialStore::isAvailable() const {
#ifdef Q_OS_MACOS
  return true;
#elif defined(Q_OS_WIN)
  return m_domain == Domain::AiFraming;
#else
  return false;
#endif
}

bool SecureCredentialStore::contains(const QString &profileId, bool *exists,
                                 QString *error) const {
  if (error)
    error->clear();
  if (!exists)
    return storeError(error, QStringLiteral("凭据状态输出无效"));
  *exists = false;
  if (!validProfile(profileId, error))
    return false;
#ifdef Q_OS_MACOS
  auto query = credentialQuery(profileId, m_domain);
  CFDictionarySetValue(query, kSecReturnAttributes, kCFBooleanTrue);
  CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
  CFTypeRef attributes = nullptr;
  const OSStatus status = SecItemCopyMatching(query, &attributes);
  CFRelease(query);
  if (attributes)
    CFRelease(attributes);
  if (status == errSecItemNotFound)
    return true;
  if (status != errSecSuccess)
    return storeError(error, QStringLiteral("无法查询系统钥匙串凭据状态"));
  *exists = true;
  return true;
#elif defined(Q_OS_WIN)
  if (m_domain != Domain::AiFraming)
    return storeError(error, QStringLiteral("当前平台没有可用的安全凭据存储"));
  PCREDENTIALW credential = nullptr;
  const auto target = credentialTarget(profileId);
  if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
    if (GetLastError() == ERROR_NOT_FOUND)
      return true;
    return storeError(error, QStringLiteral("无法查询 Windows 凭据状态"));
  }
  *exists = true;
  // WinCred has no metadata-only read; never copy its returned secret.
  if (credential->CredentialBlob && credential->CredentialBlobSize)
    SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
  CredFree(credential);
  return true;
#else
  return storeError(error, QStringLiteral("当前平台没有可用的安全凭据存储"));
#endif
}

bool SecureCredentialStore::read(const QString &profileId, QByteArray *key,
                             QString *error) const {
  if (error)
    error->clear();
  if (!key)
    return storeError(error, QStringLiteral("凭据输出无效"));
  key->clear();
  if (!validProfile(profileId, error))
    return false;
#ifdef Q_OS_MACOS
  auto query = credentialQuery(profileId, m_domain);
  CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
  CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
  CFTypeRef value = nullptr;
  const OSStatus status = SecItemCopyMatching(query, &value);
  CFRelease(query);
  if (status == errSecItemNotFound)
    return true;
  if (status != errSecSuccess || !value) {
    if (value)
      CFRelease(value);
    return storeError(error, QStringLiteral("无法读取系统钥匙串凭据"));
  }
  if (CFGetTypeID(value) != CFDataGetTypeID()) {
    CFRelease(value);
    return storeError(error, QStringLiteral("系统钥匙串凭据格式无效"));
  }
  auto data = static_cast<CFDataRef>(value);
  if (CFDataGetLength(data) > 2048) {
    CFRelease(value);
    return storeError(error, QStringLiteral("系统凭据长度无效"));
  }
  *key = QByteArray(reinterpret_cast<const char *>(CFDataGetBytePtr(data)),
                    CFDataGetLength(data));
  CFRelease(value);
  return true;
#elif defined(Q_OS_WIN)
  if (m_domain != Domain::AiFraming)
    return storeError(error, QStringLiteral("当前平台没有可用的安全凭据存储"));
  PCREDENTIALW credential = nullptr;
  const auto target = credentialTarget(profileId);
  if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
    if (GetLastError() == ERROR_NOT_FOUND)
      return true;
    return storeError(error, QStringLiteral("无法读取 Windows 凭据"));
  }
  if (credential->CredentialBlobSize > 2048) {
    if (credential->CredentialBlob)
      SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree(credential);
    return storeError(error, QStringLiteral("系统凭据长度无效"));
  }
  *key = QByteArray(reinterpret_cast<const char *>(credential->CredentialBlob),
                    credential->CredentialBlobSize);
  if (credential->CredentialBlob && credential->CredentialBlobSize)
    SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
  CredFree(credential);
  return true;
#else
  return storeError(error, QStringLiteral("当前平台没有可用的安全凭据存储"));
#endif
}

bool SecureCredentialStore::write(const QString &profileId, const QByteArray &key,
                              QString *error) {
  if (error)
    error->clear();
  if (!validProfile(profileId, error))
    return false;
  if (key.isEmpty() || key.size() > 2048)
    return storeError(error, QStringLiteral("凭据长度必须为 1–2048 字节"));
#ifdef Q_OS_MACOS
  auto query = credentialQuery(profileId, m_domain);
  auto data = CFDataCreate(
      kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(key.constData()),
      key.size());
  auto update = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                         &kCFTypeDictionaryKeyCallBacks,
                                         &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(update, kSecValueData, data);
  OSStatus status = SecItemUpdate(query, update);
  if (status == errSecItemNotFound) {
    CFDictionarySetValue(query, kSecValueData, data);
    status = SecItemAdd(query, nullptr);
  }
  CFRelease(update);
  CFRelease(data);
  CFRelease(query);
  return status == errSecSuccess ||
         storeError(error, QStringLiteral("无法保存系统钥匙串凭据"));
#elif defined(Q_OS_WIN)
  if (m_domain != Domain::AiFraming)
    return storeError(error, QStringLiteral("当前平台没有可用的安全凭据存储"));
  auto target = credentialTarget(profileId);
  CREDENTIALW credential{};
  credential.Type = CRED_TYPE_GENERIC;
  credential.TargetName = target.data();
  credential.CredentialBlobSize = static_cast<DWORD>(key.size());
  credential.CredentialBlob =
      reinterpret_cast<LPBYTE>(const_cast<char *>(key.constData()));
  credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
  return CredWriteW(&credential, 0) != FALSE ||
         storeError(error, QStringLiteral("无法保存 Windows 凭据"));
#else
  return storeError(error, QStringLiteral("当前平台没有可用的安全凭据存储"));
#endif
}

bool SecureCredentialStore::remove(const QString &profileId, QString *error) {
  if (error)
    error->clear();
  if (!validProfile(profileId, error))
    return false;
#ifdef Q_OS_MACOS
  auto query = credentialQuery(profileId, m_domain);
  const OSStatus status = SecItemDelete(query);
  CFRelease(query);
  return status == errSecSuccess || status == errSecItemNotFound ||
         storeError(error, QStringLiteral("无法删除系统钥匙串凭据"));
#elif defined(Q_OS_WIN)
  if (m_domain != Domain::AiFraming)
    return storeError(error, QStringLiteral("当前平台没有可用的安全凭据存储"));
  const auto target = credentialTarget(profileId);
  if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) ||
      GetLastError() == ERROR_NOT_FOUND)
    return true;
  return storeError(error, QStringLiteral("无法删除 Windows 凭据"));
#else
  return storeError(error, QStringLiteral("当前平台没有可用的安全凭据存储"));
#endif
}

bool SecureCredentialStore::writeVerified(const QString &profileId,
                                           const QByteArray &key,
                                           QString *error) {
  if (error)
    error->clear();
  if (!validProfile(profileId, error))
    return false;
  if (key.isEmpty() || key.size() > 2048)
    return storeError(error, QStringLiteral("凭据长度必须为 1–2048 字节"));
  QByteArray previous;
  if (!read(profileId, &previous, error))
    return false;
  if (previous == key)
    return true;
  QByteArray saved;
  if (write(profileId, key, error) && read(profileId, &saved, error) && saved == key)
    return true;
  // Failed verification must retain a prior valid credential for recovery.
  const bool restored = previous.isEmpty() ? remove(profileId) : write(profileId, previous);
  QByteArray recovered;
  if (!restored || !read(profileId, &recovered) || recovered != previous)
    return storeError(error, QStringLiteral("系统凭据保存失败且恢复未确认，请保留旧凭据并重试恢复"));
  return storeError(error, QStringLiteral("系统凭据保存校验失败，原凭据已恢复，旧配置未清除"));
}
