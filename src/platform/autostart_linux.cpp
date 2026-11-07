#include "platform/autostart.h"

#include "core/appinfo.h"
#include "core/apppaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcAutostart, "qtranslate.autostart", QtWarningMsg)

namespace {

QString configHome()
{
    // AppPaths already validates and normalizes the XDG environment.
    return QFileInfo(AppPaths::configDirectory()).absolutePath();
}

QString desktopFilePath()
{
    return QDir(configHome())
        .filePath(QStringLiteral("autostart/qtranslate.desktop"));
}

// Desktop Entry quoting, not shell quoting: inside a quoted argument a
// backslash escapes '"', '`', '$' and '\', and '%' is doubled because field
// codes are expanded after unquoting.
QString quoteExecArgument(const QString &argument)
{
    QString quoted = argument;
    quoted.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    quoted.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    quoted.replace(QLatin1Char('`'), QStringLiteral("\\`"));
    quoted.replace(QLatin1Char('$'), QStringLiteral("\\$"));
    quoted.replace(QLatin1Char('%'), QStringLiteral("%%"));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QByteArray desktopFileContents()
{
    const QString command =
        Autostart::commandForExecutable(QCoreApplication::applicationFilePath());
    return QStringLiteral("[Desktop Entry]\n"
                          "Type=Application\n"
                          "Name=%1\n"
                          "Exec=%2\n"
                          "Icon=qtranslate\n"
                          "Terminal=false\n"
                          "X-GNOME-Autostart-enabled=true\n")
        .arg(AppInfo::name(), command)
        .toUtf8();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

namespace Autostart {

QString commandForExecutable(const QString &executablePath)
{
    return quoteExecArgument(executablePath) + QStringLiteral(" --minimized");
}

bool isEnabled()
{
    return QFileInfo::exists(desktopFilePath());
}

bool setEnabled(bool enabled)
{
    return sync(enabled);
}

bool sync(bool wanted)
{
    const QString path = desktopFilePath();

    if (!wanted) {
        if (!QFileInfo::exists(path))
            return true;
        if (!QFile::remove(path)) {
            qCWarning(lcAutostart) << "Cannot remove the desktop entry" << path;
            return false;
        }
        qCInfo(lcAutostart) << "Autostart entry removed";
        return true;
    }

    const QByteArray expected = desktopFileContents();
    if (QFileInfo::exists(path) && readFile(path) == expected)
        return true;

    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) {
        qCWarning(lcAutostart) << "Cannot create the autostart directory" << directory;
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcAutostart) << "Cannot write the desktop entry" << path;
        return false;
    }
    if (file.write(expected) != expected.size()) {
        qCWarning(lcAutostart) << "Cannot write the desktop entry" << path;
        return false;
    }
    qCInfo(lcAutostart) << "Autostart entry updated";
    return true;
}

bool isApproved()
{
    return true; // the XDG desktop entry has no Task Manager equivalent
}

} // namespace Autostart
