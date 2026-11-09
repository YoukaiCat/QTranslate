---
kind: task
status: done
detailed: true
---

# Yandex web parser and service

## Task

Add the `yandex-web` service and its pure parser, register the service in the app, and cover it with
fixtures and the local `QTcpServer` tests. This task implements the protocol, the 5000-unit
chunking, the 750 ms pause, `auto`, the language mapping and the classified errors. It does not add
the settings group, translations or documentation; those are `02-yandex-web-settings` and
`03-yandex-web-docs`.

The service is `yandex-web`, derives from `QueuedWebService`, sets a 750 ms pause, returns
`requiresOptIn() == true` and `isConfigured() == true`, and sends
`POST https://api.browser.yandex.com/instaserp/translate` with
`application/x-www-form-urlencoded`. The form fields, in the documented order, are `text`,
`brandID=int`, `statLang=<target>`, `targetLang=auto`, `locale=<target>`, `clid=2270494`,
`disable=serp`, `use_llm_srv=0`. The endpoint expects a browser User-Agent (Chrome/YaBrowser); set it
on this request only, after `NetworkManager::makeRequest()` has set `AppInfo::userAgent()`, and
document that this is protocol emulation, not a claim that the app is a browser.
`NetworkManager::makeRequest()` and `AppInfo::userAgent()` stay unchanged, because the free Google
endpoint and the official APIs rely on the app User-Agent.

The response is a JSON array. The first element is an object with `text` (the translation) and
`from` (the detected source language); the observed shape also has `to`, and extra fields must be
tolerated. The parser returns `BadResponse` for an empty array, a non-array root, a malformed
document or a missing/non-string/empty `text`, and maps `from` back with
`Languages::fromServiceCode()`. The request/response shape was observed from the upstream
`ahatem/QTranslate` plugin `yandex-web-services` (`YandexWebClient`, `YandexWebResponse`) on
2026-11-09 and is recorded in `doc/free-endpoints.md` §2; write the implementation from the observed
HTTP protocol, do not copy the plugin code (MPL-2.0).

The protocol has no source-language field: every request sends `targetLang=auto`, and the target is
sent as `statLang` and `locale`. The internal source may be `auto`; an explicit source is validated
against the Yandex catalogue, but the endpoint detects the source itself. Map the internal codes to
the Yandex form and declare only the codes with a known mapping; an unmapped source or target
returns `UnsupportedLanguage` before any request. Long text is split with `TextChunker::split` at
5000 UTF-16 code units, sent sequentially inside one reply, and the edge whitespace is restored.

Errors: a bare 429/403 and a rate-limit body become `RateLimited`; 5xx without a recognised body
becomes `Network`; an HTML page, a captcha page or a blocked response becomes `ServiceUnavailable` without
raw HTML; 413 becomes `TooLong`; other non-2xx becomes `BadResponse`; a timeout and `abort()` stop
the request. The form body contains the text, so messages and logs must not include the text, the
body or the URL, and the browser User-Agent must not be logged as if it were a secret (it is not
one, but keep the log short).

## Steps

1. **Add the `yandex-web` language branch.** In `src/core/languages.cpp`, next to `kMozhi`, add
   `constexpr QLatin1String kYandexWeb("yandex-web");`, a `yandexWebCodeOf()` helper and the branch in
   `serviceCodeOf()`. Do not add a `LanguageInfo` column and do not change the table.

   Yandex takes one Chinese code (`zh`) for both variants, one Portuguese code (`pt`), `no` for
   Norwegian and `he` for Hebrew; the other codes are the internal ISO 639-1 codes. The observed
   provider catalogue is this whitelist (from the upstream plugin, 2026-11-09):

   ```cpp
   constexpr const char *kYandexWebCodes[] = {
       "af", "az", "sq", "ar", "hy", "eu", "be", "bg", "ca", "zh", "hr", "cs",
       "da", "nl", "en", "et", "fi", "fr", "gl", "de", "el", "ht", "he", "hi",
       "hu", "is", "id", "it", "ga", "ja", "ka", "ko", "lv", "lt", "mk", "ms",
       "mt", "no", "fa", "pl", "pt", "ro", "ru", "sr", "sk", "sl", "es", "sw",
       "sv", "th", "tr", "uk", "ur", "vi", "cy", "yi", "eo", "la", "lo", "kk",
       "uz", "si", "tg", "te", "km", "mn", "kn", "ta", "mr", "bn", "tt",
   };
   constexpr CodeOverride kYandexWebOverrides[] = {
       {"zh-Hans", "zh"},
       {"zh-Hant", "zh"},
       {"nb", "no"},
       {"pt-BR", "pt"},
   };
   ```

   `yandexWebCodeOf(const LanguageInfo &info)` starts from `info.code`, applies the override (if
   any), returns the whitelisted pointer when the result is in `kYandexWebCodes`, and `nullptr`
   otherwise. `pt-PT` is deliberately not overridden and is therefore unsupported: Yandex has one
   Portuguese code, `pt-BR` is the catalogue default, and `Languages::closest()` maps `pt-PT` to
   `pt-BR`. `zh-Hant` stays declared (it maps to `zh` like `zh-Hans`); a reverse `zh` maps to
   `zh-Hans` because of the table order, which is recorded as a known limitation. `fil`, `jv`,
   `ceb`, `haw`, `hmn`, `zu` and every other code outside the whitelist return `nullptr`.
   `fromServiceCode()`, `supported()` and `closest()` then reverse the mapping for free.

   In `tests/tst_languages.cpp`, add `const QString yandexWeb = QStringLiteral("yandex-web");`.
   - Forward rows: `en`→`en`, `ru`→`ru`, `uk`→`uk`, `tr`→`tr`, `de`→`de`, `zh-Hans`→`zh`,
     `zh-Hant`→`zh`, `nb`→`no`, `pt-BR`→`pt`, `he`→`he`, `pt-PT`→empty, `fil`→empty, `jv`→empty,
     `ceb`→empty, `zu`→empty, `xx`→empty, `auto`→empty.
   - Reverse rows: `zh`→`zh-Hans`, `no`→`nb`, `pt`→`pt-BR`, `he`→`he`, `en`→`en`, `ru`→`ru`,
     `xx`→empty.
   - `roundTrip()`: add `yandexWeb` to the service list. Skip `zh-Hant` for `yandex-web` in both
     directions because it shares `zh` with `zh-Hans`; `pt-PT` is not in `supported()`, so the loop
     does not see it. Keep the existing `pt-PT`/`zh-Hant` source exception for the other services.
   - `supportedLists()`: assert the `yandex-web` source and target lists are equal, non-empty,
     contain `ru`, `uk`, `tr`, `zh-Hans`, `he`, `nb` and `pt-BR`, and exclude `auto`, `fil`, `jv`
     and `pt-PT`. Keep the existing `"yandex"` unknown-service rows: the id is `yandex-web`, so
     `Languages::supported("yandex", ...)` and `closest("yandex", ...)` must stay empty.

