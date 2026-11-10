---
kind: task
status: done
detailed: true
depends: [02-selection-check-and-replace]
---

# Proofreading hotkey

## Task

Add the fifth global hotkey action `HotkeyAction::ProofreadSelection` and the `proofreading/hotkey`
setting (empty by default), edit its row in the **Hotkeys** tab, register the action in the Windows,
X11 and Wayland `GlobalShortcuts` backends, and dispatch it to
`AppController::proofreadSelectionInPlace()` from `02-selection-check-and-replace`. The `HotkeyManager`
public API stays as it is: the enum value is appended, the Windows backend's `idForAction` and event
mapping are already action-generic, and the Linux portal table plus its `HotkeyAction` range loops
are the only backend changes. On Wayland the bind stays asynchronous and `registrationFailed` is
unchanged. An empty `proofreading/hotkey` registers nothing.

## Steps

1. `src/platform/hotkeymanager.h`: append `ProofreadSelection` after `ShowMainWindow`; do not renumber
   existing values or add methods. `src/platform/hotkeymanager_win.cpp` needs no logic change
   (`idForAction` derives from `int(action)` and `m_actionsById` is generic), but the Windows test
   must still build.
2. `src/platform/hotkeymanager_linux.cpp`: add the fifth `ShortcutInfo` entry
   (`proofread-selection`, description "Check the selected text and replace it") and replace the
   `for (int i = 0; i <= int(HotkeyAction::ShowMainWindow); ++i)` bounds with a local
   `kLastAction = HotkeyAction::ProofreadSelection` (or the enum's last value) so
   `actionForShortcutId()`, `BindShortcuts` and `ShortcutsChanged` cover the new action. Keep the
   asynchronous `BindShortcuts`/`registrationFailed` semantics unchanged.
3. Settings: add `QKeySequence hotkey;` (empty) to `ProofreadingSettings` in
   `src/core/settings.h`; load `[proofreading] hotkey` through `readKeySequence()` and write it as
   PortableText in `src/core/settings.cpp` (`save()` next to the other `[proofreading]` keys). Do not
   add it to `HotkeySettings`/`[hotkeys]` — the design key is `proofreading/hotkey`. No credential and
   no `translation.*` change.
4. `SettingsDialog` (`src/ui/settingsdialog.h/.cpp`): a fifth `QKeySequenceEdit`
   (`objectName proofreadingHotkey`) and the row `Check the selection in another application:` after
   the replace-selection row; load/save `result.proofreading.hotkey`; include it in `validate()`'s
   one-combination and duplicate checks and in `anyHotkeyEditFocused()`. The existing hint "Leave a
   field empty to disable that action" already covers it.
5. `AppController` (`src/app/appcontroller.cpp`):
   - register the new action only when the sequence is non-empty, in `createHotkeys()` and in the
     `applySettings()` hotkey update block;
   - `activated(HotkeyAction::ProofreadSelection)` calls `proofreadSelectionInPlace()`;
   - include it in `updateTrayToolTip()` only when its `hotkey()` is non-empty (on Wayland the portal
     reports an empty sequence after assignment — the existing documented gap, do not special-case);
   - the existing `registrationFailed` handler already reports busy combinations; no new wording.
6. Tests:
   - `tests/tst_hotkeymanager_x11.cpp`: add the fifth action to `registersAllActions()` and to the
     `unregisterAll()` case where the four are enumerated; use a sequence that does not collide with
     the existing four (for example Ctrl+Alt+Shift+F11).
   - `tests/tst_hotkeymanager_wayland.cpp`: add the fifth `ActionCase` in `actionCases()` (id
     `proofread-selection`, trigger `CTRL+ALT+SHIFT+F11`), update the hard-coded counts 4 → 5
     (`bindSubsetIsReported`, `happyPathBindsAllActions`, `changedSequenceRebinds`, and the other
     `lastShortcuts.size()` assertions) and the `DropShowMainWindow` mock logic if it assumes four
     entries.
   - `tests/tst_settings.cpp`: the empty default, the round trip in `[proofreading]`, the raw group
     presence, a malformed value falling back to empty, and no `translation.*` change.
   - `tests/tst_settingsdialog.cpp`: the row exists and round-trips, a duplicate with one of the four
     existing combinations is rejected with the fixed message, an empty value is accepted, and the
     proofreading mode does not disable the row.
   - `tests/tst_hotkeysequence.cpp` is pure mapping and needs no case; record that.
7. Run `update_translations`: one new dialog label; the portal description is intentionally not
   translated (stage 2 rule). Record the measured `.ts` total (baseline after task 02) and the test
   counts: no new target, Linux stays 45/45; Windows 39 not performed. The Astra X11 test binary is
   already trusted; a fresh build tree needs the documented `trusted.d`/`fly-set-execid.sh` step.
8. Verify on Linux Debug and Release: the X11 and Wayland mock tests pass, the empty setting registers
   nothing through `AppController` (record a manual check or a focused `tst_settings` assertion; do
   not add an `AppController` test) and the full suite is 45/45 with zero warnings. A real
   portal-capable desktop remains a manual gap.

## Done when

- The fifth action compiles and registers on Windows, X11 and the Wayland portal; an empty
  `proofreading/hotkey` registers nothing; a busy combination reports through `registrationFailed`
  with the existing tray behaviour.
- The **Hotkeys** tab edits `proofreading/hotkey`, rejects duplicates with the existing four
  combinations and multi-combination values, and the setting round-trips through `[proofreading]`
  without touching translation settings.
- Activating the hotkey calls the same `proofreadSelectionInPlace()` flow as
  `--proofread-selection`.
- Tests cover the new action on X11 and the Wayland mock, the settings round trip and the dialog
  validation; no new target, Linux 45/45 in Debug and Release with zero warnings; Windows 39 not
  performed; the measured `.ts` count and the trusted-client step are recorded.

## Risks

- Adding a value to `HotkeyAction` is the minimal frozen-API change; do not add methods, do not
  renumber existing values and keep `Q_DECLARE_METATYPE` working.
- The Linux backend has several loops bounded by `ShowMainWindow`; missing one breaks
  `ShortcutsChanged` or the bind subset silently. Use one `kLastAction` constant and grep every
  `ShowMainWindow` use.
- The Wayland tests hard-code 4 in several places; a missed count is the likely failure.
- Windows is not built here; record it as not performed.

## Outcome

Done. The fifth hotkey action is registered by all three backends and dispatches to the same
`AppController::proofreadSelectionInPlace()` flow as `--proofread-selection`.

- Files: `src/platform/hotkeymanager.{h}`, `src/platform/hotkeymanager_linux.cpp`,
  `src/core/settings.{h,cpp}`, `src/ui/settingsdialog.{h,cpp}`, `src/app/appcontroller.cpp`,
  `tests/tst_hotkeymanager_x11.cpp`, `tests/tst_hotkeymanager_wayland.cpp`, `tests/tst_settings.cpp`,
  `tests/tst_settingsdialog.cpp` and the 14 `i18n/*.ts`. No change to the Windows backend, the stub,
  `CMakeLists.txt`, `tests/CMakeLists.txt`, README, `AGENTS.md` or other `doc/` files.
- API: `ProofreadSelection` is appended after `ShowMainWindow`; nothing is renumbered and no method
  changed. The Windows backend needed no logic edit (`idForAction()` is `1 + int(action)` and
  `m_actionsById` is generic); `tests/tst_hotkeymanager.cpp` is untouched and stays compilable.
- Linux backend: `shortcutInfo()` gained `proofread-selection` / "Check the selected text and
  replace it" (not translated, stage rule), and the three `ShowMainWindow`-bounded loops
  (`actionForShortcutId`, `bindPending`, `onShortcutsChanged`) now use one
  `constexpr HotkeyAction kLastAction = HotkeyAction::ProofreadSelection`. The asynchronous
  `BindShortcuts` and `registrationFailed` semantics are unchanged.
- Settings: `ProofreadingSettings::hotkey` is empty by default, read through `readKeySequence()` and
  written as PortableText under `[proofreading]`. `[hotkeys]` and `translation.*` are untouched;
  `normalizeSettings` needed no change.
- Dialog: the fifth `QKeySequenceEdit` (`proofreadingHotkey`) sits between the replace-selection and
  main-window rows with the label "Check the selection in another application:"; load, save, the
  one-combination/validity and duplicate checks and `anyHotkeyEditFocused()` include it. The unused
  `name` field of `validate()`'s local table receives an empty string for the new row, so the change
  adds exactly one translatable message instead of a second, never-shown one.
- Controller: registered only when non-empty in `createHotkeys()` and through the existing
  `updateHotkey()` lambda in `applySettings()`; added to `updateTrayToolTip()` where the existing
  empty-sequence check already hides it on Wayland after a desktop assignment; the
  `registrationFailed` handler and the `allowOnlineServices` gate are untouched.
- Empty setting: an empty `proofreading/hotkey` registers nothing because the `registerHotkey()`
  lambda skips empty sequences; covered by the empty default in `tst_settings` and the existing
  controller code. No `AppController` test was added, as the plan required.
- Tests: X11 `registersAllActions()` has the fifth action (`Ctrl+Alt+Shift+F11`) and
  `unregisterAllReleasesEverything()` registers the new action too (`Ctrl+Alt+Shift+F12`, because
  that test already used F10 and F11; the plan's "four enumerated" wording was stale). The Wayland
  mock has the fifth `ActionCase` (`proofread-selection`, `CTRL+ALT+SHIFT+F11`) and every
  hard-coded count is 5; `shortcutsChangedReportsRemoval` expects four returned shortcuts after
  `show-main-window` is dropped, and `DropShowMainWindow` still removes exactly one entry.
  `tst_settings` covers the empty default, the `[proofreading]` round trip, the raw
  `proofreading/hotkey` value, a malformed value (`Ctrl+Bogus??`) falling back to empty and the
  untouched `translation.*` values. `tst_settingsdialog` covers the row, the settings-to-widget
  load, the round trip, a duplicate with an existing action and with the new row, the accepted
  empty value and both proofreading modes. `tst_hotkeysequence` is pure key mapping and needs no
  case.
- Translations: `update_translations` measured **383** `<message>` in each of the 14 files (382
  before; exactly one new label, translated in all 14). No `unfinished`, `vanished` or lost
  translation; the rest of the `.ts` diff is recomputed `<location>` lines.
- Verification: Linux Debug and Release build with `-Wall -Wextra` and zero warnings; the full
  `xvfb-run -a ctest` is **45/45** in both configurations (no new target). `tst_hotkeymanager_x11`
  16 passed / 0 skipped, `tst_hotkeymanager_wayland` 20/20, `tst_settings` 65/65,
  `tst_settingsdialog` 31/31, `tst_translations` 4/4. `git diff --check` is clean.
- Astra trusted-client step: after rebuilding the X11 test binary it skipped with the keylogger
  policy until `sudo /usr/bin/fly-set-execid.sh` was run (the `trusted.d` entries already existed);
  16/16 passed afterwards. Recorded because the step is easy to miss.
- Review: two minor test-coverage remarks accepted and fixed (the dialog now proves that a saved
  `proofreading/hotkey` reaches the widget, and a dedicated case checks that the row stays enabled
  with `proofreading/enabled` off). Nothing was rejected.
- Not performed: Windows build and its 39 tests (no MSVC; the backend and its test are unchanged and
  action-generic) and a real portal-capable Wayland desktop (the mock is the automated gate).
- Found while working, not edited here: `doc/architecture.md` §4.3 still shows four `HotkeyAction`
  values and the four rows of the action table, and `AGENTS.md` still says 34 Windows / 39 Linux
  tests. Both were stale before this task and belong to a documentation pass.
