#include "platform/autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QLoggingCategory>
#include <QSettings>

Q_LOGGING_CATEGORY(lcAutostart, "qtranslate.autostart", QtWarningMsg)

namespace {

constexpr auto kRunKey =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto kApprovedKey =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr auto kValueName = "qtranslate";

QSettings::Status writeValue(const QString &value)
{
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (value.isEmpty())
        run.remove(QString::fromLatin1(kValueName));
    else
        run.setValue(QString::fromLatin1(kValueName), value);
    run.sync();
    return run.status();
}

} // namespace

namespace Autostart {

QString commandForExecutable(const QString &executablePath)
{
    return QStringLiteral("\"%1\" --minimized")
        .arg(QDir::toNativeSeparators(executablePath));
}

bool isEnabled()
{
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    return run.contains(QString::fromLatin1(kValueName));
}

bool setEnabled(bool enabled)
{
    QString command;
    if (enabled) {
        command = commandForExecutable(QCoreApplication::applicationFilePath());
    }

    const QSettings::Status status = writeValue(command);
    if (status != QSettings::NoError) {
        qCWarning(lcAutostart) << "Cannot update the Run key, status" << int(status);
        return false;
    }
    qCInfo(lcAutostart) << (enabled ? "Autostart enabled" : "Autostart disabled");
    return true;
}

bool sync(bool wanted)
{
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    const QString expected = wanted
        ? commandForExecutable(QCoreApplication::applicationFilePath())
        : QString();
    const QString current =
        run.value(QString::fromLatin1(kValueName)).toString();

    if (wanted && current == expected)
        return true;
    if (!wanted && !run.contains(QString::fromLatin1(kValueName)))
        return true;

    if (wanted)
        run.setValue(QString::fromLatin1(kValueName), expected);
    else
        run.remove(QString::fromLatin1(kValueName));
    run.sync();

    if (run.status() != QSettings::NoError) {
        qCWarning(lcAutostart) << "Cannot sync autostart, status" << int(run.status());
        return false;
    }
    qCInfo(lcAutostart) << (wanted ? "Autostart entry updated" : "Autostart entry removed");
    return true;
}

bool isApproved()
{
    QSettings approved(QString::fromLatin1(kApprovedKey), QSettings::NativeFormat);
    const QByteArray value =
        approved.value(QString::fromLatin1(kValueName)).toByteArray();
    if (value.isEmpty())
        return true; // no Task Manager decision yet
    return quint8(value.at(0)) != 0x03;
}

} // namespace Autostart
