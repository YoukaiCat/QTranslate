---
kind: stage
status: in-progress
base: 53b43a5
depends: [01-free-web-infrastructure]
---

# Bing web

## Goal

Add the unofficial Bing web endpoint as the last scraped service before Reverso. It needs a two-step
handshake — an HTML page for `IG`, `data-iid`, the abuse-prevention key/token, `MUID`, `SID`, and
`TID`, then a form POST — and is the most fragile target in the stage. The stage requires it "where
feasible"; the implementation must either translate or fail with a clear error, never with raw HTML
or a retry loop.

Use `doc/free-endpoints.md` §3 for the protocol, §8 for the rules, and §9 for the priority. The
service is `bing-web` and implements `TranslationService` with `requiresOptIn() == true`.

## What to do, what is touched

- `01-bing-web-parser` (task, detailed): the pure `BingWebParser` — the tolerant extraction of the
  translator-page markers (`IG`, `data-iid`, `params_AbusePreventionHelper` key/token/lifetime,
  `MUID`, `SID`, `TID`), the `MUID` helper for `Set-Cookie` values, and the translation/error JSON
  parsing — plus the `bing-web` language branch in `languages.cpp`, the saved fixtures and
  `tst_bingwebparser`/`tst_languages`. No service, registration, UI, translations or docs.
- `02-bing-web-service` (task): `BingWebService` with the two-step handshake, the token cache, the
  one-time 401 refresh, the explicit `MUID` cookie, the 1000 ms pause, 5000 UTF-16-unit chunks,
  registration in `AppController`, and `tst_bingwebservice` against a local `QTcpServer`. No
  settings UI, translations or docs.
- `03-bing-web-settings` (task): the gate-aware `Bing` group in `SettingsDialog`, `tst_settingsdialog`
  and the 14 `.ts` files. No service behaviour change.
- `04-bing-web-docs` (task): `doc/architecture.md`, `doc/free-endpoints.md` and the `AGENTS.md` test
  counts. No code.

Decisions carried through all children:

- The service id is `bing-web`; the display name is `Bing`; `isConfigured()` and
  `requiresOptIn()` return true. It stays off by default, is hidden from the pickers and is refused
  by `TranslationManager` with `ServiceUnavailable` while `translation/allowFreeServices` is off.
  There is no key, no per-service URL and no `AppSettings`/INI field for Bing.
- Step 1 is `GET https://www.bing.com/translator`. The parser extracts the first `IG:"..."`,
  `data-iid="..."`, `params_AbusePreventionHelper = [key,"token",lifetimeMs];`, and the optional
  `"muid"`, `"sid"` and `"tid"` values with tolerant string searches; it is not an HTML parser and
  must never put raw HTML into a message. The helper key may be a JSON number, so its digits are
  preserved, not reformatted through a double.
- Step 2 is `POST https://www.bing.com/ttranslatev3?isVertical=1&IG=...&IID=...` with
  `application/x-www-form-urlencoded` and the fields `text`, `fromLang`, `to`, `token`, `key`,
  `isAuthv2=true`, `tryFetchingGenderDebiasedTranslations=true`, in that order, with every name and
  value percent-encoded as UTF-8. This corrects the older "multipart form data" wording: the
  2026-11-09 observation against `www.bing.com` returned a translation for the urlencoded form and
  `{"statusCode":205,"errorMessage":""}` for multipart with the same fields.
- `MUID` is sent as an explicit `Cookie: MUID=...` header on the POST. The service prefers the
  `MUID` value from the GET's `Set-Cookie` header and falls back to the `muid` marker in the page.
  Both requests set `CookieLoadControlAttribute` and `CookieSaveControlAttribute` to `Manual`, so the
  shared `QNetworkAccessManager` cookie jar is never read or written: a cookie from another service
  cannot reach Bing, and the Bing cookie cannot reach another host. A missing `MUID` is
  `ServiceUnavailable` before the POST.
- The token is cached per service instance and process for the lifetime reported by the page
  (`3600000` ms in the observed page, about one hour; a missing or invalid value falls back to that
  constant). It is never persisted to the INI or the log. `applySettings()` clears the cache so a
  configuration change cannot reuse a token from another configuration.
