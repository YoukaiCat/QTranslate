---
kind: task
status: done
detailed: true
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

Established facts, measured on this machine on 2026-11-15 (this session runs on the reported Astra
desktop, so they are observations, not assumptions):

- `DISPLAY=:0`, `XDG_CURRENT_DESKTOP=fly`, `DESKTOP_SESSION=fly`, `XDG_SESSION_TYPE=x11`,
  `qtpaths6 --query QT_VERSION` → `6.10.2`.
- The session bus has `org.kde.StatusNotifierWatcher` and `org.kde.StatusNotifierHost-1380692`;
  PID 1380692 is `fly-wm`, and the `flytray` window (`0xa00212`, 48x48, `_NET_WM_PID=1380692`)
  belongs to the same process. That window has no child windows while no legacy icon runs, which points
  to the panel's tray being a StatusNotifierItem host inside `fly-wm` rather than the XEmbed dock
  (step 3 confirms this rather than trusting it).
- Root `_NET_SUPPORTED` lists `_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR` and `_FLY_QT5_TRAY_ICON`; the root
  property `FLY_SYSTEM_TRAY_GEOMETRY_DP-2` was `3413, 2100, 427, 60` (the tray strip of the panel).
- A live foreign SNI item on this desktop answers
  `org.freedesktop.DBus.Properties.GetAll /StatusNotifierItem org.kde.StatusNotifierItem`, and its
  introspection lists `IconName` (`s`) and `IconPixmap` (`a(iiay)`).
- A 9 s `dbus-monitor --session "interface=org.freedesktop.DBus.Properties"` trace of the idle desktop
  recorded no property calls at all, so `fly-wm` does not poll every registered item on a timer while
  nothing happens; any re-read is triggered by a registration or a signal.
- Qt 6.10.2 here: `libQt6Gui.so.6` exports `QStatusNotifierItemAdaptor` and
  `RegisterStatusNotifierItem`; in qtbase 6.10 `QGenericUnixTheme::createPlatformSystemTrayIcon()`
  returns `QDBusTrayIcon` when the SNI watcher is registered and `nullptr` otherwise
  (`src/gui/platform/unix/qgenericunixtheme.cpp:109-114`, `:280-289`), and the xcb plugin directory
  has `qxcbsystemtraytracker.cpp` but no `qxcbsystemtrayicon.cpp`. So the tray on Fly is expected to
  be SNI, not XEmbed; step 3 confirms it instead of trusting this.
- In `QDBusTrayIcon` the item is registered in `init()` **before** the icon is published
  (`src/gui/platform/unix/dbustray/qdbustrayicon.cpp:124-129`, `:224-238`), and `IconName` stays empty
  outside Unity/indicator-application because `tempIcon()` only fires there (`:172-211`). The panel
  therefore gets the icon only from `IconPixmap`, in a window between the registration and the first
  `iconChanged`/`NewIcon`. This is the ordering the measured trace has to watch.
- `AppIcon::create()` (`src/ui/appicon.cpp:44-50`) renders nine pixmaps and asks for `Segoe UI`, which
  does not exist on Astra, so the first `QFont` lookup goes through fontconfig.

## Steps

1. Create the evidence directory `/tmp/tray-latency/` and record the baseline of the desktop before any
   code change: `dbus-send --session --print-reply /org/freedesktop/DBus org.freedesktop.DBus.ListNames`
   filtered with `grep -i statusnotifier`, `xwininfo -root -tree | grep -i flytray` (the
   `flytray` id is taken from here; it was `0xa00212` today), `xprop -id <flytray-id> _NET_WM_PID` and
   `xprop -root | grep FLY_SYSTEM_TRAY_GEOMETRY`, plus `pgrep -a qtranslate` and `pgrep -a fly-wm`.
   Write the output to `/tmp/tray-latency/evidence-baseline.txt`. The tray strip coordinates for the
   screenshots and the PID of the panel process are taken from here, not hardcoded.

