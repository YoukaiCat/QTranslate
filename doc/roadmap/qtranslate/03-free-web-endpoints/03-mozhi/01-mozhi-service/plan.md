---
kind: task
status: in-progress
detailed: true
---

# Mozhi parser and service

## Task

Add the `mozhi` service and its pure parser, register the service in the app, and cover it with
fixtures and the local `QTcpServer` tests. This task implements the protocol, the 5000-unit chunking,
the 500 ms pause, `auto`, the language mapping and the classified errors. It does not add `[mozhi]`
settings keys, the settings group, translations or documentation; those are `02-mozhi-settings` and
`03-mozhi-ui-and-docs`.

The service is `mozhi`, derives from `QueuedWebService`, sets a 500 ms pause, returns
`requiresOptIn() == true` and `isConfigured() == true`, and sends
`GET https://<instance>/api/translate?engine=<engine>&from=<src>&to=<dst>&text=...`. In this task the
instance and engine are the defaults (`https://mozhi.adminforge.de`, `google`) with
`setInstance()`/`setEngine()` seams for tests and for `02-mozhi-settings`; `applySettings()` stays a
no-op until then, so the intermediate state is a working service with the defaults. The language
mapping reuses the Google column; `auto` is sent as `auto` instead of being rejected before the
request, because the default Google engine accepts it and a rejected pair comes back as a recognised
language error and becomes `UnsupportedLanguage`. All error and log messages must avoid the text, the
full URL and the body; network errors use `NetworkManager::describeNetworkError(request, false)`,
never `QNetworkReply::errorString()`.

The protocol was checked on 2026-11-09 against the public instances
(`mozhi.pussthecat.org` for the response and code shapes, the default `mozhi.adminforge.de` for the
rate-limit body), and the parser/service must follow the observed shapes:

- `engine=google`, `from=en`, `to=ru`, `text=hi` → HTTP 200 with
  `{"engine":"google","detected":"","translated-text":"привет","source_language":"en",
  "target_language":"ru","target_transliteration":"",...}`. `translated-text` carries the
  translation; `detected` is filled for `from=auto` (`"en"`), while `source_language` then stays
  `"auto"`.
- The instance expects Google-style codes. `iw`, `jw`, `tl`, `no`, `zh-CN`, `zh-TW` and `pt` work;
  `he`, `jv`, `fil`, `nb`, `zh-Hans`, `zh-Hant` and `pt-BR` return "Source language code invalid".
- Errors are plain text, not JSON: `Target language code invalid` (HTTP 500), `from, to, engine,
  text are required query strings.` (HTTP 400), `Engine does not exist or has been disabled.`
  (HTTP 500), `instance has been rate limited` (HTTP 500), and the default instance answered a valid
  request with HTTP 500 and `request failed with status code: 429`. The body must be classified
  before the generic 5xx → `Network` rule.

## Steps

