#pragma once

#include <QByteArray>
#include <QString>

// Encrypts secrets for the INI file (see doc/architecture.md, §4.11).
// Windows uses DPAPI; other platforms have a stub that refuses to store them.
namespace SecretProtector {

// Raw protected bytes; an empty result means failure.
QByteArray protect(const QString &plainText);

// Empty if the data is corrupted or belongs to another user/machine.
QString unprotect(const QByteArray &protectedData);

} // namespace SecretProtector