2. Observation pass without code changes, three launches. Make sure no instance runs
   (`pgrep -a qtranslate`); a second instance only forwards the command and exits
   (`src/main.cpp:70-72`). For each run start, in this order and in parallel:
   `dbus-monitor --session "interface=org.kde.StatusNotifierWatcher,interface=org.kde.StatusNotifierItem,interface=org.freedesktop.DBus.Properties" > run-N/dbus.log`,
   a frame series of the tray strip
   (`for i in $(seq 1 120); do scrot -a 3413,2100,427,60 run-N/frames/$(printf %03d $i).png; done`,
   about 10 frames/s for 12 s), and the application with `--verbose`
   (`build/linux-release/qtranslate --verbose`, or `build/final-release/qtranslate`). The log goes to
   `$XDG_STATE_HOME/qtranslate/qtranslate.log` (`~/.local/state/qtranslate/qtranslate.log`), appended,
   with ISO timestamps in milliseconds (`src/app/logging.cpp:38-48`). From the three sources extract
   and write into `run-N/summary.txt`: t_launch, t_register (`RegisterStatusNotifierItem` in
   `dbus.log`), the index of the first frame in which the QTranslate icon is drawn, and the
   `Started in N ms` line
   (`src/main.cpp:87`). Compare the frame series against frame 001 and against a shot taken with
   qtranslate stopped. Report whether the 3-5 s gap reproduces and its size; if it does not reproduce,
   repeat with the Debug binary once and record the result honestly.

3. Identify the tray backend that actually runs and what the panel receives. In the same run:
   - confirm SNI by looking for `qt.qpa.tray` messages. `Logging::initialize` calls
     `QLoggingCategory::setFilterRules("qtranslate.*=true")` (`src/app/logging.cpp:80`), so pass
     `QT_LOGGING_RULES="qt.qpa.tray.debug=true"`; if the lines stay absent, temporarily add
     `qt.qpa.tray.debug=true` to that `setFilterRules` string and remove it in step 9. Expected lines:
     `registering org.kde.StatusNotifierItem-<pid>-1` and the `updateIcon` line.
   - find the item name (`StatusNotifierItem-<pid>-`) on the bus and read its properties at about
     +0.3 s and +5 s after the registration:
     `dbus-send --session --print-reply --dest=<name> /StatusNotifierItem org.freedesktop.DBus.Properties.GetAll string:org.kde.StatusNotifierItem`.
     Record whether `IconPixmap` is non-empty at the first read and whether `IconName` is empty.
   - check the watcher's view: `dbus-send --session --print-reply --dest=org.kde.StatusNotifierWatcher /StatusNotifierWatcher org.freedesktop.DBus.Properties.Get string:org.kde.StatusNotifierWatcher string:RegisteredStatusNotifierItems`.
   - from `dbus.log` list every call `fly-wm` makes to the item and its timing: `GetAll` at
     registration, any `Get IconPixmap`, and whether anything happens after `NewIcon`.
   - cross-check XEmbed is not involved: the `flytray` window still has no children, and no
     `qxcbsystemtray*` registration appears.
   Write the result to `run-N/backend.txt`.

4. Classify the cause with the numbers from steps 2-3 and write the reasoning down before touching
   code. The cases are exclusive:
   - **A — late `show()`**: t_register is late (more than ~1 s after t_launch) and the pre-`show()`
     stages are slow. The slow stage is named in step 5.
   - **B — the panel draws late**: t_register is early (under ~300 ms), the watcher has the item, and
     the first frame with the icon is 3-5 s later. Inside B, distinguish:
     - **B1 — the panel reads an empty `IconPixmap` first and never re-reads it**: `fly-wm` calls
       `GetAll` in the window between the registration and `updateIcon`, so the slot is created empty
       and is filled only at the panel's next refresh (the Qt-internal ordering of step 3). This is a
       QTranslate-side race we can break.
     - **B2 — the panel reads a correct pixmap and draws it late**: `IconPixmap` is non-empty at the
       first read and the frame comes seconds later. This is the panel's own refresh interval —
       external, no code workaround.
   - **C — the icon build/theme lookup is slow**: the delay is inside `AppIcon::create()` or a `QIcon`
     theme lookup, visible from the per-stage stamps of step 5.
   Record which case holds and the exact evidence lines in `run-N/verdict.txt`.