1. **Add the pure parser.** Create `src/services/mozhiparser.h/.cpp` with:

   ```cpp
   namespace MozhiParser {
   struct Response {
       TranslationResult result;       // translatedText/detectedSourceLang/error; serviceId empty
       QString sourceLanguage;         // raw source_language, may be "auto"
       QString targetTransliteration;  // raw target_transliteration
   };
   Response parseResponse(const QByteArray &body);
   TranslationResult parseErrorBody(const QByteArray &body);
   TranslationError errorForHttpStatus(int status);
   }
   ```

   Include `core/translationtypes.h`, `core/languages.h`, `QJsonDocument`, `QJsonObject`,
   `QJsonParseError`, `QJsonValue` and `QByteArray`. `parseErrorBody` recognises only error bodies and
   returns `TranslationError::None` when the body is not a recognised error, so the service can fall
   back to the HTTP status. Its order is: (a) a trimmed body that starts with `<` →
   `ServiceUnavailable` "the endpoint returned an HTML page"; (b) a JSON object with a non-null,
   non-empty `error` value (a string, or the `message` string of an object) → classify that text, and
   an unknown error text → `ServiceUnavailable` "the endpoint returned an error"; (c) a plain-text
   body (not starting with `{`/`[`) → classify case-insensitively: "rate limit", "too many
   requests", "status code: 429" or "status code: 403" → `RateLimited` "the endpoint reported a rate
   limit"; "language code invalid", "language pair", "invalid language" or "unsupported language" →
   `UnsupportedLanguage` "the language pair is not supported"; "engine does not exist", "engine has
   been disabled", or "engine" together with "disabled" → `ServiceUnavailable` "the selected engine
   is not available on this instance"; "required query strings" → `BadResponse` "the endpoint
   rejected the request parameters"; anything else → `None`; (d) an empty or malformed JSON body →
   `None`. Every message is a fixed English literal and never contains the body, the text, a URL or
   `responseDetails`-like content.

   `parseResponse` first returns the result of `parseErrorBody` when it has an error. Otherwise it
   parses the JSON: a malformed document, a non-object root or a missing/non-string/empty (after
   `trimmed()`) `translated-text` → `BadResponse` "no translated text in the response". Read the
   optional string fields `detected`, `source_language` and `target_transliteration`. Fill
   `result.detectedSourceLang` from `detected` with
   `Languages::fromServiceCode("mozhi", ..., Direction::Source)`; if that is empty, try
   `source_language` unless it is empty or `"auto"`; an unknown code stays empty. Copy
   `source_language` and `target_transliteration` to the `Response` fields, leave `serviceId` empty
   and leave `errorMessage` empty on success. `errorForHttpStatus` maps 403/429 → `RateLimited`,
   413/414 → `TooLong`, 5xx → `Network`, every other non-2xx → `BadResponse`. Add
   `src/services/mozhiparser.cpp/.h` to `qtranslate_core` in `CMakeLists.txt` (services block,
   alphabetically before `mymemory*`).

2. **Add the Mozhi language branch and extend `tst_languages`.** In `src/core/languages.cpp` add
   `constexpr QLatin1String kMozhi("mozhi");` next to `kMyMemory`, a `mozhiCodeOf(const
   LanguageInfo &info)` that returns `info.google` (the instance converts Google-style codes per
   engine) and the `mozhi` branch in `serviceCodeOf()`. The mapping must produce
   `zh-Hans`→`zh-CN`, `zh-Hant`→`zh-TW`, `he`→`iw`, `jv`→`jw`, `nb`→`no`, `fil`→`tl`,
   `pt-BR`→`pt`, `pt-PT`→`pt-PT`; `info.google == nullptr` returns `nullptr`. Do not add a
   `LanguageInfo` column, a synonym table or a settings field; `mozhiCodeOf()` is the single fix
   point if a later engine needs a different code. `fromServiceCode()`, `supported()` and
   `closest()` then reverse the same mapping for free.

   In `tests/tst_languages.cpp` add `const QString mozhi = "mozhi";`, forward rows
   (`en`, `ru`, `pt-BR`→`pt`, `pt-PT`→`pt-PT`, `zh-Hans`→`zh-CN`, `zh-Hant`→`zh-TW`,
   `he`→`iw`, `jv`→`jw`, `nb`→`no`, `fil`→`tl`, unknown → empty), reverse rows
   (`zh-CN`→`zh-Hans`, `zh-TW`→`zh-Hant`, `iw`→`he`, `jw`→`jv`, `no`→`nb`, `tl`→`fil`,
   `pt`→`pt-BR`, `pt-PT`→`pt-PT`), add `mozhi` to `roundTrip` (the existing `pt-PT`/`zh-Hant`
   source exception already covers the shared `pt` code) and assert in `supportedLists` that the
   Mozhi list equals the Google list in both directions, contains `ru`/`pt-BR`/`zh-Hans`, and
   excludes `auto`. Keep the Google/DeepL/MyMemory rows unchanged.

