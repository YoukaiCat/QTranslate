#pragma once

#include <QString>

// Autostart through the per-user Run registry key on Windows and through an
// XDG desktop entry on Linux (see doc/architecture.md, §4.12). The header has
// no platform-specific types.
namespace Autostart {

// Windows: '"C:\...\qtranslate.exe" --minimized'
// Linux: a Desktop Entry quoted executable followed by --minimized.
QString commandForExecutable(const QString &executablePath);

bool isEnabled();
bool setEnabled(bool enabled);

// Brings the platform autostart entry in line with the setting and updates the
// executable path if the program was moved. Returns false if the entry could
// not be written or removed.
bool sync(bool wanted);

// False if Windows disabled the entry in Task Manager (StartupApproved). The
// XDG desktop entry has no equivalent, so Linux returns true.
bool isApproved();

} // namespace Autostart
