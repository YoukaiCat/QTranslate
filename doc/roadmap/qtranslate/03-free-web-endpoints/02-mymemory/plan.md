---
kind: stage
status: done
base: 371ab1e
depends: [01-free-web-infrastructure]
---

# MyMemory service

## Goal

Add MyMemory as the first real no-key service and the first consumer of the opt-in gate and the
shared per-host queue from `01-free-web-infrastructure`: a `mymemory` service with a 350 ms pause,
500 UTF-8-byte chunks, explicit source language only, classified errors, a settings group, and
tests/docs. The work is split into three tasks so each part has its own acceptance: the shared
prerequisites, the service and parser, and the UI/translations/documentation.

## What to do, what is touched

- `01-split-utf8-and-language-codes` (task, detailed): `TextChunker::splitUtf8` in
  `src/core/textchunker.*` with `tst_textchunker`; the MyMemory language-code branch and override
  table in `src/core/languages.cpp` with `tst_languages`. No service, UI or documentation.
- `02-mymemory-service` (task): `src/services/mymemoryparser.*`, `src/services/mymemoryservice.*`,
  fixtures and `tst_mymemoryparser`/`tst_mymemoryservice`, registration in `AppController`, CMake
  and the two new test targets. No settings UI, translations or docs.
- `03-mymemory-settings-and-docs` (task): the `MyMemory` group in `SettingsDialog` with
  `tst_settingsdialog`, the 14 `.ts` files, `doc/architecture.md`, `doc/free-endpoints.md`, and the
  `AGENTS.md` test counts. No service behaviour change.

Decisions carried through all children:

- No `de` email parameter and no new `AppSettings` field or INI key. The anonymous quota is enough
  for a fallback, and a personal-data flow needs a separate user decision.
- No `LanguageInfo` column: the MyMemory mapping is a branch plus a small override table in
  `languages.cpp` (`zh-Hans`→`zh-CN`, `zh-Hant`→`zh-TW`, `nb`→`no`, `fil`→`tl`); all other languages
  use their internal ISO 639-1/RFC 3066 code. The catalogue follows Google's.
- `TextChunker::splitUtf8` is a separate function; `TextChunker::split()` stays byte-identical.
- The parser accepts only the optional `responseData.detectedLanguage`; MyMemory normally reports no
  detected language because the source is explicit.
- `README.md`, `README.ru.md`, `doc/handoff.md`, the root-plan counters and the final
  provider/fallback tables stay with `07-fallback-chain` and the lead. `doc/architecture.md`,
  `doc/free-endpoints.md` and the `AGENTS.md` counts are updated in child `03`.

## Done when

- `01-split-utf8-and-language-codes` is done: `splitUtf8` and the MyMemory code mapping are tested,
  `split()` is unchanged, and no service/UI/docs are touched.
- `02-mymemory-service` is done: `mymemory` is registered with `requiresOptIn() == true`,
  `isConfigured() == true`, a 350 ms default pause, explicit-source-only behaviour, 500-byte
  `splitUtf8` chunks, sequential queue pacing and the classified errors; the parser/service tests
  are green, and Linux has 31 tests (Windows 26, not run).
- `03-mymemory-settings-and-docs` is done: the Services page has the gate-aware `MyMemory` group with
  no key field, the 14 `.ts` files are complete, and the architecture/free-endpoints/AGENTS documents
  have the 350 ms, 500-byte, language and 31/26 notes.
- The overall MyMemory criteria hold: the service is behind `translation/allowFreeServices`, uses
  350 ms and 500 UTF-8 bytes, rejects `auto` before a request, maps 429/403/quota/5xx/HTML/malformed
  responses to the documented errors, serializes requests through `QueuedWebService`, and shows the
  settings group.
- The full suite passes 31/31 on Linux in Debug and Release without warnings; Windows Debug/Release
  and its 26 tests are recorded as not performed; the optional live smoke is recorded as not
  performed unless the user runs it. CTest never contacts the real endpoint.
- `README.md`, `README.ru.md`, `doc/handoff.md`, the root-plan counters and `07-fallback-chain` are
  not edited here.

## Depends on

`01-free-web-infrastructure` (the gate and `QueuedWebService`). The children are ordered:
`02-mymemory-service` depends on `01-split-utf8-and-language-codes`, and
`03-mymemory-settings-and-docs` depends on `02-mymemory-service`. The order is a real dependency, not
only numbering.