2. **Add the pure parser.** Create `src/services/yandexwebparser.h/.cpp` with:

   ```cpp
   namespace YandexWebParser {
   // A 200/2xx body: [{"text":"...","from":"en","to":"de"}]; the first element is used.
   TranslationResult parseResponse(const QByteArray &body);
   // Recognises an HTML/captcha page, a JSON error and a plain-text rate-limit/block body.
   // Returns TranslationError::None when the body is not a recognised error.
   TranslationResult parseErrorBody(const QByteArray &body);
   TranslationError errorForHttpStatus(int status);
   }
   ```

   Include `core/translationtypes.h`, `core/languages.h`, `QJsonArray`, `QJsonDocument`,
   `QJsonObject`, `QJsonParseError`, `QJsonValue` and `QByteArray`. `errorResult()` returns a result
   with `serviceId` empty, like `MozhiParser`.

   `parseResponse()` first returns the result of `parseErrorBody()` when it reports an error.
   Otherwise it parses the JSON: a malformed document, a non-array root, an empty array, a
   non-object first element, or a missing/non-string/empty (after `trimmed()`) `text` becomes
   `BadResponse` with the fixed message `no translated text in the response`. Read the optional
   string `from`; map it with
   `Languages::fromServiceCode("yandex-web", from, Languages::Direction::Source)`. Read `to` and any
   other fields only to tolerate them; do not use them. Fill `translatedText` and
   `detectedSourceLang`, leave `serviceId` empty and leave `errorMessage` empty on success.

   `parseErrorBody()` order:
   - a trimmed body that starts with `<` → `ServiceUnavailable` with
     `the endpoint returned an HTML page` (covers a block/captcha page);
   - a JSON object: read the string `error` (or the `message` string of an `error` object), else
     the string `message`. Classify case-insensitively: `rate limit`, `too many requests`,
     `quota`, `status code: 429` or `status code: 403` → `RateLimited` with
     `the endpoint reported a rate limit`; `captcha`, `blocked`, `forbidden`, `access denied`,
     `banned`, `robot` or `unavailable` → `ServiceUnavailable` with
     `the endpoint is blocked or unavailable`; any other non-empty error text →
     `ServiceUnavailable` with `the endpoint returned an error`. A missing/empty error text → `None`;
   - a plain-text body (not starting with `{`/`[`) → the same keyword classification; a rate-limit
     text → `RateLimited`, a block/captcha text → `ServiceUnavailable`, anything else → `None` so
     the HTTP status decides;
   - an empty or malformed JSON body, or a JSON array, → `None`.

   `errorForHttpStatus()` maps 403/429 → `RateLimited`, 413 → `TooLong`, 5xx → `Network`, every
   other non-2xx → `BadResponse`. Every message is a fixed English literal and never contains the
   body, the text, a URL, `{`, `}` or `<`.

   Add `src/services/yandexwebparser.cpp/.h` to `qtranslate_core` in `CMakeLists.txt` after
   `queuedwebservice.cpp/.h` (alphabetically before `yandexwebservice`).

