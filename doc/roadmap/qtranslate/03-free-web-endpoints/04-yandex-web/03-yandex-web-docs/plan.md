---
kind: task
status: todo
depends: [01-yandex-web-service, 02-yandex-web-settings]
---

# Yandex documentation and status

## Task

Bring the architecture, free-endpoints and test-count documents in line with the implemented
`yandex-web` service. This is documentation only: no code, tests, UI or translations. It depends on
`01-yandex-web-service` for the protocol, the language mapping and the counts, and on
`02-yandex-web-settings` for the settings group and its translation strings.

`doc/architecture.md`:
- §3.1: add `yandexwebservice.h/.cpp` and `yandexwebparser.h/.cpp` to the `src/services/` block,
  add `yandexweb/` to the fixtures line, and add `tst_yandexwebparser`/`tst_yandexwebservice` to the
  tests tree (the service test marked `service against a local HTTP server`).
- §3.2: add `yandexwebparser`/`yandexwebservice` to the `qtranslate_core` list, add both tests to the
  tests list, and change the counts to 30 Windows/35 Linux.
- §4.5: add `yandex-web` to the `id()` example and name `YandexWebService` among the
  `QueuedWebService` users with its 750 ms pause.
- New §4.7.5 `YandexWebService — unofficial Yandex web endpoint` after §4.7.4. It documents: the
  service id/display name, no key (`isConfigured() == true`), `requiresOptIn() == true` and the fact
  that it is the third implemented opt-in free service; the fixed
  `POST https://api.browser.yandex.com/instaserp/translate` and the form fields `text`,
  `brandID=int`, `statLang=<target>`, `targetLang=auto`, `locale=<target>`, `clid=2270494`,
  `disable=serp`, `use_llm_srv=0`; the browser User-Agent (Chrome/YaBrowser) set on this request only
  after `makeRequest()`, with the note that this is protocol emulation, not a claim that the app is a
  browser, and that the common `AppInfo::userAgent()` is unchanged; that the protocol has no
  source-language field, every request sends `targetLang=auto`, the target is `statLang`/`locale`,
  an explicit source is validated and the endpoint detects the source itself; the language mapping
  (see §4.8); 5000 UTF-16 code units via `TextChunker::split`, the 750 ms assumption, sequential
  chunks inside one queue slot and edge-whitespace restoration; `SameOriginRedirectPolicy`; the
  response `[{"text":...,"from":...,"to":...}]` with `text` required and `from` mapped through
  `Languages::fromServiceCode()`; the error table (a bare 429/403 and a rate-limit body →
  `RateLimited`, 413 → `TooLong`, 5xx without a recognised body → `Network`, an HTML/captcha/blocked
  body → `ServiceUnavailable`, malformed/empty/missing `text` and an unknown non-2xx status →
  `BadResponse`, timeout/`abort()` → `Timeout`/`Cancelled`); fixed messages without the text, body
  or URL and no captcha solver/cookie harvesting/bypass; the opt-in gate; `applySettings()` as a
  no-op; and the tests (`tst_yandexwebparser`, `tst_yandexwebservice`, the `yandex-web` branch in
  `tst_languages`, and the `Yandex` group in `tst_settingsdialog`). CTest never contacts the real
  endpoint; a live smoke is manual only.
- §4.8: add a Yandex paragraph: `zh-Hans`/`zh-Hant` → `zh`, `nb` → `no`, `pt-BR` → `pt`,
  `he` → `he`, and the observed ISO whitelist; `pt-PT` is not declared and `closest()` maps it to
  `pt-BR`; `yandexWebCodeOf()` is the single fix point; no new `LanguageInfo` column. Note that a
  detected `zh` reverses to `zh-Hans`, and that `zh-Hant` sends `zh` (a known limitation).
- §4.9: name Yandex as the third real opt-in consumer after MyMemory and Mozhi; the gate logic is
  unchanged.
- §8: add `tst_yandexwebparser` and `tst_yandexwebservice` rows and update the `tst_languages` row
  with the Yandex codes (`zh`, `no`, `pt`, `he`).
- §4.14: change 33 tests (25 shared plus 8 Linux platform tests) to 35 (27 shared plus 8) and "the
  other 25 tests" to "the other 27 tests". Leave the historical 0.12.0 sentence unchanged.
