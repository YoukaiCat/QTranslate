---
kind: task
status: done
detailed: true
---

# LibreTranslate parser and service

## Task

Implement the LibreTranslate protocol recorded in
`../../01-endpoint-research/notes/duckduckgo-libretranslate.md` (§1.1–§1.4, §1.10 and Appendix A.1) as a
pure parser plus a `QueuedWebService`, register the service, add its language column and the fallback
id, and cover it with fixtures and local-server tests. The base URL and the optional API key are
injected through `setBaseUrl()`/`setApiKey()`; the INI keys, the settings group and the strings belong
to `02-libretranslate-settings-ui`. No UI, no `.ts` and no `doc/` change here.

The service id is `libretranslate` and the display name is the product name `LibreTranslate`,
untranslated, like `Google`, `Reverso` and `DuckDuckGo`. The id is a top-level service id; it is
unrelated to the `libretranslate` *Mozhi engine* id of the same name (`mozhiEngineIds()`,
`src/core/settings.cpp:298-306`), and the two namespaces never interact.

Protocol (research §1.1): `POST <base>/translate` with `Content-Type: application/json`,
`Accept: application/json` and the compact JSON body of `q` (the chunk), `source` (the code or `auto`)
and `target` (the code), plus `api_key` only when a key is set. The response is
`{"translatedText":"…"}` and, when `source` was `auto`, additionally
`{"detectedLanguage":{"confidence":90.0,"language":"ru"}}`; `confidence` is tolerated and ignored,
because `TranslationResult` has no field for it. `source` is mandatory on the server side, so
auto-detection is sent as the literal `"source":"auto"` (omitting it is a `400`). A base URL with a
path prefix must keep the prefix: `<base>/translate` after trimming the trailing slash, exactly as
`MozhiService` builds `<instance>/api/translate` (`src/services/mozhiservice.cpp:69-81`).

The base URL has **no default**: research §1.5 found no public key-less instance that answered, and the
stage forbids sending a user's text to a volunteer server the user did not enter. `setBaseUrl()` stores
what it is given (the tests point it at a local `QTcpServer`), `defaultBaseUrl()` returns an empty
`QUrl` and documents that emptiness, and `isConfigured()` returns false for an empty, invalid or
non-`http(s)` value with a `configurationError()` that names the base URL. The API key is optional and
never affects `isConfigured()`; an empty key simply omits `api_key`. `applySettings()` stays empty in
this task — `02-libretranslate-settings-ui` adds `LibreTranslateSettings`, the shared base-URL helpers
and the call to `setBaseUrl()`/`setApiKey()`. Until then the provider is registered but unconfigured,
which is the intended intermediate state of the stage.

Language handling (research §1.3): `libreTranslateCodeOf()` in `src/core/languages.cpp` declares the 51
codes that a full `/languages` of a 1.9.6 server returned, with the two lossy aliases internal `fil` →
`tl` and internal `pt-PT` → `pt`; every one of the other 59 internal codes is refused with
`UnsupportedLanguage` before any request. `auto` is supported and is sent as `auto`;
`detectedLanguage.language` is mapped back through `Languages::fromServiceCode(…,
Direction::Source)`. The service keeps the static table and never fetches `/languages` per request: a
server with fewer models answers `400 <code> is not supported`, which becomes `UnsupportedLanguage`
after one request (research §1.3).

Chunking and pacing: 5000 UTF-16 code units (`TextChunker::split`) and a 500 ms pause between requests,
both named conservative assumptions taken from research §1.4 — the observed stock server accepted 6460
characters and did not throttle, so these values are not measurements, exactly as `MozhiService` says in
its header. Chunks are sent sequentially inside one reply slot, edge whitespace is restored, and a failed
chunk fails the whole reply without a partial translation.

Errors are classified body-first, because a key-protected instance answers `400` (not `401`) with a key
message, and the server sends `{"error":"<sentence>"}` with a human-readable English sentence rather than
a stable code: a `Slowdown` body or `429` → `RateLimited`; a body with `exceeds text limit` or `413`/`414`
→ `TooLong`; a body with `is not supported` → `UnsupportedLanguage`; a body that mentions an API key
(`Invalid API key`, `Visit https://portal.libretranslate.com to get an API key`) → `Auth`; a body with
`Invalid request:` → `BadResponse`; an HTML page or any other provider error → `ServiceUnavailable`;
`5xx` without a recognised body → `Network`; an unknown non-2xx status → `BadResponse`; malformed JSON,
a missing, non-string, empty or whitespace `translatedText` → `BadResponse`; timeout and `abort()` →
`Timeout`/`Cancelled`. The base URL is user input, so every request uses `SameOriginRedirectPolicy`, and
no message or log line contains the text, the body, the base URL or the key; network errors use
`NetworkManager::describeNetworkError(request, false)`.

Register the service in `AppController::createTranslation()` behind the existing providers and in
`applySettings()`, append `libretranslate` to `fallbackServiceIds()` in `src/core/settings.cpp` (and
update the exact-list assertion at `tests/tst_settings.cpp:122-125`), add the two sources to the root
`CMakeLists.txt`, and add the two tests to `tests/CMakeLists.txt`.

## Steps

