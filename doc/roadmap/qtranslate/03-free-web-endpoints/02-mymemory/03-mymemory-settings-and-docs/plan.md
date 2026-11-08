---
kind: task
status: done
detailed: true
depends: [02-mymemory-service]
---

# MyMemory settings, translations and docs

## Task

Expose MyMemory in the Services page, translate the new strings, and bring the architecture,
free-endpoints and test-count documents in line. This task changes no service, parser or translation
behaviour; those are done in `02-mymemory-service`. It keeps the stage decisions: no `de` email, no
`AppSettings` field, no `LanguageInfo` column and no key or URL setting.

The group is a single `MyMemory` `QGroupBox` with `mymemoryGroup`/`mymemoryHint`, the free/no-key
warning, the opt-in hint (the gate checkbox is above it), no key or URL field, and the note that
MyMemory cannot detect the source language and sends long text in several requests. It is visible but
disabled while `translation/allowFreeServices` is off and follows the gate checkbox. The two new
`SettingsDialog` strings go into all 14 `.ts` files. `doc/architecture.md`, `doc/free-endpoints.md`
and the `AGENTS.md` counts are updated; `README.md`, `README.ru.md`, `doc/handoff.md`, the root-plan
counters and the final provider/fallback tables stay with `07-fallback-chain`/the lead.

**Settings group.** `src/ui/settingsdialog.h/.cpp` gets `class QGroupBox;`, the member
`QGroupBox *m_mymemoryGroup = nullptr;` and `void updateMyMemoryControls();`. In `buildUi()`, after
the `freeWebGroup` (so the gate checkbox is above it), add a `QGroupBox(tr("MyMemory"))` with
objectName `mymemoryGroup`, a word-wrapped `QLabel` with objectName `mymemoryHint` and the text
`tr("MyMemory is a free translation API and needs no key. Enable free web services above to use it. "
"It cannot detect the source language, and long text is sent in several requests. The text is sent "
"to a third party.")`, and no key/URL fields. Connect `m_allowFreeServices` `toggled` to
`updateMyMemoryControls()`, implement it as `setEnabled(m_allowFreeServices->isChecked())`, and call
it at the end of `loadValues()` so the default-off gate starts with the group disabled.
`tests/tst_settingsdialog.cpp` gets `#include <QGroupBox>` and a `myMemoryGroup` case: find
`mymemoryGroup`/`mymemoryHint`; with `AppSettings{}` the group is disabled; the hint is word-wrapped
and contains "no key", "source language" and "third party"; the group has no `QLineEdit`; checking
`allowFreeServices` enables it and unchecking disables it again; `dialog.settings()` has no new field.

**Translations.** Run `lupdate` over `src` for all 14 files in `QTRANSLATE_TS_FILES` (or the
`update_translations` target) with `--no-obsolete`, fill the two new `SettingsDialog` messages in all
14 `i18n/qtranslate_*.ts`, and keep 0 new/0 vanished; `tst_translations` must pass. Service error
messages stay English literals like the other services and add no `.ts` messages. Keep the extra
`src/platform/hotkeymanager_win.cpp` source in the `lupdate` scan so the Windows-only hotkey message
does not become vanished on Linux.

**Architecture and free-endpoints.** In `doc/architecture.md`, §4.5 updates the `QueuedWebService`
paragraph ("DeepLWebService and MyMemoryService use it; MyMemory sets 350 ms") and the `id()` example.
A new §4.7.3 `MyMemoryService — free no-key API` describes the endpoint, query, explicit-source rule,
500-byte `splitUtf8` chunking, sequential chunk pacing, response fields, the error table, the no-`de`
decision, the opt-in gate and the tests. §4.8 adds the MyMemory column/note with the codes and the
override table and states that there is no `LanguageInfo` field. §4.9 notes that MyMemory is the first
real service behind the gate. §8 adds `tst_mymemoryparser` and `tst_mymemoryservice` rows and updates
the `tst_textchunker`/`tst_languages`/`tst_settingsdialog` descriptions. §4.14 changes 29→31,
21 shared→23 shared, 24→26 Windows and "the other 20 tests" to "the other 23 tests". In
`doc/free-endpoints.md`, update the status line, §6 with an "Implementation in qtranslate" note, §8
items 2-4 with the implemented values, and §9 item 2 "implemented". In `AGENTS.md`, change the
build/test sentence from 24/29 to 26/31.

