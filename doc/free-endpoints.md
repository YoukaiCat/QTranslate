# Free web translation endpoints

Date: 2026-11-05. Status: **roadmap stage 3 (old stage 13); DeepL web is implemented as a `free-web`
mode of the DeepL service; the other endpoints are research**.

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
- **Limits:** unofficial endpoint; QTranslate keeps a 750 ms pause and on error tries Mozhi with
  `engine=yandex`.
- **Why:** good coverage of ru/uk/tr and CIS languages.

## 3. Bing web

- **Preparation:** `GET https://www.bing.com/translator` and HTML parsing: `IG`, `data-iid`,
  `params_AbusePreventionHelper` (key/token), `MUID`, `SID`, `TID`.
- **Request:** `POST https://www.bing.com/ttranslatev3?isVertical=1&IG=...&IID=...`
- **Format:** form-data: `text`, `fromLang`, `to`, `token`, `key`, `isAuthv2=true`,
  `tryFetchingGenderDebiasedTranslations=true`; cookie `MUID`.
- **Response:** JSON with `detectedLanguage` and a `translations[].text` array.
- **Limits:** the token lives about an hour; on `401`, refresh the token once and retry, without
  loops. Bing's HTML changes, so the parser is fragile.
- **Complexity:** higher than the others because of token and cookie extraction.

## 4. Reverso

- **Request:** `POST https://cps.reverso.net/api2/TranslateText`
- **Body:** JSON `{"source":"...","direction":"...","deviceId":"0","uiLang":"en",
  "origin":"chromeextension","accessToken":"","appId":"0"}`.
- **Response:** `translation`, `directionFrom`, `directionTo`, `success`, `message`.
- **Limits:** unofficial endpoint; a 600 ms pause; its own direction codes; narrower language
  coverage than Google/DeepL.

## 5. Mozhi

- **Request:** `GET https://<instance>/api/translate?engine=<engine>&from=<src>&to=<dst>&text=...`
- **Response:** `"translated-text"`, `detected`, `"source_language"`, `"target_transliteration"`.
- **Engines:** Google, DeepL, DuckDuckGo, LibreTranslate, MyMemory, Reverso, Yandex.
- **Instances:** public (`mozhi.adminforge.de`, `mozhi.pussthecat.org`, and others) or self-hosted;
  public ones may be unavailable, so instance selection and a fallback are needed.
- **Pro:** one integration gives several engines and a self-hosted option.
- **Con:** it is a proxy to third-party endpoints; stability and terms depend on the instance.

## 6. MyMemory

- **Request:** `GET https://api.mymemory.translated.net/get?q=...&langpair=en|ru&mt=1`
- **Response:** `responseData.translatedText`, `responseStatus`, `responseDetails`, `quotaFinished`.
- **Limits:** a free API, but about **500 bytes per request**; an explicit source language is
  required (auto-detection is not supported); there is a quota; QTranslate keeps a 350 ms pause and
  splits text into 500-byte chunks.
- **Pro:** no key is needed, and it is more predictable than scraping.
- **Con:** short requests, mixed quality, and many requests for long texts.

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
   warning in settings; there is no separate service. The common "Allow free web services" toggle
   (`translation/allowFreeServices`, off by default) is implemented infrastructure: every service
   that overrides `requiresOptIn()` is hidden from the pickers and refused by `TranslationManager`
   with `ServiceUnavailable` until the user enables it. The DeepL `free-web` mode stays outside the
   gate as an explicit mode with its own warning.
2. For each service — its own `TranslationService` and parser; no keys are needed.
3. Per-host rate limiting: implemented infrastructure in `QueuedWebService` — replies of one host
   are serialized and paused by the service-specific value (DeepL 2 s, Yandex 750 ms, Reverso
   600 ms, MyMemory 350 ms); no parallel requests to one host.
4. Text splitting: DeepL 5000 characters, MyMemory 500 bytes, the rest — according to the service
   limits.
5. Errors: `429`/`403`/an HTML stub → `RateLimited`/`ServiceUnavailable` with a clear message,
   without a retry storm.
6. Fallback: the official API → free web → Mozhi/another service; do not show raw HTML.
7. Privacy: the text goes to a third-party service; do not write text, keys, or cookies to the log.
8. Tests: response fixtures and a local `QTcpServer`; real endpoints — manual smoke only, because
   they are live and unstable.

## 9. Priorities

1. **DeepL web** — implemented: high quality but high risk; available as a DeepL mode.
2. **MyMemory** — the simplest and relatively official free API.
3. **Mozhi** — one integration, many engines, self-hosting possible.
4. **Yandex web** — important for the Russian audience.
5. **Bing web** — fragile auth and HTML; do it after the rest.
6. **Reverso** — niche, fewer languages.

## 10. What not to do

- Do not enable unofficial endpoints by default or present them as an official API.
- Do not bypass paid quotas or publish other people's keys/cookies.
- Do not use public Mozhi/LibreTranslate instances as the only channel without a fallback.
- Do not copy QTranslate code without complying with MPL-2.0; write implementations yourself from
  the observed HTTP protocol.
