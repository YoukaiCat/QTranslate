---
kind: task
status: todo
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