5. Only if case A or C holds, add temporary per-stage stamps and rerun once. Use one `QElapsedTimer`
   created at the top of `AppController::start()` and `qCInfo(lcApp)` lines before and after each
   stage: `AppIcon::create()`/`QApplication::setWindowIcon()` (`src/app/appcontroller.cpp:111-112`),
   `TrayIcon::isAvailable()` (`:114`), `applyUiLanguage()` (`:116`), `createTranslation()`/
   `createProofreading()` (`:118-119`), `MainWindow` construction, `restoreGeometry`/`restoreState` and
   `initMainWindowLanguages()` (`:123-130`), `updateProofreadingUi()` (`:135`), `new TrayIcon` (`:138`),
   `m_tray->show()` (`:146`), `updateProofreadingUi()` again (`:154`), `createHotkeys()` (`:169`) and
   `Autostart::sync()` (`:176`). Also stamp `SettingsStore::load()` (`src/main.cpp:83`), the
   `SingleInstance` construction (`:70-72`) and `AppController::start()` itself (`:86`). Diff the stamp
   log against the same run without stamps and confirm the stamps themselves add no visible delay.

6. Only if case B1 or B2 needs to be separated from a QTranslate-specific cause, build a throwaway
   probe outside the repository (`/tmp/tray-probe/main.cpp`, a 30-line `QApplication` with a
   `QSystemTrayIcon` fed a plain filled `QPixmap`, same `QT_LOGGING_RULES`), build it against the
   system Qt 6.10.2 (`g++ … $(pkg-config --cflags --libs Qt6Widgets)` or a two-line `CMakeLists.txt`),
   run the same frame series, and compare its t_icon − t_register with QTranslate's. Do not add it to
   the repository and do not reference it from `CMakeLists.txt`. If case B2 is already unambiguous
   from step 3, skip this probe.

7. Apply exactly one minimal fix, chosen by the verdict, and nothing else:
   - case A: create the tray before the slow stages, i.e. move the block
     `src/app/appcontroller.cpp:137-154` up to right after `TrayIcon::isAvailable()` (`:114`). Keep the
     invariants: the signal connections may be made early (no signal can arrive before
     `QApplication::exec()`), but `updateServiceSelectors()`/`setServices()` stay after
     `createTranslation()` (`:170`), and the Wayland `showMessage` block (`:156-167`) stays after the
     tray. Verify that `updateProofreadingUi()` is called after both the tray and `createProofreading()`
     and that the `m_trayAvailable` fallback at `:147-151` still runs before `MainWindow` is queried
     for `setCloseHidesToTray()` (`:124`) — if moving the tray above `MainWindow` breaks that order,
     move only `createTranslation()`/`createProofreading()`/`MainWindow` work down instead, and keep
     the diff inside `start()`.
   - case B1: re-publish the icon after the registration from inside `TrayIcon::show()`
     (`src/ui/trayicon.cpp:59-62`) instead of changing `QSystemTrayIcon` usage elsewhere: store the
     `QIcon` passed to the constructor (private member, no public API change) and call
     `m_tray->show(); m_tray->setIcon(m_icon);` so `NewIcon` is emitted after `RegisterStatusNotifierItem`.
     A `QTimer::singleShot(0, ...)` variant is acceptable only if the immediate repeat proves
     insufficient in the measurement.
   - case B2: no code change. Write the panel interval and the measured numbers into the outcome, and
     hand the wording for `doc/handoff.md` to the lead.
   - case C: hoist or cache the slow call (`AppIcon::create()` result reused as in
     `src/app/appcontroller.cpp:111-112`, or the `QFont` family changed to a present one) without
     changing the icon's look.
   Constraints for every case: no public API change in `TrayIcon`/`AppController`, no new dependency,
   no new `tr()` string (if one appears, stop and report it), no `Q_OS_*` branch added, and no change
   to the Windows or Wayland paths beyond what compiling the same code implies.

8. Verify. Debug and Release builds of the touched tree: `cmake --build build/linux-debug` and
   `cmake --build build/linux-release` (or fresh `build/tray-debug`, `build/tray-release`), zero
   warnings with `-Wall -Wextra`; `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`
   → 50/50. Repeat the step 2 measurement three times on the fixed binary and record t_register and
   the first-frame index: target t_register ≤ 300 ms after t_launch and the first frame with the icon
   within 150 ms of t_register (one refresh at 10 fps), or the external interval exactly as profiled in
   case B2. Check the tray still works by hand: left click toggles the window, right click opens the
   menu, the `Service` submenu lists the providers with the active one checked, `Check text…` follows
   `proofreading/enabled`, `Pause hotkeys` toggles, `Settings…` and `Quit` work, and the tooltip shows
   the hotkey sequences. Check the fallback path once with `QT_QPA_PLATFORM=offscreen` (no tray: the
   existing warning at `src/app/appcontroller.cpp:148` must still appear and the application must not
   crash) and, if Weston is available, one Wayland launch for the
   `Limited Wayland support` message. Windows is not built here: record it as reviewed by diff, and keep
   the diff to the two files named in step 7.

