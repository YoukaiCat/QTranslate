---
kind: stage
status: in-progress
base: 11742fd
depends: [01-linux-build-baseline]
---

# Global hotkeys on Linux

## Goal

Register Ctrl+Q (popup), Ctrl+Shift+Q (translate and replace), Ctrl+W (main window), and the
optional show-window shortcut on Linux. X11 is the full target through `XGrabKey`. On Wayland,
use the `org.freedesktop.portal.GlobalShortcuts` portal where the desktop provides it; when the
portal is absent, report the limitation clearly and leave the CLI commands and desktop shortcuts as
the fallback. Do not synthesize keyboard input on Wayland.

## Done when

- On X11, all four actions register, `registrationFailed` reports conflicts and unsupported
  sequences, suspension releases and restores the grabs, and the existing `HotkeyManager` contract
  and signals stay unchanged.
- On Wayland with a `GlobalShortcuts` portal, the actions bind and `Activated` emits the same
  `HotkeyManager::activated`; without the portal or when a session fails, registration returns false
  with a clear reason, a bind failure after the asynchronous request is reported through
  `registrationFailed`, and the application keeps working through CLI commands.
- `tst_hotkeymanager_x11` and `tst_hotkeymanager_wayland` pass under `xvfb-run` in Debian 13
  (trixie); the Wayland fallback is verified under a headless Wayland compositor (Weston), and the
  portal happy path with the mock DBus service; a real portal-capable desktop is recorded as not
  performed with the reason when unavailable.
- No `ydotool`, `wtype`, RemoteDesktop portal, XTest, or other synthetic input is added under
  Wayland. No X11 display is opened on a Wayland session.
- The 20 shared tests and both Linux hotkey tests (22 total) pass, the build has no warnings, and
  Windows is reviewed for `#ifdef` safety but not compiled here.
