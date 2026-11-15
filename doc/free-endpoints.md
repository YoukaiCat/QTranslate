# Free web translation endpoints

Date: 2026-11-14. Status: **roadmap stage 3 (old stage 13); DeepL web is implemented as a `free-web`
mode of the DeepL service; MyMemory, Mozhi, Yandex web, Bing web and Reverso are implemented as
no-key services, available by default since the opt-in gate was removed in stage 8**.

This document records the free endpoints without an API key that can be added to qtranslate as
experimental services. The main source is the open project
[ahatem/QTranslate](https://github.com/ahatem/QTranslate) (MPL-2.0), plugins `deepl-services`,
`yandex-web-services`, `bing-services`, `reverso-services`, `mozhi-services`, `mymemory-services`.

All web endpoints listed below, except MyMemory and self-hosted LibreTranslate/Mozhi, are
**unofficial**: they have no SLA or quotas, they may change, require cookies/captcha, start
returning `429`, or block IPs. The services' terms may restrict automated use. DeepL web is
implemented as a switchable mode of a single "DeepL" service without a key, but it is explicitly
marked in the UI as unofficial; the other risky endpoints are better enabled only explicitly and not
made the only translation method.

## 1. DeepL web

- **Request:** `POST https://oneshot-free.www.deepl.com/v1/translate`
- **Headers:** `Authorization: None`, `Content-Type: application/json`, `Accept: application/json`,
  a browser `User-Agent`.
- **Body:** `{"text":["..."],"target_lang":"de"}`; `source_lang` may be omitted.
- **Response:** `{"translations":[{"detected_source_language":"en","text":"...", "...":...}]}`.
- **Limits:** unofficial endpoint; rate limit by IP; error `1042911` / `Too many requests`.
- **What QTranslate does:** tries the official API when a key is present; if the key is rejected,
  switches to the web endpoint once; web requests no more often than once every 2 seconds; text is
  split into 5000-character chunks; on `429`, a pause and one retry.
- **Verification:** on 2026-11-05 the endpoint answered `200` to a neutral phrase; before that it
  returned `429`, so the limit is real and depends on IP/rate.

**Implementation in qtranslate (2026-11-05).**

- `free-web` mode in the settings of the "DeepL" service, next to the API mode, as with Google
  free/Cloud. There is no separate `deepl-web` in the service list anymore. The mode is available
  without a key, the key field is disabled, and the settings show a warning about the unofficial
  status. Old `service=deepl-web` settings migrate to `service=deepl` + `deepl.mode=free-web`.
- Headers: `Authorization: None`, `Accept: application/json`, `Content-Type: application/json`; the
  qtranslate `User-Agent` is suitable, and there is no need to masquerade as a browser.
- Language codes: base ones in lowercase (`DE` → `de`), regional ones — `pt-BR`, `en-US`, `zh-Hans`.
  Important: the endpoint takes uppercase `DE` as an unknown code and returns the source text with
  `200`, so lowercasing is mandatory. `PT-BR`, `EN-US`, and `zh-Hans` (after conversion from
  `ZH-HANS`) were verified with live requests.
- 5000 characters per chunk; all chunks are sent in one request as a `text` array; leading and
  trailing spaces are restored.
- Inside `DeepLWebService`, requests are serialized and sent no more often than once every 2 s; on
  `429`, a 3 s pause and one retry; `403` is treated as a block/limit. `413` → `TooLong`, `5xx` →
  `Network`.
- Code: `src/services/deeplwebservice.*`, `src/services/deeplwebparser.*`; tests:
  `tst_deeplwebparser`, `tst_deeplwebservice`.

## 2. Yandex web

- **Request:** `POST https://api.browser.yandex.com/instaserp/translate`
- **Format:** `application/x-www-form-urlencoded`; a browser `User-Agent` (in QTranslate —
  Chrome/YaBrowser).
- **Fields:** `text`, `brandID=int`, `statLang=<target>`, `targetLang=auto`, `locale=<target>`,
  `clid=2270494`, `disable=serp`, `use_llm_srv=0`.
- **Response:** a JSON array; the first element contains `text` and the detected language.
- **Limits:** unofficial endpoint; the implementation keeps a 750 ms pause and sends at most 5000
  UTF-16 code units per request. The research note that QTranslate falls back to Mozhi with
  `engine=yandex` on error is **not** implemented by this service; the fallback chain belongs to
  `07-fallback-chain`.
- **Why:** good coverage of ru/uk/tr and CIS languages.

**Implementation in qtranslate (2026-11-09).**

- Service `yandex-web`, no key; available in the pickers like the other providers. The fixed `POST`
  and the form fields above, with both names and values percent-encoded as UTF-8.
- The browser User-Agent is set on this request only; `AppInfo::userAgent()` and every other service
  keep the application UA. This is protocol emulation, not a claim that the application is a
  browser.
- The protocol has no source-language field: `targetLang=auto` is constant, the target goes to
  `statLang`/`locale`, and an explicit source is validated but not sent.
- Languages: the observed 71-code whitelist with `zh-Hans`/`zh-Hant`→`zh`, `nb`→`no`, `pt-BR`→`pt`,
  and `he` unchanged; `pt-PT` is not declared (`closest()` maps it to `pt-BR`); `zh-Hant` is a
  known limitation because it is sent as `zh`, so Traditional Chinese is served as Simplified.
- 5000 UTF-16 code units and a 750 ms pause are conservative named assumptions; chunks are
  sequential inside one queue slot, edge whitespace is restored, and `SameOriginRedirectPolicy`
  refuses a cross-origin redirect.
- Response `[{"text","from","to"}]`: `text` is required and `from` is mapped back; extra fields and
  a second element are ignored.
- Errors are classified body-first: a rate-limit body or a bare 403/429 → `RateLimited`, 413 →
  `TooLong`, 5xx without a recognised body → `Network`, an HTML/captcha/blocked body or a JSON
  `error` → `ServiceUnavailable`, malformed/empty/missing `text` and an unknown non-2xx status →
  `BadResponse`, timeout/abort → `Timeout`/`Cancelled`. Messages never contain the text, body, or
  URL.
- Tests: `tst_yandexwebparser`, `tst_yandexwebservice`, `tst_languages`, and
  `tst_settingsdialog`; CTest never contacts the real endpoint, and a live smoke is manual only.

## 3. Bing web

- **Preparation:** `GET https://www.bing.com/translator` and HTML parsing: `IG`, `data-iid`,
  `params_AbusePreventionHelper` (key/token), `MUID`, `SID`, `TID`.
- **Request:** `POST https://www.bing.com/ttranslatev3?isVertical=1&IG=...&IID=...`
- **Format:** `application/x-www-form-urlencoded`; fields `text`, `fromLang`, `to`, `token`, `key`,
  `isAuthv2=true`, `tryFetchingGenderDebiasedTranslations=true`; cookie `MUID`.
- **Response:** JSON with `detectedLanguage` and a `translations[].text` array.
- **Limits:** the token lives about an hour; on `401`, refresh the token once and retry, without
  loops. Bing's HTML changes, so the parser is fragile.
- **Complexity:** higher than the others because of token and cookie extraction.

**Implementation in qtranslate (2026-11-09).**

- The `bing-web` service: no key, available in the pickers like the other providers. Fixed
  endpoints `https://www.bing.com/translator` (GET) and
  `https://www.bing.com/ttranslatev3` (POST). The page parser extracts `IG`, the first `data-iid`,
  the `params_AbusePreventionHelper` key/token/lifetime, and the optional `muid`/`sid`/`tid`; it is
  not an HTML parser and fails with a fixed message when the markers are absent.
- POST body: `application/x-www-form-urlencoded`, not multipart; the fields in the order above, with
  every name and value percent-encoded as UTF-8. The 2026-11-09 observation returned a translation
  for the urlencoded form and `{"statusCode":205,"errorMessage":""}` for multipart with the same
  fields.
- `MUID` comes from the GET's `Set-Cookie` first and the page's `muid` second; both requests set
  `CookieLoadControlAttribute`/`CookieSaveControlAttribute` to `Manual`, and the POST sends exactly
  one explicit `Cookie: MUID=...`, so the shared cookie jar is neither read nor written.
- The token and `MUID` are cached together for the page-reported lifetime (3600000 ms default),
  cleared by `applySettings()`, and never persisted or logged. HTTP 401 or a body `statusCode` 401
  refreshes once and retries the same chunk; a second 401 is `RateLimited`; a 403 is a block with no
  refresh.
- 5000 UTF-16 code units and a 1000 ms pause are conservative named assumptions; chunks are
  sequential inside one queue slot, edge whitespace is restored, and `SameOriginRedirectPolicy`
  refuses a cross-origin redirect. A browser User-Agent is set on these requests only; the
  application UA and `AppInfo::userAgent()` are unchanged.
- Response `[{"translations":[{"text":...}],"detectedLanguage":{"language":...}}]`: the first element
  with translations is used, `text` values are joined, and `detectedLanguage` is mapped back. Errors
  are body-first: 401/403/429 and rate-limit bodies → `RateLimited`, 413 → `TooLong`, 5xx →
  `Network`, HTML/missing markers/missing MUID/blocked/other provider errors → `ServiceUnavailable`,
  malformed/empty responses → `BadResponse`, timeout/abort → `Timeout`/`Cancelled`.
- Tests: `tst_bingwebparser`, `tst_bingwebservice`, `tst_languages`, `tst_settingsdialog`; CTest
  never contacts the real endpoint, and a live smoke is manual only.

## 4. Reverso

- **Request:** `POST https://cps.reverso.net/api2/TranslateText`
- **Body:** JSON `{"source":"...","direction":"...","deviceId":"0","uiLang":"en",
  "origin":"chromeextension","accessToken":"","appId":"0"}`.
- **Response:** `translation`, `directionFrom`, `directionTo`, `success`, `message`, `error`.
- **Limits:** unofficial endpoint; a 600 ms pause; its own direction codes; narrower language
  coverage than Google/DeepL.

**Implementation in qtranslate (2026-11-09).**

- The `reverso` service: `POST https://cps.reverso.net/api2/TranslateText` with
  `Content-Type: application/json`, `Accept: application/json` and a fixed desktop Chrome
  `User-Agent` on this request only; no key (`isConfigured() == true`). The body is a compact JSON
  object with seven string values: `source` (the chunk text), `direction` (`<source>-<target>`),
  `deviceId` (`"0"`), `uiLang` (`"en"`), `origin` (`"chromeextension"`), `accessToken` (`""`) and
  `appId` (`"0"`). `deviceId`/`appId` are strings, not numbers. No cookie is read or written and no
  `Cookie` header is sent; `SameOriginRedirectPolicy` refuses a cross-origin redirect.
- Languages: only `en`, `fr`, `de`, `es`, `it` and `ru`, taken from the upstream mapper; the internal
  codes equal Reverso's, and the six languages form the full 36-direction cross product. `auto` is
  not supported: it finishes with `UnsupportedLanguage` and the message
  `Reverso needs an explicit source language; choose one instead of Auto-detect` before any request,
  and any unmapped code is rejected the same way.
- 5000 UTF-16 code units per request (`TextChunker::split`) and a 600 ms pause between requests are
  conservative assumptions, not measured limits; chunks are sent sequentially inside one queue slot
  of `QueuedWebService`, edge whitespace is restored, and a failed chunk fails the whole reply
  without a partial translation.
- Response: a flat object with `error`, `success`, `translation`, `directionFrom`, `directionTo` and
  `message`. `error == true` or `success == false` is an error even with HTTP 200, so the body is
  classified before the status. `success == true` without a non-empty string `translation` is
  `BadResponse`; `directionFrom` fills `detectedSourceLang` through `Languages::fromServiceCode`,
  `directionTo` is parsed and tolerated but does not change the result, and extra fields are ignored.
- Errors: 403/429 or a rate-limit body → `RateLimited`; 413 → `TooLong`; 5xx → `Network`; an HTML
  page, a blocked body or another provider error object → `ServiceUnavailable`; malformed JSON, a
  missing success flag/translation or an unknown non-2xx status → `BadResponse`; timeout/abort →
  `Timeout`/`Cancelled`. The only variable text is the provider `message` after the short/markup/
  control-character check; it may reach `errorMessage` but is never logged, and the text, the body
  and the URL never appear in a message or the log.
- Available without a gate, like the other no-key services; there is no key, URL or
  `AppSettings`/INI field; `applySettings()` is empty. The settings hint warns that the endpoint is
  unofficial.
- Code: `src/services/reversoparser.*`, `src/services/reversoservice.*`; tests:
  `tst_reversoparser`, `tst_reversoservice`, `tst_languages`, `tst_settingsdialog`. CTest never
  contacts the real endpoint; a live smoke is manual only and is recorded as passed or as not
  performed with the reason (a block, a rejected pair, or no live run).

## 5. Mozhi

- **Request:** `GET https://<instance>/api/translate?engine=<engine>&from=<src>&to=<dst>&text=...`
- **Response:** `"translated-text"`, `detected`, `"source_language"`, `"target_transliteration"`.
- **Engines:** Google, DeepL, DuckDuckGo, LibreTranslate, MyMemory, Reverso, Yandex.
- **Instances:** public (`mozhi.adminforge.de`, `mozhi.pussthecat.org`, and others) or self-hosted;
  public ones may be unavailable, so instance selection and a fallback are needed.
- **Pro:** one integration gives several engines and a self-hosted option.
- **Con:** it is a proxy to third-party endpoints; stability and terms depend on the instance.

**Implementation in qtranslate (2026-11-09).**

- The `mozhi` service: `GET <instance>/api/translate?engine=<engine>&from=<src>&to=<dst>&text=<text>`
  with `Accept: application/json`; no key (`isConfigured() == true`). The instance is a base URL, so
  a path prefix is kept (`https://host/mozhi` → `https://host/mozhi/api/translate`), and the settings
  reject credentials, a query and a fragment. The instance is editable, with the default
  `https://mozhi.adminforge.de`; there is no hidden instance rotation, so the text goes only to the
  configured instance.
- The engine is editable, default `google`, with the documented ids `google`, `deepl`,
  `duckduckgo`, `libretranslate`, `mymemory`, `reverso` and `yandex`; no key or other secret field.
- Language codes reuse the Google column: `zh-Hans`→`zh-CN`, `zh-Hant`→`zh-TW`, `he`→`iw`,
  `jv`→`jw`, `nb`→`no`, `fil`→`tl`, `pt-BR`→`pt`; `mozhiCodeOf()` is the single fix point. `auto`
  is sent as `auto` (the default `google` engine accepts it); a rejected pair becomes
  `UnsupportedLanguage`. An engine can pass an upstream error through `translated-text` with HTTP
  200 (seen with `engine=mymemory` and `auto`), so `auto` depends on the engine.
- 5000 UTF-16 code units per request (`TextChunker::split`) and a 500 ms pause between chunks are
  conservative assumptions, not measured limits; chunks are sent sequentially inside one queue slot
  of `QueuedWebService`, edge whitespace is restored, and HTTP 413/414 → `TooLong` means the
  percent-encoded URL got too long.
- Every request uses `SameOriginRedirectPolicy`: a cross-origin redirect is refused.
- Response fields: `translated-text` (required), `detected`, `source_language`,
  `target_transliteration`; `detected` fills `detectedSourceLang`, falling back to a non-`auto`
  `source_language`.
- Errors: 403/429 or a rate-limit body → `RateLimited`; 413/414 → `TooLong`; 5xx without a
  recognised body → `Network`; an HTML page, a disabled engine or a JSON `error` →
  `ServiceUnavailable`; an invalid-language body → `UnsupportedLanguage`; malformed JSON, a
  missing/empty text or an unknown non-2xx status → `BadResponse`; timeout/abort →
  `Timeout`/`Cancelled`. The body is classified before the status; messages are fixed literals
  without the text, the body or the URL.
- Available without a gate, like MyMemory; `applySettings()` reads the normalized `[mozhi]` values.
  The settings hint warns about unofficial instances and rate limits.
- Code: `src/services/mozhiparser.*`, `src/services/mozhiservice.*`; tests: `tst_mozhiparser`,
  `tst_mozhiwebservice`, `tst_languages`, `tst_settings`, `tst_settingsdialog`. CTest never contacts
  a real instance; a live smoke is manual only.

## 6. MyMemory

- **Request:** `GET https://api.mymemory.translated.net/get?q=...&langpair=en|ru&mt=1`
- **Response:** `responseData.translatedText`, `responseStatus`, `responseDetails`, `quotaFinished`.
- **Limits:** a free API, but about **500 bytes per request**; an explicit source language is
  required (auto-detection is not supported); there is a quota; QTranslate keeps a 350 ms pause and
  splits text into 500-byte chunks.
- **Pro:** no key is needed, and it is more predictable than scraping.
- **Con:** short requests, mixed quality, and many requests for long texts.

**Implementation in qtranslate (2026-11-08).**

- The `mymemory` service: `GET https://api.mymemory.translated.net/get?q=…&langpair=<src>|<dst>&mt=1`
  with `Accept: application/json`; no key, no URL or email setting, `isConfigured() == true`.
- Explicit source language only: `auto` finishes with `UnsupportedLanguage` before the request.
  The codes follow Google's catalogue with `zh-Hans`→`zh-CN`, `zh-Hant`→`zh-TW`, `nb`→`no`,
  `fil`→`tl`; the other languages use the internal code.
- 500 UTF-8 bytes per chunk (`TextChunker::splitUtf8`); a long text is split, the chunks are sent
  one after another inside one queue slot of `QueuedWebService` with the 350 ms pause, and edge
  whitespace is restored when the parts are joined.
- Errors: 403/429 → `RateLimited`, quota details or `quotaFinished` → `QuotaExceeded`, 413/414 or a
  query-length detail → `TooLong`, 5xx → `Network`, HTML → `ServiceUnavailable`, a malformed or
  unknown status → `BadResponse`. The messages are fixed literals and never contain the text, the
  URL or `responseDetails`.
- Available without a gate, like the other no-key services; the settings hint warns about the
  unofficial endpoint and rate limits.
- Code: `src/services/mymemoryparser.*`, `src/services/mymemoryservice.*`; tests:
  `tst_mymemoryparser`, `tst_mymemoryservice`, `tst_languages`, `tst_textchunker`.

## 7. DeepL API: the current free tier

This section used to list `DeepL API Free` with 500,000 characters per month. That is outdated: now
**DeepL API Developer** gives **1,000,000 characters in total**, and this limit **does not reset
monthly**. After it is exhausted, you have to move to DeepL API Growth. Keys with the `:fx` suffix
still use the `api-free.deepl.com` host, but this is no longer an endless monthly free tier.

Practical conclusion: an official DeepL key is effectively a 1-million-character trial, not a
permanent free option. Therefore the DeepL web endpoint is useful as a free mode or fallback, but it
remains unofficial.

## 8. How to integrate

1. DeepL web — the `free-web` mode inside `DeepLService` (the internal `DeepLWebService`) and a
   warning in settings; there is no separate service. The DeepL `free-web` mode stays an explicit
   mode with its own warning.
2. For each service — its own `TranslationService` and parser; no keys are needed (LibreTranslate
   takes an optional key). Implemented for MyMemory (`src/services/mymemory*`), Mozhi
   (`src/services/mozhi*`), Yandex web (`src/services/yandexweb*`), Bing web
   (`src/services/bingweb*`), Reverso (`src/services/reverso*`), DuckDuckGo
   (`src/services/duckduckgo*`) and LibreTranslate (`src/services/libretranslate*`); they are the
   seven no-key web services, available by default like every other provider.
3. Per-host rate limiting: implemented infrastructure in `QueuedWebService` — replies of one host
   are serialized and paused by the service-specific value (DeepL 2 s, MyMemory 350 ms, Mozhi
   500 ms, Yandex web 750 ms, Bing web 1000 ms, Reverso 600 ms, DuckDuckGo 1000 ms, LibreTranslate
   500 ms); no parallel requests to one host.
4. Text splitting: implemented for DeepL (5000 characters), MyMemory (500 UTF-8 bytes), Mozhi
   (5000 UTF-16 code units), Yandex web (5000 UTF-16 code units), Bing web (5000 UTF-16 code units),
   Reverso (5000 UTF-16 code units), DuckDuckGo (900 UTF-16 code units) and LibreTranslate
   (5000 UTF-16 code units); the rest — according to the service limits.
5. Errors: `429`/`403`/an HTML stub → `RateLimited`/`ServiceUnavailable` with a clear message,
   without a retry storm.
6. Fallback: implemented as one configured hop in `TranslationManager` — a
   `translation/fallbackService` service (`yandex-web`, `bing-web`, `mymemory`, `mozhi`, `reverso`,
   `duckduckgo` or `libretranslate`) is tried once when the primary fails with `Auth`,
   `QuotaExceeded`, `RateLimited`, `ServiceUnavailable`, `Network` or `Timeout`; success,
   `EmptyText`, `TooLong`, `UnsupportedLanguage`, `Cancelled` and `BadResponse` never fall back. A
   failed fallback returns the primary error; the fallback is not retried and there is no second hop.
   A change clears the cache. Do not show raw HTML. See [`architecture.md`](architecture.md) §4.9 and
   §4.11.
7. Privacy: the text goes to a third-party service; do not write text, keys, or cookies to the log.
8. Tests: response fixtures and a local `QTcpServer`; real endpoints — manual smoke only, because
   they are live and unstable.

## 9. Priorities

1. **DeepL web** — implemented: high quality but high risk; available as a DeepL mode.
2. **MyMemory** — implemented: the simplest and relatively official free API.
3. **Mozhi** — implemented: one integration, many engines, self-hosting possible.
4. **Yandex web** — implemented: important for the Russian audience, but an unofficial scraped
   endpoint.
5. **Bing web** — implemented: fragile auth and HTML, one of the no-key web services.
6. **Reverso** — implemented: niche, fewer languages, no automatic detection, but a clear fallback
   among its six languages.
7. **DuckDuckGo** — implemented: a token-gated frontend route with the smallest body limit; the user
   accepted it on 2026-11-15 because Bing already uses the same token/User-Agent pattern.
8. **LibreTranslate** — implemented: a server the user points the application at, self-hosted or
   hosted with an optional key; there is no public key-less default.

The fallback itself is implemented as one configured hop after the seven services, not as an
arbitrary chain: Google and DeepL free modes are modes of the same service, so they are not separate
fallback links; Mozhi proxies to engines, so choosing Mozhi as the fallback already covers a
different engine. The user picks one fallback service; the primary stays the primary.

## 10. What not to do

- Do not present unofficial endpoints as an official API. They are available by default, but the
  settings hints must keep the unofficial, rate-limit and block warnings.
- Do not bypass paid quotas or publish other people's keys/cookies.
- Do not use public Mozhi/LibreTranslate instances as the only channel without a fallback.
- Do not present DuckDuckGo as a public API. It is a frontend route guarded by a `vqd` token read
  from the page HTML and a browser User-Agent; the standalone provider is a user decision
  (2026-11-15) and may be blocked or changed without notice.
- Do not copy QTranslate code without complying with MPL-2.0; write implementations yourself from
  the observed HTTP protocol.

## 11. DuckDuckGo (standalone, added 2026-11-15)

- Route: `GET https://duckduckgo.com/?q=translate` with a browser User-Agent, then
  `vqd="..."` from the returned HTML; `POST https://duckduckgo.com/translation.js?vqd=<token>&query=translate&to=<code>[&from=<code>]`
  with the raw text as the body and `Content-Type: application/json`; the response is
  `{"detected_language": "...", "translated": "..."}`. No cookies. A token still answered after 26
  minutes; a `403` refreshes it once per reply, a second `403` becomes `RateLimited`.
- Limits: the body between 986 and 2006 characters (986 accepted, 2006 rejected as `invalid_text`);
  the service splits at 900 UTF-16 code units with a 1000 ms pause, both named assumptions.
- Languages: DuckDuckGo is Bing Translate 1-1 (Mozhi `langlist-duckduckgo.go`, libmozhi commit
  `7324b5c528fe`); the language column reuses the observed Bing catalogue plus `es`. `es` is missing
  from `kBingWebCodes`, so `bing-web` does not offer Spanish — a separate Bing gap.
- Terms: an internal frontend route, not an API; the terms do not bless automated use, and it can be
  rate-limited, challenged or changed without notice. The user accepted this on 2026-11-15 because
  Bing already uses the same pattern.
- Tests: `tst_duckduckgoparser` and `tst_duckduckgoservice` with fixtures and a local `QTcpServer`;
  CTest never contacts the real endpoint.

## 12. LibreTranslate (standalone, added 2026-11-15)

- Route: `POST <base>/translate` with the JSON body `q`, `source`, `target` and `api_key` when set;
  the response is `translatedText` with an optional `detectedLanguage.language`/`confidence`;
  `source:"auto"` is supported; `/languages` and `/health` exist. The base URL is a server the user
  enters (usually self-hosted); there is no default, and an empty base URL means "not configured".
- Key: optional. A self-hosted server without `--api-keys` needs none; a hosted instance such as
  `libretranslate.com` requires one. The key is stored as a secret (DPAPI/libsecret/`0600`) and sent
  as `api_key`. The service sends `Accept-Language: en` so error bodies stay English and `Auth` is
  classified on any system locale.
- Languages: the 51 codes of the observed `/languages` (49 map directly; `fil`→`tl`,
  `pt-PT`→`pt`); a server with fewer installed models rejects a pair and maps to
  `UnsupportedLanguage`.
- Limits: 5000 UTF-16 code units per chunk and a 500 ms pause are named assumptions; the server's own
  limits are its configuration (`--req-limit`, `--api-keys`).
- Licence: AGPL-3.0 server; QTranslate stays GPL-3.0-or-later and ships only an HTTP client, no server
  code or binary.
- Tests: `tst_libretranslateparser` and `tst_libretranslateservice` with fixtures and a local
  `QTcpServer`; CTest never contacts a real server.
