---
kind: task
status: done
detailed: true
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

The manager, the service and the settings already exist (`01-proofreading-core`,
`03-languagetool-service`, `04-text-checking-tab`) and are owned by `AppController`
(`appcontroller.cpp:274-287`). Nothing here changes a service, a manager or a setting; this node is
the UI, its wiring and the strings.

The design decisions this plan fixes (the details are in the steps):

- **The widgets stay service-free.** `MainWindow` emits `checkRequested(const QString &text)` and
  `checkCancelled()`; `AppController` builds the `ProofreadingRequest` from
  `settings.proofreading.language`/`.categories`, runs it on `ProofreadingManager::Channel::MainWindow`
  and calls `MainWindow::showProofreadingResult(const ProofreadingResult &)`. `MainWindow` also gets
  two plain booleans, `setProofreadingAvailable(bool)` (the mode is on and a provider is registered)
  and `setProofreadingOptions(bool checkBeforeTranslation, bool autoReplace)`; it never sees a
  manager, a service or a settings object.
- **The issue list is one widget**: the new `src/ui/proofreadingpanel.{h,cpp}` holds the displayed
  issues, the states and the four action buttons, and emits five signals —
  `issueSelected(int row)`, `replacementChosen(int row, int replacementIndex)`, `ignored(int row)`,
  `replaceAllRequested()` and `copyCorrectedRequested()`. It never edits a document and holds no
  manager or service; `MainWindow` is the only place that touches the source document.
- **Corrected text is pure logic in `qtranslate_core`** (`src/core/proofreadingtext.{h,cpp}`), so
  Replace all, automatic replacement, Copy and the future external scenario (`07`) share one rule:
  the first replacement of each issue, applied from the end of the text to the beginning, skipping
  empty replacements, out-of-range ranges and any range overlapping an already-applied edit.
- **One undo step per action.** Every edit of the source field goes through a `QTextCursor` inside
  one `beginEditBlock()`/`endEditBlock()`: a single Ctrl+Z restores the exact previous text.
- **Ignore is local.** It drops the row and its highlight only; the text is untouched.
- **A stale result is never applied.** Editing the source text (other than by our own replacement),
  starting a translation, closing the window and a new check all clear the list and the highlights
  and cancel the open request, so an offset from an older check can never corrupt the text.
- **Check before translation lives in the window** (it is a UI flow over a value type): with the
  option on, an explicit translate request (button, Ctrl+Enter, Ctrl+W/`translateText()`) first
  starts a check; the automatic-translation timer is excluded, because a modal question per typing
  pause is unusable. No issues — or a failed check — translate immediately; with issues a modal
  dialog offers "Apply corrections and translate" / "Translate anyway", and nothing changes the text
  without a choice.
- **Error texts are written for checking, not reused from translation.** A separate
  `src/ui/proofreadingerrortext.{h,cpp}` maps `ProofreadingResult.error` to checking wording (no
  "translation service", no "API key") and appends `result.errorMessage` in parentheses exactly as
  `TranslationErrorText::forResult()` does. `TranslationErrorText` and its wording stay untouched.

## Steps

1. **Add the pure corrected-text helper in `src/core/proofreadingtext.{h,cpp}`.**
   - Header:
     ```cpp
     // Pure text operations on a check result: no UI, no document (see
     // doc/proofreading.md, §4, "for text replacement").
     namespace ProofreadingText {
     // True when `issue` has a replacement and its [offset, offset+length) range
     // lies inside `text`; offset/length count UTF-16 units.
     bool isApplicable(const QString &text, const ProofreadingIssue &issue);

     // Applies the first replacement of every applicable issue, from the end of
     // the text to the beginning, and returns the corrected text. An issue whose
     // range overlaps an already-applied edit is skipped, so no two edits ever
     // fight over one character; an empty replacement list is skipped.
     QString corrected(const QString &text, const QVector<ProofreadingIssue> &issues);
     }
     ```
     Include `core/proofreadingtypes.h` and `<QVector>`.
   - `isApplicable()`: `!issue.replacements.isEmpty()`, `issue.offset >= 0`, `issue.length >= 0`,
     `issue.offset + issue.length <= text.size()`, and the range does not split a surrogate pair
     (if `length == 0`, the position must not sit between the halves of a pair — check
     `text.at(offset)` is not a low surrogate and `text.at(offset - 1)` is not a high surrogate).
   - `corrected()`: copy the applicable issues into a local `QVector`, `std::sort` by
     `offset` descending, then `length` descending, then `ruleId`; keep `int appliedFrom = text.size();`
     and for each issue skip it when `issue.offset + issue.length > appliedFrom`, otherwise
     `text.replace(issue.offset, issue.length, issue.replacements.first())` and set
     `appliedFrom = issue.offset`. Return the original text when nothing applies.
   - Add `src/core/proofreadingtext.cpp src/core/proofreadingtext.h` to `qtranslate_core` between
     `proofreadingservice.*` (line 46) and `proofreadingtypes.h` (line 47).
   - This helper is also what `07-external-selection-proofreading` needs for the captured selection;
     do not inline it into a widget.

