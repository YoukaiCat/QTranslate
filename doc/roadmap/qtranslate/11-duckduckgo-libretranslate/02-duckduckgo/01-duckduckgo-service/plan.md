---
kind: task
status: done
detailed: true
---

# DuckDuckGo parser and service

## Task

Implement the DuckDuckGo protocol recorded in
`../../01-endpoint-research/notes/duckduckgo-libretranslate.md` (§2.1-§2.3, §4 and Appendix A.3) as a
pure parser plus a `QueuedWebService`, register the service, add its language column and the fallback
id, and cover it with fixtures and local-server tests. No settings key, no UI, no `.ts` and no `doc/`
change: the group and the strings belong to `02-duckduckgo-ui`.

The service id is `duckduckgo` and the display name is the product name `DuckDuckGo`, untranslated,
like `Google` and `Reverso`. The route is the search frontend's `translation.js`: a `vqd` token read
out of the HTML of `https://duckduckgo.com/?q=translate` with a browser User-Agent, then the text
posted to `https://duckduckgo.com/translation.js?vqd=<token>&query=translate&to=<code>[&from=<code>]`
with `Content-Type: application/json`; the response is `{"detected_language":"en","translated":"…"}`.
No cookies were sent or received, and a token scraped at 22:44 still answered at 23:10, so the cache
carries no lifetime of its own. `isConfigured()` returns true; `applySettings()` stays empty, because
no `AppSettings` value reaches this protocol.

The user accepted the token scrape on 2026-11-15 because `BingWebService` already reads
`params_AbusePreventionHelper` from a page and overrides the User-Agent per request
(`src/services/bingwebservice.cpp:233-294`); this task copies that shape, not the Mozhi engine path.
The `duckduckgo` service id coincides in spelling with the `duckduckgo` *Mozhi engine* id
(`mozhiEngineIds()`, `src/core/settings.cpp:298-306`, `src/ui/settingsdialog.cpp:386`); the two are
different namespaces and neither the engine list nor the Mozhi service changes.

Language handling: `duckDuckGoCodeOf()` in `src/core/languages.cpp` reuses the observed Bing catalogue
(`kBingWebCodes` and `kBingWebOverrides`, `src/core/languages.cpp:236-252`) instead of declaring a
table of its own. The inheritance comes from the source: the note's follow-up probe (2026-11-15)
confirms the route answers like Bing, Mozhi documents the engine as "1-1 with Bing Translate", and
`langlist-duckduckgo.go` in `libmozhi` at commit `7324b5c528fe` is that Bing list converted to
Mozhi's format. The probe answered `200` with a translation for `de`, `fr`, `es`, `it`, `pt`,
`zh-Hans`, `ja`, `ko`, `pl`, `uk`, `tr`, `ar`, `hi` and `ru` as targets. Every other code in the
inherited catalogue is marked in the code as inherited and unverified: a code the route does not
accept answers `400 {"error":"invalid_to"}` → `UnsupportedLanguage` after one request. The four-code
table of the first note is superseded and must not come back.
`auto` **is** supported by the route: omitting `from` returned `detected_language` (`fr` for a French
sentence), and an explicit `from=ru` returned `detected_language: null`.

Chunking and pacing are named constants taken from the research. The observed body limit lies between
986 and 2006 characters, so the chunk is 900 UTF-16 code units, below the lowest proven success; the
pause has no measurement behind it and stays a conservative named assumption, as `MozhiService` and
`BingWebService` do. Chunks are sent sequentially inside one reply, edge whitespace is restored, and
a failed chunk fails the whole reply without a partial translation.

Errors are classified body-first, as in `BingWebService` and `ReversoService`: a `403` on the
translate request refreshes the token once and then becomes `RateLimited`, a `429` is `RateLimited`
directly; `400 {"error":"invalid_text"}` → `TooLong`;
`400 {"error":"invalid_to"}` → `UnsupportedLanguage`; `5xx` without a recognised body → `Network`; an
HTML page, a cookie challenge or an unknown provider error object → `ServiceUnavailable`; malformed,
empty or missing translation → `BadResponse`; unknown non-2xx → `BadResponse`; timeout and `abort()`
→ `Timeout`/`Cancelled`. Every request uses `SameOriginRedirectPolicy`. No message or log line
contains the text, the body, the token or the URL, and network errors use
`NetworkManager::describeNetworkError(request, false)`, never `QNetworkReply::errorString()`.

## Steps

1. **Add the fixtures.** Create `tests/fixtures/duckduckgo/` with synthetic bodies only (no token, no
   text and no page from the probe). The shapes are the ones quoted in the research note, Appendix
   A.3:

   - `token_page.html` — a small HTML page whose script contains
     `vqd="4-00000000000000000000000000000000000"`, preceded by one unrelated `vqd` mention
     (`vqd=` without quotes) and followed by other markup, so the parser has to pick the quoted value
     and nothing else;
   - `token_page_missing.html` (no `vqd`), `token_page_empty.html` (`vqd=""`),
     `token_page_truncated.html` (no closing quote), `token_page_control.html` (a `\n` inside the
     token);
   - `response.json` — `{"detected_language":"en","translated":"Hallo"}` exactly;
     `response_no_detected.json` — `{"detected_language":null,"translated":"Good morning"}` (the
     explicit-source shape); `response_missing_detected.json`, `response_missing_translated.json`,
     `response_empty_translated.json`, `response_whitespace_translated.json`,
     `response_non_string_translated.json`, `response_array.json`, `response_string.json`,
     `response_malformed.json`, `response_empty.json`;
   - `error_invalid_text.json` — `{"error":"invalid_text"}`; `error_invalid_to.json` —
     `{"error":"invalid_to"}`; `error_forbidden.json` — `{"error":"Forbidden"}`;
     `error_unknown.json` — `{"error":"something_new"}`;
   - `html_challenge.html` (a captcha/consent-looking page), `rate_limit.txt`
     (`Too many requests`), `blocked.txt` (`Forbidden: automated access`), `plain.txt`
     (`Hello there`, not an error).

   Keep the filenames listed here; the parser test reads them through
   `QTRANSLATE_FIXTURES_DIR "/duckduckgo/"`, as `tests/tst_bingwebparser.cpp:10-16` does.