Do not touch `README.md`, `README.ru.md`, `doc/handoff.md`, the root `plan.md`, or the stage/child
plans of `07-fallback-chain`; the final provider/fallback documentation belongs to
`07-fallback-chain`. Record the 31/26 counts in the outcome so the lead and `07` can reconcile the
remaining counters.

**Verification and manual checks.** Run the full suite in Debug and Release on Linux with
`xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`; zero `warning:` with
`-Wall -Wextra`, 31/31. Check the Services page offscreen at the default gate-off state and after
checking the gate; check the pickers include MyMemory only with the gate on, and that an INI
`service=mymemory` with the gate off keeps Google active and logs the warning. Record the optional
live smoke (short en→ru and a long multi-chunk text) as not performed unless the user runs it.
Record Windows Debug/Release and its 26 tests as not performed (no MSVC).

## Steps

1. **Add the `MyMemory` group to `SettingsDialog`.** In `src/ui/settingsdialog.h`, add
   `class QGroupBox;`, the member `QGroupBox *m_mymemoryGroup = nullptr;` and
   `void updateMyMemoryControls();`. In `src/ui/settingsdialog.cpp`, create the group after
   `freeWebGroup` (so the gate checkbox is above it): `auto *myMemoryGroup = new
   QGroupBox(tr("MyMemory"), servicesContent);`, objectName `mymemoryGroup`, a word-wrapped
   `QLabel` with objectName `mymemoryHint` and the text
   `tr("MyMemory is a free translation API and needs no key. Enable free web services above to use "
   "it. It cannot detect the source language, and long text is sent in several requests. The text "
   "is sent to a third party.")`; add the hint to the group layout, add the group to
   `servicesLayout` after `freeWebGroup`, and store it in `m_mymemoryGroup`. Do not add a key,
   URL, email or settings field. Connect `m_allowFreeServices` `toggled` to
   `updateMyMemoryControls()`; implement it as `setEnabled(m_allowFreeServices->isChecked())`;
   call it at the end of `loadValues()` after `m_allowFreeServices->setChecked(...)` so the
   default-off state starts disabled. No service/parser change.

2. **Extend `tst_settingsdialog`.** In `tests/tst_settingsdialog.cpp`, add `#include <QGroupBox>`
   and a `myMemoryGroup` case. Find `mymemoryGroup` and `mymemoryHint`; with `AppSettings{}` the
   group is disabled; the hint is word-wrapped and contains "no key", "source language" and "third
   party"; the group has no `QLineEdit` child; checking `allowFreeServices` enables the group and
   unchecking disables it; `dialog.settings()` has no new field and no `de`/URL value. Run
   `QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux-release -R tst_settingsdialog
   --output-on-failure` (or the equivalent Debug command).

3. **Update all 14 `.ts` files.** Run `lupdate` over `src` for every file in
   `QTRANSLATE_TS_FILES` (`CMakeLists.txt:150-165`) with `--no-obsolete`, including
   `src/platform/hotkeymanager_win.cpp` in the scan (the CMake `QTRANSLATE_LUPDATE_EXTRA_SOURCES`
   already does this for the target; the direct `lupdate src` command includes it). The two new
   `SettingsDialog` messages are the group title `MyMemory` and the hint; translate both in all 14
   `i18n/qtranslate_*.ts`. The message count should go from 299 to 301. `tst_translations` must
   find no new, unfinished, empty or vanished messages. Service error messages stay English
   literals and add no `.ts` messages.

4. **Update `doc/architecture.md`.** In §4.5, change the `id()` example to include `mymemory`
   (`:636`) and the queue paragraph (`:684`) to say that `DeepLWebService` and `MyMemoryService`
   use `QueuedWebService`, with MyMemory setting 350 ms. Add §4.7.3 `MyMemoryService — free no-key
   API` after §4.7.2 (`:880`) and before §4.8: endpoint `GET
   https://api.mymemory.translated.net/get?q=…&langpair=<src>|<dst>&mt=1`, explicit source only
   (`auto` → `UnsupportedLanguage`), 500-byte `splitUtf8` chunks, sequential chunks inside one
   queue slot, `responseData.translatedText`/`responseStatus`/`responseDetails`/`quotaFinished`,
   optional `detectedLanguage`, the error table (429/403, quota, 413/414, 5xx, HTML, malformed,
   unknown body status), no `de` email, `requiresOptIn()`, and the tests. In §4.8 (`:887-895`), add
   a MyMemory column/note with `zh-Hans`→`zh-CN`, `zh-Hant`→`zh-TW`, `nb`→`no`, `fil`→`tl`, and
   state that there is no `LanguageInfo` field. In §4.9 (`:967-973`), note that MyMemory is the
   first real opt-in consumer; the gate logic is unchanged. In §8 (`:1579-1610`), add
   `tst_mymemoryparser` and `tst_mymemoryservice` rows and update the `tst_textchunker` (UTF-8
   byte split), `tst_languages` (MyMemory codes), `tst_settingsdialog` (MyMemory group) and
   `tst_translationmanager` (real gated service) descriptions. In §4.14 (`:1325` and `:1367`),
   change 29→31, 21 shared→23 shared, 24→26 Windows and "the other 20 tests" to "the other 23
   tests". Do not change the historical 0.12.0 sentence at `:1769`.