3. **Add the service and register its sources.** Create `src/services/yandexwebservice.h/.cpp`:

   - `class YandexWebService : public QueuedWebService`; `id()` returns `yandex-web`,
     `displayName()` returns `Yandex`, `isConfigured()` and `requiresOptIn()` return true,
     `sourceLanguages()`/`targetLanguages()` return `Languages::supported("yandex-web", ...)` (no
     auto), and `applySettings(const AppSettings &)` is an empty no-op with an unnamed parameter
     (there are no per-service settings).
   - File-local constants with comments: `kMaxChunkUnits = 5000` (UTF-16 code units; the doc gives no
     Yandex-specific limit, so this is a conservative assumption) and `kDefaultRateLimitMs = 750`
     (from `doc/free-endpoints.md` §8). The constructor sets `setRateLimitMs(kDefaultRateLimitMs)`.
   - The endpoint is fixed: `static QUrl defaultEndpoint()` returns
     `https://api.browser.yandex.com/instaserp/translate`; add `setEndpoint(const QUrl &)` and
     `QUrl endpoint() const` as the test seam, like `MyMemoryService`. An empty/invalid URL falls
     back to `defaultEndpoint()`.
   - Add `static QString browserUserAgent()`, returning the literal
     `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)
     Chrome/144.0.0.0 YaBrowser/26.3.0.0 Safari/537.36` (observed from the upstream plugin on
     2026-11-09). Put a header comment next to it: the endpoint expects a browser; this is protocol
     emulation, not a claim that QTranslate is a browser, and it is set only on this service's
     requests after `makeRequest()`.
   - `translate()`: return `EmptyText` for empty/whitespace-only text. Map the source: `auto` stays
     `auto`; otherwise `Languages::toServiceCode("yandex-web", ..., Direction::Source)`. Map the
     target with `toServiceCode(..., Direction::Target)`. An empty target, or an empty non-auto
     source, returns `UnsupportedLanguage` with `language "%1" is not supported` before any request.
     The source is not sent (the protocol has no source field); it only gates unsupported requests.
     Split the text with `TextChunker::split(text, 5000)`, strip and restore each part's edge
     whitespace, treat an all-whitespace part as unsent, and enqueue the reply. Log the part count
     and the `source`/`target` internal codes only.
   - The nested `Reply` derives from `QueuedWebService::Reply` and holds the network/service
     pointers, endpoint, source/target, parts, translations and the first detected code. `start()`
     schedules `send()` through `Qt::QueuedConnection`; `send()` builds the form body and sends it.
     Use a file-local helper that percent-encodes every value as UTF-8, e.g.
     `QUrl::toPercentEncoding(value)`; append the fields in the documented order:
     `text=<part>`, `brandID=int`, `statLang=<target>`, `targetLang=auto`, `locale=<target>`,
     `clid=2270494`, `disable=serp`, `use_llm_srv=0`. The text must carry spaces as `%20`, non-ASCII
     as UTF-8 escapes, and literal `&`, `=`, `+` and `%` escaped.
   - Build `QNetworkRequest request = m_network->makeRequest(m_endpoint);` then set exactly:
     `Content-Type: application/x-www-form-urlencoded`, `Accept: application/json`, and
     `User-Agent: browserUserAgent()` (this overrides the app User-Agent for this request only).
     Set `QNetworkRequest::RedirectPolicyAttribute = QNetworkRequest::SameOriginRedirectPolicy`, so
     the body cannot move to another host through a redirect. Call `service->noteRequestSent()`
     immediately before `nam()->post(request, body)`, mark the part sent and start a request timer.
     Never log the body, the URL or the User-Agent.
   - `onFinished()`: read the body only when `status != 0 || error == NoError`, delete the reply and
     return when already finished. `OperationCanceledError`/`TimeoutError` → `Timeout` with
     `Yandex did not respond within %1 s`. A non-2xx status → first
     `YandexWebParser::parseErrorBody(body)`; if it reports an error, use it; otherwise
     `YandexWebParser::errorForHttpStatus(status)` with `HTTP %1`. Other network errors → `Network`
     through `describeNetworkError(request, false)` or `Network error` when the manager is gone. Then
     `YandexWebParser::parseResponse(body)`: on success append the text, keep the first non-empty
     `detectedSourceLang`, and either schedule the next part after `service->rateLimitMs()` or
     finish. On a chunk error fail the whole reply without a partial result; if the number of
     translations does not match the sent parts, fail with `BadResponse`.
   - `abortImpl()` disconnects and aborts the active request; pending chunk timers check
     `isFinished()`. The joined result sets `serviceId = "yandex-web"` and restores the edge
     whitespace of every part, including unsent whitespace-only parts.
   - `Q_LOGGING_CATEGORY(lcYandexWeb, "qtranslate.service.yandex-web", QtWarningMsg)`; log only the
     part count, the internal language codes, the HTTP status, elapsed time and fixed messages. Never
     log the text, the body, the URL, the form fields or `QNetworkReply::errorString()`. Add the
     service files to `qtranslate_core` after the parser.

