#pragma once

#include <QByteArray>
#include <QString>

// Encrypts secrets for the INI file (see doc/architecture.md, §4.11).
// Windows uses DPAPI; Linux stores secrets through libsecret when it is
// compiled in and the keyring is usable, and otherwise falls back to a 0600
// file readable only by the user. Other platforms have a stub that refuses to
// store them.
namespace SecretProtector {

// Raw protected bytes; an empty result means failure.
QByteArray protect(const QString &plainText);

// Empty if the data is corrupted or belongs to another user/machine.
QString unprotect(const QByteArray &protectedData);

// Whether an existing protected value can stay as-is when its plaintext did
// not change. Windows accepts any non-empty DPAPI blob. Linux keeps l: tokens
// unconditionally (a transient keyring outage must not downgrade to plaintext),
// keeps f: tokens only while libsecret is unavailable, and rejects unknown
// formats.
bool canReuse(const QByteArray &protectedData);

// True when a platform secret service is usable: DPAPI on Windows, libsecret
// with a reachable keyring on Linux. False means the fallback storage is
// active.
bool isAvailable();

} // namespace SecretProtector
