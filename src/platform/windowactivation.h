#pragma once

#include <QtGlobal>

// Helpers around the OS rules for bringing windows to the foreground and
// sending text input to the window the user is working in.
namespace Platform {

// Lets any other process take the foreground (Windows: AllowSetForegroundWindow).
// Called by a secondary instance before asking the primary to show its window.
void allowAnyProcessToActivate();

// Native handle of the current foreground window, 0 if unavailable.
quintptr foregroundWindow();

// True if `window` is still the foreground window. Used before pasting a
// translation, so a delayed answer cannot be inserted into another program.
bool isForegroundWindow(quintptr window);

// Sends Ctrl+V to the foreground window (SendInput). False if the input could
// not be synthesized.
bool sendPasteShortcut();

} // namespace Platform
