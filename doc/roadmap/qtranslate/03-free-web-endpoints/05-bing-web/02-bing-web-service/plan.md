---
kind: task
status: in-progress
detailed: true
depends: [01-bing-web-parser]
---

# Bing web service

## Task

Add `BingWebService` on top of the parser from `01-bing-web-parser`, register it in `AppController`
behind the free-web gate, and cover the two-step handshake with a local `QTcpServer` test. This task
implements the network behaviour, the token cache, the one-time 401 refresh, the explicit `MUID`
cookie, the 1000 ms pause and the 5000 UTF-16-unit chunks. It does not add the settings group,
translations or documentation; those are `03-bing-web-settings` and `04-bing-web-docs`.

The service is `bing-web`, derives from `QueuedWebService`, sets `setRateLimitMs(1000)` in the
constructor, returns `isConfigured() == true` and `requiresOptIn() == true`, and takes its source and
target lists from `Languages::supported("bing-web", ...)` without `auto`. The endpoint is fixed:
`static QUrl defaultTokenEndpoint()` is `https://www.bing.com/translator` and
`static QUrl defaultTranslateEndpoint()` is `https://www.bing.com/ttranslatev3`.
`setEndpoints(const QUrl &tokenUrl, const QUrl &translateUrl)`, `tokenEndpoint()` and
`translateEndpoint()` are the test seam, like `setEndpoint()` on the other free services; an empty or
invalid URL falls back to its default independently. `applySettings(const AppSettings &)` clears the
token cache and has no settings to read; no `AppSettings` field, INI key or per-service URL is added.

`translate()` returns `EmptyText` for empty/whitespace-only text, accepts `auto` as `auto-detect`,
maps an explicit source and the target with `Languages::toServiceCode("bing-web", ...)`, and returns
`UnsupportedLanguage` with the code before any request when either is unmapped. The text is split
with `TextChunker::split(text, 5000)`, every part's edge whitespace is stripped and restored, an
all-whitespace part is never sent, and the parts are sent sequentially inside one reply.

The nested `Reply` performs the handshake:

1. If the service has no usable cached token, `GET tokenEndpoint()` with the fixed browser User-Agent,
   `Accept: text/html`, `CookieLoadControlAttribute = Manual` and
   `CookieSaveControlAttribute = Manual`, `SameOriginRedirectPolicy`, and `noteRequestSent()` just
   before `nam()->get()`. On success, call `BingWebParser::parseTranslatorPage()`; a failure is
   `ServiceUnavailable` with the parser's fixed message and no POST. Store the token with its
   `lifetimeMs` from the page (default 3600000 ms; the observed page reports one hour). Prefer the
   `MUID` from the GET's `Set-Cookie` values (collect the values from `QNetworkReply::rawHeaderPairs()`
   and pass them to `BingWebParser::muidFromSetCookieValues()`); fall back to the page's `muid`; if
   both are empty, fail with `ServiceUnavailable` before the POST.
2. `POST` the current chunk to `translateEndpoint()` with the query `isVertical=1`, `IG` and `IID`,
   `Content-Type: application/x-www-form-urlencoded`, `Accept: application/json`, the browser
   User-Agent, `Cookie: MUID=<value>`, the two manual cookie attributes and
   `SameOriginRedirectPolicy`. The body fields, in this order, are `text`, `fromLang`, `to`, `token`,
   `key`, `isAuthv2=true`, `tryFetchingGenderDebiasedTranslations=true`; both names and values are
   percent-encoded as UTF-8, so a space, `&`, `=`, `+` or `%` in the text cannot break the body.
   Call `noteRequestSent()` just before `nam()->post()`.
3. On HTTP 401 or a JSON body with `statusCode` 401, clear the cached token if it is the one that was
   used, fetch the token page once more and retry the same chunk once. A second 401 in the same reply
   fails with `RateLimited` without a third token fetch or POST. A 403 (HTTP or body) is a block:
   `RateLimited`, no refresh. A failed refresh surfaces its own error, with no retry.
4. On a 2xx body, call `BingWebParser::parseResponse()`: append the text, keep the first non-empty
   detected language, and either schedule the next chunk after `rateLimitMs()` or finish. A failed
   chunk fails the whole reply without a partial result; a translation count that does not match the
   sent parts is `BadResponse`. The joined result sets `serviceId = "bing-web"` and restores the edge
   whitespace of every part, including unsent whitespace-only parts.
