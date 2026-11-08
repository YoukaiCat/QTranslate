---
kind: task
status: done
detailed: true
depends: [01-split-utf8-and-language-codes]
---

# MyMemory parser and service

## Task

Add the MyMemory parser and service, register it in the app, and cover it with fixtures and the local
`QTcpServer` tests. This task does not add the settings group, translations or documentation; those
are `03-mymemory-settings-and-docs`. It builds on `01-split-utf8-and-language-codes` for
`TextChunker::splitUtf8` and the `mymemory` language codes.

The service is `mymemory`, derives from `QueuedWebService`, sets a 350 ms pause, returns
`requiresOptIn() == true` and `isConfigured() == true`, and sends
`GET https://api.mymemory.translated.net/get?q=...&langpair=<src>|<dst>&mt=1`. It rejects `auto` with
`UnsupportedLanguage` before any request, splits with `splitUtf8(..., 500)`, sends chunks sequentially
inside one reply, restores the edge whitespace, and classifies HTTP/body errors as described below.
No `de` email, no `AppSettings` field, no key, no URL setting, no `LanguageInfo` column. All error and
log messages must avoid the text and the URL; network errors use
`NetworkManager::describeNetworkError(request, false)`, never `QNetworkReply::errorString()`.

**Parser.** `src/services/mymemoryparser.h/.cpp` exposes
`TranslationResult parseResponse(const QByteArray &body)` and
`TranslationError errorForHttpStatus(int status)` and is pure. It reads
`responseData.translatedText`, `responseStatus` (number or numeric string; missing means 200),
`responseDetails`, `quotaFinished` (bool or `"true"`/`"false"` string) and the optional
`responseData.detectedLanguage` (converted with `Languages::fromServiceCode("mymemory", ...)`).
An HTML body (trimmed body starts with `<`) → `ServiceUnavailable`; malformed JSON/non-object →
`BadResponse`; `quotaFinished == true` or quota/free-translations/daily-limit details →
`QuotaExceeded`; query-length details → `TooLong`; invalid-language/language-pair/distinct-languages
details → `UnsupportedLanguage`; too-many-requests/rate-limit details → `RateLimited`; a non-200
body status with unknown details or a missing/empty `translatedText` → `BadResponse`.
`errorForHttpStatus`: 403/429 → `RateLimited`, 413/414 → `TooLong`, 5xx → `Network`, other non-2xx →
`BadResponse`. Messages are fixed English strings and never echo `responseDetails`, the body, the text
or a URL. Fixtures go under `tests/fixtures/mymemory/`: a normal response, the optional detected
language, a string `responseStatus`, `quotaFinished`, quota details, a body-level invalid pair, an
unknown body status, HTML, malformed JSON and a missing `translatedText`; `tests/tst_mymemoryparser`
covers them and the status mapping.

**Service.** `src/services/mymemoryservice.h/.cpp` derives from `QueuedWebService`; its id is
`mymemory`, its display name is `MyMemory`, `isConfigured()` is true, `requiresOptIn()` is true, and
`sourceLanguages()`/`targetLanguages()` come from `Languages::supported("mymemory", ...)`.
`applySettings(const AppSettings &)` is a no-op with an unnamed parameter. The constructor stores
`NetworkManager`, sets `m_endpoint` and calls `setRateLimitMs(350)`. `translate()` rejects `auto` with
`UnsupportedLanguage` and "MyMemory needs an explicit source language; choose one instead of
Auto-detect", maps both codes with `Languages::toServiceCode`, returns `UnsupportedLanguage` for an
empty code, splits with `splitUtf8(..., 500)`, strips and restores the edge whitespace of every part,
treats all-whitespace parts as unsent, returns `EmptyText` for empty or whitespace-only text, and
enqueues a reply. The reply sends one chunk per `GET` (`q`, `langpair`, `mt=1`, `Accept:
application/json`), calls `noteRequestSent()` before every request, schedules the next chunk after
`rateLimitMs()` and holds the queue slot until the last chunk; on a chunk error it fails the whole
reply without a partial result. `abortImpl()` stops the request. The error mapping is the one above;
a timeout is `Timeout` with "MyMemory did not respond within %1 s", a cancellation is `Cancelled`.
Log only the part count, source/target codes, HTTP status, elapsed time and fixed messages.

