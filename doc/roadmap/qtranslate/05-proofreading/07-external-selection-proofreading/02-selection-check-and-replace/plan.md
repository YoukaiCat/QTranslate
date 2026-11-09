---
kind: task
status: done
detailed: true
depends: [01-proofreading-popup-window]
---

# Selection check and replacement

## Task

Wire the external scenario end to end in `AppController`: capture the selection with the existing
`SelectionGrabber`, check it through `ProofreadingManager::Channel::InPlace` with the `[proofreading]`
language and categories, show `ProofreadingPopupWindow`, and replace the other application's
selection only after `replaceRequested()`. The flow reuses the translation in-place safeguards: the
target window is captured before the grab; a missing target (Wayland or X11 `PointerRoot`/`None`) or
a different foreground window means "copy the corrected text to the clipboard" instead of a paste;
the clipboard is snapshotted and restored through the existing paste path; a cancel, a focus loss and
a new request stop the check and the paste. The same action is reachable through a new
`--proofread-selection` command forwarded to a running instance; the existing three commands and the
legacy wire lines stay byte-identical.

## Steps

1. `SingleInstance` (`src/app/singleinstance.h/.cpp`): add `Command::ProofreadSelection`, map it to a
   new wire line (`proofread-selection\n`) and back; leave `activate`/`noop` and the three existing
   scenario lines untouched.
2. `CommandLineOptions` (`src/app/commandline.h/.cpp`): add the `--proofread-selection` option with
   the description "Check the selected text and replace it."; include it in the mutual-exclusion
   count and update the translated error to name all four options; `scenario()` returns the new
   command. `src/main.cpp` needs no change — it already forwards `scenario`.
3. `AppController::proofreadSelectionInPlace()` (`src/app/appcontroller.h/.cpp`), declared next to
   `translateSelectionInPlace()` and called from `executeCommand()`; also add the new command to
   `hasStartupCommand` so a startup `--proofread-selection` does not show the main window first, and
   do not set `m_showMainWindowAfterInPlace` for it:
   - if `m_grabber->isBusy()`, return without touching the running grab (same guard as the
     translation flow);
   - cancel `ProofreadingManager::Channel::InPlace`, drop the current reply and close the previous
     popup; capture `const quintptr target = Platform::foregroundWindow()` **before** the grab;
   - `m_grabber->grab(...)`: an empty text shows `tr("Nothing is selected.")` through
     `showProofreadingMessage()`; otherwise build `ProofreadingRequest{text,
     settings.proofreading.language, settings.proofreading.categories}`, open
     `ProofreadingPopupWindow::showChecking(text, QCursor::pos())` (only after the grab returned -
     showing it earlier would steal focus from the source application), then call
     `m_proofreading->check(request, Channel::InPlace)`. Connect `finished`: only the current reply
     and a result that is not `Cancelled` feeds `showResult()`; always `deleteLater()`;
   - while the popup is open, a 300–500 ms `QTimer` checks `target != 0 &&
     !Platform::isForegroundWindow(target)` and cancels: hide the popup, cancel the in-place check,
     stop the timer and leave the target text unchanged. On Wayland (`target == 0`) there is no timer;
     the confirmation falls back to the clipboard;
   - `replaceRequested`: only for the current reply and result; compute the corrected text with
     `ProofreadingText::corrected(text, result.issues)` (the popup shows the same text); if
     `target == 0` or `!Platform::isForegroundWindow(target)`, put the corrected text on the
     clipboard and show a fixed message (`Could not paste automatically; the corrected text is on the
     clipboard.` or `The active window changed; the corrected text was copied to the clipboard.`);
     otherwise call the shared paste path, so the clipboard snapshot, the clipboard-history markers
     and the 500 ms restore stay in one place. Hide the popup and stop the timer in every branch;
   - `copyRequested`: put the corrected text on the clipboard, show
     `tr("Corrected text copied to the clipboard")` (an existing string) and close the popup;
   - `closed`: cancel the current `Channel::InPlace` check and clear the pointers, but do not cancel
     twice when the flow itself hid the popup — use a small "closing" flag or compare the popup
     invocation;
   - `prepareForQuit()` and the settings path that turns `proofreading.enabled` off close the popup
     and cancel the in-place check. No `translation.*` value is read or written.
