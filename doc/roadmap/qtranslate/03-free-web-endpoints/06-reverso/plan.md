---
kind: stage
status: in-progress
base: d2cdf3c
depends: [01-free-web-infrastructure]
---

# Reverso service

## Goal

Add the unofficial Reverso endpoint as the last free service of `03-free-web-endpoints`. Reverso is a
niche service with a narrower language set — the upstream plugin declares only English, French,
German, Spanish, Italian and Russian and rejects automatic detection — so it is a fallback rather
than a replacement for Google/DeepL. The stage requires it to return clear errors, and a successful
translation is a bonus verified by a manual smoke.

The original node was a single task. It is split here into four children because the pure parser and
the language branch, the network service and its registration, the settings UI with the 14 `.ts`
files, and the documentation each have their own acceptance and can be reviewed independently.

Use `doc/free-endpoints.md` §4 for the protocol, §8 for the rules, and §9 for the priority. The
protocol shapes below were also checked against the upstream `reverso-services` plugin
(`ReversoClient.kt`, `ReversoLanguageMapper.kt`, `ReversoModels.kt`,
`ReversoTranslatorService.kt` and `ReversoServicesTest.kt`, branch `develop`, read on 2026-11-09),
which is the source named in `doc/free-endpoints.md`.

## What to do, what is touched

- `01-reverso-parser` (task, detailed): the `reverso` language branch in `languages.cpp` and the pure
  `ReversoParser` — the flat JSON success/error body, `directionFrom`/`directionTo`, HTTP status
  mapping — plus saved fixtures, `tst_reversoparser` and `tst_languages`. No service, registration,
  UI, translations or docs.
- `02-reverso-service` (task): `ReversoService` on `QueuedWebService` with the fixed JSON `POST`, the
  600 ms pause, 5000 UTF-16-unit chunks, `auto` rejection, registration in `AppController` and
  `tst_reversoservice` against a local `QTcpServer`. No settings UI, translations or docs.
- `03-reverso-settings` (task): the gate-aware `Reverso` group in `SettingsDialog`,
  `tst_settingsdialog` and the 14 `.ts` files. No service behaviour change.
- `04-reverso-docs` (task): `doc/architecture.md`, `doc/free-endpoints.md` and the `AGENTS.md` test
  counts. No code.

Decisions carried through all children:

- The service id is `reverso`; the display name is `Reverso`; `isConfigured()` and
  `requiresOptIn()` return true. It stays off by default, is hidden from the pickers and is refused
  by `TranslationManager` with `ServiceUnavailable` while `translation/allowFreeServices` is off.
  There is no key, no per-service URL and no `AppSettings`/INI field for Reverso.
- The endpoint is fixed: `POST https://cps.reverso.net/api2/TranslateText` with a JSON object whose
  fields are `source`, `direction`, `deviceId` (`"0"`), `uiLang` (`"en"`), `origin`
  (`"chromeextension"`), `accessToken` (`""`) and `appId` (`"0"`). The document's prose and the
  upstream model use strings for the numeric-looking fields, so `deviceId` and `appId` are strings,
  not numbers. The upstream plugin sends a browser Chrome `User-Agent`; Reverso is a
  Chrome-extension endpoint, so the service sets a fixed browser `User-Agent` on its requests only,
  after `NetworkManager::makeRequest()`, as protocol emulation (the Yandex/Bing pattern).
  `AppInfo::userAgent()` and every other service keep the application UA. No cookie is read or
  written.
- The language branch declares only the six codes the upstream mapper supports: `en`, `fr`, `de`,
  `es`, `it`, `ru`. The internal codes equal Reverso's codes, so no override is needed. The six
  languages form a full cross product of 36 directions (the upstream service validates the two
  languages individually and has no pair table); the `direction` value is `<source>-<target>`.
  `auto` is not supported: the service returns `UnsupportedLanguage` with a message telling the user
  to choose an explicit source language, before any request. Any code outside the six returns
  `UnsupportedLanguage` before any request; do not claim the Google catalogue.
- The response is a flat JSON object: `translation`, `directionFrom`, `directionTo`, `success`,
  `message`, and the upstream `error` flag. `error == true` or `success == false` is an error even
  with HTTP 200; `success == true` without a non-empty `translation` is `BadResponse`.
  `directionFrom` fills `detectedSourceLang` through the `reverso` branch; `directionTo` is parsed
  and tolerated but not used by the service.
- Errors are classified body-first: HTTP 429/403 and a rate-limit body become `RateLimited`; 5xx
  becomes `Network`; an HTML page or a blocked response becomes `ServiceUnavailable`; 413 becomes
  `TooLong`; a `success: false` object becomes `ServiceUnavailable` with the service `message` only
  when it is short and plain, otherwise with a fixed literal. A malformed or undocumented body is
  `BadResponse`, never raw HTML. A timeout and `abort()` stop the request with
  `Timeout`/`Cancelled`.
- The service uses `QueuedWebService` with a 600 ms pause and sends long text in 5000 UTF-16-code-unit
  chunks (`TextChunker::split`) sequentially inside one queue slot, restoring edge whitespace; an
  all-whitespace part is never sent. The 600 ms pause and the 5000-unit chunk are conservative named
  assumptions, not measured limits; the document gives no Reverso-specific value.
- The JSON body contains the text, so the error/log messages must not include the body. The
  validated short provider `message` may be shown to the user but is not logged; the log has only
  fixed literals, the internal language codes, the safe `direction`, the HTTP status and elapsed
  time. The text, the body and the URL must never be logged or put into a message.
- Tests use saved fixtures and a local `QTcpServer`; CTest never contacts the real endpoint. A live
  smoke is manual and is recorded as passed or as not performed with the reason (a narrower language
  set, a block, or no live run). There is no captcha solver, no cookie harvesting and no bypass of a
  block.
