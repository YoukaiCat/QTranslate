---
kind: stage
status: done
base: fdd3bfd
depends: [01-free-web-infrastructure]
---

# Yandex web

## Goal

Add the unofficial Yandex web endpoint as an opt-in free service. It is important for the Russian
audience because it covers ru/uk/tr and several CIS languages better than the other free services,
but it is a scraped browser endpoint with no SLA and must stay a fallback. The original node was a
single task; it is split here into three children because the service and parser, the settings UI
and translations, and the documentation each have their own acceptance.

## What to do, what is touched

- `01-yandex-web-service` (task, detailed): the pure `YandexWebParser`, `YandexWebService` with the
  `POST` form protocol, the browser User-Agent for this request only, the `yandex-web` language
  branch in `languages.cpp`, registration in `AppController`, fixtures and the new
  `tst_yandexwebparser`/`tst_yandexwebservice`. No UI, translations or docs.
- `02-yandex-web-settings` (task): the `Yandex` group in `SettingsDialog` with the unofficial
  warning and the opt-in hint (no keys or URLs), `tst_settingsdialog`, and the 14 `.ts` files. No
  service behaviour change.
- `03-yandex-web-docs` (task): `doc/architecture.md`, `doc/free-endpoints.md` and the `AGENTS.md`
  test counts/status. No code.

Decisions carried through all children:

- The service is `yandex-web` and implements `TranslationService` with `requiresOptIn() == true`; it
  stays off by default and is hidden/refused while `translation/allowFreeServices` is off.
- `POST https://api.browser.yandex.com/instaserp/translate` with
  `application/x-www-form-urlencoded` and the fields `text`, `brandID=int`, `statLang=<target>`,
  `targetLang=auto`, `locale=<target>`, `clid=2270494`, `disable=serp`, `use_llm_srv=0`.
- The endpoint expects a browser User-Agent (Chrome/YaBrowser). It is set on this request only,
  after `NetworkManager::makeRequest()`, and documented as protocol emulation, not a claim that the
  app is a browser; the common `AppInfo::userAgent()` for other services is unchanged.
- The response is a JSON array; the first element contains `text` and the detected language in
  `from`. Extra fields are tolerated; an unexpected shape is `BadResponse`.
- The source may be `auto`. Internal codes are mapped to the Yandex form, only the codes with a
  known mapping are declared, and an unmapped code returns `UnsupportedLanguage` before the request.
  The protocol has no source-language field: every request sends `targetLang=auto`; the target is
  sent as `statLang` and `locale`; the detected `from` is mapped back to the internal code.
- 5000 UTF-16 code units per chunk via `TextChunker::split`; chunks are sent sequentially inside one
  `QueuedWebService` slot with the 750 ms pause, and edge whitespace is restored.
- Errors: a bare 429/403 and a rate-limit body → `RateLimited`; 5xx without a recognised body →
  `Network`;
  an HTML page, a captcha page or a blocked response → `ServiceUnavailable` without raw HTML; 413 →
  `TooLong`; other non-2xx → `BadResponse`; timeout/`abort()` → `Timeout`/`Cancelled`.
- No captcha solver, cookie harvesting or bypass of a block. The text, the body and the URL are
  never logged; the browser User-Agent is not logged as if it were a secret.
- Tests use fixtures and a local `QTcpServer`; a live smoke is manual because the endpoint may block
  an IP. `README.md`, `README.ru.md`, `doc/handoff.md`, the root plan and `07-fallback-chain` are not
  edited here; the final provider/fallback pass belongs to `07-fallback-chain`/the lead.

## Done when

- `01-yandex-web-service` is done: `YandexWebParser` and `YandexWebService` exist and are registered
  behind the gate; the POST form fields, the browser User-Agent on this request only, the language
  mapping, `auto`, 5000-unit chunks, the 750 ms pause, the classified errors and the message hygiene
  are covered by `tst_yandexwebparser`, `tst_yandexwebservice` and `tst_languages`; Linux is 35 tests
  and Windows would be 30.
- `02-yandex-web-settings` is done: the Services page has a gate-aware `Yandex` group with the
  unofficial warning and the opt-in hint, no key or URL field; `tst_settingsdialog` covers it; all 14
  `.ts` files are complete and `tst_translations` passes.
- `03-yandex-web-docs` is done: `doc/architecture.md`
  §3.1/§3.2/§4.5/§4.7.5/§4.8/§4.9/§8/§4.14/§9.2, `doc/free-endpoints.md` §2/§8/§9 and `AGENTS.md`
  describe the implemented service and the 30/35 counts; all relative links resolve.