1. **Add the fixtures.** Create `tests/fixtures/libretranslate/` with synthetic bodies shaped like the
   recorded ones — no page or body taken verbatim from a live server — quoting Appendix A.1:

   - `response.json` — `{"translatedText":"Привет"}` (the explicit-source shape: no
     `detectedLanguage` at all);
   - `response_auto.json` —
     `{"detectedLanguage":{"confidence":90.0,"language":"ru"},"translatedText":"Good morning"}` exactly
     as recorded; `response_auto_no_confidence.json` — the same without `confidence`;
   - `response_detected_unknown.json` — `detectedLanguage.language` is `xx`;
     `response_detected_null.json` — `"detectedLanguage":null`;
   - `response_missing_translated.json`, `response_empty_translated.json`,
     `response_whitespace_translated.json`, `response_non_string_translated.json`,
     `response_array_translated.json` (`{"translatedText":["Привет","Доброе утро"]}` — the batch shape
     this client never asks for), `response_array.json`, `response_string.json`,
     `response_malformed.json`, `response_empty.json`;
   - `error_not_supported.json` — `{"error":"xx is not supported"}`;
     `error_text_limit.json` — `{"error":"Invalid request: request (6000) exceeds text limit (5000)"}`;
     `error_missing_q.json` — `{"error":"Invalid request: missing q parameter"}`;
     `error_invalid_key.json` — `{"error":"Invalid API key"}`;
     `error_portal.json` — `{"error":"Visit https://portal.libretranslate.com to get an API key"}`;
     `error_slowdown.json` — `{"error":"Slowdown: 10 per minute"}`;
     `error_unknown.json` — `{"error":"something new"}`;
   - `html_page.html` (a small 403/policy-looking page), `plain.txt` (`Hello there`, not an error).

   Read them through `QStringLiteral(QTRANSLATE_FIXTURES_DIR "/libretranslate/") + name`, as
   `tests/tst_reversoparser.cpp:12` does.

2. **Write the parser.** Create `src/services/libretranslateparser.h/.cpp` with
   `namespace LibreTranslateParser`, no network and no state, modelled on
   `src/services/duckduckgoparser.*` (the newest parser that also fills `detectedSourceLang`):

   - `TranslationResult parseErrorBody(const QByteArray &body)`:
     - an empty body returns a default `TranslationResult` (error `None`), so the caller falls back to
       the HTTP status;
     - a body whose first non-space byte is `<` → `ServiceUnavailable`,
       `the endpoint returned an HTML page`;
     - a JSON object with a string `error` is classified by that string, case-insensitively and in this
       order: `slowdown` → `RateLimited`, `the endpoint reported a rate limit`; `exceeds text limit` →
       `TooLong`, `the request is too long for the endpoint`; `is not supported` →
       `UnsupportedLanguage`, `the endpoint rejected the language`; `api key` → `Auth`,
       `the endpoint rejected the API key`; a value that starts with `invalid request` → `BadResponse`,
       `the endpoint rejected the request`; anything else → `ServiceUnavailable`,
       `the endpoint returned an error`;
     - any other body (plain text, a JSON array, JSON without `error`) returns `None`; the recorded
       endpoint has no plain-text error shape, so no block-word list is needed here (unlike
       `BingWebParser`);
   - `TranslationResult parseResponse(const QByteArray &body)`: call `parseErrorBody()` first (a proxy
     or a misconfigured server may answer `200`), then require a JSON object whose `translatedText` is a
     string that is not empty after `trimmed()`, else `BadResponse` with the fixed
     `no translated text in the response`. `detectedLanguage` is optional and may be `null`: an object
     with a string `language` is mapped through
     `Languages::fromServiceCode(QStringLiteral("libretranslate"), value, Languages::Direction::Source)`,
     any other shape leaves `detectedSourceLang` empty, and `confidence` is ignored. Every other field
     is ignored. `serviceId` is left empty for the caller to set;
   - `TranslationError errorForHttpStatus(int status)`: `401`/`403` → `Auth`; `429` → `RateLimited`;
     `413`/`414` → `TooLong`; `500`–`599` → `Network`; everything else (`400`, `404`, an unfollowed
     `3xx`) → `BadResponse`.

   Include `core/languages.h` and `core/translationtypes.h`; every message is a fixed literal that never
   repeats the body, the error sentence, the text or a URL. The `Auth` message names the key, never the
   text (stage `Done when`).

