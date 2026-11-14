---
kind: task
status: todo
depends: [03-extract-proofreading-window]
---

# Check-and-replace hotkey

## Task

Add the second text-checking action the user asked for on 2026-11-14: a global hotkey that checks
the selection made in another application and immediately replaces it with the corrected text, with
no confirmation window. The existing `ProofreadSelection` action keeps the popup result (check and
show, replace only by its own button), so the two actions are:

1. `ProofreadSelection` — the popup check. Relabel its Hotkeys row to say it shows the result in a
   popup; the flow, the popup and its Replace selection/Copy/Cancel buttons do not change.
2. A new `ProofreadAndReplace` action — checks the selection, computes the corrected text with
   `ProofreadingText::corrected()` and pastes it into the source field immediately.

Both hotkeys are empty by default and both do nothing while `proofreading/enabled` is off: child
`01` already put that gate in `proofreadSelectionInPlace()`, and the new method repeats the same
three-line gate with the identical `tr()` literal (one translation message per context+source) and
returns before the grab. Do not extract a helper for two call sites. The feature stays a standalone
one: no pre-translation check, no `autoReplace` setting, no online gate.

Scope: `HotkeyAction::ProofreadAndReplace`, the `[proofreading] replaceHotkey` settings key
(`ProofreadingSettings::replaceHotkey`, empty by default, read/written with the existing
`readKeySequence`/`PortableText` helpers, `settingsVersion` stays `1`), registration and updates in
`AppController`, the Hotkeys row with duplicate/multi-combination validation, the Windows/X11/
Wayland tables and the tests.

Design decisions already fixed for the detailer:

- The new flow is an `AppController` method (`proofreadSelectionAndReplace()`) that reuses the
  existing pieces instead of duplicating them: the `m_grabber->isBusy()` guard,
  `closeProofreadingPopup()`, the in-place translation cancel, `Platform::foregroundWindow()`
  captured before the grab, `m_grabber->grab()`, `m_proofreading->check(request,
  ProofreadingManager::Channel::InPlace)`, `ProofreadingText::corrected()`, `pasteText()` and
  `showProofreadingMessage()`. No new files, no new `ProofreadingManager` channel: `InPlace` is the
  external-selection stream, so a popup check and a replace check cancel each other and
  `closeProofreadingPopup()` (feature switched off, another selection action, quit) cancels the
  replace check too. Update the `Channel` comment in `proofreadingmanager.h:20-23`, which still says
  the external scenario has a confirmation window.
- `AppController` wiring: register the action at startup next to `ProofreadSelection`, update it in
  the `applySettings()` hotkey block, add a `case` to the `HotkeyManager::activated` switch and add
  the action to the `updateTrayToolTip()` key list, so its combination appears in the tray tooltip
  like the other four.
- The Hotkeys tab gets the new row directly after the popup-check row, before the main-window rows.
- No popup and no focus-watch timer: the target window is compared once, right before the paste, with
  `Platform::isForegroundWindow(target)`. `target == 0` (Wayland, or X11 focus is
  PointerRoot/None) and a changed window both fall back to the clipboard with the two existing
  messages (`Could not paste automatically; the corrected text is on the clipboard.` /
  `The active window changed; the corrected text was copied to the clipboard.`).
- Results: success with issues → `pasteText(corrected)`; no issues, or a corrected text equal to the
  original (all issues inapplicable) → one message and nothing pasted, no clipboard write; a failed
  check → `ProofreadingErrorText::forResult()` in the same tray/non-modal message. Only one new flow
  message (the no-issues one) is needed; with the new hotkey label it is the two new `.ts` messages.
- Message title: reuse `showProofreadingMessage()` and its `Check the selection` title. The known
  "translation" wording of `pasteText()`'s own paste-failure message stays (stage 5 gap), do not fix
  it here.
- `--proofread-selection` keeps the popup flow; there is no CLI command for the replace action (the
  user asked for a hotkey). Both entry points are gated by `proofreading.enabled` already.
