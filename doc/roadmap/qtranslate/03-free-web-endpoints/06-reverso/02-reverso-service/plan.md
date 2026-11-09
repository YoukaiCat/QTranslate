---
kind: task
status: done
detailed: true
depends: [01-reverso-parser]
---

# Reverso service

## Task

Add `ReversoService` on top of `01-reverso-parser`, register it in `AppController` behind the
free-web gate, and cover the JSON `POST` protocol with a local `QTcpServer` test. This task
implements the network behaviour, the 600 ms pause, the 5000 UTF-16-code-unit chunks and the error
classification. It adds no settings group, translations or documentation; those are
`03-reverso-settings` and `04-reverso-docs`.

The protocol is `doc/free-endpoints.md` §4, checked against the upstream `reverso-services` plugin
(`ReversoClient.kt`, `ReversoTranslatorService.kt`, branch `develop`, 2026-11-09): `POST
https://cps.reverso.net/api2/TranslateText` with the JSON object `source`, `direction`
(`<source>-<target>`), `deviceId` (`"0"`), `uiLang` (`"en"`), `origin` (`"chromeextension"`),
`accessToken` (`""`) and `appId` (`"0"`). The response is the flat object that `ReversoParser`
already reads, so the service only sends, paces, classifies and joins chunks. Reverso is a niche
fallback with six languages and no automatic detection, so there is no key, no per-service URL and
no `AppSettings`/INI field.

## Steps

1. **Add the service class and register it in `qtranslate_core`.** Create
   `src/services/reversoservice.h/.cpp` with
   `class ReversoService : public QueuedWebService`:

   - a constructor `explicit ReversoService(NetworkManager *network, QObject *parent = nullptr)`
     that stores `m_network`, sets `m_endpoint = defaultEndpoint()` and calls
     `setRateLimitMs(kDefaultRateLimitMs)`;
   - overrides `id()` (`reverso`), `displayName()` (`Reverso`), `isConfigured()` (`true`),
     `requiresOptIn()` (`true`), `sourceLanguages()`/`targetLanguages()` through
     `Languages::supported(kServiceId, Languages::Direction::Source)` and the `Target` call,
     `applySettings(const AppSettings &)` with an empty body and a comment that no Reverso setting
     affects the protocol, and `translate(const TranslationRequest &)`;
   - the test seam `void setEndpoint(const QUrl &url)`, `QUrl endpoint() const` and
     `static QUrl defaultEndpoint()` returning `https://cps.reverso.net/api2/TranslateText`;
     `setEndpoint()` falls back to the default when the URL is empty or invalid, like Yandex; the
     pause is changed through the inherited `setRateLimitMs()`, which the test uses as the only
     timing seam;
   - `static QString browserUserAgent()` returning a fixed Chrome UA string, duplicated here rather
     than borrowed from `BingWebService`/`YandexWebService`, so each service owns its identity;
   - `class Reply;` in the private section and `Q_LOGGING_CATEGORY(lcReverso,
     "qtranslate.service.reverso", QtWarningMsg)` in the `.cpp`.

   File-local constants and helpers in the `.cpp`: `kServiceId = "reverso"`,
   `kMaxChunkUnits = 5000`, `kDefaultRateLimitMs = 600`, each with the comment that the document
   gives no Reverso-specific value and the number is a named assumption, not a measurement. Copy
   the `Part` struct, `splitParts()` and `finishedWithError()` helpers from
   `yandexwebservice.cpp` (`TextChunker::split`, the edge whitespace kept out of the request and
   restored at the joints, an all-whitespace part never sent). Add
   `src/services/reversoservice.cpp/.h` to `qtranslate_core` in `CMakeLists.txt` after
   `src/services/reversoparser.*` and before `src/services/yandexwebparser.*`, so the list stays
   alphabetical. The header comment names the unofficial endpoint, the opt-in gate and the six
   languages.