2. **Add a separate checking error table in `src/ui/proofreadingerrortext.{h,cpp}`.**
   - Do **not** reuse `TranslationErrorText::forResult()`: its titles say "translation service" and
     `Auth` says "rejected the API key", while stage 5 sends no credentials and the checking mode is
     not translation — the user would read `The translation service rejected the API key (the
     endpoint rejected the request)`. A mode parameter on the existing table would mix two
     vocabularies in one switch and would still need the same eleven new `.ts` messages, so a
     separate file is the smaller and cleaner change.
   - `proofreadingerrortext.h`: `#include "core/proofreadingtypes.h"` and
     ```cpp
     // User-facing description of a failed text check. Separate from
     // TranslationErrorText: the checking vocabulary never mentions translation
     // or an API key (stage 5 sends no credentials).
     namespace ProofreadingErrorText {
     QString forResult(const ProofreadingResult &result);
     }
     ```
   - `proofreadingerrortext.cpp`: a file-local `const char *title(TranslationError)` covering **all**
     twelve `TranslationError` values (`core/translationtypes.h:16-31`), `nullptr` for `None`, every
     string marked with `QT_TRANSLATE_NOOP("ProofreadingErrorText", ...)`, and a `switch` **without a
     `default:`** so a new enum value is reported by `-Wswitch` (`-Wall`). `forResult()` translates the
     key and appends `" (%1)"` with `result.errorMessage` when it is not empty — the same body as
     `TranslationErrorText::forResult()` (`translationerrortext.cpp:50-59`).
   - The eleven strings (`Network`, `Timeout`, `Auth`, `QuotaExceeded`, `RateLimited`,
     `ServiceUnavailable`, `TooLong`, `UnsupportedLanguage`, `BadResponse`, `Cancelled`, `EmptyText`):
     `Network error`, `The text checking service did not respond in time`,
     `The text checking service rejected the request`,
     `The text checking quota is exceeded; try again later`,
     `Too many requests; try again later or use a self-hosted server`,
     `The text checking service is unavailable`, `The text is too long to check`,
     `The language is not supported by the text checking service`,
     `The text checking service returned an unexpected response`, `Text checking cancelled`,
     `There is no text to check`. None of them contains "translation", "translate" or "API key".
   - Use `result.errorMessage` as it arrives from the manager/service; do not re-map or filter it.
     The manager's gate text (`Enable online text checking in Settings → Text checking.`), the
     provider messages and the `language "%1" is not supported` text already exist in the
     `ProofreadingManager` context and appear only inside the parentheses.
   - Add `src/ui/proofreadingerrortext.cpp src/ui/proofreadingerrortext.h` to the `qtranslate` target
     next to `src/ui/proofreadingpanel.*`, and to the test target in step 6. `TranslationErrorText`
     and its file are not touched.

3. **Add `src/ui/proofreadingpanel.{h,cpp}` (new widget, `Qt::Widgets`).**
   - Public surface:
     ```cpp
     class ProofreadingPanel : public QWidget
     {
         Q_OBJECT
     public:
         explicit ProofreadingPanel(QWidget *parent = nullptr);

         void showChecking();                        // clears the list, shows "Checking…"
         void showResult(const ProofreadingResult &result); // issues, "No issues found." or the error
         void clear();                               // empty list, no state text

         bool hasIssues() const;
         QVector<ProofreadingIssue> issues() const;  // the rows still shown, in order
         const ProofreadingIssue *issueAt(int row) const;
         QString correctedText(const QString &text) const; // ProofreadingText::corrected()

     signals:
         void issueSelected(int row);
         void replacementChosen(int row, int replacementIndex);
         void ignored(int row);
         void replaceAllRequested();
         void copyCorrectedRequested();
     };
     ```
   - Layout: a header `QLabel` (`proofreadingTitle`, text "Issues"), a state `QLabel`
     (`proofreadingState`, word-wrapped, red while it shows an error), a `QListWidget`
     (`proofreadingIssues`), a `QComboBox` (`proofreadingReplacement`, text "Suggestions:"-free —
     just the replacement strings, object name is enough for the test) and a button row
     Replace (`proofreadingReplace`), Ignore (`proofreadingIgnore`); at the bottom Replace all
     (`proofreadingReplaceAll`) and Copy corrected text (`proofreadingCopy`).
   - Row text: the issue message on the first line and `tr("Rule: %1").arg(ruleId)` on the second;
     store the row index only (the vector is the model). `currentRowChanged` selects the row and
     refills the combo with that issue's `replacements` under a `QSignalBlocker`; an empty list shows
     one disabled placeholder entry `tr("No suggestions")` and disables Replace. Ignore stays enabled
     for any row. Replace all and Copy are enabled while `hasIssues()`.
   - `showChecking()` / `showResult()` / `clear()` set the state text and the enabled buttons: while
     checking there is no list and no actions; a successful empty result shows "No issues found."; a
     failed result shows `ProofreadingErrorText::forResult(result)` and no rows (the four states of the
     node: checking, no errors, provider unavailable and request limit are all `showResult()` cases).
   - `showResult()` appends `result.issues` to the list and sets `m_issues`; it never removes an
     ignored row by itself — `ignored(row)` is emitted *after* the row and `m_issues` entry are
     removed, so the owner can repaint its highlights from `issues()`.
   - The widget holds no manager, service, reply or `AppSettings`, and never touches a
     `QPlainTextEdit`. Errors arrive as the formatted string through the overload from step 2.
   - `changeEvent(QEvent::LanguageChange)` re-applies every text (`retranslateUi()`), like
     `MainWindow` (`mainwindow.cpp:274-279`).
   - Add `src/ui/proofreadingpanel.cpp src/ui/proofreadingpanel.h` to the `qtranslate` target after
     `src/ui/mainwindow.*` (`CMakeLists.txt:102`).

