---
kind: task
status: todo
depends: [01-duckduckgo-service]
---

# DuckDuckGo settings group and translations

## Task

Add the `DuckDuckGo` group to the Services page, translate its hint in all 14 `.ts` files and cover the
group with a `tst_settingsdialog` case. No service, parser or language behaviour changes; those are
`01-duckduckgo-service`. DuckDuckGo has no key and no user-editable endpoint, so the group has no input
field unless the research found a value the user must set — in that case this task also owns the INI
key, its normalization and the `tst_settings` rows.

The group follows `Reverso`: a `QGroupBox` with objectName `duckduckgoGroup` appended at the end of the
Services page, after the `Reverso` group and after the `LibreTranslate` group when that provider is
added first, so the page follows the provider order. It holds one word-wrapped `QLabel` with objectName
`duckduckgoHint` that says the endpoint is unofficial and needs no key, names the observed language
coverage and whether the source language can be detected, and says that long text is sent in several
requests and that the endpoint may stop working, rate-limit or block requests. The product name is not
translated. Reuse an existing source string when the meaning is identical, so translators do not see a
near duplicate.

Strings are added through the `update_translations` CMake target (`lupdate`) and translated by hand in
`i18n/qtranslate_*.ts` (14 files); `lrelease` must report no unfinished or vanished message for the new
strings. `AGENTS.md`, `doc/` and both READMEs are the lead's pass; this task does not edit them.

## Done when

- The Services page shows the `DuckDuckGo` group in the provider order, with the hint visible and no
  input field (or with the field the research required), and `tst_settingsdialog` finds the group and
  the hint and checks its text is not empty and mentions the unofficial status.
- The new strings exist in all 14 `.ts` files, are translated, and introduce no unfinished or vanished
  entry.
- No service, parser, language or settings-storage file changes when the group needs no value; Debug and
  Release build without warnings on Linux and the full suite passes.