2. **Write the parser.** Create `src/services/duckduckgoparser.h/.cpp` with
   `namespace DuckDuckGoParser`, no network and no state, modelled on `src/services/bingwebparser.*`:

   - `bool tokenFromPage(const QByteArray &html, QString *token, QString *errorMessage)`: the first
     `vqd="` in the page and the bytes up to the next `"`; a missing marker, an empty value or a
     value with a control character (`< 0x20`) fails with the fixed message
     `the token page has no usable vqd token`. Do not parse the page as HTML, do not run JavaScript
     and do not accept an unquoted `vqd=`;
   - `TranslationResult parseResponse(const QByteArray &body)`: call `parseErrorBody()` first (the
     endpoint reports errors under HTTP 200 as well), then require a JSON object whose `translated`
     is a non-empty, non-whitespace string, else `BadResponse` with the fixed
     `no translated text in the response`. `detected_language` is optional and may be JSON `null`
     (observed with an explicit source): a string is mapped through
     `Languages::fromServiceCode("duckduckgo", value, Direction::Source)`, everything else leaves
     `detectedSourceLang` empty. Any other field is ignored;
   - `TranslationResult parseErrorBody(const QByteArray &body)`: a body whose first non-space byte is
     `<` → `ServiceUnavailable`, `the endpoint returned an HTML page`; a JSON object with an `error`
     string → `invalid_text` → `TooLong`, `the request is too long for the endpoint`;
     `invalid_to` → `UnsupportedLanguage`, `the endpoint rejected the language`; any other value,
     including `Forbidden`, → `ServiceUnavailable`, `the endpoint returned an error`. Compare the
     `error` value exactly and case-insensitively, never by substring: these are the only two codes
     the research recorded, and a new one must not be mistaken for either;
   - a plain-text body (not JSON) is classified by fixed block words, as `BingWebParser` does
     (`src/services/bingwebparser.cpp:38-67`): rate-limit words → `RateLimited`,
     `the endpoint reported a rate limit`; block words → `ServiceUnavailable`,
     `the endpoint is blocked or unavailable`; an empty body or any unrecognised text returns an
     empty `TranslationResult` (not an error), so the caller falls back to the status;
   - `TranslationError errorForHttpStatus(int status)`: `403` and `429` → `RateLimited`, `413` →
     `TooLong`, `500`-`599` → `Network`, everything else (`400`, `404`, an unfollowed `3xx`) →
     `BadResponse`.

   Include `core/languages.h` and `core/translationtypes.h`; every message is a fixed literal that
   never repeats the body, the page, the text or a URL.

3. **Add the language column.** In `src/core/languages.cpp`, next to `kReversoCodes` and
   `reversoCodeOf()`:

   - `constexpr QLatin1String kDuckDuckGo("duckduckgo");` after `kReverso`;
   - `constexpr const char *kDuckDuckGoExtraCodes[] = {"es"};` — the only code the follow-up probe
     confirmed that the Bing table does not declare. Do not add `es` to `kBingWebCodes`: that table is
     the Bing evidence, and widening Bing's provider coverage is not this task and has no probe behind
     it; note the gap in the report instead;
   - `duckDuckGoCodeOf(const LanguageInfo &info)`: return the identity code when `info.code` is in
     `kDuckDuckGoExtraCodes`, otherwise return `bingWebCodeOf(info)`. That is the whole function, with
     the comment that the DuckDuckGo frontend route is Bing Translate behind the search page (Mozhi
     "1-1 with Bing Translate"; `libmozhi`'s `langlist-duckduckgo.go` at `7324b5c528fe` is the Bing
     list converted), that the catalogue is therefore inherited from `kBingWebCodes` and
     `kBingWebOverrides` rather than copied, that the probe confirmed `de`, `fr`, `es`, `it`, `pt`,
     `zh-Hans`, `ja`, `ko`, `pl`, `uk`, `tr`, `ar`, `hi` and `ru`, and that every other inherited code
     is **inherited and unverified**;
   - a `serviceId == kDuckDuckGo` branch in `serviceCodeOf()` returning `duckDuckGoCodeOf(info)`.

   Reuse the table through this function instead of duplicating it, following `mozhiCodeOf()`
   (`src/core/languages.cpp:280-286`): one list, one named fix point where the two providers can
   diverge later, and no second 80-entry array to keep in sync. A duplicate would also hide the fact
   that `es` is the one confirmed code Bing lacks. No synonym is needed: `Languages::fromServiceCode()`
   scans `serviceCodeOf()` in both directions (`src/core/languages.cpp:430-451`), so
   `pt`→`pt-BR`, `sr-Cyrl`→`sr` and the identity rows already work. The Bing-only synonym
   `sr-Latn`→`sr` stays scoped to `bing-web`, because nothing was observed about what DuckDuckGo
   reports. Do not touch `kSynonyms`, the Mozhi column or the Bing table.

