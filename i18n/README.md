# UI translations

English is the source language. UI strings are marked with `tr()` or `QT_TRANSLATE_NOOP`;
translations live in `*.ts` next to this file and are compiled by `qt_add_translations` into `.qm`
files that are embedded in the `:/i18n` resources.

## Supported languages

| Code | File | Language |
|---|---|---|
| `en` | — | English (source) |
| `ru` | `qtranslate_ru_RU.ts` | Русский |
| `es` | `qtranslate_es.ts` | Español |
| `de` | `qtranslate_de.ts` | Deutsch |
| `fr` | `qtranslate_fr.ts` | Français |
| `pt-BR` | `qtranslate_pt_BR.ts` | Português (Brasil) |
| `it` | `qtranslate_it.ts` | Italiano |
| `pl` | `qtranslate_pl.ts` | Polski |
| `uk` | `qtranslate_uk.ts` | Українська |
| `tr` | `qtranslate_tr.ts` | Türkçe |
| `zh-Hans` | `qtranslate_zh_CN.ts` | 简体中文 |
| `ja` | `qtranslate_ja.ts` | 日本語 |
| `ko` | `qtranslate_ko.ts` | 한국어 |
| `ar` | `qtranslate_ar.ts` | العربية |
| `hi` | `qtranslate_hi.ts` | हिन्दी |

The list of codes and native names for the dropdown is defined in `src/core/uilanguages.cpp`.
Adding a language requires a new row in this table, a new `.ts`, and an entry in
`QTRANSLATE_TS_FILES` in `CMakeLists.txt`.

Qt standard dialogs (`QMessageBox` buttons, context menus, file dialogs) are translated by separate
`qt_<locale>.qm` files. They are deployed via `--translations` in `CMakeLists.txt` and loaded in
`AppController::applyUiLanguage()` from `translations/` next to the installed exe (in a dev build —
from `QLibraryInfo::TranslationsPath`). Qt 6.12 has no `qt_hi.qm`, so standard dialogs for Hindi
stay English.

## Updating strings

After adding or changing `tr()` strings:

```powershell
C:\Qt\6.12.0\msvc2022_64\bin\lupdate.exe src -ts i18n/qtranslate_ru_RU.ts i18n/qtranslate_es.ts ... --no-obsolete
```

Or build the `update_translations` target if it is available in the selected generator. New strings
appear with `type="unfinished"`; they must be translated. `tst_translations` fails if any supported
`.ts` still has an empty or unfinished string.

Integrity check:

```powershell
ctest --test-dir build/cli-Release -R tst_translations --output-on-failure
```

## Translation rules

- Keep `%1`, `%2`, `%n`, and other placeholders unchanged.
- Keep emoji and symbols `📋`, `↗`, `✓`, `→`.
- Do not translate the names `qtranslate`, `DeepL`, `HTTP`, `SOCKS5`; `Google Translate` can be
  kept as a brand or replaced with the local name of the service.
- Do not add or remove leading or trailing spaces in strings like `" characters"` and `" ms"`.
- For Arabic, check RTL: windows, menus, the popup, and settings must not be clipped.
- Translations are a machine-assisted first pass; selective native-speaker review is desirable
  before a release.

## Adding a new language

1. Create a `.ts` with `lupdate` or copy an existing one and replace `language`.
2. Add the file to `QTRANSLATE_TS_FILES` in `CMakeLists.txt`.
3. Add the code, native name, and the path to the `.qm` in `src/core/uilanguages.cpp`.
4. Add the file to the `tst_translations` list and check code normalization in `tst_uilanguages`.
5. Build Debug/Release and check that the language switches without a restart.