4. Rename `AppController::pasteTranslation()` to `pasteText()` (declaration, definition and the two
   translation call sites) with no behaviour change, because the proofreading path shares it; keep the
   clipboard guard and the restore semantics identical.
5. Add `AppController::showProofreadingMessage()` with the title `tr("Check the selection")` and the
   same tray/non-modal fallback as `showInPlaceMessage()`; log only the fixed message text, never the
   selection.
6. Update `tests/tst_cli.cpp` (Linux only): parser cases for the new option alone, each pair and the
   four-way conflict, the positional-arguments case, and the two-process forwarding with a real
   `SingleInstance`; assert the legacy wire lines still parse to their old commands.
7. Run `update_translations` and verify the new/changed messages in all 14 `.ts`: the CLI description
   is new, the mutual-exclusion message changes, the two copy-fallback messages are new; the existing
   `Nothing is selected.` and `Corrected text copied to the clipboard` are reused. Record the measured
   total; the baseline after task 01 is expected to be 376.
8. Build and test Linux Debug and Release; there is no new test target, so the suite stays 44/44 with
   zero warnings. The X11 replacement itself is the next task; do not claim it with this one. Windows
   39 is not performed.

## Done when

- `--proofread-selection` parses, forwards through the single-instance socket to a running instance
  and runs the flow; `--translate-popup`, `--translate-main`, `--translate-inplace`, `--minimized`,
  `--verbose` and the legacy wire lines keep their behaviour.
- The selection is checked on `Channel::InPlace`; the popup shows the issue list and the corrected
  text; with `proofreading/allowOnlineServices` off the manager's gate message is shown and nothing is
  replaced; the check does not start a translation.
- The selection is replaced only after `replaceRequested()`, only while the captured window is still
  foreground; otherwise the corrected text is copied; the clipboard is restored by the shared paste
  path; a focus loss, a new request, Cancel and quit stop the check and the paste.
- A pending reply is cancelled and deleted safely; no selection, issue message or replacement appears
  in the log.
- `tst_cli` passes with the new parser and forwarding cases; Linux 44/44; Windows 39 not performed;
  the measured `.ts` total is recorded.

## Outcome

Done. The external-selection scenario is wired end to end in `AppController` and reachable from the
CLI before the hotkey exists. Task `03-x11-replacement-integration` still owns the real X11 paste
test and task `04-proofreading-hotkey` the fifth hotkey; neither was attempted here.

