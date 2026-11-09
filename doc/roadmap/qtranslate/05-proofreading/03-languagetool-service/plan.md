---
kind: task
status: done
detailed: true
depends: [01-proofreading-core, 02-languagetool-parser]
---

# LanguageTool service

## Task

Add `LanguageToolService` and wire it into `AppController` through the `ProofreadingManager` from
`01-proofreading-core`. This is the first real provider: it checks text with the public LanguageTool
API or a self-hosted server, without any credentials, splits long text at the documented
20 000-character request limit, shifts the chunk offsets, filters the issues by the requested
categories, cancels cleanly and maps every error with a fixed message. The settings tab, the `.ts`
strings and the main-window UI are `04-text-checking-tab` and `05-main-window-check-and-replace`.

The service is a `ProofreadingService`, not a `TranslationService`, so it cannot derive from
`QueuedWebService` (that base is a `TranslationService` whose `Reply` is a `TranslationReply`). Step 1
therefore extracts the accepted per-host queue into a small `RequestQueue` that both bases use, with
no behaviour change for the free web services. The service then owns one `RequestQueue`, keeps its
`ProofreadingReply` in one slot and paces every request with `noteRequestSent()`, exactly like the
free services do (`src/services/queuedwebservice.cpp`, `src/services/mozhiservice.cpp:159-273`).

Interface: `id()` is `languagetool`, `displayName()` is `LanguageTool`, `isConfigured()` is true
(the public endpoint needs no credentials and no key is stored or sent), `isLocal()` is false,
`supportsAutoLanguage()` is **true**, and `languages()` is
`Languages::supported("languagetool", Languages::Direction::Source)` from
`02-languagetool-parser` (the explicit codes, without `auto`). `auto` is a separate request value:
when `request.language == Languages::autoCode()`, the service sends `language=auto` and does not map
it through `toServiceCode()`; `ProofreadingManager::check()` lets it through because
`supportsAutoLanguage()` is true (`src/core/proofreadingmanager.cpp:161-167`). The detector may pick
any language and the response carries it in `language.detectedLanguage`, but the service ignores it
because `ProofreadingResult` has no detected-language field. A failed detection (an HTTP 400 or a
body that is not a check response) is a `BadResponse` with a fixed message and never surfaces the
body. `04-text-checking-tab` will offer "Auto-detect" for this provider.

`applySettings()` reads `proofreading.endpoint` only. An empty endpoint means the default
`https://api.languagetool.org/v2/check`; an invalid value also falls back to the default. The
constructor and `applySettings()` set the pause explicitly:
`kPublicRateLimitMs = 3000` when the effective host is `api.languagetool.org` (the public limit is
about 20 requests/min) and `kCustomRateLimitMs = 500` for any other (self-hosted) host, so the 2000
ms base default is never silently inherited and a self-hosted server is not stalled for 3 s. The
75 000 characters/min limit is not modelled by a sliding window: the app caps one request at 20 000
characters (the stricter per-request limit), a long multi-chunk check can still receive 429 and is
mapped to `RateLimited`, and the self-hosted URL is the remedy. `setEndpoint(const QUrl &)` is the
test seam and sets only the URL; `setRateLimitMs()`/`rateLimitMs()` delegate to the queue for tests
(the test applies settings first and then sets 0).

The request is `POST <endpoint>` with `Content-Type: application/x-www-form-urlencoded` and
`Accept: application/json`; the body carries exactly `text`, `language` (the mapped LanguageTool
code) and `enabledOnly=false`, and nothing else — no `apiKey`, no `username`, no `Authorization`, no
cookie and no other credential (user decision of 2026-11-09: premium LanguageTool is out of stage 5;
the public API rejects credentials and can echo a submitted value in a plain-text 400). Both names
and values are percent-encoded as UTF-8 with `QUrl::toPercentEncoding`, so a space becomes `%20` (a
literal `+` is never a space), and `&`, `=`, `+`, `%` and non-ASCII text cannot break the body. The
request uses `NetworkManager::makeRequest()` (timeout and User-Agent), `SameOriginRedirectPolicy` so
a redirect cannot silently change the recipient, and manual cookie load/save attributes so nothing
from the shared jar is sent to the checker.

