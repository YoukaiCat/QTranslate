---
kind: task
status: todo
depends: [01-libretranslate-service]
---

# LibreTranslate base URL setting, settings group and translations

## Task

Store the LibreTranslate base URL, connect it to the service, expose it on the Services page, translate
the new strings and cover the setting and the dialog with tests. No parser, protocol or error-mapping
change: those are `01-libretranslate-service`.

Settings: `LibreTranslateSettings` in `src/core/settings.h` with `baseUrl`, defaulting to the value the
research chose (a public instance that answered without a key) or to an empty string when there is none;
`SettingsStore` loads and saves `[libretranslate] baseUrl` and normalizes it with the shared base-URL
helpers. An empty value is valid and means "not configured": the service then reports
`configurationError()`. The settings format version stays 1, because the new key has a default and an old
file without the group loads with it; do not rewrite the file only because the key is absent.

Shared helpers: extract `isValidServiceBaseUrl()`/`normalizedServiceBaseUrl()` in
`src/core/settings.h/.cpp` from the Mozhi rules — absolute `http(s)`, a host, no credentials, no query,
no fragment, a path prefix kept, a trailing slash removed — and keep
`isValidMozhiInstance()`/`normalizedMozhiInstance()` as thin wrappers with unchanged behaviour, so
`tst_settings` and `03-free-web-endpoints/03-mozhi` stay valid. The helpers do not log;
`SettingsStore::normalize()` logs a fixed warning that never echoes the typed value.

Wiring: `LibreTranslateService::applySettings()` reads the value and calls `setBaseUrl()`;
`AppController` clears the translation cache when the value changes and does not abort an active reply,
like the Mozhi instance change. The provider stays at the end of the provider order.

UI: a `LibreTranslate` `QGroupBox` with objectName `libretranslateGroup` appended at the end of the
Services page, after the `Reverso` group and after the `DuckDuckGo` group when that provider exists, so
the page follows the provider order. It has an editable `QLineEdit` with objectName
`libretranslateBaseUrl` prefilled with the setting and the default as placeholder, a word-wrapped
`QLabel` with objectName `libretranslateEffectiveUrl` showing the normalized value, a `QLabel` with
objectName `libretranslateUrlWarning` visible only for an invalid value, and a word-wrapped `QLabel`
with objectName `libretranslateHint` that says the provider talks to a LibreTranslate server, usually
self-hosted, that the base URL must point at a server the user trusts, that its terms, availability and
installed languages are the user's responsibility, that quality is lower than Google/DeepL, and that
long text is sent in several requests. `settings()` returns the normalized value; `validate()` adds no
new failure because normalization handles invalid input. Reuse an existing source string when the
meaning is identical (for example the effective-value label), so translators do not see near duplicates.

Tests and translations: `tst_settings` covers the default (empty or the public instance), the round
trip, the normalization rows and the unchanged Mozhi rows; `tst_settingsdialog` covers the group, the
field, the effective label, the warning and the hint. The new strings go through the
`update_translations` target and are translated by hand in all 14 `i18n/qtranslate_*.ts` files;
`lrelease` reports no unfinished or vanished message for them. `AGENTS.md`, `doc/` and both READMEs are
the lead's pass.

If the shared helper extraction, the dialog and the 14 `.ts` files grow beyond a task, report
`НЕ УКЛАДЫВАЕТСЯ` instead of splitting silently: settings and UI could then become two nodes.

## Done when

- `[libretranslate] baseUrl` is loaded, saved and normalized with the documented default and the shared
  helpers; an empty value is preserved as "not configured"; `tst_settings` covers the default, the round
  trip, the normalization rows and still passes every Mozhi row unchanged.
- `LibreTranslateService::applySettings()` applies the normalized value, and a change clears the
  translation cache without aborting an active reply.
- The Services page shows the `LibreTranslate` group in the provider order with the field, the effective
  value, the warning and the hint; `tst_settingsdialog` finds them and checks the effective value of an
  uncanonical but valid input and the hidden warning for a valid one.
- The new strings are in all 14 `.ts` files, translated, with no unfinished or vanished entry
  introduced.
- Debug and Release build without warnings on Linux and the full suite passes; Windows is reviewed by
  diff and recorded as not performed.
