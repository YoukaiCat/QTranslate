#include "platform/autostart.h"

namespace Autostart {

QString commandForExecutable(const QString &executablePath)
{
    return QStringLiteral("\"%1\" --minimized").arg(executablePath);
}

bool isEnabled()
{
    return false;
}

bool setEnabled(bool)
{
    return false;
}

bool sync(bool)
{
    return false;
}

bool isApproved()
{
    return true;
}

} // namespace Autostart
