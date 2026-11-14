---
kind: stage
status: done
base: 3e75331
---

# Text checking as a standalone feature

## Goal

Make text checking a standalone feature on par with translation, in its own window and its own mode,
per the user's 2026-11-14 correction and clarification. Translation and checking are **not** one
window and not one workflow:

1. The **translation window** translates only. It contains no checking element at all: no issue
   panel, no **Check** button, no pre-translation check, no automatic replacement, none of the
   checking signals or helpers.
2. The **checking window** is a separate, non-modal top-level window with its own source editor, a
   **Check** button, the issue list with Replace/Ignore/Replace all/Copy corrected text, the
   checking/no-issues/provider-error states and undo of its own edits. It is opened from the tray
   menu item **Check text…**, shares no widgets or text with the translation window, and both
   windows may be open at the same time.
3. Two global hotkeys act on a selection made in another application: one shows the result in the
   popup (the existing `ProofreadSelection` action), the other checks and immediately replaces the
   selection in the source application (new action).

The three settings the user rejected are removed: **Check before translation**, **Apply corrections
automatically** (the new hotkey does that) and **Allow online text checking** (the **Enable text
checking** switch is enough; there is no second privacy gate).

`proofreading/enabled` stays the only switch and keeps its `false` default: it is the consent for
sending text to the provider and the only control of the feature. While it is off, the tray offers
no **Check text…** item, neither hotkey checks anything, the checking window is not shown and no
request is made.

## What to do, what is touched

- `src/core/settings.h`/`.cpp`: remove `checkBeforeTranslation`, `autoReplace` and
  `allowOnlineServices` from `ProofreadingSettings`, the INI load/save and their comments; keep
  `enabled`, `provider`, `language`, `endpoint`, `categories` and the first hotkey; add the second
  hotkey key; `settingsVersion` stays `1`, old keys are ignored. Update the constants in
  `tst_settings`.
- `src/core/proofreadingmanager.*`: remove the online gate and `isLocal()` if it becomes unused;
  rename the `MainWindow` request channel to `Window`, because the request now serves the checking
  window, not the translation window. Any configured provider may check when the feature is enabled.
- `src/core/proofreadingservice.h`, `src/services/languagetoolservice.*`: remove `isLocal()` if the
  manager no longer uses it; keep the provider interface otherwise.
- `src/ui/settingsdialog.*`: the **Text checking** tab keeps only the enable switch, provider,
  language, URL and categories; remove the **Options** group (both checkboxes and the warning) and
  the **Online checking** group (checkbox and warning). The **Hotkeys** tab gets the second
  proofreading row; the existing row becomes the popup check, the new row is check-and-replace.
- `src/ui/mainwindow.*`: remove the whole checking surface — the `ProofreadingPanel` splitter pane,
  the **Check** button, `setProofreadingAvailable()`, `setProofreadingOptions()`,
  `showProofreadingResult()`, `onProofreadingResult()`, `startCheck()`, the highlight/selection
  helpers, `applyReplacement()`, `ignoreIssue()`, `replaceAll()`, `previewCorrections()`,
  `applyIssueEdits()`, `copyCorrected()`, the `checkRequested`/`checkCancelled` signals, the
  `m_applyingEdit`/`m_checkBeforeTranslation`/`m_autoReplace`/`m_pendingTranslationCheck` members and
  the `proofreadingtypes.h`/`proofreadingtext.h`/`proofreadingpanel.h` includes. The window keeps
  languages, services, translate, copy, swap, auto-translate, status bar and its own result handling.
- `src/ui/proofreadingwindow.{h,cpp}` (new): a top-level `QMainWindow` for the checking mode, with
  its own source editor, the reused `ProofreadingPanel`, a **Check** button, the states of the
  panel, one-undo-step replacements, Replace/Ignore/Replace all (with the preview dialog)/Copy
  corrected text, highlight of the issues in its own editor and stale-result cancellation on edits.
  It knows nothing about services, the network or the settings: it emits `checkRequested()` and
  `checkCancelled()` and receives results through `showProofreadingResult()`.