4. **Extend `src/ui/mainwindow.{h,cpp}`: Check button, panel, highlights, replacement actions.**
   - `mainwindow.h`: `#include "core/proofreadingtypes.h"` next to `core/translationtypes.h`; update
     the class comment (`mainwindow.h:16-20`) to say the window also emits the check signals and
     never talks to a manager. Add `class ProofreadingPanel;`, the two setters
     (`setProofreadingAvailable(bool)`, `setProofreadingOptions(bool checkBeforeTranslation,
     bool autoReplace)`), `showProofreadingResult(const ProofreadingResult &)`, the signals
     `checkRequested(const QString &text)` and `checkCancelled()`, and the private helpers
     `void startCheck();`, `void onProofreadingResult(const ProofreadingResult &)`,
     `void updateHighlights();`, `void selectIssue(int row);`,
     `void applyReplacement(int row, int replacementIndex);`, `void ignoreIssue(int row);`,
     `void replaceAll(bool preview);`, `void copyCorrected();`,
     `enum class BeforeTranslationChoice { ApplyCorrections, TranslateAnyway, Dismissed };
     BeforeTranslationChoice askBeforeTranslation(int issueCount);`,
     `bool previewCorrections(const QString &corrected);`,
     `void applyIssueEdits(const QVector<ProofreadingIssue> &issues);` and
     `void requestTranslation(bool userInitiated = true);`.
     Members: `ProofreadingPanel *m_proofreadingPanel`, `QPushButton *m_checkButton`,
     `bool m_proofreadingAvailable`, `bool m_checkBeforeTranslation`, `bool m_autoReplace`,
     `bool m_pendingTranslationCheck`, `bool m_applyingEdit`.
   - `buildUi()`: create `m_proofreadingPanel` as the third pane of the existing splitter
     (`mainwindow.cpp:70-83`), hidden by default, with a fixed-ish width (~260) so it never squeezes
     the two text fields; create `m_checkButton` (`tr("Check")`, objectName `check`,
     tooltip `tr("Check the source text")`) in the bottom bar before Translate
     (`mainwindow.cpp:86-97`), hidden until `setProofreadingAvailable(true)`. Connect the panel's
     five signals to the helpers above.
   - `setProofreadingAvailable(bool)`: store it, show/hide the panel and the Check button, and on
     `false` run `m_proofreadingPanel->clear()`, `updateHighlights()`, clear
     `m_pendingTranslationCheck` and emit `checkCancelled()`.
   - `setProofreadingOptions()`: store the two booleans only.
   - `startCheck()`: return for empty/whitespace-only text with the status `tr("Nothing to check")`;
     otherwise drop the pending flow flag, `m_proofreadingPanel->showChecking()`,
     `updateHighlights()` (empty list) and emit `checkRequested(m_sourceEdit->toPlainText())`. The
     panel is visible from here on and the highlights are cleared, so an old result can never be
     applied to changed text.
   - `showProofreadingResult(result)`: ignore `Cancelled`; otherwise
     `m_proofreadingPanel->showResult(result)` and `updateHighlights()`, then run the branches of the
     next bullet.
   - Check-before-translation flow: `requestTranslation(bool userInitiated)` keeps its current body
     (`mainwindow.cpp:281-288`) but, when `userInitiated && m_checkBeforeTranslation &&
     m_proofreadingAvailable && !text.trimmed().isEmpty()`, sets `m_pendingTranslationCheck = true`
     and calls `startCheck()` instead of emitting `translateRequested`. The auto-translate timer
     (`mainwindow.cpp:106-116`) and every other internal call pass `false`. In
     `showProofreadingResult()` with `m_pendingTranslationCheck`:
     * the result is not ok → show it and translate anyway (no dialog, no hang);
     * no issues → translate;
     * issues → `askBeforeTranslation(issues.size())`: `ApplyCorrections` applies the corrected text
       in one edit block, then translates; `TranslateAnyway` translates without touching the text;
       `Dismissed` (Escape/close) does nothing.
     "Translate" means building the `TranslationRequest` from the current field/languages and
     emitting `translateRequested` directly — never re-entering `requestTranslation()`, which would
     loop. `askBeforeTranslation()` is a `QDialog` with the title `tr("Check before translation")`,
     the label `tr("%1 issues found in the source text.")`, and the two `QPushButton`s
     `applyCorrectionsAndTranslate` / `translateAnyway` with the texts
     `tr("Apply corrections and translate")` and `tr("Translate anyway")`; Escape returns
     `Dismissed`.
   - Automatic replacement: in `showProofreadingResult()` (non-pending branch), when
     `m_autoReplace && result.ok() && !result.issues.isEmpty()`, call `replaceAll(false)` right after
     showing the result, so the corrections are applied in one edit block and the text is **not**
     checked again; with the option off nothing is applied without a click.
   - `updateHighlights()`: build `QList<QTextEdit::ExtraSelection>` from
     `m_proofreadingPanel->issues()`, one per issue with a valid range, using
     `QTextCharFormat::SpellCheckUnderline` and a red underline colour, and call
     `m_sourceEdit->setExtraSelections(...)`. The count therefore always equals the number of rows
     still listed; skipping an invalid range must not happen for real results, so log nothing and
     just omit it.
   - `selectIssue(row)`: take `m_proofreadingPanel->issueAt(row)`, clamp the range to the document,
     `QTextCursor` + `setPosition(offset)`/`setPosition(offset + length, KeepAnchor)`, and
     `m_sourceEdit->setTextCursor(cursor)` — do not scroll away with `ensureCursorVisible` disabled.
   - `applyReplacement(row, index)`: read the issue and
     `issue->replacements.value(index)`; validate with `ProofreadingText::isApplicable()` against the
     current document text; set `m_applyingEdit = true`, apply through a `QTextCursor` inside one
     `beginEditBlock()`/`endEditBlock()`, reset the flag, then `startCheck()` — a replacement can move
     the remaining offsets, so the window asks for a fresh check instead of shifting them.
   - `replaceAll(preview)`: build `const QString corrected = m_proofreadingPanel->correctedText(
     m_sourceEdit->toPlainText())`; when `preview` is true and `corrected != current`, ask
     `previewCorrections(corrected)` and return on cancel. Apply the first replacement of every
     applicable issue from the end of the text to the beginning inside **one**
     `beginEditBlock()`/`endEditBlock()` (use the same sorting and overlap rule as
     `ProofreadingText::corrected()`, or apply them through one cursor block and compare the result),
     then `m_proofreadingPanel->clear()`, `updateHighlights()` and, for the automatic path, the status
     `tr("Corrections applied")`. Do not request a check here. Because the offsets are stale
     afterwards, the list is cleared rather than kept.
   - `previewCorrections(corrected)`: a modal `QDialog` (title `tr("Preview of corrections")`) with the
     label `tr("Corrected text:")` and a read-only `QPlainTextEdit` named `proofreadingPreview`,
     closing with a `QDialogButtonBox(Ok | Cancel)`; the standard buttons come from Qt's own
     translations, so they add no `.ts` entry.
   - `ignoreIssue(row)`: the panel has already dropped the row; call `updateHighlights()` only.
   - `copyCorrected()`: `QGuiApplication::clipboard()->setText(
     m_proofreadingPanel->correctedText(m_sourceEdit->toPlainText()))`, leave the document and the
     highlights untouched, and show `tr("Corrected text copied to the clipboard")` for 2 s, like
     `copyResult()` (`mainwindow.cpp:310-317`).
   - Stale results: in the existing `textChanged` lambda (`mainwindow.cpp:109-116`), when
     `!m_applyingEdit`, call `m_proofreadingPanel->clear()`, `updateHighlights()` and emit
     `checkCancelled()`. `translateText()` and `showTranslation()` (`mainwindow.cpp:212-220`,
     `250-262`) use a `QSignalBlocker`, so clear the panel and emit `checkCancelled()` there
     explicitly; `showTranslationStarted()` (`mainwindow.cpp:181-188`) does the same and also clears
     `m_pendingTranslationCheck`.
   - `closeEvent` (`mainwindow.cpp:264-272`): emit `checkCancelled()` before hiding/closing, so the
     manager's `MainWindow` channel is aborted.
   - `retranslateUi()` (`mainwindow.cpp:229-248`): add the Check button text and tooltip; the panel
     retranslates itself.

