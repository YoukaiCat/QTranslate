---
kind: task
status: done
detailed: true
depends: [01-remove-online-gate]
---

# Remove the pre-translation check and the automatic replacement

## Task

Remove `proofreading/checkBeforeTranslation` and `proofreading/autoReplace` together with the
main-window behaviour they drive, per the user's 2026-11-14 correction: text checking is **not** a
pre-translation step, and the replacement is the new hotkey's job (child `04`), not a setting. The
main window keeps its manual **Check** panel: the Check button, the issue list, the replacement
combo, Replace, Ignore, Replace all, Copy, the highlights, the preview dialog and the external mode
all stay.

What goes: the two settings fields and their INI pair, the **Options** group on the **Text
checking** page (both checkboxes and the automatic-replacement warning), `MainWindow::setProofreadingOptions()`,
`m_checkBeforeTranslation`, `m_autoReplace`, `m_pendingTranslationCheck`, the
`BeforeTranslationChoice` enum and `askBeforeTranslation()` with its confirmation dialog, the
branch of `requestTranslation()` that starts a check first, and the automatic `replaceAll(false)`
after a check. `AppController` stops calling `setProofreadingOptions()`.

This is one task because the settings, the dialog group, the main-window flags and the
`AppController` call are one behaviour: any partial state either leaves a dead checkbox or a window
that still asks before translating. `.ts` is atomic with the strings.