5. A non-2xx status classifies the body first (`BingWebParser::parseErrorBody()`) and then falls back
   to `errorForHttpStatus()`; a timeout or `abort()` finishes with `Timeout`/`Cancelled` and stops the
   active request; other network errors use `NetworkManager::describeNetworkError(request, false)` or
   the fixed `Network error`. The page, body, text, token, key, `MUID`, cookie and
   `QNetworkReply::errorString()` are never logged or put into a message; the log has only the part
   count, internal language codes, HTTP status, elapsed time and fixed messages
   (`Q_LOGGING_CATEGORY(lcBingWeb, "qtranslate.service.bing-web", QtWarningMsg)`).

Register the service in `AppController` after Yandex: `class BingWebService;` and
`BingWebService *m_bingWeb = nullptr;` in the header, construct `new BingWebService(m_network)`,
call its `applySettings(settings)`, `registerService(m_bingWeb)` after the Yandex service, and forward
`applySettings(newSettings)` in the settings-update path. The gate, picker filtering and active-service
switch are already generic; do not add a cache reset in `AppController`, because the service clears
its own token cache in `applySettings()`.

Add `tests/tst_bingwebservice.cpp` by copying the `FakeHttpServer` from `tests/tst_mozhiwebservice.cpp`
(extend its `Response` with the optional `Set-Cookie` header values needed for the handshake and the
cookie assertions). `init()` creates the server, `NetworkManager` and `BingWebService`, points
`setEndpoints()` at `http://127.0.0.1:<port>/translator` and
`http://127.0.0.1:<port>/ttranslatev3`, and calls `setRateLimitMs(0)`; `cleanup()` resets all three.
The test covers the properties and defaults (`bing-web`, display name `Bing`, configured, opt-in,
equal non-empty source/target lists without `auto`, default endpoints, `rateLimitMs() == 1000`), the
GET path and headers, the POST path/query/content type/fields, the explicit `MUID` cookie and the
manual cookie attributes (the `Set-Cookie` value preferred over the page marker, with the page
marker as the fallback), the UTF-8 encoding, the mapped target codes (`zh-Hans`, `zh-Hant`, `pt-BR`
→ `pt`, `pt-PT` → `pt-PT`, `nb`, `sr` → `sr-Cyrl`, `he`, `fil`, `jv`), `auto` → `auto-detect`, the
rejected unmapped codes without a request, the empty text, edge whitespace, 5000-unit chunks, chunk
pacing (`setRateLimitMs(100)`), queue serialization, cancelling a queued reply, aborting an active
one, a failed middle chunk (the whole reply fails, no partial translation, no extra request), the
token reuse for two translations, the cache lifetime from a 0 ms page lifetime (two token fetches),
the `applySettings()` cache clear, the one-time 401 refresh (exactly two token GETs and two POSTs,
the retry carrying the refreshed token, and the refreshed token serving the next translation without
a third token GET), the second 401 (`RateLimited`, no third request), a failed
refresh (its error is surfaced, no retry), the HTTP/HTML/provider-error/rate-limit/malformed/timeout
cases, a cross-origin redirect refused, the cookie isolation (a preloaded jar cookie is not sent to
Bing, the Bing `MUID` is not stored in the jar and not sent to another host, while the jar still works
for another request), and the message/log hygiene with a `TOPSECRET` text and token. Register the
target immediately after `tst_bingwebparser` in the platform-neutral block of
`tests/CMakeLists.txt`; keep it platform-neutral. CTest never contacts the real endpoint.

## Steps

