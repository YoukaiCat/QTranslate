---
kind: task
status: todo
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

## Done when

- `MyMemoryService` and `MyMemoryParser` exist under `src/services/`, are listed in
  `qtranslate_core` and `tests/CMakeLists.txt`, and return the internal `mymemory` id with
  `requiresOptIn() == true`, `isConfigured() == true` and a 350 ms default pause; Google, DeepL
  (including `free-web`), the LLM service and their tests are unchanged.
- `tst_mymemoryparser` covers a normal response, a numeric and a string `responseStatus`, the
  optional `responseData.detectedLanguage`, `quotaFinished`, quota details, a non-200
  `responseStatus` with `responseDetails` (invalid pair and unknown details), an HTML body, malformed
  JSON, a missing `responseData.translatedText`, and the `errorForHttpStatus` mapping; each case
  produces the documented `TranslationError` and a fixed message without the text, body or URL.
- `tst_mymemoryservice` with a local `QTcpServer` verifies the `GET` method, the `/get` path, the
  `q`, `langpair` (with `|`) and `mt=1` fields, percent-encoding of spaces and non-ASCII text, the
  absence of an `Authorization` header, the `mymemory` service id, the optional detected language,
  and the parsed response body.
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
- No text, key or URL containing the text is logged; every user-facing error message and log line
  contains only a fixed description, the HTTP status and the service name.
- Debug and Release build without warnings on Linux; the full suite passes 31/31 (29 existing plus
  `tst_mymemoryparser` and `tst_mymemoryservice`); `tst_mymemoryservice` runs at least 10 times
  without a timing flake; Windows Debug/Release and its 26 tests are recorded as not performed (no
  MSVC).
- The settings group, translations, `doc/architecture.md`, `doc/free-endpoints.md` and the
  `AGENTS.md` counts are not touched here; `03-mymemory-settings-and-docs` owns them.

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