5. **Wire it in `src/app/appcontroller.{h,cpp}`.**
   - `appcontroller.h`: declare `class ProofreadingReply;`, add
     `void checkInMainWindow(const QString &text);`, `void updateProofreadingUi();` and
     `QPointer<ProofreadingReply> m_mainWindowCheck;` next to `m_mainWindowReply`
     (`appcontroller.h:116`).
   - `appcontroller.cpp`: include `core/proofreadingservice.h` next to the manager header.
   - `start()` after the existing main-window connections (`appcontroller.cpp:125-128`): connect
     `checkRequested` → `checkInMainWindow` and `checkCancelled` →
     `m_proofreading->cancel(ProofreadingManager::Channel::MainWindow)`, then call
     `updateProofreadingUi()`.
   - `updateProofreadingUi()`: `const bool available = m_proofreading
     && m_proofreading->activeService() != nullptr;` and
     `m_mainWindow->setProofreadingAvailable(available && settings.proofreading.enabled);`
     plus `m_mainWindow->setProofreadingOptions(settings.proofreading.checkBeforeTranslation,
     settings.proofreading.autoReplace);` (guard `m_mainWindow`). Call it at the end of
     `applySettings()` (`appcontroller.cpp:650-654`, unconditionally and after
     `setActiveService()`) and connect `ProofreadingManager::activeServiceChanged` to it in
     `createProofreading()`.
   - `checkInMainWindow(text)`: guard `m_proofreading`; build
     `ProofreadingRequest{text, settings.proofreading.language, settings.proofreading.categories}`
     from `m_settings->current()` (`AppSettings{}` when there is no store), call
     `m_proofreading->check(request, ProofreadingManager::Channel::MainWindow)`, take the reply with
     `setParent(this)` and store it in `m_mainWindowCheck`, and connect `finished` exactly like
     `translateInMainWindow` (`appcontroller.cpp:838-853`): show the result only when
     `reply == m_mainWindowCheck` and the error is not `Cancelled`, then `deleteLater()`. Do not call
     `showProofreadingStarted()` — the window shows "Checking…" itself before it emits.
   - No change to `translateInMainWindow`, the translation settings, the popup or the in-place path.
     `prepareForQuit()` already calls `cancelAll()` (`appcontroller.cpp:1169-1170`).