3. **Add the language column.** In `src/core/languages.cpp`, next to `kDuckDuckGo`:

   - `constexpr QLatin1String kLibreTranslate("libretranslate");` after `kDuckDuckGo` (line 164);
   - `constexpr const char *kLibreTranslateCodes[]` with the 51 codes observed by
     `GET https://libretranslate.com/languages` on 2026-11-15, in the note's order: `en sq ar az eu bn
     bg ca zh-Hans zh-Hant cs da nl eo et fi fr gl de el he hi hu id ga it ja ko ky lv lt ms nb fa pl pt
     pt-BR ro ru sr sk sl es sw sv tl th tr uk ur vi`. The comment names the source (research §1.3), says
     that a server's own `/languages` is the authority for that server and that the table is not fetched
     per request, and says that the 59 internal codes outside it are refused before a request;
   - `constexpr CodeOverride kLibreTranslateOverrides[] = {{"fil", "tl"}, {"pt-PT", "pt"}};` — the two
     lossy aliases: the server names the Tagalog model `tl` and has no `pt-PT` model, so European
     Portuguese falls back to the general Portuguese code. The comment says both are lossy and why;
   - `libreTranslateCodeOf(const LanguageInfo &info)`: apply `kLibreTranslateOverrides`, then return the
     code only when it is in `kLibreTranslateCodes`, else `nullptr` — the same shape as
     `yandexWebCodeOf()` (`src/core/languages.cpp:298-312`);
   - a `serviceId == kLibreTranslate` branch in `serviceCodeOf()` returning `libreTranslateCodeOf(info)`
     (after the `kDuckDuckGo` branch, line 397-398).

   Do **not** add a `kSynonyms` row: `Languages::fromServiceCode()` scans `serviceCodeOf()` in both
   directions (`src/core/languages.cpp:460-481`), so `tl` resolves to `fil` (no other internal code maps
   to `tl`) and `pt` resolves to `pt-PT` (`pt-BR` maps to `pt-BR`, a different string, so the scan never
   matches it). A synonym would be dead code, because synonyms are consulted only when the scan finds
   nothing. Add a comment on `libreTranslateCodeOf()` saying exactly that, so nobody "fixes" it later.

4. **Add the service class and its seams.** Create `src/services/libretranslateservice.h/.cpp` with
   `class LibreTranslateService : public QueuedWebService`:

   - constructor `explicit LibreTranslateService(NetworkManager *network, QObject *parent = nullptr)`
     storing `m_network`, leaving `m_baseUrl` empty and `m_apiKey` empty, and calling
     `setRateLimitMs(kDefaultRateLimitMs)`;
   - `id()` → `libretranslate`; `displayName()` → `LibreTranslate`; `sourceLanguages()`/
     `targetLanguages()` through `Languages::supported(kServiceId, …)`;
   - `bool isConfigured() const override`: `!m_baseUrl.isEmpty() && m_baseUrl.isValid()
     && (scheme == "http" || scheme == "https") && !m_baseUrl.host().isEmpty()` — the same four rules
     `isValidMozhiInstance()` enforces (`src/core/settings.cpp:334-345`). Add a comment that
     `02-libretranslate-settings-ui` extracts those rules into `isValidServiceBaseUrl()` and that this
     check is deliberately the same predicate, so the later switch changes no behaviour; do not extract
     the helper here (that task owns it);
   - `QString configurationError() const override`: when unconfigured,
     `tr("LibreTranslate needs the base URL of a LibreTranslate server.")`; otherwise empty. This is a
     user-visible string added through `tr()`; it reaches the `.ts` files in the
     `update_translations` pass of `02-libretranslate-settings-ui`, so do not touch `i18n/` here;
   - `applySettings(const AppSettings &)` with an empty body and a comment that
     `LibreTranslateSettings`, `[libretranslate] baseUrl`/`apiKey` and the shared URL helpers arrive in
     `02-libretranslate-settings-ui`, which calls `setBaseUrl()`/`setApiKey()`; there is no cache to
     clear in this task;
   - test and wiring seams: `void setBaseUrl(const QUrl &url)` storing the value **as given** — there is
     no default to fall back to, so an empty or invalid URL is kept and makes the service unconfigured,
     unlike `MozhiService::setInstance()`; `QUrl baseUrl() const`;
     `void setApiKey(const QString &key)` storing the value as given and `QString apiKey() const`;
     `static QUrl defaultBaseUrl()` returning an empty `QUrl` with the comment that research §1.5 found
     no public key-less instance and that a default would send text to a server the user did not enter;
   - private `class Reply;`, `NetworkManager *m_network`, `QUrl m_baseUrl`,
     `QString m_apiKey; // optional; never logged`, plus
     `Q_LOGGING_CATEGORY(lcLibreTranslate, "qtranslate.service.libretranslate", QtWarningMsg)` in the
     `.cpp`.

   File-local constants with their justification comments: `kServiceId = "libretranslate"`,
   `kMaxChunkUnits = 5000`, `kDefaultRateLimitMs = 500` (research §1.4 names both as conservative
   assumptions: the stock server accepted 6460 characters and did not throttle). Copy `Part`,
   `splitParts()`, `finishedWithError()` and `endpointUrl()` from `src/services/mozhiservice.cpp:42-90`
   (`endpointUrl()` appends `/translate` instead of `/api/translate` and keeps a path prefix), and
   `errorKind()` from `src/services/reversoservice.cpp:92-126`.

5. **Implement `translate()`.**

   - `if (!isConfigured())` → `finishedWithError(TranslationError::Auth, configurationError())`, before
     anything else, so a direct caller gets the same hint `TranslationManager` produces for an
     unconfigured service (`src/core/translationmanager.cpp:354-360`), as
     `src/services/deeplservice.cpp:297-305` does;
   - `request.text.trimmed().isEmpty()` → `EmptyText` with an empty message, before any request;
   - source: `request.sourceLang == Languages::autoCode()` → `QStringLiteral("auto")`, otherwise
     `Languages::toServiceCode(kServiceId, request.sourceLang, Direction::Source)`; target through
     `Direction::Target`. An empty mapped code (`af`, `kk`, `pa`, `haw`, `ceb`, `xx`, …) returns
     `UnsupportedLanguage` with `language "<code>" is not supported`, naming the unmapped side, before
     any request — the same message style as `MozhiService` and `ReversoService`. Do not special-case
     `auto` with a separate message: this server supports it;
   - `const QList<Part> parts = splitParts(request.text);`, log only
     `Translating <n> part(s), <source|auto> -> <target>` (internal codes, no text, no URL, no key), and
     `enqueue(new Reply(m_network, endpointUrl(m_baseUrl), m_apiKey, parts, source, target, this))`.
     One text is one reply, however many chunks it has. The endpoint and the key are captured now, so a
     later settings change cannot alter a request that is already in flight (the stage requires that a
     key change does not abort an active reply).

