---
kind: task
status: todo
depends: [01-free-web-infrastructure]
---

# Yandex web service

## Task

Add the unofficial Yandex web endpoint as an opt-in service. It is important for the Russian
audience because it covers ru/uk/tr and several CIS languages better than the other free services,
but it is a scraped browser endpoint with no SLA and must stay a fallback.

Use `doc/free-endpoints.md` §2 for the protocol, §8 for the rules, and §9 for the priority. The
service is `yandex-web` and implements `TranslationService` with `requiresOptIn() == true`. It sends
`POST https://api.browser.yandex.com/instaserp/translate` with
`application/x-www-form-urlencoded` and the fields `text`, `brandID=int`, `statLang=<target>`,
`targetLang=auto`, `locale=<target>`, `clid=2270494`, `disable=serp`, `use_llm_srv=0`. The endpoint
expects a browser User-Agent (Chrome/YaBrowser); set it on this request only, overriding the common
`AppInfo::userAgent()` from `NetworkManager::makeRequest()`, and document that this is protocol
emulation, not a claim that the app is a browser. The response is a JSON array; the first element
contains `text` and the detected language. A new pure parser extracts them, tolerates extra fields,
and returns `BadResponse` on an unexpected shape.

The service uses `QueuedWebService` with a 750 ms pause and sends long text in 5000-character chunks
sequentially inside one reply, restoring edge whitespace. The source language may be `auto`; map the
internal codes to the Yandex form and declare only the codes with a known mapping; an unmapped
language returns `UnsupportedLanguage` before any request. Errors: 429/403 and a rate-limit body
become `RateLimited`; 5xx becomes `Network`; an HTML page or a blocked response becomes
`ServiceUnavailable` without raw HTML; a timeout and `abort()` stop the request. The form body
contains the text, so the error/log messages must not include the body, the URL query, or the text,
and the browser User-Agent must not be logged as if it were a secret (it is not one, but keep the log
short).

The settings entry is a Yandex group with the free/unofficial warning and the opt-in hint; there are
no keys or per-service URLs. Tests use fixtures and a local `QTcpServer`; a live smoke is manual
because the endpoint may block an IP. The doc's note that QTranslate uses `engine=yandex` through
Mozhi on failure is not this task: the fallback chain is `07-fallback-chain`, and Mozhi's engine
selection already provides the proxy path.

## Done when

- `YandexWebService` and `YandexWebParser` exist under `src/services/`, are listed in
  `CMakeLists.txt`, and return the internal `yandex-web` id with `requiresOptIn() == true`; the
  service is not active by default.
- `tst_yandexwebparser` covers a normal array response with `text` and a detected language, an empty
  array, a missing/string-instead-of-array body, an HTML stub, and malformed JSON; each case produces
  the documented error and a message without the text.
- `tst_yandexwebservice` with a local `QTcpServer` verifies the POST path, the
  `application/x-www-form-urlencoded` content type, every documented field including `brandID`,
  `clid`, `disable`, and `use_llm_srv`, the browser User-Agent on this request, URL encoding of
  spaces and non-ASCII text, and that other services' requests still use `AppInfo::userAgent()`.
- A supported pair is sent with the mapped Yandex codes; `auto` is sent as the documented `targetLang`
  form; an unmapped language returns `UnsupportedLanguage` before any request; the detected language
  is mapped back to the internal code.
- Long text is split into 5000-character chunks at paragraph/sentence boundaries and sent
  sequentially with the 750 ms pause (`setRateLimitMs(0)` in tests); the result restores edge
  whitespace and matches the concatenation of the chunk translations.
- HTTP 429/403 and a rate-limit body return `RateLimited`; 5xx returns `Network`; an HTML/blocked
  response returns `ServiceUnavailable` without raw HTML; a timeout and `abort()` finish with
  `Timeout`/`Cancelled` and stop the request.
- Requests to the host are serialized through `QueuedWebService`; a second translation waits for the
  active one and for the pause; cancelling a queued request does not send it.
- The settings dialog has a Yandex group with the free/unofficial warning; the opt-in gate hides or
  refuses the service as described in `01-free-web-infrastructure`; `tst_settingsdialog` covers the
  group.
- `doc/architecture.md` §4.5/§4.9 and `doc/free-endpoints.md` §2/§8/§9 are updated; both READMEs list
  Yandex web as an unofficial fallback; the manual live smoke is recorded as passed or as not
  performed with the reason (the endpoint may return a captcha/403).
- Debug and Release build without warnings; the full suite passes with the new tests; Windows is
  recorded as not performed when MSVC is unavailable.

## Risks

- The endpoint is unofficial and browser-oriented. A browser User-Agent is required today, but the
  fields, the clid, or the response shape may change without notice; the parser must fail with a
  clear `BadResponse`/`ServiceUnavailable`, never a crash or raw HTML. Do not add a captcha solver,
  cookie harvesting, or any bypass of a block.
- The terms may restrict automated use. Keep the service opt-in, mark it unofficial, and do not make
  it the only translation method; the user must enable it explicitly.
- The endpoint may block a datacenter or a country IP. The automated gate is the local-server test;
  a live smoke is manual and its failure is a recorded result, not a code change.
- The browser User-Agent is set per request; do not change `NetworkManager::makeRequest()` for all
  services, because the free Google endpoint and the official APIs rely on the app User-Agent.
- The doc gives no Yandex-specific chunk limit. The 5000-character chunk is a conservative
  assumption; keep it in a named constant and record any measured limit from a live smoke.