1. **Add the service skeleton and list it in `qtranslate_core`.** Create
   `src/services/bingwebservice.h/.cpp`. `BingWebService` derives from `QueuedWebService`, takes
   `NetworkManager *` in the constructor and calls `setRateLimitMs(kDefaultRateLimitMs)` with
   `constexpr int kDefaultRateLimitMs = 1000`. Declare the overrides `id()`, `displayName()`,
   `isConfigured()`, `requiresOptIn()`, `sourceLanguages()`, `targetLanguages()`,
   `applySettings(const AppSettings &)` and `translate(const TranslationRequest &)`. Add the test
   seam and the fixed endpoints:

   ```cpp
   void setEndpoints(const QUrl &tokenUrl, const QUrl &translateUrl);
   QUrl tokenEndpoint() const;
   QUrl translateEndpoint() const;
   static QUrl defaultTokenEndpoint();     // https://www.bing.com/translator
   static QUrl defaultTranslateEndpoint(); // https://www.bing.com/ttranslatev3
   static QString browserUserAgent();
   ```

   `setEndpoints()` replaces the two URLs independently; each empty or invalid URL falls back to its
   own default. `browserUserAgent()` is public for the test, like
   `YandexWebService::browserUserAgent()` (`src/services/yandexwebservice.h:43`); it is protocol
   emulation and is set on the Bing requests only. Use file-local constants
   `kServiceId = "bing-web"`, `kMaxChunkUnits = 5000` and `kDefaultRateLimitMs = 1000`; comment
   that `doc/free-endpoints.md` gives no Bing-specific pause or chunk limit, so both values are
   conservative named assumptions. Do not add a token-lifetime constant:
   `BingWebParser::TokenPage::lifetimeMs` already defaults to 3600000.
   Private state: `NetworkManager *m_network`, the two endpoints, and one cache entry that keeps the
   token and its `MUID` together, because a cached token skips the GET that produced the cookie:

   ```cpp
   struct CachedToken
   {
       QString ig, iid, key, token, muid;
       qint64 lifetimeMs = 0;
       QElapsedTimer age; // restarted when the page is parsed

       bool isValid() const
       {
           return age.isValid() && age.elapsed() < lifetimeMs && !token.isEmpty();
       }
   };
   CachedToken m_token;
   ```

   Add `src/services/bingwebservice.cpp/.h` to `qtranslate_core` in `CMakeLists.txt` directly after
   `bingwebparser.cpp` (line 52), keeping the alphabetical order; do not add a new library or target.

2. **Implement the public surface and `translate()`.** `id()` returns `bing-web`, `displayName()`
   returns `Bing`, `isConfigured()` and `requiresOptIn()` return true; the language lists are
   `Languages::supported(kServiceId, Languages::Direction::Source/Target)` and already exclude
   `auto` (the `bing-web` branch from `01-bing-web-parser`). `applySettings(const AppSettings &)`
   resets `m_token` (token and `MUID` together) and reads no setting; no `AppSettings` field, INI key
   or URL setting is added.

   In `translate()`:
   - return `finishedWithError(TranslationError::EmptyText, QString())` for empty or whitespace-only
     text, before any request;
   - map the source as `auto` → the literal `auto-detect`, otherwise
     `Languages::toServiceCode(kServiceId, request.sourceLang, Source)`. Do not call
     `toServiceCode()` for `auto`, because it returns an empty string for the pseudo-code; follow
     the guard in `YandexWebService::translate()` (`src/services/yandexwebservice.cpp:373-375`);
   - map the target with `Languages::toServiceCode(kServiceId, request.targetLang, Target)`. If
     either mapped code is empty, return `UnsupportedLanguage` with
     `language "<internal code>" is not supported` and make no request;
   - split with `TextChunker::split(text, kMaxChunkUnits)` (`src/core/textchunker.h:14`), strip each
     part's edge whitespace into `leading`/`text`/`trailing`, and keep an all-whitespace part in the
     list as unsent (`Part` and `splitParts()` in `src/services/yandexwebservice.cpp:39-64` are the
     model to copy);
   - construct one `Reply` with the parts, the two mapped codes and a copy of both endpoints (the
     active reply must keep the endpoints it started with, as the other queued services do), call
     `enqueue(reply)` and return it. Log only the part count and the internal codes.

3. **Implement the `Reply` state machine.** Mirror the `Reply` of `YandexWebService`
   (`src/services/yandexwebservice.cpp:99-322`): a nested class with `start()`, `abortImpl()`,
   `stopRequest()`, `fail()`, `finish`, `QPointer<NetworkManager>`, `QPointer<BingWebService>`,
   `QPointer<QNetworkReply> m_request` and an elapsed timer. `start()` records the elapsed timer and
   posts a queued `QMetaObject::invokeMethod(this, [this] { send(); }, Qt::QueuedConnection)`, so
   `finished()` is never emitted from inside `translate()`. Keep an explicit stage
   (`TokenPage`/`Post`) and the index of the chunk being sent. Initialise `m_current` to the first
   non-empty part and mark a part sent only after its POST succeeds, so a 401 retry resends the same
   chunk. Guard `m_service` like `m_network`: a reply must fail with `Network` if the service has
   already been destroyed.

   `send()` chooses the next request: when `m_service->m_token.isValid()` is false it fetches the
   token page, otherwise it POSTs `m_current`. Every request goes through one helper that sets the
   browser User-Agent (after `NetworkManager::makeRequest()` has set the application one,
   `src/net/networkmanager.cpp:61`), `QNetworkRequest::CookieLoadControlAttribute = Manual`,
   `QNetworkRequest::CookieSaveControlAttribute = Manual` and
   `QNetworkRequest::RedirectPolicyAttribute = SameOriginRedirectPolicy`, and calls
   `m_service->noteRequestSent()` immediately before `nam()->get()`/`nam()->post()`. The attributes
   are client-side only, so the test verifies their effect on the jar, not the attributes
   themselves.