2. **Implement `translate()`: validation, language mapping and chunking.** In
   `ReversoService::translate()`:

   - `request.text.trimmed().isEmpty()` returns `TranslationError::EmptyText` with an empty message
     before any request, using `finishedWithError()` so `serviceId` is already `reverso`;
   - `request.sourceLang == Languages::autoCode()` returns `UnsupportedLanguage` with exactly
     `Reverso needs an explicit source language; choose one instead of Auto-detect`; this message is
     user-visible through `TranslationErrorText::forResult()`, which appends `errorMessage` in
     parentheses, so keep the sentence verbatim;
   - map both codes with `Languages::toServiceCode(kServiceId, ..., Languages::Direction::Source)`
     and `...::Target`; if either is empty (for example `pt-BR`, `zh-Hans`, `uk`, `pl` or `xx`),
     return `UnsupportedLanguage` with `language "<code>" is not supported`, naming the unmapped
     side as the sibling services do;
   - build `const QString direction = source + QLatin1Char('-') + target;` and
     `const QList<Part> parts = splitParts(request.text);`; log only
     `Translating <n> part(s), <source> -> <target>` (`<direction>` is safe: internal codes, no
     text);
   - create one `Reply` with the copied endpoint and the parts, `enqueue(reply)` and return it. One
     text is one reply, however many chunks it has; the reply holds its queue slot until it
     finishes.

3. **Implement `Reply::start()` and the JSON `POST`.** The nested `Reply` behaves like
   `YandexWebService::Reply`/`BingWebService::Reply`:

   - `start()` starts `m_elapsed` and schedules `send()` through
     `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`, so `finished()` is never emitted from
     inside `translate()`;
   - `send()` returns when the reply is finished, fails with `Network` and the fixed
     `network is not available`/`the service is not available` when `m_network`/`m_service` is gone,
     and otherwise picks the first unsent non-empty part (`nextUnsentPart()`); no unsent part means
     `finishTranslation()`;
   - the request is `m_network->makeRequest(m_endpoint)`, then
     `Content-Type: application/json` through `QNetworkRequest::ContentTypeHeader`,
     `Accept: application/json` through `setRawHeader()`, the fixed
     `QNetworkRequest::UserAgentHeader = browserUserAgent()` **after** `makeRequest()` (which set
     `AppInfo::userAgent()`), and
     `QNetworkRequest::SameOriginRedirectPolicy`; set no cookie attribute and no `Cookie` header,
     unlike Bing, because the protocol needs neither;
   - the body is a `QJsonObject` with `source` (the chunk text), `direction`
     (`<source>-<target>`), `deviceId` (`"0"`), `uiLang` (`"en"`), `origin`
     (`"chromeextension"`), `accessToken` (`""`) and `appId` (`"0"`), serialized with
     `QJsonDocument(object).toJson(QJsonDocument::Compact)`. All seven values are JSON strings:
     `deviceId` and `appId` must be `"0"`, not `0`. The serializer writes UTF-8, so spaces and
     non-ASCII text stay readable inside the JSON string, and `QJsonObject` does not preserve
     insertion order, so neither the implementation nor the test may depend on byte order;
   - call `m_service->noteRequestSent()` immediately before `m_network->nam()->post(request, body)`,
     start `m_requestElapsed`, connect `QNetworkReply::finished` to `onFinished()`, and keep the
     endpoint in the reply, so a later `setEndpoint()` cannot move a request that is already in
     flight.

4. **Handle a 2xx body: append, pace and finish.** `onFinished()` takes the status from
   `HttpStatusCodeAttribute`, the `QNetworkReply::NetworkError` and the body (`readAll()` only when
   the transfer completed), calls `deleteLater()`, logs `HTTP <status> in <elapsed> ms` and then:

   - a timeout or `abort()`: `OperationCanceledError`/`TimeoutError` finishes with `Timeout` and
     the fixed `Reverso did not respond within <n> s` (format the value from
     `m_network->timeoutMs()` with `QString::number(seconds, 'g', 3)`, as the sibling services do);
     an explicit `abort()` has already finished the reply with `Cancelled` through
     `TranslationReply::abort()`, so the `isFinished()` guard must return before this branch is
     reported as a timeout;
   - 2xx: `ReversoParser::parseResponse(body)`; a `!result.ok()` fails the whole reply with
     `result.error`/`result.errorMessage`; on success append `translatedText` to `m_translations`,
     keep the first non-empty `detectedSourceLang`, mark the part sent, and either schedule the
     next chunk through `QTimer::singleShot(rateLimitMs(), this, ...)` or call
     `finishTranslation()` when no unsent part remains;
   - `finishTranslation()` counts the sent parts, returns `BadResponse` with
     `expected <n> translations, got <m>` when the count differs, and otherwise builds a
     `TranslationResult` with `serviceId = "reverso"` and the joined text: every sent part
     contributes `leading + translation + trailing`, every unsent (whitespace-only) part contributes
     its whole `leading` (the helper stores the entire part there), so the original formatting at
     the joints survives. Finish with `finish(result)`;
   - a failed chunk fails the whole reply: no partial translation and no further chunk. Do not keep
     the translations of earlier chunks in the result.

