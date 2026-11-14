---
kind: task
status: done
detailed: true
depends: [02-remove-pre-translation-flow]
---

# Extract the checking window

## Task

Make text checking its own window and its own mode, per the user's 2026-11-14 clarification: one
window for translation, one for checking, never one window for both. `MainWindow` loses the whole
checking surface; a new top-level `ProofreadingWindow` owns the source editor, the **Check** button,
the reused `ProofreadingPanel`, the issue list, Replace/Ignore/Replace all (with the preview
dialog)/Copy corrected text, the highlights, the one-undo-step edits, the checking/no-issues/error
states and the status messages. The tray menu gets **Check text…**, the only entry point.
`AppController` owns both windows, routes the window's `checkRequested()` to the manager and the
result back, and hides and cancels it when the feature is switched off. The manager's
`MainWindow` request channel becomes `Window`.

Why this is one task: the removal from `MainWindow`, the new window, the tray item, the
`AppController` wiring, the channel rename and the moved tests are one behaviour. A partial state
either keeps two checking UIs or leaves none, and the `.ts` cannot be separated from the moved
strings.

Out of scope: the check-and-replace hotkey (`04-check-and-replace-hotkey`), the external-selection
popup and its flow (`ProofreadingPopupWindow` stays byte-identical), the settings tab and the hotkey
row, and documentation (the lead's pass).

Context: the surface to move is `src/ui/mainwindow.h:3` (the `proofreadingtypes.h` include), `:10`
(the `ProofreadingPanel` forward declaration), `:52-53`, `:59-60`, `:78-79`, `:96-106`, `:115`,
`:117`, `:125`, `:129` and `src/ui/mainwindow.cpp:5-9` (includes), `:90-98`, `:107-110`, `:119`,
`:136-149`, `:155-168`, `:230-247`, `:257-258`, `:292-296`, `:322-323`, `:341-345`, `:350-354`,
`:387-455`, `:457-478`, `:480-493`, `:495-516`, `:518-523`, `:525-537`, `:539-565`, `:605-645`,
`:647-652`. All these line numbers are from `3e75331`: child `02` has already deleted
`setProofreadingOptions()`, the `requestTranslation()` branch, `askBeforeTranslation()`,
`m_checkBeforeTranslation`, `m_autoReplace` and `m_pendingTranslationCheck`, so those exact ranges
have shrunk — look the code up by name, not by line. The window, panel and popup already separate the
UI from the manager (`src/ui/proofreadingpanel.h`, `src/ui/proofreadingpopupwindow.h`), so this is a
move, not a rewrite.

Design decisions:

- `ProofreadingWindow : public QMainWindow`, no parent, not modal. Layout: a horizontal `QSplitter`
  with the source `QPlainTextEdit` (object name `sourceText`, accessible name `Text to check`,
  placeholder `Enter text to check`) on the left and the `ProofreadingPanel` (object name
  `proofreadingPanel`) on the right; a bottom bar with the **Check** button (object name `check`,
  tooltip `Check the source text`); the status bar with a `status` label for `Nothing to check` and
  `Corrections applied`. The window title is `Text checking`.
- The public contract mirrors the old `MainWindow` one: `setProofreadingAvailable(bool)` (shows or
  hides the panel and the Check button, clears and emits `checkCancelled()` when it goes off),
  `showProofreadingResult(const ProofreadingResult &)`, signals `checkRequested(const QString &)` and
  `checkCancelled()`, and `showAndActivate()` (show, raise, activate, focus the editor). It includes
  and knows nothing about `ProofreadingManager`, services, the network or the settings.
- The private code moves from `MainWindow` almost verbatim: `startCheck()`,
  `onProofreadingResult()` (ignore `Cancelled`, show the result, repaint highlights),
  `updateHighlights()`, `selectIssue()`, `applyReplacement()`, `ignoreIssue()`, `replaceAll()`,
  `previewCorrections()`, `applyIssueEdits()`, `copyCorrected()`, `setStatus()` and the
  `m_applyingEdit` guard. The only edits are the removed pre-translation leftovers (child `02`
  already took them out) and `m_sourceEdit` becoming the window's own editor.
- Keep the object names the tests use: `sourceText`, `check`, `status`, `proofreadingPreviewDialog`,
  `proofreadingPreview`, `proofreadingPreviewOk`, `proofreadingPreviewCancel`, plus the panel's own
  names. `ProofreadingPanel` itself is unchanged: it still supports its external mode for the popup.
- Editing the window's text cancels the running check, drops the issue list and the highlights; the
  window's own Replace/Replace all edits are guarded by `m_applyingEdit`. Closing the window cancels
  the check, clears the panel and hides the window (it stays alive for the next open); the text is
  kept, so it works as a scratchpad. The same cleanup runs when `setProofreadingAvailable(false)`
  arrives.
- Ctrl+Return and Ctrl+Enter (keypad) start a check inside this window. The translation window keeps
  Ctrl+Enter for translating; no shortcut is shared across windows.
- No language, category or provider selector in the window: a request uses
  `[proofreading] language`/`categories`, the same way the popup flow does. Rejected: moving the
  Text checking tab into the window (duplicate settings and strings); a detected-language hint is a
  separate task.
- No window geometry persistence and no new settings key: the window is created when first opened,
  reused while the app runs and recreated on the next start. Rejected: a `[proofreading]` geometry
  group (a new format for no user request).
- Entry point: the tray item **Check text…** (object name `checkText`, signal
  `checkTextRequested()`), directly after **Show window**, hidden until checking is enabled and a
  provider is registered. No CLI command: the user named the tray as the entry point, the hotkeys
  cover the external selection, and a new wire line would be a protocol change for no requested use
  case. Known limitation: without a system tray the window has no entry point (the application
  already warns that the tray is unavailable; the hotkeys and the popup still work).
- `AppController` keeps the window in `std::unique_ptr<ProofreadingWindow> m_proofreadingWindow`,
  creates it lazily in `showProofreadingWindow()`, and is the only place that knows both windows.
  `updateProofreadingUi()` now sets the tray item's visibility and the window's availability instead
  of `MainWindow`'s; it must be called once more after the tray is created (the current call at
  `appcontroller.cpp:137` runs before the tray block at `:140`).
