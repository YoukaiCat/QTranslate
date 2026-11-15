---
kind: task
status: done
detailed: true
depends: [13-tray-icon-latency]
---

# Deliver the hotkey warning through the log and the settings dialog

## Task

Node `13-tray-icon-latency` proved that the empty tray slot for ~5 s after every start is QTranslate's
own doing: `HotkeyManager::registrationFailed` is answered with `TrayIcon::showMessage()`
(`src/app/appcontroller.cpp:354-369`), and a tray balloon puts the StatusNotifierItem into
`NeedsAttention` for the 5000 ms of `src/ui/trayicon.cpp:64-66`; the Fly panel does not paint an item
in that state, so the slot stays blank until the status returns to `Active` (plan `13`, `## Outcome`,
sections "The mechanism" and "Case B1 is disproved").

User decision (2026-11-15, journal "User decisions after the tray investigation"): the warning is not
delivered as a tray balloon, and there is no startup popup at all. It goes to the `--verbose` log and
to a hint in the settings dialog. Rejected: one non-modal message box in the style of the no-tray and
Wayland warnings, one shortened 2 s balloon, and the current behaviour.

Scope of this task:

- Remove only the hotkey-failure balloon. `TrayIcon::showMessage()` keeps its public signature and
  every other caller: the `Autostart` failures (`src/app/appcontroller.cpp:179`, `:753`), the
  `Limited Wayland support` startup message (`:156-167`), `Check the selection` (`:1433`) and
  `Translate and replace` (`:1491`).
- Keep the existing `qCWarning(lcApp) << "Hotkey" << keys << "is unavailable:" << reason;` line — it
  is the log half of the decision. The log file exists only with `--verbose`
  (`src/app/logging.cpp:55-82`: `initialize(false)` returns without installing the handler).
- Collect the failed combinations so the settings dialog can show them, and show them there as a red
  hint in the style of the existing `secretStorageHint` (`src/ui/settingsdialog.cpp:86-93`,
  `:103`, test `secretStorageHint` at `tests/tst_settingsdialog.cpp:1013`).
- Do not change `TrayIcon`, `HotkeyManager` or any platform file; no new dependency, no `Q_OS_*`
  branch, no new settings key.
- Windows is built by nobody here: it is reviewed by diff, and nothing in the change may depend on
  the platform.

Decisions taken while planning, and why:

- **The manager has no "registration succeeded" signal.** `src/platform/hotkeymanager.h:54-58`
  declares only `activated` and `registrationFailed`; a success appears only as an `lcHotkey` log line
  (`src/platform/hotkeymanager_linux.cpp:1744`, `src/platform/hotkeymanager_win.cpp:173`), and
  `registerHotkey` returns `bool`, but the Wayland portal fails *asynchronously* later
  (`src/platform/hotkeymanager_linux.cpp:1080-1108`) where the return value proves nothing. So the
  state is rebuilt from attempts instead of patched from a signal: the list is wiped at startup, the
  entry of an action is dropped **before** every new attempt for that action, and a failure records it
  again. Nothing has to be un-recorded on success.
- **The hint retries the failed combinations when it is about to be shown.** That is how an entry
  disappears when the combination became free later (the user closed the other program) without a
  success signal: opening `Settings` re-attempts exactly the recorded actions once, before the dialog
  reads the list. Only actions that are already recorded are retried, so a working registration is
  never disturbed. Skipped while the manager is suspended (a fullscreen app is in the foreground,
  `HotkeyManager::setSuspended` would only remember the sequence and report nothing) and on Wayland,
  where a retry would re-open the portal's shortcut dialog; there the list refreshes at the next
  launch or settings change.
- **The failure reason is not rendered.** It is either an untranslated English literal
  (`src/platform/hotkeymanager_linux.cpp:568`, `:1713`) or a raw OS error string
  (`winErrorString`, `src/platform/hotkeymanager_win.cpp:17-32`), so putting it into the dialog
  would mix untranslated and OS-language text into the UI. It stays in the `--verbose` log, which is
  where the user decision puts it; the hint only names the combinations and points at the log.