3. **Add parser fixtures and `tst_mozhiparser`.** Create `tests/fixtures/mozhi/` with
   `normal.json` (200, `translated-text: "Hallo Welt!"`, `detected: "en"`), `source_language.json`
   (`detected: ""`, `source_language: "en"`), `auto_source.json` (`source_language: "auto"`,
   `detected: "en"`), `auto_empty.json` (`source_language: "auto"`, `detected: ""`),
   `transliteration.json` (`target_transliteration: "privet"`),
   `empty_translation.json`, `missing_translation.json`, `malformed.json`,
   `html.html` (`<!DOCTYPE html>...blocked`), `json_error.json`
   (`{"error":"Engine does not exist or has been disabled."}`), `invalid_language.json`
   (`{"error":"Target language code invalid"}`), `rate_limit.txt`
   (`instance has been rate limited`), `status_code_429.txt`
   (`request failed with status code: 429`), `invalid_language.txt`
   (`Target language code invalid`), `unknown_engine.txt`
   (`Engine does not exist or has been disabled.`) and `required_fields.txt`
   (`from, to, engine, text are required query strings.`). Use the `fixture()` helper and the
   `QTRANSLATE_FIXTURES_DIR` pattern from `tests/tst_mymemoryparser.cpp` and
   `Q_DECLARE_METATYPE(TranslationError)`.

   Cover a normal response (text, empty `serviceId`), `detected`, the `source_language` fallback,
   `auto` with and without a detected language, `target_transliteration`, the documented fields, an
   empty and a missing `translated-text` (`BadResponse`), malformed JSON (`BadResponse`), an HTML
   body (`ServiceUnavailable`), a JSON `error` (`ServiceUnavailable`/`UnsupportedLanguage`), the five
   plain-text bodies (`RateLimited`, `RateLimited`, `UnsupportedLanguage`, `ServiceUnavailable`,
   `BadResponse`), and the `errorForHttpStatus` rows (403/429, 413/414, 500/503, 400). Add a helper
   that asserts every error message is non-empty and contains no `"SECRET"`, no `<` and no `://`.
   Add `qtranslate_add_test(tst_mozhiparser)` to `tests/CMakeLists.txt` in alphabetical order
   (`tst_mozhi*` before `tst_mymemory*`), configure, build and run the target.