- Expected counts: `01-reverso-parser` adds one target, so Linux becomes 38 and Windows would be 33;
  `02-reverso-service` adds the second, so Linux becomes 39 and Windows would be 34 (31 shared plus
  3 Windows-only on Windows; 31 shared plus 8 Linux-only on Linux). `03-reverso-settings` adds no
  target and moves the `.ts` message count from 311 to 313. Windows is derived from
  `tests/CMakeLists.txt`, not run here.
- `README.md`, `README.ru.md`, `doc/handoff.md`, the root plan and `07-fallback-chain` stay with the
  lead and are not edited by the children. `doc/architecture.md`, `doc/free-endpoints.md` and the
  `AGENTS.md` counts belong to `04-reverso-docs`.

## Done when

- `01-reverso-parser` is done: the `reverso` language branch exists with exactly `en`, `fr`, `de`,
  `es`, `it` and `ru`; `auto` and every other code map to nothing; `ReversoParser` parses the flat
  success and error objects, the `error` flag, `directionFrom`/`directionTo`, and returns the
  documented errors; all parser and language cases are green; Linux is 38 tests and Windows would be
  33.
- `02-reverso-service` is done: `ReversoService` is registered in `AppController` behind the gate;
  `tst_reversoservice` verifies the POST path, the JSON fields and their string values, UTF-8
  encoding of spaces and non-ASCII text, every declared direction pair, the `auto` and unmapped-code
  rejection before any request, 5000-unit chunks with the 600 ms pause, the serialized queue and
  cancellation, the classified errors, the parsed result and the message/log hygiene; Linux is 39
  tests and Windows would be 34.
- `03-reverso-settings` is done: the Services page has a gate-aware `Reverso` group with the
  unofficial/no-key warning, the six-language limitation and the opt-in hint, no key or URL field;
  `tst_settingsdialog` covers it; all 14 `.ts` files are complete (311 → 313 messages) and
  `tst_translations` passes.
- `04-reverso-docs` is done: `doc/architecture.md` §3.1/§3.2/§4.5/§4.7.7/§4.8/§4.9/§4.14/§8/§9.2,
  `doc/free-endpoints.md` status/§4/§8/§9 and `AGENTS.md` (34 Windows/39 Linux) describe the
  implemented service; all relative links resolve.
- The overall Reverso criteria hold: the service is behind `translation/allowFreeServices`; a
  supported pair is sent as `<source>-<target>` with the JSON fields above; `auto` and an unmapped
  code return `UnsupportedLanguage` before any request, the `auto` message tells the user to choose
  an explicit source language and the settings hint states that Reverso cannot detect it; 5000-unit
  chunks are paced at 600 ms; 429/403/rate-limit bodies, 5xx, HTML/blocked responses,
  `success: false`, malformed bodies, timeouts and `abort()` all finish with the documented error and
  never show raw HTML; the text is never logged or put into a message; official Google/DeepL remain
  primary and the service is not enabled by default.
- Debug and Release build on Linux without warnings and the full suite passes; Windows Debug/Release
  and its 34 tests are recorded as not performed (no MSVC); the manual live smoke is recorded as
  passed or as not performed with the reason; no push is made.
- `README.md` and `README.ru.md` list Reverso as an unofficial, niche fallback; this is the lead's
  final pass, not a child edit. `doc/handoff.md`, the root plan and `07-fallback-chain` are also
  lead-owned; the children do not edit them.

## Depends on

`01-free-web-infrastructure` (the opt-in gate and `QueuedWebService`). The four children are
sequential: `02-reverso-service` depends on `01-reverso-parser` for the parser and the language
branch; `03-reverso-settings` depends on `02-reverso-service` for the service id and the gate;
`04-reverso-docs` depends on both `02` and `03` for the final behaviour, the UI strings and the
counts. The node follows `05-bing-web` in the stage order but has no technical dependency on it;
`07-fallback-chain` depends on this node.

## Risks and what is needed from the user

- Reverso's language coverage is genuinely narrow: six languages, no automatic detection. Do not
  extend the whitelist from memory or from Reverso's marketing pages; add a code only with an
  observed direction and a fixture. The service is useful only for pairs among those six languages;
  the UI must say so, and the picker will honestly offer only those six languages.
- The protocol observation comes from the document and the upstream plugin, not from a live request
  made here. The observed success sample is
  `{"error":false,"success":true,"translation":"La banque est fermee.","directionFrom":"en","directionTo":"fr"}`
  and the provider failure sample is `{"error":true,"success":false,"message":"Too many requests"}`.
  If a live smoke shows different field names, a different required header or a different
  `User-Agent`, record it and revisit the service; do not silently guess.
- The document's §4 field list omits the upstream `error` flag. A response with `error: true` and
  `success: true` would otherwise look successful; treat either flag as an error. Keep the upstream
  models as the reference for this field.
- `success: false` with HTTP 200 is a normal error path. Include the service `message` only when it
  is short and plain; never render the raw response, an HTML page or a message that contains
  markup. A provider message could echo the source text in theory, so keep the short-message rule
  and the fixed fallback.
- The document gives no Reverso-specific chunk limit. The 5000 UTF-16-code-unit chunk and the 600 ms
  pause are conservative assumptions; keep them in named constants and record any measured limit
  from a live smoke.
- The endpoint may reject a datacenter IP or require a browser origin. The automated gate is the
  local-server test; a live failure is recorded, not worked around with cookies or a captcha solver.
- Windows Debug/Release and its 34 tests are unavailable on this Linux machine; record them as not
  performed, not as passed. The live smoke is a manual step and may be recorded as not performed.