4. **Add the service class and its seams.** Create `src/services/duckduckgoservice.h/.cpp` with
   `class DuckDuckGoService : public QueuedWebService`:

   - constructor `explicit DuckDuckGoService(NetworkManager *network, QObject *parent = nullptr)`
     storing `m_network`, setting `m_endpoint = defaultEndpoint()` and calling
     `setRateLimitMs(kDefaultRateLimitMs)`;
   - `id()` → `duckduckgo`; `displayName()` → `DuckDuckGo`; `isConfigured()` → `true`;
     `sourceLanguages()`/`targetLanguages()` through `Languages::supported(kServiceId, …)`;
     `applySettings(const AppSettings &)` with an empty body and the comment that no DuckDuckGo
     setting reaches the protocol and that the token is not tied to a setting (a token invalidated by
     a proxy change is refreshed on the first `403`), so the cache is **not** cleared here;
   - the test seam `void setEndpoint(const QUrl &url)`, `QUrl endpoint() const` and
     `static QUrl defaultEndpoint()` returning `https://duckduckgo.com/`. `setEndpoint()` falls back
     to the default for an empty or invalid URL, makes the path end with `/` (so
     `QUrl::resolved()` cannot climb out of it) and clears the cached token, because a token belongs
     to the host that issued it;
   - `static QString browserUserAgent()` returning a fixed Chrome UA string, duplicated here rather
     than borrowed from `BingWebService`, so each service owns its identity;
   - private `class Reply;`, `NetworkManager *m_network`, `QUrl m_endpoint` and
     `QString m_token; // cached vqd; no lifetime of its own`, plus
     `Q_LOGGING_CATEGORY(lcDuckDuckGo, "qtranslate.service.duckduckgo", QtWarningMsg)` in the `.cpp`.

   File-local constants with their justification comments: `kServiceId = "duckduckgo"`,
   `kMaxChunkUnits = 900` (the research accepted 986 characters and rejected 2006 with
   `invalid_text`, so 900 is below the lowest proven success and a UTF-16 unit is never more than one
   character), `kDefaultRateLimitMs = 1000` (no measured pause exists; one request per second to a
   scraped frontend is the conservative named assumption, and the queue already keeps one request per
   host in flight). Copy the `Part` struct, `splitParts()`, `finishedWithError()` and `errorKind()`
   helpers from `src/services/bingwebservice.cpp:38-66` and `src/services/reversoservice.cpp:86-126`.

5. **Implement `translate()`.** In `DuckDuckGoService::translate()`:

   - `request.text.trimmed().isEmpty()` → `EmptyText` with an empty message through
     `finishedWithError()`, before any request;
   - `auto`: when `request.sourceLang == Languages::autoCode()`, no `from` is sent at all — the route
     detects the language and reports it; there is no `auto` value to send, so do not invent one;
   - otherwise map the source and the target with `Languages::toServiceCode(kServiceId, …,
     Direction::Source)` and `…::Target`; an empty mapped code (`ceb`, `kk`, `pa`, `haw`, `xx`, …)
     returns `UnsupportedLanguage` with `language "<code>" is not supported`, naming the unmapped
     side, before any request — the same message style as `ReversoService`. Do not special-case
     `auto` with a separate message: this route supports it;
   - `const QList<Part> parts = splitParts(request.text);`, log only
     `Translating <n> part(s), <source|auto> -> <target>` (internal codes, no text), create one
     `Reply` with the endpoint, the parts, the mapped source (empty for auto) and the target,
     `enqueue(reply)` and return it. One text is one reply, however many chunks it has.

6. **Implement the token stage of `Reply`.** The nested `Reply` follows
   `BingWebService::Reply` (`src/services/bingwebservice.cpp:112-263`):

   - `start()` starts `m_elapsed` and schedules `send()` through
     `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`, so `finished()` is never emitted from
     inside `translate()`; `~Reply()` and `abortImpl()` call `stopRequest()` (`disconnect`, `abort`,
     `deleteLater`), and `abortImpl()` logs `Cancelled`;
   - `send()` returns when finished, fails with `Network` and the fixed
     `network is not available`/`the service is not available` when `m_network`/`m_service` is gone,
     and otherwise starts the first unsent non-empty part (`nextUnsentPart()`);
   - when `m_service->m_token` is empty, run the token stage first: `GET <endpoint>?q=translate`
     (`QUrlQuery` with `q=translate` on the endpoint URL), `Accept: text/html`, then
     `noteRequestSent()` directly before `m_network->nam()->get()`, and
     `QNetworkReply::finished` connected to `onFinished()`; record `m_requestElapsed` so the log line
     can carry the duration;
   - `preparedRequest(url)` builds `m_network->makeRequest(url)`, sets
     `QNetworkRequest::UserAgentHeader = browserUserAgent()` **after** `makeRequest()` (which set
     `AppInfo::userAgent()`), sets `CookieLoadControlAttribute` and `CookieSaveControlAttribute` to
     `Manual` (the route needs no cookie, and the shared jar must neither supply nor receive one) and
     `SameOriginRedirectPolicy` (the token and the text must never move to another origin). Keep this
     in one helper so both stages cannot drift apart;
   - `finishTokenPage()`: a non-2xx status classifies the body first through
     `DuckDuckGoParser::parseErrorBody()` and otherwise fails with
     `DuckDuckGoParser::errorForHttpStatus(status)` and `HTTP <status>`; any other
     `QNetworkReply::NetworkError` fails with `Network` and
     `m_network->describeNetworkError(request, false)`; a `2xx` body goes to
     `DuckDuckGoParser::tokenFromPage()`, whose failure is `ServiceUnavailable` with its fixed
     message. On success store the token in `m_service->m_token` and `scheduleSend()`, so the GET →
     POST pair is paced too;
   - the token stage never refreshes itself: a `403` on the page is a block, so it becomes
     `RateLimited` through `errorForHttpStatus()` and there is no loop.