6. **Write `tests/tst_proofreadingpanel.cpp` and register it.**
   - `tests/CMakeLists.txt`: after `tst_proofreadingmanager` (line 53), before
     `tst_queuedwebservice`:
     ```cmake
     qtranslate_add_test(tst_proofreadingpanel
         ${PROJECT_SOURCE_DIR}/src/ui/mainwindow.cpp
         ${PROJECT_SOURCE_DIR}/src/ui/mainwindow.h
         ${PROJECT_SOURCE_DIR}/src/ui/proofreadingpanel.cpp
         ${PROJECT_SOURCE_DIR}/src/ui/proofreadingpanel.h
         ${PROJECT_SOURCE_DIR}/src/ui/proofreadingerrortext.cpp
         ${PROJECT_SOURCE_DIR}/src/ui/proofreadingerrortext.h
         ${PROJECT_SOURCE_DIR}/src/ui/languagecombobox.cpp
         ${PROJECT_SOURCE_DIR}/src/ui/languagecombobox.h
         ${PROJECT_SOURCE_DIR}/src/ui/translationerrortext.cpp
         ${PROJECT_SOURCE_DIR}/src/ui/translationerrortext.h
     )
     target_link_libraries(tst_proofreadingpanel PRIVATE Qt::Widgets)
     set_tests_properties(tst_proofreadingpanel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
     ```
     `QTEST_MAIN` of a `QObject` gives a `QApplication` (Widgets linked), as `tst_popupwindow` and
     `tst_settingsdialog` already do. Register `TranslationRequest`/`TranslationResult`/`ProofreadingResult`
     metatypes in `initTestCase()`; no network and no real manager — the test emits and observes the
     window's signals and calls `showProofreadingResult()` with hand-built results.
   - Cases (each with an `issue()` helper building `ProofreadingIssue` with message/rule/replacements
     and a `result()` helper):
     1. `checkButtonVisibility` — hidden before `setProofreadingAvailable(true)`; clicking it with an
        empty source shows "Nothing to check" in the status label and emits nothing; with text it
        emits `checkRequested` carrying exactly the field text and the panel shows `Checking…`.
     2. `showsIssuesAndHighlights` — `showProofreadingResult` with two issues: two rows whose text
        contains the message and `Rule: <id>`, the panel is visible, the state label is empty, and
        `sourceText->extraSelections().size() == 2`.
     3. `noIssuesState` — an ok result with no issues shows "No issues found.", zero rows, zero
        highlights.
     4. `errorStates` — `ServiceUnavailable` with
        `Enable online text checking in Settings → Text checking.` and `RateLimited`: the state label
        contains that text / the rate-limit wording, no rows, no highlights, and the Check button
        stays visible and enabled.
     5. `replacementChoicesAndApply` — select row 1, assert the combo lists that issue's
        replacements, pick the second one, click Replace: the field text differs exactly at the
        issue's range, and a new `checkRequested` is emitted (spy count 1) while the old highlights
        are gone.
     6. `singleUndoAfterReplace` — after one Replace, `sourceText->undo()` once restores the exact
        original text.
     7. `ignoreRemovesRowOnly` — Ignore on row 0: rows and highlights decrease by one, the text is
        unchanged, and no `checkRequested` is emitted.
     8. `replaceAllAppliesAllAndSkips` — three issues where the middle one has no replacement:
        clicking Replace all shows the preview dialog found with
        `QApplication::activeModalWidget()`, its `proofreadingPreview` holds the expected corrected
        text; accepting applies it, one `undo()` restores the original, and no check is requested;
        cancelling leaves the text untouched.
     9. `copyCorrectedText` — Copy puts the corrected text on the clipboard, leaves the document and
        the highlight count alone.
     10. `autoReplaceAppliesOnce` — `setProofreadingOptions(false, true)` and a result with issues:
        the text is corrected immediately, one `undo()` restores it, no `checkRequested` arrives,
        and the list and the highlights are cleared; with `(false, false)` nothing is applied.
     11. `checkBeforeTranslationWithIssues` — `setProofreadingOptions(true, false)` and text: click
        Translate → one `checkRequested`, no `translateRequested`; then
        `showProofreadingResult` with issues shows the dialog; clicking `translateAnyway` emits
        `translateRequested` with the unchanged text; a second run clicking
        `applyCorrectionsAndTranslate` emits `translateRequested` with the corrected text and the
        field holds it; Escape emits neither.
     12. `checkBeforeTranslationWithoutIssues` — an ok empty result after the click emits
        `translateRequested` with no dialog; a failed result also translates (and shows the error).
     13. `checkBeforeTranslationOff` — with the option off the click emits `translateRequested`
        immediately and no `checkRequested`.
     14. `autoTranslateBypassesCheck` — the option on plus a short `setAutoTranslateDelay()`: typing
        emits `translateRequested` and no `checkRequested`.
     15. `editingClearsAndCancels` — after a result, typing in the source field clears the rows and
        the highlights and emits `checkCancelled()`; closing the window also emits it.
     16. `correctedTextRules` — the core helper: two non-overlapping issues applied from the end, an
        overlapping issue skipped deterministically, an out-of-range issue skipped, an empty
        replacement skipped, a length-0 insertion applied, and the original returned when nothing
        applies; plus `isApplicable()` for a range that ends at `text.size()`.
   - Modals: `QDialog::exec()` runs its own event loop on the offscreen platform; drive it with
     `QTimer::singleShot(0, ...)` that fetches `QApplication::activeModalWidget()`, checks the
     object names and clicks the button. Add one helper
     `static bool clickModalButton(const QString &objectName)` used by cases 8 and 11.
   - The window is created per case (`MainWindow window;`), never shown except where a dialog needs
     it; `QTEST_MAIN` + `#include "tst_proofreadingpanel.moc"`.

7. **Add the new strings to the 14 `.ts` files.**
   - Planned new messages (source baseline `grep -c '<message' i18n/qtranslate_ru_RU.ts` is 341):
     * `ProofreadingPanel` context (9): `Issues`, `Checking…`, `No issues found.`, `Replace`,
       `Ignore`, `Replace all`, `Copy corrected text`, `No suggestions`, `Rule: %1`.
     * `MainWindow` context (11): `Check`, `Check the source text`, `Nothing to check`,
       `Corrected text copied to the clipboard`, `Corrections applied`, `Check before translation`,
       `%1 issues found in the source text.`, `Apply corrections and translate`,
       `Translate anyway`, `Preview of corrections`, `Corrected text:`.
     * `ProofreadingErrorText` context (11, one per `TranslationError` value except `None`):
       `Network error`, `The text checking service did not respond in time`,
       `The text checking service rejected the request`,
       `The text checking quota is exceeded; try again later`,
       `Too many requests; try again later or use a self-hosted server`,
       `The text checking service is unavailable`, `The text is too long to check`,
       `The language is not supported by the text checking service`,
       `The text checking service returned an unexpected response`, `Text checking cancelled`,
       `There is no text to check`.
     Expected total then: **372** messages per file (341 + 31). The context is part of the key, so
     `Check before translation` and `Network error` are new messages even though other classes
     already have that text.
   - Update them with the CMake target, not by hand:
     `cmake --build build/linux-release --target update_translations` (the Linux configuration adds
     `src/platform/hotkeymanager_win.cpp` through `QTRANSLATE_LUPDATE_EXTRA_SOURCES`,
     `CMakeLists.txt:181-193`, which keeps the Windows-only `HotkeyManager` message alive).
   - Translate the new entries in all 14 files (machine-assisted first pass, per `i18n/README.md`),
     keep `%1` and punctuation, keep `LanguageTool` and the rule ids untranslated, then rebuild so
     `lrelease` regenerates the embedded `.qm`. `tst_translations` must stay green (0 unfinished,
     0 empty, 0 vanished) and `grep -c '<message' i18n/qtranslate_ru_RU.ts` must report the measured
     count — record the exact number in the outcome, as `04-text-checking-tab` did when its plan
     listed 18 and the implementation needed 25.

