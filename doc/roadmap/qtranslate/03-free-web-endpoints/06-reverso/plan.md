---
kind: task
status: todo
depends: [01-free-web-infrastructure]
---

# Reverso service

## Task

Add the unofficial Reverso endpoint as the last free service. It is a niche service with narrower
language coverage, so it is a fallback rather than a replacement for Google/DeepL; the stage requires
it to return clear errors, and a successful translation is a bonus verified by a manual smoke.

Use `doc/free-endpoints.md` §4 for the protocol, §8 for the rules, and §9 for the priority. The
service is `reverso` and implements `TranslationService` with `requiresOptIn() == true`. It sends
`POST https://cps.reverso.net/api2/TranslateText` with the JSON body `source`, `direction`,
`deviceId=0`, `uiLang=en`, `origin=chromeextension`, `accessToken=""`, and `appId=0`; the direction
is Reverso's own code pair (`<source>-<target>`), not the internal codes. Add a small mapping table
for the languages the endpoint actually supports and return `UnsupportedLanguage` for any other code
before a request; do not claim the full Google catalogue. A new pure parser reads `translation`,
`directionFrom`, `directionTo`, `success`, and `message`: `success == false` becomes
`ServiceUnavailable` with the short `message` (or `BadResponse` when the body is not the documented
JSON), never raw HTML; a missing `translation` with `success == true` is `BadResponse`.

The service uses `QueuedWebService` with a 600 ms pause and sends long text in 5000-character chunks
sequentially, restoring edge whitespace. Errors: HTTP 429/403 and a rate-limit body become
`RateLimited`; 5xx becomes `Network`; an HTML page or a blocked response becomes
`ServiceUnavailable`; a timeout and `abort()` stop the request. The JSON body contains the text, so
the error/log messages must not include the body, the `direction` is safe to log, and the text must
never be logged.

The settings entry is a Reverso group with the free/unofficial warning and the opt-in hint; no keys
and no per-service URL. Tests use fixtures for the JSON responses and a local `QTcpServer`; a live
smoke is manual.

## Done when

- `ReversoService` and `ReversoParser` exist under `src/services/`, are listed in `CMakeLists.txt`,
  and return the internal `reverso` id with `requiresOptIn() == true`; the service is not active by
  default.
- `tst_reversoparser` covers a successful response with `translation`/`directionFrom`/`directionTo`,
  `success == false` with a `message`, a successful response without `translation`, an HTML body,
  malformed JSON, and an empty translation; each case produces the documented error and a message
  without the text.
- `tst_reversoservice` with a local `QTcpServer` verifies the POST path, the JSON body fields
  (`source`, `direction`, `deviceId`, `uiLang`, `origin`, `accessToken`, `appId`), the direction
  mapping for the declared pairs, UTF-8 encoding of spaces and non-ASCII text, and the parsed result.
- An unsupported source/target language returns `UnsupportedLanguage` before any request; `auto`
  is handled with Reverso's documented auto direction if it exists, otherwise the task returns a
  clear `UnsupportedLanguage` and the UI tells the user to choose a source language.
- Long text is split into 5000-character chunks at paragraph/sentence boundaries and sent
  sequentially with the 600 ms pause (`setRateLimitMs(0)` in tests); the result restores edge
  whitespace and matches the concatenation of the chunk translations.
- HTTP 429/403 and a rate-limit body return `RateLimited`; 5xx returns `Network`; an HTML/blocked
  response returns `ServiceUnavailable`; `success == false` returns `ServiceUnavailable` with the
  service `message`; a timeout and `abort()` finish with `Timeout`/`Cancelled` and stop the request.
- Requests to the host are serialized through `QueuedWebService`; a second translation waits for the
  active one and for the pause; cancelling a queued request does not send it.
- The settings dialog has a Reverso group with the free/unofficial warning; the opt-in gate hides or
  refuses the service as described in `01-free-web-infrastructure`; `tst_settingsdialog` covers the
  group.
- `doc/architecture.md` §4.5/§4.9 and `doc/free-endpoints.md` §4/§8/§9 are updated; both READMEs list
  Reverso as an unofficial, niche fallback; the manual live smoke is recorded as passed or as not
  performed with the reason (a narrower language set or a block).
- Debug and Release build without warnings; the full suite passes with the new tests; Windows is
  recorded as not performed when MSVC is unavailable.

## Risks

- Reverso's direction codes and language coverage are narrower and may be undocumented. Derive the
  mapping from the observed protocol, declare only the pairs with a fixture, and fail with
  `UnsupportedLanguage` for the rest; do not guess a code that silently returns the source text.
- `success == false` with HTTP 200 is a normal error path. Show the service `message` only when it is
  short and not HTML; otherwise use the generic `ServiceUnavailable` message. Never render the raw
  response.
- The doc gives no Reverso-specific chunk limit. The 5000-character chunk and 600 ms pause are
  conservative assumptions; keep them in named constants and record any measured limit from a live
  smoke.
- The endpoint may reject a datacenter IP or require a browser origin. The automated gate is the
  local-server test; a live failure is recorded, not worked around with cookies or a captcha solver.