4. **Add parser fixtures and `tst_yandexwebparser`.** Create `tests/fixtures/yandexweb/`:
   `normal.json` (`[{"text":"Hallo Welt!","from":"en","to":"de"}]`),
   `extra_fields.json` (a first object with `text`, `from`, `to` plus `detected`, `requestId` and a
   second array element that must be ignored), `from_zh.json` (`from: "zh"`),
   `from_no.json` (`from: "no"`), `from_pt.json` (`from: "pt"`), `from_he.json` (`from: "he"`),
   `from_unknown.json` (`from: "xx"`), `missing_from.json`, `empty_array.json` (`[]`),
   `empty_text.json` (`text: ""`), `missing_text.json`, `first_element.json` (`[1]`),
   `object.json` (`{"text":"..."}`), `string.json` (`"..."`), `malformed.json`,
   `html.html` (`<!DOCTYPE html>...captcha`),
   `rate_limit.json` (`{"message":"Too many requests"}`), `rate_limit.txt` (`Too many requests`),
   `blocked.json` (`{"error":"Request blocked"}`), `blocked.txt` (`Access denied`) and
   `unknown_error.json` (`{"error":"Something went wrong"}`). Use the `fixture()` helper and the
   `QTRANSLATE_FIXTURES_DIR` pattern from `tests/tst_mozhiparser.cpp`, with
   `Q_DECLARE_METATYPE(TranslationError)`.

   Cover: a normal response (text, empty `serviceId`); an extra first-element field and a second
   element ignored; `from` rows `en`, `zh`→`zh-Hans`, `no`→`nb`, `pt`→`pt-BR`, `he`→`he`,
   `xx`→empty and missing→empty; an empty array, empty text, missing text, a non-object first
   element, a JSON object, a JSON string and malformed JSON → `BadResponse`; an HTML body →
   `ServiceUnavailable`; the JSON and plain-text rate-limit bodies → `RateLimited`; the JSON and
   plain-text block bodies → `ServiceUnavailable`; an unknown JSON error → `ServiceUnavailable`; a
   plain-text unknown body → `parseErrorBody()` returns `None` and `parseResponse()` returns
   `BadResponse`; the `errorForHttpStatus` rows (403/429, 413, 500/503, 400). Add a helper that
   asserts every error message is non-empty and contains no `"SECRET"`, no `<`, no `{`, no `}` and
   no `://`. Add `qtranslate_add_test(tst_yandexwebparser)` to `tests/CMakeLists.txt` in
   alphabetical order, so `tst_yandexwebparser` comes after `tst_windowactivation_linux` and
   before `tst_yandexwebservice`.