8. **Build, count and verify (Linux only).**
   - Configure and build Debug and Release with `-Wall -Wextra`
     (`cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release`,
     `cmake --build build/linux-release`), then run `tst_proofreadingpanel`, `tst_popupwindow`,
     `tst_settingsdialog`, `tst_translations` and finally
     `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`.
   - `ctest -N` must show **43** Linux tests (42 + the new platform-neutral target) and would show
     **38** on Windows (37 + 1); Windows is recorded as **not performed** (no MSVC on this machine).
   - Scope check: `git diff --stat` may show only `src/core/proofreadingtext.{h,cpp}`,
     `src/ui/proofreadingpanel.{h,cpp}`, `src/ui/mainwindow.{h,cpp}`,
     `src/ui/proofreadingerrortext.{h,cpp}`, `src/app/appcontroller.{h,cpp}`, `CMakeLists.txt`,
     `tests/tst_proofreadingpanel.cpp`, `tests/CMakeLists.txt` and the 14 `i18n/*.ts`. No service,
     manager, settings, `doc/`, `AGENTS.md`, README or `doc/handoff.md` change; no commit (the lead
     commits).

## Done when

- `src/core/proofreadingtext.{h,cpp}` provides `isApplicable()` and `corrected()` in
  `qtranslate_core`: the first replacement of every applicable issue, applied from the end, with
  out-of-range, empty and overlapping issues skipped deterministically (no split surrogate pair),
  and the original text returned when nothing applies.
- `src/ui/proofreadingpanel.{h,cpp}` shows the issues with message, rule id and replacement choices,
  the states checking / no issues / error (including provider unavailable and request limit), the
  four buttons and the empty-selection placeholder; it receives the error text through
  `ProofreadingErrorText::forResult(const ProofreadingResult &)`, emits exactly
  `issueSelected`, `replacementChosen`, `ignored`, `replaceAllRequested` and `copyCorrectedRequested`,
  and neither edits a document nor holds a manager, a service or settings.
- `src/ui/proofreadingerrortext.{h,cpp}` maps all twelve `TranslationError` values (`None` → an empty
  string) to checking wording with no "translation", "translate" or "API key" in it (`Auth` is
  `The text checking service rejected the request`; the other ten titles are the strings listed in
  step 2), appends a non-empty `result.errorMessage` as `"%1 (%2)"`, and leaves
  `TranslationErrorText` and its texts untouched.
- The main window has a Check button next to Translate, visible only while the mode is on and a
  provider is registered (`AppController::updateProofreadingUi()`, refreshed on every settings change
  and on `activeServiceChanged`); a check goes through `AppController` on the `MainWindow` channel
  with the language and categories of `[proofreading]`, a second request cancels the first and closing
  the window or editing the source cancels the open one.
- Issues are underlined in the source field via extra selections; the highlight count always equals
  the number of listed rows; clicking a row selects its fragment; the list and the highlights are
  cleared by a new check, by a translation, by an edit of the source, by an empty result, by Replace
  all, by automatic replacement and by turning the mode off.
- Replace applies the chosen replacement of one issue in one undo block (a single Ctrl+Z restores
  the exact previous text) and requests a fresh check instead of shifting offsets; Replace all shows
  a modal preview with the corrected text, applies the first replacement of every issue from the end
  in one undo block, skips issues without a replacement and does not re-check; Ignore removes one row
  and its highlight without touching the text; Copy corrected text puts the corrected text on the
  clipboard without changing the document.
- `proofreading/autoReplace` (default off) applies the corrections right after a completed check in
  one undo block, clears the now-stale list and highlights, and does not re-check the result; with
  the option off nothing is applied without an explicit click.
- Check before translation (default off, and only while the mode is on): the Translate button,
  Ctrl+Enter and the Ctrl+W path check the source text first, the automatic-translation timer does
  not; with no issues or a failed check the translation starts, with issues a modal dialog offers
  "Apply corrections and translate" (one undo block, then a translation of the corrected text) and
  "Translate anyway" (translation of the unchanged text), and nothing changes without a choice.
- `tests/tst_proofreadingpanel.cpp` is new, platform-neutral, `QT_QPA_PLATFORM=offscreen`, compiling
  `mainwindow`, `proofreadingpanel`, `proofreadingerrortext`, `languagecombobox` and
  `translationerrortext` like `tst_settingsdialog` (`tests/CMakeLists.txt:58-65`), and covers every
  case of step 6 — the four
  button flows, the highlight count, the replacement change, single undo, Replace all with the
  preview accepted and cancelled, Ignore, Copy, the states, autoReplace off/on, both check-before-
  translation branches, the auto-translate bypass, editing/close cancellation and the helper rules.
  `tst_popupwindow`, `tst_settingsdialog` and `tst_translations` stay green.