- On HTTP 401, or a JSON body with `statusCode` 401 (the observed endpoint reports many errors under
  HTTP 200), the service fetches the token page once more and retries the POST once; a second 401 in
  the same reply fails with `RateLimited` and no third request. A 403 is a block, not a token
  rejection: `RateLimited`, no refresh. The test asserts the exact number of token-page and POST
  requests, so a retry storm cannot pass review.
- The pause between requests to the host is `1000` ms, a conservative named constant because the doc
  gives no Bing-specific limit. Long text is split with `TextChunker::split` at 5000 UTF-16 code
  units at paragraph/sentence boundaries, the chunks are sent sequentially inside one
  `QueuedWebService` slot, and the edge whitespace is restored. Both values are documented
  assumptions; a live smoke may record a measured limit.
- The source may be `auto` and is sent as the documented `auto-detect`; an explicit source and the
  target are mapped to Bing's codes, and an unmapped code returns `UnsupportedLanguage` before any
  request. The observed catalogue and the overrides are in `01-bing-web-parser`.
- Errors: HTTP 429/403 and a rate-limit body become `RateLimited`; 5xx without a recognised body
  becomes `Network`; an HTML page, a missing token marker, a missing `MUID`, or a provider error
  object becomes `ServiceUnavailable` without raw HTML; malformed JSON, a missing/empty
  `translations` array or a missing/empty `text` becomes `BadResponse`; 413 becomes `TooLong`; a
  timeout and `abort()` stop both requests and finish with `Timeout`/`Cancelled`.
- Requests use `SameOriginRedirectPolicy`, so a cross-origin redirect is refused and the text cannot
  silently move to another host. The page, the form body, the token, the key and the cookie are never
  logged; messages are fixed literals without the text, the body or the URL.
- Tests use saved fixtures and a local `QTcpServer`; CTest never contacts the real endpoint. A live
  smoke is manual and is recorded as passed or as not performed with the reason (captcha, HTML
  change, or block). There is no captcha solver, no headless browser and no bypass of a block.
- Expected counts: `01-bing-web-parser` adds one target, so Linux becomes 36 and Windows would be 31;
  `02-bing-web-service` adds the second, so Linux becomes 37 and Windows would be 32 (29 shared plus
  3 Windows-only on Windows; 29 shared plus 8 Linux-only on Linux). Windows is derived from
  `tests/CMakeLists.txt`, not run here.
- `README.md`, `README.ru.md`, `doc/handoff.md`, the root plan and `07-fallback-chain` stay with the
  lead and are not edited by the children. `doc/architecture.md`, `doc/free-endpoints.md` and the
  `AGENTS.md` counts belong to `04-bing-web-docs`.

## Done when

- `01-bing-web-parser` is done: `BingWebParser` and the `bing-web` language branch exist; the parser
  extracts `IG`, `data-iid`, the abuse-prevention key/token/lifetime, `MUID`, `SID` and `TID` from
  the saved page and fails clearly for a page without the markers, a truncated page or a JSON body
  passed by mistake; the JSON parser reads `detectedLanguage` and `translations[].text`; all parser
  and language cases are green; Linux is 36 tests and Windows would be 31.
- `02-bing-web-service` is done: `BingWebService` is registered in `AppController` behind the gate;
  `tst_bingwebservice` verifies the GET and the POST (path, query, fields, content type, explicit
  `MUID` cookie, browser headers, UTF-8 encoding), the token reuse and cache lifetime, the one-time
  401 refresh and the second-401 `RateLimited`, the 1000 ms pacing and the 5000-unit chunks, the
  serialized queue and cancellation, the classified errors, the cookie isolation, and the message and
  log hygiene; Linux is 37 tests and Windows would be 32.
- `03-bing-web-settings` is done: the Services page has a gate-aware `Bing` group with the
  unofficial/no-key warning and the opt-in hint, no key or URL field; `tst_settingsdialog` covers it;
  all 14 `.ts` files are complete (309 → 311 messages) and `tst_translations` passes.