5. **Classify the failures and keep the log clean.** In the same `onFinished()`:

   - a non-2xx status first calls `ReversoParser::parseErrorBody(body)`; when it reports an error,
     `fail(parsed.error, parsed.errorMessage)`, otherwise
     `fail(ReversoParser::errorForHttpStatus(status), QStringLiteral("HTTP %1").arg(status))`.
     Never render the body or an HTML page;
   - any other `QNetworkReply::NetworkError` (closed port, TLS, proxy) uses
     `m_network->describeNetworkError(request, false)` or the fixed `Network error` when the network
     is gone; never `QNetworkReply::errorString()`, because the request body contains the text;
   - `fail()` builds a `TranslationResult` with `serviceId = "reverso"`, the error and
     `errorMessage`, and logs the error kind through a small file-local helper (short fixed
     literals such as `network`, `timeout`, `rate-limited`, `bad-response`, `unavailable`,
     `too-long`) with a fixed `Translation failed: ...` text. Do **not** log `errorMessage`: the
     parser may put a validated short provider `message` there, and that message could echo the
     source text. The log line has only the part count, the internal language codes, the safe
     `direction`, the HTTP status and the elapsed time; the text, the JSON body and the URL never
     appear in the log or in a message. `abortImpl()` logs `Cancelled` at debug level and calls
     `stopRequest()`; `stopRequest()` disconnects the reply, aborts it and deletes it later.

6. **Register the service in `AppController`.** In `src/app/appcontroller.h` add
   `class ReversoService;` to the forward-declaration block (after `OpenAiCompatibleService`) and
   `ReversoService *m_reverso = nullptr;` after `m_bingWeb`. In `src/app/appcontroller.cpp`:

   - add `#include "services/reversoservice.h"` to the services block (after
     `openaicompatibleservice.h`, before `yandexwebservice.h`);
   - construct `m_reverso = new ReversoService(m_network);` after `m_bingWeb` in
     `createTranslation()`, call `m_reverso->applySettings(settings);` with the other services, and
     call `m_translations->registerService(m_reverso);` after the Bing registration, so the pickers
     list it last;
   - in `applySettings(oldSettings, newSettings)` add
     `if (m_reverso) m_reverso->applySettings(newSettings);` after the `m_bingWeb` line;
   - do not add a cache reset (this service has no cache) and do not touch the gate, the picker
     filtering or the active-service switch: they are already generic and treat every
     `requiresOptIn()` service alike.