The text is split with `TextChunker::splitWithOffsets(text, 20000)`; the parts are sent sequentially
inside one reply with `noteRequestSent()` pacing and `rateLimitMs()` between chunks, every issue's
`offset` is shifted by `part.offset`, issues whose `category` is not in `request.categories`
(empty = all) are dropped, and the merged list is sorted by `offset`/`length`/`ruleId` with exact
duplicates removed. The category filter is client-side and not LanguageTool's `enabledCategories`:
our four buckets map many-to-one onto LanguageTool's category ids (`punctuation` covers
`TYPOGRAPHY` and `PUNCTUATION`, `style` covers `CASING`/`REDUNDANCY`/…), and the parser already maps
`issueType`/`category.id`, so filtering on the server would need the same table twice and would
still miss issue-type-only matches. A failed chunk fails the whole reply with no partial issue list;
a cancelled or aborted reply stops the active request and finishes with `Cancelled`.

## Steps

1. **Extract the per-host queue into `src/services/requestqueue.{h,cpp}`.** The accepted
   `QueuedWebService` queue is typed to `TranslationReply`, so a proofreading service cannot use it:
   - `class RequestQueue : public QObject` with `setRateLimitMs(int)`, `rateLimitMs()`, and
     `void enqueue(QObject *owner, std::function<bool()> isDone, std::function<void()> start)`;
     store `struct Task { QPointer<QObject> owner; std::function<bool()> isDone;
     std::function<void()> start; }` in `QList<Task>`, plus `QPointer<QObject> m_active`,
     `QElapsedTimer m_lastRequest`, `QTimer *m_timer` and `int m_rateLimitMs = 2000`.
     `enqueue()` appends and calls `schedule()`; `schedule()` drops tasks whose `owner` is null or
     `isDone()` returns true, waits for the remaining pause, then `startNext()`; `startNext()` sets
     `m_active`, connects `owner`'s `destroyed` to clear the slot and schedule, and calls `start()`;
     `noteRequestSent()` restarts the timer. Add `void taskFinished(QObject *owner)` that clears the
     slot only when `owner == m_active` and then calls `schedule()`. Never call `isDone()` before the
     `QPointer` check (the lambda captures a raw reply pointer).
   - `src/services/queuedwebservice.{h,cpp}`: keep the public API (`Reply`, `setRateLimitMs`,
     `rateLimitMs`, `noteRequestSent`, `enqueue`, `enqueue(Reply *)`) and delegate to a
     `RequestQueue *m_queue`; move the queue members, `schedule()` and `startNext()` out. In
     `enqueue(Reply *)` connect `reply->finished` to `m_queue->taskFinished(reply)` and call
     `m_queue->enqueue(reply, [reply]{ return reply->isFinished(); }, [reply]{ reply->start(); })`.
     No other service changes.
   - Add both files to `qtranslate_core` in `CMakeLists.txt` after `queuedwebservice.*`.
   - The regression gate is the unchanged `tst_queuedwebservice` plus `tst_deeplwebservice`,
     `tst_deeplservice`, `tst_mymemoryservice`, `tst_mozhiwebservice`, `tst_yandexwebservice`,
     `tst_bingwebservice` and `tst_reversoservice`; they must pass without edits. If any timing or
     queue behaviour changes, stop and report instead of adjusting the tests.

