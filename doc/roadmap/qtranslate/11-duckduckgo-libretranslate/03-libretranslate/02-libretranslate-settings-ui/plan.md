---
kind: task
status: todo
depends: [01-libretranslate-service]
---

# LibreTranslate base URL setting, settings group and translations

## Task

Store the LibreTranslate base URL and the optional API key, connect them to the service, expose them
on the Services page, translate the new strings and cover the settings and the dialog with tests. No
parser, protocol or error-mapping change: those are `01-libretranslate-service`.

Settings: `LibreTranslateSettings` in `src/core/settings.h` with `baseUrl` (empty by default; the
research found no public key-less instance) and `apiKey` (empty by default, optional); `SettingsStore`
loads and saves `[libretranslate] baseUrl` and normalizes it with the shared base-URL helpers, and
stores `apiKey` as a secret through `SecretProtector` (`writeSecret`/read path, as the DeepL, Google
and LLM keys do). An empty base URL is valid and means "not configured": the service then reports
`configurationError()`. An empty key is valid and omits `api_key`. The settings format version stays 1,
because the new keys have defaults and an old file without the group loads with them; do not rewrite
the file only because the keys are absent.

Shared helpers: extract `isValidServiceBaseUrl()`/`normalizedServiceBaseUrl()` in
`src/core/settings.h/.cpp` from the Mozhi rules — absolute `http(s)`, a host, no credentials, no query,
no fragment, a path prefix kept, a trailing slash removed — and keep
`isValidMozhiInstance()`/`normalizedMozhiInstance()` as thin wrappers with unchanged behaviour, so
`tst_settings` and `03-free-web-endpoints/03-mozhi` stay valid. The helpers do not log;
`SettingsStore::normalize()` logs a fixed warning that never echoes the typed value.

Wiring: `LibreTranslateService::applySettings()` reads both values and calls
`setBaseUrl()`/`setApiKey()`; `AppController` clears the translation cache when the base URL changes
and does not abort an active reply, like the Mozhi instance change. A key change does not abort an
active reply and does not have to clear the cache, because the key only authenticates the request.
The provider stays at the end of the provider order.

UI: a `LibreTranslate` `QGroupBox` with objectName `libretranslateGroup` appended at the end of the
Services page, after the `Reverso` group and after the `DuckDuckGo` group when that provider exists, so
the page follows the provider order. It has an editable `QLineEdit` with objectName
`libretranslateBaseUrl` prefilled with the setting and the default as placeholder, a `QLineEdit` with
objectName `libretranslateApiKey` in password echo prefilled with the stored key and a placeholder
that says the key is optional and needed only by a hosted instance, a word-wrapped `QLabel` with
objectName `libretranslateEffectiveUrl` showing the normalized value, a `QLabel` with objectName
`libretranslateUrlWarning` visible only for an invalid value, and a word-wrapped `QLabel` with
objectName `libretranslateHint` that says the provider talks to a LibreTranslate server, usually
self-hosted, that the base URL must point at a server the user trusts, that the key is optional and
only for a server that requires one, that its terms, availability and installed languages are the
user's responsibility, that quality is lower than Google/DeepL, and that long text is sent in several
requests. `settings()` returns the normalized value; `validate()` adds no
new failure because normalization handles invalid input. Reuse an existing source string when the
meaning is identical (for example the effective-value label), so translators do not see near duplicates.

Tests and translations: `tst_settings` covers the empty default of both values, the base-URL round
trip and normalization rows, the key round trip through the secret protector and the unchanged Mozhi
rows; `tst_settingsdialog` covers the group, both fields, the effective label, the warning and the
hint. The new strings go through the `update_translations` target and are translated by hand in all 14
`i18n/qtranslate_*.ts` files; `lrelease` reports no unfinished or vanished message for them.
`AGENTS.md`, `doc/` and both READMEs are the lead's pass.

If the shared helper extraction, the dialog and the 14 `.ts` files grow beyond a task, report
`НЕ УКЛАДЫВАЕТСЯ` instead of splitting silently: settings and UI could then become two nodes.

## Done when

- `[libretranslate] baseUrl` and `apiKey` are loaded, saved and normalized; the key goes through
  `SecretProtector`; an empty base URL is preserved as "not configured" and an empty key as "no key";
  `tst_settings` covers the empty defaults, the base-URL round trip and normalization rows, the key
  round trip and still passes every Mozhi row unchanged.
- `LibreTranslateService::applySettings()` applies both values; a base-URL change clears the
  translation cache without aborting an active reply.
- The Services page shows the `LibreTranslate` group in the provider order with both fields (the key
  in password echo), the effective value, the warning and the hint; `tst_settingsdialog` finds them and
  checks the effective value of an uncanonical but valid input and the hidden warning for a valid one.
- The new strings are in all 14 `.ts` files, translated, with no unfinished or vanished entry
  introduced.
- Debug and Release build without warnings on Linux and the full suite passes; Windows is reviewed by
  diff and recorded as not performed.