4. **Fetch and validate the token page.** `GET tokenEndpoint()` with `Accept: text/html`, no
   `Cookie` header (the page may set a fresh `MUID`; an old one is not sent) and no `Authorization`.
   On `finished`:
   - classify a non-2xx status with `BingWebParser::parseErrorBody()`, then
     `BingWebParser::errorForHttpStatus()`; a timeout, abort and other network errors use the same
     rules as the POST (step 6);
   - on 2xx, collect the `Set-Cookie` values from `reply->rawHeaderPairs()` (compare the name with
     `Set-Cookie` case-insensitively) into a `QList<QByteArray>` and call
     `BingWebParser::muidFromSetCookieValues()`;
   - call `BingWebParser::parseTranslatorPage()`; on failure finish with
     `ServiceUnavailable` and the parser's fixed message, with no POST;
   - take the MUID from the `Set-Cookie` value first and from `page.muid` second, trim it, and treat
     a value with a character below `0x20` as missing (it is pasted into a header); if both sources
     are empty, finish with `ServiceUnavailable` and the fixed
     `the token page has no session cookie` before any POST;
   - store `ig`, `iid`, `key`, `token`, `muid` and the page's `lifetimeMs` in `m_service->m_token`
     and restart its `age` (`parseTranslatorPage()` already defaults the lifetime to 3600000 and
     preserves a reported 0);
   - schedule the POST with `QTimer::singleShot(m_service->rateLimitMs(), ...)`, not immediately:
     the 1000 ms pause applies to the GET→POST pair too, not only between chunks.

5. **Build the POST and implement the success path and the one-time 401 refresh.**
   - URL: copy `translateEndpoint()`, add the query items `isVertical=1`, `IG` and `IID` from
     `m_service->m_token` (use `QUrlQuery`, which percent-encodes the values).
   - Body: a `formField(name, value)` helper using `QUrl::toPercentEncoding` for both sides and a
     `formBody()` with the seven fields in exactly this order: `text`, `fromLang`, `to`, `token`,
     `key`, `isAuthv2=true`, `tryFetchingGenderDebiasedTranslations=true` (same helper shape as
     `src/services/yandexwebservice.cpp:69-86`).
   - Headers: `Content-Type: application/x-www-form-urlencoded` (no charset, no multipart),
     `Accept: application/json`, the browser User-Agent, `Cookie: MUID=<cached muid>` (exactly one
     cookie), the manual cookie attributes and the same-origin policy; `noteRequestSent()` just
     before `nam()->post()`.
   - On the POST reply, in this order:
     - timeout/abort and generic network errors as in step 6;
     - if the HTTP status is 401 or `BingWebParser::isTokenRejection(body)` is true: when
       `m_refreshed` is false, set it true, invalidate `m_service->m_token` when its `token` still
       equals the one used, and fetch the token page again with `m_current` marked for retry. When
       `m_refreshed` is already true, finish with `RateLimited` and
       `the endpoint rejected the token` without any further request. An HTTP 403 or a 403 body is
       `RateLimited` with no refresh;
     - a non-2xx status: `parseErrorBody()` first, then `errorForHttpStatus()`, with the
       `HTTP %1` fallback message;
     - a 2xx body: `BingWebParser::parseResponse()`; on error fail the whole reply with that error
       and discard any partial text; on success append the text, keep the first non-empty
       `detectedSourceLang`, mark the part sent, advance `m_current`, then either schedule the next
       POST after `rateLimitMs()` or finish.
   - `m_refreshed` is one flag per reply, not per chunk: a later chunk that still answers 401 after a
     successful refresh fails with `RateLimited`. Reuse the token-page path for the refresh and keep
     `m_current` unchanged, so the success path naturally resends the same chunk and there is one
     refresh site, not duplicated request code.
   - `finishTranslation()`: fail with `BadResponse` when the number of sent parts and translations
     differs, join `leading + translated + trailing` for sent parts and `leading` only for unsent
     whitespace-only parts, set `serviceId = "bing-web"` and the first detected language, and log the
     part count, codes and elapsed time.

