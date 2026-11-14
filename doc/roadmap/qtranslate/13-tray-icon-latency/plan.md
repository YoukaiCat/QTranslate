---
kind: task
status: todo
detailed: false
---

# Tray icon appears 3-5 seconds late on Astra

## Task

User report (2026-11-15): on Astra Linux (1.8.7, X11, Qt 6.10.2), when QTranslate starts, the system
tray shows an empty slot for 3-5 seconds before the icon appears. The main window and the hotkeys start
normally; only the tray icon is late.

Reproduce on Astra and find which component is late before changing anything:

- whether `QSystemTrayIcon` is created early in `AppController::start()`/`MainWindow` and when
  `setIcon()`/`show()` run relative to the blocking startup work (hotkey registration, autostart
  sync, settings load, secret protection, network/DBus probes);
- whether the tray uses StatusNotifierItem over DBus or the legacy XEmbed dock on this desktop, and
  when the item is registered vs when the panel first draws it;
- whether the icon pixmap/theme lookup or the resource load is slow;
- whether the panel itself polls the tray on a timer, which would make the delay external.

Record the cause with evidence (timestamps, `--verbose` logs, panel/desktop name, tray backend), then
fix the ordering or the setup so the icon appears with the first tray refresh. If the delay is the
desktop panel's polling interval, record it as an external limitation and do not add a code workaround.

## Done when

- The cause is identified with evidence and written into the outcome (which component was late, and
  why it was not visible earlier).
- On Astra the icon is visible at the first tray refresh after launch, or the delay is proven external
  and documented with the panel's behaviour.
- The tray still works on the other Linux desktops and on Windows by diff; Debug and Release build
  without warnings and all Linux tests pass.
- The manual check on Astra is recorded as performed with the desktop name, or as not performed with
  the reason.

## Risks and what is needed from the user

- The bug was seen only on Astra; this environment may not reproduce it. The fix needs a real Astra
  session and the desktop name (Fly/MATE/XFCE and the panel's tray implementation).
- Tray implementations differ: StatusNotifierItem over DBus and XEmbed behave differently, and an
  Astra-specific change must not break the other path.
- If the panel polls the tray, the 3-5 seconds may be its interval; in that case the honest result is a
  documented limitation, not a workaround in QTranslate.