7. **Implement the POST stage, the chunk loop and the single refresh.** In the same `Reply`:

   - `postChunk()` posts to `m_endpoint.resolved(QUrl("translation.js"))` with the query items in the
     recorded order: `vqd=<token>`, `query=translate`, `to=<target>` and `from=<source>` only when
     the source is explicit. `QUrlQuery` percent-encodes the token, so a token with `+`, `/` or `=`
     cannot break the query;
   - the body is `m_parts.at(m_current).text.toUtf8()` — the chunk text **as is**, not a JSON
     document: the endpoint takes the raw text and only its `Content-Type` says `application/json`.
     Wrapping the text in `{"text":…}` or quoting it breaks the route, so do not "fix" this;
   - headers: `Content-Type: application/json`, `Accept: application/json`, the browser User-Agent and
     the cookie/redirect attributes from `preparedRequest()`; keep `m_usedToken` for the refresh
     guard; call `noteRequestSent()` immediately before `nam()->post(request, body)`;
   - `onFinished()` takes the status and the error, reads the body only when the transfer completed,
     `deleteLater()`s the reply, returns when finished, logs `HTTP <status> in <elapsed> ms`, and
     turns `OperationCanceledError`/`TimeoutError` into `Timeout` with the fixed
     `DuckDuckGo did not respond within <n> s`, formatting `m_network->timeoutMs()` with
     `QString::number(seconds, 'g', 3)` exactly as the sibling services do;
   - `403` in the POST stage → `handleTokenRejection()`: when `m_refreshed` is already set, fail with
     `RateLimited`, `the endpoint rejected the token`; otherwise set `m_refreshed`, clear
     `m_service->m_token` **only** if it still equals `m_usedToken` (a token another reply already
     replaced must not be dropped), keep `m_current`, and `scheduleSend()`, so the same chunk is
     resent with the fresh token. One refresh per reply, never a loop: a second `403` is the block;
   - any other non-2xx: `DuckDuckGoParser::parseErrorBody(body)` first; when it reports an error,
     `fail(parsed.error, parsed.errorMessage)`, otherwise
     `fail(DuckDuckGoParser::errorForHttpStatus(status), QStringLiteral("HTTP %1").arg(status))`;
   - any other `QNetworkReply::NetworkError` fails with `Network` and
     `m_network->describeNetworkError(request, false)`;
   - `2xx`: `DuckDuckGoParser::parseResponse(body)`; on failure fail the whole reply; on success
     append `translatedText` to `m_translations`, keep the first non-empty `detectedSourceLang`, mark
     the part sent, and either `scheduleSend()` for the next chunk or `finishTranslation()`;
   - `scheduleSend()` uses `QTimer::singleShot(rateLimitMs(), this, …)` and re-checks
     `isFinished()`, so `setRateLimitMs(0)` makes the tests deterministic;
   - `finishTranslation()` joins `leading + translation + trailing` for every sent part and the whole
     `leading` for an unsent whitespace-only part, sets `serviceId = "duckduckgo"` and the first
     detected language, logs `Translated <n> part(s), <source> -> <target> in <elapsed> ms` and calls
     `finish(result)`; a failed chunk never leaves a partial translation behind.

8. **Keep the logs clean.** `fail()` builds a `TranslationResult` with `serviceId = "duckduckgo"` and
   logs only the error kind through the copied `errorKind()` helper (`Translation failed: <kind>`),
   never `errorMessage`, as `ReversoService` does (`src/services/reversoservice.cpp:343-352`). The only
   other lines are the fixed `HTTP <status> in <elapsed> ms`, `Translating …`, `Translated …` and
   `Cancelled`. No line contains the text, the chunk body, the token, a cookie, the endpoint or any
   part of a URL. `errorMessage` carries only the fixed literals named in steps 2, 5 and 7 and
   `NetworkManager::describeNetworkError(request, false)`; none of them can repeat the request.

9. **Register the service and the fallback id.** In `src/app/appcontroller.h` add
   `class DuckDuckGoService;` after `class DeepLService;` (line 30) and
   `DuckDuckGoService *m_duckduckgo = nullptr;` after `m_reverso` (line 128). In
   `src/app/appcontroller.cpp`: `#include "services/duckduckgoservice.h"` after `deeplservice.h`;
   `m_duckduckgo = new DuckDuckGoService(m_network);` **last**, after `m_reverso` (line 259) in
   `createTranslation()`, `m_duckduckgo->applySettings(settings);` after the `m_reverso` line
   (line 267) and `m_translations->registerService(m_duckduckgo);` after the Reverso registration
   (line 275), so the pickers keep their order and every existing service keeps its place. In
   `applySettings(oldSettings, newSettings)` add
   `if (m_duckduckgo) m_duckduckgo->applySettings(newSettings);` after the `m_reverso` line (line 657).
   In `src/core/settings.cpp` append `QStringLiteral("duckduckgo")` after `"reverso"` in
   `fallbackServiceIds()` (lines 308-317) and update the exact-list assertion at
   `tests/tst_settings.cpp:122-125`. Nothing else in `settings.cpp` needs a change: there is no key,
   no normalization and no INI section, and `settings.translation.service` is not validated against a
   list.

10. **Register the sources and the test targets.** In the root `CMakeLists.txt` add
    `src/services/duckduckgoparser.{cpp,h}` and `src/services/duckduckgoservice.{cpp,h}` to
    `qtranslate_core` after `src/services/deeplwebservice.*` and before `googleparser.*`, so the block
    stays alphabetical. In `tests/CMakeLists.txt` add `qtranslate_add_test(tst_duckduckgoparser)` and
    `qtranslate_add_test(tst_duckduckgoservice)` after `tst_deeplwebservice` and before
    `tst_googleparser`. Both targets are platform-neutral: no `Qt::Widgets`, no `Qt::DBus`, no
    `xvfb` property, because they talk to a local `QTcpServer` only.