**Service tests.** `tests/tst_mymemoryservice.cpp` uses the local `FakeHttpServer` pattern from
`tests/tst_deeplwebservice.cpp` and covers the properties and default 350 ms pause; `GET /get` with
`q`, `langpair=en|ru`, `mt=1` and no `Authorization`; percent-encoded spaces and Cyrillic/emoji;
`ru|pt-BR`, `zh-Hans`→`zh-CN`, `nb`→`no`, `fil`→`tl`; `serviceId` and the optional detected language;
`auto` and unknown targets rejected without a request; long Cyrillic/emoji text split into several
requests, every decoded `q` ≤ 500 UTF-8 bytes, the joined result equals the original, and the
request count matches `splitUtf8`; chunk pacing with `setRateLimitMs(100)`; two translations
serialized with `setRateLimitMs(200)`; aborting a queued reply sends nothing; aborting the active
reply finishes `Cancelled`; HTTP 429/403, 503, 414 and 400; a 200 HTML body; `quotaFinished`; a
rate-limit body; a timeout with `setTimeoutMs(300)` and `respond=false`. Every error message must
contain neither the text nor the query/URL. Use `QTRY_*` and lower-bound timing assertions.

**App and build.** `src/app/appcontroller.h/.cpp` forward declares `MyMemoryService`, adds
`m_mymemory`, constructs it in `createTranslation()`, calls `applySettings(settings)`, registers it
after the LLM service and calls `m_mymemory->applySettings(newSettings)` in `applySettings()`. Add a
`gatedRealServiceIsRefused` case to `tests/tst_translationmanager.cpp` with the real service, the
gate off, `ServiceUnavailable` and no request. Add `mymemoryparser.*`/`mymemoryservice.*` to
`qtranslate_core` in `CMakeLists.txt` and `tst_mymemoryparser`/`tst_mymemoryservice` to
`tests/CMakeLists.txt` in alphabetical order after `tst_languages`; both are platform-neutral, so the
counts become 31 Linux (29 + 2) and 26 Windows (24 + 2). Run the targeted tests, the full suite in
Debug and Release without warnings, and `tst_mymemoryservice` at least 10 times; record the counts in
the outcome. `AGENTS.md` and the docs are updated in `03-mymemory-settings-and-docs`.

## Steps

1. **Add the pure parser.** Create `src/services/mymemoryparser.h/.cpp` with
   `TranslationResult parseResponse(const QByteArray &body)` and
   `TranslationError errorForHttpStatus(int status)`. Include `core/translationtypes.h`,
   `core/languages.h`, `QJsonDocument`, `QJsonObject`, `QJsonValue` and `QJsonParseError`. In
   `parseResponse`, trim the body only for the HTML check: a body that starts with `<` returns
   `ServiceUnavailable` with "the endpoint returned an HTML page"; malformed JSON or a non-object
   root returns `BadResponse`; `responseStatus` may be a number or a numeric string and defaults to
   200 when absent, any other type is `BadResponse`; `quotaFinished` may be a bool or a
   `"true"`/`"false"` string and `true` returns `QuotaExceeded`. Classify `responseDetails`
   case-insensitively before a generic non-200: query-length text (`QUERY LENGTH`/`MAX ALLOWED
   QUERY`/`QUERY TOO LONG`) → `TooLong`; invalid-language/language-pair/distinct-languages text →
   `UnsupportedLanguage`; quota/free-translations/daily-limit text → `QuotaExceeded`;
   too-many-requests/rate-limit text → `RateLimited`. Read the non-empty string
   `responseData.translatedText`; if it is missing/empty after the details classification return
   `BadResponse`; a non-200 body status with unknown details is `BadResponse`. Read the optional
   `responseData.detectedLanguage` and convert it with
   `Languages::fromServiceCode("mymemory", code, Direction::Source)`; leave it empty when absent or
   unknown. Leave `serviceId` empty. Every message is a fixed English literal and contains no
   `responseDetails`, body, text or URL. `errorForHttpStatus` maps 403/429 → `RateLimited`,
   413/414 → `TooLong`, 5xx → `Network`, other non-2xx → `BadResponse`. Add
   `src/services/mymemoryparser.cpp/.h` to `qtranslate_core` in `CMakeLists.txt` in the services
   block after `googletranslateservice`.

