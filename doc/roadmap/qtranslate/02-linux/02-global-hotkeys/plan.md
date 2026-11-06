---
kind: stage
status: done
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

## Outcome

Accepted. Both children are done: X11 `XGrabKey` (`5ef937e`) and the Wayland `GlobalShortcuts` portal
(`1fbc6e3`). The public `HotkeyManager` API, signals, `HotkeyAction`, `core/hotkeysequence.*`,
`hotkeymanager_win.cpp`, `AppController`, and the UI are unchanged; the X11 code moved
byte-equivalently to `src/platform/hotkeymanager_linux.cpp` and dispatches by the constructor-cached
session type. The portal backend creates one session with `QUuid::Id128` tokens, sends one batched
`BindShortcuts` for the four stable IDs, handles `Activated`/`Deactivated`/`ShortcutsChanged`, reports
asynchronous bind failures through `registrationFailed`, treats suspend logically, returns an empty
`hotkey()` after the desktop assigns triggers, and closes the session on `unregisterAll`/destruction.
No synthetic input is added, and `XOpenDisplay` is not called on a Wayland session.

Verification: trixie Debug/Release 22/22 with 0 warnings; Astra Debug/Release 22/22 after
`sudo /usr/bin/fly-set-execid.sh`; `tst_hotkeymanager_x11` 16/16 and `tst_hotkeymanager_wayland` 20/20;
Weston headless starts with the portal+CLI reason and no X11 connection. The real KDE/GNOME portal was
not performed (Astra X11; Weston has no portal) and is covered by the mock DBus test; Windows was
reviewed by diff only (no MSVC). The acceptance made no commits; the tree was clean at `bd7410b`.

Known gaps and follow-ups: the tray text still says "already used by another program" for a missing
Wayland portal (task 06); the tray tooltip can show the configured sequence before an asynchronous bind
completes or after the desktop reassigns it (task 06/07); two latent signal-reentrancy edges remain
(`activated` on X11 and `registrationFailed` on Wayland, no current caller); the X11 `xtestActivation`
still masks a registration failure as a skip; `XOpenDisplay` can flake under load; `SettingsDialog`'s
validator still uses the Win32 helper on Linux; and the `setSuspended` header comment describes
X11/Windows, not the logical Wayland pause.

Facts for the remaining work: Linux has 22 tests, Windows keeps 23; `Qt6::DBus` is Linux-only in
`qtranslate` and the tests, while `qtranslate_core` stays D-Bus-free; `libqt6dbus6` is the runtime
dependency for task 07 and `dbus`/`dbus-daemon` the test dependency; `libx11-6` comes with Qt's xcb
plugin, and `libx11-dev`/`libxtst-dev` are build/test dependencies. After every rebuild on Astra run
`sudo /usr/bin/fly-set-execid.sh`, otherwise `tst_hotkeymanager_x11` skips on Parsec.