- The collected list therefore stores action id → sequence text; the reason is not carried.
- The collection lives in a small `qtranslate_core` class (`src/core/hotkeywarnings.h`) so that the
  fragile part — forgetting an entry before a retry and re-recording on failure — is unit-testable.
  `AppController` itself has no test target, so a private `QMap` there would leave the rule untested.
  The action is kept as `int` because `HotkeyAction` is declared in `src/platform/hotkeymanager.h`
  and core must not depend on the platform layer; the comment says so.

## Steps

1. **Add the record of failed registrations: `src/core/hotkeywarnings.h` and
   `src/core/hotkeywarnings.cpp`** (new files, no Qt Widgets; style of `src/core/hotkeysequence.*`):

   ```cpp
   // Global hotkeys whose last registration attempt failed. The key is
   // int(HotkeyAction): HotkeyAction belongs to the platform layer, so core
   // keeps it as a number. There is no "registered" signal; an entry is dropped
   // before the next attempt for its action and re-added if that attempt fails.
   class HotkeyWarnings
   {
   public:
       void clear();                                       // a fresh start pass
       void forget(int action);                            // before a new attempt
       void record(int action, const QString &sequence);   // an empty text is ignored
       bool isEmpty() const;
       QList<int> actions() const;                         // ascending, i.e. HotkeyAction order
       QStringList sequences() const;                      // same order, duplicates dropped

   private:
       QMap<int, QString> m_sequences;
   };
   ```

   `sequences()` must return the texts sorted by action id and must not contain an empty or a
   duplicated text (two actions can never legitimately share a combination — `SettingsDialog::
   validate()` rejects duplicates — but deduplicating keeps the label sane if a hand-edited ini
   manages it). Register the two files in the `qtranslate_core` source list at `CMakeLists.txt:40-54`
   (between `src/core/hotkeysequence.*` and `src/core/languages.*`).

2. **`src/app/appcontroller.h`**: `#include "core/hotkeywarnings.h"`, a member
   `HotkeyWarnings m_hotkeyWarnings;` next to `m_hotkeys` (`:157`), and one new private method declared
   beside `updateTrayToolTip()`/`showSettings()` (`:76`, `:79`):
   `void retryUnavailableHotkeys();`. No public API and no signal change.

3. **`src/app/appcontroller.cpp`, `createHotkeys()` (`:308-388`)**:

   - Clear the record once per start, before anything is registered: put
     `m_hotkeyWarnings.clear();` right after `m_hotkeys = new HotkeyManager(this);` (`:315`). The
     following `setSuspended(true)` (`:316-317`) registers nothing, so it cannot produce a failure.
   - Rewrite the `registrationFailed` lambda (`:354-369`): name the first parameter
     `HotkeyAction action`, keep the `keys`/`qCWarning` lines exactly, add
     `m_hotkeyWarnings.record(static_cast<int>(action), keys);`, and delete the whole balloon part —
     the `if (m_waylandSession) return;` guard and the `m_tray->showMessage(...)` block. The `tr()`
     strings `Hotkey unavailable` and `%1 is already used by another program. Close that program or
     choose another combination in the settings.` disappear from this file; their `.ts` entries become
     vanished and are dropped in step 8. Remove the now stale comment about the Wayland startup
     warning only if the surrounding code no longer explains anything — the Wayland message itself
     stays at `:156-167` and keeps its `showMessage`.

4. **`src/app/appcontroller.cpp`, `applySettings()` (`:587-619`)**: in the `updateHotkey` lambda, after
   the `oldSequence == newSequence` early return and **before** the unregister/register branch, call
   `m_hotkeyWarnings.forget(static_cast<int>(action));`. Both branches matter: an emptied combination
   unregisters without any signal, so its entry must go too; a changed combination either re-records
   itself through the signal or silently succeeds. Leave the six calls, the fullscreen block and
   `updateTrayToolTip()` untouched — an action whose sequence did not change keeps its recorded
   failure, which is correct, because nothing was attempted for it.