7. **Add `tst_reversoservice` on a local `QTcpServer`.** Create `tests/tst_reversoservice.cpp`
   by copying the `FakeHttpServer` from `tests/tst_mozhiwebservice.cpp` (it already has
   `method`, `url`, `headers`, `body`, `elapsedMs`, `respond` and `Location`). Include
   `core/appinfo.h`, `core/settings.h`, `core/textchunker.h`, `net/networkmanager.h`,
   `services/reversoservice.h`, the JSON, `QSignalSpy`, `QTcpServer`, `QTcpSocket` and
   `QLoggingCategory` headers, and declare `Q_DECLARE_METATYPE(TranslationError)`. Add a
   `translationResponse()` helper that builds the flat success object, the `longText()` helper
   (Cyrillic, emoji, paragraphs, more than 12000 UTF-16 units) and `verifyFixedMessage()`, which
   rejects an empty message and `TOPSECRET`, `<`, `{`, `}`, `://` and `127.0.0.1`. `init()` creates
   the server, `NetworkManager` and `ReversoService`, points
   `setEndpoint(QUrl("http://127.0.0.1:<port>/api2/TranslateText"))` and calls
   `setRateLimitMs(0)`; `cleanup()` resets all three. Cover:

   - `properties()`: `id() == "reverso"`, `displayName() == "Reverso"`, `isConfigured()`,
     `requiresOptIn()`, both language lists equal to `Languages::supported("reverso", ...)`, six
     entries each, no `auto`; `defaults()`: a fresh instance reports `rateLimitMs() == 600` and
     `endpoint() == defaultEndpoint()`, `setEndpoint()` with an empty and with an invalid URL falls
     back to the default, and `applySettings(AppSettings{})` changes neither;
   - `postsJson()`: one `POST` to `/api2/TranslateText`, `Content-Type: application/json`,
     `Accept: application/json`, the body parsed as JSON with all seven fields as strings
     (`source`, `direction`, `deviceId == "0"`, `uiLang == "en"`, `origin == "chromeextension"`,
     `accessToken == ""`, `appId == "0"`), the result's `translatedText`, `serviceId == "reverso"`
     and `detectedSourceLang` from `directionFrom`; the browser UA equals
     `ReversoService::browserUserAgent()` and differs from `AppInfo::userAgent()`, while a plain
     `makeRequest()` to the same server still sends `AppInfo::userAgent()` (the override is on this
     request only);
   - `utf8Body()`: text with spaces and non-ASCII (`Café — Москва 😀`) reaches the server as raw
     UTF-8 in the JSON string (no `\u` escapes, spaces are ordinary spaces) and parses back to the
     same text;
   - `allDirections()`: a data-driven test with 36 rows, including the four named examples `en-fr`,
     `fr-en`, `de-es` and `ru-it`; 36 requests, `direction == <mapped source>-<mapped target>`, and
     the `source` field equals the chunk text (`Hello`);
   - `rejectsAuto()` and `rejectsUnmappedLanguage()`: the exact `auto` message, the
     `language "xx" is not supported` message for an unmapped code, `UnsupportedLanguage`, and zero
     requests after `QTest::qWait(50)`;
   - `emptyText()` (data-driven empty/whitespace-only): `EmptyText` with zero requests;
   - `edgeWhitespace()`: `" \tHello\r\n "` is sent as `Hello` and the result is `" \tX\r\n "`; a
     long text whose middle part is whitespace-only never sends that part and still restores it;
   - `chunks()`: `TextChunker::split(text, 5000)` defines the expected parts; each request carries
     at most 5000 UTF-16 units, no chunk boundary splits `\r\n` or a surrogate pair, the request
     count equals the non-whitespace parts, and the joined `translatedText` equals the input text;
   - `pacesChunks()`: `setRateLimitMs(100)`, at least three chunks, every consecutive pair at least
     90 ms apart (lower bound only, as the sibling tests do);
   - `serializesRequests()`: two replies with `setRateLimitMs(200)`, the second request at least
     180 ms after the first;
   - `cancelsQueuedRequest()`: the first reply holds the slot (the handler does not answer), the
     second is aborted before its request, `Cancelled`, exactly one request;
   - `abortActive()`: abort the in-flight reply, `Cancelled`, exactly one request and no retry;
   - `failedMiddleChunk()`: the handler answers the first request and fails the second; the whole
     reply fails with the classified error, `translatedText` is empty, exactly two requests are
     made, and no third request arrives after `QTest::qWait(50)`;
   - `httpErrors()`: data-driven 429, 403, 503, 413, 400, a `success: false` body under 200, an
     HTML page under 200, a plain rate-limit body and a malformed/empty body; assert the error kind
     and `verifyFixedMessage()`;
   - `providerMessage()`: `success: false` with a short validated message reaches `errorMessage`
     verbatim under `ServiceUnavailable`; a message with `<html>`, `://`, a control character or
     more than 200 characters falls back to the fixed literal; `{"error":true,"success":true,
     "translation":"..."}` takes the error path and never returns the translation;
   - `timeout()`: `m_network->setTimeoutMs(300)` and a handler that never answers; `Timeout` with
     the `Reverso did not respond within 0.3 s` message;
   - `closedServerIsNetwork()`: `m_server->close()`; `Network` with the fixed `Network error`;
   - `crossOriginRedirectIsNotFollowed()`: a 302 to a second `FakeHttpServer`; the second server
     receives nothing, the reply is not ok, the translation is empty and the message is fixed;
   - `logsAreClean()`: the `LogCapture` pattern from `tests/tst_bingwebservice.cpp`
     (`qInstallMessageHandler` plus a filter rule for `qtranslate.service.reverso.debug`), one
     success and one `success: false` failure with `TOPSECRET` in the text and in the provider
     `message`; the captured lines contain neither sentinel, nor `://`, `<html`, `127.0.0.1` or the
     provider message, while the user-facing `errorMessage` of the validated short message is still
     the message.

   Finish with `QTEST_GUILESS_MAIN(TestReversoService)` and `#include "tst_reversoservice.moc"`.
   CTest never contacts the real endpoint.

