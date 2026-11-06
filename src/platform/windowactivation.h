#pragma once

#include <QtGlobal>

// Helpers around the OS rules for bringing windows to the foreground and
// sending text input to the window the user is working in.
namespace Platform {

// Lets any other process take the foreground (Windows: AllowSetForegroundWindow).
// Called by a secondary instance before asking the primary to show its window.
// X11 and Wayland: no-op.
void allowAnyProcessToActivate();

// Native handle of the current foreground window, 0 if unavailable.
// X11: _NET_ACTIVE_WINDOW with an XGetInputFocus fallback; Wayland: 0.
quintptr foregroundWindow();

// True when another process owns a fullscreen foreground window (a game,
// fullscreen video, etc.). Used to release global hotkeys so they reach the
// game instead of being consumed by QTranslate.
// X11: _NET_WM_STATE_FULLSCREEN on a viewable window; Wayland: false.
bool isFullscreenForegroundWindow();

// True if `window` is still the foreground window. Used before pasting a
// translation, so a delayed answer cannot be inserted into another program.
bool isForegroundWindow(quintptr window);

// Sends Ctrl+V to the foreground window (Windows: SendInput; X11: XTest).
// False if the input could not be synthesized; always false on Wayland.
bool sendPasteShortcut();

} // namespace Platform