5. **`src/app/appcontroller.cpp`, new `retryUnavailableHotkeys()`** (place it next to
   `updateTrayToolTip()`):

   ```cpp
   void AppController::retryUnavailableHotkeys()
   {
       if (!m_hotkeys || m_hotkeyWarnings.isEmpty())
           return;
       // A suspended manager only remembers the sequence, and a Wayland retry
       // would show the desktop's shortcut dialog again.
       if (m_hotkeys->isSuspended() || m_waylandSession)
           return;
       const QList<int> actions = m_hotkeyWarnings.actions();
       for (const int action : actions) {
           m_hotkeyWarnings.forget(action);
           const QKeySequence sequence = m_hotkeys->hotkey(static_cast<HotkeyAction>(action));
           if (!sequence.isEmpty())
               m_hotkeys->registerHotkey(static_cast<HotkeyAction>(action), sequence);
       }
   }
   ```

   `HotkeyManager::hotkey()` returns the last attempted sequence for an action whose registration
   failed (`src/platform/hotkeymanager_linux.cpp:1661-1672`, `src/platform/hotkeymanager_win.cpp:124-128`;
   only a desktop-assigned Wayland trigger returns an empty one, which the guard covers).
   A failure inside the loop re-records itself through the existing connection; no extra logging is
   needed because `registerHotkey` already logs `Registered`/`Cannot register` per attempt.

6. **`src/app/appcontroller.cpp`, `showSettings()` (`:765-875`)**: inside the existing
   `if (m_hotkeys)` block, call `retryUnavailableHotkeys();` and pass the result to the dialog right
   after the dialog is constructed (`:788-792`):

   ```cpp
   retryUnavailableHotkeys();
   dialog.setUnavailableHotkeys(m_hotkeyWarnings.sequences());
   ```

   The dialog is modal and hotkeys are applied only after `Accepted`, so there is no live update to
   make; the list is correct on every opening. Do not touch the secret-storage warning below
   (`:859-869`) and do not add a message box on accept.

7. **`src/ui/settingsdialog.h` / `.cpp`: the hint.** Follow `m_secretStorageHint` exactly
   (`:86-93`, `:103`):

   - Header: a private member `QLabel *m_hotkeyWarning = nullptr;` next to `m_secretStorageHint`
     (`src/ui/settingsdialog.h:101`) and a public method below `validate()`: `void
     setUnavailableHotkeys(const QStringList &sequences);` (`QStringList` is already included at
     `:8`).
   - `buildUi()`, Hotkeys page: create the label in `hotkeysPage` after the `hotkeysHint` label
     (`:141-148`), object name `hotkeyWarning`, `setWordWrap(true)`,
     `setStyleSheet(QStringLiteral("color: #d32f2f;"))`, `setVisible(false)`, and
     `hotkeysLayout->addWidget(m_hotkeyWarning);` after `hotkeysHint`. Keep the trailing
     `addStretch()` last.
   - The setter: empty list → `setText(QString())` and `setVisible(false)`; otherwise
     `tr("Unavailable shortcuts: %1. They will not work; choose other combinations, or run with "
     "--verbose to see why.").arg(sequences.join(QStringLiteral(", ")))` and `setVisible(true)`. The
     label always exists once the constructor has returned (`buildUi()` runs in it), so no null check
     is needed — the other result setters of this dialog have none either.
     Do not claim "already used by another program": the same message appears on Wayland, where the
     reason is a missing portal (`src/platform/hotkeymanager_linux.cpp:1421`, `:1505`), which is
     exactly the misleading wording stage 2 recorded as a known gap.

   That is **one** new source string; no other `tr()` is added or changed.

