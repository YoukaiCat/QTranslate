#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>

// Per-user configuration and state directories (see doc/architecture.md,
// §4.11). Linux follows the XDG Base Directory specification and uses the
// lowercase "qtranslate" directory; other platforms keep QStandardPaths.
namespace AppPaths {

inline QString configDirectory()
{
#if defined(Q_OS_LINUX)
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    // The XDG Base Directory specification says relative values are invalid;
    // treat them as unset so paths do not depend on the process CWD.
    if (base.isEmpty() || !QDir::isAbsolutePath(base))
        base = QDir(QDir::homePath()).filePath(QStringLiteral(".config"));
    return QDir(base).filePath(QStringLiteral("qtranslate"));
#else
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#endif
}

inline QString stateDirectory()
{
#if defined(Q_OS_LINUX)
    QString base = qEnvironmentVariable("XDG_STATE_HOME");
    if (base.isEmpty() || !QDir::isAbsolutePath(base))
        base = QDir(QDir::homePath()).filePath(QStringLiteral(".local/state"));
    return QDir(base).filePath(QStringLiteral("qtranslate"));
#else
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
#endif
}

} // namespace AppPaths
