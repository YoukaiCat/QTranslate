#include "app/commandline.h"

#include <QCommandLineParser>
#include <QCoreApplication>

CommandLineOptions::CommandLineOptions()
    : minimizedOption(QStringLiteral("minimized"),
                      QCoreApplication::translate("main", "Start hidden in the system tray."))
    , verboseOption(
          QStringLiteral("verbose"),
          QCoreApplication::translate(
              "main", "Write verbose logs to the application log file."))
    , translatePopupOption(
          QStringLiteral("translate-popup"),
          QCoreApplication::translate("main",
                                      "Translate the selected text in a popup window."))
    , translateMainOption(
          QStringLiteral("translate-main"),
          QCoreApplication::translate("main",
                                      "Translate the selected text in the main window."))
    , translateInplaceOption(
          QStringLiteral("translate-inplace"),
          QCoreApplication::translate("main", "Translate the selected text and replace it."))
{
}

void CommandLineOptions::addTo(QCommandLineParser &parser) const
{
    parser.addOption(minimizedOption);
    parser.addOption(verboseOption);
    parser.addOption(translatePopupOption);
    parser.addOption(translateMainOption);
    parser.addOption(translateInplaceOption);
}

SingleInstance::Command CommandLineOptions::scenario(const QCommandLineParser &parser,
                                                    QString *error) const
{
    const int count = parser.isSet(translatePopupOption) + parser.isSet(translateMainOption)
        + parser.isSet(translateInplaceOption);
    if (count > 1) {
        if (error) {
            *error = QCoreApplication::translate(
                "main",
                "The --translate-popup, --translate-main and --translate-inplace options "
                "are mutually exclusive.");
        }
        return SingleInstance::Command::None;
    }

    if (parser.isSet(translatePopupOption))
        return SingleInstance::Command::TranslatePopup;
    if (parser.isSet(translateMainOption))
        return SingleInstance::Command::TranslateMain;
    if (parser.isSet(translateInplaceOption))
        return SingleInstance::Command::TranslateInplace;
    return SingleInstance::Command::None;
}

bool CommandLineOptions::isStartHidden(const QCommandLineParser &parser) const
{
    return parser.isSet(minimizedOption);
}

bool CommandLineOptions::isVerbose(const QCommandLineParser &parser) const
{
    return parser.isSet(verboseOption);
}