8. **Register the test target.** Add `qtranslate_add_test(tst_reversoservice)` to
   `tests/CMakeLists.txt` immediately after `qtranslate_add_test(tst_reversoparser)` and before
   `tst_settings`, so the platform-neutral block stays alphabetical. Add no platform guard and no
   `Qt::Widgets`/`Qt::DBus` link, because the test talks to a local `QTcpServer` only.

9. **Build, count and verify.** Configure and build Linux Debug and Release with `-Wall -Wextra`
   (`cmake -S . -B build/linux-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug` and the same with
   `build/linux-release`/`Release`; `cmake --build`), then run
   `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`. Zero warnings in both
   configurations; `tst_reversoservice` is green, `tst_reversoparser` stays 40/40 and
   `tst_languages` 200/200. `ctest -N` must show 39 Linux tests (38 + the new target); Windows
   would be 34 (33 + 1). `git diff --stat` may show only
   `src/services/reversoservice.{h,cpp}`, `src/app/appcontroller.{h,cpp}`,
   `tests/tst_reversoservice.cpp`, `tests/CMakeLists.txt` and `CMakeLists.txt`. Do not touch the
   settings UI, `.ts`, `doc/`, `AGENTS.md`, the READMEs, `doc/handoff.md`, the root plan or
   `07-fallback-chain`. Windows Debug/Release and its 34 tests are recorded as not performed (no
   MSVC); commit and push stay with the coordinator/lead.

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
  chunks and the default 600 ms pause (asserted through `rateLimitMs()`, with the pacing itself
  checked at `setRateLimitMs(100)`), queue serialization and cancellation, the failed-middle-chunk
  rule, the classified errors, the parsed result, the cross-origin redirect refusal, and the
  message/log hygiene. The target is platform-neutral and registered after `tst_reversoparser`.
- Debug and Release build on Linux without warnings; `ctest -N` shows 39 Linux tests and would show
  34 on Windows; the full suite passes 39/39; Windows Debug/Release and its 34 tests are recorded as
  not performed (no MSVC).
- No settings UI, `.ts`, `doc/`, `AGENTS.md`, README, `doc/handoff.md`, root-plan or
  `07-fallback-chain` file is changed; commit and push are the coordinator/lead's.

## Risks

- The JSON object is order-free and the test must parse it. A byte comparison would pass on one Qt
  version and fail on the next, and it would hide a wrong field name behind a coincidental order.
  Assert the seven keys and their values, and assert `QJsonValue::isString()` for `deviceId` and
  `appId`; sending the numbers `0` instead of the strings `"0"` is the easiest mistake here.
- A failed chunk must fail the whole reply. Do not append the successful prefixes and do not send
  the next part after a failure; `failedMiddleChunk()` pins both the error and the exact request
  count. The translation-count check is defensive and may stay unreachable from the wire, because
  one successful response yields exactly one translation.
- The provider `message` may echo the source text. `fail()` therefore logs the error kind, not
  `errorMessage`, and the hygiene test captures the log. Do not "improve" the log by adding the
  message; that would defeat the stage's privacy rule and the stage acceptance.
