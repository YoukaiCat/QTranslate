---
kind: task
status: todo
depends: [01-free-web-infrastructure]
---

# Bing web service

## Task

Add the unofficial Bing web endpoint as the last of the scraped services before Reverso. It needs a
two-step handshake (an HTML page for `IG`, `data-iid`, the abuse-prevention key/token, `MUID`, `SID`,
and `TID`, then a form-data POST) and is the most fragile target in the stage. The stage requires it
"where feasible"; the implementation must therefore either translate or fail with a clear error,
never with raw HTML or a retry loop.

Use `doc/free-endpoints.md` §3 for the protocol, §8 for the rules, and §9 for the priority. The
service is `bing-web` and implements `TranslationService` with `requiresOptIn() == true`. The first
request is `GET https://www.bing.com/translator`; a new parser extracts the token parameters from the
HTML without a full HTML parser, using tolerant string searches for the documented markers. The
second is `POST https://www.bing.com/ttranslatev3?isVertical=1&IG=...&IID=...` with multipart form
data: `text`, `fromLang`, `to`, `token`, `key`, `isAuthv2=true`,
`tryFetchingGenderDebiasedTranslations=true`, and the `MUID` cookie. Do not rely on the shared
`QNetworkAccessManager` cookie jar for `MUID`; take the `Set-Cookie` value from the token response and
send it as an explicit `Cookie` header on the POST, so a Bing cookie cannot leak into other services
and another service cannot overwrite it. Cache the token for about an hour. On HTTP 401, fetch the
token page once more and retry the POST once; a second 401 fails with `RateLimited` (a block), never
another refresh.

The service uses `QueuedWebService` with a conservative 1000 ms pause (the doc gives no Bing-specific
limit; keep it a named constant and document the assumption) and sends long text in 5000-character
chunks sequentially, restoring edge whitespace. The source language may be `auto`; map the internal
codes to Bing's `fromLang`/`to` codes for the declared languages and return `UnsupportedLanguage` for
an unmapped one before any request. Errors: HTTP 429/403 and a rate-limit body become `RateLimited`;
5xx becomes `Network`; an HTML page where JSON is expected, a missing token parameter, or a blocked
response becomes `ServiceUnavailable` without raw HTML; a timeout and `abort()` stop both requests.
The HTML page and the form body contain the text and the token; never log them.

The settings entry is a Bing group with the free/unofficial warning and the opt-in hint; no keys and
no per-service URL. Tests use fixtures for the HTML page and the JSON response and a local
`QTcpServer` that serves both steps; a live smoke is manual because the HTML and the anti-abuse
token change.

## Done when

- `BingWebService` and `BingWebParser` exist under `src/services/`, are listed in `CMakeLists.txt`,
  and return the internal `bing-web` id with `requiresOptIn() == true`; the service is not active by
  default.
- `tst_bingwebparser` extracts `IG`, `data-iid`, the abuse-prevention key and token, `MUID`, `SID`,
  and `TID` from the saved `bing_translator_page.html` fixture and returns a clear failure for an
  HTML page without the markers, a truncated page, and a JSON response from the translate step;
  the JSON parser reads `detectedLanguage` and `translations[].text`, and a missing array/empty
  translation is a `BadResponse`.
- `tst_bingwebservice` with a local `QTcpServer` verifies the first GET, the second POST path and
  query (`isVertical=1`, `IG`, `IID`), the multipart fields, the explicit `MUID` cookie, the browser
  headers needed by the endpoint, URL/body encoding of spaces and non-ASCII text, and the parsed
  result with the detected language.
- The token is reused for a second translation inside the cache lifetime (the test checks that the
  token page is fetched once for two POSTs); an expired token (HTTP 401 on the first POST) triggers
  one token refresh and one retry, and a second 401 fails with `RateLimited` without a third request;
  no loop is possible.
- A supported pair is sent with the mapped Bing codes; `auto` is sent as the documented `fromLang`;
  an unmapped language returns `UnsupportedLanguage` before the token page is fetched.
- Long text is split into 5000-character chunks at paragraph/sentence boundaries and sent
  sequentially with the 1000 ms pause (`setRateLimitMs(0)` in tests); the result restores edge
  whitespace and matches the concatenation of the chunk translations.
- HTTP 429/403 and a rate-limit body return `RateLimited`; 5xx returns `Network`; an HTML/blocked
  response, a missing token parameter, or a malformed translate response returns
  `ServiceUnavailable`/`BadResponse` without raw HTML; a timeout and `abort()` stop both requests and
  leave no pending reply.
- Requests to the host are serialized through `QueuedWebService`; a second translation waits for the
  active one and for the pause; cancelling a queued request does not send it.
- The settings dialog has a Bing group with the free/unofficial warning; the opt-in gate hides or
  refuses the service as described in `01-free-web-infrastructure`; `tst_settingsdialog` covers the
  group.
- `doc/architecture.md` §4.5/§4.9 and `doc/free-endpoints.md` §3/§8/§9 are updated; both READMEs list
  Bing web as an unofficial, fragile fallback; the manual live smoke is recorded as passed or as not
  performed with the reason (captcha, HTML change, or block).
- Debug and Release build without warnings; the full suite passes with the new tests; Windows is
  recorded as not performed when MSVC is unavailable.

## Risks

- Bing's HTML and anti-abuse tokens are the most fragile part of the stage. Do not write a general
  HTML parser or execute JavaScript; use tolerant marker extraction and fail with
  `ServiceUnavailable` when the markers are absent. If the live page no longer contains them, the
  service is still correct: it reports a clear error and the stage's "return clear errors" criterion
  is met. Do not add a captcha solver or a headless browser.
- The `MUID` cookie must not be shared with other services. Use an explicit `Cookie` header on the
  POST and do not touch `QNetworkAccessManager`'s global cookie jar; test that a cookie set by
  another request is not sent to Bing and that the Bing cookie is not sent to another host.
- The token cache is per service instance and per process; changing the proxy or the service settings
  should not reuse a token from another configuration. Clear it when the service settings change and
  never persist it to the INI or the log.
- A 401 refresh must be exactly once. A second 401 is a block; the test must assert the exact number
  of token-page requests, otherwise a retry storm is easy to introduce.
- The doc gives no Bing-specific pause or chunk limit. The 1000 ms pause and 5000-character chunk are
  conservative assumptions; keep them in named constants and record any measured limit from a live
  smoke.
