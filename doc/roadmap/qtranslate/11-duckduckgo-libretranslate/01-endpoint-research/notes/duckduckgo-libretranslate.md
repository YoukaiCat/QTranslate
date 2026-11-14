# DuckDuckGo and LibreTranslate: endpoint research

Date: 2026-11-15. Probes were run on 2026-11-14 22:39-23:12 UTC (2026-11-15 in local
time) from one Linux host. Stage: `11-duckduckgo-libretranslate`, task
`01-endpoint-research` (see `../plan.md`). Status: **research only**; no source, test, CMake,
`.ts` or `doc/` file was changed and this note is the only artifact. The lead copies the accepted
parts into `doc/free-endpoints.md`, `doc/architecture.md` and both READMEs.

## Verdicts

| Service | Verdict | One-line reason | Map node |
| --- | --- | --- | --- |
| DuckDuckGo | **not viable** | The only working route is the search frontend's `translation.js`, which answers `403` unless it is given a `vqd` token scraped from the HTML of `https://duckduckgo.com/?q=translate` and a browser User-Agent; the `mozhi` provider already reaches the same engine. | `02-duckduckgo` and both children are dropped |
| LibreTranslate | **viable** (self-hosted) | A LibreTranslate server the user runs answers a key-less `POST /translate` with no cookies and no token, returns `translatedText` plus `detectedLanguage`, and carries no terms of its own beyond the user's server; the licence question is settled by adding no AGPL code. | `03-libretranslate` is worked |

A hosted LibreTranslate instance is **not** part of the viable path: `libretranslate.com` refuses
`/translate` without a key (`400`, `{"error":"Visit https://portal.libretranslate.com to get an API
key"}`), and neither key-less mirror in the official list answered a request today (§1.5).

## Constraints the answer must satisfy

From the stage rule, `doc/free-endpoints.md` §5 and §8-§10 and the existing services:

- No key that the user does not have. A key-protected hosted instance is not a key-less provider.
- No browser requirement, no JavaScript execution, no cookie replay, and no token scraped from an
  HTML page. A LibreTranslate server the user runs counts even though it is not a public endpoint.
- The terms must not forbid the use, and the route must not be a personal scraping trick.
- The quality must not be obviously unusable (wrong language, gibberish, an empty stub).
- House conventions for the future service: its own `TranslationService` per endpoint on
  `QueuedWebService`, a per-request language mapper in `Languages::toServiceCode`/`fromServiceCode`
  (`src/core/languages.*`), a settings group in `src/core/settings.h` (`MozhiSettings` is the base-URL
  precedent), an id in `fallbackServiceIds()`, serialized replies with a named pause, a body-first
  error classification, `SameOriginRedirectPolicy`, and no text, body, cookie or full URL in a log
  line or an error message. An unofficial provider stays available by default only with a hint that
  says it is unofficial.

## 1. LibreTranslate

### 1.1 Protocol (observed on 1.9.6)

- `POST <base>/translate`, `Content-Type: application/json`, `Accept: application/json`.
- Body: `{"q":"<text>","source":"<code>|auto","target":"<code>"}`. `source` is **required**; omitting
  it is a `400 Invalid request: missing source parameter`, so auto-detection must be sent as
  `"source":"auto"` explicitly. `format` is optional and defaults to `text`; `"format":"html"`
  returned `{"translatedText":"<p>Привет.</p>"}` for `"<p>Hello</p>"` and is not needed by the
  translation window.
- Success: `200`, `{"translatedText":"Привет"}`.
- Auto-detection: `{"detectedLanguage":{"confidence":90.0,"language":"ru"},"translatedText":"Good
  morning"}`. The field is present **only** when `source` is `auto`; with an explicit `source` the
  response is the bare `translatedText`. `confidence` is a float, 0-100.
- Error shape: a flat `{"error":"<message>"}` with a `4xx`/`5xx` status. The message is a
  human-readable English sentence, not a stable code:
  - unsupported target: `400 {"error":"xx is not supported"}`; a real but unloaded pair
    (`en→ja` on a server with only `en` and `ru`) is the same `400 {"error":"ja is not supported"}`;
  - wrong body: `400 {"error":"Invalid request: missing q parameter"}`;
    `400 {"error":"Invalid request: missing source parameter"}`;
  - malformed JSON: `400 {"error":"The browser (or proxy) sent a request that this server could not
    understand."}`;
  - no `Content-Type: application/json`: the body is ignored and the request fails as a missing `q`;
  - `GET /translate`: `405`;
  - a key-protected hosted instance without a key: `400 {"error":"Visit
    https://portal.libretranslate.com to get an API key"}` on `libretranslate.com`, and
    `403 {"error":"Invalid API key"}` with a made-up key;
  - rate limit (`429`) is rendered by the application as `{"error":"Slowdown: <description>"}`
    (`libretranslate/app.py`, `@bp.errorhandler(429)` at the time of writing).