11. **Write `tests/tst_duckduckgoparser.cpp`** (`QTEST_GUILESS_MAIN`), modelled on
    `tests/tst_bingwebparser.cpp`:

    - `tokenPage()`: the quoted token is returned; the earlier unquoted `vqd=` mention in the same page
      is ignored;
    - `tokenPageFailures()` data-driven over `token_page_missing.html`, `token_page_empty.html`,
      `token_page_truncated.html`, `token_page_control.html` and a JSON body passed by mistake:
      `false` and the fixed `the token page has no usable vqd token`;
    - `response()`: `Hallo` and `detectedSourceLang == "en"`; `responseWithoutDetection()`:
      `detected_language: null` (and the missing field) leaves `detectedSourceLang` empty;
    - `detectedLanguage()` data-driven: `en`/`fr`/`de`/`ru` → the same internal code, `xx` → empty;
    - `badResponses()` data-driven over `response_missing_translated`, `response_empty_translated`,
      `response_whitespace_translated`, `response_non_string_translated`, `response_array`,
      `response_string`, `response_malformed`, `response_empty` → `BadResponse` and
      `no translated text in the response`;
    - `errorBodies()` data-driven: `error_invalid_text` → `TooLong`; `error_invalid_to` →
      `UnsupportedLanguage`; `error_forbidden`/`error_unknown` → `ServiceUnavailable`;
      `html_challenge` → `ServiceUnavailable`; `rate_limit.txt` → `RateLimited`;
      `blocked.txt` → `ServiceUnavailable`;
    - `unrecognisedBodiesAreNotErrors()`: `plain.txt`, an empty body and `{}` return
      `TranslationError::None`;
    - `httpStatus()` data-driven: `400`/`404` → `BadResponse`, `403`/`429` → `RateLimited`, `413` →
      `TooLong`, `500`/`503` → `Network`;
    - `verifyFixedMessage()` (no `SECRET`, `<`, `{`, `}`, `://`) on every message the parser returns.