5. **Update `doc/free-endpoints.md` and `AGENTS.md`.** In `doc/free-endpoints.md`, update the
   status line near the top, add an "Implementation in qtranslate" note in §6, mark the
   implemented values in §8 items 2-4, and make §9 item 2 "implemented". In `AGENTS.md:39`,
   change "24 Windows tests and all 29 Linux tests" to "26 Windows tests and all 31 Linux tests".
   Do not touch `README.md`, `README.ru.md`, `doc/handoff.md`, the root `plan.md`, or the
   `07-fallback-chain` plans; the outcome records 31/26 for the final documentation pass.

6. **Verify and record.** Build Debug and Release on Linux with `-Wall -Wextra`; run the full suite
   with `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`; expect 31/31 and
   zero `warning:` lines. Check the Services page offscreen: with the gate off the group is
   disabled, with it on it is enabled; no key/URL field. Check the pickers: MyMemory is absent
   with `allowFreeServices` off and appears when it is on. Check that an INI
   `translation/service=mymemory` with the gate off keeps Google active and logs "Configured
   service needs free web services to be enabled". A real UI check is optional; record it as
   passed or as not performed. Record the optional live smoke (short en→ru and a long multi-chunk
   text) as not performed unless the user runs it. Record Windows Debug/Release and its 26 tests
   as not performed (no MSVC). Leave commit and push to the coordinator/lead.

## Done when

- The settings dialog shows `mymemoryGroup` with `mymemoryHint`, the free/no-key warning, the opt-in
  hint, no key or URL field and no new settings value; the group is disabled while
  `allowFreeServices` is off and enabled when it is checked; `tst_settingsdialog` covers the group,
  the hint, the missing key field and the gate toggle.
- The 14 `.ts` files contain the two new `SettingsDialog` messages (299 → 301 messages);
  `tst_translations` finds no new, unfinished, empty or vanished messages; no service error string
  was moved to `tr()`.
- `doc/architecture.md` §4.5/§4.7.3/§4.8/§4.9/§8/§4.14 and `doc/free-endpoints.md` §6/§8/§9 are
  updated with the endpoint, explicit-source rule, 350 ms, 500-byte chunks, error table, language
  codes and tests; `AGENTS.md` has the 26/31 counts.
- `README.md`, `README.ru.md`, `doc/handoff.md`, the root-plan counters and `07-fallback-chain` are
  not edited here; the outcome records the 31/26 counts for the final documentation pass.
- The full suite passes 31/31 on Linux in Debug and Release without warnings; the settings group and
  the picker gate check are recorded as passed or as not performed with the reason; the optional
  live smoke is recorded as not performed unless the user runs it; Windows Debug/Release and its 26
  tests are recorded as not performed (no MSVC).
- No service, parser or translation behaviour is changed; the group is the only code behaviour
  added here.

## Risks

- The new `SettingsDialog` strings must be translated in all 14 `.ts`; `lupdate` on Linux can mark
  the Windows-only hotkey message vanished unless the scan includes
  `src/platform/hotkeymanager_win.cpp`. Keep that source in the scan and run `tst_translations`.
- A group with only labels can look misleading if it stays enabled while the gate is off. Keep the
  `setEnabled(m_allowFreeServices->isChecked())` behaviour and call `updateMyMemoryControls()` after
  `loadValues()`; otherwise the default-off state shows an enabled MyMemory block.
- Do not add a key, URL or `de` email field here. MyMemory needs none of them; a new `AppSettings`
  field would expand the INI and UI surface without a user decision.
- Keep the README/handoff/root/`07` boundary. Duplicating the provider/fallback tables here would
  create two sources of truth; the outcome carries the 31/26 counts for the final pass.
- The docs must stay consistent with the code: 350 ms, 500 UTF-8 bytes, explicit source only,
  `zh-CN`/`zh-TW`/`no`/`tl` mappings, no `LanguageInfo` column and the 31/26 counts. A stale count or
  a claimed live smoke is worse than a not-performed note.
