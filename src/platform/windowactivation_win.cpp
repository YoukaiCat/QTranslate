#include "platform/windowactivation.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace Platform {

void allowAnyProcessToActivate()
{
    AllowSetForegroundWindow(ASFW_ANY);
}

} // namespace Platform
