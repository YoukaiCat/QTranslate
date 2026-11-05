---
kind: task
status: todo
---

# X11 XGrabKey backend

## Task

Implement the X11 backend behind the existing `HotkeyManager` API. The manager must register global
hotkeys with `XGrabKey` on the root window, receive the key presses through its own Xlib display
connection and a `QSocketNotifier`, and keep the public contract unchanged: `registerHotkey`,
`unregisterHotkey`, `unregisterAll`, `setSuspended`, `hotkey`, the `activated` and
`registrationFailed` signals, and the `HotkeyAction` enum. The stub is no longer used on Linux but
stays for other non-Windows platforms.

`XGrabKey` is per-client, so the implementation can use a dedicated `Display*`; do not depend on
Qt's xcb native event filter. Support Control, Shift, Alt (`Mod1`), and Super (`Mod4`) plus the
Lock/NumLock variants so Caps Lock or Num Lock does not break a hotkey; detect `BadAccess` with a
scoped X error handler and `XSync`; ungrab on unregister, suspend, and destruction, and re-grab on
resume. Map `QKeySequence` to X11 keysym/keycode in this platform file; the shared
`core/hotkeysequence` helper is Windows-specific and stays untouched. Reject empty, modifier-only,
multi-chord, and unknown sequences.

## Done when

- `src/platform/hotkeymanager_linux.cpp` (or the X11 part of it) is selected on Linux in
  `CMakeLists.txt`; the stub is only compiled on other non-Windows platforms; the target links
  `X11::X11`.
- Ctrl+Q, Ctrl+Shift+Q, Ctrl+W, F-keys, letters, digits, arrows, and common navigation keys map to
  valid X11 hotkeys; empty, modifier-only, multi-chord, and unknown sequences fail through
  `registrationFailed` with a non-empty reason.
- Two managers cannot own the same combination: the second `registerHotkey` returns false and emits
  `registrationFailed` with the sequence and reason. Suspension releases the grab, another client can
  take the combination, and resuming re-grabs it; registration while suspended is deferred and
  applied on resume.
- The destructor and `unregisterAll` release every grab and close the display; no X errors or leaks
  appear in the test run.
- `tst_hotkeymanager` on Linux passes under `xvfb-run` in Debian 13 (trixie) and on the real X11
  session;
  the test `QSKIP`s when `XOpenDisplay` fails. The 20 shared tests still pass and the build has no
  warnings.
- On a Wayland session the X11 backend is not selected: `registerHotkey` returns false with a clear
  "global hotkeys are not available without the portal" reason and no X display is opened. The
  portal backend is the sibling task `02-wayland-globalshortcuts`.
- Manual check: with the application running on X11, `xdotool key ctrl+q` reaches
  `AppController::translateSelectionInPopup` (the popup or its "nothing is selected" state is
  observable) and the four configured actions are logged/triggered.