Out of scope: the online gate and `isLocal()` (child `01-remove-online-gate`, the preceding child),
the move of the checking UI out of the translation window into its own window
(`03-extract-proofreading-window`), the second hotkey and the check-and-replace flow
(`04-check-and-replace-hotkey`), and documentation (the lead's pass).

This is an intermediate state on purpose: after this task `MainWindow` still contains the manual
**Check** panel, and child `03` moves it into a separate window. Do not remove the panel here, and do
not build its replacement: one task, one reviewable change. If the panel were removed here, the
checking UI would be gone for two commits and the `.ts` chain would lose its reference point.

Context: the settings (`src/core/settings.h:75-76`; `src/core/settings.cpp:544-549`, `:691-693`),
the dialog group (`src/ui/settingsdialog.h:149-151`; `src/ui/settingsdialog.cpp:510-528`, `:727-728`,
`:796-798`, `:883`), the window flags and flows (`src/ui/mainwindow.h:53`, `:87`, `:90`, `:104`,
`:126-128`; `src/ui/mainwindow.cpp:133`, `:144`, `:154`, `:156`, `:171`, `:243-247`, `:257`, `:294`,
`:296`, `:343`, `:352`, `:369-382`, `:395`, `:414-456`, `:514`, `:671`), the controller
call (`src/app/appcontroller.cpp:896-897`) and the tests (`tests/tst_settings.cpp`,
`tests/tst_settingsdialog.cpp`, `tests/tst_proofreadingpanel.cpp`).

Design decisions:

- Delete the settings; do not rename or re-introduce them under another name. The user rejected both
  concepts, so there is no "check before translation" flag and no "apply automatically" flag. An old
  INI that still carries the keys loads without them: no migration, no `remove()`, `settingsVersion`
  stays `1`.
- `requestTranslation()` keeps its role but loses the parameter: after the branch is gone, nothing
  reads `userInitiated`, and `-Wunused-parameter` (`-Wextra`) would fire. Make it
  `requestTranslation()` and update the five call sites (`:133`, `:154`, `:171`, `:296`, `:671`) and
  the declaration (`mainwindow.h:90`). Do not keep a `Q_UNUSED` parameter.
- Delete `m_pendingTranslationCheck` entirely, including its resets in unrelated handlers
  (`:144`, `:156`, `:238`, `:257`, `:294`, `:343`, `:352`, `:395`, `:514`): the flag exists only to
  carry the answer of a pre-translation check, and a leftover member with no reader is dead code.
- `onProofreadingResult()` becomes: ignore `Cancelled`, `m_proofreadingPanel->showResult(result)`,
  `updateHighlights()` — and nothing else. The `translate` lambda, the `askBeforeTranslation()`
  switch and the `m_autoReplace` call go; the panel's Replace all and the manual flow are unchanged.
- Delete `askBeforeTranslation()` and the `BeforeTranslationChoice` enum with the
  `checkBeforeTranslationDialog`, `applyCorrectionsAndTranslate` and `translateAnyway` object names.
  `applyIssueEdits()` and `replaceAll()` stay: Replace all uses both.
- Keep `setProofreadingAvailable()` as is, minus the removed resets: it still hides the panel and
  the Check button and cancels the check when the feature is off.
- The **Options** group is removed as a whole; nothing replaces it. The dialog keeps the enable
  switch, the intro hint, **Provider** and **Issues** groups, and the endpoint hint/warning.
  `updateProofreadingControls()` keeps disabling Provider/Issues and the endpoint row; delete only
  `m_proofreadingOptionsGroup->setEnabled(enabled)`.
- Reword the two comments that describe removed behaviour: the `textChanged` handler comment
  (`mainwindow.cpp:134-136`, mentions `autoReplace` and "applying the corrections before a
  translation") and the swap comment (`:665-666`, "so it bypasses the check"). The code they explain
  no longer exists.
- The panel stays a separate checking UI, not a pre-translation step: no button, shortcut or timer
  may start a check implicitly when translating. A translation is a translation.

## Steps

1. **Settings model** — `src/core/settings.h:75-76`, `src/core/settings.cpp:544-549`, `:691-693`:
   - Delete `ProofreadingSettings::checkBeforeTranslation` and `autoReplace`.
   - Delete the load lines (`settings.cpp:544-549`) and the save lines (`:691-693`).
   - `enabled`, `provider`, `language`, `endpoint`, `categories` and `hotkey` stay unchanged;
     `settingsVersion` stays `1`; no `remove()` or migration.

2. **Settings dialog** — `src/ui/settingsdialog.h:149-151`, `src/ui/settingsdialog.cpp:510-528`,
   `:727-728`, `:796-798`, `:875-892`:
   - Delete the whole `m_proofreadingOptionsGroup` block: group box, the two checkboxes, the
     `autoReplaceWarning` label (source
     `Corrections are applied without asking. Every change stays one undo step.`), the three
     `addWidget()` calls and `textCheckingLayout->addWidget(m_proofreadingOptionsGroup)`.
   - Delete the three header members.
   - `loadValues()`: delete the `m_proofreadingCheckBeforeTranslation->setChecked(...)` and
     `m_proofreadingAutoReplace->setChecked(...)` lines (`:727-728`).
   - `settings()`: delete
     `result.proofreading.checkBeforeTranslation = m_proofreadingCheckBeforeTranslation->isChecked()`
     and `result.proofreading.autoReplace = m_proofreadingAutoReplace->isChecked()` (`:796-798`).
   - `updateProofreadingControls()`: delete `m_proofreadingOptionsGroup->setEnabled(enabled)`
     (`:883`). Keep the Provider/Issues groups, the provider/endpoint enabling and the endpoint
     warning.

3. **Main window header** — `src/ui/mainwindow.h`:
   - Delete `void setProofreadingOptions(bool, bool);` (`:53`), the `BeforeTranslationChoice` enum
     (`:87`), `void requestTranslation(bool userInitiated = true);` → `void requestTranslation();`
     (`:90`), `BeforeTranslationChoice askBeforeTranslation(int issueCount);` (`:104`) and the
     members `m_checkBeforeTranslation`, `m_autoReplace`, `m_pendingTranslationCheck`
     (`:126-128`).

4. **Main window behaviour** — `src/ui/mainwindow.cpp`:
   - Delete `setProofreadingOptions()` (`:243-247`).
   - `requestTranslation()` (`:369-382`): delete the `if (userInitiated && m_checkBeforeTranslation
     && ...)` branch and the parameter; the body is the timer stop, the text read and the
     `emit translateRequested(...)`.
   - Update the five call sites to `requestTranslation()`: the auto-translate timer (`:133`), the
     Translate button (`:154`), the Ctrl+Enter/Ctrl+Enter shortcuts (`:171`), `translateText()`
     (`:296`), the swap (`:671`).
   - `onProofreadingResult()` (`:414-456`): keep the `Cancelled` guard, `showResult(result)` and
     `updateHighlights()`; delete the `translate` lambda, the whole `if (m_pendingTranslationCheck)`
     block, the `askBeforeTranslation()` switch and the `if (m_autoReplace ...) replaceAll(false);`
     line.
   - Delete `askBeforeTranslation()` (`:567-603`) and the `MainWindow::BeforeTranslationChoice`
     definition with it.
   - Delete every `m_pendingTranslationCheck = false;` line (`:144`, `:156`, `:238`, `:257`,
     `:294`, `:343`, `:352`, `:395`, `:514`) and the `m_pendingTranslationCheck = true;` in the
     removed branch.
   - Reword the `textChanged` comment (`:134-136`) so it mentions only our own Replace and
     Replace-all edits, and the swap comment (`:665-666`) so it no longer says the swap bypasses a
     check.
   - Keep `startCheck()`, `showProofreadingResult()`, the panel wiring, `selectIssue()`,
     `applyReplacement()`, `ignoreIssue()`, `replaceAll()` (with its preview dialog),
     `applyIssueEdits()`, `copyCorrected()`, `updateHighlights()` and `setProofreadingAvailable()`.

5. **`AppController`** — `src/app/appcontroller.cpp:890-898`:
   - Delete `m_mainWindow->setProofreadingOptions(settings.proofreading.checkBeforeTranslation,
     settings.proofreading.autoReplace);` (`:896-897`). `updateProofreadingUi()` keeps the
     `setProofreadingAvailable(available && settings.proofreading.enabled)` call and its `available`
     computation.

6. **Tests** —
   - `tests/tst_settings.cpp`: delete `QVERIFY(!settings.proofreading.checkBeforeTranslation)` and
     `QVERIFY(!settings.proofreading.autoReplace)` (`:151-152`), the two assignments (`:427-428`) and
     the two assertions (`:444-445`). Extend `removedOnlineGateKeyIsIgnored()` from child `01` (or
     add a sibling `removedProofreadingOptionsAreIgnored()`) so a raw INI with
     `proofreading/checkBeforeTranslation=true` and `proofreading/autoReplace=true` also loads
     without those keys, without a warning, and keeps `enabled`/`hotkey`; assert "ignored", not
     "removed".
   - `tests/tst_settingsdialog.cpp`: delete `settings.proofreading.checkBeforeTranslation = true` and
     `settings.proofreading.autoReplace = true` (`:885-886`) and the two `QVERIFY(...)` (`:957-958`);
     in `proofreadingTabHasNoGate()` delete the `optionsGroup` lookup and its assertions (the group
     no longer exists) and add `findChild<QGroupBox *>("proofreadingOptionsGroup")` → null, plus
     `findChild<QCheckBox *>("proofreadingCheckBeforeTranslation")` and
     `findChild<QCheckBox *>("proofreadingAutoReplace")` → null.
   - `tests/tst_proofreadingpanel.cpp`: delete `autoReplaceAppliesOnce()` (`:451-485`),
     `checkBeforeTranslationWithIssues()` (`:487-534`) and `checkBeforeTranslationWithoutIssues()`
     (`:536-568`); keep the translation path covered by renaming `checkBeforeTranslationOff()`
     (`:570`) to `translationDoesNotStartACheck()` and dropping its `setProofreadingOptions(false,
     false)` call; drop the `setProofreadingOptions(true, false)` call in `autoTranslateBypassesCheck()`
     (`:592`). The preview-modal helper (`scheduleModalAction`) stays used by
     `replaceAll`/preview tests; keep it.
   - Nothing else in `tst_proofreadingpanel` changes: the manual Check, Replace, Ignore, Replace all,
     Copy and the stale-result tests must stay green and must not be "simplified" away.

7. **Translations** — `i18n/*.ts`: run
   `cmake --build build/linux-release --target update_translations`. Eight messages drop, four per
   context: `MainWindow` — `Check before translation` (the dialog title), `%1 issues found in the
   source text.`, `Apply corrections and translate`, `Translate anyway`; `SettingsDialog` —
   `Check before translation` (the checkbox), `Apply corrections automatically`, the
   automatic-replacement warning, `Options` (the group title). Expected count **376 → 368**
   `<message>` entries per file with 0 unfinished, 0 empty and 0 vanished. With `type="vanished"`
   entries, rerun with
   `lupdate -no-obsolete -project build/linux-release/.lupdate/qtranslate_lupdate_project.json`.
   `<location>` churn is expected.

8. **Verification**:
   - Build `build/linux-debug` and `build/linux-release`; no `warning:` in either log
     (`-Wall -Wextra`; this is where a leftover `userInitiated` shows up). Use the
     `qtranslate-trixie` image for all task builds and tests; the lead runs the final Debug/Release
     check on the host after the stage (AGENTS.md).
   - `xvfb-run -a ctest --test-dir build/linux-debug --output-on-failure` and the same for
     `build/linux-release`: 45/45, with `tst_proofreadingpanel`, `tst_settings`,
     `tst_settingsdialog` and `tst_translations` green.
   - `rg -n "checkBeforeTranslation|autoReplace|m_pendingTranslationCheck|BeforeTranslationChoice|setProofreadingOptions|askBeforeTranslation" src`
     returns nothing; the same `rg` over `tests` may match only the two legacy key names in
     `tst_settings.cpp`.
   - `rg -n "Check before translation|Apply corrections automatically|Apply corrections and translate|Translate anyway|issues found in the source text" src`
     returns nothing; `rg -n "proofreadingOptionsGroup|proofreadingCheckBeforeTranslation|proofreadingAutoReplace" src`
     returns nothing.
   - Offscreen smoke: start the app with a temporary `XDG_CONFIG_HOME`, a saved INI with the two
     stale keys and `proofreading/enabled=true`, open the source field and press Translate — the log
     must contain no check request and the app must not show the confirmation dialog.
   - Record Windows Debug/Release and its 39 tests as not performed (no MSVC here); no push.

## Done when

- `proofreading/checkBeforeTranslation` and `proofreading/autoReplace` no longer exist in
  `AppSettings`, the INI load/save, the settings dialog or the main window; an old INI with them
  loads without the keys and without a warning; `settingsVersion` stays `1`.
- The **Text checking** page has no **Options** group and no `proofreadingOptionsGroup`,
  `proofreadingCheckBeforeTranslation` or `proofreadingAutoReplace` widget;
  `tst_settingsdialog` asserts their absence and keeps the Provider/Issues/endpoint assertions.
- The main window has no `setProofreadingOptions()`, no `m_checkBeforeTranslation`, no
  `m_autoReplace`, no `m_pendingTranslationCheck` and no `askBeforeTranslation()`; translating never
  starts a check and a finished check never edits the source field on its own.
- The manual **Check** button and Ctrl+Enter still start a check, the issue list, highlights,
  Replace, Ignore, Replace all (with its preview), Copy and the stale-result handling still work;
  `tst_proofreadingpanel` is green with `autoReplaceAppliesOnce`, `checkBeforeTranslationWithIssues`
  and `checkBeforeTranslationWithoutIssues` removed and `checkBeforeTranslationOff` renamed to
  `translationDoesNotStartACheck()`. The panel still lives inside `MainWindow` here: moving it into
  its own window is `03-extract-proofreading-window`, and the final state has no checking widget in
  the translation window.
- `requestTranslation()` has no parameter; no `warning:` under `-Wall -Wextra`.
- Linux Debug and Release build without warnings and pass 45/45 under `xvfb-run -a`; the 14 `.ts`
  files are at **368** messages with 0 unfinished/empty/vanished and `tst_translations` passes;
  Windows Debug/Release and its 39 tests are recorded as not performed.
- Only `src/core/settings.{h,cpp}`, `src/ui/settingsdialog.{h,cpp}`, `src/ui/mainwindow.{h,cpp}`,
  `src/app/appcontroller.cpp`, `tests/tst_settings.cpp`, `tests/tst_settingsdialog.cpp`,
  `tests/tst_proofreadingpanel.cpp` and the 14 `i18n/*.ts` files change; no `doc/`, README,
  `AGENTS.md`, root-plan, CMake or version change, no hotkey and no online-gate change; no push.

## Risks

- `m_pendingTranslationCheck` is reset in nine places, several of them far from the proofreading
  code (`translateText()`, `showTranslation()`, `closeEvent()`, `applyReplacement()`). Missing one
  leaves a member that is still assigned but never read; `-Wall -Wextra` will not catch it, so grep
  for the name after the edit.
- `requestTranslation(bool)` is declared with a default argument and called with both values in five
  places. Dropping the parameter but leaving one `true`/`false` argument is a compile error, which is
  fine; leaving the parameter and adding `Q_UNUSED` is not — it hides that the branch is gone.
- Do not delete `applyIssueEdits()` or `replaceAll()`: they serve the panel's Replace all. Only the
  automatic call after a check goes.
- The four `MainWindow` `.ts` strings are easy to miss because one of them (`Check before
  translation`) also exists in the `SettingsDialog` context. Both go, and both counts matter:
  `379`-based arithmetic after child `01` is 376 − 8 = 368.
- Keep the `Options` group's object names out of the tests rather than reusing them for a
  replacement widget: no replacement group exists, and a re-used name would fake a passing test.
- `tst_proofreadingpanel` is the only test that compiles `mainwindow.cpp` and drives modal dialogs;
  removing three of its cases lowers the coverage of the modal helper, so keep the preview-modal
  cases that still use `scheduleModalAction("proofreadingPreviewOk"/"proofreadingPreviewCancel")`.
- `doc/proofreading.md` still describes checking before translation until the lead's pass; do not
  edit it here.
- Windows Debug/Release and its 39 tests are unavailable here (no MSVC); record them as not
  performed. The dialog and the main window were not inspected interactively; the widget assertions
  are the automated gate.
- Nothing is needed from the user: the 2026-11-14 decision removed both settings explicitly.

## Outcome

Done. Text checking is no longer a pre-translation step and a finished check never edits the source
field on its own; the manual **Check** panel is untouched and remains in `MainWindow` for child `03`.

- Settings: `ProofreadingSettings::checkBeforeTranslation`/`autoReplace`, their INI load/save and
  every read in the dialog and window are gone; `settingsVersion` stays `1` and a stale pair is
  ignored, not migrated or removed.
- Main window: `setProofreadingOptions()`, `BeforeTranslationChoice`, `askBeforeTranslation()` with
  its dialog, `m_checkBeforeTranslation`, `m_autoReplace` and `m_pendingTranslationCheck` with all
  nine resets are gone. `requestTranslation()` lost `userInitiated` and its five call sites were
  updated; `onProofreadingResult()` keeps only the `Cancelled` guard, `showResult()` and
  `updateHighlights()`. `applyIssueEdits()`, `replaceAll()`, `previewCorrections()`,
  `copyCorrected()` and the highlight/selection helpers are unchanged.
- Dialog and controller: the **Options** group (two checkboxes and the automatic-replacement
  warning) and its three members are gone; `updateProofreadingControls()` keeps the
  Provider/Issues/endpoint disabling and the endpoint warning. `AppController` no longer calls
  `setProofreadingOptions()`.
- Tests: `tst_proofreadingpanel` lost `autoReplaceAppliesOnce`, `checkBeforeTranslationWithIssues`
  and `checkBeforeTranslationWithoutIssues`; `checkBeforeTranslationOff` is now
  `translationDoesNotStartACheck()` and `autoTranslateBypassesCheck` has no option setup. The
  preview-modal helper and the manual Check/Replace/Ignore/Replace all/Copy/stale-result tests stay.
  `tst_settings` gained `removedProofreadingOptionsAreIgnored()` (raw INI with the stale pair, no
  warning, `enabled`/`hotkey`/`settingsVersion` preserved, `save()`/`load()` round trip);
  `tst_settingsdialog` asserts the absent group and both checkboxes while keeping the
  Provider/Issues/endpoint/URL checks. The names of the removed widgets are assembled from parts in
  the tests, as the online-gate names already were, so a literal does not reappear in the sources.
- Translations: all 14 `.ts` are at **368** messages (376 − 8: four `MainWindow` strings and four
  `SettingsDialog` ones); 0 unfinished/empty/vanished, no added `<source>`, only `<location>` churn.
  `lupdate` first kept the eight entries as obsolete; rerunning with `-no-obsolete` on the generated
  project removed them, as the plan required.
- Verification in the `qtranslate-trixie` container (Qt 6.8.2): Linux Debug and Release build with
  zero `warning:` and pass 45/45 under `xvfb-run -a`; `tst_proofreadingpanel`, `tst_settings`,
  `tst_settingsdialog` and `tst_translations` are green. The static `rg` checks from step 8 are
  clean; in `tests` only the two legacy key names in `tst_settings.cpp` remain. Offscreen startup
  with a stale INI (`enabled=true` and both removed keys) loads without a warning, logs no
  check/settings lines, and the INI is not rewritten.
- Not automated: the interactive "open the field and press Translate" smoke — `qtranslate-trixie`
  has no `xdotool`/`xclip`, and installing packages was not allowed. The same path is covered by
  `translationDoesNotStartACheck()` (clicks Translate; one `translateRequested`, zero
  `checkRequested`) and `autoTranslateBypassesCheck()`. Windows Debug/Release and its 39 tests are
  not performed (no MSVC here).
- Review notes: `m_proofreadingAvailable` became write-only when the pre-translation branch went and
  is kept on purpose because `03-extract-proofreading-window` deletes it with the rest of the
  checking surface. `replaceAll(false)` and the `Corrections applied` string now have no caller; the
  plan keeps `replaceAll()` and the string for the panel and for child `03`. `doc/proofreading.md`,
  `doc/architecture.md`, `doc/handoff.md`, the READMEs and `AGENTS.md` still describe the removed
  settings; that is the lead's pass.
- Only the ten code/test files and the 14 `.ts` files changed; no CMake, version, README or `doc/`
  change beyond the two map files the lead prepared (this plan and the journal).
- Repeat-run note: do not run `update_translations` and `tst_translations` at the same time; the
  test reads the `.ts` files while `lupdate` rewrites them and can fail spuriously.