8. **Regenerate the `.ts` and translate the new string.** Copy the 14 `i18n/qtranslate_*.ts` to
   `/tmp/ts-before-14/` first, then from the repository root configure a build directory and run the
   lupdate target (never the Qt 5 `lupdate` from `PATH`):

   `cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release`
   `cmake --build build/linux-release --target update_translations`

   Each file starts at **384** messages. After the target: the two removed strings are `type="vanished"`
   and the new one is present with `type="unfinished"`. The target runs `lupdate` without
   `-no-obsolete` (`build/*/build.ninja`, the `qtranslate_lupdate` rule), so drop the two vanished
   entries with the Qt 6 lupdate that target used, never the Qt 5 one from `PATH`:
   `/<qt6-prefix>/bin/lupdate -no-obsolete -project build/linux-release/.lupdate/qtranslate_lupdate_project.json`
   (in the trixie image that is `/usr/lib/qt6/bin/lupdate`). Then translate the new message in all 14
   files (`ru_RU es de fr pt_BR it pl uk tr zh_CN ja ko ar hi`), keeping `%1` and `--verbose`
   byte-identical and clearing `type="unfinished"`. Result per file: **383** messages, 0 unfinished,
   0 vanished, no empty translation — `tst_translations` (`tests/tst_translations.cpp:84-121`)
   enforces all of that plus the `%1` placeholder, so a mistranslated placeholder fails the suite.
   If the count or the vanished set differs from this expectation, stop and report the actual diff
   instead of editing around it.

9. **Tests.** Two places:

   - New `tests/tst_hotkeywarnings.cpp` (register it as `qtranslate_add_test(tst_hotkeywarnings)` in
     `tests/CMakeLists.txt`, alphabetically after `tst_hotkeysequence` at `:35`; it links
     `qtranslate_core` automatically, no Widgets, no extra properties). Cases: starts empty; records
     and lists in action order (`record(3), record(0), record(5)` → `actions()` `0,3,5` and the same
     sequence order); `forget()` drops one entry and leaves the others; `clear()` empties everything
     and `isEmpty()` follows; recording the same action twice keeps one entry with the newer text;
     an empty sequence text is ignored; two actions with the same text yield one item in
     `sequences()`; after `forget()` + a simulated successful registration (no `record()`) the entry
     stays gone — that is the whole "success without a signal" rule.
   - `tests/tst_settingsdialog.cpp`: add `void hotkeyWarningHint()` next to `secretStorageHint()`
     (`:1013`). Construct the dialog with the four required arguments, find
     `QLabel *hint = dialog.findChild<QLabel *>(QStringLiteral("hotkeyWarning"))`, and assert:
     `QVERIFY(hint); QVERIFY(hint->isHidden());` then `dialog.setUnavailableHotkeys({"Ctrl+Q",
     "Ctrl+W"})` → `QVERIFY(!hint->isHidden())`, text contains `Ctrl+Q`, `Ctrl+W`, `--verbose`;
     `QVERIFY(!hint->text().contains(QStringLiteral("already used")))` (the wording rule of step 7);
     `dialog.setUnavailableHotkeys({})` → `isHidden()` again and an empty text. The dialog is not
     shown in this test, as in `secretStorageHint()`, so `isHidden()` is the right predicate.

   Test counts after this task: **51 Linux CTest targets** (50 + `tst_hotkeywarnings`) and **45
   Windows** (44 + the same platform-neutral test); `tst_settingsdialog` goes from 35 slots to 36
   (`Totals: 37 passed` → `Totals: 38 passed`). The `.ts` count is **383** messages per file — the
   coordinator must report that number to the lead, because `AGENTS.md:58` and the comment at
   `src/services/libretranslateservice.cpp:408` still say 382; those edits are not part of this task.

10. **Build and test.** Prefer the `qtranslate-trixie` image for the loop and run the final check on
    the host: `cmake --build build/linux-debug`, `cmake --build build/linux-release`, grep both logs
    for `warning:` (`-Wall -Wextra` clean), then
    `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure` → **51/51**
    (`ctest -N` reports 51) and the same for the debug tree; `tst_settingsdialog`,
    `tst_hotkeywarnings`, `tst_translations` and `tst_hotkeymanager*` green. A trixie Release run also
    proves that its Qt 6.8 `lrelease` accepts the files written by Qt 6.10 `lupdate`. Windows Debug
    and Release and its 45 tests are recorded as not performed (no MSVC), reviewed by diff.
    `git status --porcelain` must show only `CMakeLists.txt`, `tests/CMakeLists.txt`,
    `src/core/hotkeywarnings.{h,cpp}`, `src/app/appcontroller.{h,cpp}`, `src/ui/settingsdialog.{h,cpp}`,
    `tests/tst_hotkeywarnings.cpp`, `tests/tst_settingsdialog.cpp` and the 14 `.ts`.