6. **Classify errors, cancellation and logging.** A shared handler for a finished request:
   - `QNetworkReply::OperationCanceledError`/`TimeoutError` → `Timeout` with
     `Bing did not respond within %1 s` using `m_network->timeoutMs()`, as in
     `src/services/yandexwebservice.cpp:220-226`;
   - any other network error → `Network` with `m_network->describeNetworkError(request, false)` or
     the fixed `Network error`; never `QNetworkReply::errorString()`, because the request body
     contains the text;
   - a non-2xx HTTP status → `BingWebParser::parseErrorBody(body)` when it reports an error, else
     `BingWebParser::errorForHttpStatus(status)` with `HTTP %1`;
   - a 2xx body that `parseResponse()` rejects keeps the parser's error and message (HTML,
     provider object, malformed JSON, empty/missing text).
   `abortImpl()` calls `stopRequest()`, which disconnects, aborts and `deleteLater()`s the active
   reply; the destructor calls it too. `abort()` finishes the result as `Cancelled` before the stop,
   so `stopRequest()` must stay silent. Use
   `Q_LOGGING_CATEGORY(lcBingWeb, "qtranslate.service.bing-web", QtWarningMsg)` and log only fixed
   messages, the part count, internal codes, HTTP status and elapsed time; the page, body, text,
   token, key, MUID, cookie and `errorString()` are never logged or put into a message.

7. **Register the service in `AppController` after Yandex.** In `src/app/appcontroller.h`, forward
   declare `class BingWebService;` in the alphabetical forward-declaration block before
   `class DeepLService;` (line 24) and add `BingWebService *m_bingWeb = nullptr;` after
   `m_yandexWeb` (line 106). In
   `src/app/appcontroller.cpp`: include the header, construct `m_bingWeb = new
   BingWebService(m_network);` after the Yandex construction (line 234), call
   `m_bingWeb->applySettings(settings);` after line 240, `m_translations->registerService(m_bingWeb)`
   after line 246, and forward `m_bingWeb->applySettings(newSettings)` in the settings-update path
   after line 612. The gate, picker filtering and service switch are already generic; do not add a
   cache reset in `AppController`, because the service clears its own cache in `applySettings()` and
   there is no Bing setting whose change would invalidate a cached translation.

