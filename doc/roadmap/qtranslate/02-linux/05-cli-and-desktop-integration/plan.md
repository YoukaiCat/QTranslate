---
kind: task
status: todo
depends: [02-global-hotkeys, 03-x11-selection-and-inplace, 04-xdg-autostart-and-secrets]
---

# CLI commands and Linux desktop integration

## Task

Add `--translate-popup`, `--translate-main`, and `--translate-inplace` as the portable way to
trigger the three scenarios, forward them to the running instance, and make the Linux desktop
behaviour explicit: tray availability, Wayland limitations, and the in-place clipboard fallback.
The CLI commands are platform-neutral and also work on Windows; the Linux-specific warnings are
only shown there.

The commands cover Wayland and desktops where a global hotkey cannot be bound: the user selects or
copies text, binds the command in the desktop's shortcut settings, and the running instance executes
it against the current active window. If no instance is running, the command starts the application,
executes the scenario once, and leaves it in the tray. A secondary instance must forward the command
over the existing single-instance socket and exit 0.

Wayland scope is unchanged: `GlobalShortcuts` portal where available, Ctrl+Q with manual copying,
CLI commands, and explicit warnings. Do not add `ydotool`, `wtype`, RemoteDesktop portal, or any
other synthetic input, and do not expand the stage.

## Done when

- `main.cpp` parses the three options (mutually exclusive; compatible with `--minimized` and
  `--verbose`); `SingleInstance::Command` carries them over the local socket; the secondary exits 0
  after forwarding; the primary executes the command once against the current active window.
- Starting with a scenario option and no running instance does not open the main window when a tray
  is available; it runs the scenario and stays in the tray. Without a tray it shows the main window
  so the user can see the result.
- Wayland: one startup warning says that global hotkeys need the `GlobalShortcuts` portal, that the
  selection must be copied manually when the portal is unavailable, and that in-place puts the
  translation on the clipboard instead of pasting. The warning is not repeated for every failed
  hotkey; no synthetic input is attempted.
- Tray: when `QSystemTrayIcon::isSystemTrayAvailable()` is false, the application shows the main
  window and a clear one-time warning instead of only logging; closing the window quits (the current
  behaviour) and the message explains why. The GNOME/AppIndicator note is left for the lead's
  documentation.
- X11: the three commands work with and without a running instance; Ctrl+Q/popup, Ctrl+W/main, and
  Ctrl+Shift+Q/in-place behave as before; the in-place command replaces the selection or copies the
  translation when the active window changed.
- The single-instance command forwarding and the CLI parsing are covered by a Linux-only test or a
  scripted check (two processes, a temporary lock/socket key); `tst_translations` passes with the new
  warning strings in all 14 `.ts` files; the 20 shared tests still pass; the build has no warnings.
- No `ydotool`/`wtype` or other synthetic input is added; the Wayland scope stays portal + manual
  copy + CLI + warnings.

## Risks

- `QCommandLineParser` treats unknown options as errors; a desktop shortcut with extra arguments must
  still work or fail with a clear message.
- The single-instance protocol is a line-based local socket; keep it backward compatible with the
  existing `activate`/`noop` messages and test the old and new paths.
- A command-started instance must not steal focus from the application the user is translating in;
  avoid `activateWindow()` before the grab, and do not call `Platform::allowAnyProcessToActivate()`
  for the translate commands.
- The tray warning and the Wayland warning add UI strings; all 14 `.ts` files must be updated or
  `tst_translations` fails.