- Debug and Release build on Linux without warnings under `-Wall -Wextra` and the full suite passes;
  `ctest -N` shows 43 Linux tests and would show 38 on Windows (the new target is platform-neutral);
  Windows is recorded as not performed.
- The new `tr()` strings are in all 14 `i18n/*.ts` files with no unfinished, empty or vanished
  message, the Windows-only `HotkeyManager` message survives, `tst_translations` passes and the
  measured `grep -c '<message' i18n/qtranslate_ru_RU.ts` is recorded in the outcome (planned: 31 new
  messages — 9 panel, 11 window/dialog, 11 error titles — 372 total).
- No service, manager, settings, `doc/`, `AGENTS.md`, README, `doc/handoff.md` or root-plan change;
  no commit.

## Depends on

`03-languagetool-service` for a real provider and the manager wiring, and `04-text-checking-tab` for
the settings that switch the mode and the confirmation on.

## Risks

- **A widget list and a document can drift apart.** Every edit of the source field invalidates the
  stored offsets. The plan closes that with three guards: `m_applyingEdit` around our own edits, a
  full clear + `checkCancelled()` on any other `textChanged`, and clearing after Replace all /
  automatic replacement. Do not "just shift" the offsets after a replacement and do not keep a list
  whose offsets no longer match the text.
- **`QDialog::exec()` in a test.** No existing test drives a modal dialog, so this is the flake risk
  of the new target: offscreen Qt supports nested event loops, but the dialog must appear
  synchronously after the click (it does — `exec()` is called from the slot) and the helper must
  fail loudly (`QVERIFY(activeModalWidget())` inside the timer is not enough, so collect a flag and
  assert it after the action). If it proves flaky, keep the preview/confirmation windows as separate
  small classes shown with `open()` and connect their `accepted`/`rejected`, without changing the
  user-visible behaviour.
- **Linking `mainwindow.cpp` into a test.** There is no `tst_mainwindow*` today, so the new target is
  the first build of `MainWindow` outside `qtranslate`. Its dependencies are all already in the
  target list (`core/appinfo`, `core/languages`, `ui/languagecombobox`, `ui/translationerrortext`,
  `ui/proofreadingerrortext`), and it needs no `platform/` code, so the risk is low but real: if a `MainWindow` change later pulls
  in a platform or app file, the test needs that file added rather than the source split.
- **One undo step.** `QPlainTextEdit::setPlainText()` clears the undo stack, so no replacement may be
  implemented with `setPlainText()`; every edit must go through a `QTextCursor` inside one
  `beginEditBlock()`/`endEditBlock()`. A second `beginEditBlock()` per issue would break the single
  Ctrl+Z guarantee that the stage's "Done when" requires.
- **Check before translation must not loop.** The confirmation path re-emits `translateRequested`
  directly and never calls `requestTranslation()`, otherwise the check-before-translation branch
  would restart itself. A failed check translates anyway, so the user is never stuck with a Check
  that cannot finish.
- **The automatic-translation timer is excluded on purpose.** With `translation/autoTranslateDelayMs`
  set and check-before-translation on, a modal dialog per typing pause would make the window
  unusable; the node names the button, Ctrl+Enter and Ctrl+W only.
- **Two error vocabularies exist on purpose.** Translation and checking have separate title tables
  (`TranslationErrorText` / `ProofreadingErrorText`); do not merge them back and do not pass a mode
  flag into the translation one. The checking switch has no `default:` so a new `TranslationError`
  value is reported by `-Wswitch`; add it to both tables when it appears. `ProofreadingResult` reuses
  `TranslationError` and its `errorMessage`, so the two tables must stay keyed on the same enum.
  A manager suffix can repeat a title (the unsupported-language case), which is the accepted
  `"%1 (%2)"` rule, not a bug.
- **The `.ts` count is an estimate.** The planned 31 messages are the strings this layout and the
  error table need; `04-text-checking-tab` planned 18 and measured 25 (four group titles plus three
  manager strings). If the panel or a dialog needs one more label, the outcome records the measured
  total instead of forcing the plan's number — but every new string must be translated in all 14
  files and `tst_translations` must stay green.
- **The online gate is not a hidden button.** With `proofreading/allowOnlineServices` off the Check
  button stays visible and the manager's `Enable online text checking in Settings → Text checking.`
  appears in the panel — the stage wants that state visible, not a silently missing button.
- **Windows is not built here.** Record the count and the build as not performed, never as passed.

## Outcome

Done. The main window can check the source text with LanguageTool, show the issues, and apply or
ignore corrections while staying free of any manager, service or network knowledge.

- Files: new `src/core/proofreadingtext.{h,cpp}`, `src/ui/proofreadingpanel.{h,cpp}`,
  `src/ui/proofreadingerrortext.{h,cpp}` and `tests/tst_proofreadingpanel.cpp`; modified
  `src/ui/mainwindow.{h,cpp}`, `src/app/appcontroller.{h,cpp}`, `CMakeLists.txt`,
  `tests/CMakeLists.txt` and the 14 `i18n/*.ts`. No service, manager, settings, `doc/`, README,
  `AGENTS.md`, `doc/handoff.md`, root-plan or later-child change.
- Core: `ProofreadingText::isApplicable()` and `corrected()` apply the first replacement of every
  applicable issue from the end of the text to the beginning; out-of-range, empty and overlapping
  issues are skipped deterministically, a range may not split a surrogate pair at either end (not
  only the zero-length case), and the original text is returned when nothing applies.