- Batch: `"q":["Hello","Good morning"]` returns `{"translatedText":["Привет","Доброе утро"]}`.
  The service does not need batches: it chunks and sends one string per request.
- `GET <base>/languages` → `[{"code":"en","name":"English","targets":["en","ru"]}, …]`; `targets` is
  the list of target codes that server can produce from that source.
- `GET <base>/health` → `200 {"status":"ok"}`.
- `POST <base>/detect` → `200 [{"confidence":90.0,"language":"ru"}]`. Not needed: `/translate` with
  `source:"auto"` already reports the detected language.
- The server has a `--url-prefix` option, so a base URL with a path prefix is plausible; it was
  **not probed** (a prefix is a named assumption, not an observation).
- The User-Agent does not matter: `qtranslate/0.13.0`, a browser UA and an empty UA all answered
  `200`. No cookie is read or written; no `Referer` is needed.

### 1.2 Auth

- Self-hosted default: **none**. The stock image has `--api-keys` off, and every probe above ran
  without a key. A server the user runs may enable `--api-keys`; then `403 {"error":"Invalid API
  key"}` appears and the service must map it to `Auth` rather than to a language error.
- Hosted `libretranslate.com`: a key is mandatory (`400` with the portal link). This is outside the
  key-less rule and outside this stage. **No `api_key` field is needed for the viable path.** If the
  user later wants the hosted instance, a stored secret, secret storage and a key-protection path are
  a separate user decision; they are not planned here.

### 1.3 Language codes

The codes are the ISO-style codes that `/languages` of that server reports. LibreTranslate maps its
Argos model codes onto them (`zh`→`zh-Hans`, `zt`→`zh-Hant`, `pb`→`pt-BR`; `libretranslate/language.py`,
`aliases`), so the API speaks `zh-Hans`, `zh-Hant` and `pt-BR`, not the model names.

Observed full list (`GET https://libretranslate.com/languages`, 51 codes, full cross-product targets):

```
en sq ar az eu bn bg ca zh-Hans zh-Hant cs da nl eo et fi fr gl de el he hi hu id ga it ja ko
ky lv lt ms nb fa pl pt pt-BR ro ru sr sk sl es sw sv tl th tr uk ur vi
```

Mapping to the internal catalogue (`src/core/languages.cpp`, 110 codes):

- 49 identity codes: `en sq ar az eu bn bg ca zh-Hans zh-Hant cs da nl eo et fi fr gl de el he hi hu
  id ga it ja ko ky lv lt ms nb fa pl pt-BR ro ru sr sk sl es sw sv th tr uk ur vi`.
- Two aliases: internal `fil` → `tl` (LibreTranslate names the model Tagalog), internal `pt-PT` →
  `pt` (the server has no `pt-PT` model; this is a lossy but honest mapping, and `closest()` would
  otherwise drop European Portuguese entirely).
- 59 internal codes that no LibreTranslate code covers and that must fail with
  `UnsupportedLanguage` **before** a request is sent: `af am hy be bs ceb ny co hr fy ka gu ht ha haw
  hmn is ig jv kn kk km rw ku lo la lb mk mg ml mt mi mr mn my ne or ps pa sm gd st sn sd si so su tg
  ta tt te tk ug uz cy xh yi yo zu`.
- `auto` is supported (`"source":"auto"`) and reports `detectedLanguage.language` in the same code
  space, so `fromServiceCode("libretranslate", …, Direction::Source)` needs the reverse aliases
  `tl`→`fil`, `pt`→`pt-PT` and is identity otherwise.
- A server's `/languages` is the authority for that server. The service should keep a static table
  (the 51 codes above) for `closest()`/`supported()` and let a `400 … is not supported` from the
  server become `UnsupportedLanguage`; it must not fetch `/languages` per request.

### 1.4 Limits