8. **Add `tests/tst_bingwebservice.cpp` and register the target.** Copy the `FakeHttpServer` from
   `tests/tst_mozhiwebservice.cpp:23-120` and extend `Response` with
   `QList<QByteArray> setCookies`; write one `Set-Cookie:` line per value before
   `Connection: close`, keep `location` for the redirect case. Reuse the `formKeys`/`formValue`
   helpers from `tests/tst_yandexwebservice.cpp:125-147`, the `longText()` builder and
   `verifyFixedMessage()`; add a small `tokenPage(token, muid, lifetimeMs)` builder that returns a
   synthetic page with `IG:"..."`, the first `data-iid="..."`, the helper array and the optional
   `muid` (no live page, token or cookie), and a `translationResponse(text, detected)` builder for
   the live JSON shape. Count requests by `request.url.path()` (`/translator` vs `/ttranslatev3`) so
   the refresh tests assert exact numbers.

   `init()` creates the server, `NetworkManager` and `BingWebService`, points `setEndpoints()` at
   `http://127.0.0.1:<port>/translator` and `http://127.0.0.1:<port>/ttranslatev3` and calls
   `setRateLimitMs(0)`; `cleanup()` resets all three. The handler branches on the path and returns
   the programmed page or POST response, and the test asserts:
   - properties and defaults: id, display name, configured, opt-in, equal non-empty source/target
     lists without `auto`, `BingWebService::defaultTokenEndpoint()`/`defaultTranslateEndpoint()`,
     `rateLimitMs() == 1000` on a fresh instance, and `setEndpoints()` with an empty and an invalid
     URL falling back independently;
   - the GET: path, `Accept: text/html`, browser User-Agent (not `AppInfo::userAgent()`), no
     `Authorization` and no `Cookie`;
   - the POST: path, query `isVertical=1`/`IG`/`IID` taken from the page, content type,
     `Accept: application/json`, `formKeys()` order, every `formValue()`, `auto` → `auto-detect`,
     the mapped target codes (`zh-Hans`, `zh-Hant`, `pt-BR`→`pt`, `pt-PT`→`pt-PT`, `nb`,
     `sr`→`sr-Cyrl`, `he`, `fil`, `jv`), and percent-encoding of a space, `&`, `=`, `+`, `%`,
     Cyrillic and an emoji (no raw space or `+`);
   - the explicit cookie: the `Set-Cookie` `MUID` wins over the page's `muid`, the page `muid` is
     the fallback when no `Set-Cookie` is present, and no `Set-Cookie` plus a page without `muid`
     gives `ServiceUnavailable` before the POST;
   - the text cases: `EmptyText` for empty/whitespace, edge whitespace trimmed on the wire and
     restored in the result, `UnsupportedLanguage` for an unmapped code with zero requests;
   - long text: `TextChunker::split(text, 5000)` parts, each `text` field at most 5000 UTF-16 units
     and trimmed, no surrogate split, the joined result equal to the input, the request count equal
     to the sent parts; pacing with `setRateLimitMs(100)` asserts every inter-request delta ≥ 90 ms,
     including the first POST after the token GET; `setRateLimitMs(200)` plus two translations shows
     serialization; a queued reply is cancelled without a request; an active reply is aborted with
     `Cancelled` and no further request; a failed middle chunk fails the whole reply with no partial
     text and no extra request;
   - token behaviour: two translations inside the lifetime do one GET and two POSTs; a 0 ms page
     lifetime does a GET before each translation; `applySettings()` forces a new GET; the refreshed
     token serves the next translation without a third GET;
   - the 401 rules with exact counts: a body `statusCode` 401 or an HTTP 401 gives two GETs and two
     POSTs, the retry body carries the refreshed token and the same text, and the result is ok; a
     second 401 in the same reply gives `RateLimited` with exactly two GETs and two POSTs; a failed
     refresh (HTTP 500) surfaces `Network` with one GET, one POST and one failed GET, and no retry;
     HTTP 403 gives `RateLimited` with one GET and one POST; a 401 on a later chunk after a
     successful refresh is not refreshed again;
   - the error matrix: HTTP 429/403/413/5xx/400, an HTML page, a page without markers (the parser's
     `the token page is missing required parameters`), a provider `statusCode` 205/403, a malformed
     JSON body, an empty array, empty translations, a missing text, a timeout (`setTimeoutMs(300)`),
     a closed server (`Network error`) and a cross-origin redirect on the GET and on the POST (the
     other server sees no request);
   - cookie isolation: preload `jar=value` into `m_network->nam()->cookieJar()` with
     `setCookiesFromUrl()` for `http://127.0.0.1:<port>/`; neither Bing request carries it; the POST
     carries exactly `MUID=<value>` and no other cookie; after the exchange the jar still holds `jar`
     and no `MUID`/`MUIDB`; a plain `makeRequest()` to the local server still receives `jar=value`
     and never the MUID. The jar is checked before and after, not only the header, because the
     attributes are not visible on the wire;
   - hygiene: `TOPSECRET` in the text, the token and the MUID; every error message passes
     `verifyFixedMessage()`, and a `qInstallMessageHandler` installed for one success and one
     failure collects the log lines and asserts that none contains the text, the token, the key,
     the MUID, the page, the body or the URL.
   Register `qtranslate_add_test(tst_bingwebservice)` in `tests/CMakeLists.txt` immediately after
   `tst_bingwebparser` (line 25) in the platform-neutral alphabetical block; no new platform
   conditional. CTest never contacts the real endpoint.

9. **Build, count and verify.** Configure and build Linux Debug and Release with `-Wall -Wextra`
   (`cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release`,
   `cmake --build build/linux-release`), run `tst_bingwebservice`, then
   `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`. `ctest -N` must show 37
   Linux tests (36 + the new target); Windows would be 32 (31 + 1) and is recorded as not performed
   (no MSVC). Record the manual live smoke as passed or as not performed with the reason (captcha,
   HTML change or block). `git diff --stat` may show only `src/services/bingwebservice.*`,
   `src/app/appcontroller.*`, `CMakeLists.txt`, `tests/tst_bingwebservice.cpp` and
   `tests/CMakeLists.txt`; do not touch the settings UI, `.ts`, `doc/`, `AGENTS.md`, the READMEs,
   `doc/handoff.md`, the root plan or `07-fallback-chain`. Leave commit and push to the lead.