2. **Add parser fixtures and `tst_mymemoryparser`.** Create `tests/fixtures/mymemory/` with
   `normal.json` (numeric 200, "Hallo Welt!"), `detected.json` (`detectedLanguage: "en"`),
   `status_string.json` (`responseStatus: "200"`), `quota_finished.json` (bool true),
   `quota_finished_string.json` (`"true"`), `quota_details.json`
   (`responseDetails: "MYMEMORY WARNING: YOU USED ALL AVAILABLE FREE TRANSLATIONS FOR TODAY"`),
   `status_403_details.json` (`responseStatus: 403`, `responseDetails: "INVALID LANGUAGE PAIR"`, a
   `translatedText: "SECRET"` to prove it is not echoed), `status_500_unknown.json`
   (`responseStatus: 500`, unknown details, `"SECRET"`), `html.html` (`<!DOCTYPE html>...blocked`),
   `malformed.json` and `missing_translated_text.json`. Use the `fixture()` helper and
   `QTRANSLATE_FIXTURES_DIR` pattern from `tests/tst_deeplparser.cpp`; add
   `Q_DECLARE_METATYPE(TranslationError)`. Cover `normalResponse` (text, empty `serviceId`),
   `detectedLanguage`, `statusAsString`, `quotaFinishedBool`, `quotaFinishedString`,
   `quotaDetails`, `invalidPair` (`UnsupportedLanguage`), `unknownNon200` (`BadResponse`),
   `htmlBody` (`ServiceUnavailable`), `malformedJson` and `missingTranslatedText` (`BadResponse`),
   and the `errorForHttpStatus` rows (403/429, 413/414, 500/503, 400). Add a helper that asserts
   each error message contains no `"SECRET"`, no `"blocked"`, no `<` and no URL. Add
   `qtranslate_add_test(tst_mymemoryparser)` to `tests/CMakeLists.txt` after `tst_languages`;
   configure, build and run the target.

3. **Add the service and register its sources.** Create `src/services/mymemoryservice.h/.cpp`.
   Derive from `QueuedWebService`; `id()` returns `mymemory`, `displayName()` returns `MyMemory`,
   `isConfigured()` and `requiresOptIn()` return true, `sourceLanguages()`/`targetLanguages()`
   return `Languages::supported("mymemory", ...)` (no auto), and `applySettings(const AppSettings &)`
   is an empty no-op with an unnamed parameter. The constructor stores `NetworkManager`, sets
   `m_endpoint = defaultEndpoint()` and calls `setRateLimitMs(350)`; expose `setEndpoint()` for tests
   and a static `defaultEndpoint()` for `https://api.mymemory.translated.net/get`. Add a file-local
   `Part {leading, text, trailing, sent}` and `splitPartsUtf8()` using
   `TextChunker::splitUtf8(text, 500)`, trimming/restoring the edges and marking all-whitespace parts
   unsent; do not modify `DeepLWebService`. In `translate()`, reject `auto` first with
   `UnsupportedLanguage` and "MyMemory needs an explicit source language; choose one instead of
   Auto-detect"; return `EmptyText` for empty/whitespace-only text; map both codes with
   `Languages::toServiceCode`; an empty code → `UnsupportedLanguage` with
   `language "%1" is not supported`; enqueue a `Reply`.

   `Reply` derives from `QueuedWebService::Reply`, holds the network/service pointers, endpoint,
   source/target, parts, translations and the first detected code. `start()` schedules `send()` with
   `Qt::QueuedConnection`. `send()` takes the next unsent non-empty part, builds `QUrl`/`QUrlQuery`
   (`q`, `langpair` = source + `|` + target, `mt=1`), sets `Accept: application/json` and no
   `Authorization`, calls `service->noteRequestSent()` immediately before `nam()->get()`, marks the
   part sent and starts a request timer. `onFinished()` reads/deletes the reply, returns when already
   finished, maps `OperationCanceledError`/`TimeoutError` to `Timeout` with "MyMemory did not respond
   within %1 s", non-2xx via `MyMemoryParser::errorForHttpStatus` with "HTTP %1", other network
   errors via `m_network->describeNetworkError(request, false)` or a literal "Network error" when the
   manager is gone (never `errorString()`), then parses
   with `MyMemoryParser::parseResponse`; on success append the text, keep the first non-empty
   detected code, and either schedule the next part after `service->rateLimitMs()` or finish with
   `serviceId`, the concatenated parts and the restored edge whitespace. On any error fail the whole
   reply (no partial result); if the translation count does not match the sent parts, fail with
   `BadResponse`. `abortImpl()` disconnects and aborts the active request; pending chunk timers check
   `isFinished()`. Use `Q_LOGGING_CATEGORY(lcMyMemory, "qtranslate.service.mymemory", QtWarningMsg)`
   and log only part count, source/target codes, HTTP status, elapsed time and fixed messages; never
   the URL, query, text, body or `errorString()`. Add the service files to `qtranslate_core` after
   the parser.