12. **Write `tests/tst_duckduckgoservice.cpp`** on a local `QTcpServer`, copying
    `FakeHttpServer` from `tests/tst_bingwebservice.cpp:31-131` (method, URL, headers, body, arrival
    time, `respond`, `Location`, `setCookies`); only the cookie-isolation case answers with `Set-Cookie`,
    because the route itself uses no cookie. `init()` builds the server, `NetworkManager`,
    `DuckDuckGoService`, calls `setEndpoint(QUrl("http://127.0.0.1:<port>/"))` and
    `setRateLimitMs(0)`; `cleanup()` resets all three. Helpers: `tokenPage(token)` (the synthetic page),
    `translationResponse(text, detected)` (the JSON object of the note), `longText()` (Cyrillic,
    paragraphs, more than 3000 UTF-16 units), `verifyFixedMessage()` and `LogCapture` from
    `tests/tst_bingwebservice.cpp:243-268` with the rule `qtranslate.service.duckduckgo.debug=true`.
    Cover:

    - `properties()`: id, display name, `isConfigured()`, and the two language lists equal to
      `Languages::supported("duckduckgo", …)`; the probe-confirmed codes `de`, `fr`, `es`, `it`,
      `pt` (as `pt-BR`), `zh-Hans`, `ja`, `ko`, `pl`, `uk`, `tr`, `ar`, `hi`, `en` and `ru` are
      present, and `auto` is not in either list;
    - `defaultsAndEndpoints()`: a *fresh* instance reports `rateLimitMs() == 1000` and
      `endpoint() == defaultEndpoint() == https://duckduckgo.com/`, and `applySettings(AppSettings{})`
      changes neither; an empty and an invalid URL fall back to the default; a URL without a trailing
      slash still yields `…/translation.js`; `setEndpoint()` drops the cached token (a second endpoint
      forces a new GET);
    - `tokenThenPost()`: exactly two requests — `GET /` with `q=translate`, `Accept: text/html` and
      `UserAgent == DuckDuckGoService::browserUserAgent()`, then `POST /translation.js` with `vqd`,
      `query=translate`, `to=de` and **no** `from`; body `Hello` byte for byte, `Content-Type:
      application/json`, `Accept: application/json`, the same browser UA; the result carries `Hallo`,
      `serviceId == "duckduckgo"` and `detectedSourceLang == "en"`. A plain
      `m_network->nam()->get(m_network->makeRequest(url))` to the same server must still send
      `AppInfo::userAgent()`, so the override is scoped to this service;
    - `explicitSourceSendsFrom()`: `from=ru` is present and a `detected_language: null` fixture leaves
      `detectedSourceLang` empty;
    - `reusesCachedToken()`: two reply rounds → one GET and two POSTs, the second POST carrying the
      same `vqd`;
    - `refreshesTokenOn403()`: a one-chunk text; the handler answers the token page with `T1` and `T2`
      alternately and rejects the POST that carries `T1` with `403` and an empty body; asserts four
      requests in the order GET(T1), POST(T1), GET(T2), POST(T2), that both POSTs carry the same body,
      that the result is `Hallo`, and that no further GET follows;
    - `refreshesOnLaterChunkKeepsIndex()`: three chunks; the first POST succeeds, the POST of the
      second chunk is rejected once, then succeeds with `T2`, then the third chunk follows; the bodies
      arrive in the order `c1, c2, c2, c3` and the joined result keeps the order; no chunk is skipped
      and none is duplicated in the result;
    - `second403IsRateLimited()`: every POST answers `403` → exactly two GETs and two POSTs,
      `RateLimited`, `the endpoint rejected the token`, no third round after `QTest::qWait(50)`;
    - `failedRefreshSurfacesItsError()`: the second GET answers `503` → `Network`;
    - `httpErrors()` data-driven: `400 invalid_to` → `UnsupportedLanguage` with
      `the endpoint rejected the language`; `400 invalid_text` → `TooLong`; `429` → `RateLimited`;
      `403` with `{"error":"Forbidden"}` → `RateLimited` after the refresh; `500` → `Network`; `404`
      → `BadResponse`; `200` with an HTML body → `ServiceUnavailable`; `200` with
      `error_unknown.json` → `ServiceUnavailable`; `200` malformed/empty → `BadResponse`; all with
      `verifyFixedMessage()`;
    - `unsupportedLanguage()`: `ceb` as the target → `UnsupportedLanguage`,
      `language "ceb" is not supported`; `haw` as the source → the same error naming `haw`; zero
      requests after `QTest::qWait(50)`;
    - `emptyText()` data-driven (empty and whitespace-only) → `EmptyText`, zero requests;
    - `autoIsSupported()`: `auto` → no `from`, `detected_language` mapped into `detectedSourceLang`;
    - `edgeWhitespace()`: `" \tHello\r\n "` reaches the server as `Hello` and the result is
      `" \tX\r\n "`; an all-whitespace middle part is never sent and is restored;
    - `chunks()`: `TextChunker::split(text, 900)` defines the parts; every request body is at most
      900 UTF-16 units, the request count equals the non-whitespace parts, the order matches, and the
      joined result equals the input with each chunk answered separately;
    - `pacesChunks()`: `setRateLimitMs(100)`, at least three chunks, every consecutive pair at least
      90 ms apart (lower bound only), including the GET → POST pair;
    - `serializesRequests()`: two replies with `setRateLimitMs(200)`; one GET, two POSTs, the second
      POST at least 180 ms after the first;
    - `abortActiveTokenRequest()` and `abortActivePostRequest()`: `Cancelled`, one request, recorded
      server-side, no retry;
    - `failedMiddleChunkFailsTheWholeReply()`: the second POST fails; the whole reply fails with that
      error kind, `translatedText` is empty, exactly two POSTs and no third;
    - `timeout()`: `setTimeoutMs(300)` and a handler that never answers → `Timeout` and
      `DuckDuckGo did not respond within 0.3 s`;
    - `closedServerIsNetwork()`: `m_server->close()` → `Network`, `Network error`;
    - `crossOriginRedirectOnGetIsNotFollowed()` and `crossOriginRedirectOnPostIsNotFollowed()`: a
      `302` to a second `FakeHttpServer` — the second server receives nothing, the reply is not ok,
      the translation is empty and the message is fixed;
    - `cookieIsolation()`: a `Set-Cookie` on the token page is neither stored in the shared
      `QNetworkCookieJar` nor sent on the POST;
    - `logsAndMessagesAreClean()`: a success and a failure with `TOPSECRET` in the text and in a
      provider error body; neither the captured log nor `errorMessage` contains `TOPSECRET`, the
      token, `<`, `{`, `}`, `://` or `127.0.0.1`;
    - `fallbackServiceWorks()`: a `TranslationManager` with a fake primary service that finishes with
      `Network` and `DuckDuckGoService` as `setFallbackService("duckduckgo")`; the fallback translates
      against the local fixture and the result carries `serviceId == "duckduckgo"`. Copy the small
      `FakeService` shape from `tests/tst_translationmanager.cpp:24-65`.

    Finish with `QTEST_GUILESS_MAIN` and `#include "tst_duckduckgoservice.moc"`. CTest never contacts
    `duckduckgo.com`.