## Done when

- `BingWebService` exists under `src/services/`, is listed in `qtranslate_core` directly after
  `bingwebparser.cpp` (alphabetical), derives from `QueuedWebService`, and returns id `bing-web`,
  display name `Bing`, `isConfigured() == true`, `requiresOptIn() == true`, equal non-empty
  `Languages::supported(kServiceId, ...)` lists without `auto`, the two default endpoints and
  `rateLimitMs() == 1000` on a fresh instance. `setEndpoints()` replaces both URLs independently and
  falls back to the matching default for an empty or invalid URL. `applySettings()` clears the token
  and `MUID` cache and reads no setting; no `AppSettings` field, INI key or cold cache reset is added.
- `translate()` returns `EmptyText` for empty/whitespace-only text, sends `auto` as `auto-detect` in
  `fromLang`, sends an explicit source and the target with the mapped Bing codes (`zh-Hans`,
  `zh-Hant`, `pt-BR`→`pt`, `pt-PT`→`pt-PT`, `nb`, `sr`→`sr-Cyrl`, `he`, `fil`, `jv`), and returns
  `UnsupportedLanguage` with the internal code before any request when either code is unmapped. It
  splits at 5000 UTF-16 code units with `TextChunker::split`, strips and restores edge whitespace,
  never sends a whitespace-only part, and keeps every chunk in one `QueuedWebService` slot.
- The handshake is exactly two steps. The GET of `tokenEndpoint()` sends the browser User-Agent,
  `Accept: text/html`, the manual cookie attributes and the same-origin redirect policy, and no
  `Cookie` header; it extracts the token with `BingWebParser::parseTranslatorPage()`, prefers the
  `Set-Cookie` `MUID` via `BingWebParser::muidFromSetCookieValues()` over the page's `muid`, and fails
  with `ServiceUnavailable` before the POST when the page is unusable or both MUID sources are empty.
  The POST of `translateEndpoint()` sends `isVertical=1`, `IG` and `IID` in the query, the seven
  documented fields in order, UTF-8 percent-encoding on every name and value,
  `Content-Type: application/x-www-form-urlencoded`, `Accept: application/json`, the browser
  User-Agent, `Cookie: MUID=<value>` (exactly one cookie), the manual cookie attributes and the
  same-origin redirect policy.
- The token and its `MUID` are cached together per service instance for the page-reported lifetime
  (default 3600000 ms); a second translation inside the lifetime does not refetch the page; a 0 ms
  lifetime refetches before every translation; `applySettings()` clears both; an expired token is
  never reused. The token, key, `MUID`, page and body are never persisted or logged.
- All requests of one reply are paced by at least `rateLimitMs()` (1000 ms in production, and the
  GET→POST pair is paced too, not only the chunks); tests use `setRateLimitMs(0)` except for the
  pacing and queue cases. A second translation waits for the active reply; cancelling a queued reply
  sends nothing; aborting an active reply stops both the token and the POST request and finishes with
  `Cancelled`.
- HTTP 401 or a body `statusCode` 401 triggers exactly one refresh (token GET) and one retry of the
  same chunk inside the reply; the retry carries the refreshed token and the same text; a second 401
  in that reply is `RateLimited` with exactly two token GETs and two POSTs; a failed refresh surfaces
  its error with no retry POST; a 403 (HTTP or body) is `RateLimited` with no refresh; a 401 on a
  later chunk after a successful refresh is not refreshed again. The test asserts the exact request
  counts and the token value in the retry body.
- Long text is split and sent sequentially inside one reply with the pause; the joined result
  restores edge whitespace, keeps the first non-empty detected language and sets
  `serviceId = "bing-web"`. A failed chunk fails the whole reply without a partial translation or a
  further request; a translation count that does not match the sent parts is `BadResponse`.
- HTTP 429/403 and a rate-limit body return `RateLimited`; 413 returns `TooLong`; 5xx without a
  recognised body returns `Network`; an HTML/interstitial page, a page without markers, a missing
  `MUID`, a blocked body and a provider error object with a status other than 401/403/429 return
  `ServiceUnavailable` without raw HTML; malformed JSON, an empty array, missing/empty translations
  and a missing text return `BadResponse`; a timeout finishes with `Timeout`
  (`Bing did not respond within … s`), a closed server with `Network` (`Network error`), and
  `abort()` with `Cancelled`; a cross-origin redirect on either request is refused. No message
  contains the text, the page, the body, the URL, the token, the key or the cookie, and the collected
  log lines contain none of them either.