5. **Add `tst_yandexwebservice`.** Copy the `FakeHttpServer` from `tests/tst_mozhiwebservice.cpp`
   (method, URL, headers, body, elapsed time, `respond=false`, and the optional `Location` header
   for the redirect case). `init()` creates the server, `NetworkManager` and `YandexWebService`,
   calls `setEndpoint(QUrl("http://127.0.0.1:<port>/instaserp/translate"))` and
   `setRateLimitMs(0)`; `cleanup()` resets all three. Add `Q_DECLARE_METATYPE(TranslationError)`, a
   form-decoding helper (`split('&')`, split at the first `=`, `QUrl::fromPercentEncoding` on both
   sides; values are UTF-8, so the decoded `text` is comparable with the input), a
   `translationResponse(text, from = {})` helper that writes a JSON array with `text`, `from` and
   `to`, and an `echoResponse(body)` helper that returns the form `text` as the translation.

   Cover:
   - `properties`: id `yandex-web`, display name `Yandex`, configured, opt-in, source and target
     lists non-empty without `auto`, containing `ru`/`uk`/`tr`/`zh-Hans`/`he`/`nb`/`pt-BR` and
     excluding `fil`/`jv`/`pt-PT`, and `defaultEndpoint()`.
   - `defaultPause`: a fresh service has `rateLimitMs() == 750`.
   - `sendsPostRequest`: method `POST`, path `/instaserp/translate`, `Content-Type` exactly
     `application/x-www-form-urlencoded`, `Accept: application/json`, no `Authorization`, and the
     decoded fields `text=Hello`, `brandID=int`, `statLang=de`, `targetLang=auto`, `locale=de`,
     `clid=2270494`, `disable=serp`, `use_llm_srv=0`; `serviceId == "yandex-web"` and the parsed
     result.
   - `usesBrowserUserAgentOnThisRequestOnly`: the Yandex request's `user-agent` equals
     `YandexWebService::browserUserAgent()` and differs from `AppInfo::userAgent()`; a request built
     through `m_network->makeRequest()` and sent directly through `nam()->get()` (the common path
     every other service uses) still has `AppInfo::userAgent()`.
   - `encodesFormBody`: text with spaces, Cyrillic, emoji, `&`, `=`, `+` and `%`; the decoded `text`
     equals the input; the raw body contains no literal space and the separators are only the seven
     field `&`s; `%20`, `%2B`, `%26`, `%3D` and Cyrillic/emoji escapes are present.
   - `mapsLanguageCodes_data`/`mapsLanguageCodes`: target rows `en`, `ru`, `uk`, `tr`, `de`,
     `zh-Hans`→`zh`, `zh-Hant`→`zh`, `nb`→`no`, `pt-BR`→`pt`, `he`→`he`; both `statLang` and
     `locale` carry the mapped code and `targetLang` stays `auto`.
   - `autoSourceIsAccepted`: source `auto`, target `ru`; the request succeeds, no source field is
     sent, and a response `from: "en"` becomes `detectedSourceLang == "en"`; a response with
     `from: "zh"` becomes `zh-Hans`.
   - `unmappedLanguagesAreRejected`: an unmapped source (`fil`, `jv`, `ceb` or `zu`) and an
     unmapped target (`fil`, `jv` or `zu`) return `UnsupportedLanguage` with a clear message and no
     request is sent.
   - `emptyTextIsEmptyError`: empty and whitespace-only text, no request.
   - `edgeWhitespaceIsTrimmed`: `" \tHello\r\n "` is sent trimmed and the result restores it.
   - `longTextIsSplitIntoChunks`: a long Cyrillic text with paragraphs and emoji produces one
     request per non-whitespace-only part of `TextChunker::split(text, 5000)`, every decoded `text`
     is at most 5000 UTF-16 code units, no `\r\n` or surrogate pair is split, the result equals the
     chunk translations with the original edge whitespace, and the request count matches the sent
     parts.
   - `pacesChunks` (`setRateLimitMs(100)`, gaps ≥ 90 ms) and `serializesRequests`
     (`setRateLimitMs(200)`, two translations, the second waits for the first and the pause).
   - `cancelsQueuedRequest` (only the first request is sent) and `abortActive` (first request seen,
     abort → `Cancelled`).
   - `failedChunkFailsTheWholeReply`: the first chunk returns 200, the second a 500 rate-limit body;
     the reply fails with `RateLimited`, `translatedText` is empty and exactly two requests were
     sent.
   - `httpErrors_data`: 429 and 403 with an empty body → `RateLimited`; 500/503 with an empty body →
     `Network`; 413 → `TooLong`; 400 → `BadResponse`. Use an empty body for the `Network` rows so
     the body classifier does not turn the status into `ServiceUnavailable`.
   - `htmlBody` (200 HTML/captcha) → `ServiceUnavailable` without the raw HTML; `rateLimitBody`
     (200 `{"message":"Too many requests"}`) → `RateLimited`; `blockedBody` (200 HTML or
     `{"error":"Request blocked"}`) → `ServiceUnavailable`; `jsonError` (200 unknown error) →
     `ServiceUnavailable`; `malformedJson`, an empty array, a missing text and a JSON object →
     `BadResponse`.
   - `timeout` with `setTimeoutMs(300)` and `respond=false` → `Timeout`; a closed server →
     `Network` with the fixed `Network error`.
   - `crossOriginRedirectIsNotFollowed`: the first fake server answers 302 with a `Location` on a
     second fake server; the second server receives no request and the reply fails with a fixed
     message. Extend the fake response with the `Location` header for this case.
   - `errorMessageOmitsTextAndUrl`: a 503/HTML/rate-limit case with a `TOPSECRET` text; the message
     contains neither the text nor `://` nor `127.0.0.1`, and the request body does contain the
     text.

   Add `qtranslate_add_test(tst_yandexwebservice)` after `tst_yandexwebparser`; both are
   platform-neutral and add no Widgets.

6. **Register the service in `AppController`.** In `src/app/appcontroller.h` add
   `class YandexWebService;` and `YandexWebService *m_yandexWeb = nullptr;` after `m_mozhi`. In
   `src/app/appcontroller.cpp` include `services/yandexwebservice.h`, construct
   `m_yandexWeb = new YandexWebService(m_network);`, call `m_yandexWeb->applySettings(settings);` and
   `m_translations->registerService(m_yandexWeb);` after the Mozhi service; in `applySettings()` call
   `if (m_yandexWeb) m_yandexWeb->applySettings(newSettings);`. Do not add an `AppSettings` field or
   an INI key; the gate, picker filtering and active-service switch are already generic. Do not add
   a cache reset: there is no Yandex setting to change.

7. **Build, count and verify.** Configure and build Linux Debug and Release with `-Wall -Wextra`;
   run `tst_yandexwebparser`, `tst_yandexwebservice`, `tst_languages`, `tst_queuedwebservice`,
   `tst_mozhiwebservice` and `tst_mymemoryservice`, then the full suite with
   `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`. `ctest -N` must show 35
   Linux tests (33 + the two new targets); Windows would be 30 (28 + 2). Run
   `tst_yandexwebservice` at least 10 times for timing flakes. Record 35/30 in the outcome; do not
   touch the settings UI, `.ts`, `doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md`, the
   READMEs, `doc/handoff.md`, the root plan or `07-fallback-chain`. Windows/MSVC is unavailable:
   record the Windows Debug/Release build and its 30 tests as not performed. CTest must never contact
   the real endpoint; the manual live smoke is recorded as not performed with the reason (the
   endpoint may return a captcha/403), or as passed if the user runs it. Leave commit and push to the
   coordinator/lead.

## Done when

- `YandexWebService` and `YandexWebParser` exist under `src/services/`, are listed in
  `qtranslate_core` and `tests/CMakeLists.txt`, and return the internal `yandex-web` id with
  `requiresOptIn() == true`, `isConfigured() == true`, a 750 ms default pause and the default
  endpoint on a fresh service; the service is not active by default; Google, DeepL (including
  `free-web`), the LLM service, MyMemory, Mozhi and their tests are unchanged.
