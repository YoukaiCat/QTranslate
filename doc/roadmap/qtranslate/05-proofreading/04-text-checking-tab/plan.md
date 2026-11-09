---
kind: task
status: in-progress
detailed: true
depends: [03-languagetool-service]
---

# Text checking settings tab

## Task

Add the separate **Text checking** tab to `SettingsDialog` and make the `proofreading/*` settings
group editable, without touching the Translation tab or any translation setting. The tab shows the
mode switch, the provider, the checking language, the LanguageTool endpoint, the four categories,
"Check before translation", "Apply corrections automatically" and the online-services switch with the
privacy warning. There is no key, username or password row: stage 5 sends no credentials (user
decision of 2026-11-09). `doc/proofreading.md` §5/§6 and `doc/architecture.md` §4.13/§4.15 define the
fields; `01-proofreading-core` stores and normalises them, so this child is the UI plus the
translations.

The page is built like the Services page (`src/ui/settingsdialog.cpp:181-513`), keeps editing a copy
of `AppSettings`, and is inserted as the fifth tab, after **Services** and before **Network**
(`addTab` calls at `settingsdialog.cpp:176-178`, `:458`, `:513`). The dialog receives the registered
providers from `AppController` so a provider id that no service implements cannot be selected:
LanguageTool is the only entry in this stage, and the row is kept for stage 6. A stored id that is
not registered keeps the saved value and shows the first registered provider. The language combo is
filled from the selected provider's `languages()` plus "Auto-detect" only when
`supportsAutoLanguage()` is true (LanguageTool returns true, so Auto-detect is available), and is
rebuilt when the provider changes.

The mode switch (`proofreading/enabled`, off by default) disables the provider/options rows while it
is off, except the switch itself, the Services-tab gate pattern (`settingsdialog.cpp:724-756`). The
online switch (`proofreading/allowOnlineServices`, off by default) is **not** a disabling gate: the
settings stay editable with the switch off, and a warning label explains that no check runs until it
is enabled (a self-hosted server also needs it). The endpoint hint says that an empty field uses the
public LanguageTool endpoint, that the public service sees the checked text and has rate limits, and
that a self-hosted URL keeps the text on the user's machine. Endpoint validation uses
`isValidProofreadingEndpoint()`; an invalid non-empty value shows a fixed warning and `validate()`
blocks OK with one message (the same text), so an invalid URL is never saved as typed.

## Steps

1. **Add the provider description and the new page to `src/ui/settingsdialog.{h,cpp}`.**
   - `settingsdialog.h`: add a public struct
     ```cpp
     struct ProofreadingProvider
     {
         QString id;
         QString name;
         QStringList languages;      // internal codes, without "auto"
         bool supportsAutoLanguage = false;
     };
     ```
     and a constructor parameter
     `const QList<ProofreadingProvider> &proofreadingProviders = {}` **after**
     `bool secretStorageFallback` and **before** `QWidget *parent`; store it in
     `QList<ProofreadingProvider> m_proofreadingProviders`. Defaulted, so the existing four-argument
     test calls keep compiling; update the single `AppController` call site (step 6).
   - Add the widget members: `QCheckBox *m_proofreadingEnabled`, `QComboBox *m_proofreadingProvider`,
     `LanguageComboBox *m_proofreadingLanguage`, `QLineEdit *m_proofreadingEndpoint`,
     `QLabel *m_proofreadingEndpointHint`, `QLabel *m_proofreadingEndpointWarning`,
     `QCheckBox *m_proofreadingSpelling`, `...Grammar`, `...Punctuation`, `...Style`,
     `QCheckBox *m_proofreadingCheckBeforeTranslation`, `QCheckBox *m_proofreadingAutoReplace`,
     `QCheckBox *m_proofreadingAllowOnline`, `QLabel *m_proofreadingOnlineWarning`, and the group
     pointers needed by the enable/disable logic.
   - Add `void updateProofreadingControls();` and `void updateProofreadingLanguage();` next to the
     other `update*Controls()` declarations (`settingsdialog.h:72-80`).