- Request size: the stock server has `--char-limit -1` (no limit; `libretranslate/default_values.py`)
  and accepted a 6460-character body with `200`. A server may set a limit; an over-limit request is
  `400 {"error":"Invalid request: request (N) exceeds text limit (M)"}` → `TooLong`.
- Rate: the stock server has `--req-limit -1` and `--hourly-req-limit -1` (none) and accepted ~30
  requests in a few minutes without throttling. A server may set them; then `429
  {"error":"Slowdown: …"}` → `RateLimited`.
- Batch limit `--batch-limit -1`; not used.
- Consequence for the client: chunk at **5000 UTF-16 code units** (`TextChunker::split`, the same
  named conservative assumption as Mozhi, Yandex web, Bing web and Reverso) and pace requests with a
  **500 ms** pause. Both are named conservative assumptions, not measured limits: the observed server
  accepted more per request and did not throttle. They keep one bad chunk from failing a whole text
  and keep the app polite to a user's small home server.

### 1.5 Public instances (2026-11-15)

The official list `https://docs.libretranslate.com/community/mirrors/` names exactly three HTTP
mirrors: `libretranslate.com` (API key required), `translate.cutie.dating` and
`translate.fedilab.app` (both marked "no key").

| Host | Date | Result |
| --- | --- | --- |
| `libretranslate.com` | 2026-11-14 23:08 UTC | `/health` 200, `/languages` 200 (51 codes) without a key, but `POST /translate` without a key `400 {"error":"Visit https://portal.libretranslate.com to get an API key"}`; with a made-up key `403 {"error":"Invalid API key"}`. |
| `translate.fedilab.app` | 2026-11-14 22:40-23:08 UTC | `403` with a fixed 93-byte HTML body (`<h1>403 Forbidden</h1> Request forbidden by administrative rules.`) for `/`, `/health`, `/languages` and `POST /translate`, with curl's UA, with a browser UA, directly and through the host's HTTP proxy. Nothing answered. |
| `translate.cutie.dating` | 2026-11-14 22:43-23:08 UTC | **Not performed**: the TCP connection to `109.230.238.120:443` timed out directly and through the proxy, so no HTTP response was obtained. |

Conclusion: **no key-less public instance answered a request today.** The refusal at `fedilab` is
served by the host itself (a fixed policy page, not a DNS or TLS failure), so it is not a transient
network error; whether any key-less mirror answers automated clients from another network is
**unknown** and was not verified.

### 1.6 Self-hosted server (observed)

- Image: `libretranslate/libretranslate:latest`, digest
  `sha256:6f03f063a10d120cdb5986e8995d4524729d14125269bc6c49f791e49789aec8`, reported version
  **v1.9.6**. Docker was available on the host.
- The documented first start (`docker run -p 5055:5000 -e LT_LOAD_ONLY=en,ru …`) **did not finish**
  on this host: the entrypoint stayed in its `Booting...` phase for over twenty minutes with an idle
  CPU, and the packages directory remained empty. The cause observed is this host's egress,
  not the image: a direct (proxy-less) large download from `argos-net.com` stalls here from the host
  and from the container alike, while the same file downloads at ~11 MB/s through the host's HTTP
  proxy. Separately, `argos-net.com` answers `403` to a request without a `User-Agent` and `200` with
  one. The models were therefore downloaded through the proxy on the host, copied in with
  `docker cp`, installed with `argostranslate.package.install_from_path()`, and the server was started
  manually with `gunicorn -c scripts/gunicorn_conf.py --bind 0.0.0.0:5000 'wsgi:app()'` and
  `LT_LOAD_ONLY=en,ru`. It answered `/health` `200 {"status":"ok"}` about 12 seconds after the start.
- Model sizes seen: `translate-en_ru-1_9.argosmodel` 195,746,693 bytes,
  `translate-ru_en-1_9.argosmodel` 156,239,112 bytes; installing both took a few seconds.
- The first start downloads models into `/home/libretranslate/.local/share/argos-translate/packages`;
  the index comes from `raw.githubusercontent.com/argosopentech/argospm-index` and the packages from
  `argos-net.com/v1/…`. A user who self-hosts must be able to reach both. This is an installation
  note for the hint and the documentation, not a defect of the protocol.

### 1.7 Licence

- LibreTranslate is **AGPL-3.0** (`LICENSE` in the image: `GNU AFFERO GENERAL PUBLIC LICENSE,
  Version 3`).