- The Yandex language branch maps `zh-Hans`/`zh-Hant`→`zh`, `nb`→`no`, `pt-BR`→`pt`, `he`→`he` and
  the observed ISO codes; `pt-PT`, `fil`, `jv`, `ceb` and every code outside the whitelist are not
  declared; `fromServiceCode()` reverses `zh`→`zh-Hans`, `no`→`nb`, `pt`→`pt-BR`, `he`→`he`;
  `toServiceCode("yandex-web", ..., Source/Target)` is empty for an unsupported code; no
  `LanguageInfo` column is added.
- `tst_yandexwebparser` covers a normal array response with `text` and a detected language, extra
  fields and a second element ignored, the `from` mapping rows, an empty array, a missing/empty
  `text`, a non-object first element, a JSON object, a JSON string, malformed JSON, an HTML/captcha
  body, the JSON and plain-text rate-limit/block bodies, an unknown JSON error and the
  `errorForHttpStatus` mapping; each case produces the documented `TranslationError` and a fixed
  message without the text, body or URL.
- `tst_yandexwebservice` with a local `QTcpServer` verifies the POST path, the
  `application/x-www-form-urlencoded` content type, every documented field including `brandID`,
  `clid`, `disable` and `use_llm_srv`, the browser User-Agent on this request, a common
  `NetworkManager::makeRequest()` request still using `AppInfo::userAgent()`, URL encoding of spaces,
  non-ASCII text and `&`/`=`/`+`/`%`, the mapped target code in `statLang` and `locale`, the constant
  `targetLang=auto`, and the response body and `yandex-web` service id.
- A supported target is sent as the mapped Yandex code; `auto` is accepted and `targetLang=auto` is
  sent as documented; an unmapped source or target returns `UnsupportedLanguage` before any request;
  the detected `from` is mapped back to the internal code.
- Long text is split with `TextChunker::split` at 5000 UTF-16 code units at paragraph/sentence
  boundaries and sent sequentially inside one reply with the 750 ms pause (`setRateLimitMs(0)` in
  tests); the result restores the edge whitespace and equals the concatenation of the chunk
  translations; a failed chunk fails the whole reply without a partial result.
- HTTP 429/403 with an empty body and a rate-limit body return `RateLimited`; HTTP 5xx with an
  unrecognised body returns `Network`; an HTML/captcha/blocked response returns `ServiceUnavailable`
  without raw HTML; 413 returns `TooLong`; an unknown non-2xx status returns `BadResponse`; a timeout
  and `abort()` finish with `Timeout`/`Cancelled` and stop the request; a cross-origin redirect is
  refused.
- Requests to the endpoint are serialized through `QueuedWebService`; a second translation waits for
  the active one and the pause; cancelling a queued request does not send it; no text, body, URL or
  User-Agent is logged; `QNetworkReply::errorString()` is never used.
- `AppController` creates and registers the service after Mozhi and calls its no-op
  `applySettings()`; no `AppSettings` field, INI key or cache reset is added.
- Debug and Release build without warnings on Linux; `ctest -N` shows 35 Linux tests and would show
  30 on Windows; the full suite passes 35/35; `tst_yandexwebservice` runs at least 10 times without
  a timing flake; Windows Debug/Release and its 30 tests are recorded as not performed (no MSVC); the
  manual live smoke is recorded as not performed with the reason (or passed if run).
- The settings group, translations, `doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md`,
  both READMEs, `doc/handoff.md`, the root plan and `07-fallback-chain` are not touched here;
  `02-yandex-web-settings` and `03-yandex-web-docs` own them.

## Risks

- The endpoint is unofficial and browser-oriented. A browser User-Agent is required today, but the
  fields, the clid, or the response shape may change without notice; the parser must fail with a
  clear `BadResponse`/`ServiceUnavailable`, never a crash or raw HTML. Do not add a captcha solver,
  cookie harvesting, or any bypass of a block.
- The browser User-Agent is set per request; do not change `NetworkManager::makeRequest()` or
  `AppInfo::userAgent()`, because the free Google endpoint and the official APIs rely on the app
  User-Agent. The test must prove both: the Yandex request has the browser UA and a common
  `makeRequest()` request still has the app UA.
- The form body contains the text and the endpoint has no source field. `targetLang=auto` is always
  sent, and an explicit source is validated only. If a live smoke shows different semantics, record
  it; do not guess silently or send the source somewhere undocumented.
- Yandex has one `zh` and one `pt`: `zh-Hant` is declared but sends `zh`, so a traditional-Chinese
  request can return simplified text, and a detected `zh` maps back to `zh-Hans`. `pt-PT` is not
  declared and `closest()` maps it to `pt-BR`. These are known limitations, not parser bugs.
- The 5000 UTF-16 units and 750 ms are conservative assumptions from the doc, not measured Yandex
  limits. Keep them in named constants; if a live smoke shows a lower limit or a different pause,
  change the constant and record the measurement. Do not add a hidden cap.