- The optional live smoke may fail because of quota or network; record it as not performed rather
  than claiming success. Windows Debug/Release and its 26 tests are unavailable on this Linux
  machine; record them as not performed, not as passed.

## Outcome

Done on 2026-11-09. The Services page shows the gate-aware `MyMemory` group, the 14 `.ts` files
carry the two new strings, and the architecture, free-endpoints and `AGENTS.md` documents are in
line with the service. No service, parser, `AppSettings` or INI behaviour changed.

- **Settings group:** `src/ui/settingsdialog.h/.cpp` has `m_mymemoryGroup`, `mymemoryHint` and
  `updateMyMemoryControls()`. The group is created after `freeWebGroup` (so the gate checkbox is
  above it), has a word-wrapped hint with the free/no-key warning, the opt-in hint, the explicit
  source and multi-request notes, and no key, URL, email or `QLineEdit` field. `toggled` follows the
  gate checkbox, and the call at the end of `loadValues()` starts the default-off dialog with the
  group disabled. `tests/tst_settingsdialog.cpp::myMemoryGroup` covers the title, hint text and word
  wrap, the absent `QLineEdit`, the disabled default, both toggle directions and
  `dialog.settings().translation.allowFreeServices`; no new settings field exists.
- **Translations:** all 14 `i18n/qtranslate_*.ts` went from 299 to 301 messages. `lupdate` ran from
  the `update_translations` target (Qt 6.10.2, with `src/platform/hotkeymanager_win.cpp` in the
  scan), the group title and hint are translated in every file, the Windows-only hotkey message is
  still present, and `tst_translations` finds no new, unfinished, empty or vanished messages. Service
  error strings remain English literals and add no `.ts` messages.
- **Architecture:** `doc/architecture.md` §4.5 has `mymemory` in the `id()` example and names
  `DeepLWebService` and `MyMemoryService` as the queue users (MyMemory 350 ms). The new §4.7.3
  documents the endpoint and query, explicit-source-only behaviour, 500-byte `splitUtf8` chunks,
  sequential chunks inside one queue slot, the response fields, the error table (403/429, quota,
  413/414, 5xx, HTML, malformed, unknown body status), fixed messages, no `de` email,
  `requiresOptIn()` and the tests. §4.8 adds the MyMemory code column and states the override table
  (`zh-Hans`→`zh-CN`, `zh-Hant`→`zh-TW`, `nb`→`no`, `fil`→`tl`) and the absent `LanguageInfo`
  field; §4.9 notes the first real opt-in consumer. §8 adds `tst_mymemoryparser` and
  `tst_mymemoryservice` and updates the `tst_textchunker`, `tst_languages`, `tst_settingsdialog` and
  `tst_translationmanager` descriptions. §4.14 now says 31 tests (23 shared plus 8 Linux platform),
  26 Windows and "the other 23 tests"; the historical 0.12.0 and migration sentences are unchanged.
  For consistency with the already registered service, §3.1 and §3.2 also gained the MyMemory
  sources, fixtures, tests and the 26/31 counts.
- **Free endpoints and `AGENTS.md`:** `doc/free-endpoints.md` has the updated status line, the §6
  "Implementation in qtranslate" note, implemented values in §8 items 2-4 and "implemented" in §9
  item 2. `AGENTS.md:39` says 26 Windows and 31 Linux tests. `README.md`, `README.ru.md`,
  `doc/handoff.md`, the root plan and `07-fallback-chain` were not edited; the final
  provider/fallback documentation and counters stay with them.
- **Verification:** Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2) and `qtranslate-trixie:latest` (Qt 6.8.2,
  GCC 14.2), Debug and Release: builds without `warning:` under `-Wall -Wextra`; the full suite is
  31/31 in all four configurations; `tst_settingsdialog` 22 passed, `tst_translations` passed,
  `tst_translationmanager` 24 passed. The Services group was checked offscreen by the test. The
  picker filter was checked in `AppController::updateServiceSelectors()` and by
  `tst_translationmanager::gatedRealServiceIsRefused`; a real offscreen run with
  `translation/service=mymemory` and the gate off kept Google active and logged
  `Configured service needs free web services to be enabled: "mymemory"`. The optional live smoke
  (short en→ru and a long multi-chunk text) and Windows Debug/Release with its 26 tests were not
  performed (no MSVC; CTest never contacts the real endpoint).
- **Review:** no findings, no rejected remarks, no deferred real problems.
