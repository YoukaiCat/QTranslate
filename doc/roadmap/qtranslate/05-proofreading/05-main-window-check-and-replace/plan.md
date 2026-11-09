---
kind: task
status: todo
depends: [03-languagetool-service, 04-text-checking-tab]
---

# Main-window checking and replacement

## Task

Make the checking mode usable from the main window: a Check button next to Translate, highlighted
issues in the source field, an issue list with Replace, Ignore and Replace all, copying the corrected
text, one undo step per replacement, the request states, and the check-before-translation
confirmation. This is the first user-visible result of the stage and the base for the external
scenario in `07-external-selection-proofreading`. `doc/proofreading.md` §2/§6 and
`doc/architecture.md` §4.13 define the behaviour; the UI keeps knowing nothing about services and the
network (the `MainWindow` contract in `mainwindow.h:16-20`).

The new widget is `src/ui/proofreadingpanel.{h,cpp}`: a list of issues (message, rule id, the
replacement choices) with the buttons Replace, Ignore, Replace all and Copy corrected text, and the
states checking / no errors / provider unavailable / key required / request limit. It receives a
`ProofreadingResult` plus the error text, emits `replacementChosen(int issue, int replacement)`,
`ignored(int issue)`, `replaceAllRequested()` and `copyCorrectedRequested()`, and never edits the
document itself. `MainWindow` owns the panel, adds the Check button and paints the issues with
`QPlainTextEdit::setExtraSelections()` (a `SpellCheckUnderline` in the `QTextCharFormat`), selects
the fragment of a clicked row, and applies a replacement through a `QTextCursor` inside one
`beginEditBlock()`/`endEditBlock()`, so a single Ctrl+Z restores the text. Replace all shows a
preview dialog with the corrected text and applies the first replacement of every issue from the end
of the document to the beginning in one edit block; issues without a replacement are skipped.
`proofreading/autoReplace` (off by default) applies the same Replace all right after a check finishes,
still in one block, and never re-checks the text it just produced, so it cannot loop.

After a single replacement the window asks for a fresh check instead of shifting the remaining
offsets locally, because a replacement can change the surrounding issues; the new reply replaces the
list and the highlights. Replace all and autoReplace do not re-check. The Check button is visible
only when `proofreading/enabled` is true and a provider is registered. `AppController` owns the
`ProofreadingManager` wiring (from `03-languagetool-service`), runs the check on
`MainWindow::checkRequested()` for the `MainWindow` channel, feeds `ProofreadingResult` back and
cancels on a new request or when the window closes. For the error text, add an overload
`TranslationErrorText::forResult(const ProofreadingResult &)` in `src/ui/translationerrortext.{h,cpp}`
with the same wording as the translation overload.

Check before translation: when `proofreading/checkBeforeTranslation` is on and the user triggers a
main-window translation (the Translate button, Ctrl+Enter or the Ctrl+W hotkey path), the window
checks the source text first. With no issues it translates immediately; with issues it shows the
panel and a modal confirmation offering "Apply corrections and translate" and "Translate anyway";
nothing is changed without the user's choice. When the option is off the behaviour is exactly today's.

## Done when

- `src/ui/proofreadingpanel.{h,cpp}` shows the issues with message, rule id and replacement choices,
  the four buttons and the five states, receives the error text through the new
  `TranslationErrorText::forResult(const ProofreadingResult &)` overload, and emits only the four
  signals above; it does not edit the document and holds no manager or service.
- The main window has a Check button next to Translate, visible only while the mode is on and a
  provider is registered; a check request goes through `AppController` on the `MainWindow` channel,
  a second request cancels the first, and closing the window cancels the open one.
- Issues are underlined in the source field via extra selections; clicking a list row selects the
  fragment; the highlight count equals the issue count and both are cleared by a new check, by a
  translation and by an empty result.
- Replace applies the chosen replacement of one issue in one undo block (a single Ctrl+Z restores the
  exact previous text) and requests a fresh check; Replace all shows the preview with the corrected
  text, applies the first replacement of every issue without one undo step per issue, and skips
  issues without a replacement; Ignore removes one issue from the list and its highlight without
  touching the text; Copy puts the corrected text on the clipboard.
- `proofreading/autoReplace` (default off) applies the corrections after a completed check in one undo
  block and does not re-check the resulting text; with the option off nothing is applied without an
  explicit click.
- Check before translation (default off): with the option on and issues present, the panel and the
  two-choice confirmation appear before any translation and the text is unchanged unless the user
  chooses to apply; with no issues or the option off, the translation starts exactly as before.
- `tests/tst_proofreadingpanel.cpp` (new, Widgets, `QT_QPA_PLATFORM=offscreen`, compiling
  `proofreadingpanel`, `mainwindow`, `languagecombobox` and `translationerrortext` like
  `tst_settingsdialog` in `tests/CMakeLists.txt:55-62`) covers the list and the four buttons, the
  highlight count, the change of a selected replacement, single undo after Replace, Replace all with
  the preview, Ignore, Copy, the five states, autoReplace off/on, and the check-before-translation
  branch with and without issues; the existing `tst_popupwindow`/`tst_settingsdialog` stay green.
- Debug and Release build on Linux without warnings and the full suite passes; `ctest -N` shows 43
  Linux tests and would show 38 on Windows (the new target is platform-neutral); Windows is recorded
  as not performed. No service, settings, `.ts`, documentation, README or `doc/handoff.md` change; the
  new `.ts` strings belong to `04-text-checking-tab` unless this child adds its own, in which case
  they are added to all 14 files here.

## Depends on

`03-languagetool-service` for a real provider and the manager wiring, and `04-text-checking-tab` for
the settings that switch the mode and the confirmation on.