- The checking request channel is renamed `ProofreadingManager::Channel::MainWindow` →
  `Channel::Window`, because the request no longer belongs to the translation window; `channelName()`
  logs `window`. `AppController::checkInMainWindow()` → `checkInProofreadingWindow()` and
  `m_mainWindowCheck` → `m_windowCheck`.
- On `proofreading.enabled` going off, hide the window and cancel its check (the old code cleared the
  main-window panel for the same reason); on quit hide it like the translation popup. A UI language
  change must retranslate the window and **keep** its text and issues — it is not a translation
  popup and must not be reset like `m_popup` in `applyUiLanguage()`.
- Tests: the checking-window coverage is the old `tst_proofreadingpanel` moved to
  `tst_proofreadingwindow`, minus the cases that coupled the panel to translation (auto-translate,
  `showTranslationStarted()`, `translateText()`, `showTranslation()`). `tst_mainwindow` is new and
  small: it proves the translation window has no checking UI and keeps the translation coverage that
  `tst_proofreadingpanel` accidentally gave `mainwindow.cpp` (translate request, auto-translate,
  copy). The suite becomes 46 Linux / 40 Windows tests.

## Steps

1. **New window files** — `src/ui/proofreadingwindow.h`, `src/ui/proofreadingwindow.cpp`:
   - Class comment: the checking mode as a separate window; it emits `checkRequested()` and
     `checkCancelled()` and never talks to a manager, a service, the network or the settings.
   - Build the layout as described: splitter, editor, panel (`setObjectName("proofreadingPanel")`),
     bottom bar with Check, status bar label `status`, window title `Text checking`. Give the panel a
     sensible minimum width (the old splitter pane was limited to 240-320 px) and `resize()` the
     window to about 720 × 380. Start with the panel and the button hidden until
     `setProofreadingAvailable(true)`.
   - Move `startCheck()`, `onProofreadingResult()`, `showProofreadingResult()`,
     `updateHighlights()`, `selectIssue()`, `applyReplacement()`, `ignoreIssue()`, `replaceAll()`,
     `previewCorrections()`, `applyIssueEdits()`, `copyCorrected()`, `setStatus()` and the connects
     of the panel's five signals from `src/ui/mainwindow.cpp` (`:155-168`, `:387-455`, `:457-478`,
     `:480-493`, `:495-516`, `:518-523`, `:525-537`, `:539-565`, `:605-645`, `:647-652`).
   - `textChanged` handler: on user edits clear the panel, drop the highlights and emit
     `checkCancelled()`, skipped while `m_applyingEdit` is set (the old `mainwindow.cpp:136-149`
     logic without the translation parts).
   - `closeEvent`: emit `checkCancelled()`, clear the panel and the highlights, then accept the close
     (the widget hides, it is not deleted). `changeEvent`: `QEvent::LanguageChange` →
     `retranslateUi()`; re-apply the window title, the Check button text/tooltip, the editor
     accessible name and placeholder inside.
   - Ctrl+Enter shortcuts (`Qt::Key_Return` and `Qt::Key_Enter`) → `startCheck()`.
   - `showAndActivate()`: `show()`, `raise()`, `activateWindow()`, `m_sourceEdit->setFocus()`.

