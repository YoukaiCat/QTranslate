---
kind: task
status: todo
depends: [01-reverso-parser]
---

# Reverso service

## Task

Add `ReversoService` on top of `01-reverso-parser`, register it in `AppController` behind the free-web
gate, and cover the JSON `POST` protocol with a local `QTcpServer` test. This task implements the
network behaviour, the 600 ms pause, the 5000 UTF-16-code-unit chunks and the error classification.
It does not add the settings group, translations or documentation; those are
`03-reverso-settings` and `04-reverso-docs`.

The service is `reverso`, derives from `QueuedWebService`, sets `setRateLimitMs(600)` in the
constructor, returns `isConfigured() == true` and `requiresOptIn() == true`, and takes its source and
target lists from `Languages::supported("reverso", ...)` without `auto`. The endpoint is fixed:
`static QUrl defaultEndpoint()` is `https://cps.reverso.net/api2/TranslateText`.
`setEndpoint(const QUrl &)` and `endpoint()` are the test seam, like the other free services; an
empty or invalid URL falls back to the default. `applySettings(const AppSettings &)` has no settings
to read; no `AppSettings` field, INI key or per-service URL is added.

The upstream plugin sends a browser Chrome `User-Agent`; this service sets a fixed browser
`User-Agent` on its requests only (`static QString browserUserAgent()`), after
`NetworkManager::makeRequest()`, as protocol emulation. It is not a claim that the application is a
browser, and `AppInfo::userAgent()` plus every other service are unchanged.

`translate()` returns `EmptyText` for empty/whitespace-only text and `UnsupportedLanguage` for
`auto` before any request, with the message `Reverso needs an explicit source language; choose one
instead of Auto-detect`. It maps an explicit source and the target with
`Languages::toServiceCode("reverso", ...)`, returns `UnsupportedLanguage` with the code when either
is unmapped, and builds the direction as `<source>-<target>`. The text is split with
`TextChunker::split(text, 5000)`, every part's edge whitespace is stripped and restored, an
all-whitespace part is never sent, and the parts are sent sequentially inside one reply.

The nested `Reply` behaves like `YandexWebService::Reply`/`BingWebService::Reply`:

1. `start()` schedules `send()` through `Qt::QueuedConnection`; the first chunk is posted with
   `Content-Type: application/json`, `Accept: application/json`, the fixed browser `User-Agent` on
   this request only, `SameOriginRedirectPolicy` and no cookie manipulation.
   `noteRequestSent()` is called just before `nam()->post()`.
2. The body is a `QJsonObject` with the fields `source`, `direction`, `deviceId` (`"0"`), `uiLang`
   (`"en"`), `origin` (`"chromeextension"`), `accessToken` (`""`) and `appId` (`"0"`), serialized
   with `QJsonDocument(object).toJson(QJsonDocument::Compact)`. `QJsonObject` does not preserve
   insertion order, so the test parses the body as JSON and checks the values, not the byte order.
   The serializer writes UTF-8, so spaces and non-ASCII text stay readable in the JSON string.
3. A 2xx body goes to `ReversoParser::parseResponse()`: append the translation, keep the first
   non-empty detected language, and either schedule the next chunk after `rateLimitMs()` or finish.
   A failed chunk fails the whole reply without a partial result; a translation count that does not
   match the sent parts is `BadResponse`. The joined result sets `serviceId = "reverso"` and
   restores the edge whitespace of every part, including unsent whitespace-only parts.
4. A non-2xx status classifies the body first (`ReversoParser::parseErrorBody()`) and then falls
   back to `errorForHttpStatus()`; a timeout or `abort()` finishes with `Timeout`/`Cancelled` and
   stops the active request; other network errors use `NetworkManager::describeNetworkError(request,
   false)` or the fixed `Network error`.
5. The JSON body, the text and the URL are never logged or put into a message; the log has only the
   part count, internal language codes, the safe `direction`, the HTTP status and elapsed time
   (`Q_LOGGING_CATEGORY(lcReverso, "qtranslate.service.reverso", QtWarningMsg)`). The short provider
   `message` from a `success: false` body may reach the user through the parser's validated result;
   `fail()` logs the error kind and a fixed text, not `errorMessage`, so the provider message is not
   logged. The raw body and an HTML page never reach the user or the log.

