---
kind: task
status: todo
depends: [01-x11-xgrabkey]
---

# Wayland GlobalShortcuts portal

## Task

Add the Wayland backend for `HotkeyManager` through the
`org.freedesktop.portal.GlobalShortcuts` portal. Register the four `HotkeyAction` values with stable
shortcut IDs and user-visible descriptions, create a portal session, bind the shortcuts, map
`Activated` to `HotkeyManager::activated`, and handle `Deactivated` and `ShortcutsChanged` without
changing the public API. Close the session and the DBus connections on destruction.

This is the Wayland scope the user approved: the portal where the desktop provides it, Ctrl+Q with
manual copying, the CLI commands as the fallback, and explicit warnings. Do not add `ydotool`,
`wtype`, the RemoteDesktop portal, XTest, or any other synthetic input. Do not open an X11 display on
a Wayland session. If the portal is missing, the session or bind fails, or the desktop version is
too old, `registerHotkey` must return false with a clear reason so the UI can show one warning and
rely on CLI commands bound in the desktop settings.

## Done when

- `HotkeyManager` detects `org.freedesktop.portal.Desktop` and the `GlobalShortcuts` interface in the
  session bus; it creates a session with a unique handle token, binds the four actions, and emits
  `activated` on the portal's `Activated` signal.
- `HotkeyManager::hotkey()` reflects the bound shortcuts where the portal reports triggers, and an
  empty sequence where the desktop assigned one; `unregisterAll` and destruction close the session.
- If the portal, the session, or the bind is unavailable, registration fails with a clear reason,
  the application does not crash, and the CLI fallback (`--translate-popup`, `--translate-main`,
  `--translate-inplace`, wired in task 05) is unaffected.
- No synthetic input is added: no `ydotool`, `wtype`, RemoteDesktop portal, XTest, or fake key
  events. The X11 backend is not opened on Wayland.
- The fallback is verified under Weston headless (`weston --backend=headless-backend.so`,
  `QT_QPA_PLATFORM=wayland`): the app starts, registration reports the missing portal, and no
  synthetic input is attempted. The portal happy path is verified with a mock DBus service or on a
  portal-capable desktop (KDE/GNOME); if neither is available, the outcome records it as not
  performed and why.
- `tst_hotkeymanager` still passes on X11 under `xvfb-run` in Debian 13 (trixie); the 20 shared
  tests still
  pass; the build has no warnings; the documentation is the lead's.
