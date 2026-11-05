#include "core/appinfo.h"

#ifndef QTRANSLATE_VERSION
#error "QTRANSLATE_VERSION must be defined by the build system"
#endif

namespace AppInfo {

QString name()
{
    return QStringLiteral("QTranslate");
}

QString version()
{
    return QStringLiteral(QTRANSLATE_VERSION);
}

QString userAgent()
{
    return name() + QLatin1Char('/') + version();
}

} // namespace AppInfo
