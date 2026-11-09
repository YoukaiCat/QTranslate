#pragma once

#include "app/singleinstance.h"

#include <QCommandLineOption>
#include <QString>

class QCommandLineParser;

// The command-line options shared by main() and the CLI tests.
//
// Build the struct after QCoreApplication exists: the descriptions are
// translated, so they must not be created at static-initialization time.
struct CommandLineOptions
{
    CommandLineOptions();

    void addTo(QCommandLineParser &parser) const;

    // The scenario selected by --translate-popup, --translate-main,
    // --translate-inplace or --proofread-selection. Returns Command::None when
    // none or several are set; several set *error to the translated
    // mutual-exclusion message.
    SingleInstance::Command scenario(const QCommandLineParser &parser, QString *error) const;

    bool isStartHidden(const QCommandLineParser &parser) const;
    bool isVerbose(const QCommandLineParser &parser) const;

    QCommandLineOption minimizedOption;
    QCommandLineOption verboseOption;
    QCommandLineOption translatePopupOption;
    QCommandLineOption translateMainOption;
    QCommandLineOption translateInplaceOption;
    QCommandLineOption proofreadSelectionOption;
};