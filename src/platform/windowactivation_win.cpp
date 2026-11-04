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

quintptr foregroundWindow()
{
    return reinterpret_cast<quintptr>(GetForegroundWindow());
}

bool isForegroundWindow(quintptr window)
{
    return window != 0
        && GetForegroundWindow() == reinterpret_cast<HWND>(window);
}

bool sendPasteShortcut()
{
    INPUT inputs[4] = {};
    for (INPUT &input : inputs) {
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_CONTROL;
    }
    // Ctrl down, V down, V up, Ctrl up.
    inputs[1].ki.wVk = 'V';
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(4, inputs, sizeof(INPUT)) == 4;
}

} // namespace Platform