4. **Add the service and register its sources.** Create `src/services/mozhiservice.h/.cpp`:

   - `class MozhiService : public QueuedWebService`; `id()` returns `mozhi`, `displayName()` returns
     `Mozhi`, `isConfigured()` and `requiresOptIn()` return true, `sourceLanguages()`/`targetLanguages()`
     return `Languages::supported("mozhi", ...)` (no auto), and `applySettings(const AppSettings &)`
     is an empty no-op with an unnamed parameter in this task.
   - File-local constants `kMaxChunkUnits = 5000` (UTF-16 code units; the doc gives no Mozhi limit)
     and `kDefaultRateLimitMs = 500` (a conservative assumption, documented in the header and later
     in §4.7.4), both with comments. The constructor stores `NetworkManager`, sets
     `m_instance = defaultInstance()`, `m_engine = defaultEngine()` and
     `setRateLimitMs(kDefaultRateLimitMs)`.
   - Expose `setInstance(const QUrl &)`, `QUrl instance() const`, `setEngine(const QString &)`,
     `QString engine() const`, `static QUrl defaultInstance()` and `static QString defaultEngine()`.
     `setInstance` falls back to `defaultInstance()` for an empty/invalid URL; `setEngine` trims and
     falls back to `defaultEngine()` for an empty string.
   - Build the endpoint in `translate()`: take `m_instance`, clear any query/fragment (the settings
     layer also rejects them), chop trailing `/` from the path, append `/api/translate`, and pass the
     result to the reply. A prefix such as `https://host/mozhi/` therefore becomes
     `https://host/mozhi/api/translate`; an origin becomes `/api/translate`.
   - `translate()`: return `EmptyText` for empty/whitespace-only text; map the source with
     `request.sourceLang == Languages::autoCode() ? "auto" : Languages::toServiceCode("mozhi", ...)`
     and the target with `toServiceCode`; an empty target, or an empty non-auto source, returns
     `UnsupportedLanguage` with `language "%1" is not supported` before any request; split the text
     with `TextChunker::split(text, 5000)`; strip and restore each part's edge whitespace, treat an
     all-whitespace part as unsent, and enqueue the reply.
   - The nested `Reply` derives from `QueuedWebService::Reply` and holds the network/service
     pointers, endpoint, engine, source/target, parts, translations and the first detected code.
     `start()` schedules `send()` through `Qt::QueuedConnection`. `send()` builds a `QUrl` from the
     endpoint and a `QUrlQuery` with `engine`, `from`, `to` and `text` (the trimmed part), sets
     `Accept: application/json`, sets
     `QNetworkRequest::RedirectPolicyAttribute = QNetworkRequest::SameOriginRedirectPolicy`, calls
     `service->noteRequestSent()` immediately before `nam()->get()`, marks the part sent and starts
     a request timer. Encode the query as in `MyMemoryService`: take
     `url.query(QUrl::FullyEncoded)`, replace a literal `+` with `%2B` so `C++`/`1+1` survive, and
     set it back with `QUrl::StrictMode`. Never log or echo the URL.
   - `onFinished()`: read the body only when `status != 0 || error == NoError`, delete the reply,
     return when already finished. `OperationCanceledError`/`TimeoutError` → `Timeout` with
     "Mozhi did not respond within %1 s". A non-2xx status → first `MozhiParser::parseErrorBody(body)`;
     if it reports an error, use it, otherwise `MozhiParser::errorForHttpStatus(status)` with
     `"HTTP %1"`. Other network errors → `Network` through `describeNetworkError(request, false)` or
     "Network error" when the manager is gone. Then `MozhiParser::parseResponse(body)`: on success
     append the text, keep the first non-empty `detectedSourceLang`, and either schedule the next
     part after `service->rateLimitMs()` or finish. On a chunk error fail the whole reply without a
     partial result; if the number of translations does not match the sent parts, fail with
     `BadResponse`. `abortImpl()` disconnects and aborts the active request; pending chunk timers
     check `isFinished()`.
   - The joined result sets `serviceId = "mozhi"` and restores the edge whitespace of every part,
     including unsent whitespace-only parts. Add a header comment that the 500 ms pause and 5000-unit
     chunk are assumptions from `doc/free-endpoints.md` §5/§8, not measured limits.
   - Use `Q_LOGGING_CATEGORY(lcMozhi, "qtranslate.service.mozhi", QtWarningMsg)` and log only the
     part count, source/target codes, engine, HTTP status, elapsed time and fixed messages; never the
     URL, query, text, body or `errorString()`. Add the service files to `qtranslate_core` after the
     parser.