- The Windows backend is action-generic (`idForAction()`), so only the enum changes there. On Linux
  `kLastAction` moves to the new value and the portal table gets a row; the existing
  `proofread-selection` portal description must say the result is shown in a popup, because "check
  and replace" is now the other action.
- Hotkey rows stay editable while `proofreading.enabled` is off: the combination may be assigned in
  advance, the action itself is gated at activation. Update the comment in
  `tst_settingsdialog::proofreadingHotkeyIsIndependentOfMode()`, which currently says an explicit
  invocation is not gated by the mode.
- No new setting besides `replaceHotkey`, and no automatic replacement after a manual check: the
  replacement is this action's job. Do not reintroduce `autoReplace` in the checking window or the
  translation window.
- The action is the external-selection path only: it must not open the popup, the checking window or
  the translation window, and it must not touch `MainWindow`. A check of the text typed in the
  checking window is that window's **Check** button, not this hotkey.

## Done when

- `HotkeyAction::ProofreadAndReplace` exists, `[proofreading] replaceHotkey` is read and written and
  is empty by default; the action is registered at startup, updated when the settings change,
  dispatched by the `activated` switch and listed in the tray tooltip; the Hotkeys tab has two
  proofreading rows with distinct labels and `validate()` rejects a duplicate combination between
  them and a multi-combination sequence; `anyHotkeyEditFocused()` includes the new edit.
- Pressing the replace hotkey with the feature enabled grabs the selection, sends one check and, on a
  result with issues, pastes the corrected text into the source field without any confirmation
  window; with no issues (or no applicable issue) it shows a message and pastes nothing; with a
  missing or changed target window it puts the corrected text on the clipboard and shows the existing
  message. No checked text, corrected text or translation appears in the log.
- Pressing either proofreading hotkey with `proofreading/enabled` off shows
  `Enable text checking in Settings → Text checking.` and performs no grab and no request.
- A running replace check is cancelled by the popup hotkey, by another selection action, by turning
  the feature off and by quitting; a stale reply never pastes.
- `tst_settings` (default, round trip, `PortableText`, an INI without the key), `tst_settingsdialog`
  (two rows, labels, duplicate and multi-combination validation, rows editable in both modes),
  `tst_hotkeymanager_x11` and `tst_hotkeymanager_wayland` (the new action registered, portal id
  `proofread-replace`) are green; the Windows-only `tst_hotkeymanager` needs no new case.
- Linux Debug and Release build without warnings and pass 46/46 under `xvfb-run -a` in the
  `qtranslate-trixie` image (the lead runs the final host check after the stage); the 14 `.ts`
  files end at **374** messages (372 after child `03`, plus the new hotkey label and the empty-result
  message; the relabelled popup row replaces its old source) with 0 unfinished/empty/vanished and
  `tst_translations` passes; Windows Debug/Release and its 40 tests are recorded as not performed.
- No `doc/`, README, `AGENTS.md`, root-plan, CMake or version change; no push.

## Risks

- The paste path is the riskiest part: it reuses `pasteText()` and its clipboard snapshot/restore
  window, so the target check must happen before `pasteText()`, not inside the clipboard code, and
  the reply handler must use the existing `deleteLater()`-first, `if (reply != m_replaceCheck)`
  pattern or a cancelled reply can paste stale text.
- `Channel::InPlace` is shared with the popup flow on purpose; do not add a third channel without a
  reason, and do not let the new action leave `m_inPlaceCheck` pointing at its own reply.
- `AppController` has no test target: the new flow is covered by code review and the existing X11
  selection test premises, not by a unit test. Do not add an unprepared test target in this task.
- The Wayland mock portal asserts the returned shortcuts; adding a portal row changes the registered
  count, and `kLastAction`-driven loops must include the new value or the desktop never binds it.
- The `.ts` count is the scope check: **374**. A different number means an extra `tr()` string, a
  missing relabel or a leftover of the removed messages.
- The tray notification path is only as good as `showProofreadingMessage()` when no tray exists
  (`showNonModalMessage`), which is pre-existing and untested interactively; record it as not
  performed rather than claiming it works.
