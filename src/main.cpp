#include "app/appcontroller.h"
#include "app/logging.h"
#include "app/singleinstance.h"
#include "core/appinfo.h"
#include "core/settings.h"
#include "platform/secretprotector.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>

int main(int argc, char *argv[])
{
    QElapsedTimer startupTimer;
    startupTimer.start();

    QApplication app(argc, argv);
    // No organization name on purpose: data then lives in %APPDATA%\qtranslate.
    QApplication::setApplicationName(AppInfo::name());
    QApplication::setApplicationVersion(AppInfo::version());
    // The app lives in the tray; hiding the last window must not quit it.
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QApplication::translate("main", "Fast desktop translator."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption minimizedOption(
        QStringLiteral("minimized"),
        QApplication::translate("main", "Start hidden in the system tray."));
    parser.addOption(minimizedOption);
    parser.process(app);

    const bool startHidden = parser.isSet(minimizedOption);

    SingleInstance instance(SingleInstance::defaultKey());
    if (!instance.tryBecomePrimary(startHidden ? SingleInstance::Command::None
                                               : SingleInstance::Command::Activate)) {
        return 0;
    }

    SettingsStore settings;
    settings.setSecretCodec(
        {[](const QString &plainText) { return SecretProtector::protect(plainText); },
         [](const QByteArray &protectedData) {
             return SecretProtector::unprotect(protectedData);
         }});
    settings.load();

    AppController controller(&instance, &settings, AppController::Options{startHidden});
    controller.start();
    qCInfo(lcApp) << "Started in" << startupTimer.elapsed() << "ms";

    return QApplication::exec();
}