- Files: `src/app/singleinstance.{h,cpp}`, `src/app/commandline.{h,cpp}`,
  `src/app/appcontroller.{h,cpp}`, `tests/tst_cli.cpp` and the 14 `i18n/*.ts`. No change to
  `src/main.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `src/ui/*`, platform code, `doc/`
  (except this Outcome), README or `AGENTS.md`.
- CLI: `Command::ProofreadSelection` maps to `proofread-selection\n` and back; the parser gains
  `--proofread-selection` ("Check the selected text and replace it."), the mutual-exclusion count
  and message now name all four options. `activate`/`noop`, the three translate lines and the
  `--minimized`/`--verbose`/`--translate-*` behaviour are unchanged. `hasStartupCommand` includes
  the new command and does not set `m_showMainWindowAfterInPlace`.
- Flow: `AppController::proofreadSelectionInPlace()` keeps the `isBusy()` guard, closes a previous
  popup and cancels the previous `Channel::InPlace` check, captures
  `Platform::foregroundWindow()` before the grab, shows
  `ProofreadingPopupWindow::showChecking(text, QCursor::pos())` only after the grab returned and then
  calls `ProofreadingManager::check()` with `settings.proofreading.{language,categories}`. The
  `finished` handler accepts only the current reply and a non-`Cancelled` result, stores
  `ProofreadingText::corrected()` for the later confirmation and always deletes the reply.
- Confirmation: the focus watchdog is a `QTimer` with a 400 ms interval, created lazily and started
  only for a non-zero target; a lost foreground hides the popup, cancels the check, stops the timer
  and leaves the target text untouched. `replaceRequested` re-checks `target == 0` and
  `isForegroundWindow(target)` and falls back to the clipboard with one of the two fixed messages,
  otherwise it calls the shared paste path. `copyRequested` copies the corrected text and reports
  through the existing `Corrected text copied to the clipboard` string. `hideProofreadingPopup()`
  sets a closing flag so the `closed` signal does not cancel the check a second time; a user Cancel
  goes through `closed` and cancels it once. `prepareForQuit()` and `applySettings()` when
  `proofreading.enabled` turns false close the popup and cancel the check. Nothing is gated by
  `proofreading.enabled` for an explicit invocation, and no `translation.*` value is read or written.
- Reuse: `pasteTranslation()` is now `pasteText()` (declaration, definition and call sites); the
  clipboard guard, snapshot, history markers and the 500 ms restore are unchanged. The proofreading
  flow is its second caller.
- Messages: `showProofreadingMessage()` uses the `Check the selection` title and the same
  tray/non-modal fallback as `showInPlaceMessage()`; only the fixed message text is logged, never
  the selection, an issue message or the corrected text.
- Tests: `tst_cli` gained the single-option case, `--proofread-selection` with `--minimized` and
  `--verbose`, all six pairs and the four-way conflict (also asserting that the error text names
  every option), the positional-argument case, two-process forwarding with a real `SingleInstance`
  and the raw-socket `proofread-selection\n` line next to the four legacy lines. A temporary
  mutation of either mapping direction made the corresponding case fail, after which the mapping
  was restored: 30/30 passed.
- Translations: the measured total is **382** `<message>` in each of the 14 files. The plan's
  expected baseline of 376 was stale: task 01 already measured 377 (the panel's `Replace
  selection` was the fifth message). 382 = 377 + 5 net new messages: the CLI description, the two
  copy-fallback messages, and `Check the selection` and `Corrected text copied to the clipboard`
  in the `AppController` context. The mutual-exclusion message changed in place, so it did not
  change the count. No file has `unfinished`, `vanished` or an empty translation.
- Verification: Linux Debug and Release build with `-Wall -Wextra` and zero warnings; the full
  `xvfb-run -a ctest` is 44/44 in both configurations (no new target). `tst_translations`,
  `tst_proofreadingpopup`, `tst_proofreadingpanel` and `tst_cli` pass in both. `git diff --check`
  is clean. Windows 39 tests are **not performed** (no MSVC in this environment).
- Review: one remark, rejected. When `Platform::sendPasteShortcut()` fails inside the shared
  `pasteText()`, the proofreading flow shows the existing translation-worded message and the
  `Translate and replace` title. The corrected text is still on the clipboard and nothing is lost;
  the wording is only inaccurate in that rare branch, and making it caller-specific would change
  the shared paste path that step 4 explicitly keeps behaviour-identical. Left as a possible
  follow-up, not a defect of this task.
- Found while working: `doc/architecture.md` and both READMEs still list only the three translate
  CLI commands; this task was not allowed to touch them, so the documentation update is still
  pending. The popup also keeps the default width instead of `popup.width`, which belongs to the
  translation popup.
- Known gaps for the next tasks: the popup is non-activating, but on some window managers clicking
  it may still change `_NET_ACTIVE_WINDOW` and force the clipboard fallback; verifying and, if
  needed, fixing that is part of `03-x11-replacement-integration`. A real portal-capable Wayland
  desktop remains a manual gap.