## Risks and what is needed from the user

- No new user access or key is needed: MyMemory is a free API. A live smoke may fail because of the
  quota or the network; it is optional and recorded as not performed rather than claimed as passed.
- The language code table is based on the Google-derived catalogue and is not live-verified;
  `zh-CN`, `zh-TW`, `no`, `tl`, `he` and `jv` are the likely codes. The override table is the single
  fix point if a live smoke shows an invalid pair; do not add a per-service `LanguageInfo` column or
  a settings field for a code.
- The 500-byte limit applies to the decoded `q`; the percent-encoded URL expands. If MyMemory rejects
  a 500-byte chunk with 414/TooLong or a query-length body, lower the limit in one constant and
  record the measured behaviour.
- Long texts produce many sequential requests (20,000 Cyrillic characters are about 80 chunks at
  350 ms). Do not add a hidden cap or an overall timeout without a user decision; the fallback policy
  belongs to `07-fallback-chain`.
- The splitter is unused in production until `02-mymemory-service`; this is accepted so the core
  change can be reviewed alone. Do not add a temporary call site.
- The service is registered in `02` but its settings group and hint arrive in `03`; the intermediate
  state is acceptable because the gate and the pickers already exist, but the picker check and the
  user-facing hint belong to `03`.
- Windows Debug/Release and its 26 tests are unavailable on this Linux machine; record them as not
  performed, not as passed. The final documentation is split: the service docs in `03`, the provider
  and fallback docs in `07-fallback-chain`/the lead.

## Outcome

Accepted on 2026-11-08 over `371ab1e..082dac4`. Three children are `done`:
`01-split-utf8-and-language-codes` (`d3fc444`), `02-mymemory-service` (`f8fb880`), and
`03-mymemory-settings-and-docs` (`9cfbdde`).

- **Core:** `TextChunker::splitUtf8` limits each part by UTF-8 bytes without splitting a surrogate
  pair or `\r\n`, and `split()`/`cutLength()` are unchanged; `Languages` has the `mymemory` branch and
  the `zh-Hans`→`zh-CN`, `zh-Hant`→`zh-TW`, `nb`→`no`, `fil`→`tl` overrides without a new
  `LanguageInfo` column.
- **Service:** `MyMemory` is an opt-in `QueuedWebService` (`requiresOptIn() == true`) with a 350 ms
  pause, `GET /get` with `q`/`langpair`/`mt=1`, a 500-byte UTF-8 split, explicit source language
  (`auto` → `UnsupportedLanguage` before any request), classified 403/429/quota/413/414/5xx/HTML/
  malformed errors with fixed messages, sequential chunks in one reply, abort/timeout, and no text,
  URL, body, or `errorString()` in the logs. `MyMemoryParser` covers the response fields and leaves
  `detectedLanguage` optional. `AppController` registers the service after the LLM service; the gate
  hides it and keeps Google active when the INI names it with the gate off.
- **UI/docs:** the Services page has the disabled-while-gated `MyMemory` group and hint; all 14 `.ts`
  have the two new messages (301, no unfinished/vanished); `doc/architecture.md`
  §3.1/§3.2/§4.5/§4.7.3/§4.8/§4.9/§8/§4.14, `doc/free-endpoints.md` §6/§8/§9 and `AGENTS.md` 26/31
  are updated.
- **Verification:** Astra Linux 1.8.7 (Qt 6.10.2) and `qtranslate-trixie` (Qt 6.8.2), Debug and
  Release, zero warnings, 31/31; `tst_mymemoryservice` 36 cases and 10 repeats; the gate/INI picker
  behaviour was checked offscreen; `.ts` complete.

The acceptance found one real defect, fixed in `082dac4`: a literal `+` in the query was not encoded,
so `C++`, `1+1`, or `+7...` reached MyMemory as spaces; the query now sends `%2B`, and
`tst_mymemoryservice` checks the raw and decoded values.

Real problems left unfixed: Auto-detect remains selectable for MyMemory, which has no auto-detection,
so a user who leaves it gets `UnsupportedLanguage` until choosing a language (the hint and the error
explain it; fixing the shared language combos is a later decision). A possible HTML-entity decoding
of `translatedText` is unconfirmed without a live smoke. Windows Debug/Release and the 26 tests and
the live MyMemory smoke were not performed (no MSVC; CTest is offline). Next: `03-mozhi`.