- The endpoint may block a datacenter or a country IP and may answer with a captcha HTML page.
  CTest uses only the local server; a live smoke is manual and its failure is a recorded result, not
  a code change. A 403 with an HTML body is `ServiceUnavailable`, a bare 403 is `RateLimited`; the
  body is classified before the status, so a block page never becomes a fake translation.
- Timing tests are the flake risk: use `setRateLimitMs(0)` for most cases and 100/200 ms only for
  the pacing/queue cases, with lower bounds, no upper bounds and `QTRY_*` timeouts.
- Windows Debug/Release and its 30 tests are unavailable on this Linux machine; record them as not
  performed, not as passed.

## Outcome

Done on 2026-11-09. `YandexWebParser` and `YandexWebService` exist and are registered in the app
behind the free-web opt-in gate; the `yandex-web` language branch, the parser fixtures and two new
platform-neutral tests are in place. Google, DeepL (including `free-web`), the LLM service, MyMemory,
Mozhi and the shared `QueuedWebService`/`TextChunker`/`Languages` behaviour are unchanged. The
settings group, the translations and the documentation belong to `02-yandex-web-settings` and
`03-yandex-web-docs` and are not touched here.

- **Parser:** `src/services/yandexwebparser.h/.cpp`. `parseResponse()` classifies the body as an error
  first, then reads the first element of the JSON array: a malformed document, a non-array root, an
  empty array, a non-object first element, and a missing, non-string or empty `text` are
  `BadResponse` with "no translated text in the response"; `to` and any extra fields, as well as
  later array elements, are tolerated and unused; `from` is mapped with
  `Languages::fromServiceCode("yandex-web", ..., Direction::Source)` (an unknown or missing value
  stays empty); `serviceId` is empty on success and in errors. `parseErrorBody()` recognises a
  trimmed body starting with `<` as `ServiceUnavailable` ("the endpoint returned an HTML page"), a
  JSON object by its `error` string or `error.message`, falling back to the root `message`, and a
  plain-text body by keywords; rate-limit keywords are matched first (`rate limit`, `too many
  requests`, `quota`, `status code: 429`, `status code: 403` → `RateLimited`), then block keywords
  (`captcha`, `blocked`, `forbidden`, `access denied`, `banned`, `robot`, `unavailable` →
  `ServiceUnavailable`); any other non-empty JSON error text is `ServiceUnavailable`, while a
  missing/empty JSON error text, an empty or malformed body, a JSON array and an unrecognised plain
  text return `TranslationError::None` so the caller falls back to the HTTP status.
  `errorForHttpStatus()` maps 403/429 → `RateLimited`, 413 → `TooLong`, 5xx → `Network` and every
  other non-2xx → `BadResponse`. Every message is a fixed English literal and never contains the
  body, the text or a URL.
- **Service:** `src/services/yandexwebservice.h/.cpp`. Id `yandex-web`, display name `Yandex`,
  `isConfigured()`/`requiresOptIn()` true, languages from `Languages::supported()` without `auto`,
  a 750 ms default pause (from `doc/free-endpoints.md` §8) and a no-op
  `applySettings(const AppSettings &)` with an unnamed parameter. The endpoint is the fixed
  `https://api.browser.yandex.com/instaserp/translate`; `setEndpoint()`/`endpoint()` are the test
  seam and an empty or invalid URL falls back to the default. `browserUserAgent()` returns the
  observed Chrome/YaBrowser literal and its header comments the protocol emulation: the browser
  User-Agent is set only on this service's requests, after `NetworkManager::makeRequest()` has set
  `AppInfo::userAgent()`, and neither the shared request factory nor the app User-Agent changes.
  `translate()` returns `EmptyText` for empty/whitespace-only text, accepts `auto`, validates an
  explicit source and the target with `Languages::toServiceCode()` and returns
  `UnsupportedLanguage` before any request for an unmapped code. The protocol has no source field,
  so the source is never sent; the target is sent as `statLang` and `locale` and `targetLang` is
  always `auto`. The text is split with `TextChunker::split(text, 5000)`; every part's edge
  whitespace stays out of the request and is restored in the joined result, and a whitespace-only
  part is never sent. The form body percent-encodes every name and value as UTF-8 (spaces as `%20`,
  literal `&`, `=`, `+` and `%` escaped) and appends `text`, `brandID=int`, `statLang`,
  `targetLang=auto`, `locale`, `clid=2270494`, `disable=serp`, `use_llm_srv=0` in that order. Each
  request is a `POST` with `Content-Type: application/x-www-form-urlencoded`,
  `Accept: application/json`, the browser User-Agent and `SameOriginRedirectPolicy`;
  `noteRequestSent()` is called immediately before `nam()->post()`, and all chunks of one reply are
  sent sequentially inside its single queue slot with the pause. A failed chunk fails the whole reply
  without a partial result, and a translation count that does not match the sent parts is
  `BadResponse`. A non-2xx status classifies the body first and then uses `errorForHttpStatus()`;
  a timeout/`abort()` finishes with `Timeout`/`Cancelled`; other network errors use
  `NetworkManager::describeNetworkError(request, false)` or the fixed "Network error". Logs contain
  only the part count, language codes, HTTP status, elapsed time and fixed messages; the text, body,
  URL, form fields and `QNetworkReply::errorString()` are never logged.