4. **Add `tst_mymemoryservice`.** Copy the `FakeHttpServer` from
   `tests/tst_deeplwebservice.cpp` (it already records `elapsedMs`) and add a decoded-`q` helper or
   a `text` field; keep `respond=false` for the timeout/abort cases. `init()` creates the server,
   `NetworkManager` and `MyMemoryService`, sets the endpoint to
   `http://127.0.0.1:<port>/get` and `setRateLimitMs(0)`; `cleanup()` resets all three. Add
   `Q_DECLARE_METATYPE(TranslationError)` and a `translationResponse(text, detected)` helper.
   Cover: `properties` (id, displayName, isConfigured, requiresOptIn, language lists, no auto);
   `defaultPause` on a fresh service (350 ms); `sendsGetRequest` (GET `/get`, decoded `q`,
   `langpair=en|ru`, `mt=1`, no `Authorization`, `Accept`, `serviceId`, result text);
   `encodesQuery` (spaces and Cyrillic/emoji: raw query has no literal space and contains UTF-8
   percent escapes, decoded `q` equals the input); `mapsLanguageCodes` data rows for
   `ru|pt-BR`, `zh-Hans`→`zh-CN`, `zh-Hant`→`zh-TW`, `nb`→`no`, `fil`→`tl`, `he`→`he`;
   `detectedLanguage`; `autoSourceIsRejected` and `unknownTargetIsRejected` (no request);
   `emptyTextIsEmptyError` (empty and whitespace-only, no request);
   `longTextIsSplitIntoByteChunks` (a text without whitespace-only chunks; several requests, every
   decoded `q` ≤ 500 bytes, joined result equals the input, request count equals
   `TextChunker::splitUtf8(text, 500).size()`);
   `pacesChunks` with `setRateLimitMs(100)` (each gap ≥ 90 ms); `serializesRequests` with
   `setRateLimitMs(200)` (second gap ≥ 180 ms); `cancelsQueuedRequest` (abort the second reply, only
   one request); `abortActive` (wait for the first request, abort, `Cancelled`); `httpErrors` data
   rows 429/403 → `RateLimited`, 503 → `Network`, 414 → `TooLong`, 400 → `BadResponse`;
   `htmlBody` → `ServiceUnavailable` without the body; `quotaFinished` → `QuotaExceeded`;
   `rateLimitBody` → `RateLimited`; `timeout` with `setTimeoutMs(300)` and `respond=false`;
   `connectionRefusedIsNetwork` after closing the fake server; `errorMessageOmitsTextAndUrl` (a
   503/HTML case with a `TOPSECRET` query). Use `QTRY_*`/`QSignalSpy` and lower-bound assertions.
   Add `qtranslate_add_test(tst_mymemoryservice)` after `tst_mymemoryparser`; both are
   platform-neutral and add no Widgets.

5. **Register the service in `AppController`.** In `src/app/appcontroller.h` add
   `class MyMemoryService;` and `MyMemoryService *m_mymemory = nullptr;` after `m_llm`. In
   `src/app/appcontroller.cpp` include `services/mymemoryservice.h`, construct
   `m_mymemory = new MyMemoryService(m_network);`, call `m_mymemory->applySettings(settings);` and
   `m_translations->registerService(m_mymemory);` after the LLM service; in `applySettings()` call
   `if (m_mymemory) m_mymemory->applySettings(newSettings);`. Do not add an `AppSettings` field or
   an INI key; the existing gate/picker logic in `TranslationManager`/`AppController` is unchanged.
   Add `gatedRealServiceIsRefused` to `tests/tst_translationmanager.cpp` with
   `NetworkManager network;`, `TranslationManager manager;`, `new MyMemoryService(&network)`,
   active `mymemory`, `setFreeServicesAllowed(false)`, a translated request and an assertion of
   `ServiceUnavailable`, `serviceId == "mymemory"` and the gate message; no request is made. Include
   `net/networkmanager.h` and `services/mymemoryservice.h`. Run `tst_translationmanager`.

