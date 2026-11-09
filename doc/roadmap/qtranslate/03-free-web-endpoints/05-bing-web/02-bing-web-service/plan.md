---
kind: task
status: todo
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

## Done when

- `BingWebService` exists under `src/services/`, is listed in `qtranslate_core` after the parser, and
  returns id `bing-web`, display name `Bing`, `isConfigured() == true`, `requiresOptIn() == true`,
  the `Languages::supported()` lists without `auto`, the two default endpoints and a fresh 1000 ms
  pause; `setEndpoints()` falls back to the defaults for an empty/invalid URL; `applySettings()`
  clears the token cache and reads no setting; no `AppSettings`/INI field or cold cache reset is
  added.
- `translate()` returns `EmptyText` for empty/whitespace-only text, sends `auto` as `auto-detect`,
  sends a supported pair with the mapped Bing codes, returns `UnsupportedLanguage` for an unmapped
  source or target before any request, splits at 5000 UTF-16 code units, restores edge whitespace and
  never sends a whitespace-only part.
- The handshake is exactly two steps: the first GET of `tokenEndpoint()` uses the browser User-Agent,
  `Accept: text/html`, the manual cookie attributes and the same-origin redirect policy; it extracts
  the token via `BingWebParser::parseTranslatorPage()`; the POST goes to `translateEndpoint()` with
  `isVertical=1`, `IG`, `IID`, the seven documented fields in order, UTF-8 percent-encoding, the
  explicit `Cookie: MUID=...` header, `Accept: application/json`, the browser User-Agent, the manual
  cookie attributes and the same-origin redirect policy.
- The token is cached per service instance for the page-reported lifetime (default 3600000 ms); a
  second translation inside the lifetime does not refetch the page; a 0 ms lifetime forces a refetch;
  `applySettings()` clears the cache. The token, key, `MUID` and page/body are never persisted or
  logged.
- HTTP 401 or a body `statusCode` 401 triggers exactly one token refresh and one retry of the same
  chunk; the retry carries the fresh token; a second 401 is `RateLimited` with exactly two token GETs
  and two POSTs; a failed refresh surfaces its error without a retry; a 403 is `RateLimited` without a
  refresh. The test asserts the exact request counts.
- Long text is split and sent sequentially inside one `QueuedWebService` slot with the 1000 ms pause
  (`setRateLimitMs(0)` in most tests, 100/200 ms only for the pacing/queue cases); a second
  translation waits for the active one; cancelling a queued reply does not send it; aborting an
  active reply stops the request and finishes with `Cancelled`.
- HTTP 429/403 and a rate-limit body return `RateLimited`; 413 returns `TooLong`; 5xx without a
  recognised body returns `Network`; an HTML/interstitial page, a missing token marker, a missing
  `MUID`, a blocked body, and a provider error object with a status other than 401/403/429 return
  `ServiceUnavailable` without raw HTML; malformed JSON, an empty array, missing/empty translations
  and a missing text return `BadResponse`; a timeout and `abort()` finish with
  `Timeout`/`Cancelled`; a cross-origin redirect is refused; no message contains the text, the page,
  the body, the URL, the token, the key or the cookie.
- Cookie isolation: a cookie preloaded in the shared `QNetworkAccessManager` jar is not sent on either
  Bing request; the Bing `MUID` is not stored in the jar and is not sent to another host; a request to
  another host still receives its own jar cookie; neither request writes a cookie to the jar.
- `AppController` constructs, configures and registers the service after Yandex and forwards
  `applySettings()`; no `AppSettings` field or INI key is added.
- `tst_bingwebservice` covers every case above with the local `QTcpServer`, is platform-neutral, and
  `ctest -N` shows 37 Linux tests (36 + the new target) and would show 32 on Windows (31 + 1). Debug
  and Release build on Linux without warnings and the full suite passes 37/37; Windows Debug/Release
  and its 32 tests are recorded as not performed (no MSVC); the manual live smoke is recorded as
  passed or as not performed with the reason (captcha, HTML change or block). CTest never contacts the
  real endpoint and no commit is made.
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
- Windows Debug/Release and its 32 tests are unavailable on this Linux machine; record them as not
  performed, not as passed.