- **Languages:** `src/core/languages.cpp` adds `kYandexWeb`, the observed 71-code whitelist
  `kYandexWebCodes`, `kYandexWebOverrides` (`zh-Hans`/`zh-Hant` → `zh`, `nb` → `no`, `pt-BR` → `pt`)
  and `yandexWebCodeOf()`, called from `serviceCodeOf()`; no `LanguageInfo` column and no synonym
  table is added. `pt-PT` is deliberately not overridden: Yandex has one Portuguese code, `pt-BR`
  is the catalogue default, and `closest()` maps `pt-PT` to `pt-BR`. `zh-Hant` is declared and sends
  `zh`; a reverse `zh` maps to `zh-Hans` because of the table order. `tst_languages` covers the
  forward and reverse rows, the `zh-Hant` exception in `roundTrip()` and the equal source/target
  lists without `auto`, `fil`, `jv` or `pt-PT`; the existing `"yandex"` unknown-service rows are
  kept, because the new id is `yandex-web`.
- **App and build:** `AppController` constructs `YandexWebService` after Mozhi, applies its no-op
  settings, registers it after Mozhi and forwards `applySettings()`; no `AppSettings` field, INI key
  or cache reset is added, because there is no Yandex setting to change. Both source pairs are in
  `qtranslate_core` after `queuedwebservice`, and both test targets are registered after
  `tst_windowactivation_linux` and before `tst_yandexwebservice`, platform-neutral.
- **Tests:** `tests/fixtures/yandexweb/` holds the 21 planned fixtures. `tst_yandexwebparser` covers
  the normal array response with `text` and a detected language, extra fields and a second element
  ignored, the `from` mapping rows (`en`, `zh`→`zh-Hans`, `no`→`nb`, `pt`→`pt-BR`, `he`, unknown,
  missing), the empty array, empty/missing `text`, a non-object first element, a JSON object, a JSON
  string, malformed JSON, an HTML/captcha body, the JSON and plain-text rate-limit and block bodies,
  an unknown JSON error, the plain-text "not an error" fallback and the `errorForHttpStatus` rows,
  with a helper that proves every message is non-empty and free of the text marker, `<`, `{`, `}`
  and `://`. `tst_yandexwebservice` uses a local `QTcpServer` (the Mozhi `FakeHttpServer` with the
  optional `Location` header) and covers the properties and defaults, the POST path, the exact
  content type, the eight form fields in order without `Authorization`, the browser User-Agent on
  this request and the app User-Agent on a common `makeRequest()` request, URL encoding of spaces,
  Cyrillic, emoji and `&`/`=`/`+`/`%`, the mapped target code in `statLang` and `locale` with a
  constant `targetLang=auto`, `auto`, rejected unmapped languages and empty text without a request,
  edge whitespace, 5000-unit chunking at `TextChunker` boundaries, chunk pacing and queue
  serialization, cancelling a queued reply and aborting an active one, a failed middle chunk (whole
  reply fails, no partial translation, no third request), the HTTP status rows, the HTML/JSON/malformed
  bodies, the rate-limit and block bodies, timeout, a closed server, a cross-origin redirect that
  must not reach the second server, and message hygiene. CTest never contacts the real endpoint.
- **Verification:** Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2) and `qtranslate-trixie:latest`
  (Qt 6.8.2, GCC 14.2), Debug and Release: zero `warning:` lines, `ctest -N` 35, the six targeted
  tests 6/6, the full suite 35/35 in every configuration, and `tst_yandexwebservice` 10 repeats per
  host configuration without a timing flake. The trixie runs used a read-only source mount, `/tmp`
  build directories, `--user`, `HOME=/tmp` and read-only `/etc/passwd`/`/etc/group` mounts for
  `dbus-daemon` in `tst_hotkeymanager_wayland` (an environment workaround, not repository content);
  no root-owned files were left in the tree.
- **Review:** no findings — no correctness, security, regression or missing-coverage defects in the
  new code, and no old-code problem was noticed in passing. Nothing was accepted or rejected, and no
  real problem was deferred because of cost.
- **Not performed and deferred by design:** Windows/MSVC Debug and Release and its 30 tests (no
  MSVC; both new tests are platform-neutral) and a live smoke against the real endpoint (optional:
  the endpoint is unofficial and may answer with a captcha or a 403 from this IP). The settings
  group, the 14 `.ts` files, `doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md`, both
  READMEs, `doc/handoff.md`, the root plan and `07-fallback-chain` are untouched.
- **Known limitations:** `zh-Hant` is declared but sends `zh`, so a traditional-Chinese request can
  return simplified text, and a detected `zh` maps back to `zh-Hans`; `pt-PT` is not declared and
  `closest()` maps it to `pt-BR`. The 5000 UTF-16 units and the 750 ms pause are documented
  assumptions from `doc/free-endpoints.md` §8, not measured Yandex limits; a live smoke should
  record any measured value before changing the constants.
