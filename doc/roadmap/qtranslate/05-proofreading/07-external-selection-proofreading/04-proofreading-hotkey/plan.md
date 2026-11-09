---
kind: task
status: in-progress
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
