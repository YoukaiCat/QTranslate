---
kind: task
status: in-progress
detailed: true
---

# Proofreading popup window

## Task

Add `ProofreadingPopupWindow`, the confirmation surface of the external-selection scenario
(`doc/proofreading.md` §2 scenario 3 and §6). It shows the issues of the captured text, the corrected
text and the undo caveat, and it asks for an explicit confirmation before anything is replaced in
another application. The window is a plain `QWidget` in `src/ui`: it never grabs a selection, never
pastes, never copies by itself (it emits a signal) and holds no manager, service or settings.
`AppController` feeds it a `ProofreadingResult` in `02-selection-check-and-replace`.

The window must not take the foreground. The source application has to stay foreground until the
paste, otherwise `Platform::isForegroundWindow()` cannot pass; a window that activates itself would
make the guarded replacement unusable. `Qt::WindowDoesNotAcceptFocus` and
`Qt::WA_ShowWithoutActivating` are therefore part of the contract, not a style choice.

`ProofreadingPanel` (`src/ui/proofreadingpanel.*`) already renders the issue list, the replacement
combo and the Replace/Ignore/Replace all/Copy actions. Reuse it in a new external mode instead of
duplicating the list; a synthetic paste cannot edit another application fragment by fragment, so the
external mode hides the per-issue Replace/Ignore and the replacement combo and relabels Replace all
as Replace selection. The main-window mode keeps its current behaviour and object names.

## Steps

1. Extend `ProofreadingPanel` with `void setExternalMode(bool external)` in
   `src/ui/proofreadingpanel.h/.cpp` (`m_externalMode`, default `false`):
   - in `retranslateUi()` set `m_replaceAllButton` to `tr("Replace selection")` in external mode and
     `tr("Replace all")` otherwise; keep the `LanguageChange` path working;
   - in `updateActions()` hide `m_replaceButton`, `m_ignoreButton` and `m_replacements` in external
     mode (or disable them if hiding breaks the layout) and leave `m_replaceAllButton`/`m_copyButton`
     driven by `hasIssues()` as today;
   - do not emit `replacementChosen`/`ignored` in external mode; `replaceAllRequested` and
     `copyCorrectedRequested` stay the only actions. No manager, settings or clipboard access;
   - `setExternalMode()` must call `retranslateUi()`/`updateActions()` itself, because the popup
     switches the mode after the panel constructor has already built the labels.
2. Add `src/ui/proofreadingpopupwindow.{h,cpp}` modelled on `PopupWindow`:
   - flags `Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
     Qt::WindowDoesNotAcceptFocus`, `WA_DeleteOnClose` false, `WA_ShowWithoutActivating`; show with
     `show()` + `raise()` only — no `activateWindow()`, no `setFocus()`, no Esc shortcut;
   - public API: `showChecking(const QString &text, const QPoint &anchor)`,
     `showResult(const ProofreadingResult &result)`, `QString correctedText() const`, and a
     `setPreferredWidth(int)` test seam; store the captured text in `m_text`;
   - layout: a title label (`tr("Check the selection")`), the embedded `ProofreadingPanel` in
     external mode, a read-only `QPlainTextEdit` preview (`objectName proofreadingCorrected`)
     preceded by the existing `tr("Corrected text:")` label, a word-wrapped caveat and a footer with
     `Replace selection` / `Copy corrected text` / `Cancel`;
   - `showChecking()` clears the preview, puts the panel into its checking state, disables the
     confirmation, remembers the anchor, places the window with `PopupPlacement::positionNear()` and
     shows it; `showResult()` is ignored when hidden or `Cancelled`, feeds the panel, fills the
     preview from `ProofreadingText::corrected(m_text, panel.issues())` on success and disables the
     confirmation on no issues or an error (the panel already renders the manager's gate message
     through `ProofreadingErrorText::forResult()`);
   - the caveat states that the text is replaced only after confirmation and that undo depends on the
     other application; use the same screen/available-geometry logic as `PopupWindow`;
   - signals: `replaceRequested()` (emitted only when the confirmation is enabled),
     `copyRequested()` and `closed()`; `hideEvent`/`closeEvent` emit `closed()` exactly once per
     `showChecking()` and reset the flag on the next show. The corrected text is data and is never
     logged.
3. `CMakeLists.txt`: add `src/ui/proofreadingpopupwindow.cpp`/`.h` to the `qtranslate` target next to
   `src/ui/popupwindow.*`; no core or platform change.
4. Add `tests/tst_proofreadingpopup.cpp` (platform-neutral, `QT_QPA_PLATFORM=offscreen`, links
   `proofreadingpopupwindow`, `proofreadingpanel`, `proofreadingerrortext` and `Qt::Widgets`;
   `qtranslate_core` already supplies `proofreadingtext` and `popupplacement`). Cover:
   - the window flags include `Qt::WindowDoesNotAcceptFocus`; `proofreadingReplace` and
     `proofreadingIgnore` are hidden and `proofreadingReplaceAll` reads "Replace selection";
   - `showChecking()` shows the checking state, an empty preview and a disabled confirmation;
   - a result with two issues fills the list and the preview equals `ProofreadingText::corrected()`;
   - a no-issue result and an error result (including `ServiceUnavailable` with the gate message)
     disable the confirmation and show the state text;
   - clicking `Replace selection` emits `replaceRequested()` once; `Copy corrected text` emits
     `copyRequested()`; `Cancel` hides the window and emits `closed()` once; a second hide does not
     emit again;
   - `LanguageChange` rebuilds the labels/buttons and preserves the list;
   - the clipboard is untouched by the window (set a sentinel before, compare after).
5. Run `update_translations`; the new strings (`Replace selection`, `Check the selection`, the caveat
   and `Cancel`) must appear in all 14 `i18n/*.ts` with no unfinished or vanished message and no
   change to the existing panel/MainWindow strings. Record the measured `grep -c '<message'` total;
   the baseline is 372 and the four new strings are expected to bring it to 376.
6. Build and test Linux Debug and Release under `xvfb-run -a`; run the new target repeatedly (offscreen
   window tests are the flake risk: do not assert activation state, assert the flags; use `QTRY_*`
   around show/hide signals). The full suite becomes 44 (43 + `tst_proofreadingpopup`); Windows would
   become 39 and is recorded as **not performed** (no MSVC).

## Done when

- `ProofreadingPopupWindow` is in `src/ui`, reuses `ProofreadingPanel` in external mode and shows the
  checking, issues + corrected text, no-issues and error (including the online-gate message) states.
- The window does not take the foreground (`WindowDoesNotAcceptFocus` + `WA_ShowWithoutActivating`);
  it never pastes, edits or copies by itself and holds no manager, service or settings.
- The whole-selection semantics and the undo caveat are visible; per-issue Replace/Ignore are hidden
  in the external window and the main-window panel is unchanged.
- `tst_proofreadingpopup` passes offscreen; Linux Debug and Release build with `-Wall -Wextra` and
  44/44 tests with zero warnings; Windows 39 is not performed; the measured `.ts` count is in the
  outcome.
- No checked text, issue message or replacement is logged; no `.ts` message is unfinished or
  vanished.