13. **Extend `tests/tst_languages.cpp` and `tests/tst_settings.cpp`.** Add
    `const QString duckduckgo = QStringLiteral("duckduckgo");` to the service constants. In
    `toServiceCode_data()`, mirror the Bing rows for the inherited shapes (`pt-BR` → `pt`,
    `pt-PT` → `pt-PT`, `zh-Hans`/`zh-Hant` identity, `nb`, `sr` → `sr-Cyrl`, `he`, `fil`, `jv`,
    `hmn`), add the probe-confirmed identity rows the Bing table does not carry (`es`, `ja`, `ko`,
    `pl`, `uk`, `tr`, `ar`, `hi`, `it`, `fr`, `de`, `ru`, `en`), and the empty rows (`ceb`, `kk`,
    `pa`, `haw`, `xx`, `auto`). In `fromServiceCode_data()`: the same identity codes, `pt` →
    `pt-BR`, `sr-Cyrl` → `sr`, `FR` (mixed case) → `fr`, `xx` → empty, and `sr-Latn` → empty (that
    synonym is Bing's alone until DuckDuckGo is observed reporting it). In `roundTrip()`: add
    `duckduckgo` to the service list; the existing `pt-PT`/`zh-Hant` source exclusions apply as they
    do to Bing. In `supportedLists()`: `supported(duckduckgo, Source)` equals the `Target` list,
    contains the fifteen probe-confirmed codes, excludes `auto`, `ceb`, `kk`, `pa` and `haw`, and
    differs from `Languages::supported("bing-web", …)` by exactly `es` — assert that as a set
    comparison, so an accidental divergence between the two columns fails here. `tst_settings` only
    gets the extra `duckduckgo` element in the `fallbackServiceIds()` assertion (lines 122-125); do
    not touch the Mozhi engine rows at lines 213-214.

14. **Build, count and verify.** Run the container first, so the host stays responsive:
    `docker run --rm -v "$PWD:/src:ro" -w /src qtranslate-trixie bash -c 'cmake -S /src -B /tmp/build
    -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/build --parallel && xvfb-run -a ctest
    --test-dir /tmp/build --output-on-failure'` (the command from `packaging/linux/trixie.Dockerfile`).
    Then the host Debug and Release:
    `cmake -S . -B build/linux-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug` and
    `build/linux-release`/`Release`, `cmake --build` each, `xvfb-run -a ctest --test-dir
    build/linux-release --output-on-failure`. Zero warnings with `-Wall -Wextra` in both
    configurations; `ctest -N` shows 48 Linux tests (46 + the two new targets); the new targets are
    green and the rest of the suite does not regress. `git diff --stat` should show only
    `src/services/duckduckgo{parser,service}.{h,cpp}`, `src/core/languages.cpp`,
    `src/core/settings.cpp`, `src/app/appcontroller.{h,cpp}`, `CMakeLists.txt`,
    `tests/CMakeLists.txt`, `tests/tst_duckduckgo{parser,service}.cpp`, `tests/tst_languages.cpp`,
    `tests/tst_settings.cpp` and `tests/fixtures/duckduckgo/*`. No `.ts`, no `doc/`, no `AGENTS.md`,
    no README, no `doc/handoff.md`. Windows Debug/Release and its 40 tests are recorded as not
    performed (no MSVC on this machine) and reviewed by diff.

## Done when

- `DuckDuckGoService` exists under `src/services/`, is listed in `qtranslate_core`, derives from
  `QueuedWebService`, returns the id `duckduckgo`, the display name `DuckDuckGo`, `isConfigured() ==
  true` and the language lists the inherited Bing catalogue plus `es` produces, without `auto`; the
  parser lives in `src/services/duckduckgoparser.*` and touches no network.
- `duckDuckGoCodeOf()` reuses `kBingWebCodes`/`kBingWebOverrides` through `bingWebCodeOf()` and adds
  only `es`, which the follow-up probe confirmed and the Bing table lacks; the header comment names
  the source (Mozhi "1-1 with Bing Translate", `libmozhi`'s `langlist-duckduckgo.go`), lists the
  fifteen probe-confirmed codes and marks every other inherited code as inherited and unverified; no
  second 80-entry array and no change to the Bing table.
- The service is registered in `AppController` after `m_reverso` and receives `applySettings()`;
  `duckduckgo` is in `fallbackServiceIds()` after `reverso` and in the exact-list assertion of
  `tst_settings`; there is no `[duckduckgo]` INI key, no secret and no per-service URL.
- `translate()` rejects every unmapped code with `UnsupportedLanguage` and
  `language "<code>" is not supported` before any request, accepts `auto` by omitting `from`, splits
  at 900 UTF-16 code units, restores edge whitespace, never sends an all-whitespace part, paces the
  chunks sequentially inside one queue slot and never returns a partial translation after a failed
  chunk.
- The wire behaviour matches the research: `GET <endpoint>?q=translate` with a browser User-Agent
  yields the `vqd`, the token is cached without a lifetime, the POST carries the raw chunk as the body
  with `Content-Type: application/json`, the response `translated`/`detected_language` fills the
  result, a `403` refreshes the token exactly once per reply and resends the same chunk, a second
  `403` is `RateLimited`, and both requests set `SameOriginRedirectPolicy` and the manual cookie
  attributes.
- `tst_duckduckgoparser` and `tst_duckduckgoservice` cover the token page, the success and detected
  shapes, `invalid_text`, `invalid_to`, `Forbidden`, an unknown error object, an HTML page, plain
  block bodies, malformed/empty bodies, the `httpStatus` mapping, the chunk split and order, the
  pacing asserted at 100 ms with `setRateLimitMs(0)` everywhere else, `auto` (and the absence of
  `from`), the unsupported-language and empty-text rejections with zero requests, abort, timeout,
  the closed server, both cross-origin redirects, cookie isolation, the fallback translation and the
  message/log hygiene; `tst_languages` covers the inherited table (including the Bing shapes, `es`
  as the probe-only addition, the mixed case and the exclusions) and pins the difference from
  `bing-web` to exactly `es`.
- Debug and Release build on Linux without warnings and the full suite passes with 48 tests; Windows
  is reviewed by diff and its 40 tests are recorded as not performed.
- No UI, `.ts`, `doc/`, `AGENTS.md`, README or `doc/handoff.md` file is changed; commit and push stay
  with the lead.

## Risks

- The body limit is a range, not a number: 986 characters were accepted and 2006 rejected. 900 UTF-16
  code units is deliberately below the lower bound; do not raise it to 986, because the endpoint may
  count characters or bytes and the first failure would be `invalid_text` on a whole text. The 1000 ms
  pause is likewise unmeasured.
- The body is the raw text, not JSON, although the content type says `application/json`. A programmer
  who "corrects" this to `{"text":…}` or `QJsonDocument` quoting breaks the route silently; the test
  asserts the byte-identical body.
- `403` is the only signal for a rejected token, and it is also the signal for a block. Exactly one
  refresh per reply, then `RateLimited`: no retry loop and no second refresh, even for a later chunk.
  Do not add a 429-style backoff.
- The language catalogue is **inherited from Bing by source, not measured on DuckDuckGo**. Fifteen
  codes are probe-confirmed (`de`, `fr`, `es`, `it`, `pt`, `zh-Hans`, `ja`, `ko`, `pl`, `uk`, `tr`,
  `ar`, `hi`, `en`, `ru`); everything else — including `zh-Hant`, `pt-PT`, `sr-Cyrl`, `fil`, `hmn`
  and `jv` — is inherited and unverified. A code the route rejects answers `invalid_to` and becomes
  `UnsupportedLanguage`, which is safe; the risk is the opposite: a code the route accepts but maps to
  a different variant (`zh-Hant` producing Simplified, `pt-PT` producing Brazilian) would return a
  silently wrong translation. That is the cost the user accepted when choosing the inherited catalogue,
  and it is recorded here so `02-duckduckgo-ui` can phrase the hint honestly; do not "fix" it by
  dropping inherited codes, and do not add a code that neither the Bing table nor the probe names.
- `es` is the one probe-confirmed code the Bing table does not declare. It lives in
  `kDuckDuckGoExtraCodes` so the Bing column keeps its own evidence; whether `bing-web` also misses
  `es` is a finding for the lead (the Bing catalogue at `src/core/languages.cpp:239-247` has no `es`),
  not a change this task makes.
- The token cache has no time limit. The research only proved a token older than 26 minutes; the
  refresh-on-403 path is the correctness guarantee, so a long-running instance pays one rejected POST
  when a token dies. Do not add an invented expiry without a measurement, and do not remove the
  refresh.
- The route is unofficial: it can be renamed, challenged or closed. Never contact it from a test, and
  never make a failure look like a bug in the app. A live smoke is not part of this task; if one is
  run, record it as passed or as not performed with the reason.
- The token sits in the POST URL query, so it is the most leak-prone value here. The log lines carry
  only fixed literals, the status and durations, and `fail()` logs the error kind; keep it that way and
  keep the hygiene case green.
- The service test file is large (the sibling files are 900-1500 lines) and is the context risk for
  the programmer. Keep the shared helpers small and the cases independent; if the file does not fit,
  report it instead of dropping the refresh, redirect or hygiene cases.

## Outcome

Done. `src/services/duckduckgoparser.{h,cpp}` and `src/services/duckduckgoservice.{h,cpp}` are new
and listed in `qtranslate_core`. The service is a `QueuedWebService` with the id `duckduckgo`, the
untranslated display name `DuckDuckGo`, `isConfigured() == true` and an empty `applySettings()`. It
fetches `GET <endpoint>?q=translate` with the request-scoped browser User-Agent, reads the first
`vqd="..."` out of the page bytes, caches the token without a lifetime and posts each 900-UTF-16-unit
chunk as the raw body to `translation.js?vqd=…&query=translate&to=…[&from=…]`; `auto` omits `from`.
One rejected token is refreshed per reply and the same chunk is resent; a second `403` is
`RateLimited`. Both stages set `SameOriginRedirectPolicy` and manual cookie attributes, the chunk
pause is 1000 ms, and a failed chunk fails the whole reply. Errors are classified body-first with the
fixed literals of steps 2, 5 and 7; the logs carry only fixed literals, the status and durations.
`duckDuckGoCodeOf()` in `src/core/languages.cpp` reuses `kBingWebCodes`/`kBingWebOverrides` through
`bingWebCodeOf()` and adds `es` from `kDuckDuckGoExtraCodes`; the Bing table, `kSynonyms` and the
Mozhi column are untouched. `duckduckgo` is registered in `AppController` after Reverso, receives
`applySettings()` and is appended to `fallbackServiceIds()`; there is no INI key and no `.ts`, UI or
`doc/` change in this task.

Tests: `tests/tst_duckduckgoparser.cpp` (43 cases on 24 synthetic fixtures) and
`tests/tst_duckduckgoservice.cpp` (39 cases on a local `QTcpServer`, no request ever leaves the
machine), plus the `duckduckgo` column in `tst_languages` and the fallback list in `tst_settings`.
A clean host Debug and a clean host Release build are warning-free and pass 48/48 Linux tests
(46 existing + `tst_duckduckgoparser` + `tst_duckduckgoservice`); a clean Release build in the
`qtranslate-trixie` image (Qt 6.8.2) is also warning-free and 48/48. Windows Debug/Release and its
40 tests are recorded as not performed (no MSVC on this machine) and were reviewed by diff only. A
live smoke against `duckduckgo.com` was not run: the tests must not contact the real frontend.

What turned out on the way:

- `es` is absent from `kBingWebCodes` (`src/core/languages.cpp:239-247`), which is why the extra array
  exists. The Bing provider therefore does not offer Spanish as a target even though Bing translates
  it; whether to widen `bing-web` is a separate decision with no probe behind it, so this task leaves
  the Bing table as it is.
- The plan's remark that `QUrlQuery` percent-encodes `+` and `/` in the token is not accurate on
  Qt 6.10: those characters are legal in a query and stay literal, while `&` and `=` are escaped.
  The property that matters — a token cannot break or extend the query — holds; the code comment
  states it that way.
- Step 6 and the `## Task` section disagree slightly about a `403` on the token page: the step says
  the body is classified first and then calls a page `403` a block, while the task section classifies
  an HTML page as `ServiceUnavailable`. The implementation follows the task section (body first); an
  empty `403` body still becomes `RateLimited` through `errorForHttpStatus()`. The plan wording could
  be clarified, but the behaviour is the safe one.
- The reviewer noted that no test pins "`applySettings()` does not clear the token cache"; it was
  rejected as out of the plan's acceptance: `applySettings()` is an empty function with a comment,
  and the cache-expiry decision is already covered by `setEndpoint()` in `defaultsAndEndpoints()`.
- Writing the parser test hit a moc limitation: a raw string whose content ends with a quote
  (`R"({"detected_language":")"`) makes moc drop the class and the link fails with an undefined
  vtable. The case was rewritten as an escaped literal; no other raw string in the tests is affected.

Deferred to the lead: the documentation pass (`doc/free-endpoints.md`, `doc/architecture.md`, both
READMEs, `doc/handoff.md` and the test/message counts in `AGENTS.md`); the settings group, the hint
and the `.ts` strings are `02-duckduckgo-ui`.