9. Clean up and record. Remove every temporary stamp and the extra `qt.qpa.tray` filter rule unless one
   log line is worth keeping for good (then justify it in the outcome and keep exactly one).
   `git diff --stat` must show only the files the chosen case needs. Put the verdict, the quoted
   evidence lines (log timestamps, `dbus-monitor` times, frame indices, panel PID and desktop name),
   the three-run numbers and the manual check into `## Outcome` for the coordinator; documentation
   edits in `doc/` stay with the lead.

## Done when

- The cause is identified with evidence in `## Outcome`: the late component is named (a stage of
  `AppController::start()`, the SNI registration in `TrayIcon::show()`, or the Fly panel), with
  timestamps in milliseconds, the tray backend (StatusNotifierItem, with the `fly-wm` PID and the
  `org.kde.StatusNotifierItem-<pid>-N` name), and the three-run numbers from step 2 and step 8.
- On Astra with `XDG_CURRENT_DESKTOP=fly` the icon is drawn on the first panel refresh after
  `TrayIcon::show()`: t_register ≤ 300 ms after launch and t_icon − t_register ≤ 150 ms measured with
  the 10 fps frame series, in three consecutive launches; or the delay is proven external, the panel's
  refresh interval is quoted with the measurement that proves it, and no workaround was added to the
  code.
- `git diff` touches only `src/app/appcontroller.cpp` and/or `src/ui/trayicon.cpp` (plus nothing else);
  no public `TrayIcon`/`AppController` signature changed, no new dependency, no new `tr()` string, no
  added `Q_OS_*` branch.
- Debug and Release build without warnings on the host, and all 50 Linux tests pass
  (`xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`).
- The tray still works on the other Linux paths: the no-tray warning and the Wayland message paths were
  exercised or recorded as not performed with the reason; Windows is recorded as reviewed by diff only
  (no MSVC here).
- The manual check on Astra is recorded as performed, with `XDG_CURRENT_DESKTOP=fly`, `fly-wm` as the
  tray host, Qt 6.10.2, and the commands used; or as not performed with the reason.

## Risks and what is needed from the user

- The bug was seen only on Astra; this environment is that Astra, and the desktop matches the report
  (`fly`, X11, Qt 6.10.2), but the delay itself was not reproduced by the planner, so a single launch
  is not evidence. Measure three runs and check a cold start after `fly-wm` has seen no SNI
  registration for a while.
- The tray path here appears to be StatusNotifierItem hosted by `fly-wm` (confirmed in step 3); the fix
  must not assume XEmbed. The `flytray` window belongs to `fly-wm` and stays empty because legacy icons
  are not used, which is why an XEmbed check would be misleading.
- Qt publishes the icon only as `IconPixmap` (`IconName` is empty outside Unity), and registers the item
  before the icon in `QDBusTrayIcon`. If `fly-wm` reads the property before `updateIcon`, the empty slot
  is a Qt/panel interaction, and a re-published `NewIcon` is the only honest in-app mitigation; a
  repeated timer that keeps re-setting the icon would be a workaround and is not acceptable.
- Astra ships a patched Qt: `libQt6XcbQpa` contains `Fly/TrayIconThemeName` and the root
  `_NET_SUPPORTED` lists `_FLY_QT5_TRAY_ICON`. If the per-stage stamps blame the icon or a theme lookup,
  the patch is the place to look before changing our code.
- Reordering `start()` is the riskiest candidate: the tray slots reach `m_translations`, `m_mainWindow`
  and `m_proofreading`, and `updateProofreadingUi()` is already called twice on purpose
  (`src/app/appcontroller.cpp:135`, `:154`). Keep both calls, keep `updateServiceSelectors()` after the
  translations exist, and keep the Wayland message after the tray.
- Screen measurements depend on the panel layout: read `FLY_SYSTEM_TRAY_GEOMETRY_DP-2` before every run
  instead of hardcoding `3413, 2100, 427, 60`, and keep a reference frame from a launch with qtranslate
  stopped.