2. **Remove the checking surface from the translation window** — `src/ui/mainwindow.h`,
   `src/ui/mainwindow.cpp`:
   - Header: delete the `proofreadingtypes.h` include (`:3`), the `ProofreadingPanel` forward
     declaration (`:10`), `setProofreadingAvailable()` (`:52`), `showProofreadingResult()` (`:60`),
     the `checkRequested`/`checkCancelled` signals (`:78-79`), `startCheck()`,
     `onProofreadingResult()`, `updateHighlights()`, `selectIssue()`, `applyReplacement()`,
     `ignoreIssue()`, `replaceAll()`, `copyCorrected()`, `previewCorrections()`,
     `applyIssueEdits()` (`:96-106`), the members `m_proofreadingPanel` (`:115`), `m_checkButton`
     (`:117`), `m_proofreadingAvailable` (`:125`) and `m_applyingEdit` (`:129`), and fix the class
     comment (`:18-22`), which mentions checking. `setProofreadingOptions()` and the
     pre-translation members are already gone (child `02`): do not reintroduce or re-delete them.
   - `buildUi()`: delete the panel pane and the splitter's third widget (`:90-98`), the Check button
     (`:107-110`, `:119`) and the panel/Check connects (`:155-168`).
   - Delete `setProofreadingAvailable()` (`:230-241`), the panel/highlight/cancel lines in
     `showTranslationStarted()` (`:255-258`), `translateText()` (`:292-296`), `showTranslation()`
     (`:341-345`) and `closeEvent()` (`:350-354`), the Check text in `retranslateUi()` (`:322-323`)
     and the `m_applyingEdit` guard in the `textChanged` handler (`:136-142`), keeping the
     auto-translate part.
   - Delete the whole checking block `startCheck()` … `copyCorrected()` (`:387-652`), including the
     preview dialog and `applyIssueEdits()`. After child `02` this block no longer contains
     `askBeforeTranslation()` or the pre-translation branch of `onProofreadingResult()`.
   - Remove the includes that only served it: `core/proofreadingtext.h`, `ui/proofreadingpanel.h`,
     `QColor`, `QDialog`, `QDialogButtonBox`, `QTextCharFormat`, `QTextEdit` and `<algorithm>`. Keep
     `QTextCursor` (the editor's `moveCursor()` in `translateText()`/`showTranslation()`).
   - The window keeps languages, services, translate, copy, swap, auto-translate, status bar, the
     translation error text and `setStatus()`.

3. **Tray menu item** — `src/ui/trayicon.h`, `src/ui/trayicon.cpp`:
   - Add `void setCheckTextVisible(bool visible);`, the signal `void checkTextRequested();`, the
     member `QAction *m_checkTextAction = nullptr;` and the forward declaration it needs.
   - In the constructor, insert `m_checkTextAction = m_menu->addAction(tr("Check text…"));` directly
     after **Show window** (before the **Service** submenu), with object name `checkText`, hidden by
     default, connected to `checkTextRequested`.
   - `retranslate()`: `m_checkTextAction->setText(tr("Check text…"))`. `setCheckTextVisible()` uses
     `setVisible()`/`setEnabled()` like `setHotkeysPaused()` uses the signal blocker — a plain
     `setVisible()` is enough, no extra state.

4. **`AppController`** — `src/app/appcontroller.h`, `src/app/appcontroller.cpp`:
   - Header: forward-declare `ProofreadingWindow`, add `void showProofreadingWindow();`, rename
     `checkInMainWindow()` → `checkInProofreadingWindow()`, add the
     `std::unique_ptr<ProofreadingWindow> m_proofreadingWindow;` member and rename
     `m_mainWindowCheck` → `m_windowCheck` (comment: the check the checking window waits for).
   - `start()`: drop the `MainWindow::checkRequested`/`checkCancelled` connects (`:131-136`); keep
     `updateProofreadingUi()`; connect the tray's `checkTextRequested` to
     `showProofreadingWindow()` and call `updateProofreadingUi()` again after the tray block
     (`:140-148`) so the item gets its visibility.
   - `showProofreadingWindow()`: create the window lazily, connect `checkRequested` →
     `checkInProofreadingWindow` and `checkCancelled` → `m_proofreading->cancel(Channel::Window)`,
     call `updateProofreadingUi()` on the new window, then `showAndActivate()`.
   - `checkInMainWindow()` → `checkInProofreadingWindow()`: same body, `Channel::Window`,
     `m_windowCheck`, result to `m_proofreadingWindow->showProofreadingResult(...)`.
   - `updateProofreadingUi()`: compute `available` as today, then
     `if (m_proofreadingWindow) m_proofreadingWindow->setProofreadingAvailable(available);` and
     `if (m_tray) m_tray->setCheckTextVisible(available);`; if `!available` also hide the window. Do
     not touch `m_mainWindow` here.
   - `applySettings()`: when `oldSettings.proofreading.enabled && !newSettings.proofreading.enabled`,
     hide the window in addition to `closeProofreadingPopup()` (the availability update clears it and
     cancels the check).
   - `prepareForQuit()`: hide `m_proofreadingWindow` next to `m_popup`.
   - `applyUiLanguage()`: the window is a top-level and gets `LanguageChange` itself; do **not** reset
     it like `m_popup` — its text and issues are user data.
   - `showSettings()` and `showNonModalMessage()` keep using `m_mainWindow.get()` as the parent.

5. **Manager channel rename** — `src/core/proofreadingmanager.h:20-23`,
   `src/core/proofreadingmanager.cpp:45-55`, `:206`, `:215` and the comment above `check()`:
   `Channel::MainWindow` → `Channel::Window`, `channelName()` → `"window"`. Keep `InPlace` and every
   other behaviour.

6. **CMake** — `CMakeLists.txt:103-106`: add `src/ui/proofreadingwindow.cpp` and
   `src/ui/proofreadingwindow.h` to the `qtranslate` sources next to `proofreadingpanel`.
   `tests/CMakeLists.txt:54-67`: rename the target `tst_proofreadingpanel` →
   `tst_proofreadingwindow`, point it at `tests/tst_proofreadingwindow.cpp` and the new window's
   sources instead of `mainwindow.cpp`, keep `proofreadingpanel.cpp`, `proofreadingerrortext.cpp`,
   `Qt::Widgets` and the offscreen environment. Add a `tst_mainwindow` target with
   `tests/tst_mainwindow.cpp`, `mainwindow.cpp`, `languagecombobox.cpp`, `translationerrortext.cpp`,
   `proofreadingpanel.cpp` and `proofreadingerrortext.cpp`, `Qt::Widgets` and the offscreen
   environment. The Linux X11 test's source list (`:193-194`) keeps `proofreadingpanel.cpp`.

7. **Tests** —
   - `git mv tests/tst_proofreadingpanel.cpp tests/tst_proofreadingwindow.cpp` (or an equivalent
     rename); rename the class to `TestProofreadingWindow` and the `#include` accordingly.
     Replace `MainWindow window;` with `ProofreadingWindow window;` and the signal spies with
     `&ProofreadingWindow::checkRequested`/`checkCancelled`. Keep these slots:
     `checkButtonVisibility`, `showsIssuesAndHighlights`, `noIssuesState`, `errorStates`,
     `replacementChoicesAndApply`, `singleUndoAfterReplace`, `emptyReplacementEmptiesTextAndPanel`,
     `ignoreRemovesRowOnly`, `replaceAllAppliesAllAndSkips`, `copyCorrectedText`,
     `editingClearsAndCancels`, `correctedTextRules`. Add `disableClearsPanelAndCancels` from the last
     part of `translationAndDisableClearPanel` (show a result, `setProofreadingAvailable(false)`,
     panel hidden, list empty, highlights gone, one `checkCancelled`).
   - Delete `translationAndDisableClearPanel()` (`:646-699`, the translation halves),
     `translationDoesNotStartACheck()` (renamed by child `02`; `MainWindow::checkRequested` no longer
     exists, and the translation path moves to `tst_mainwindow`), `autoTranslateBypassesCheck()`
     (`:588-603`, moved to `tst_mainwindow`) and `autoTranslateDoesNotCancelCheck()` (`:605-644`).
     `checkButtonVisibility` keeps asserting that the Check button's status label reads
     `Nothing to check` for an empty editor and that the click emits exactly one request with the
     text.
   - New `tests/tst_mainwindow.cpp`: `translationWindowHasNoCheckingUi()` — no
     `findChild<ProofreadingPanel *>()`, no `findChild<QPushButton *>("check")`, no
     `findChild<QWidget *>("proofreadingPanel")`, while `sourceText`, `translate` and `copy` exist;
     `translatingEmitsTheRequest()` — typing text and clicking Translate emits exactly one
     `translateRequested` with that text; `autoTranslateRequestsTranslation()` — with
     `setAutoTranslateDelay(10)` typing triggers one request; `copyTranslation()` — after
     `showTranslationStarted()` and `showResult()`, Copy puts the result on the clipboard.
     `initTestCase()` registers `TranslationRequest`/`TranslationResult` and `init()` clears the
     clipboard.
   - `tests/tst_proofreadingmanager.cpp`: rename the 19 `Channel::MainWindow` occurrences to
     `Channel::Window` (`:190-510`); the `channel`-related assertions stay otherwise identical.
   - `tests/tst_proofreadingpopup.cpp` and `tests/tst_proofreading_selection_x11.cpp` are not
     touched: the popup keeps its own panel instance and its external mode.

8. **Translations** — `i18n/*.ts`: run
   `cmake --build build/linux-release --target update_translations`. Seven messages move from the
   `MainWindow` context to `ProofreadingWindow` (`Check`, `Check the source text`, `Nothing to
   check`, `Corrections applied`, `Preview of corrections`, `Corrected text:`, `Corrected text copied
   to the clipboard`), which is a net 0; four sources are new: `Text checking` (`ProofreadingWindow`
   window title), `Text to check` and `Enter text to check` (`ProofreadingWindow`) and `Check text…`
   (`TrayIcon`). Expected count **368 → 372** `<message>` entries per file with 0 unfinished, 0
   empty and 0 vanished. The moved strings need their translations re-entered in all 14 files (their
   old translations vanish with the `MainWindow` context); copy them from the previous `MainWindow`
   entries before regenerating if the `lupdate` output lost them. With `type="vanished"` entries,
   rerun with
   `lupdate -no-obsolete -project build/linux-release/.lupdate/qtranslate_lupdate_project.json`.

9. **Verification**:
   - Build `build/linux-debug` and `build/linux-release`; no `warning:` in either log
     (`-Wall -Wextra`). Use the `qtranslate-trixie` image for all task builds and tests; the lead
     runs the final Debug/Release check on the host after the stage (AGENTS.md).
   - `xvfb-run -a ctest --test-dir build/linux-debug --output-on-failure` and the same for
     `build/linux-release`: 46/46, with `tst_proofreadingwindow`, `tst_mainwindow`,
     `tst_proofreadingmanager`, `tst_proofreadingpopup`, `tst_proofreading_selection_x11` and
     `tst_translations` green.
   - `rg -n "Proofreading|proofreading|checkRequested|checkCancelled" src/ui/mainwindow.h src/ui/mainwindow.cpp`
     returns nothing; `rg -n "setProofreadingAvailable|showProofreadingResult|checkInMainWindow|m_mainWindowCheck" src`
     returns nothing; `rg -n "Channel::MainWindow" src tests` returns nothing.
   - `rg -n "ProofreadingPanel" src/ui/mainwindow.*` returns nothing while
     `src/ui/proofreadingwindow.cpp` uses it; `rg -n "proofreadingPanel" src/ui/proofreadingwindow.cpp`
     finds the object name.
   - Offscreen/manual check of the entry point: with a tray and `proofreading/enabled=false` the menu
     has no **Check text…**; after enabling, the item opens the window, Check runs a request against
     a local `QTcpServer` or the self-hosted URL, the result shows in the window, and the translation
     window stays empty of checking UI. Record that the interactive tray pass is manual; the widget
     assertions are the automated gate.
   - Record Windows Debug/Release and its 40 tests as not performed (no MSVC here); no push.

## Done when

- `MainWindow` contains no checking element: no `ProofreadingPanel`, no **Check** button, no
  `setProofreadingAvailable()`/`setProofreadingOptions()`/`showProofreadingResult()`, no
  `checkRequested`/`checkCancelled`, no highlight/selection/apply helpers, no proofreading includes
  or members; `tst_mainwindow` asserts it and the translation paths still work.
- `ProofreadingWindow` is a separate, non-modal top-level window with its own editor, the reused
  `ProofreadingPanel`, a **Check** button, the issue list, Replace/Ignore/Replace all (with the
  preview dialog)/Copy corrected text, the checking/no-issues/error states, issue highlights in its
  own editor and one-undo-step replacements; `tst_proofreadingwindow` covers the states, the actions,
  the undo step, the edit-cancels-check rule and the disable cleanup.
- The tray menu has **Check text…** after **Show window**, visible only while checking is enabled and
  a provider is registered; it opens the window and is the only entry point; there is no new CLI
  command or wire line.
- `AppController` owns both windows, routes the checking request on `Channel::Window`, hides the
  window and cancels its check when the feature is switched off, hides it on quit, and keeps its text
  and issues across a UI language change. Both windows can be open at the same time.
- The 14 `.ts` files are at **372** messages with 0 unfinished/empty/vanished; `tst_translations`
  passes.
- Linux Debug and Release build without warnings and pass 46/46 under `xvfb-run -a`; Windows
  Debug/Release and its 40 tests are recorded as not performed.
- Only `src/ui/proofreadingwindow.{h,cpp}` (new), `src/ui/mainwindow.{h,cpp}`,
  `src/ui/trayicon.{h,cpp}`, `src/app/appcontroller.{h,cpp}`,
  `src/core/proofreadingmanager.{h,cpp}`, `tests/tst_proofreadingwindow.cpp` (renamed),
  `tests/tst_mainwindow.cpp` (new), `tests/tst_proofreadingmanager.cpp`, `CMakeLists.txt`,
  `tests/CMakeLists.txt` and the 14 `i18n/*.ts` files change; no settings, hotkey, popup, settings
  dialog or documentation change; no push.

## Risks

- The tray item is the only entry point. Without a system tray the checking window cannot be opened:
  the app already warns about the missing tray and keeps the main window open, and the hotkeys and
  the popup still work. Do not add a main-window button to fix it — the user explicitly forbade
  checking UI in the translation window — and do not add a CLI command in this task.
- `updateProofreadingUi()` currently runs before the tray is created (`appcontroller.cpp:137` vs
  `:140`). Without a second call after the tray block the menu item never becomes visible; this is
  the kind of defect no test catches (`AppController` has no test target).
- The window is a real top-level widget: with a tray `quitOnLastWindowClosed` is `false`
  (`main.cpp:41`), so closing it must not quit. In the no-tray branch
  (`appcontroller.cpp:150`) the window cannot be opened at all, so the two rules do not conflict —
  check that ordering before changing `prepareForQuit()`.
- Do not reset the checking window in `applyUiLanguage()` the way `m_popup` is reset: the window
  holds user text. A language change must only retranslate labels.
- Keep the object names (`sourceText`, `check`, `status`, `proofreadingPreview*`,
  `proofreadingPanel`): the moved tests locate widgets by name, and a rename looks green only until
  `findChild` returns null.
- The seven moved strings change translation context. If they are regenerated without their old
  translations, all 14 files get unfinished entries and `tst_translations` fails; copy the
  translations over, then translate the four new ones.
- Renaming the test target and adding `tst_mainwindow` changes the documented counts: 46 Linux / 40
  Windows. `AGENTS.md` and the root plan are the lead's pass; do not edit them here.
- `tst_mainwindow` must not become a copy of the old checking tests: it exists to prove the absence
  of checking UI and to keep the translation paths covered. If a test needs a `ProofreadingPanel`,
  it belongs to `tst_proofreadingwindow`.
- `AppController` still has no test target: the tray item, the window lifetime, the disable/quit
  paths and the channel routing are covered by code review and the manual tray pass only. Say so in
  the outcome; do not add an unprepared test target.
- Windows Debug/Release and its 40 tests are unavailable here (no MSVC); record them as not
  performed, never as passed. The tray menu and the window layout were not inspected interactively
  in this environment.
- Nothing is needed from the user: the 2026-11-14 clarification (separate windows and modes, tray
  entry point) is recorded in the stage.

## Outcome

Done. Text checking is a separate non-modal window and mode; the translation window contains no
checking element.

- New `src/ui/proofreadingwindow.{h,cpp}`: a top-level `QMainWindow` with its own `sourceText`
  editor, the reused `ProofreadingPanel` (`proofreadingPanel`), the `check` button, the `status`
  label, the `Text checking` title and the moved checking code (`startCheck()`,
  `onProofreadingResult()`, `showProofreadingResult()`, `updateHighlights()`, `selectIssue()`,
  `applyReplacement()`, `ignoreIssue()`, `replaceAll()`, `previewCorrections()`,
  `applyIssueEdits()`, `copyCorrected()`, `setStatus()`, the `m_applyingEdit` guard, the five panel
  signals, the `proofreadingPreview*` dialog names and the Ctrl+Return/Ctrl+Enter shortcuts). It
  emits `checkRequested()`/`checkCancelled()` only and knows nothing about the manager, services,
  the network or the settings. A user edit and the close event clear the panel and the highlights
  and cancel the running check; closing hides the window, which stays alive and keeps its text for
  the next open; `setProofreadingAvailable(false)` does the same cleanup; `showAndActivate()` shows,
  raises, activates and focuses the editor.
- `MainWindow` lost the whole checking surface: the panel pane, the Check button,
  `setProofreadingAvailable()`/`showProofreadingResult()`, the `checkRequested`/`checkCancelled`
  signals, every checking helper and member, the proofreading includes and the
  `QTextEdit`/`QColor`/`QDialog`/`QDialogButtonBox`/`QTextCharFormat`/`<algorithm>` includes.
  Translation, swap, copy, auto-translate, the status bar and hide-to-tray are unchanged.
- The tray has `Check text…` (object name `checkText`) directly after **Show window**, hidden until
  checking is enabled and a provider is registered; it is the only entry point. No CLI command, no
  second hotkey, no new settings.
- `AppController` owns `m_proofreadingWindow`, creates it lazily in `showProofreadingWindow()`,
  connects its signals to `checkInProofreadingWindow()` on `ProofreadingManager::Channel::Window`
  (`m_windowCheck`), calls `updateProofreadingUi()` again after the tray exists, hides the window and
  cancels its check when `proofreading.enabled` goes off, hides it on quit, and does not reset it on
  a UI language change. `updateProofreadingUi()` no longer touches `m_mainWindow`.
- `ProofreadingManager::Channel::MainWindow` is `Channel::Window`; `channelName()` logs `window`.
- Tests: `tst_proofreadingpanel` is now `tst_proofreadingwindow` and covers the window states, the
  actions, the single undo step, the edit-cancels-check rule, the close/disable cleanup and the
  corrected-text rules; the new `tst_mainwindow` proves the translation window has no checking UI
  and keeps translate/auto-translate/copy; `tst_proofreadingmanager` uses the new channel name.
  Linux is 46 tests; Windows 40 is derived and not performed.
- Translations: the 14 `.ts` files are at **372** messages with 0 unfinished, empty or vanished; the
  seven moved `MainWindow` strings keep their translations in the `ProofreadingWindow` context and
  the four new strings (`Text checking`, `Text to check`, `Enter text to check`, `Check text…`) are
  translated in all 14 files.
- Verification in the `qtranslate-trixie` container: Linux Debug and Release build with zero
  `warning:` (`-Wall -Wextra`) and pass **46/46** under `xvfb-run -a`, including
  `tst_proofreadingwindow`, `tst_mainwindow`, `tst_proofreadingmanager`, `tst_proofreadingpopup`,
  `tst_proofreading_selection_x11` and `tst_translations`. The static gates are clean (`MainWindow`
  has no proofreading name; `checkInMainWindow`/`m_mainWindowCheck` and
  `ProofreadingManager::Channel::MainWindow` are gone; `ProofreadingPanel` is not referenced from
  `mainwindow.*`). `git diff --check` is clean. `ProofreadingPanel` and `ProofreadingPopupWindow`
  are byte-identical.
- Not performed: Windows Debug/Release and its 40 tests (no MSVC); the interactive tray pass and a
  live provider smoke (no tray or display here) — the widget assertions are the automated gate. The
  `AppController` window lifetime, tray visibility and disable/quit paths remain code-review only
  because it has no test target.
- Plan deviations and review notes: the plan's step-9 grep lines were inaccurate — the new window
  necessarily has `setProofreadingAvailable()`/`showProofreadingResult()`, and
  `TranslationManager::Channel::MainWindow` is a different enum; the gates used were the
  proofreading-qualified ones. `ProofreadingWindow::startCheck()` has an availability guard (no
  reachable path when the feature is off because the tray item and the window are hidden);
  `setStatus()` dropped the unused `isError` parameter. The manager's private `m_mainWindowReply`
  name was kept; only the enum and the log name changed. The review found only unused includes
  (`QSignalBlocker` in `proofreadingwindow.cpp`, `QTextDocument` in `mainwindow.cpp`); rejected as
  cosmetic, with no failure scenario.
- Only the task files, the 14 `.ts` files, this plan and the lead's journal change are touched; no
  push.