6. **Build, count and verify.** Configure Linux Debug and Release (for example
   `cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release`) and build with
   `-Wall -Wextra`; run `tst_mymemoryparser`, `tst_mymemoryservice`, `tst_textchunker`,
   `tst_languages`, `tst_translationmanager`, `tst_deeplservice`, `tst_deeplwebservice` and
   `tst_queuedwebservice`, then the full suite with
   `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`. `ctest -N` must show 31
   Linux tests (29 + `tst_mymemoryparser` + `tst_mymemoryservice`); Windows would be 26 (24 + 2).
   Run `tst_mymemoryservice` at least 10 times for timing flakes. Record 31/26 in the outcome; do
   not touch Settings UI, `.ts`, `doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md`,
   README, `doc/handoff.md` or the root plan. Windows/MSVC is unavailable: record the Windows
   Debug/Release build and its 26 tests as not performed. No live smoke and no real network in
   CTest; leave commit and push to the coordinator/lead.

## Done when

- `MyMemoryService` and `MyMemoryParser` exist under `src/services/`, are listed in
  `qtranslate_core` and `tests/CMakeLists.txt`, and return the internal `mymemory` id with
  `requiresOptIn() == true`, `isConfigured() == true` and a 350 ms default pause on a fresh service;
  Google, DeepL (including `free-web`), the LLM service and their tests are unchanged.
- `tst_mymemoryparser` covers a normal response, a numeric and a string `responseStatus` (missing
  means 200), `quotaFinished` in bool and string form, the optional
  `responseData.detectedLanguage`, quota details, a non-200 `responseStatus` with `responseDetails`
  (invalid pair and unknown details), an HTML body, malformed JSON, a missing
  `responseData.translatedText`, and the `errorForHttpStatus` mapping; each case produces the
  documented `TranslationError` and a fixed message without the text, body or URL.
- `tst_mymemoryservice` with a local `QTcpServer` verifies the `GET` method, the `/get` path, the
  `q`, `langpair` (with `|`) and `mt=1` fields, percent-encoding of spaces and non-ASCII text, the
  absence of an `Authorization` header, the `mymemory` service id, the optional detected language,
  the parsed response body, the fresh-service 350 ms pause, `EmptyText` for empty/whitespace-only
  text, and a closed-port connection as `Network`.
- A source language of `auto` returns `UnsupportedLanguage` with a clear "choose a source language"
  message before any request; an unknown target language is rejected the same way before any request;
  a supported pair is sent with the exact MyMemory codes (`zh-Hans`→`zh-CN`, `zh-Hant`→`zh-TW`,
  `nb`→`no`, `fil`→`tl`, `pt-BR`→`pt-BR`, `he`→`he`).
- Long text is split into chunks of at most 500 UTF-8 bytes at word/sentence boundaries without
  breaking a UTF-8 sequence, a surrogate pair or `\r\n`; chunks are sent sequentially inside one
  reply, requests are at least `rateLimitMs()` apart, the result restores the edge whitespace and
  equals the concatenation of the chunk translations.
- HTTP 429, HTTP 403 and a rate-limit body return `RateLimited`; `quotaFinished` and quota text
  return `QuotaExceeded`; HTTP 413/414 and query-length text return `TooLong`; HTTP 5xx returns
  `Network`; an HTML/blocked response returns `ServiceUnavailable` without raw HTML; malformed JSON,
  a missing `translatedText` and an unknown non-200 status return `BadResponse`; a timeout and
  `abort()` finish with `Timeout`/`Cancelled` and stop the request.
- Requests to the host are serialized through `QueuedWebService`; a second translation waits for the
  active one and for the rate pause; cancelling a queued request does not send it; a failed chunk
  fails the whole reply without a partial translation.
- `AppController` creates and registers the service after the LLM service and calls its no-op
  `applySettings()`; no `AppSettings` field or INI key is added; `tst_translationmanager` proves the
  real service is refused with `ServiceUnavailable` while the gate is off.
- No text, key or URL containing the text is logged; `Q_LOGGING_CATEGORY(lcMyMemory,
  "qtranslate.service.mymemory", QtWarningMsg)` and every user-facing error message/log line contains
  only a fixed description, the HTTP status and the service name; `QNetworkReply::errorString()` is
  never used.