- Cookie isolation: a cookie preloaded in the shared `QNetworkAccessManager` jar is not sent on either
  Bing request; neither request writes a cookie to the jar; the Bing `MUID` is not stored in the jar
  and no other request carries it; a plain request through the same manager still receives its own
  jar cookie.
- `AppController` includes the header, holds the service after Yandex, constructs it after Yandex,
  calls `applySettings()`, registers it after Yandex and forwards `applySettings()` in the
  settings-update path; no `AppSettings` field or INI key is added and no cache reset is added there.
- `tst_bingwebservice` covers every case above against a local `QTcpServer` with the extended
  `FakeHttpServer` (Set-Cookie values), is platform-neutral, and is registered in the
  platform-neutral block of `tests/CMakeLists.txt` immediately after `tst_bingwebparser`. `ctest -N`
  shows 37 Linux tests (36 + the new target) and would show 32 on Windows (31 + 1). Debug and Release
  build on Linux without warnings and the full suite passes 37/37; Windows Debug/Release and its 32
  tests are recorded as not performed (no MSVC); the manual live smoke is recorded as passed or as
  not performed with the reason (captcha, HTML change or block); CTest never contacts the real
  endpoint and no commit is made.
- The settings group, `.ts`, `doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md`, the READMEs,
  `doc/handoff.md`, the root plan and `07-fallback-chain` are not touched; `03-bing-web-settings` and
  `04-bing-web-docs` own them.

## Risks

- The endpoint reports many errors under HTTP 200. The token rejection can be an HTTP 401 or a body
  `statusCode` 401; a 403 is a block per the node's rule. If the live endpoint uses another shape for
  an expired token, record it and change the single check in `Reply`; do not add retries in several
  places.
- The 401 refresh is the easiest place for a retry storm. Keep one refresh counter per reply (not per
  chunk and not per service), retry only the same chunk, and assert the exact GET/POST counts in the
  test. A 401 on a later chunk after a successful refresh must still not refresh a second time.
- The explicit `MUID` cookie and the manual cookie attributes are both required. Setting only the
  header but leaving the jar automatic can add other Bing cookies to the POST or leak the `MUID` to
  another service; setting only the attributes sends no cookie. Test the jar before and after the
  exchange, not only the header.
- The 1000 ms pause and the 5000 UTF-16-unit chunk are conservative assumptions. Keep them in named
  constants; tests use `setRateLimitMs(0)` except for the pacing cases, with lower bounds and
  `QTRY_*` timeouts, because timing tests are the flake risk. A live smoke may measure a different
  limit; record it before changing the constant.
- The token cache is per instance and per process and is cleared by `applySettings()`. An active reply
  can repopulate it after a settings change; with no Bing-specific settings this is harmless and is
  not the deferred `TranslationManager` cache-generation race. Do not add a generation counter here.
- The endpoint may block a datacenter or a country IP and may answer with a captcha HTML page. CTest
  uses only the local server; a live smoke is manual and its failure is a recorded result, not a code
  change. No captcha solver, headless browser, cookie harvesting or block bypass.
- The token page adds a GET before the first POST, so the first translation is always two paced
  requests. Do not "optimise" that pause away: the 1000 ms applies to every request to the scraped
  host. The pacing test must therefore assert a lower bound between the GET and the first POST as
  well as between chunks, and use `QTRY_*`/generous `wait()` timeouts.
- The `MUID` from the GET is tied to the token page. Cache both together: if the token is reused
  while the `MUID` is dropped, the next POST has no cookie and fails with `ServiceUnavailable`
  although the token is still valid. Do not split the cache into two independently cleared entries.
- The scraped `MUID` is pasted into a request header. Trim it and reject a value containing a control
  character (below `0x20`) as missing, so a mangled page or a malicious proxy cannot inject a header;
  never build the `Cookie` header from an unchecked page value.
- The `Set-Cookie` MUID retrieval depends on the reply's raw headers, not on the jar: the manual
  cookie attributes keep the jar untouched, but `rawHeaderPairs()` still contains the response
  headers. Do not switch to `QNetworkCookieJar` reads to obtain the value, and do not remove the
  manual attributes to make the cookie "visible".
- Windows Debug/Release and its 32 tests are unavailable on this Linux machine; record them as not
  performed, not as passed.