6. **Implement the request stage of `Reply`.** The nested `Reply` follows `MozhiService::Reply`
   (`src/services/mozhiservice.cpp:94-201`), with a JSON POST instead of a query GET:

   - `start()` starts `m_elapsed` and schedules `send()` through
     `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`, so `finished()` is never emitted from inside
     `translate()`; `~Reply()` and `abortImpl()` call `stopRequest()` (`disconnect`, `abort`,
     `deleteLater`), and `abortImpl()` logs `Cancelled`;
   - `send()` returns when finished, fails with `Network` and the fixed `network is not available` when
     `m_network` is gone, and otherwise starts the first unsent non-empty part (`nextUnsentPart()`);
   - request preparation in `send()`: `m_network->makeRequest(url)` (which sets the transfer timeout
     and `AppInfo::userAgent()` — LibreTranslate does not check the User-Agent, research §1.1),
     `ContentTypeHeader` = `application/json`, raw header `Accept: application/json`, and
     `QNetworkRequest::SameOriginRedirectPolicy`, and **`Accept-Language: en`**: Qt's HTTP layer always
     adds a locale-derived `Accept-Language` when the request has none
     (`QHttpNetworkConnectionPrivate::prepareRequest()`), so the header cannot be omitted; the server
     localises its `error` sentences through `request.accept_languages.best_match()` and the classifier
     matches the English ones, so pinning `en` keeps the classifier working under every system locale
     and keeps the system locale from leaking (research §1.10, user decision 2026-11-15);
   - the body is built with `QJsonObject`:
     `q` = the chunk text, `source` = `m_source` (`auto` or the mapped code), `target` = `m_target`, and
     `api_key` = `m_api_key.trimmed()` only when `!m_api_key.trimmed().isEmpty()`, serialized with
     `QJsonDocument(object).toJson(QJsonDocument::Compact)`. JSON escaping is why a quote, a backslash or
     a newline inside the text is safe here, unlike the percent-encoded query of `MozhiService`;
     `QJsonObject` writes its keys alphabetically, so no test may compare the body byte for byte —
     parse it instead;
   - mark the part sent, call `noteRequestSent()` immediately before `m_network->nam()->post()` — not
     before building the body — start `m_requestElapsed` and connect `QNetworkReply::finished` to
     `onFinished()`.