- Debug and Release build without warnings on Linux; `ctest -N` shows 31 Linux tests and would show
  26 on Windows; the full suite passes 31/31 (29 existing plus `tst_mymemoryparser` and
  `tst_mymemoryservice`); `tst_mymemoryservice` runs at least 10 times without a timing flake;
  Windows Debug/Release and its 26 tests are recorded as not performed (no MSVC).
- The settings group, translations, `doc/architecture.md`, `doc/free-endpoints.md`, the `AGENTS.md`
  counts, `README.md`, `README.ru.md`, `doc/handoff.md`, the root plan and
  `03-mymemory-settings-and-docs` are not touched here; the third child owns them.

## Risks

- The request URL contains the text. Never use `QNetworkReply::errorString()` or log the URL/query;
  call `NetworkManager::describeNetworkError(request, false)`, and keep parser messages fixed. A
  body or `responseDetails` must never be echoed, because it can echo the query.
- The 500-byte limit applies to the decoded `q`, but the URL is percent-encoded and expands; keep the
  limit on the UTF-8 bytes and test the actual query length with Cyrillic text. If MyMemory rejects a
  500-byte chunk with 414/TooLong or a query-length body, lower the limit in one constant and record
  the measured behaviour.
- MyMemory requires an explicit source language. Do not silently send `auto` or guess a language; the
  clear `UnsupportedLanguage` is better than a wrong translation, and the fallback chain can try a
  service that supports auto-detection.
- The `responseStatus`/`quotaFinished` types are inconsistent in the wild. Accept a numeric or string
  status and a bool or string flag; when the body is unclear, return `BadResponse` with a short
  message, never raw HTML. A 200 response with `quotaFinished` or quota text must still become
  `QuotaExceeded`.
- Long texts produce many sequential requests: 20,000 characters of Cyrillic is about 80 chunks at
  350 ms, roughly half a minute plus network time. Do not add a hidden cap or an overall timeout
  without a user decision; the fallback policy belongs to `07-fallback-chain`.
- Timing tests are the flake risk: use `setRateLimitMs(0)` for most cases and small values (100/200
  ms) only for the pacing/queue cases, with lower bounds and about 10% tolerance, no upper bounds,
  and `QTRY_*` timeouts.
- The `detectedLanguage` field is optional in the parser because MyMemory normally reports no
  detected language. If the real field is named differently, change the parser and its fixture, not
  the service.
- Windows Debug/Release and its 26 tests are not available on this Linux machine; record them as not
  performed, not as passed. No live smoke is part of this task; the final manual check belongs to
  `03-mymemory-settings-and-docs`.
- `03-mymemory-settings-and-docs` owns the settings group, translations and docs; do not add a
  temporary settings field or documentation here.

## Outcome

Done on 2026-11-08. The `mymemory` parser and service are registered in the app and covered by
fixtures and two platform-neutral tests; Google, DeepL (including `free-web`), the LLM service and
the shared `QueuedWebService`/`TextChunker`/`Languages` code are unchanged. No settings group,
translation, `.ts`, `doc/`, README or `AGENTS.md` change is part of this task.

- **Parser:** `src/services/mymemoryparser.h/.cpp`. `parseResponse()` returns `ServiceUnavailable`
  for an HTML body, `BadResponse` for malformed JSON, a non-object root, an invalid
  `responseStatus` type, an empty `translatedText` or a non-200 status with unknown details;
  `responseStatus` may be a number or a numeric string and missing means 200; `quotaFinished`
  accepts a bool or a case-insensitive `"true"`/`"false"` string. `responseDetails` is classified
  case-insensitively before the generic status check (query length → `TooLong`, invalid
  language/pair/distinct languages → `UnsupportedLanguage`, quota/free translations/daily limit →
  `QuotaExceeded`, too many requests/rate limit → `RateLimited`). The optional
  `responseData.detectedLanguage` goes through `Languages::fromServiceCode("mymemory", ...)`.
  `errorForHttpStatus()` maps 403/429 → `RateLimited`, 413/414 → `TooLong`, 5xx → `Network`, other
  non-2xx → `BadResponse`. Every message is a fixed English literal; `responseDetails`, the body,
  the text and URLs are never echoed.