- §9.2 item 8: Yandex web is implemented as the third opt-in free service (§4.7.5); only Bing web
  and Reverso remain experimental. Keep the rate-limit/unofficial warning.

`doc/free-endpoints.md`:
- Update the status line/date: DeepL web, MyMemory, Mozhi and Yandex web are implemented, Bing web
  and Reverso are research.
- In §2, add an "Implementation in qtranslate" note with the request, the fields, the browser UA
  rule, the `targetLang=auto`/no-source-field rule, the language mapping, 5000 UTF-16 units, 750 ms,
  the response fields, the error mapping, the opt-in gate and the tests. State explicitly that the
  fallback to Mozhi `engine=yandex` on failure is **not** part of this service; it belongs to
  `07-fallback-chain`.
- In §8 items 3 and 4, add Yandex's 750 ms pause and 5000 UTF-16-unit chunk and mark it implemented;
  in §9 item 4, mark Yandex web implemented while keeping "important for the Russian audience".
- Do not change the fallback item 6 or the "What not to do" section.

`AGENTS.md`: change "all 28 Windows tests and all 33 Linux tests" to "all 30 Windows tests and all 35
Linux tests".

Do not touch `README.md`, `README.ru.md`, `doc/handoff.md`, the root `plan.md` or the
`07-fallback-chain` plans; the final provider/fallback pass belongs to `07-fallback-chain`/the lead.

Verification: check the changed section numbers and cross-references, all relative Markdown links,
the absence of stale "Yandex ... research" statements in the updated documents, and consistency of
the 30/35 counts and the 35-tests/27-shared statement with `ctest -N` from
`01-yandex-web-service`. `git diff --stat` must show only `doc/architecture.md`,
`doc/free-endpoints.md` and `AGENTS.md`. No commit is made.

## Done when

- `doc/architecture.md` lists the Yandex sources, fixtures and both test targets in §3.1/§3.2, says
  30 Windows/35 Linux, has `yandex-web` in the §4.5 `id()` example and the 750 ms queue user, the
  new §4.7.5 with the protocol, browser UA rule, no-source-field/targetLang rule, mapping, chunking,
  response, error table and tests, the §4.8 mapping paragraph, the §4.9 third-consumer paragraph,
  the §8 rows and the §4.14 counts (35 tests, 27 shared, "the other 27 tests"), and §9.2 item 8
  names Yandex as implemented with only Bing/Reverso experimental.
- `doc/free-endpoints.md` has the updated status line/date, the §2 implementation note (including
  that the Mozhi `engine=yandex` fallback is `07-fallback-chain`, not this service), Yandex marked
  implemented in §8 items 3-4 and §9 item 4, and the existing privacy/unofficial rules are kept.
- `AGENTS.md` says 30 Windows/35 Linux.
- All relative links in the changed documents resolve; no stale "Yandex web ... research" claim
  remains in `doc/architecture.md` or `doc/free-endpoints.md`; the counts match the test targets.
- Only `doc/architecture.md`, `doc/free-endpoints.md` and `AGENTS.md` are changed; no code, tests,
  UI, `.ts`, README, `doc/handoff.md`, root plan or `07-fallback-chain` file is touched; no commit is
  made.

## Risks

- The docs must match the code: the id is `yandex-web`, the endpoint is fixed, there is no source
  field, `targetLang=auto` is constant, `pt-PT` is not declared, the 5000/750 values are assumptions,
  and the 35/30 counts are the actual targets. A stale count or a claimed live smoke is worse than a
  "not performed" note.
- Do not claim the Mozhi `engine=yandex` fallback as part of this service. The original Yandex
  research note mentions it, but the fallback chain is `07-fallback-chain`; `doc/free-endpoints.md`
  §2 must say so.
- The browser User-Agent must be described as per-request protocol emulation, not as a secret and
  not as a change to `NetworkManager::makeRequest()` or `AppInfo::userAgent()`.
- The historical 0.12.0 sentence in §4.14 and the old release notes stay unchanged; only the current
  counts and status change.
- `README.md`, `README.ru.md`, `doc/handoff.md`, the root plan and `07-fallback-chain` intentionally
  keep their current wording until the final pass; do not "fix" them here.
- A live smoke is not performed by this task; record it as not performed with the reason (the
  endpoint may return a captcha/403) instead of claiming success.