- QTranslate stays **GPL-3.0-or-later** and adds **no LibreTranslate code**: the application is an
  HTTP client of a server the user runs separately, it ships no server binary, no container and no
  Argos model. That reading is safe and is already recorded in `doc/local-translation.md` §2 (the
  LibreTranslate row: "A Python server on top of Argos … AGPL-3.0") and §7 ("LibreTranslate — only as
  an external self-hosted server, considering AGPL"). Nothing in this research changes it.
- No public instance's terms are inherited either: the user's own server has no third-party terms.

### 1.8 Terms and stability

- The self-hosted path has **no terms of a third party**: the user runs the server and owns the data
  path. Availability is the user's server and the user's network.
- A public mirror would be a volunteer server with no SLA and no published quota (§1.5 shows one
  refusing and one unreachable), which is why it is not a default and not a sole channel.

### 1.9 Quality (impression, not a benchmark)

Self-hosted 1.9.6, Argos `en_ru` 1.9 / `ru_en` 1.9, neutral sentences:

```
en→ru  The cat sleeps on the windowsill.              → Кошка спит на подоконнике.
en→ru  Good morning, the meeting starts at nine.      → Доброе утро, встреча начинается в девять.
en→ru  Please send me the report by Friday.           → Пожалуйста, отправьте мне отчет к пятнице.
ru→en  Кошка спит на подоконнике.                     → The cat sleeps on the windowsill.
ru→en  Доброе утро, встреча начинается в девять.      → Good morning, the meeting starts at nine.
ru→en  Пожалуйста, пришлите отчёт до пятницы.         → Please send the report by Friday.
```

All six are correct; the only blemish is `отчёт` → `отчет` (ё lost). This is Argos-level quality,
below Google/DeepL on longer and idiomatic text, and the pair coverage is the server's models, not a
full matrix: a server that has only `en` and `ru` rejects `en→ja` with `400`. Not a reason to drop.

### 1.10 What to implement

- Service id `libretranslate`, display name `LibreTranslate`. `src/services/libretranslateservice.*`
  on `QueuedWebService` and `src/services/libretranslateparser.*` for the body, following
  `src/services/mozhiservice.*` (base URL + per-request mapping) and `src/services/reversoservice.*`
  (fixed endpoint, no key, no settings group of its own).
- Request: `POST <base>/translate` with `Content-Type: application/json`, `Accept: application/json`,
  compact JSON `{"q":<chunk>,"source":<code|auto>,"target":<code>}`. The qtranslate User-Agent is
  accepted; no cookie header, no `Referer`, `SameOriginRedirectPolicy` on the request. Do not send
  `Accept-Language`: the server localises its `error` strings by that header and the classifier
  matches on English text.
- Response: `translatedText` (required, non-empty string) and, when `source` was `auto`, the optional
  `detectedLanguage.language`; `confidence` is parsed and ignored. `detectedSourceLang` is filled
  through `Languages::fromServiceCode(…, Direction::Source)`.
- `auto`: sent as `"source":"auto"`. An explicit source is validated locally; an unmapped internal
  code finishes with `UnsupportedLanguage` before any request.
- Chunking and pacing: 5000 UTF-16 code units (`TextChunker::split`) and a 500 ms pause between
  chunks, sequential inside one queue slot, edge whitespace restored — named conservative
  assumptions (see §1.4).
- Error mapping, body first: `429` or a `Slowdown` body → `RateLimited`; `413` or a body with
  `exceeds text limit` → `TooLong`; `400` with `… is not supported` → `UnsupportedLanguage`; `403`
  with `Invalid API key` → `Auth`; `5xx` without a recognised body → `Network`; an HTML page or an
  unknown provider error → `ServiceUnavailable`; malformed JSON, a missing/empty `translatedText` or
  an unknown non-2xx status → `BadResponse`; timeout/abort → `Timeout`/`Cancelled`. Messages are
  fixed literals; the text, the body and the URL never appear in them or in the log.
- Settings: `struct LibreTranslateSettings { QString baseUrl; }` next to `MozhiSettings` in
  `src/core/settings.h`, the `[libretranslate] baseUrl` key with normalization in
  `src/core/settings.cpp`, `applySettings()` in the service and the cache reset in
  `AppController::applySettings()` when the base URL changes. Extract `isValidServiceBaseUrl()` and
  `normalizedServiceBaseUrl()` from the Mozhi helpers and keep `isValidMozhiInstance()` /
  `normalizedMozhiInstance()` as thin wrappers: absolute `http(s)`, a host, no credentials, no query,
  no fragment, the path prefix kept, the trailing slash trimmed (the same rules the Mozhi instance
  field already enforces, `src/core/settings.cpp:334-357`).
- **Default base URL: empty.** With an empty or invalid value `isConfigured()` returns `false` and
  `configurationError()` tells the user to enter the base URL of their LibreTranslate server. The
  evidence: no key-less public instance answered today, one mirror actively refuses, and
  `doc/free-endpoints.md` §10 forbids making a public LibreTranslate instance the only channel. A
  default that silently sends text to a volunteer server would also be the "hidden instance" the
  house rules exclude. The field is always editable and no request goes to a server the user did not
  enter.
- Registration: `AppController::createTranslation()`, `applySettings()`, the member; `fallbackServiceIds()`
  in `src/core/settings.cpp` gets `libretranslate` after `reverso`; the provider order in
  `src/ui/settingsdialog.cpp` appends `LibreTranslate` after `Reverso` so existing pickers do not
  shift; the new sources in the root `CMakeLists.txt`; tests `tst_libretranslateparser`,
  `tst_libretranslateservice`, a `tst_settings`, a `tst_settingsdialog` case and `tst_languages`.
- The settings hint: the base URL points at a server the user trusts; its availability, its terms and
  its installed models are the user's responsibility; quality is Argos-level; a long text is sent in
  several requests.

## 2. DuckDuckGo

### 2.1 The routes checked

| Route | Date | Result |
| --- | --- | --- |
| `GET https://duckduckgo.com/?q=translate+hello+to+german` (browser UA, `Accept: application/json`) | 2026-11-14 22:45 UTC | `200 text/html`, 28,476 bytes, `<title>translate at DuckDuckGo</title>`. The HTML contains no translation (`Hallo` absent): the answer is rendered by JavaScript. The same URL with `?q=translate` is 27,880 bytes and holds the `vqd` token. |
| `GET https://duckduckgo.com/?q=translate+hello+to+german&format=json` | 2026-11-14 22:45 UTC | `200 application/x-javascript` with the Instant Answer shape (`{"Abstract":"","Answer":"","AnswerType":"","RelatedTopics":[],"Results":[]}`). Not a translation route: empty for a translation query. |
| `POST https://duckduckgo.com/translation.js?vqd=<token>&query=translate&to=de`, body `Hello` | 2026-11-14 22:44 UTC | `200 application/json`, `{"detected_language":"en","translated":"Hallo"}`. This is the route the community clients use. |
| The same without `vqd` | 2026-11-14 22:44 UTC | `403`, empty body. |
| The same with the scraped token but curl's default User-Agent | 2026-11-14 22:45 UTC | `403 application/json`, `{"error":"Forbidden"}`: a browser User-Agent is part of the protocol. |
| `GET https://<mozhi>/api/translate?engine=duckduckgo&from=en&to=ru&text=Hello` | 2026-11-14 23:08 UTC | `200` on `mozhi.adminforge.de` and on `mozhi.pussthecat.org`, `{"translated-text":"Здравствуйте", …}`. |

### 2.2 The route community clients use

`codeberg.org/aryak/libmozhi`, commit `7324b5c528fe` (2026-10-29), `engines.go`:

- `ddgVqdUpdate()` (around line 334) issues `GET https://duckduckgo.com/?q=translate` with a Chrome
  User-Agent and extracts the token with the regular expression `vqd="([^"]*)"` from the returned
  HTML; a missing match fails with "could not find vqd token".
- `translateDuckDuckGo()` (around line 357) posts the text to
  `https://duckduckgo.com/translation.js?vqd=<vqd>&query=translate&to=<code>[&from=<code>]` with
  `Content-Type: application/json`, reads `translated` and `detected_language`, and treats a non-JSON
  answer as "instance has been rate limited".

`mozhi` (`codeberg.org/aryak/mozhi`) gates the engine behind the `MOZHI_DUCKDUCKGO_ENABLED`
environment variable (`utils/main.go:45`), yet both public instances probed above answer
`engine=duckduckgo` today.

### 2.3 What the route needs

1. A `vqd` token read out of an HTML page with a regular expression — exactly the "token scraped from
   an HTML page" the stage rule forbids.
2. A browser User-Agent; without it the endpoint answers `403 {"error":"Forbidden"}` even with a valid
   token.
3. No cookies: none were sent or received in any successful probe, and a token scraped at 22:44 still
   answered at 23:10 (26 minutes later) from the same User-Agent.
4. A body limit of its own: 986 characters → `200`; 2006 characters → `400 {"error":"invalid_text"}`;
   an unsupported target → `400 {"error":"invalid_to"}`. The limit lies between 986 and 2006
   characters, so a real client would have to chunk below 986, much smaller than any other provider.
5. The token is per client and can be rate-limited or turned into a challenge without notice; the
   response is an undocumented frontend payload, not an API.

### 2.4 Terms and stability

- Translation is **not** a DuckDuckGo API. It is an internal frontend route of the search page
  (`translation.js`), guarded by a search token. There is no documentation, no versioning and no
  support.
- The Terms of Service (`https://duckduckgo.com/terms`, last updated 2025-07-01) and the Acceptable
  Use Policy (`https://duckduckgo.com/acceptable-use`) do not name automated translation explicitly.
  The AUP forbids reselling any portion of the services, framing or inline-linking the services,
  interfering with their performance, and "violating any terms that apply to the particular service";
  it reserves the right to suspend access "for any users who we deem in violation of the spirit of
  these conditions". An application that scrapes the token and replays the frontend route is on the
  wrong side of "the spirit of these conditions", and the endpoint can be closed at any time.
- It can disappear by a rename of `translation.js`, a change of the `vqd` extraction point, a
  User-Agent rule or a challenge page — all invisible to the protocol we observed.

### 2.5 Does `mozhi` with `engine=duckduckgo` already cover it?

Yes. QTranslate already has the `mozhi` provider (`src/services/mozhiservice.*`), its engine list
already contains `duckduckgo` (`mozhiEngineIds()`, `src/core/settings.cpp`), and two public Mozhi
instances answered the DuckDuckGo engine with `200` today. A standalone `duckduckgo` service would
re-implement the same frontend route under a new name, with the same token scraping inside the
application instead of on the instance, and would add nothing a user cannot get by setting the Mozhi
engine. `doc/free-endpoints.md` §10 already forbids presenting this class of route as a provider.

### 2.6 Verdict

**Not viable.** The only browser-less route needs a scraped `vqd` token and a browser User-Agent and
is an undocumented frontend endpoint; the rule forbids exactly that, the terms do not bless it, the
body limit is between 986 and 2006 characters, and the `mozhi` provider already reaches the same
engine. `02-duckduckgo`, `01-duckduckgo-service` and `02-duckduckgo-ui` are dropped with this note as
the reason.

## 3. Map decision

- **Worked:** `03-libretranslate` (and its two children).
- **Dropped:** `02-duckduckgo` together with `01-duckduckgo-service` and `02-duckduckgo-ui`. The
  research note is the recorded reason.
- **Order:** the stage numbering put DuckDuckGo first and LibreTranslate second. With DuckDuckGo
  dropped, LibreTranslate is the only provider and there is no order left to choose; `03-libretranslate`
  can keep its number, and the lead only removes `02-duckduckgo` from its `depends` list (leaving
  `01-endpoint-research`). Renaming `03-libretranslate` to `02-libretranslate` is cosmetic and would
  touch the two child plans' paths; it is not required by the evidence.
- **Default base URL:** empty, `isConfigured() == false`, a configuration hint (§1.10). No public
  key-less instance answered today, and a default would send text to a volunteer server the user did
  not choose.
- **`api_key`:** not needed for the viable path. The hosted `libretranslate.com` needs one and stays
  out of scope; adding a secret field later is a separate user decision, not a silent part of this
  stage.
- **Documentation for the lead:** `doc/free-endpoints.md` gains a LibreTranslate section (§1.1-§1.4,
  §1.7, §1.10) and a line that DuckDuckGo was researched and dropped (§2); `doc/architecture.md`
  gains the provider row and the no-default-base-URL rule; both READMEs gain `LibreTranslate` in the
  provider list once the service exists.

## 4. User decision (2026-11-15)

The user overrode two conclusions of this note and chose both providers.

- **DuckDuckGo — add it.** The route in §2.3 stands (a `vqd` token from the page HTML, a browser
  User-Agent, no cookies, a body limit between 986 and 2006 characters), but the user accepted the
  token scrape because `BingWebService` already uses the same pattern
  (`params_AbusePreventionHelper` from the page plus a request-scoped User-Agent). Implement it with a
  conservative chunk below 986 characters and a token refresh on a rejected token.
- **LibreTranslate — add it with a base URL and an optional API key.** A self-hosted server works
  without a key; a hosted instance such as `libretranslate.com` works with a key. The key is stored as
  a secret like the other provider keys and sent as `api_key` in the JSON body when set. The default
  base URL stays empty.
- **Order:** `02-duckduckgo` first, then `03-libretranslate`.

This section supersedes the map decision and the `api_key` bullet of §3; the observations above are
unchanged and remain the implementation source.

### Follow-up probe (2026-11-15)

The four-code table above is too narrow. A single `vqd` token from the page above, then
`POST https://duckduckgo.com/translation.js?vqd=...&query=translate&to=<code>` with the body `Hello`,
a browser User-Agent and no `from`, returned `200` with a translated text for `de`, `fr`, `es`, `it`,
`pt`, `zh-Hans`, `ja`, `ko`, `pl`, `uk`, `tr`, `ar`, `hi` and `ru`. That matches the source: Mozhi
describes the engine as "1-1 with Bing Translate", and `langlist-duckduckgo.go` in
`codeberg.org/aryak/libmozhi`, commit `7324b5c528fe`, is the Bing list converted to Mozhi's format
(the file header says so). The implementation reuses the observed Bing catalogue
(`kBingWebCodes` and its overrides) instead of a four-code table.

## Appendix A. Probes

All probes used neutral sentences only. No cookie jar, no token and no text of a user was sent or
stored. The host needs an HTTP proxy for some destinations; where it mattered (the `fedilab` and
`cutie.dating` probes) the result was checked both directly and through the proxy.

### A.1 LibreTranslate, self-hosted (`http://localhost:5055`, v1.9.6, 2026-11-14 23:07-23:08 UTC)

```
GET /health
  200 {"status":"ok"}
GET /languages                                        (server loaded only en,ru)
  200 [{"code":"en","name":"English","targets":["en","ru"]},{"code":"ru","name":"Russian","targets":["en","ru"]}]
POST /translate {"q":"Hello","source":"en","target":"ru"}
  200 {"translatedText":"Привет"}
POST /translate {"q":"Доброе утро","source":"auto","target":"en"}
  200 {"detectedLanguage":{"confidence":90.0,"language":"ru"},"translatedText":"Good morning"}
POST /translate {"q":"The cat sleeps on the windowsill.","source":"auto","target":"ru"}
  200 {"detectedLanguage":{"confidence":100.0,"language":"en"},"translatedText":"Кошка спит на подоконнике."}
POST /translate {"q":"Hello","source":"en","target":"xx"}
  400 {"error":"xx is not supported"}
POST /translate {"q":"Hello","source":"en","target":"ja"}     (no ja model installed)
  400 {"error":"ja is not supported"}
POST /translate {"source":"en","target":"ru"}
  400 {"error":"Invalid request: missing q parameter"}
POST /translate {"q":"Hello","target":"ru"}
  400 {"error":"Invalid request: missing source parameter"}
POST /translate {bad
  400 {"error":"The browser (or proxy) sent a request that this server could not understand."}
POST /translate, no Content-Type
  400 {"error":"Invalid request: missing q parameter"}
GET /translate
  405
POST /translate {"q":["Hello","Good morning"],"source":"en","target":"ru"}
  200 {"translatedText":["Привет","Доброе утро"]}
POST /translate {"q":"<p>Hello</p>","source":"en","target":"ru","format":"html"}
  200 {"translatedText":"<p>Привет.</p>"}
POST /detect {"q":"Доброе утро"}
  200 [{"confidence":90.0,"language":"ru"}]
POST /translate, 6460-character q
  200 (whole text translated)
POST /translate, 4999 and 5001 characters
  200, 200
User-Agent qtranslate/0.13.0  → 200 {"translatedText":"Привет"}
no User-Agent header         → 200 {"translatedText":"Привет"}
```

~30 requests in about two minutes, no `429`, no `Server` error.

### A.2 LibreTranslate, public instances (2026-11-14 22:40-23:08 UTC)

```
GET  https://libretranslate.com/health                       → 200 application/json
GET  https://libretranslate.com/languages                    → 200 application/json, 15,907 bytes, 51 codes
POST https://libretranslate.com/translate {"q":"Hello","source":"en","target":"ru"}
                                                             → 400 {"error":"Visit https://portal.libretranslate.com to get an API key"}
POST … with "api_key":"not-a-real-key"                       → 403 {"error":"Invalid API key"}
GET  https://translate.fedilab.app/languages                 → 403 text/html (93 bytes), fixed policy page
POST https://translate.fedilab.app/translate (browser UA)    → 403 text/html, same page
POST https://translate.cutie.dating/translate                → not performed: TCP connect to 109.230.238.120:443 timed out
```

### A.3 DuckDuckGo (2026-11-14 22:44-23:10 UTC)

```
GET https://duckduckgo.com/?q=translate (Chrome UA)
  200 text/html 27,880 bytes; vqd="4-67470183055133197843329598710310253095"
POST https://duckduckgo.com/translation.js?vqd=<vqd>&query=translate&to=de   body "Hello"
  200 application/json {"detected_language":"en","translated":"Hallo"}
POST …&to=de   body "Hello"   (curl's default UA)
  403 application/json {"error":"Forbidden"}
POST …&to=de   body "Hello"   (no vqd parameter)
  403, empty body
POST …&to=en   body "Bonjour, comment allez-vous ?"
  200 {"detected_language":"fr","translated":"Hello, how are you?"}
POST …&to=en&from=ru   body "Доброе утро"
  200 {"detected_language":null,"translated":"Good morning"}
POST …&to=xx   body "Hello"
  400 {"error":"invalid_to"}
POST …&to=ru   body 986 characters
  200, translated
POST …&to=ru   body 2006 characters
  400 {"error":"invalid_text"}
POST …&to=ru   body 5780 characters
  400 {"error":"invalid_text"}
POST …&to=de   with the token scraped at 22:44, replayed at 23:10
  200 {"detected_language":"en","translated":"Hallo"}
GET https://duckduckgo.com/?q=translate+hello+to+german (browser UA, Accept: application/json)
  200 text/html 28,476 bytes, no "Hallo", no server-rendered translation
GET …&format=json
  200 application/x-javascript, Instant Answer JSON with empty Abstract/Answer
```

### A.4 Mozhi baseline (2026-11-14 23:08 UTC)

```
GET https://mozhi.adminforge.de/api/translate?engine=duckduckgo&from=en&to=ru&text=Hello
  200 {"engine":"duckduckgo","detected":"","translated-text":"Здравствуйте","source_language":"en", …}
GET https://mozhi.pussthecat.org/api/translate?engine=duckduckgo&from=en&to=ru&text=Hello
  200 {"engine":"duckduckgo","translated-text":"Здравствуйте", …}
GET …&engine=google      (control, both instances)
  200 {"translated-text":"Привет", …}
```

## Appendix B. Sources

- `https://docs.libretranslate.com/community/mirrors/` — fetched 2026-11-15; three HTTP mirrors.
- `https://docs.libretranslate.com/guides/supported_languages/` — fetched 2026-11-15; the Argos
  model pairs and sizes (`en_ru` 187 MB, `ru_en` 149 MB).
- `https://github.com/LibreTranslate/LibreTranslate` — `libretranslate/language.py` (the `zh`/`zt`/`pb`
  aliases), `libretranslate/default_values.py` (`CHAR_LIMIT`/`REQ_LIMIT`/`BATCH_LIMIT` default `-1`),
  `libretranslate/app.py` (`@bp.errorhandler(429)` → `{"error":"Slowdown: …"}`); read 2026-11-15.
- `libretranslate/libretranslate:latest`, digest
  `sha256:6f03f063a10d120cdb5986e8995d4524729d14125269bc6c49f791e49789aec8`, version v1.9.6, `LICENSE`
  = AGPL-3.0 — read 2026-11-15.
- `https://codeberg.org/aryak/libmozhi` — `engines.go` at commit `7324b5c528fe` (2026-10-29), the
  `vqd` scrape and the `translation.js` call; read 2026-11-15.
- `https://codeberg.org/aryak/mozhi` — `utils/main.go` (the `MOZHI_DUCKDUCKGO_ENABLED` gate),
  `go.mod`; read 2026-11-15.
- `https://duckduckgo.com/terms` (last updated 2025-07-01) and `https://duckduckgo.com/acceptable-use`
  — read 2026-11-15.
- `doc/free-endpoints.md` §5, §8-§10; `doc/local-translation.md` §2 and §7; `src/core/languages.cpp`
  (`kLanguages`), `src/core/settings.cpp` (the Mozhi URL helpers at lines 334-357 and
  `fallbackServiceIds()`), `src/services/mozhiservice.h`, `src/services/queuedwebservice.h`.
