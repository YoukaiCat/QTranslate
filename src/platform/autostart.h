#pragma once

#include <QString>

// Autostart through the per-user Run registry key (see doc/architecture.md,
// §4.12). The header has no platform-specific types.
namespace Autostart {

// '"C:\...\qtranslate.exe" --minimized'
QString commandForExecutable(const QString &executablePath);

bool isEnabled();
bool setEnabled(bool enabled);

// Brings the registry in line with the setting and updates the executable path
// if the program was moved. Returns false if the registry could not be written.
bool sync(bool wanted);

// False if Windows disabled the entry in Task Manager (StartupApproved).
bool isApproved();

} // namespace Autostart