- `src/ui/trayicon.*`: a **Check text…** menu item (object name `checkText`), visible only while
  checking is enabled and a provider is registered, emitting `checkTextRequested()`, and its
  `retranslate()` entry. It is the only entry point of the checking window; there is no new CLI
  command.
- `src/app/appcontroller.*`: own both windows; show the checking window from the tray item; route
  its `checkRequested` to the manager on the `Window` channel and the result back to it; hide it and
  cancel its check when `proofreading.enabled` goes off and on quit; update its availability with
  `updateProofreadingUi()`; keep its text and issues on a UI language change (unlike the translation
  popup, which is reset). Stop syncing the online gate; gate the popup and check-and-replace entry
  points (hotkeys and `--proofread-selection`, which shares their entry point) on
  `proofreading.enabled`.
- Hotkeys: add a `HotkeyAction` for check-and-replace, a settings key, registration, duplicate/
  multi-combination validation, the Windows, X11 and Wayland tables and the hotkey tests. The
  existing popup-check action keeps its behaviour and empty-default combination; the new action is
  empty by default too. Its flow reuses the existing selection grab, target-window capture, focus
  watch, clipboard/paste safety and cancellation rules of `proofreadSelectionInPlace()`/
  `translateAndReplace()`, but shows no confirmation popup: on success it pastes the corrected text
  immediately; with no issues it shows a message and pastes nothing; if the target changed or no
  window handle exists, it falls back to the clipboard with the same messages as the existing flows.
- Tests: `tst_settings` (defaults, round trip, removed keys ignored, new hotkey formatting),
  `tst_settingsdialog` (no Options/Online groups, two hotkey rows, duplicate validation),
  `tst_proofreadingmanager` (no gate, `Window` channel, no `isLocal`), a new
  `tst_proofreadingwindow` (the former `tst_proofreadingpanel`, moved to the checking window: states,
  issue list, Replace/Replace all/preview/Ignore/Copy, one undo step, edits cancel the check, disable
  clears and cancels), a new `tst_mainwindow` (translation only: no checking widget or button,
  translate/auto-translate/copy work), `tst_proofreadingpopup` and `tst_proofreading_selection_x11`
  (unchanged), `tst_hotkeymanager_*` (new action). Adding `tst_mainwindow` takes the suite to 46
  Linux / 40 Windows tests. The 14 `.ts` files lose the three settings labels and their warnings,
  move the checking-window strings to a new context and gain the checking-window title, the tray
  item, the editor labels and the new hotkey label; every child states its expected count.
- Documentation (`doc/proofreading.md`, `doc/architecture.md`, `doc/handoff.md`, both READMEs,
  `AGENTS.md`, the root plan, including the new test counts and the separate window) is the lead's
  pass after the code tasks.

## Done when

- `proofreading/checkBeforeTranslation`, `proofreading/autoReplace` and
  `proofreading/allowOnlineServices` no longer exist in `AppSettings`, the INI load/save, the
  manager, the settings dialog or the main window; an old INI with them loads without the keys and
  without a warning; `settingsVersion` stays `1`. `proofreading/enabled` is the only switch.
- The **Text checking** tab has the enable switch, provider, language, URL and categories only; no
  **Options** or **Online checking** group and no "Enable online text checking" error/message.
- The translation window contains no checking element: no `ProofreadingPanel`, no **Check** button,
  no `checkRequested`/`checkCancelled` signals, no pre-translation confirmation and no automatic
  replacement; `tst_mainwindow` asserts the absence and that translating still works.
- A separate checking window exists: its own source editor, **Check** button, issue list with
  Replace/Ignore/Replace all/Copy corrected text, the checking/no-issues/error states, highlights
  and one-undo-step edits in its own editor. It is not modal, does not share widgets or text with the
  translation window, and both windows can be open at once. It is opened by the tray item
  **Check text…**, which is absent while checking is disabled.
