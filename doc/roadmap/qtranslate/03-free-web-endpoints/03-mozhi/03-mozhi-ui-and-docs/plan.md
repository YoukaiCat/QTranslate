---
kind: task
status: todo
depends: [02-mozhi-settings]
---

# Mozhi settings UI, translations and docs

## Task

Expose Mozhi in the Services page, translate the new strings, and bring the architecture,
free-endpoints and test-count documents in line. This task changes no service, parser or translation
behaviour; those are done in `01-mozhi-service` and `02-mozhi-settings`. It keeps the stage
decisions: no hidden instance rotation, no key field, the default instance
`https://mozhi.adminforge.de`, the default engine `google`, and the seven documented engine ids.

The group is a `Mozhi` `QGroupBox` placed after the `Free web services` group and after the
`MyMemory` group, so the gate checkbox stays above both. It has an editable `QLineEdit` with
objectName `mozhiInstance` (prefilled with `settings.mozhi.instance`, placeholder
`https://mozhi.adminforge.de`), a `QComboBox` with objectName `mozhiEngine` (the seven entries from
`mozhiEngineIds()`, in that order, display names Google, DeepL, DuckDuckGo, LibreTranslate, MyMemory,
Reverso and Yandex), a word-wrapped `QLabel` with objectName `mozhiEffectiveInstance` showing
`Effective instance: <normalizedMozhiInstance(text)>`, a word-wrapped `QLabel` with objectName
`mozhiInstanceWarning` that is visible only when `isValidMozhiInstance(text)` is false and says that
a base http(s) instance URL (without credentials, a query or a fragment) is required and the default
instance will be used, and a word-wrapped `mozhiHint`
that says that public instances are unofficial, may stop working or rate-limit requests, that some
engines may be disabled on an instance, that a self-hosted instance's terms and stability are the
user's responsibility, that the opt-in gate above must be enabled, that long text is sent in several
requests, and that the text goes to the selected instance and engine. The group is
visible but disabled while `translation/allowFreeServices` is off and follows the gate checkbox via
`updateMozhiControls()`; call it at the end of `loadValues()` so the default-off dialog starts
disabled. Typing a valid but uncanonical URL (a trailing slash) is not a warning; the effective label
shows the canonical value. `settings()` returns the normalized instance and the selected engine so the
dialog's copy is already effective; `validate()` adds no new failure condition because normalization
replaces invalid input.

`tst_settingsdialog` gets a `mozhiGroup` case: find `mozhiGroup`, `mozhiHint`, `mozhiInstance`,
`mozhiEngine`, `mozhiEffectiveInstance` and `mozhiInstanceWarning`; with `AppSettings{}` the group is
disabled, the engine is `google`, the instance shows the default and the warning is hidden; checking
`allowFreeServices` enables the group and unchecking disables it; the combo has seven entries with
the documented data ids; typing `ftp://host` (or an empty field, or
`https://host/?engine=google`) shows the warning and the effective default, typing
`http://127.0.0.1:8080/mozhi/` hides the warning and shows
`http://127.0.0.1:8080/mozhi`; `dialog.settings().mozhi` carries the normalized values. Add a second
case that loads an `AppSettings` with a custom instance and engine and verifies the widgets.

The new `SettingsDialog` messages go into all 14 `.ts` files: the group title `Mozhi`, the
`Instance URL:` and `Engine:` labels, `Effective instance: %1`, the instance warning, and the long
`mozhiHint`. The engine names are product names and are not wrapped in `tr()`. Run `lupdate` over
`src` with `--no-obsolete` including `src/platform/hotkeymanager_win.cpp` in the scan (the CMake
`QTRANSLATE_LUPDATE_EXTRA_SOURCES` already does this on Linux), fill every new message in all 14
`i18n/qtranslate_*.ts`, and keep 0 new/0 vanished; `tst_translations` must pass. Record the actual
message count in the outcome instead of guessing; it should be about 301 → 307.

`doc/architecture.md`:
- §3.1 adds `mozhiservice.h/.cpp` and `mozhiparser.h/.cpp` to the services tree, `mozhi` to the
  fixtures line and `tst_mozhiparser`/`tst_mozhiservice` to the tests tree.
- §3.2 adds the Mozhi sources and tests to the target lists and changes the counts to 28 Windows and
  33 Linux.
- §4.5 adds `mozhi` to the `id()` example and says that `DeepLWebService`, `MyMemoryService` and
  `MozhiService` use the queue, with MyMemory at 350 ms and Mozhi at 500 ms.
- A new §4.7.4 `MozhiService — free multi-engine proxy` after §4.7.3 documents the editable instance,
  the engine list/default, the GET `/api/translate` query, the opt-in gate, `auto` and the
  `UnsupportedLanguage` behaviour, the 5000 UTF-16 units via `TextChunker::split`, the 500 ms
  assumption, the same-origin redirect policy, the response fields (`translated-text`, `detected`,
  `source_language`, `target_transliteration`), the error table (429/403/rate-limit/status-code-429,
  413/414, 5xx, HTML, invalid language, disabled engine, JSON error, malformed/missing text,
  timeout) and the tests.
- §4.8 notes that Mozhi reuses the Google column (`zh-CN`, `zh-TW`, `iw`, `jw`, `tl`, `no`, `pt`)
  and that `mozhiCodeOf()` is the single fix point; no `LanguageInfo` column is added.