- Panel: `ProofreadingPanel` shows the message plus `Rule: %1`, the replacement combo (a disabled
  `No suggestions` placeholder for an issue without replacements), the states checking / no issues /
  error (red) and Replace / Ignore / Replace all / Copy corrected text; it emits exactly
  `issueSelected`, `replacementChosen`, `ignored`, `replaceAllRequested` and
  `copyCorrectedRequested`, receives the error text through `ProofreadingErrorText::forResult()`,
  never edits a document and holds no manager, service or settings.
  `changeEvent(LanguageChange)` rebuilds the row texts and keeps the selected suggestion.
- Error table: `ProofreadingErrorText` covers all twelve `TranslationError` values (`None` → an
  empty string, `switch` without `default`), uses checking wording with no "translation",
  "translate" or "API key" (`Auth` is `The text checking service rejected the request`) and appends
  a non-empty `result.errorMessage` as `"%1 (%2)"`; `TranslationErrorText` is untouched.
- Main window: a Check button and a third splitter pane, visible only while the mode is on and a
  provider is registered; `checkRequested`/`checkCancelled` are the only contract. A check goes
  through `AppController` on `Channel::MainWindow` with `settings.proofreading.language` and
  `.categories`; the result is shown only for the current reply and not `Cancelled`. Every edit, a
  translation, the Ctrl+W/popup transfer, a new check and closing the window clear the list and the
  highlights and cancel the open request (`m_applyingEdit` protects our own edits). Issues are
  underlined through `QTextEdit::ExtraSelection`; the count equals the listed rows; a row click
  selects the fragment.
- Actions: Replace applies the chosen replacement of one issue in one edit block and requests a
  fresh check; Replace all previews the corrected text and applies all first replacements in one
  edit block without re-checking; Ignore drops the row and its highlight only; Copy corrected text
  puts the corrected text on the clipboard without touching the document. `autoReplace` applies
  the corrections right after a successful check in one edit block, clears the stale list and does
  not re-check.
- Check before translation: the Translate button, Ctrl+Enter and Ctrl+W check first; the
  automatic-translation timer does not. No issues or a failed check translate immediately; issues
  open a modal choice: "Apply corrections and translate" (one edit block, then a translation of the
  corrected text), "Translate anyway" or dismissal (no change). The path emits
  `translateRequested` directly and cannot loop.
- Review: six findings accepted and fixed. (1) The auto-translate timer could cancel an explicit
  check and fired on our own edits; Check now stops it, programmatic edits do not start it, and the
  direct translate stops it, with a test using a non-zero delay. (2) The panel did not rebuild the
  `Rule:` rows and reset the selected replacement on a language change; both are now preserved.
  (3) An empty/whitespace text after a replacement left the stale list; the empty `startCheck()`
  branch now clears the panel and the highlights. (4) `Nothing to check` stuck in the status bar
  after the next check; it is now cleared. (5) The clear paths under `QSignalBlocker` were
  untested; `translationAndDisableClearPanel` covers `showTranslationStarted()`, `translateText()`,
  `showTranslation()` and `setProofreadingAvailable(false)`. (6) The fixes for the empty branch and
  the status were initially untested; `checkButtonVisibility` asserts the cleared status and
  `emptyReplacementEmptiesTextAndPanel` covers the empty branch. No rejected findings and no open
  issues.
- Tests: `tests/tst_proofreadingpanel.cpp` is platform-neutral, `QT_QPA_PLATFORM=offscreen`, links
  `mainwindow`, `proofreadingpanel`, `proofreadingerrortext`, `languagecombobox` and
  `translationerrortext`; 19 named cases (+ init/cleanup = 21 passed) cover the four button flows,
  the highlight count, replacement and single undo, Replace all accepted/cancelled, Ignore, Copy,
  the states, autoReplace off/on, both check-before-translation branches, the auto-translate bypass
  and timer interaction, editing/close/translation cancellation, the empty-replacement clearing and
  the core helper rules. Modal dialogs are driven with `QTimer::singleShot(0)` +
  `activeModalWidget()`; 10 repeated ctest runs are green.
- Verification (Linux only): Debug and Release build with `-Wall -Wextra` and zero warnings;
  `xvfb-run -a ctest --test-dir build/linux-{release,debug} --output-on-failure` is 43/43 in both;
  `ctest -N` shows 43 tests (42 + `tst_proofreadingpanel`), so Windows would be 38 (37 + 1) and is
  **not performed** — no MSVC on this machine.
- `.ts`: `update_translations` added exactly **31** messages per file (9 `ProofreadingPanel`, 11
  `MainWindow`, 11 `ProofreadingErrorText`), so `grep -c '<message'` is **372** in all 14 files,
  0 unfinished, 0 empty, 0 vanished; `lrelease` reports 372 finished per file, the Windows-only
  `HotkeyManager` message survives, and no existing translation changed. The review fixes shifted a
  few `<location>` line numbers of `MainWindow`/`ProofreadingPanel`; they are informational and
  refresh at the next `lupdate`, while the messages and translations are complete.
- Decisions not obvious from the code: `startCheck()` does not reset `m_pendingTranslationCheck`
  itself (the manual Check button clears it before the call, `requestTranslation()` sets it and
  then calls `startCheck()`), so the check-before-translation flag survives to the result; the
  confirmation enum is ordered `Dismissed, ApplyCorrections, TranslateAnyway` so `QDialog::Rejected`
  maps to `Dismissed`; the dialogs stay `exec()`-based; programmatic corrections never start the
  automatic translation; `swapLanguages()` calls `requestTranslation(false)` and bypasses the
  check-before-translation question.

Notes for the later children:

- `07-external-selection-proofreading` reuses `ProofreadingText::corrected()` and the
  `Channel::InPlace`/panel pattern; it owns the external confirmation window.
- Stage 6 appends `llm` to `proofreadingProviderIds()` together with its service; the main window
  needs no change because `updateProofreadingUi()` follows `activeService()` and the settings.
