#pragma once

#include <QGuiApplication>
#include <QString>
#include <QtGlobal>

namespace Platform {

// The session rule is checked before any X11 call: an XWayland session with
// QT_QPA_PLATFORM=xcb still counts as Wayland, as the Linux stage requires.
// It is deliberately environment-based because the platform plugin name alone
// does not distinguish XWayland from a native X11 session.
inline bool isWaylandSession()
{
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        return true;
    if (qEnvironmentVariable("XDG_SESSION_TYPE").compare(QLatin1String("wayland"),
                                                         Qt::CaseInsensitive)
        == 0) {
        return true;
    }
    // The GUI application reports the selected platform plugin. The guiless
    // hotkey test has no QGuiApplication, so this check is skipped there.
    return qGuiApp && qGuiApp->platformName().startsWith(QLatin1String("wayland"),
                                                         Qt::CaseInsensitive);
}

} // namespace Platform
