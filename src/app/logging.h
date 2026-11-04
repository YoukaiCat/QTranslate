#pragma once

#include <QLoggingCategory>
#include <QString>

// Application-level logging. Only warnings are printed by default; enable more
// with QT_LOGGING_RULES="qtranslate.*=true".
Q_DECLARE_LOGGING_CATEGORY(lcApp)

namespace Logging {

// Enables verbose qtranslate logging and duplicates it to
// %LOCALAPPDATA%\qtranslate\qtranslate.log. No-op when `verbose` is false.
void initialize(bool verbose);

QString filePath();

} // namespace Logging