- `04-bing-web-docs` is done: `doc/architecture.md` §3.1/§3.2/§4.5/§4.7.6/§4.8/§4.9/§8/§4.14/§9.2,
  `doc/free-endpoints.md` §3/§8/§9 and `AGENTS.md` (32 Windows/37 Linux) describe the implemented
  service; all relative links resolve.
- The overall Bing criteria hold: the service is behind `translation/allowFreeServices`; a supported
  pair is sent with the mapped Bing codes and `auto` as `auto-detect`; the token is reused inside
  its lifetime and refreshed exactly once on 401; the second 401 is `RateLimited`; the `MUID` cookie
  never enters the shared jar and never reaches another host; 5000-unit chunks are paced at 1000 ms;
  429/403/rate-limit bodies, 5xx, HTML/blocked/provider errors, malformed bodies, timeouts and
  `abort()` all finish with the documented error and never show raw HTML; official Google/DeepL
  remain primary and the service is not enabled by default.
- Debug and Release build on Linux without warnings and the full suite passes; Windows Debug/Release
  and its 32 tests are recorded as not performed (no MSVC); the manual live smoke is recorded as
  passed or as not performed with the reason; no push is made.
- `README.md` and `README.ru.md` list Bing web as an unofficial, fragile fallback; this is the lead's
  final pass, not a child edit. `doc/handoff.md`, the root plan and `07-fallback-chain` are also
  lead-owned; the children do not edit them. `06-reverso` is unchanged here.

## Depends on

`01-free-web-infrastructure` (the opt-in gate and `QueuedWebService`). The four children are
sequential: `02-bing-web-service` depends on `01-bing-web-parser` for the parser and the language
branch; `03-bing-web-settings` depends on `02-bing-web-service` for the service id and the gate;
`04-bing-web-docs` depends on both `02` and `03` for the final behaviour, the UI strings and the
counts. `06-reverso` follows in the stage order but depends only on `01-free-web-infrastructure`;
`07-fallback-chain` depends on this node.

## Risks and what is needed from the user

- Bing's HTML and anti-abuse tokens are the most fragile part of the stage. Do not write a general
  HTML parser or execute JavaScript; use tolerant marker extraction and fail with
  `ServiceUnavailable` when the markers are absent. If the live page no longer contains them, the
  service is still correct: it reports a clear error and the stage's "return clear errors" criterion
  is met. Do not add a captcha solver, a headless browser, cookie harvesting or any bypass of a block.
- The observed protocol is easy to get subtly wrong: the token page has several `data-iid` values
  (use the first), the helper key is a number (do not reformat it), `MUIDB` must not be mistaken for
  `MUID`, and multipart is rejected while form-urlencoded works. These are pinned by fixtures and
  local-server tests, not by assumptions in prose.
- The `MUID` cookie must not be shared with other services. Use an explicit `Cookie` header on the
  POST, set the cookie load/save attributes to `Manual`, and test that a cookie set by another
  request is not sent to Bing and that the Bing cookie is not sent to another host.
- The token cache is per service instance and per process; `applySettings()` clears it and it is never
  persisted to the INI or the log. An active reply may repopulate the cache after a settings change;
  with no Bing-specific settings this is harmless, and it does not touch the deferred
  `TranslationManager` cache-generation race.
- A 401 refresh must be exactly once. A second 401 is a block; the test must assert the exact number
  of token-page requests, otherwise a retry storm is easy to introduce. The upstream plugin also
  refreshes on a body 403; this plan follows the node and treats 403 as `RateLimited` without a
  refresh. If a live smoke shows an expired token arriving as a body 403, record it and revisit the
  rule instead of adding a hidden retry.
- The doc gives no Bing-specific pause or chunk limit. The 1000 ms pause and the 5000 UTF-16-unit
  chunk are conservative assumptions; keep them in named constants and record any measured limit from
  a live smoke. Do not add a hidden cap or an overall timeout.
- The endpoint may block a datacenter or a country IP and may answer with a captcha HTML page. The
  automated gate is the local-server test; a live smoke is manual and its failure is a recorded
  result, not a code change. The user may run a short en→de smoke when convenient; otherwise it is
  recorded as not performed.
- Windows Debug/Release and its 32 tests are unavailable on this Linux machine; record them as not
  performed, not as passed.