11. **Manual check on Astra (this machine: `DISPLAY=:0`, `XDG_CURRENT_DESKTOP=fly`, Qt 6.10.2).**
    Record every command and its output in `/tmp/hotkey-warning/`.

    - **Trust first — this is a precondition, not a defect.** A rebuilt binary loses the XPARSEC
      `security.exec_id`, so *every* `XGrabKey` is refused with the "already used by another program"
      reason and the check would prove nothing. Check `cat /etc/X11/trusted.d/qtranslate` (it lists
      the `build/linux-debug` and `build/linux-release` binaries) and run
      `sudo /usr/bin/fly-set-execid.sh` (passwordless sudo here). Prove it by launching with
      `--verbose` and reading `Registered Ctrl+Q …` in `$XDG_STATE_HOME/qtranslate/qtranslate.log`
      (`src/platform/hotkeymanager_linux.cpp:1744`). If instead the X server log says
      `keylogger-grabber`, fix the trust and repeat; do not report it as a bug of this task.
    - **Reproduce an unavailable combination without third-party programs.** Run the ordinary
      instance from the build tree (it holds `Ctrl+Q`, `Ctrl+Shift+Q` and `Ctrl+W`), then start the
      same binary a second time as a separate primary:
      `mkdir -p /tmp/qtprobe/config /tmp/qtprobe/state` and
      `USER=probe TMPDIR=/tmp/qtprobe XDG_CONFIG_HOME=/tmp/qtprobe/config
      XDG_STATE_HOME=/tmp/qtprobe/state build/linux-release/qtranslate --verbose`. `SingleInstance`
      keys its lock file and socket on `AppInfo::name() + "-" + $USER` in `QDir::temp()`
      (`src/app/singleinstance.cpp:63-99`, `:74-77`), so a different `USER`/`TMPDIR` gives a real
      second instance on the same display, and its three grabs fail with `BadAccess`
      (`src/platform/hotkeymanager_linux.cpp:564-573`) — no foreign program involved. Record the
      three `is unavailable` lines from `/tmp/qtprobe/state/qtranslate/qtranslate.log`. If the second
      instance still forwards to the first, fall back to a combination the desktop itself has grabbed
      (a Fly panel shortcut such as `Ctrl+Alt+T`/`Ctrl+Alt+L`) and record which one; a throwaway
      `XGrabKey` probe outside the repository (`/tmp/...`, never referenced from `CMakeLists.txt`) is
      the last resort.
    - **Tray: the icon must be there from the first paint.** Read the strip fresh
      (`xprop -root | grep FLY_SYSTEM_TRAY_GEOMETRY`, it was `3413, 2100, 427, 60`),
      `dbus-monitor --session "interface=org.freedesktop.Notifications,interface=org.kde.StatusNotifierItem" > notify.log`
      before the launch, and take `scrot -a X,Y,W,H` frames at ~0.5 s and ~2 s. Expected: no
      `Notify` call, no `NewStatus … NeedsAttention`, and the probe instance's icon visible on the
      first frame of the strip next to the primary's. Also check a start that has no failures at all
      (a temporary config that assigns free combinations such as `Ctrl+Alt+F9`): one icon, drawn on
      the first frame, no `NewStatus` at all.
    - **The hint.** In the probe instance: `Settings` → `Hotkeys` shows the red `hotkeyWarning` label
      with `Ctrl+Q, Ctrl+Shift+Q, Ctrl+W`, word-wrapped, and no message box appeared at startup.
      Screenshot the dialog.
    - **It clears when the combination works again.** Quit the primary, then in the probe's dialog
      change the three combinations to free ones (for example `Ctrl+Alt+F9/F10/F11`), accept it, and
      confirm `Registered …` lines in the log. Reopen `Settings` → `Hotkeys`: the label is gone. Also
      confirm one negative case: with everything registered, opening and cancelling the dialog leaves
      the label hidden.
    - **The untouched paths.** `QT_QPA_PLATFORM=offscreen build/linux-release/qtranslate` still logs
      `System tray is not available; closing the window quits` and shows the non-modal
      `System tray unavailable` message (`src/app/appcontroller.cpp:148`, `:1506-1513`); a nested
      weston launch still shows the `Limited Wayland support` balloon (`:156-167`); for the autostart
      warning either exercise it once (a bogus autostart state) or state honestly in the outcome that
      it was only read. Delete the temporary `HOME`/`XDG_CONFIG_HOME`/`TMPDIR` trees and leave no
      instance running.