- §4.9 notes that Mozhi is the second real opt-in consumer; the gate logic is unchanged.
- §4.11 adds `mozhi` to the `AppSettings` group list and the `mozhi/instance` and `mozhi/engine`
  rows to the file-key table with the defaults, the normalization rules, the engine ids and the
  statement that the format version stays 1.
- §8 adds `tst_mozhiparser` and `tst_mozhiservice` rows and updates `tst_languages`,
  `tst_settings`, `tst_settingsdialog` and `tst_mozhiwebservice` descriptions.
- §4.14 changes 31→33, 23 shared→25 shared, 26→28 Windows and "the other 23 tests" to "the other 25
  tests"; the historical 0.12.0 sentence stays unchanged.

`doc/free-endpoints.md`: update the status line, add an "Implementation in qtranslate" note in §5
(request/response, instance/engine, defaults, 500 ms, 5000 units, `auto`, errors, gate, tests), mark
Mozhi implemented in §8 items 2-4 and §9 item 3, and keep the instance/engine table and the privacy
rules.

`AGENTS.md`: change the build/test sentence from 26/31 to 28/33. Do not touch `README.md`,
`README.ru.md`, `doc/handoff.md`, the root `plan.md` or the `07-fallback-chain` plans; the final
provider/fallback documentation and counters stay with `07-fallback-chain`/the lead.

**Verification and manual checks.** Build Debug and Release on Linux with `-Wall -Wextra`; run the
full suite with `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`; expect 33/33
and zero `warning:` lines. Check the Services page offscreen at the default gate-off state and after
checking the gate; check that the pickers include Mozhi only with the gate on and that an INI
`translation/service=mozhi` with the gate off keeps Google active and logs the existing warning.
Record the optional live smoke (the default instance and one self-hosted or alternate public
instance, a short `en→ru`, a long multi-chunk text and a disabled-engine case) as passed or as not
performed with the reason (a public instance may be down or rate-limited). Record Windows
Debug/Release and its 28 tests as not performed (no MSVC).

## Done when

- The Services page has a gate-aware `Mozhi` group with `mozhiInstance`, `mozhiEngine`,
  `mozhiEffectiveInstance`, `mozhiInstanceWarning` and `mozhiHint`; it is disabled while
  `allowFreeServices` is off and follows the gate checkbox; the only `QLineEdit` in the group is
  `mozhiInstance`, and there is no key or password field; `settings()` returns the normalized
  instance and the selected engine.
- `tst_settingsdialog` covers the default state, the gate toggle, the seven engine ids, the effective
  instance, the invalid/empty warning and the normalized round trip; a custom instance/engine loaded
  from `AppSettings` is shown correctly.
- All 14 `.ts` files contain the new `SettingsDialog` messages, `tst_translations` finds no new,
  unfinished, empty or vanished messages, and the Windows-only hotkey message is preserved (the
  `lupdate` scan includes `src/platform/hotkeymanager_win.cpp`).
- `doc/architecture.md` §3.1/§3.2/§4.5/§4.7.4/§4.8/§4.9/§4.11/§8/§4.14, `doc/free-endpoints.md`
  §5/§8/§9 and the `AGENTS.md` 28/33 counts describe the service, the keys, the normalization, the
  500 ms/5000-unit assumptions, the error table, the language mapping and the tests.
- `README.md`, `README.ru.md`, `doc/handoff.md`, the root plan and `07-fallback-chain` are not edited
  here; the outcome records 33/28 for the final pass.
- The full suite passes 33/33 on Linux in Debug and Release without warnings; the settings group and
  the picker gate check are recorded as passed or as not performed with the reason; the optional
  live smoke is recorded as passed or as not performed (a public instance may be down or
  rate-limited); Windows Debug/Release and its 28 tests are recorded as not performed (no MSVC).
- No service, parser, settings-storage or translation behaviour is changed; this task adds only the
  UI, the `.ts` content and the documentation.

## Risks

- The new `SettingsDialog` strings must be translated in all 14 `.ts`; `lupdate` on Linux can mark the
  Windows-only hotkey message vanished unless the scan includes `src/platform/hotkeymanager_win.cpp`.
  Keep that source in the scan and run `tst_translations`.
- The effective instance and the warning must use `isValidMozhiInstance()`/`normalizedMozhiInstance()`
  from `settings.h`, not a second copy of the URL rules in the dialog. A duplicated rule would let the
  UI show one value while the service uses another.
- The dialog returns the normalized values; if it returned the raw text, an invalid URL would only be
  repaired in `SettingsStore::save()`, and `dialog.settings()` would not match what the service gets.
  Keep the round trip test.
- The group must stay readable while disabled and must not look like an enabled service when the gate
  is off. Keep `updateMozhiControls()` at the end of `loadValues()` and on the gate toggle.
- The docs must stay consistent with the code: 500 ms, 5000 UTF-16 units, `auto`, the Google-style
  codes, the engine ids, the error table, the same-origin redirect policy and 33/28. A stale count or
  a claimed live smoke is worse than a not-performed note.
- The optional live smoke may fail because a public instance is down or rate-limited; record it as not
  performed rather than claiming success. Windows Debug/Release and its 28 tests are unavailable on
  this Linux machine; record them as not performed, not as passed.
- Do not add the README/handoff/root/`07` edits here; duplicating the provider/fallback tables would
  create two sources of truth, and the final pass belongs to `07-fallback-chain`/the lead.