- The overall Yandex criteria hold: a supported target is sent as the mapped Yandex code; `auto` is
  accepted and `targetLang=auto` is sent as documented; an unmapped language is refused before any
  request; the detected language is mapped back; long text is split and paced; a bare 429/403, a
  rate-limit body and HTML/blocked responses are clear and stop the request; official Google/DeepL
  remain primary and the service is not enabled by default.
- The manual live smoke is recorded as passed or as not performed with the reason (the endpoint may
  return a captcha/403); Debug and Release build without warnings and the full suite passes; Windows
  Debug/Release and its 30 tests are recorded as not performed (no MSVC).

## Depends on

`01-free-web-infrastructure` (the opt-in gate and `QueuedWebService`). The three children are
sequential in the order above: `02-yandex-web-settings` depends on `01-yandex-web-service` for the
service id and the gate; `03-yandex-web-docs` depends on both for the final behaviour and counts.
`07-fallback-chain` depends on this node.

## Risks and what is needed from the user

- The endpoint is unofficial and browser-oriented. A browser User-Agent is required today, but the
  fields, the clid, or the response shape may change without notice; the parser must fail with a
  clear `BadResponse`/`ServiceUnavailable`, never a crash or raw HTML. Do not add a captcha solver,
  cookie harvesting, or any bypass of a block.
- The terms may restrict automated use. Keep the service opt-in, mark it unofficial, and do not make
  it the only translation method; the user must enable it explicitly.
- The endpoint may block a datacenter or a country IP. The automated gate is the local-server test;
  a live smoke is manual and its failure is a recorded result, not a code change. The user may run a
  short ru/uk/tr smoke when convenient; otherwise it is recorded as not performed.
- The browser User-Agent is set per request; do not change `NetworkManager::makeRequest()` or
  `AppInfo::userAgent()`, because the free Google endpoint and the official APIs rely on the app
  User-Agent.
- The doc gives no Yandex-specific chunk limit, and the 750 ms pause comes from
  `doc/free-endpoints.md` §8. The 5000 UTF-16-unit chunk and the 750 ms pause stay named constants
  and documented assumptions; record any measured limit from a live smoke.
- The protocol has no source-language field; the request always sends `targetLang=auto`, and an
  explicit source is validated while the endpoint detects the source itself. If a live smoke shows a
  source field or different semantics, record it; do not guess silently.
- Windows Debug/Release and its 30 tests are unavailable on this Linux machine; record them as not
  performed, not as passed.

## Outcome

Accepted on 2026-11-09 over `fdd3bfd..7b2e574`. Three children are `done`:
`01-yandex-web-service` (`34dd4a6`), `02-yandex-web-settings` (`a1e0051`), and
`03-yandex-web-docs` (`7b2e574`, lead).

- **Service:** `yandex-web` is an opt-in `QueuedWebService` with the fixed `POST` endpoint, the
  documented form fields, a browser Chrome/YaBrowser UA on this request only, constant
  `targetLang=auto` (no source field; the target is `statLang`/`locale`), 5000 UTF-16 chunks, a
  750 ms pause, the 71-code whitelist with `zh-Hans`/`zh-Hant`→`zh`, `nb`→`no`, `pt-BR`→`pt`,
  `SameOriginRedirectPolicy`, the array response, body-first error classification, and no text,
  body, URL, or UA in the logs.
- **Settings/UI:** the gate-aware `Yandex` group with the unofficial/opt-in hint, no key or URL
  field; two new strings in all 14 `.ts` (309); `tst_settingsdialog` covers the group.
- **Docs:** architecture §3.1/§3.2/§4.5/§4.7.5/§4.8/§4.9/§8/§4.14/§9.2, `free-endpoints.md`
  status/§2/§8/§9, `AGENTS.md` 30/35.
- **Verification:** Astra Linux 1.8.7 (Qt 6.10.2) and `qtranslate-trixie` (Qt 6.8.2), Debug and
  Release, zero warnings, 35/35; `tst_yandexwebservice` 10 repeats per configuration; the gate/INI
  path checked offscreen. No code defects; no fixes.

Known limitations: `zh-Hant` is declared but sent as `zh`, so Traditional Chinese is served as
Simplified; `pt-PT` is not declared and `closest()` maps it to `pt-BR`; 750 ms and 5000 UTF-16 are
documented assumptions. The cache-generation race remains a deferred shared problem. Windows 30 and
the live Yandex smoke were not performed (no MSVC; captcha/403 risk). Next: `05-bing-web`.
