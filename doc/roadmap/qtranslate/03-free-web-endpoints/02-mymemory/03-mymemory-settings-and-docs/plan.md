---
kind: task
status: todo
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

## Done when

- The settings dialog shows `mymemoryGroup` with `mymemoryHint`, the free/no-key warning, the opt-in
  hint, no key or URL field and no new settings value; the group is disabled while
  `allowFreeServices` is off and enabled when it is checked; `tst_settingsdialog` covers the group,
  the hint, the missing key field and the gate toggle.
- The 14 `.ts` files contain the two new `SettingsDialog` messages; `tst_translations` finds no new,
  unfinished or vanished messages; no service error string was moved to `tr()`.
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