7. **Implement `onFinished()`, the chunk loop and the error mapping.** In the same `Reply`, in this
   order:

   - take the status, the `QNetworkReply::NetworkError`, and the body only when the transfer is readable
     (`status != 0 || error == NoError`), then `deleteLater()` the reply and return when `isFinished()`;
   - log only `HTTP <status> in <n> ms`;
   - `OperationCanceledError`/`TimeoutError` → `Timeout` with
     `LibreTranslate did not respond within <n> s`, formatting `m_network->timeoutMs()` with
     `QString::number(seconds, 'g', 3)` exactly as the sibling services do;
   - any non-2xx status: `LibreTranslateParser::parseErrorBody(body)` first; when it reports an error,
     `fail(parsed.error, parsed.errorMessage)`; otherwise fail with
     `LibreTranslateParser::errorForHttpStatus(status)` and `HTTP <status>`;
   - any other `QNetworkReply::NetworkError` → `Network` and
     `m_network->describeNetworkError(request, false)` (never `QNetworkReply::errorString()`, and never
     the reply text: the request body holds the user's text);
   - `2xx`: `LibreTranslateParser::parseResponse(body)`; on failure fail the whole reply; on success
     append `translatedText` to `m_translations`, keep the first non-empty `detectedSourceLang`, and
     call `scheduleNextOrFinish()`;
   - `scheduleNextOrFinish()` uses `QTimer::singleShot(rateLimitMs(), this, …)` and re-checks
     `isFinished()`, so `setRateLimitMs(0)` makes the tests deterministic;
   - `finishTranslation()` copies `MozhiService` exactly (`src/services/mozhiservice.cpp:275-304`):
     compare the number of sent parts with the number of translations and fail with `BadResponse` when
     they differ, join `leading + translation + trailing` for every sent part and the whole `leading` for
     an unsent whitespace-only part, set `serviceId = "libretranslate"` and the first detected language,
     log `Translated <n> part(s), <source> -> <target> in <elapsed> ms` and call `finish(result)`. A
     failed chunk never leaves a partial translation behind.

8. **Keep the logs and the messages clean.** `fail()` logs only `Translation failed: <errorKind>` and
   never `errorMessage`, as `ReversoService` does (`src/services/reversoservice.cpp:343-352`); the
   provider error sentence may echo the request (`Invalid request: …`), so it must not reach the log.
   The only other lines are the fixed `HTTP <status> in <elapsed> ms`, `Translating …`,
   `Translated …` and `Cancelled`. No line contains the text, the body, the key, the base URL or any
   part of a URL. `errorMessage` carries only the fixed literals of steps 2, 5 and 7,
   `configurationError()` and `NetworkManager::describeNetworkError(request, false)`.

9. **Register the service and the fallback id.** In `src/app/appcontroller.h` add
   `class LibreTranslateService;` after `class GoogleTranslateService;` (keep the forward-declaration
   block alphabetical) and `LibreTranslateService *m_libreTranslate = nullptr;` after `m_duckduckgo`. In
   `src/app/appcontroller.cpp`: `#include "services/libretranslateservice.h"` after the
   `languagetoolservice.h` include; `m_libreTranslate = new LibreTranslateService(m_network);` **last**,
   after `m_duckduckgo` in `createTranslation()`, `m_libreTranslate->applySettings(settings);` after the
   `m_duckduckgo` line and `m_translations->registerService(m_libreTranslate);` after the DuckDuckGo
   registration, so the pickers keep their order and every existing service keeps its place. In
   `applySettings(oldSettings, newSettings)` add
   `if (m_libreTranslate) m_libreTranslate->applySettings(newSettings);` after the `m_duckduckgo` block.
   In `src/core/settings.cpp` append `QStringLiteral("libretranslate")` after `"duckduckgo"` in
   `fallbackServiceIds()` (lines 308-317) and update the exact-list assertion at
   `tests/tst_settings.cpp:122-125`. Nothing else in `settings.cpp` changes: there is no INI key and no
   normalization in this task.

10. **Register the sources and the test targets.** In the root `CMakeLists.txt` add
    `src/services/libretranslateparser.{cpp,h}` and `src/services/libretranslateservice.{cpp,h}` to
    `qtranslate_core` after `src/services/languagetoolservice.*` and before `src/services/mozhiparser.*`,
    so the block stays alphabetical. In `tests/CMakeLists.txt` add
    `qtranslate_add_test(tst_libretranslateparser)` and `qtranslate_add_test(tst_libretranslateservice)`
    after `tst_languagetoolservice` and before `tst_mozhiparser`. Both targets are platform-neutral: no
    `Qt::Widgets`, no `xvfb` property, because they talk to a local `QTcpServer` only.

11. **Write `tests/tst_libretranslateparser.cpp`** (`QTEST_GUILESS_MAIN`), modelled on
    `tests/tst_reversoparser.cpp` and `tests/tst_duckduckgoparser.cpp`:

    - `response()`: `Привет`, empty `detectedSourceLang`, empty `serviceId`;
    - `detectedLanguage()`: `response_auto.json` → `Good morning` and `detectedSourceLang == "ru"`;
      `response_auto_no_confidence.json` → the same, proving `confidence` is optional;
      `response_detected_unknown.json` → `xx` leaves the field empty; `response_detected_null.json` and
      `response.json` → empty; two inline bodies (not fixtures) show the reverse aliases:
      `{"detectedLanguage":{"language":"tl"}}` → `fil` and `{"detectedLanguage":{"language":"pt"}}` →
      `pt-PT`;
    - `badResponses()` data-driven over `response_missing_translated`, `response_empty_translated`,
      `response_whitespace_translated`, `response_non_string_translated`,
      `response_array_translated`, `response_array`, `response_string`, `response_malformed`,
      `response_empty` → `BadResponse` and `no translated text in the response`;
    - `errorBodies()` data-driven: `error_slowdown` → `RateLimited`; `error_text_limit` → `TooLong`;
      `error_not_supported` → `UnsupportedLanguage`; `error_invalid_key` and `error_portal` → `Auth`;
      `error_missing_q` → `BadResponse`; `error_unknown` → `ServiceUnavailable`;
      `html_page.html` → `ServiceUnavailable`;
    - `unrecognisedBodiesAreNotErrors()`: `plain.txt`, an empty body, `{}` and `[1,2]` return
      `TranslationError::None`, so the caller falls back to the status;
    - `httpStatus()` data-driven: `400`/`404` → `BadResponse`, `401`/`403` → `Auth`, `429` →
      `RateLimited`, `413`/`414` → `TooLong`, `500`/`503` → `Network`;
    - `verifySafeMessage()` (as `tests/tst_reversoparser.cpp:20-28`: no `SECRET`, `<`, `{`, `}`, `://`)
      on every message the parser returns, including the `Auth` one.

12. **Write `tests/tst_libretranslateservice.cpp`** on a local `QTcpServer`, copying `FakeHttpServer`,
    `LogCapture` and `verifyFixedMessage()` from `tests/tst_duckduckgoservice.cpp` (the newest copies;
    they already record the method, URL, headers, body and arrival time, and can answer with a status,
    a body, no response or a `Location`). `init()` builds the server, `NetworkManager` and
    `LibreTranslateService`, calls `setBaseUrl(QUrl("http://127.0.0.1:<port>/"))` and
    `setRateLimitMs(0)`; `cleanup()` resets all three. Helpers: `translationResponse(text, detected)`
    and `autoResponse(text, detected)` as JSON objects, `longText()` (Cyrillic paragraphs, more than
    15000 UTF-16 units so there are at least four chunks at 5000), and a `LogCapture` with the rule
    `qtranslate.service.libretranslate.debug=true`. Cover:

    - `properties()`: id, display name `LibreTranslate`, `rateLimitMs() == 500`, `apiKey().isEmpty()`,
      the two language lists equal to `Languages::supported("libretranslate", …)`, `fil`, `pt-PT`,
      `zh-Hant`, `sr` and `nb` present, `auto`, `af`, `kk`, `pa` and `haw` absent;
    - `configuration()`: a *fresh* instance is unconfigured, `baseUrl() == defaultBaseUrl()` and both
      are empty, `configurationError()` mentions the base URL and a request for `Hello` finishes with
      `Auth` and that message after **zero** requests (`QTest::qWait(50)` and an empty
      `m_server->requests`); an invalid URL, an `ftp://` URL and a URL without a host stay unconfigured;
      `setApiKey("key")` does not make it configured and `setBaseUrl(local)` does; `setBaseUrl(QUrl())`
      makes it unconfigured again;
    - `translation()`: `en`→`ru` → exactly one `POST /translate` with `Content-Type: application/json`,
      `Accept: application/json`, `Accept-Language: en`, a body whose parsed object has
      `q == "Hello"`, `source == "en"`, `target == "ru"` and **no** `api_key` member at all; the result
      is `Привет`, `serviceId == "libretranslate"`, `detectedSourceLang` empty;
    - `apiKeyIsOptional()`: `setApiKey("secret-key")` → the body carries
      `api_key == "secret-key"`; `setApiKey("  secret-key\n")` → the body carries the trimmed
      `secret-key`; `setApiKey(QString())` → no `api_key` member; the `LogCapture` output never contains
      `secret-key`;
    - `autoIsDetected()`: `auto` → the body's `source` is `auto`, the fixture answers
      `autoResponse("Good morning", "ru")` and the result fills `detectedSourceLang == "ru"`;
    - `pathPrefixIsKept()`: base `http://127.0.0.1:<port>/lt/` → the request path is `/lt/translate`;
      a base without a trailing slash produces the same path;
    - `specialCharacters()`: a text with `"`, `\`, `\n` and `{}` reaches the server unchanged in `q` and
      the result is what the server answered, proving the JSON escaping;
    - `unsupportedLanguage()`: `af` as the target → `UnsupportedLanguage`,
      `language "af" is not supported`; `kk` as the source → the same error naming `kk`; zero requests
      after `QTest::qWait(50)`;
    - `emptyText()` data-driven (empty and whitespace-only) → `EmptyText`, zero requests;
    - `httpErrors()` data-driven, each with `verifyFixedMessage()`: `429` + `error_slowdown.json` →
      `RateLimited`; `429` with an empty body → `RateLimited`; `413` + `error_text_limit.json` →
      `TooLong`; `400` + `error_not_supported.json` → `UnsupportedLanguage`; `403` +
      `error_invalid_key.json` → `Auth`; `400` + `error_portal.json` → `Auth` (the hosted-instance
      shape); `400` + `error_missing_q.json` → `BadResponse`; `401` with an empty body → `Auth`; `500`
      with an empty body → `Network`; `503` + `error_unknown.json` → `ServiceUnavailable`; `404` with an
      empty body → `BadResponse`; `200` + `html_page.html` → `ServiceUnavailable`; `200` +
      `response_malformed.json`, `response_empty.json` and `{}` → `BadResponse`; `200` +
      `error_slowdown.json` → `RateLimited`, proving the body is classified before the status;
    - `chunks()`: `longText()` → the number of POSTs equals the number of non-whitespace
      `TextChunker::split(text, 5000)` parts, every `q` is at most 5000 UTF-16 units, the bodies arrive
      in order, and the joined result equals the input when every chunk is answered separately;
    - `edgeWhitespace()`: `" \tHello\r\n "` reaches the server as `Hello` and the result is
      `" \tX\r\n "`;
    - `pacesChunks()`: `setRateLimitMs(100)`, at least three chunks, and every consecutive pair of
      requests at least 90 ms apart (lower bound only);
    - `serializesRequests()`: two replies with `setRateLimitMs(200)`; the second POST arrives at least
      180 ms after the first;
    - `abortActiveRequest()`: abort during the request → `Cancelled`, one request, recorded
      server-side, no retry;
    - `timeout()`: `setTimeoutMs(300)` and a handler that never answers → `Timeout` and
      `LibreTranslate did not respond within 0.3 s`;
    - `closedServerIsNetwork()`: `m_server->close()` → `Network`, `Network error`;
    - `crossOriginRedirectIsNotFollowed()`: a `302` to a second `FakeHttpServer` — the second server
      receives nothing, the reply is not ok, `translatedText` is empty and the message is fixed;
    - `failedMiddleChunkFailsTheWholeReply()`: the second POST fails; the whole reply fails with that
      error kind, `translatedText` is empty, exactly two POSTs and no third;
    - `logsAndMessagesAreClean()`: a success and a failure with `TOPSECRET` in the text **and** in a
      provider `error` body; neither the captured log nor `errorMessage` contains `TOPSECRET`, the key,
      `<`, `{`, `}`, `://`, `127.0.0.1`, `/translate` or `libretranslate.com`;
    - `fallbackServiceWorks()`: a `TranslationManager` with a fake primary service that finishes with
      `Network` and `setFallbackService("libretranslate")`; the fallback translates against the local
      fixture and the result carries `serviceId == "libretranslate"` (copy the `FakeService` shape from
      `tests/tst_duckduckgoservice.cpp:281-309`).

    Finish with `QTEST_GUILESS_MAIN` and `#include "tst_libretranslateservice.moc"`. CTest never
    contacts a real LibreTranslate server.

13. **Extend `tests/tst_languages.cpp`.** Add
    `const QString libretranslate = QStringLiteral("libretranslate");` to the service constants. In
    `toServiceCode_data()`: identity rows `en`, `ru`, `de`, `es`, `ja`, `zh-Hans`, `zh-Hant`, `nb`,
    `sr`, `pt-BR`; the aliases `fil` → `tl` and `pt-PT` → `pt`; the empty rows `af`, `am`, `kk`, `pa`,
    `haw`, `ceb`, `xx` and `auto`. In `fromServiceCode_data()`: identity rows `en`, `ru`, `zh-Hant`,
    `nb`, `sr`; `tl` → `fil`, `pt` → `pt-PT`, `pt-BR` → `pt-BR`, `FR` (mixed case) → `fr`, `xx` → empty.
    In `roundTrip()`: add `libretranslate` to the service list; the existing `pt-PT`/`zh-Hant` source
    exclusions stay as they are. In `supportedLists()`: `supported(libretranslate, Source)` equals the
    `Target` list, has exactly 51 entries, contains `en`, `ru`, `fil`, `pt-BR`, `pt-PT`, `zh-Hans`,
    `zh-Hant`, `sr` and `nb`, and excludes `auto`, `af`, `kk`, `pa`, `haw` and `ceb`; assert the size,
    so a table edit that silently drops a code fails here.

14. **Build, count and verify.** Run the container first, so the host stays responsive:
    `docker run --rm -v "$PWD:/src:ro" -w /src qtranslate-trixie bash -c 'cmake -S /src -B /tmp/build
    -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/build --parallel && xvfb-run -a ctest
    --test-dir /tmp/build --output-on-failure'`. Then the host Debug and Release:
    `cmake -S . -B build/linux-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug` and the existing
    `build/linux-release`/`Release`, `cmake --build` each, and
    `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`. Zero warnings with
    `-Wall -Wextra` in both configurations; `ctest -N` shows **50** Linux tests (48 + the two new
    targets); the new targets are green and the rest of the suite does not regress. `git diff --stat`
    should show only `src/services/libretranslate{parser,service}.{h,cpp}`, `src/core/languages.cpp`,
    `src/core/settings.cpp`, `src/app/appcontroller.{h,cpp}`, `CMakeLists.txt`, `tests/CMakeLists.txt`,
    `tests/tst_libretranslate{parser,service}.cpp`, `tests/tst_languages.cpp`,
    `tests/tst_settings.cpp` and `tests/fixtures/libretranslate/*`. No `.ts`, no `doc/`, no `AGENTS.md`,
    no README, no `doc/handoff.md`. Windows Debug/Release and its 42 tests (40 + the two platform-neutral
    targets) are recorded as not performed (no MSVC on this machine) and reviewed by diff.

## Done when

- `LibreTranslateService` exists under `src/services/`, is listed in `qtranslate_core`, derives from
  `QueuedWebService`, returns the id `libretranslate`, the display name `LibreTranslate` and the
  language lists of the 51-code table without `auto`; the parser lives in
  `src/services/libretranslateparser.*` and touches no network.
- `libreTranslateCodeOf()` declares the 51 observed codes with `fil` → `tl` and `pt-PT` → `pt`; the 59
  other internal codes produce `UnsupportedLanguage` with `language "<code>" is not supported` and zero
  requests; `auto` is accepted and sent as `source: "auto"`; `detectedLanguage.language` fills
  `detectedSourceLang`, with `tl` → `fil` and `pt` → `pt-PT` resolved by the existing reverse scan and
  no new synonym.
- With `setBaseUrl()` pointing at a local `QTcpServer` the service sends `POST <base>/translate` with
  `Content-Type: application/json`, `Accept: application/json`, `Accept-Language: en`, a JSON body whose
  `q` is the chunk, whose `source` is the configured code or `auto` and whose `target` is the mapped
  code; `api_key` is present only when `setApiKey()` set a non-empty key, and is trimmed there; a base
  URL with a path prefix keeps the prefix.
- The chunk is 5000 UTF-16 code units with a 500 ms pause, chunks are sequential inside one reply slot,
  edge whitespace is restored, and a failed chunk fails the whole reply with no partial translation;
  both values are documented as named conservative assumptions.
- Errors are classified body-first and match the research: a `Slowdown` body or `429` → `RateLimited`;
  `413`/`414` or an `exceeds text limit` body → `TooLong`; an `is not supported` body →
  `UnsupportedLanguage`; a key body (`Invalid API key`, the portal link) on `400`, `401` or `403` →
  `Auth` with a message that names the key; an `Invalid request:` body → `BadResponse`; an HTML page or
  an unknown provider error → `ServiceUnavailable`; `5xx` without a body → `Network`; an unknown non-2xx
  status, malformed JSON and an empty, missing or non-string `translatedText` → `BadResponse`; timeout and
  `abort()` → `Timeout`/`Cancelled`. Every request uses `SameOriginRedirectPolicy` and a cross-origin
  redirect is refused.
- With an empty or invalid base URL, `isConfigured()` is false, `configurationError()` names the base
  URL, and `translate()` reports it as `Auth` without sending a request; the key never changes
  `isConfigured()`.
- `libretranslate` is registered in `AppController` after DuckDuckGo, receives `applySettings()`, and is
  in `fallbackServiceIds()` after `duckduckgo`; `tst_settings` asserts the extended list; there is no
  `[libretranslate]` INI key, no settings group and no UI in this task.
- `tst_libretranslateparser` and `tst_libretranslateservice` cover the response fields, the `error`
  bodies with several statuses, the key present and absent, the `Auth` mapping, `auto` and
  `detectedLanguage`, the unsupported pair with zero requests, the chunk split and order, the pause
  asserted at 100 ms with `setRateLimitMs(0)` everywhere else, abort, timeout, the same-origin redirect,
  the path prefix, the empty-base-URL behaviour, the fallback translation, and the message/log hygiene
  (no text, body, key or URL); `tst_languages` covers the 51-code table, the two aliases, the reverse
  aliases and an unsupported code, and pins the table size.
- Debug and Release build on Linux without warnings and the full suite passes with 50 tests; Windows is
  reviewed by diff and its 42 tests are recorded as not performed.
- No UI, `.ts`, `doc/`, `AGENTS.md`, README or `doc/handoff.md` file is changed; commit and push stay
  with the lead.

## Risks

- The 51-code table is the `/languages` of `libretranslate.com`, i.e. the union of a server with the
  full model set — **not** the languages of the user's server. A server with fewer models answers
  `400 <code> is not supported`, which becomes `UnsupportedLanguage` after one request. Do not fetch
  `/languages` per request (research §1.3) and do not shrink the table to `en`/`ru` because that was
  the probe's server.
- `fil` → `tl` and `pt-PT` → `pt` are lossy: Tagalog is labelled `tl` by the server and European
  Portuguese is translated by the general Portuguese model. `pt` from the server comes back as
  `pt-PT`, not `pt-BR`, because only `pt-PT` declares `pt`. This is intentional and matches research
  §1.3; do not add a `kSynonyms` row — the reverse scan already resolves both, and a synonym would
  never be reached.
- The classifier matches **English** sentences of the server's `error` field. Qt's HTTP layer always
  adds a locale-derived `Accept-Language`, which makes the server answer in the system language (a
  Russian-locale host received localised sentences and lost the `Auth` mapping), so the service pins
  `Accept-Language: en` (research §1.10, user decision 2026-11-15), and the matching is on the recorded
  phrases, not on a status alone: a hosted instance answers `400` when the key is missing, so a
  status-first rule would report `BadResponse` instead of `Auth`. If the server's wording changes, the
  fallback is `ServiceUnavailable`/`BadResponse`, never a wrong `Auth`.
- The provider error sentence may repeat the request (`Invalid request: …`). It stays in
  `TranslationResult::errorMessage` only for cases that cannot echo the text (a wrong body shape), and
  the log gets the error kind alone. Never pass the sentence to the user or the log, and keep the hygiene
  case green.
- The API key is a secret: it must not appear in a log line, in an error message or in the captured
  request log of the test; it is sent only as `api_key` to the base URL the user configured.
- `applySettings()` is deliberately empty here: until `02-libretranslate-settings-ui` lands, the
  provider is registered but unconfigured, and `TranslationManager` reports `Auth` with the hint. That is
  the intended state of the stage, not a defect; do not wire a provisional INI key.
- Both the chunk size and the pause are named conservative assumptions, not measurements (the stock
  server accepted 6460 characters and did not throttle). Change them only after a measurement.
- `QJsonObject` writes keys alphabetically and escapes the text, so a byte comparison of the request
  body would be brittle. Assert the parsed object instead; the one thing a test must not lose is that
  `api_key` is **absent**, not empty, when no key is set.
- The `configurationError()` string is user-visible and translated; it reaches the 14 `.ts` files in the
  `update_translations` pass of `02-libretranslate-settings-ui`, so this task must not touch `i18n/`
  even though `lupdate` would already pick the string up. That is safe here because `tst_translations`
  reads only the existing `.ts` files and never compares them with the sources, so the suite stays
  green with 376 messages.
- The service test file is large (the sibling files are 900–1200 lines) and is the context risk for the
  programmer. Keep the shared helpers small, copy `FakeHttpServer`/`LogCapture` verbatim from
  `tests/tst_duckduckgoservice.cpp`, and if the file does not fit, report it instead of dropping the
  redirect, timeout or hygiene cases.
- A live smoke test needs a reachable server. None is part of this task: the fixtures plus the local
  `QTcpServer` are the automated gate, and a missing live check is recorded as not performed, never as
  passed.

## Outcome

Done. `LibreTranslateService` (`src/services/libretranslateservice.*`) and the pure
`LibreTranslateParser` (`src/services/libretranslateparser.*`) exist, are listed in `qtranslate_core`,
and answer as the id `libretranslate` with the display name `LibreTranslate`. `libreTranslateCodeOf()`
declares the 51 codes observed on `libretranslate.com` with the two lossy aliases; no `kSynonyms` row
was added, because the reverse scan of `Languages::fromServiceCode()` already resolves `tl` → `fil` and
`pt` → `pt-PT`. The service is registered in `AppController` after DuckDuckGo and appended to
`fallbackServiceIds()` after `duckduckgo`. `applySettings()` is deliberately empty: until
`02-libretranslate-settings-ui` lands, the provider is registered but unconfigured and
`TranslationManager` reports `Auth` with the base-URL hint — the intended intermediate state of the
stage. No UI, no `[libretranslate]` key, no `LibreTranslateSettings` and no `.ts` change is part of this
task.

**User decision 2026-11-15 (`Accept-Language`).** The step-6 bullet "do not send `Accept-Language`"
proved impossible, and its premise was wrong. Qt's HTTP layer always adds a locale-derived header when
the request has none (`QHttpNetworkConnectionPrivate::prepareRequest()`), so the header cannot be
omitted. The server localises its `error` sentences through
`request.accept_languages.best_match()`, and the shipped image has a Russian catalogue
(`%(lang)s is not supported` → `%(lang)s не поддерживается`), so on a Russian-locale host the recorded
English body classifier saw localised text and a `400` carrying the key sentence degraded from `Auth`
to `BadResponse` — the `Done when` criterion failed exactly where it mattered. The user chose to pin
`Accept-Language: en` one line: the error sentences stay English, the body-first `Auth` mapping works
under every system locale, and the user's system locale no longer leaks to the server. Step 6, the
`translation()` test case, the `Done when` bullet, the `Accept-Language` risk and research §1.10 are
updated to this decision; the test asserts `en` instead of its absence, which is also non-vacuous on
this `ru_RU` host.

**Verification.** Container `qtranslate-trixie` (Qt 6.8.2), Release: full build with 0 warnings and
50/50. Host Debug and Release: both build with 0 warnings; `xvfb-run -a ctest --test-dir
build/linux-release` 50/50. The suite grew from 48 to 50 Linux targets; `tst_libretranslateparser` has
31 cases and `tst_libretranslateservice` 39. `.ts` are untouched at 376 messages and `tst_translations`
is green. Windows is reviewed by diff only: its 42 tests (40 + the two platform-neutral targets) are
recorded as not performed, there is no MSVC on this machine. In the container `xvfb-run` hangs after
`ctest` finishes (a lingering `Xvfb` as PID 1), so the container run used a manually started `Xvfb`
with `DISPLAY`; the host run used the documented `xvfb-run -a ctest`.

**Left for `02-libretranslate-settings-ui`.** `configurationError()` introduces a user-visible `tr()`
string (`LibreTranslate needs the base URL of a LibreTranslate server.`); it is deliberately not in
`i18n/` yet, so the `lupdate`/`lrelease` pass of the settings task must pick it up together with the
new settings strings — the `.ts` suites stay green in the meantime because `tst_translations` reads the
existing files only. That task also owns `LibreTranslateSettings`, the `[libretranslate] baseUrl` and
`apiKey` keys with secret storage, the shared `isValidServiceBaseUrl()`/`normalizedServiceBaseUrl()`
helpers (this service duplicates the Mozhi predicate on purpose, so the later switch changes no
behaviour), the call to `setBaseUrl()`/`setApiKey()` and the settings group.

**Not performed.** A live smoke test against a real server: no server is part of the task, and the
fixtures plus the local `QTcpServer` are the automated gate. `doc/` outside `doc/roadmap/` is
untouched — `doc/free-endpoints.md`, `doc/architecture.md`, both READMEs, `AGENTS.md` and
`doc/handoff.md` are the lead's documentation pass after stage 11; the roadmap bookkeeping (this plan,
the journal, the stage status and research §1.10) ships with this task.