- **Service:** `src/services/mymemoryservice.h/.cpp`. Id `mymemory`, display name `MyMemory`,
  `isConfigured()` and `requiresOptIn()` true, languages from `Languages::supported()`, a no-op
  `applySettings(const AppSettings &)` with an unnamed parameter, a 350 ms default pause set in the
  constructor, and `setEndpoint()`/`defaultEndpoint()` for
  `https://api.mymemory.translated.net/get`. `translate()` rejects `auto` first and empty or
  whitespace-only text as `EmptyText`, maps both codes with `Languages::toServiceCode`, returns
  `UnsupportedLanguage` for an empty code, splits with `TextChunker::splitUtf8(..., 500)` and
  keeps every part's edge whitespace out of the request while restoring it in the result;
  whitespace-only parts are never sent. `Reply` sends one `GET /get` per chunk (`q`,
  `langpair=<src>|<dst>`, `mt=1`, `Accept: application/json`, no `Authorization`), calls
  `noteRequestSent()` before every request, holds the queue slot until the last chunk and paces
  chunks with `rateLimitMs()`. A failed chunk fails the whole reply without a partial result;
  `abortImpl()` stops the active request; a timeout is `Timeout` with "MyMemory did not respond
  within %1 s" and a cancellation is `Cancelled`; network errors use
  `NetworkManager::describeNetworkError(request, false)`. `QNetworkReply::errorString()` is not used
  and the `lcMyMemory` logs contain only the part count, language codes, HTTP status, elapsed time
  and fixed messages.
- **Tests:** `tests/fixtures/mymemory/` holds the 11 planned fixtures, including a
  `status_403_details.json` with `translatedText: "SECRET"` and an `html.html` with `blocked` to
  prove neither is echoed. `tst_mymemoryparser` covers the normal/detected/string-status/quota/
  details/HTML/malformed/missing-text cases and the `errorForHttpStatus` rows (26 passing cases).
  `tst_mymemoryservice` uses a local `QTcpServer` and covers the properties, the fresh 350 ms pause,
  the `GET /get` fields, percent-encoding, language mapping, `auto`/unknown-language rejection
  without a request, `EmptyText`, byte chunks, chunk pacing and queue serialization, aborting a
  queued and an active reply, HTTP 403/429/503/414/400, an HTML body, `quotaFinished`, a
  rate-limit body, a timeout, a refused connection and message hygiene; the accepted review finding
  added `edgeWhitespaceIsTrimmed`, which sends `" \tHello\r\n "`, answers with a fixed `"X"` and
  checks the trimmed `q` and the restored `" \tX\r\n "`. No real network is used.
- **App and build:** `AppController` constructs `MyMemoryService` in `createTranslation()`, applies
  its no-op settings and registers it after the LLM service; `applySettings()` forwards to it. No
  `AppSettings` field or INI key is added. `tst_translationmanager` gains
  `gatedRealServiceIsRefused`, which proves the real service is refused with `ServiceUnavailable`
  and `serviceId == "mymemory"` while the gate is off. Both source pairs are in `qtranslate_core`
  and both tests are in `tests/CMakeLists.txt` after `tst_languages`; they are platform-neutral, so
  the counts are 31 Linux and 26 Windows.
- **Verification:** Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2) and `qtranslate-trixie:latest`
  (Qt 6.8.2, GCC 14.2), Debug and Release: zero `warning:` lines with `-Wall -Wextra`, the eight
  targeted tests 8/8, `ctest -N` 31, the full suite 31/31 in every configuration, and
  `tst_mymemoryservice` 10 repeats per configuration without a timing flake. The reviewer repeated
  the Release/Debug build and the full Release suite (31/31) and 5 repeats per configuration after
  the fix. The trixie runs used the existing `build/trixie-*` directories from the host uid; no
  root-owned files were left in the repository.
- **Review:** one accepted coverage finding, fixed by the new `edgeWhitespaceIsTrimmed` test; no code
  change was needed for it. No blockers and no other findings; no real problem was deferred.
- **Not performed and deferred by design:** Windows/MSVC Debug and Release and its 26 tests (no
  MSVC; both new tests are platform-neutral) and a live smoke against the real endpoint (CTest never
  contacts it). The settings group, the 14 `.ts` files, `doc/architecture.md`,
  `doc/free-endpoints.md`, the `AGENTS.md` counts, both READMEs, `doc/handoff.md`, the root plan and
  the final manual check belong to `03-mymemory-settings-and-docs`/the lead.
- **Noted in passing:** one trixie Release full-suite run produced an unrelated `tst_cli`
  (`forwarding`) flake; the next run was 31/31 and `tst_cli` alone passed 5/5.