- Two proofreading hotkeys exist and are empty by default: one checks the selection and shows the
  popup result; the other checks the selection and immediately replaces it in the source field with
  the corrected text. Neither acts while `proofreading/enabled` is off. The replace flow uses the
  same foreground/clipboard safety as translate-and-replace, pastes nothing when there are no
  issues, and shows a clear message.
- Linux Debug and Release build without warnings and pass 46/46 (`-Wall -Wextra`); the 14 `.ts`
  files are regenerated with no unfinished/empty/vanished messages and end at **374** messages
  (379 − 5 net: −3 for the gate, −8 for the pre-translation settings, +4 for the checking window and
  the tray item, +2 for the new hotkey; each child states its own count); Windows Debug/Release and
  its 40 tests are recorded as not performed (no MSVC here).
- `doc/proofreading.md`, `doc/architecture.md`, `doc/handoff.md`, both READMEs, `AGENTS.md` and the
  root plan describe the standalone feature, the separate checking window and the two hotkeys, the
  removed settings and the new test counts; the text-checking behaviour is no longer described as a
  pre-translation step, as part of the translation window, or as an online gate.

## Outcome

Done and accepted over `3e75331..31a2a36` (four task commits: `11eb5bc`, `d9428c9`, `25f0f5f`,
`31a2a36`); the lead's documentation pass is in the stage-closing commit.

- **Standalone window and mode.** Text checking no longer shares the translation window. The new
  non-modal `ProofreadingWindow` has its own editor, **Check** (Ctrl+Return/Ctrl+Enter),
  `ProofreadingPanel`, Replace/Ignore/Replace all with preview, Copy corrected text, highlighting,
  one undo step and the checking/no-issues/error states. It is opened from the tray item **Check
  text…**, which is hidden while checking is disabled or no provider is registered. The translation
  window contains no checking widget, button, signal or helper and remains translation-only; both
  windows can be open at once.
- **Removed settings and flows.** `proofreading/allowOnlineServices`, `proofreading/checkBeforeTranslation`
  and `proofreading/autoReplace` and `ProofreadingService::isLocal()` are gone; `proofreading/enabled`
  (default `false`) is the only consent. The **Options** and **Online checking** groups, the
  pre-translation confirmation, the automatic replacement and the online-gate error are removed;
  old INI keys are ignored and `settingsVersion` stays `1`.
- **Two hotkeys.** `ProofreadSelection` (empty by default) checks the external selection and shows
  the popup with Replace selection/Copy/Cancel; `ProofreadAndReplace` (empty by default,
  `proofreading/replaceHotkey`) checks and immediately replaces the selection through the shared
  foreground/clipboard safety, with no popup; no issues shows a message and pastes nothing. Both are
  gated on `proofreading/enabled` before the grab, and stale replies never paste.
- **Tests and translations.** `tst_settings`, `tst_settingsdialog`, `tst_proofreadingmanager`,
  `tst_proofreadingpopup`, `tst_hotkeymanager_x11`/`wayland`, `tst_translations`; the old
  `tst_proofreadingpanel` became `tst_proofreadingwindow` (13 slots) and a new `tst_mainwindow`
  (4 slots) proves the translation window has no checking UI. Linux Debug/Release 46/46 with no
  warnings in `qtranslate-trixie`; 14 `.ts` at 374 with 0 unfinished/empty/vanished; Windows 40 not
  performed (no MSVC).
- **Real problems left unfixed.** `AppController` has no test target, so the tray item, lazy window
  creation, disable/quit cleanup and the replace-paste branch are covered by code review and widget
  tests only. The enable gate is checked before the grab, so disabling checking while a grab is
  already running can still send one request. The shared `pasteText()` failure message still says
  "translation" on the check-and-replace path. Three new `.ts` messages are machine-translated and
  need native review.
- **Final host check.** Fresh Debug and Release trees under
  `/tmp/opencode/qtranslate-final-debug` and `...-release`: 46/46 each with no warnings. The old
  `build/linux-debug`/`build/linux-release` trees are stale and fail with a moc mismatch, so they
  were not used.
- **Not performed.** Windows Debug/Release and its 40 tests; a real tray/window manager, the live
  LanguageTool provider and both non-modal windows interactively.