5. **Add `tst_mozhiwebservice`.** Copy the `FakeHttpServer` from `tests/tst_mymemoryservice.cpp`
   (method, URL, headers, body, elapsed time, `respond=false`) and extend its `Response` with an
   optional `Location` header for the redirect case. `init()` creates the server, `NetworkManager` and
   `MozhiService`, calls `setInstance(QUrl("http://127.0.0.1:<port>"))`, `setEngine("google")` and
   `setRateLimitMs(0)`; `cleanup()` resets all three. Add `Q_DECLARE_METATYPE(TranslationError)`, a
   decoded-query helper and a `translationResponse(text, detected = {}, source = "en")` helper that
   writes `translated-text`, `detected`, `source_language`, `target_language` and
   `target_transliteration`. Cover:

   - `properties`: id `mozhi`, display name `Mozhi`, configured, opt-in, language lists non-empty and
     without `auto`.
   - `defaults`: a fresh service has `rateLimitMs() == 500`, `instance() ==
     QUrl("https://mozhi.adminforge.de")` and `engine() == "google"`.
   - `sendsGetRequest`: GET `/api/translate`, `engine=google`, `from=en`, `to=ru`, decoded `text`,
     `Accept: application/json`, no `Authorization`, `serviceId == "mozhi"`, the parsed result.
   - `usesInstancePathPrefix`: `setInstance("http://127.0.0.1:<port>/mozhi/")` requests
     `/mozhi/api/translate`; the origin requests `/api/translate`.
   - `encodesQuery`: spaces, Cyrillic/emoji, `C++` and `1+1`; the raw query has no literal space and
     the decoded `text` equals the input, including the plus signs.
   - `changesEngine`: `setEngine("deepl")` sends `engine=deepl`.
   - `mapsLanguageCodes_data`/`mapsLanguageCodes`: `zh-Hans`→`zh-CN`, `zh-Hant`→`zh-TW`, `he`→`iw`,
     `jv`→`jw`, `nb`→`no`, `fil`→`tl`, `pt-BR`→`pt`.
   - `autoSourceIsSent`: `from=auto`, no pre-request error, `detected: "en"` becomes
     `detectedSourceLang == "en"`; `source_language: "en"` with `detected: ""` also becomes `en`.
   - `autoRejectedByEndpoint`: a 500 `Source language code invalid` body →
     `UnsupportedLanguage` with a clear message and no retry.
   - `unknownLanguageIsRejected` and `emptyTextIsEmptyError`: no request is sent.
   - `edgeWhitespaceIsTrimmed`: `" \tHello\r\n "` is sent trimmed and the result restores it.
   - `longTextIsSplitIntoChunks`: a text with Cyrillic, paragraphs and emoji produces one request per
     non-whitespace-only part of `TextChunker::split(text, 5000)`, every decoded `text` is at most
     5000 UTF-16 code units, no `\r\n`/surrogate pair is split, the result equals the chunk
     translations with the original edge whitespace, and the request count matches the sent parts.
   - `pacesChunks` (`setRateLimitMs(100)`, gaps ≥ 90 ms) and `serializesRequests`
     (`setRateLimitMs(200)`, two translations, the second waits for the first and the pause).
   - `cancelsQueuedRequest` (only the first request is sent) and `abortActive` (first request seen,
     abort → `Cancelled`).
   - `httpErrors_data` rows: 429 → `RateLimited`; 403 → `RateLimited`; 503 with an empty body →
     `Network`; 414 → `TooLong`; 400 with `required query strings` → `BadResponse`; 500 with
     `Target language code invalid` → `UnsupportedLanguage`; 500 with `instance has been rate
     limited` → `RateLimited`; 500 with `request failed with status code: 429` → `RateLimited`;
     500 with `Engine does not exist or has been disabled.` → `ServiceUnavailable`.
   - `htmlBody` (200 HTML) → `ServiceUnavailable` without the raw HTML; `jsonError` (200 with an
     `error` field) → `ServiceUnavailable`; `malformedJson` and a missing/empty `translated-text` →
     `BadResponse`; `rateLimitBody` with 200 → `RateLimited`.
   - `timeout` with `setTimeoutMs(300)` and `respond=false` → `Timeout`; a closed server →
     `Network`.
   - `crossOriginRedirectIsNotFollowed`: the first fake server answers 302 with a `Location` on a
     second fake server; the second server receives no request and the reply fails with a fixed
     message. Extend the fake response with the `Location` header for this case.
   - `errorMessageOmitsTextAndUrl`: a 503/HTML/rate-limit case with a `TOPSECRET` text; the message
     contains neither the text nor `://` nor `127.0.0.1`.

   Add `qtranslate_add_test(tst_mozhiwebservice)` after `tst_mozhiparser`; both are platform-neutral
   and add no Widgets.

