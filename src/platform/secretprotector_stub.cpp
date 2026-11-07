#include "platform/secretprotector.h"

// Platforms without a secret backend: secrets are not stored at all.
namespace SecretProtector {

QByteArray protect(const QString &)
{
    return {};
}

QString unprotect(const QByteArray &)
{
    return {};
}

bool canReuse(const QByteArray &)
{
    return false;
}

bool isAvailable()
{
    return false;
}

} // namespace SecretProtector