2. **Add `src/services/languagetoolservice.{h,cpp}` and list it in `qtranslate_core`.**
   - Header: `class LanguageToolService : public ProofreadingService` with the overrides from the
     task text, the test seam `void setEndpoint(const QUrl &)`, `QUrl endpoint() const`,
     `static QUrl defaultEndpoint()`, `void setRateLimitMs(int)`, `int rateLimitMs() const`, and a
     nested `class Reply`. Private members: `NetworkManager *m_network`, `RequestQueue *m_queue`,
     `QUrl m_endpoint`, and the helpers `void enqueue(Reply *)`, `void noteRequestSent()`; the
     nested `Reply` may call them (a nested class has access to the enclosing class's privates).
     Use file-local `constexpr` `kServiceId = "languagetool"`,
     `kPublicRateLimitMs = 3000`, `kCustomRateLimitMs = 500`, `kMaxChunkUnits = 20000`, and
     `kPublicHost = "api.languagetool.org"`.
   - Constructor: `new RequestQueue(this)`, `m_endpoint = defaultEndpoint()`,
     `m_queue->setRateLimitMs(kPublicRateLimitMs)`.
   - `defaultEndpoint()` returns `QUrl(QStringLiteral("https://api.languagetool.org/v2/check"))`.
   - `applySettings(const AppSettings &)`: `const QUrl configured(settings.proofreading.endpoint)`;
     use it when it is a valid absolute `http`/`https` URL with a host, otherwise `defaultEndpoint()`;
     set `m_queue->setRateLimitMs(m_endpoint.host().compare(kPublicHost, Qt::CaseInsensitive) == 0
     ? kPublicRateLimitMs : kCustomRateLimitMs)`. Do not abort an active reply; it keeps its
     captured endpoint.
   - `setEndpoint()` sets only the URL with the same fallback; `setRateLimitMs`/`rateLimitMs`
     delegate to `m_queue`.
   - Add `src/services/languagetoolservice.cpp/.h` to `qtranslate_core` in `CMakeLists.txt` after
     `languagetoolparser.*` (line 63), before `mozhiparser.*`.

3. **Implement `check()` and the pre-network checks.**
   - Empty or whitespace-only text → `ProofreadingReply::createFinished()` with `serviceId =
     "languagetool"` and `EmptyText` (the manager already refuses it; the service must not crash
     when called directly).
   - `request.language == Languages::autoCode()` → the wire language value `auto`, without calling
     `toServiceCode()` and without requiring the code to be in `languages()`; pass the literal
     `auto` to the reply so the body carries `language=auto`.
   - A concrete language outside `Languages::supported(kServiceId, Direction::Source)` → the
     `UnsupportedLanguage` error with the internal code. Otherwise map with
     `Languages::toServiceCode(kServiceId, request.language, Direction::Source)`; an empty result is
     the same error and makes no request.
   - `TextChunker::splitWithOffsets(text, kMaxChunkUnits)`; an empty list is `EmptyText`. Copy the
     endpoint and the `request.categories` into the reply (empty = all four).
   - Create `Reply(parts, languageCode, categories, endpoint)`, connect its `finished` to
     `m_queue->taskFinished(reply)` and call
     `m_queue->enqueue(reply, [reply]{ return reply->isFinished(); }, [reply]{ reply->start(); })`,
     exactly as `QueuedWebService::enqueue` does, then return it. Log only the part count, the
     internal language code and the number of requested categories.

4. **Implement `Reply`: serialization, request body, pacing and cancellation.**
   - `start()` posts `send()` with `QMetaObject::invokeMethod(this, [this]{ send(); },
     Qt::QueuedConnection)`, so no error can emit `finished()` from inside `check()`.
   - `send()`: return immediately when `isFinished()` (the reply can be aborted before the queued
     call runs); if `m_service` is null → `fail(Network, "the service is not available")`; if
     `m_network` is null → `fail(Network, "network is not available")`; if every part is sent →
     `finishCheck()`.
   - Build the body with a `field(name, value)` helper:
     `text=<percent-encoded part>`, `language=<percent-encoded mapped code>`, `enabledOnly=false`
     joined with `&`. `QUrl::toPercentEncoding` for every value; never `QUrlQuery`/`+` encoding.
   - Request: `m_network->makeRequest(endpoint)`, `Content-Type:
     application/x-www-form-urlencoded`, `Accept: application/json`,
     `RedirectPolicyAttribute = SameOriginRedirectPolicy`,
     `CookieLoadControlAttribute = Manual`, `CookieSaveControlAttribute = Manual`; no
     `Authorization` and no `Cookie` header. Call `m_service->noteRequestSent()` immediately before
     `m_network->nam()->post(request, body)` and store the `QNetworkReply`.
   - `abortImpl()` (and the destructor) disconnects, aborts and `deleteLater()`s the active reply;
     it must stay silent because `abort()` has already finished the result as `Cancelled`.
   - Guard `m_service` in every callback: a service destroyed while the request is in flight makes
     the reply fail with `Network`, never a null dereference.

5. **Implement the response handling, chunk merge and the category filter.**
   - `onFinished()`: take `m_request` locally, clear it, read the status and the body (only when
     `status != 0` or `NoError`, as `mozhiservice.cpp:203-215`), `deleteLater()` and return if the
     reply is already finished.
   - Timeout/`OperationCanceledError` → `Timeout` with
     `LanguageTool did not respond within %1 s` using `m_network->timeoutMs()`; a non-2xx status →
     `LanguageToolParser::errorForHttpStatus(status)` with a fixed message per class:
     `Auth` → "the endpoint rejected the request", `TooLong` → "the text is too long for
     LanguageTool", `RateLimited` → "the endpoint reported a rate limit", `Network` → "the
     LanguageTool server failed", anything else → `HTTP %1` (status only). Never use the body, the
     URL or `errorString()` in a message. Other network errors → `Network` with
     `m_network->describeNetworkError(request, false)` or `Network error`.
   - A 2xx body: `LanguageToolParser::parseResponse(body, int(part.text.size()))`. On error finish
     the whole reply with that error/message and no issues. On success, for every issue apply the
     category filter (`categories.isEmpty() || categories.contains(issue.category)`), add
     `part.offset` to `offset`, append it, advance to the next part and schedule the next `send()`
     after `m_service->rateLimitMs()`.
   - `finishCheck()`: sort by `offset`, then `length`, then `ruleId`; remove exact duplicates
     (same `offset`, `length`, `ruleId`, `message`); build the `ProofreadingResult` with
     `serviceId = "languagetool"` and no error. Log the chunk count, the issue count and the elapsed
     time at debug level, and the HTTP status/error class at warning level; never the text, a
     replacement, the URL or the body (`Q_LOGGING_CATEGORY(lcLanguageTool,
     "qtranslate.service.languagetool", QtWarningMsg)`).

6. **Register the service in `AppController`.**
   - `src/app/appcontroller.h`: forward-declare `class LanguageToolService;` and
     `class ProofreadingManager;`, add `void createProofreading();` and the members
     `ProofreadingManager *m_proofreading = nullptr; LanguageToolService *m_languageTool = nullptr;`
     after `m_reverso`.
   - `src/app/appcontroller.cpp`: include `core/proofreadingmanager.h` and
     `services/languagetoolservice.h`; call `createProofreading()` in `start()` right after
     `createTranslation()` (`appcontroller.cpp:109`). The new method constructs
     `m_proofreading = new ProofreadingManager(this)`, sets
     `m_proofreading->setOnlineServicesAllowed(settings.proofreading.allowOnlineServices)`, creates
     `m_languageTool = new LanguageToolService(m_network)`, calls
     `m_languageTool->applySettings(settings)`, registers it, and calls
     `m_proofreading->setActiveService(settings.proofreading.provider)` (an unknown id is ignored by
     the manager with its fixed warning; the settings normalise to `languagetool`).
   - In `applySettings()` (`appcontroller.cpp:542`): call `m_languageTool->applySettings(newSettings)`
     when it exists, `m_proofreading->setOnlineServicesAllowed(...)` on every change, and
     `setActiveService()` when `proofreading.provider` changed. Do not clear a translation cache and
     do not touch any translation setting.
   - In `prepareForQuit()` (`appcontroller.cpp:1112`): `if (m_proofreading)
     m_proofreading->cancelAll();`.

7. **Add `tests/tst_languagetoolservice.cpp`** and register it in `tests/CMakeLists.txt` after
   `tst_languagetoolparser` (line 35), before `tst_mozhiparser` (line 36); platform-neutral.
   - Copy the `FakeHttpServer` from `tests/tst_mozhiwebservice.cpp:23-120` (it already reads the POST
     body and the `Request` carries `method`, `url`, `headers`, `body`), the `formValue` helper and
     the `verifyFixedMessage` style. Build responses inline with a `matchJson()` helper so no fixture
     files are needed.
   - `init()` creates the server, `NetworkManager` and the service; build an `AppSettings` with
     `proofreading.endpoint = "http://127.0.0.1:<port>/v2/check"`, call `applySettings()`, then
     `setRateLimitMs(0)` and `setTimeoutMs()` on the manager as needed; `cleanup()` resets all.
   - Properties and defaults: id/display name, `isConfigured() == true`, `isLocal() == false`,
     `supportsAutoLanguage() == true`, a non-empty `languages()` without `auto`, `defaultEndpoint()`,
     `endpoint()` after `setEndpoint()`, an empty/invalid `setEndpoint()` falling back to the default,
     and the pause: a fresh service is 3000 ms, `applySettings()` with the local endpoint is 500 ms,
     with an empty endpoint is 3000 ms.
   - Request: method `POST`, path `/v2/check`, `Content-Type:
     application/x-www-form-urlencoded`, `Accept: application/json`, no `Authorization`/`Cookie`
     header, form keys exactly in the order `text`, `language`, `enabledOnly` with `false`, and no
     `apiKey`/`username`; UTF-8 percent-encoding of a space, Cyrillic, an emoji, `&`, `=`, `+`, `%`
     (no raw space or `+`); mapped codes `ru`→`ru-RU`, `en`→`en-US`, `de`→`de-DE`,
     `zh-Hans`→`zh-CN`, `fil`→`tl-PH`, `pt-PT`→`pt-PT`; `zh-Hant`, `hi` and an unknown code are
     `UnsupportedLanguage` with zero requests; empty text is `EmptyText` with zero requests.
   - `auto`: a request with `Languages::autoCode()` succeeds with a 200 response, the wire `language`
     field is exactly `auto` (not a mapped code), the issues come through, and no detected language is
     exposed; a 400 (or a non-object/malformed body) for an `auto` request is `BadResponse` with a
     fixed message that contains neither the body nor the text — the auto-detection failure path.
   - Chunks: a text that `TextChunker::splitWithOffsets(text, 20000)` splits into three parts (for
     example 45 000 characters of short sentences); assert one request per part, each `text` field
     ≤20 000 units and not split inside a surrogate pair, the joined wire text equals the input, the
     shifted offsets equal the expected `part.offset` values and are sorted; a response that repeats
     the same issue produces one issue after de-duplication; a response issue outside the chunk
     length is `BadResponse`; a 500 on the second chunk fails the whole reply with no issues and no
     third request; with `setRateLimitMs(100)` every inter-chunk delta is at least 90 ms.
   - Categories: a response with one issue of each category and a request for
     `{spelling, punctuation}` keeps exactly those, an empty request list keeps all four, and the
     body contains no `enabledCategories` field.
   - Errors: rows for 400, 401, 403, 413, 429, 500 and 503 with the expected `TranslationError` and
     a fixed message; a 200 HTML body is `ServiceUnavailable`; a malformed or non-object 200 body is
     `BadResponse`; a timeout with `m_network->setTimeoutMs(300)` and a handler that never answers;
     a closed server is `Network`; a cross-origin redirect leaves the other server with zero
     requests and the reply not ok.
   - Queue and cancellation: with a 100 ms pause and a slow first reply, the second check starts
     after the first finishes; a queued check aborted before it starts sends nothing; an active
     check aborted finishes with `Cancelled` and stops the request; a queued check deleted before it
     starts does not crash and does not send.
   - Hygiene: `TOPSECRET` in the text and in `context.text`; every error message passes
     `verifyFixedMessage()` (no `SECRET`, `<`, `://`), no message or collected log line contains the
     text, a replacement, the URL or the body; assert the request carries no credential field.

8. **Build, count and verify.** Configure and build Linux Debug and Release with `-Wall -Wextra`,
   run `tst_languagetoolservice`, then `tst_queuedwebservice`, `tst_deeplwebservice`,
   `tst_mymemoryservice`, `tst_mozhiwebservice`, `tst_yandexwebservice`, `tst_bingwebservice`,
   `tst_reversoservice`, and finally `xvfb-run -a ctest --test-dir build/linux-release
   --output-on-failure`. `ctest -N` must show 42 Linux tests (41 + the new platform-neutral target);
   Windows would be 37 (36 + 1) and is recorded as not performed (no MSVC).
   `grep -c '<message' i18n/qtranslate_ru_RU.ts` must stay 316: the service adds no `tr()` string.
   `git diff --stat` may show only `src/services/requestqueue.{h,cpp}`,
   `src/services/queuedwebservice.{h,cpp}`, `src/services/languagetoolservice.{h,cpp}`,
   `src/app/appcontroller.{h,cpp}`, `CMakeLists.txt`, `tests/tst_languagetoolservice.cpp` and
   `tests/CMakeLists.txt`; no settings, UI, `.ts`, documentation, README, `doc/handoff.md` or
   later-child file is touched. Leave commit and push to the lead.

## Done when

- `RequestQueue` exists in `qtranslate_core` and owns the serialization, the pause and the
  `noteRequestSent()` clock; `QueuedWebService` keeps its public API and delegates to it;
  `tst_queuedwebservice` is unchanged and green, and all six free-web service tests
  (`tst_deeplwebservice`, `tst_mymemoryservice`, `tst_mozhiwebservice`, `tst_yandexwebservice`,
  `tst_bingwebservice`, `tst_reversoservice`) plus `tst_deeplservice` stay green, so the extraction
  changed no behaviour.
- `src/services/languagetoolservice.{h,cpp}` is in `qtranslate_core` after
  `languagetoolparser.*`, derives from `ProofreadingService`, and returns id `languagetool`,
  display name `LanguageTool`, `isConfigured() == true`, `isLocal() == false`,
  `supportsAutoLanguage() == true`, a non-empty `Languages::supported("languagetool", Source)` list
  without `auto`, the default endpoint and a working `setEndpoint()` fallback.
- `applySettings()` reads only `proofreading.endpoint` (empty/invalid → the default), sets the queue
  pause to 3000 ms for `api.languagetool.org` and 500 ms for any other host, and does not abort an
  active reply or touch another setting.
- `check()` refuses empty text and unsupported explicit languages with `EmptyText`/
  `UnsupportedLanguage` before any request, accepts `auto` (sent as `language=auto`, not mapped and
  not required to be in `languages()`), and returns a caller-owned `ProofreadingReply` for a valid
  request.
- The request is a `POST` to the configured endpoint with `Content-Type:
  application/x-www-form-urlencoded`, `Accept: application/json`, `SameOriginRedirectPolicy`, manual
  cookie attributes, and a body with exactly `text`, `language`, `enabledOnly=false` — no
  `apiKey`, `username`, `Authorization`, cookie or other credential — with every name and value
  percent-encoded as UTF-8 (`%20` for a space, never `+`).
- An `auto` check succeeds on a normal 200 response with `language=auto` on the wire and the issues
  intact; a failed auto-detection (an HTTP 400 or a body that is not a check response) is
  `BadResponse` with a fixed message, no body and no text in the message or the log. The detected
  language is ignored because `ProofreadingResult` has no field for it.
- Long text is split at 20 000 UTF-16 units with `TextChunker::splitWithOffsets`, sent one part at a
  time inside one queue slot with `noteRequestSent()`/`rateLimitMs()` pacing, every issue's offset is
  shifted by `part.offset`, the merged list is sorted by `offset`/`length`/`ruleId` with exact
  duplicates removed, no offset is outside the text, and a failed middle part fails the whole reply
  without a partial issue list.
- The category filter is client-side, applies "empty = all four", keeps only the requested
  `spelling`/`grammar`/`punctuation`/`style` values, and never changes an offset; the body has no
  `enabledCategories`.
- Errors: HTTP 401/403 → `Auth` with "the endpoint rejected the request", 413 → `TooLong`, 429 →
  `RateLimited`, 5xx → `Network`, 400 and other statuses → `BadResponse` with `HTTP %1`, an HTML 200
  body → `ServiceUnavailable`, malformed JSON → `BadResponse`, a timeout → `Timeout` with the
  configured seconds, a closed server → `Network`, and a cross-origin redirect is refused without a
  request to the other host. No message or log line contains the text, an issue message, a
  replacement, the URL or a response body; the body of a 400 is never surfaced.
- `abort()` finishes with `Cancelled` and stops the active request; a queued reply aborted or deleted
  before it starts sends nothing; a service destroyed mid-request makes the reply fail with
  `Network`, not a crash.
- `AppController` owns a `ProofreadingManager`, creates and registers `LanguageToolService`, applies
  its settings on startup and on every change, keeps `setOnlineServicesAllowed()` in sync with
  `proofreading/allowOnlineServices`, switches the active provider on a `provider` change, and calls
  `cancelAll()` on shutdown; no translation setting or service is changed.
- `tests/tst_languagetoolservice.cpp` is new, platform-neutral, registered after
  `tst_languagetoolparser`, and covers properties/defaults/pauses, the request shape and encoding,
  the language mapping, the `auto` request and its failure path, the explicit-language rejections,
  chunks with the offset shift and de-duplication, the category filter, every error above, timeout,
  abort/cancel, queue serialization and the `TOPSECRET` message/log hygiene. `ctest -N` shows 42
  Linux tests (41 + 1) and would show 37 on
  Windows (36 + 1); Debug and Release build on Linux without `-Wall -Wextra` warnings and the full
  suite passes 42/42; Windows Debug/Release and its 37 tests are recorded as not performed (no MSVC).
- No settings, UI, `.ts` (316 messages stay), documentation, README, `doc/handoff.md` or later-child
  file changes; no commit.

## Depends on

`01-proofreading-core` for the manager, the reply and the settings group; `02-languagetool-parser`
for the response parser, `errorForHttpStatus()` and the language codes.

## Risks

- **The queue extraction touches an accepted base.** `QueuedWebService` is used by six free services;
  the extraction must be mechanical (same members, same order of `schedule()`/`startNext()`, same
  `finished`/`destroyed` handling). The unchanged `tst_queuedwebservice` and every `tst_*webservice`
  are the gate. If a test fails, the extraction is wrong — do not edit the tests to match it.
- **`std::function<bool()>` captures a raw reply pointer.** Check the `QPointer` before calling
  `isDone()` and `start()`; a reply deleted while queued must be skipped, not dereferenced.
  `taskFinished()` must only clear the slot when the owner is the active task, so a stale
  `finished()` cannot free a newer slot.
- **`auto` is supported but its result is not exposed.** LanguageTool detects a language and returns
  it in `language.detectedLanguage`; `ProofreadingResult` has no detected-language field, so the
  service ignores it and must not add one in this task. `auto` is a special value handled before the
  `Languages::supported()` check, while an explicit code still has to come from that list. A failed
  detection is an HTTP 400 or a body that is not a check response and lands in `BadResponse` with a
  fixed message — never surface that body.
- **The 75 000 characters/min limit is not modelled.** The 3000 ms public pause follows the request
  count; a multi-chunk check of long text can still receive 429, which is a clear `RateLimited`
  error, and the self-hosted URL is the remedy. Do not add a sliding-window limiter in this task.
- **The public-host check is a host comparison.** Only `api.languagetool.org` gets 3000 ms; a mirror
  or proxy of the public API gets 500 ms. That is a deliberate simplification; a user who points at
  a shared mirror can raise the pause only by editing the code, which is acceptable for v1.
- **`Auth` without credentials.** 401/403 is kept as `Auth` for a server that rejects the anonymous
  request (permission or block); the message is "the endpoint rejected the request", not a key hint.
  If a live response shows a different cause, change the message/status table in one place.
- **The response body can echo a submitted value** (observed live for a credential attempt). No
  credentials are sent now, but a proxy or a future server could put anything in a plain-text 400;
  never copy a body into a message or a log line, and keep the parser's fixed literals.
- **Offsets are UTF-16 and chunk-local.** The parser validates each issue against the chunk length;
  the service adds `part.offset` and must not clamp. The parser's `int(textLength)` cast is safe
  because a chunk is at most 20 000 units.
- **Timing tests are the flake risk.** Use `setRateLimitMs(0)` everywhere except the pacing case,
  assert lower bounds with a tolerance, and give `QTRY_*` generous timeouts.
- **Windows is not built here.** Record the count and the build as not performed, never as passed.

## Outcome

Done. `LanguageToolService` is the first real `ProofreadingService`, registered in `AppController`
through `ProofreadingManager`, and the accepted per-host queue is shared through `RequestQueue`;
`tst_languagetoolservice` is green.

- Files: new `src/services/requestqueue.{h,cpp}`, `src/services/languagetoolservice.{h,cpp}` and
  `tests/tst_languagetoolservice.cpp`; modified `src/services/queuedwebservice.{h,cpp}`,
  `src/app/appcontroller.{h,cpp}`, `CMakeLists.txt` and `tests/CMakeLists.txt`. No settings, UI,
  `.ts`, documentation, README, `doc/handoff.md` or later-child file changed;
  `grep -c '<message' i18n/qtranslate_ru_RU.ts` stayed 316.
- `RequestQueue` owns the serialization, the `noteRequestSent()` clock, the pause and the
  `finished`/`destroyed` handling, with the `Task { QPointer owner, isDone, start }` shape and the
  default 2000 ms pause. The `QPointer` is checked before `isDone()`/`start()`, and `taskFinished()`
  only clears the slot when the owner is the active task. `QueuedWebService` keeps its public API
  (`Reply`, `setRateLimitMs`, `rateLimitMs`, `noteRequestSent`, `enqueue(Reply *)`) and delegates
  to one `RequestQueue`; no existing test was edited, and `tst_queuedwebservice`, the six free-web
  service tests and `tst_deeplservice` stay green, so the extraction changed no behaviour.
- `LanguageToolService` returns id `languagetool`, display name `LanguageTool`,
  `isConfigured() == true`, `isLocal() == false`, `supportsAutoLanguage() == true`, and
  `Languages::supported("languagetool", Source)` without `auto`. `applySettings()` reads only
  `proofreading.endpoint`: an empty/invalid value falls back to
  `https://api.languagetool.org/v2/check`, and the queue pause is 3000 ms for
  `api.languagetool.org` and 500 ms for any other host. An active reply keeps its captured endpoint;
  `setEndpoint()` is a test seam and does not change the pause.
- The request is a `POST` to the endpoint with `Content-Type: application/x-www-form-urlencoded`,
  `Accept: application/json`, `SameOriginRedirectPolicy`, manual cookie load/save attributes and a
  body with exactly `text`, `language`, `enabledOnly=false` in that order. Both names and values are
  percent-encoded as UTF-8 with `QUrl::toPercentEncoding` (space is `%20`, never `+`); no `apiKey`,
  `username`, `Authorization`, cookie or other credential is sent, and the body always has no
  `enabledCategories` field.
- `auto` is accepted before the explicit-language check, sent literally as `language=auto`, and is
  not required to be in `languages()`. A successful 200 keeps the issues; an HTTP 400 or a body
  that is not a check response is `BadResponse` with a fixed message that contains neither the body
  nor the text. The detected language is ignored (no field in `ProofreadingResult`).
- Long text uses `TextChunker::splitWithOffsets(text, 20000)`: the parts are sent sequentially in
  one queue slot with `noteRequestSent()`/`rateLimitMs()` pacing, every issue's `offset` is shifted
  by `part.offset`, the category filter is client-side ("empty = all four"), and the merged list is
  sorted by `offset`/`length`/`ruleId` with exact duplicates removed. A failed middle chunk fails
  the whole reply with no partial issues. The reply finishes immediately after the last chunk; the
  pause only separates chunks.
- Errors: 401/403 → `Auth` "the endpoint rejected the request", 413 → `TooLong`, 429 →
  `RateLimited`, 5xx → `Network`, 400 and other statuses → `BadResponse` with `HTTP %1`, an HTML
  200 body → `ServiceUnavailable`, malformed JSON → `BadResponse`, a timeout → `Timeout` with the
  configured seconds, a closed server → `Network`. A cross-origin redirect is refused without a
  request to the other host. Messages and logs never contain the text, an issue message, a
  replacement, the URL or a body; the log carries only the chunk/issue counts, the language, the
  category count, the HTTP status and the error class.
- `abort()` finishes with `Cancelled` and stops the request; a queued reply aborted or deleted
  before its `start()` sends nothing; a service destroyed mid-request makes the reply fail with
  `Network` instead of crashing.
- `AppController` owns a `ProofreadingManager`, creates and registers `LanguageToolService` right
  after `createTranslation()`, applies its settings on startup and on every change, keeps
  `setOnlineServicesAllowed()` in sync with `proofreading/allowOnlineServices`, switches the active
  provider when `proofreading.provider` changes and calls `cancelAll()` in `prepareForQuit()`. No
  translation setting, service or cache is touched.
- `tests/tst_languagetoolservice.cpp` is platform-neutral, registered after
  `tst_languagetoolparser`, and has no fixture files: a local `FakeHttpServer` with POST bodies,
  delays and redirects plus inline `matchJson()` responses. It covers properties/defaults/pauses,
  endpoint fallback, the request shape and percent-encoding, all mapped and rejected languages,
  `auto` success and its failure path, three-chunk checks with the offset shift and de-duplication,
  the category filter, every HTTP error row, HTML and malformed bodies, timeout, closed server,
  cross-origin redirect, queue serialization, abort/cancel/delete, mid-request service destruction,
  cookie isolation, and the `TOPSECRET` message/log hygiene. 56 cases pass in both configurations.
- Review: two findings were accepted and fixed. (1) The success path scheduled a final `send()`
  after `rateLimitMs()`, so every check waited an extra 3000/500 ms after the last response and
  kept the queue slot; it now calls `finishCheck()` immediately, with the regression test
  `finishesWithoutTrailingPause`. (2) The cookie test now preloads a cookie into the shared jar and
  checks it is neither sent nor receives a `Set-Cookie` from the response
  (`cookiesAreIsolated`), so the manual cookie attributes are actually exercised. No rejected
  findings and no open issues.
- Verification (Linux only): Debug and Release build with `-Wall -Wextra` and zero warnings;
  `xvfb-run -a ctest --test-dir build/linux-{release,debug} --output-on-failure` is 42/42 in both;
  `ctest -N` shows 42 tests (41 + `tst_languagetoolservice`). Windows would be 37 (36 + 1) and is
  **not performed** — no MSVC on this machine.

Notes for the later children:

- `04-text-checking-tab` owns the endpoint/provider/category UI and the 14 `.ts` files; the service
  already applies the endpoint and category settings without being recreated.
- The LanguageTool rules stay in `languagetoolparser.cpp`; a live response with a misfiled
  category id is a parser-table change, not a service or UI change.
- `auto` is wired end to end but the detected language is still not exposed; a UI hint for it would
  need a new `ProofreadingResult` field and a separate task.