6. **Register the service in `AppController`.** In `src/app/appcontroller.h` add
   `class MozhiService;` and `MozhiService *m_mozhi = nullptr;` after `m_mymemory`. In
   `src/app/appcontroller.cpp` include `services/mozhiservice.h`, construct
   `m_mozhi = new MozhiService(m_network);`, call `m_mozhi->applySettings(settings);` and
   `m_translations->registerService(m_mozhi);` after the MyMemory service; in `applySettings()` call
   `if (m_mozhi) m_mozhi->applySettings(newSettings);`. Do not add an `AppSettings` field or an INI
   key in this task; the gate, picker filtering and active-service switch are already generic. Do not
   duplicate the MyMemory gate test in `tst_translationmanager`: `requiresOptIn()` is asserted in
   `tst_mozhiwebservice::properties`, and the manager gate is already covered by
   `gatedRealServiceIsRefused`.

7. **Build, count and verify.** Configure and build Linux Debug and Release with `-Wall -Wextra`;
   run `tst_mozhiparser`, `tst_mozhiwebservice`, `tst_languages`, `tst_queuedwebservice`,
   `tst_deeplwebservice` and `tst_mymemoryservice`, then the full suite with
   `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`. `ctest -N` must show 33
   Linux tests (31 + `tst_mozhiparser` + `tst_mozhiwebservice`); Windows would be 28 (26 + 2). Run
   `tst_mozhiwebservice` at least 10 times for timing flakes. Record 33/28 in the outcome; do not
   touch the settings UI, `.ts`, `doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md`,
   README, `doc/handoff.md`, the root plan or `07-fallback-chain`. Windows/MSVC is unavailable:
   record the Windows Debug/Release build and its 28 tests as not performed. CTest must never contact
   a real instance; the optional live smoke belongs to `03-mozhi-ui-and-docs`/the lead. Leave commit
   and push to the coordinator/lead.

## Done when

- `MozhiService` and `MozhiParser` exist under `src/services/`, are listed in `qtranslate_core` and
  `tests/CMakeLists.txt`, and return the internal `mozhi` id with `requiresOptIn() == true`,
  `isConfigured() == true`, a 500 ms default pause and the default instance/engine on a fresh
  service; Google, DeepL (including `free-web`), the LLM service, MyMemory and their tests are
  unchanged.
- `tst_mozhiparser` covers a normal response, `detected`, the `source_language` fallback, `auto`,
  `target_transliteration`, an empty and a missing `translated-text`, malformed JSON, an HTML body,
  a JSON `error`, the plain-text rate-limit/status-code-429/invalid-language/unknown-engine/required
  fields bodies, and the `errorForHttpStatus` mapping; each case produces the documented
  `TranslationError` and a fixed message without the text, body or URL.
- `tst_mozhiwebservice` with a local `QTcpServer` verifies the GET `/api/translate` path (including a
  prefix instance), the `engine`, `from`, `to` and `text` fields, percent-encoding of spaces,
  non-ASCII text and literal `+`, the configured instance, the engine change, the response body and
  the `mozhi` service id.
- The source language `auto` is sent as `auto` and is not rejected before the request; the parsed
  `detected`/`source_language` fills `detectedSourceLang`; an invalid source or target code that the
  instance rejects with a language body returns `UnsupportedLanguage`; an internal code outside the
  catalogue returns `UnsupportedLanguage` before any request.
- Long text is split with `TextChunker::split` at 5000 UTF-16 code units at paragraph/sentence
  boundaries and sent sequentially inside one reply with the 500 ms pause (`setRateLimitMs(0)` in
  tests); the result restores the edge whitespace and equals the concatenation of the chunk
  translations; edge whitespace is not sent.
