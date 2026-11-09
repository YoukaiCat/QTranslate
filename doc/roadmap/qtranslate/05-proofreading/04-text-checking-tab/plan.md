---
kind: task
status: todo
depends: [03-languagetool-service]
---

# Text checking settings tab

## Task

Add the separate **Text checking** tab to `SettingsDialog` and make the `proofreading/*` settings
group editable, without touching the Translation tab or any translation setting. The tab shows the
mode switch, the provider, the checking language, the endpoint and key of a self-hosted or premium
LanguageTool, the four categories, "Check before translation", "Apply corrections automatically" and
the online-services switch with the privacy warning. `doc/proofreading.md` §5 and
`doc/architecture.md` §4.13/§4.15 define the fields; `01-proofreading-core` already stores and
normalises them, so this child is the UI plus the translations.

The page is built like the Services page (a `QScrollArea` with group boxes,
`src/ui/settingsdialog.cpp:183-200`), and the dialog keeps editing a copy of `AppSettings`. The
provider combo lists the registered proofreading providers (passed in from `AppController`, the same
way the translation services are passed to `MainWindow::setServices()`), so a provider id that no
service implements cannot be selected; LanguageTool is the only entry in this stage, and the row is
kept for stage 6. A stored id that is not registered keeps the saved value and shows the first
registered provider. The language combo is filled from the selected provider's `languages()` plus
"Auto-detect" only when `supportsAutoLanguage()` is true, and is rebuilt when the provider changes.
Endpoint and key are enabled only for a provider that is not local; the two toggles stay independent
of the translation tab. The online switch is followed by one warning label that says the checked text
is sent to the provider and that checking works only while the switch is on, and the whole page is
disabled while `proofreading/enabled` is off, except the mode switch itself (the Services tab's gate
pattern, `settingsdialog.cpp:303-340`, is the model).

Validation uses the helpers from `01-proofreading-core` (`isValidProofreadingEndpoint()`,
`proofreadingCategoryIds()`, `proofreadingProviderIds()`), not a copy of the rules; an invalid
endpoint shows the established fixed warning and is not saved as typed.

## Done when

- `SettingsDialog` has a fifth, separate **Text checking** page with `enabled`, `provider`,
  `language`, `endpoint`, `apiKey` (password echo), the four category checkboxes,
  `checkBeforeTranslation`, `autoReplace` and `allowOnlineServices`; the tab title and every label are
  `tr()` strings, the page is independent of the Translation page, and no translation setting reads
  or writes a `proofreading/*` key.
- The provider combo lists the registered proofreading providers (`LanguageTool` in this stage); the
  language combo follows the provider (`auto` only when supported), and the category/endpoint/key
  rows are enabled only in a state where they have an effect. The online switch carries the privacy
  warning, and the whole page is disabled while the mode is off.
- `loadValues()`/`values()` round-trip the group: defaults, an existing INI file, an invalid endpoint
  and an unknown stored provider behave as `01-proofreading-core` defines, and an invalid endpoint is
  not saved as typed.
- `tests/tst_settingsdialog.cpp` gains a case for the new page (offscreen): defaults, the field
  values round-tripped through the dialog copy, the provider→language rebuild (two provider entries
  passed to the dialog, so the rebuild is observable although only LanguageTool ships), the gate
  disabling, the warning text and the independence from the translation page; the existing cases
  stay green.
- All new strings are added to the 14 `.ts` files with `update_translations`/`lupdate`, none is
  unfinished or vanished, and `tst_translations` is green; the baseline is 316 messages per `.ts`
  (`grep -c '<message' i18n/qtranslate_ru_RU.ts`), which the outcome records with the new total.
- Debug and Release build on Linux without warnings and the full suite passes (the test count stays
  42 Linux / 37 Windows because no new target is added); Windows is recorded as not performed. No
  service, manager, main-window, documentation, README or `doc/handoff.md` change.

## Depends on

`03-languagetool-service` for a registered provider and its language list, and the settings group and
helpers from `01-proofreading-core`.
