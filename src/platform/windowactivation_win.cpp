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

bool isFullscreenForegroundWindow()
{
    const HWND window = GetForegroundWindow();
    if (!window || !IsWindowVisible(window) || IsIconic(window))
        return false;

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == GetCurrentProcessId())
        return false;

    RECT windowRect = {};
    if (!GetWindowRect(window, &windowRect))
        return false;

    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
        return false;

    // Maximized windows also cover the work area; a fullscreen window covers
    // the whole monitor and normally has no caption/thick frame.
    const RECT &screen = info.rcMonitor;
    constexpr LONG tolerance = 2;
    const bool coversMonitor =
        windowRect.left <= screen.left + tolerance
        && windowRect.top <= screen.top + tolerance
        && windowRect.right >= screen.right - tolerance
        && windowRect.bottom >= screen.bottom - tolerance;
    if (!coversMonitor)
        return false;

    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    const bool hasCaption = (style & WS_CAPTION) == WS_CAPTION;
    const bool hasThickFrame = (style & WS_THICKFRAME) == WS_THICKFRAME;
    return !(hasCaption && hasThickFrame);
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
