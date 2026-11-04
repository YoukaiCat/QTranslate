#include "platform/secretprotector.h"

// Non-Windows target: no platform secret protection yet, so secrets are not
// stored at all.
namespace SecretProtector {

QByteArray protect(const QString &)
{
    return {};
}

QString unprotect(const QByteArray &)
{
    return {};
}

} // namespace SecretProtector
