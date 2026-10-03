#include "app/base/secret_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincred.h>

#include <string>
#endif

namespace tamias {
namespace {

#if defined(_WIN32)

// 凭据管理器里的 target name：认得出是 tamias 的，也认得出是哪个槽位。
std::wstring cred_target(const QString& key) {
  return (QStringLiteral("tamias/") + key).toStdWString();
}

#else

// 非 Windows 的退路：AppConfigLocation 下一个属主可读写的文件。
// 槽位名里的 '/' 换成 '.'，免得变成子目录。
QString secret_path(const QString& key) {
  QString name = key;
  name.replace(QLatin1Char('/'), QLatin1Char('.'));
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  QDir().mkpath(dir);
  return dir + QLatin1Char('/') + name + QStringLiteral(".secret");
}

#endif

bool fail(QString* error, const QString& message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

}  // namespace

QString SecretStore::load(const QString& key) {
#if defined(_WIN32)
  PCREDENTIALW cred = nullptr;
  const std::wstring target = cred_target(key);
  if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) == FALSE || cred == nullptr) {
    return {};
  }
  const QByteArray blob(reinterpret_cast<const char*>(cred->CredentialBlob),
                        static_cast<int>(cred->CredentialBlobSize));
  CredFree(cred);
  return QString::fromUtf8(blob);
#else
  QFile file(secret_path(key));
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QString::fromUtf8(file.readAll());
#endif
}

bool SecretStore::save(const QString& key, const QString& secret, QString* error) {
#if defined(_WIN32)
  const std::wstring target = cred_target(key);
  if (secret.isEmpty()) {
    if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != FALSE) {
      return true;
    }
    // 本来就什么都没有 = 已经清干净了，不算失败。
    return GetLastError() == ERROR_NOT_FOUND
               ? true
               : fail(error, QStringLiteral("CredDeleteW failed (error %1)").arg(GetLastError()));
  }

  const QByteArray blob = secret.toUtf8();
  if (blob.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
    // 凭据管理器单条有上限（约 2.5 KB）；正常 API key 远够，撞上了就说清楚。
    return fail(error, QStringLiteral("key is too long for the credential store (%1 bytes)")
                           .arg(blob.size()));
  }

  const std::wstring user = QStringLiteral("tamias").toStdWString();
  CREDENTIALW cred{};
  cred.Type = CRED_TYPE_GENERIC;
  cred.TargetName = const_cast<LPWSTR>(target.c_str());
  cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
  cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(blob.constData()));
  cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
  cred.UserName = const_cast<LPWSTR>(user.c_str());
  if (CredWriteW(&cred, 0) == FALSE) {
    return fail(error, QStringLiteral("CredWriteW failed (error %1)").arg(GetLastError()));
  }
  return true;
#else
  const QString path = secret_path(key);
  if (secret.isEmpty()) {
    if (!QFile::exists(path)) {
      return true;
    }
    return QFile::remove(path) ? true
                               : fail(error, QStringLiteral("cannot remove %1").arg(path));
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return fail(error, file.errorString());
  }
  const QByteArray blob = secret.toUtf8();
  if (file.write(blob) != blob.size()) {
    return fail(error, file.errorString());
  }
  file.close();
  // 只有属主可读写。Windows 不走这条分支，所以不用操心这些 flag 的平台差异。
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  return true;
#endif
}

bool SecretStore::remove(const QString& key) { return save(key, QString(), nullptr); }

QString SecretStore::backend_name() {
#if defined(_WIN32)
  return QCoreApplication::translate("tamias::SecretStore", "Windows Credential Manager");
#else
  return QCoreApplication::translate(
      "tamias::SecretStore", "a per-user file in the app config directory");
#endif
}

}  // namespace tamias