- HTTP 429/403, a rate-limit body and a body containing `status code: 429` return `RateLimited`;
  HTTP 413/414 returns `TooLong`; HTTP 5xx without a recognised body returns `Network`; an
  HTML/blocked response returns `ServiceUnavailable` without raw HTML; a disabled engine and a JSON
  error return `ServiceUnavailable`; malformed JSON, a missing/empty `translated-text` and an unknown
  non-2xx status return `BadResponse`; a timeout and `abort()` finish with `Timeout`/`Cancelled` and
  stop the request.
- Requests to the configured host are serialized through `QueuedWebService`; a second translation
  waits for the active one and the pause; cancelling a queued request does not send it; a failed chunk
  fails the whole reply without a partial translation; a cross-origin redirect is not followed and
  the second host receives no request.
- No text, URL or body containing the text is logged; `Q_LOGGING_CATEGORY(lcMozhi,
  "qtranslate.service.mozhi", QtWarningMsg)` and every user-facing error/log line contains only a
  fixed description, the HTTP status and the service name; `QNetworkReply::errorString()` is never
  used.
- `AppController` creates and registers the service after MyMemory and calls its no-op
  `applySettings()`; no `AppSettings` field or INI key is added.
- Debug and Release build without warnings on Linux; `ctest -N` shows 33 Linux tests and would show
  28 on Windows; the full suite passes 33/33 (31 existing plus `tst_mozhiparser` and
  `tst_mozhiwebservice`); `tst_mozhiwebservice` runs at least 10 times without a timing flake;
  Windows Debug/Release and its 28 tests are recorded as not performed (no MSVC).
- The settings keys/normalization, settings group, translations, `doc/architecture.md`,
  `doc/free-endpoints.md`, the `AGENTS.md` counts, `README.md`, `README.ru.md`, `doc/handoff.md`, the
  root plan and `07-fallback-chain` are not touched here; `02-mozhi-settings` and
  `03-mozhi-ui-and-docs` own them.

## Risks

- The request URL contains the text and can be long: 5000 UTF-16 units of Cyrillic percent-encode to
  a long query. Never log the URL or query; map 413/414 to `TooLong`, and if a live smoke shows the
  instance/proxy rejects the default chunk, change `kMaxChunkUnits` in one constant and record the
  measurement. Do not silently lower it or add a hidden cap.
- A plain-text 5xx is not always a network error: the default instance returns HTTP 500 with
  `request failed with status code: 429`, and `libretranslate` on a public instance answers "Engine
  does not exist or has been disabled." Classify the body before the status; otherwise a rate limit
  becomes `Network` and the user retries into the same limit.
- `auto` is accepted by the Google engine, but another engine can pass an upstream error through
  `translated-text` with HTTP 200 (observed with `engine=mymemory` and `auto`: the MyMemory error
  text arrives as the translation). The client cannot distinguish that from a real translation
  without a fragile content heuristic; do not add one. Document that `auto` depends on the
  instance/engine and that the default `google` engine is the supported choice.
- The public instance is unstable and rate-limited (the default instance answered a valid request
  with an upstream 429 on 2026-11-09). CTest uses only the local server; a live smoke is optional and
  its failure is a recorded result, not a code change. Do not add instance rotation.
- Redirects: a cross-origin redirect would send the text to another host. Keep
  `SameOriginRedirectPolicy` on every Mozhi request; the exact Qt error for a blocked redirect may
  vary between versions, so the test asserts no second request and a non-success reply rather than
  one enum.
- Timing tests are the flake risk: use `setRateLimitMs(0)` for most cases and 100/200 ms only for the
  pacing/queue cases, with lower bounds, no upper bounds, and `QTRY_*` timeouts.
- The engine ids are the documented lower-case names. A self-hosted instance can disable any engine;
  send the id unchanged and map "Engine does not exist or has been disabled." to
  `ServiceUnavailable`, not to `UnsupportedLanguage`.
- Windows Debug/Release and its 28 tests are unavailable on this Linux machine; record them as not
  performed, not as passed.