2. **Build the tab in `buildUi()` after the Services page (`settingsdialog.cpp:181-513`).**
   - A `QWidget` page with a `QVBoxLayout`; top: `m_proofreadingEnabled` with text
     "Enable text checking", objectName `proofreadingEnabled`, plus one word-wrapped hint that the
     mode is separate from translation.
   - `QGroupBox` "Provider" with a `QFormLayout`:
     * `m_proofreadingProvider` (objectName `proofreadingProvider`): one item per
       `m_proofreadingProviders` entry, data = id, text = name (the product name `LanguageTool` is a
       literal, not `tr()`); if the list is empty, disable the combo and show no items.
     * `m_proofreadingLanguage` (`LanguageComboBox`, objectName `proofreadingLanguage`).
     * `m_proofreadingEndpoint` (`QLineEdit`, objectName `proofreadingEndpoint`) with the public URL
       as placeholder; `m_proofreadingEndpointHint` (word-wrapped) below it.
     * `m_proofreadingEndpointWarning` (objectName `proofreadingEndpointWarning`, word-wrapped,
       red) hidden until the typed value is invalid.
   - `QGroupBox` "Issues" with a `QHBoxLayout` of the four checkboxes, objectNames
     `proofreadingSpelling`, `proofreadingGrammar`, `proofreadingPunctuation`,
     `proofreadingStyle`, each checked by default.
   - `QGroupBox` "Options" with `m_proofreadingCheckBeforeTranslation` and
     `m_proofreadingAutoReplace` (both off by default) and one word-wrapped warning under the
     automatic replacement ("Corrections are applied without asking. Every change stays one undo
     step.").
   - `QGroupBox` "Online checking" with `m_proofreadingAllowOnline` and
     `m_proofreadingOnlineWarning` (word-wrapped). No key, username or password row.
   - Insert the page with `m_tabs->addTab(textCheckingPage, tr("Text checking"));` after the
     Services tab.
   - Connect `m_proofreadingEnabled` and `m_proofreadingAllowOnline` to
     `updateProofreadingControls()`, `m_proofreadingProvider::currentIndexChanged` to
     `updateProofreadingLanguage()` and `updateProofreadingControls()`, and
     `m_proofreadingEndpoint::textChanged` to `updateProofreadingControls()`.

3. **Implement the two update methods.**
   - `updateProofreadingLanguage()`: clear the combo; fill it from the current provider's
     `languages` (`LanguageComboBox::setLanguages(codes, includeAuto)` with
     `includeAuto = provider.supportsAutoLanguage`); keep the previous code when it is still
     available, otherwise select Auto-detect if included, else the first entry; block signals while
     refilling so `settings()` is not disturbed.
   - `updateProofreadingControls()`: `const bool enabled = m_proofreadingEnabled->isChecked();`
     enable the provider/language/endpoint/categories/options groups and the online group by
     `enabled` (the mode switch stays enabled); keep the online warning visible (or show it when the
     switch is off) independently of `enabled`; recompute
     `m_proofreadingEndpointWarning->setVisible(!text.trimmed().isEmpty() &&
     !isValidProofreadingEndpoint(text))`; enable the endpoint row only when the selected provider
     exists (`!m_proofreadingProviders.isEmpty()`).

4. **Extend `loadValues()` and `settings()` (`settingsdialog.cpp:537-666`).**
   - Load: `m_proofreadingEnabled->setChecked(settings.proofreading.enabled)`; select the provider
     by `settings.proofreading.provider` (find by data; fall back to index 0);
     `updateProofreadingLanguage()`; then `m_proofreadingLanguage->setCurrentCode(...)` (fall back
     to `Languages::autoCode()` when included, else index 0); `m_proofreadingEndpoint->setText(settings.proofreading.endpoint)`
     (the normalised full URL or empty); check the four category boxes from
     `settings.proofreading.categories` (a missing entry means unchecked, but the settings layer
     always stores all four); set the three toggles; finish with `updateProofreadingControls()`.
   - Save into the copy: `result.proofreading.enabled`, `.provider =
     m_proofreadingProvider->currentData().toString()`, `.language =
     m_proofreadingLanguage->currentCode()`, `.endpoint =
     normalizedProofreadingEndpoint(m_proofreadingEndpoint->text())`, `.categories` collected in
     `proofreadingCategoryIds()` order from the checked boxes, `.checkBeforeTranslation`,
     `.autoReplace`, `.allowOnlineServices`. Do not read or write any `translation.*` field for this
     page; do not touch `[proofreading]` keys that are not in the struct.

5. **Validate the endpoint in `validate()` (`settingsdialog.cpp:770`).**
   - After the proxy check, when `m_proofreadingEnabled->isChecked()` and the trimmed endpoint is
     non-empty and `!isValidProofreadingEndpoint(...)`, set
     `*error = tr("Enter a valid http(s) LanguageTool URL, or leave the field empty for the public endpoint.")`
     and return false. An empty endpoint is valid (the public default).
   - No other new validation: categories may be all unchecked (the settings layer normalises an
     empty list to all four, so the page cannot disable checking by accident; do not add a
     warning for it in this child).

6. **Pass the providers from `AppController` (`src/app/appcontroller.cpp:739-745`).**
   - In `showSettings()`, build
     `QList<SettingsDialog::ProofreadingProvider> proofreadingProviders;` from
     `m_proofreading->services()`: `{service->id(), service->displayName(), service->languages(),
     service->supportsAutoLanguage()}` (guard `m_proofreading` when it is null, e.g. before
     startup). Pass it as the new constructor argument before `m_mainWindow.get()`.
   - Do not change the translation `sourceLanguages`/`targetLanguages` arguments or the dialog's
     other signals.

7. **Add the strings to the 14 `.ts` files.**
   - The 18 new `tr()` strings (baseline 316 messages per `.ts`, `grep -c '<message'`):
     1 `Text checking` (tab), 2 `Enable text checking`, 3 `Finds spelling, grammar, and punctuation
     errors in a separate mode and does not change translation settings.`, 4 `Provider:`,
     5 `Checking language:`, 6 `LanguageTool URL:`, 7 `Leave empty to use the public LanguageTool
     endpoint. A self-hosted server keeps the text on your machine; the public service has rate
     limits and sees the checked text.`, 8 `Enter a valid http(s) LanguageTool URL, or leave the
     field empty for the public endpoint.` (hint and validation share it), 9 `Categories:`,
     10 `Spelling`, 11 `Grammar`, 12 `Punctuation`, 13 `Style`, 14 `Check before translation`,
     15 `Apply corrections automatically`, 16 `Corrections are applied without asking. Every change
     stays one undo step.`, 17 `Allow online text checking`, 18 `The checked text is sent to the
     provider. Text checking works only while this is on; a self-hosted server still needs it.`
     Expected total: **334** messages per `.ts` (316 + 18).
   - Run `update_translations` from a Linux build (`cmake --build build/linux-release --
     t update_translations`, per `i18n/README.md`); it keeps the Windows-only `HotkeyManager`
     message because `src/platform/hotkeymanager_win.cpp` is already an extra lupdate source
     (`CMakeLists.txt:179-186`). Fill the 18 entries in all 14 files (the project's machine-assisted
     first pass), keep `%1`/`%2` and punctuation, leave `LanguageTool` untranslated, then run
     `lrelease`/build. `tst_translations` must stay green with 0 unfinished, 0 empty and 0 vanished
     messages; `grep -c '<message' i18n/qtranslate_ru_RU.ts` must report 334.

8. **Extend `tests/tst_settingsdialog.cpp` and verify.**
   - The file currently has 26 named `void name()` cases (measured); add two:
     `proofreadingTab()` and `proofreadingGateAndEndpoint()` (28 named slots). Use the
     `findChild<QWidget *>(objectName)` pattern of the existing cases.
   - `proofreadingTab()`: build a dialog with a two-entry provider list (LanguageTool with
     `{"en","ru","zh-Hans"}` and `supportsAutoLanguage = true`, plus a second fake entry with no Auto
     and one language, so the rebuild is observable), `proofreading.enabled = true`, a custom
     endpoint, a subset of categories and the toggles on; assert the tab count/order (Text checking
     is fifth, between Services and Network), the enabled state, the provider items, the language
     list starts with Auto-detect and contains the provider's codes, the endpoint text and hint, the
     four category boxes, and that the three toggles and the options page round-trip through
     `dialog.settings().proofreading`; switch the provider and assert the language list changes.
   - `proofreadingGateAndEndpoint()`: with the default settings assert the mode is off, the mode
     switch disables the provider/options groups, the online switch is off and its warning is
     visible, the provider/endpoint rows are still enabled (the gate does not lock the settings); type
     an invalid endpoint and assert the warning is visible; `dialog.settings().proofreading.endpoint`
     is empty after the dialog copy is read with an invalid value (normalisation), while
     `validate(&error)` returns false with the fixed message; clear the field and assert validate
     passes; assert no `translation.*` value changes when only proofreading widgets are edited.
   - Run `ctest -R tst_settingsdialog` and record the exact pass count in the outcome (the named
     count is 28; data rows make the run count higher).
   - Debug and Release build on Linux with `-Wall -Wextra` and zero warnings; the full suite passes
     42/42 in both; `ctest -N` stays 42 Linux and would stay 37 on Windows (no new target); Windows
     is recorded as not performed (no MSVC). No service, manager, main-window, documentation,
     README or `doc/handoff.md` change; no commit.

## Done when

- `SettingsDialog` has a fifth, separate **Text checking** page between **Services** and **Network**
  with `enabled`, `provider`, `language`, `endpoint`, the four category checkboxes,
  `checkBeforeTranslation`, `autoReplace` and `allowOnlineServices`; there is no key, username or
  password row. The tab title and every label are `tr()` strings, and the page does not read or
  write any `translation.*` key.
- The provider combo lists the providers passed from `AppController` (`LanguageTool` in this stage;
  the product name stays untranslated); an empty provider list disables the combo; a stored id that
  is not registered shows the first registered provider. The language combo follows the selected
  provider: Auto-detect plus the provider's 30 LanguageTool codes when `supportsAutoLanguage()` is
  true, and is rebuilt on a provider change.
- The endpoint row accepts an empty value (public default) or a valid absolute `http(s)` URL; the
  hint explains the public/self-hosted choice, the third-party exposure and the rate limits; an
  invalid value shows the fixed warning, `settings()` normalises it to empty and `validate()` blocks
  OK with the same message.
- The four categories default to all checked, are saved in `proofreadingCategoryIds()` order, and an
  empty selection is normalised by the settings layer (no extra UI rule).
- The mode switch is off by default and disables the provider/options rows while off, except the
  switch itself. The online switch is off by default and does **not** disable the settings; its
  warning states that no check runs until it is on and that a self-hosted server also needs it.
- `loadValues()`/`settings()` round-trip the whole `[proofreading]` group (`enabled`, `provider`,
  `language`, `endpoint`, `categories`, `checkBeforeTranslation`, `autoReplace`,
  `allowOnlineServices`) and leave every other group untouched; the dialog copy keeps the values,
  and the `AppController` call site passes the provider list.
- `tests/tst_settingsdialog.cpp` has the two new cases and they pass offscreen; the existing cases
  stay green; the tab order, defaults, round trip, provider/language rebuild, gate behaviour,
  endpoint warning/validation and translation independence are covered. The outcome records the
  exact run count for the target.
- The 18 new strings are present in all 14 `.ts` files, none is unfinished, empty or vanished,
  `tst_translations` is green, and `grep -c '<message' i18n/qtranslate_ru_RU.ts` is 334 (the outcome
  records the exact count). `LanguageTool`, `Auto-detect` (existing) and the URL placeholder are not
  new translated strings.
- Debug and Release build on Linux without warnings and the full suite passes 42/42; `ctest -N`
  shows 42 Linux tests and would show 37 on Windows; Windows is recorded as not performed. No
  service, manager, main-window, documentation, README or `doc/handoff.md` change; no commit.

## Depends on

`03-languagetool-service` for a registered provider and its language list, and the settings group and
helpers (`proofreadingCategoryIds()`, `proofreadingProviderIds()`, `isValidProofreadingEndpoint()`,
`normalizedProofreadingEndpoint()`) from `01-proofreading-core`.

## Risks

- **The `.ts` workflow is the easiest place to lose a message.** `update_translations` must run on
  Linux with the current CMake configuration, which already adds
  `src/platform/hotkeymanager_win.cpp` as an extra lupdate source (`CMakeLists.txt:179-186`); if
  that message vanishes, the Windows-only `HotkeyManager` text disappears from all 14 files and
  `tst_translations` catches it. Do not hand-edit `.ts` XML beyond the new entries, and re-run
  `lrelease`/the build so the embedded `.qm` matches.
- **`normalizedProofreadingEndpoint()` is not a validator.** It may return the trimmed value; the
  page must call `isValidProofreadingEndpoint()` for the warning and `validate()` for the block, and
  `settings()` must never store a typed invalid URL. An empty value is valid and means the public
  default.
- **The provider list is passed in, not built in the dialog.** `AppController` must build it from
  `m_proofreading->services()` and guard a null manager; the dialog must not fall back to a
  hard-coded provider id, or stage 6 would show an id that no service implements.
- **The online gate is not a disable switch.** The user's rule is that the settings stay
  configurable while online checking is off; only the mode switch (`enabled`) disables rows. Do not
  copy the free-web `setEnabled(gate)` pattern for `allowOnlineServices`, or the user cannot prepare
  the settings before enabling online checking.
- **Auto-detect must come from the provider capability.** LanguageTool returns
  `supportsAutoLanguage() == true`, so the combo includes it; a future provider without it must not
  show Auto. Build the list from the passed capability, not from a constant.
- **The constructor signature change touches the one `AppController` call site and every test
  call.** Keep the new parameter defaulted and before `QWidget *parent`; update only
  `appcontroller.cpp:739-745` and add the provider list, leaving the translation arguments as they
  are.
- **A one-entry provider combo is intentional.** LanguageTool is the only provider in stage 5; the
  two-entry list exists only in the test to prove the language rebuild. Do not add a second real
  provider or a dead id.
- **Windows is not built here.** Record the count and the build as not performed, never as passed.
