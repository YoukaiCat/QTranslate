#include "app/appcontroller.h"
#include "app/logging.h"
#include "app/singleinstance.h"
#include "core/appinfo.h"
#include "core/settings.h"
#include "platform/secretprotector.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // The installer and uninstaller detect a running copy through this named
    // mutex (AppMutex in packaging/windows/qtranslate.iss). The handle stays
    // open until the process exits; Windows releases it automatically.
    const HANDLE installerMutex = CreateMutexW(nullptr, FALSE, L"Local\\qtranslate-app");
    Q_UNUSED(installerMutex);
#endif

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
    const QCommandLineOption verboseOption(
        QStringLiteral("verbose"),
        QApplication::translate("main", "Write verbose logs to the application log file."));
    parser.addOption(verboseOption);
    parser.process(app);

    const bool startHidden = parser.isSet(minimizedOption);
    Logging::initialize(parser.isSet(verboseOption));

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