- What is needed from the user: nothing to start. If the verdict turns out to be B2 (the panel's own
  refresh interval), a decision on whether the limitation is acceptable is the user's, but the
  measurement itself does not block on it.

## Outcome

The task is done as an investigation: the cause is named and measured, and **no code changed**. The
empty tray slot lasts exactly while the StatusNotifierItem is in `NeedsAttention`, and it is
QTranslate's own startup notification that puts it there; the Fly panel does not paint an item in that
state. `git diff` touches only this plan and the journal.

### Environment and backend

Astra Linux 1.8.7, X11, `XDG_CURRENT_DESKTOP=fly`, `DESKTOP_SESSION=fly`, Qt 6.10.2. The tray is
StatusNotifierItem, not XEmbed: `org.kde.StatusNotifierWatcher` and
`org.kde.StatusNotifierHost-1380692` are on the session bus, `fly-wm` (PID 1380692) owns the `flytray`
window `0xa00212` (`xprop -id 0xa00212 _NET_WM_PID` = 1380692), that window has 0 children during a
launch, and no `qxcbsystemtray*` registration appears. The tray strip was read before every run
(`FLY_SYSTEM_TRAY_GEOMETRY_DP-2 = 3413, 2100, 427, 60`). Every run combined `dbus-monitor --session`,
a frame series of the tray strip at ~20 fps, `xwd` snapshots of the item's own window, and `--verbose`.
All times below are from process start.

### Startup is not late (cases A and C are out)

`RegisterStatusNotifierItem` at 0.054-0.073 s, `Started in 62-106 ms`, and the item is on the bus with
seven pixmaps (16/20/24/32/40/48/64) and an empty `IconName`. `fly-wm` makes its first property read
0.200 s after registration and reads a complete `IconPixmap` at +0.247 s. `AppIcon::create()` and the
fontconfig lookup for the absent `Segoe UI` family are not on the critical path.

### The mechanism

With the startup notification this desktop produces:

```
0.072  RegisterStatusNotifierItem
0.094  NewStatus "NeedsAttention"      <- AppController's "Hotkey unavailable" showMessage()
0.272  fly-wm: Get Status        -> "NeedsAttention"
0.273  fly-wm: Get IconPixmap    -> non-empty (7 pixmaps)
       ... no call from fly-wm to the item at all ...
5.330  NewStatus "Active"              <- the 5000 ms lifetime of TrayIcon::showMessage()
5.550  first frame with the icon on screen (frame #112, run-8)
```

The panel paints the item 0.202-0.247 s after the status returns to `Active`, in all six runs, and
makes no call to it between the first read and that moment — there is no polling interval to quote.
The 5 s is the `5000` of `src/ui/trayicon.cpp:66`: Qt turns the `msecs` argument into the status
change, and `rg '5000' src/` finds no other 5 s timer in the tray path.

Without the notification (portable copy, every hotkey sequence emptied so `registerHotkey()` is never
called; 0 `Notify` calls, 0 "Hotkey" lines, no `NewStatus` at all):

```
0.072  RegisterStatusNotifierItem
0.272  fly-wm: Get Status/IconName/... -> "Active"
0.319  fly-wm: Get IconPixmap
0.412  first frame with the icon (frame #014)
```

