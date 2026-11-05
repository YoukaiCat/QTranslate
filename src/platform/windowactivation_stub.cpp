#include "platform/windowactivation.h"

namespace Platform {

void allowAnyProcessToActivate()
{
}

quintptr foregroundWindow()
{
    return 0;
}

bool isFullscreenForegroundWindow()
{
    return false;
}

bool isForegroundWindow(quintptr)
{
    return false;
}

bool sendPasteShortcut()
{
    return false;
}

} // namespace Platform