12. **Hand-off.** Do not edit `doc/`, `README.md`, `README.ru.md` or `AGENTS.md` in this task. The
    lead's documentation pass must cover `doc/architecture.md:565-567` ("`AppController` shows a tray
    notification suggesting another combination" is no longer true), the message count (383) and the
    behaviour note in `doc/handoff.md`; the coordinator reports the measured numbers in `## Outcome`
    and commits. Nothing is pushed.

## Done when

- `src/app/appcontroller.cpp` no longer calls `TrayIcon::showMessage()` for a hotkey failure
  (`rg -n "showMessage" src/app/appcontroller.cpp` lists only the Wayland startup message, the two
  `Autostart` messages, `Check the selection` and `Translate and replace`), and the
  `qCWarning(lcApp) << "Hotkey" …` line is still there and still carries the reason.
- A failed registration is recorded per action, forgotten before the next attempt of that action, and
  retried once when the settings dialog is opened (not while suspended, not on Wayland); the unit
  test of `HotkeyWarnings` proves the record/forget/order/dedup rules.
- `SettingsDialog::setUnavailableHotkeys()` shows the red `hotkeyWarning` label in the Hotkeys page
  with the recorded combinations and hides it for an empty list; `tst_settingsdialog` covers both
  states; no other `tr()` text, widget, object name or layout in that dialog changed.
- The 14 `.ts` files have **383** messages, the one new source string translated, 0 unfinished,
  0 vanished, and `tst_translations` passes.
- Debug and Release build on the host with zero `-Wall -Wextra` warnings and all **51** Linux tests
  pass (`xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`); Windows 45 is
  recorded as not performed (no MSVC) and reviewed by diff; the diff touches only the files listed in
  step 10.
- The Astra manual check is recorded as performed — with the re-trust step, the way the failure was
  reproduced, the tray frames showing the icon from the first paint, no notification at all, the hint
  with its three combinations, the hint gone after the fix, and the no-tray/Wayland paths — or as not
  performed with the exact reason.

## Risks and what is needed from the user

