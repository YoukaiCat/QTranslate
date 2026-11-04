#pragma once

#include <QString>

// Static information about the application: name, version, User-Agent.
namespace AppInfo {

// Internal application name. Also used as QCoreApplication::applicationName,
// which determines settings and data locations.
QString name();

// Semantic version from CMake's project(VERSION ...), e.g. "0.9.1".
QString version();

// User-Agent for outgoing HTTP requests, e.g. "qtranslate/0.9.1".
QString userAgent();

} // namespace AppInfo