| run | config | t_register | first NewStatus | first screen frame with icon | t_icon − t_register |
|-----|--------|-----------|-----------------|------------------------------|---------------------|
| 1 | user config | 0.072 | 0.098 NeedsAttention | 5.197 s (#105) | 5.125 s |
| 2 | user config | 0.072 | 0.100 NeedsAttention | 5.613 s (#114) | 5.541 s |
| 3 | portable, Ctrl+Alt+F9..F12 (also refused) | 0.071 | 0.093 NeedsAttention | 5.563 s (#110) | 5.491 s |
| 7 | user config | 0.073 | 0.100 NeedsAttention | 5.096 s (#104) | 5.023 s |
| 8 | user config | 0.072 | 0.094 NeedsAttention | 5.546 s (#112) | 5.474 s |
| 9 | user config | 0.054 | 0.075 NeedsAttention | 5.528 s (#134) | 5.474 s |
| clean-1 | hotkeys unassigned | 0.072 | none | 0.412 s (#014) | 0.340 s |
| clean-2 | hotkeys unassigned | 0.072 | none | 0.413 s (#014) | 0.341 s |
| clean-3 | hotkeys unassigned | 0.071 | none | 0.449 s (#015) | 0.378 s |

### Case B1 is disproved

Re-publishing the icon from `TrayIcon` after registration (temporary patch in `src/ui/trayicon.cpp`
selected by `QTR_TRAY_REPUBLISH_MS`, reverted) emitted `NewIcon` at 0.072 s and again at 0.500 s; the
panel re-read the pixmap 7 ms after the second one and still painted at 5.367 s and 5.582 s. The
plan's candidate fix (`m_tray->show(); m_tray->setIcon(m_icon);`) does not help here. The panel does
ignore the first `NewIcon`, because it arrives between `RegisterStatusNotifierItem` and the watcher's
own `StatusNotifierItemRegistered`, but a later repeat of the same signal changes nothing.

### The "Done when" criteria, honestly

- Cause identified with evidence: **met** — the late component is the panel's paint of the item, held
  back by the `NeedsAttention` status that our warning sets, with timestamps, the SNI bus name, the
  `fly-wm` PID and nine runs.
- `t_register ≤ 300 ms` and `t_icon − t_register ≤ 150 ms` in three launches: t_register is met
  (0.054-0.073 s); the 150 ms is **not**, and it is not reachable by reordering. A plain start takes
  0.340-0.378 s, of which 0.200 s is the panel's own first property read, 0.047 s its read of
  `IconPixmap`, and 0.093 s its paint after that. Under 150 ms would need re-publishing the icon on a
  timer, which the plan rules out as a workaround and which was measured not to work anyway.
- "Delay proven external, the panel's refresh interval quoted": the delay is rendered externally, but
  **there is no refresh interval** — the panel reacts to a status change within 0.2 s and does not
  poll. The measured trigger is recorded instead, and no workaround was added to the code.
- Diff scope: **met** — only this plan and the journal; nothing in `src/`, no new dependency, no new
  `tr()` string, no added `Q_OS_*` branch, no public signature change.
- Debug and Release without warnings, 50 Linux tests: the tree is unchanged since the last green build
  (`ninja: no work to do` for both), and `xvfb-run -a ctest --test-dir build/linux-release
  --output-on-failure` reports 50/50.
- The other Linux paths: `QT_QPA_PLATFORM=offscreen` still logs
  `System tray is not available; closing the window quits` (`src/app/appcontroller.cpp:148`) and does
  not crash; a launch under a nested weston shows the `Limited Wayland support` notification and the
  GlobalShortcuts portal failure path. Windows was not built (no MSVC) and there is no code diff to
  review by.
- Manual check on Astra: **performed** with `XDG_CURRENT_DESKTOP=fly`, `fly-wm` as the tray host and
  Qt 6.10.2. Left click toggles the main window, right click opens the menu, the `Service` submenu
  lists the 10 providers with the active one checked, `Check text…` follows `proofreading/enabled`,
  `Pause hotkeys` toggles, `Settings…` opens the dialog, `Quit` exits, the tooltip shows the hotkey
  sequences.

### What is left, and why

- This desktop will keep showing the empty slot for ~5 s at every start until either the hotkey
  warnings stop or the warning is delivered without `NeedsAttention` (one warning instead of three,
  delayed delivery, `NoIcon`, or another channel). All of those change what the user sees, so the
  choice is the user's and no code was changed here.
- The panel never asked for `AttentionIconPixmap` (three `AttentionIconName` reads, no pixmap read),
  so it could not draw an attention icon in any case. That is the vendor-side half of the defect and
  belongs in a report to Astra, not in an application workaround.
- Separately, and unrelated to the tray: on this desktop no global hotkey can be registered at all
  (`Ctrl+Q`, `Ctrl+Shift+Q`, `Ctrl+W` and `Ctrl+Alt+F8..F12` are all refused as "already used by
  another program"), which is why every launch here shows three warnings. The user's report says the
  hotkeys start normally for them, so the two may or may not be the same defect; it needs its own
  task. Measured support: with the user's config the three failed registrations are followed
  immediately by three `showMessage` calls, and a start without notifications takes 0.34 s.
- `AppIcon::create()` asks for `Segoe UI`, which does not exist on Astra. It is harmless for this
  defect (the icon is ready before registration) and was left alone.