- The 600 ms pause and the 5000 UTF-16-unit chunk are named assumptions. Keep them in the two
  constants, change them only after a measurement, and never add a retry storm: the queue already
  serializes requests per host.
- `QJsonDocument::toJson(Compact)` writes UTF-8, so a non-ASCII text stays readable; do not
  pre-encode it or escape it. A space is a normal space in the JSON string, unlike a form body.
- The timing tests are lower-bound only, with a ~10% tolerance, and the service must never be
  tested against the real endpoint. A live Reverso smoke is not part of this task; if one is run,
  record it as passed or as not performed with the reason, and do not work around a block with
  cookies or a captcha.
- The test file is large (the sibling service tests are 867-1519 lines) and is the context risk for
  the programmer. Keep the shared helpers small and the cases independent; if the file grows past
  that, report `DOES NOT FIT` instead of dropping the log-hygiene or failed-chunk case.

## Outcome

Done. `ReversoService` is registered in `AppController` behind the free-web gate and covered by
`tst_reversoservice` on a local `QTcpServer`; the full Linux suite passes 39/39 in Debug and
Release, and both configurations build without warnings. Windows/MSVC is unavailable on this
machine, so its build and its 34 tests are recorded as not performed.

- `src/services/reversoservice.{h,cpp}` derives from `QueuedWebService`, returns the internal
  `reverso` id and the display name `Reverso`, is configured and requires opt-in, and exposes the
  six Reverso languages without `auto`. It sends the seven-field JSON `POST` (all values strings,
  `deviceId` and `appId` as `"0"`), sets the browser User-Agent on its own requests only, keeps
  `SameOriginRedirectPolicy`, rejects `auto` and every unmapped code before any request, and splits
  at 5000 UTF-16 units with the edge whitespace restored at the joints. The pause is 600 ms by
  default; `setEndpoint()` and the inherited `setRateLimitMs()` are the only test seams.
- A failed chunk fails the whole reply without a partial translation, and `fail()` logs only the
  error kind, never `errorMessage`, because the validated provider `message` may echo the source
  text. The service is created after Bing, so the pickers list it last; the gate, the picker
  filtering and the active-service switch stay generic and were not touched.
- The plan described `source` as the mapped language code in steps 3 and 7, but §4 and the upstream
  plugin send the chunk text in `source` and the language pair in `direction`. The implementation
  followed the protocol; the plan wording was corrected here, with no production change.
- `tst_reversoservice` runs 69 cases: the properties/defaults, the JSON request and the browser UA
  on this request only, the UTF-8 body, all 36 directions, the `auto`/unmapped/empty rejections
  with zero requests, edge whitespace, the 5000-unit chunks with a `\r\n` fixture, the 100 ms
  pacing, queue serialization, queued and active cancellation, the failed middle chunk, the
  classified HTTP and body errors, the provider message, the timeout, the closed server, the
  cross-origin redirect refusal and the log/message hygiene; the acceptance added the explicit cookie
  isolation (manual cookie attributes, no jar read/write).
- Found while working and fixed in this task's review: `chunks()` checked that a chunk boundary
  never splits `\r\n`, but `longText()` had no `\r`; the fixture now uses `\r\n\r\n` paragraph
  breaks and asserts that it contains CRLF, so the check is no longer vacuous. The `pacesChunks()`
  count comparison with `TextChunker::split()` was left as is: the fixture has no all-whitespace
  chunk, so it is stable; changing the fixture to a long whitespace run would make it fragile.
- Known gaps: the translation-count branch is defensive and unreachable from the wire; the
  `m_service`/`m_network` destruction guards have no dedicated test; the Reverso-specific gate
  scenario relies on the shared `TranslationManager` tests. A live `cps.reverso.net` smoke was not
  run (the endpoint is never contacted); Windows Debug/Release and its 34 tests were not performed.
  No settings group, `.ts`, `doc/free-endpoints.md`, README, `doc/handoff.md`, root plan or
  `07-fallback-chain` file was changed; those belong to `03-reverso-settings` and `04-reverso-docs`.