Register the service in `AppController` after Bing: `class ReversoService;` and
`ReversoService *m_reverso = nullptr;` in the header, construct `new ReversoService(m_network)`,
call its `applySettings(settings)`, `registerService(m_reverso)` after the Bing service, and forward
`applySettings(newSettings)` in the settings-update path. The gate, picker filtering and
active-service switch are already generic; do not add a cache reset in `AppController`, because this
service has no cache.

Add `tests/tst_reversoservice.cpp` by copying the `FakeHttpServer` from
`tests/tst_mozhiwebservice.cpp`. `init()` creates the server, `NetworkManager` and `ReversoService`,
points `setEndpoint()` at `http://127.0.0.1:<port>/api2/TranslateText` and calls
`setRateLimitMs(0)`; `cleanup()` resets all three. The test covers the properties and defaults
(`reverso`, display name `Reverso`, configured, opt-in, equal non-empty source/target lists without
`auto`, default endpoint, `rateLimitMs() == 600`), the POST path, the browser `User-Agent` on this
request only, the JSON fields and their string values, the UTF-8 encoding of spaces and non-ASCII
text, all 36 declared direction pairs
(`en-fr`, `fr-en`, `de-es`, `ru-it`, and the loop over the six sources and six targets), the
`auto` rejection and the unmapped-code rejection with no request, the empty text, edge whitespace,
5000-unit chunks, chunk pacing (`setRateLimitMs(100)`), queue serialization, cancelling a queued
reply, aborting an active one, a failed middle chunk (the whole reply fails, no partial translation,
no extra request), the HTML/rate-limit/provider-error/malformed/timeout cases, a cross-origin
redirect refused, and the message/log hygiene with a `TOPSECRET` text and a provider `message`
sentinel that must reach `errorMessage` only when it is a validated short message and must never
appear in the captured log. Register the target immediately after `tst_reversoparser` in the
platform-neutral block of `tests/CMakeLists.txt`; keep it platform-neutral. CTest never contacts the
real endpoint.

Expected counts: this child adds the second target, so Linux becomes 39 and Windows would be 34 (31
shared plus 3 Windows-only on Windows; 31 shared plus 8 Linux-only on Linux). Windows is derived
from `tests/CMakeLists.txt`, not run here.

## Done when

- `ReversoService` exists under `src/services/`, is listed in `qtranslate_core`, derives from
  `QueuedWebService`, returns the internal `reverso` id and the display name `Reverso`,
  `isConfigured() == true` and `requiresOptIn() == true`, and returns the six-language
  source/target lists without `auto`.
- The service is registered in `AppController` after Bing and is hidden/refused while
  `translation/allowFreeServices` is off; no `AppSettings`/INI field or per-service URL is added.
- `translate()` rejects `auto` and every unmapped code with `UnsupportedLanguage` before any
  request, builds `direction` as `<source>-<target>` for the 36 declared pairs, splits at 5000
  UTF-16 code units, restores edge whitespace and never sends an all-whitespace part.
- `tst_reversoservice` with a local `QTcpServer` verifies the POST path, the browser `User-Agent`
  on this request only, the seven JSON fields and their string values, the UTF-8 encoding of spaces
  and non-ASCII text, all declared direction pairs, the rejections with zero requests, the 5000-unit
  chunks and the 600 ms pause, queue serialization and cancellation, the failed-middle-chunk rule,
  the classified errors, the parsed result, the cross-origin redirect refusal, and the message/log
  hygiene.
- Debug and Release build on Linux without warnings; `ctest -N` shows 39 Linux tests and would show
  34 on Windows; the full suite passes 39/39; Windows Debug/Release and its 34 tests are recorded as
  not performed (no MSVC).
- No settings UI, `.ts`, `doc/`, `AGENTS.md`, README, `doc/handoff.md`, root-plan or
  `07-fallback-chain` file is changed; commit and push are the coordinator/lead's.