- **The trust attribute is the main trap.** Without `sudo /usr/bin/fly-set-execid.sh` after the build
  every combination is refused (the state that produced the 13 investigation's warnings), the hint
  would look "correct" for the wrong reason, and the tray check would be meaningless. Re-trust first
  and prove it from the `Registered` lines.
- **The second-instance reproduction depends on `SingleInstance`'s key.** If overriding `USER` or
  `TMPDIR` does not produce a second primary, do not conclude the feature is broken: use the
  panel-owned combination or the throwaway probe and say in the outcome which method was used.
- **`applySettings()` must forget before both branches.** Forgetting only before `registerHotkey`
  leaves a stale entry for a combination the user emptied; forgetting only on failure cannot work,
  because success has no signal. The unit test covers the rule, the manual step covers the wiring.
- **A retry inside the dialog can surprise.** It runs `registerHotkey` for up to six actions when
  `Settings` opens, which logs one line per attempt. `HotkeyManager` is not re-created, and no
  retry happens for actions that are working, so the visible behaviour is limited to the recorded
  ones. If a retry makes a *previously* unrecorded action fail (impossible today, since only recorded
  actions are retried), that would be a defect to report.
- **The Wayland hint can be stale by design.** Because the retry is skipped there, a portal that
  starts working mid-session is only reflected after the next launch or settings change. Recorded as a
  limitation, not fixed: a retry would re-open the desktop's shortcut dialog.
- **`tst_translations` is strict about vanished entries.** The lupdate target alone leaves the two
  removed strings as `type="vanished"` (385 `<message>` elements); `-no-obsolete` from the same Qt 6
  lupdate is what brings the count to 383. Never accept 385 and never hand-delete the two `<message>`
  blocks without re-running lupdate afterwards.
- **The label is only refreshed when the dialog opens.** The user sees the effect of a change on the
  next opening — accepted, because the dialog owns the change and no modal message may appear. If the
  user expected a live label inside the open dialog, that would be a different feature.
- Nothing is needed from the user to start: the decision is recorded, sudo works without a password,
  and this machine is the Astra desktop the report came from. The one thing the user must know is the
  repeated re-trust step after each build.

## Outcome

The task is done as planned and no decision was revisited. A failed hotkey registration is no longer
announced by a tray balloon: it keeps the `qCWarning` line of the `--verbose` log and is collected per
action, to be shown as a red hint on the Hotkeys page of the settings dialog. `git diff` touches
`CMakeLists.txt`, `tests/CMakeLists.txt`, `src/core/hotkeywarnings.{h,cpp}` (new),
`src/app/appcontroller.{h,cpp}`, `src/ui/settingsdialog.{h,cpp}`, `tests/tst_hotkeywarnings.cpp` (new),
`tests/tst_settingsdialog.cpp` and the 14 `i18n/qtranslate_*.ts` — the list of step 10, and nothing
else in `src/`.

### What was built

- `HotkeyWarnings` (`src/core/hotkeywarnings.{h,cpp}`) holds action id → sequence text: `clear()`,
  `forget(action)`, `record(action, sequence)`, `isEmpty()`, `actions()`, `sequences()` (ascending
  action order, empty and duplicate texts dropped). The `int` key is documented in the header.
- `AppController`: `m_hotkeyWarnings` is cleared once in `createHotkeys()` right after
  `new HotkeyManager(this)`, written in the `registrationFailed` lambda next to the unchanged
  `qCWarning`, forgotten in `applySettings()` before both branches of `updateHotkey`, and retried once
  by the new `retryUnavailableHotkeys()` — skipped when the manager is suspended and on Wayland.
  `showSettings()` calls the retry and then `setUnavailableHotkeys()` inside the existing
  `if (m_hotkeys)` block.
- `SettingsDialog`: the `hotkeyWarning` label (word-wrapped, `#d32f2f`, hidden) after `hotkeysHint` and
  the public `setUnavailableHotkeys()`. One new source string, two removed.
- Tests: `tst_hotkeywarnings` (8 cases: empty state, action and text order, `forget()`, `clear()`,
  re-record with a newer text, ignored empty text, duplicate texts, "an entry stays gone after a
  successful retry") and `hotkeyWarningHint()` in `tst_settingsdialog`.

### Measured numbers

| what | before | after |
|------|--------|-------|
| Linux CTest targets | 50 | **51** (`xvfb-run -a ctest --test-dir build/linux-release`) |
| `tst_settingsdialog` | 35 slots, `Totals: 37 passed` | **36 slots, `Totals: 38 passed`** |
| `tst_hotkeywarnings` | — | **10 passed** (8 cases + init/cleanup) |
| `.ts` messages per file (14 files) | 384 | **383**, 0 unfinished, 0 vanished, no empty translation |
| `-Wall -Wextra` warnings, host Debug and Release | 0 | **0** |
| Windows CTest targets | 44 | 45 (derived, **not run**: no MSVC here — reviewed by diff) |

`git diff` was run through both trees: 51/51 in `build/linux-release` and in `build/linux-debug`.
`%1` and `--verbose` survive in all 14 translations, so `tst_translations` is the guard for them.

### Manual check on Astra, and its precondition

Astra Linux 1.8.7, X11, `DISPLAY=:0`, `XDG_CURRENT_DESKTOP=fly`, Qt 6.10.2, StatusNotifierItem hosted
by `fly-wm`. Artifacts are in `/tmp/hotkey-warning/`.

**The trust attribute is a precondition, not a defect.** A rebuilt binary loses the XPARSEC
`security.exec_id`, and then every `XGrabKey` is refused with "already used by another program", which
would make the check prove the wrong thing. After the build `sudo /usr/bin/fly-set-execid.sh` was run
again (passwordless), and the log showed `Registered "Ctrl+Q" for action 0`, `"Ctrl+Shift+Q"` and
`"Ctrl+W"` at 17:03:50. This repeats after *every* build on this machine and belongs to Astra/Parsec,
not to the product.

- **Reproduction without a third-party program.** A second primary with
  `USER=probe TMPDIR=/tmp/qtprobe XDG_CONFIG_HOME=/tmp/qtprobe/config XDG_STATE_HOME=/tmp/qtprobe/state`
  produced three `Hotkey "Ctrl+Q" is unavailable: "The hotkey is already used by another program."`
  lines in `/tmp/qtprobe/state/qtranslate/qtranslate.log` while the first instance held the
  combinations. The `SingleInstance` key behaved as the plan predicted.
- **The tray no longer blanks.** Four launches were traced with `dbus-monitor` (two separate match rules,
  see the deviations): **0** `Notify` calls and **0** `NewStatus … NeedsAttention`; the item reports
  `Status=Active` from the start. Frames of the tray strip show no empty slot at any moment — the second
  icon is painted 0.34-0.58 s after `NewIcon`, which is the panel's own add animation.
- **The hint.** `Settings → Hotkeys` in the probe instance shows the red word-wrapped line
  `Недоступные сочетания: Ctrl+Q, Ctrl+Shift+Q, Ctrl+W. Они не будут работать; выберите другие
  сочетания или запустите с --verbose, чтобы узнать причину.` (`big-fail-hotkeys2.png`), and no message
  box appeared at startup.
- **The hint clears.** With the first instance killed, reopening `Settings` retried the three
  combinations, logged `Registered …` at 17:09:52 and showed no hint (`big-fixed-hotkeys.png`).
- **A healthy start.** A single instance with the default combinations registers all three, and
  `Settings → Hotkeys` has no hint; opening and cancelling the dialog changes nothing.
- **The untouched paths.** `QT_QPA_PLATFORM=offscreen` still logs `System tray is not available; closing
  the window quits`; a nested weston launch still takes the `Limited Wayland support` branch (the
  GlobalShortcuts portal is unreachable inside the nested compositor — `AccessDenied` — which is
  expected there). The Autostart warning was only read, not exercised: it needs a broken autostart
  state and no code path to it changed.
- Cleanup: `pgrep -a qtranslate` is empty, no windows remain, `/tmp/qtprobe` and the weston directory
  are gone, and the repository holds no temporary file.

### Where the plan's text had to be adjusted

- `dbus-monitor --session "interface=A,interface=B"` is rejected by this session bus
  (`Key interface specified twice in match rule`); two separate match rules were used instead.
- `Ctrl+Alt+F9/F10/F11` are virtual-terminal switches on this Astra, not free combinations: pressing
  `Ctrl+Alt+F9` in the dialog blanked the screen and needed `sudo chvt 7` to come back. The healthy path
  was therefore checked with the default combinations on a single instance.
- The icon row extends left of the declared `FLY_SYSTEM_TRAY_GEOMETRY` (it was `3413, 2100, 427, 60`
  and later `3307, 2100, 533, 60`), so the frames were aligned on the blue column instead of the
  property alone.
- The Parsec trust applies to `XGrabKey` of *any* binary, so a throwaway `/tmp` probe without
  `fly-set-execid.sh` gets `BadAccess` for every combination and cannot enumerate free ones.
- A notification left by an older build stays in the Astra notification area until the next start and
  is easy to mistake for the new build's output; the new binary calls `Notify` not at all.

### Left open, honestly

- The wiring `record`/`forget`/retry in `AppController` has no unit test, because `AppController` has no
  test target; the rule itself is covered by `tst_hotkeywarnings`, the wiring by the manual check above.
- On Wayland the retry is skipped, so a portal that starts working mid-session is reflected only at the
  next launch or settings change. Deliberate: a retry would re-open the desktop's shortcut dialog.
- The hint is refreshed when the dialog opens, not live while it is open; the dialog is modal and owns
  the change, so that is the designed behaviour, not a gap.
- Documentation is outside this task: `doc/architecture.md:565-567` still says `AppController` shows a
  tray notification, and `AGENTS.md:58` plus `src/services/libretranslateservice.cpp:408` still say 382
  messages where the measured number is 383. The lead's pass owns those.
