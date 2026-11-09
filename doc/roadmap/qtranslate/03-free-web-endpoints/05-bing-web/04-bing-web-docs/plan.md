---
kind: task
status: done
detailed: true
depends: [02-bing-web-service, 03-bing-web-settings]
---

# Bing documentation and status

## Task

Bring the architecture, free-endpoints and test-count documents in line with the implemented
`bing-web` service. This is documentation only: no code, tests, UI or translations. It depends on
`02-bing-web-service` for the protocol, the language mapping, the token/cookie behaviour and the
counts, and on `03-bing-web-settings` for the settings group and its translation strings.

`doc/architecture.md`:
- §3.1: add `bingwebservice.h/.cpp` and `bingwebparser.h/.cpp` to the `src/services/` block, add
  `bingweb/` to the fixtures line, and add `tst_bingwebparser`/`tst_bingwebservice` to the tests tree
  (the service test marked `service against a local HTTP server`).
- §3.2: add `bingwebparser` and `bingwebservice` to the `qtranslate_core` list, add both tests to the
  tests list, and change the counts to 32 Windows/37 Linux.
- §4.5: add `bing-web` to the `id()` example and name `BingWebService` among the `QueuedWebService`
  users with its 1000 ms pause (DeepL 2 s, MyMemory 350 ms, Mozhi 500 ms, Yandex 750 ms, Bing 1000 ms).
- New §4.7.6 `BingWebService — unofficial Bing web endpoint` after §4.7.5. It documents: the id
  `bing-web`/display name `Bing`, no key (`isConfigured() == true`), `requiresOptIn() == true` and
  the fact that it is the fourth real opt-in free service; no key/URL field and no `[bing]` settings
  group, so `applySettings()` only clears the token cache and no `AppSettings`/INI key is added;
  `AppController` registers it after Yandex. The two-step request block:
  `GET https://www.bing.com/translator` with the markers `IG:"..."`, the first `data-iid="..."`,
  `params_AbusePreventionHelper = [key,"token",lifetimeMs];`, and the optional `"muid"`, `"sid"`,
  `"tid"` values; then `POST https://www.bing.com/ttranslatev3?isVertical=1&IG=...&IID=...` with
  `Content-Type: application/x-www-form-urlencoded` and the fields `text`, `fromLang`, `to`, `token`,
  `key`, `isAuthv2=true`, `tryFetchingGenderDebiasedTranslations=true`, both names and values
  percent-encoded as UTF-8. State that the body is urlencoded, **not multipart**: the 2026-11-09
  observation returned `{"statusCode":205,"errorMessage":""}` for multipart with the same fields,
  while the urlencoded form translated. Document the browser User-Agent as per-request protocol
  emulation, `AppInfo::userAgent()` unchanged, and no captcha solver/headless browser/cookie
  harvesting/block bypass. Document the explicit `Cookie: MUID=...` from `Set-Cookie` (preferred) or
  the page marker, the `CookieLoadControlAttribute`/`CookieSaveControlAttribute = Manual` rule and
  the cross-service isolation; the token cache for the page-reported lifetime (default 3600000 ms,
  about one hour), per instance/process, never persisted, cleared by `applySettings()`; the one-time
  refresh on HTTP 401 or a body `statusCode` 401 and the second-401 `RateLimited`; a 403 as a block
  without refresh; the 1000 ms pause and 5000 UTF-16-unit chunks; the `[{"translations":[...],
  "detectedLanguage":{"language":...}}]` response with `text` joined and the detected language
  mapped; the error table; `SameOriginRedirectPolicy`; the gate; and the tests
  (`tst_bingwebparser`, `tst_bingwebservice`, the `bing-web` rows in `tst_languages`, and the `Bing`
  group in `tst_settingsdialog`). CTest never contacts the real endpoint; a live smoke is manual only.
- §4.8: add a Bing paragraph: `auto` is sent as `auto-detect`; `pt-BR`→`pt`, `pt-PT`→`pt-PT`,
  `sr`→`sr-Cyrl`, `nb`→`nb`, `zh-Hans`/`zh-Hant`, `he`, `fil`, `jv`, `hmn` unchanged, and the
  observed catalogue; a detected `pt` reverses to `pt-BR`, `sr-Cyrl`/`sr-Latn` to `sr`. Codes
  outside the catalogue (for example `ceb`, `kk`, `pa`, `haw`) are not declared. `bingWebCodeOf()` is
  the single fix point; no `LanguageInfo` column is added.
- §4.9: name Bing as the fourth real opt-in consumer after MyMemory, Mozhi and Yandex; the gate
  logic is unchanged.
- §8: add `tst_bingwebparser` and `tst_bingwebservice` rows, update the `tst_languages` row with the
  Bing codes (`pt`, `pt-PT`, `sr-Cyrl`, `nb`, `sr-Latn`→`sr`) and the `tst_settingsdialog` row with
  the `Bing` group.
- §4.14: change 35 tests (27 shared plus 8 Linux platform tests) to 37 (29 shared plus 8) and "the
  other 27 tests" to "the other 29 tests". Leave the historical 0.12.0 sentence in §9.2 unchanged.
- §9.2 item 8: Bing web is implemented as the fourth opt-in free service (§4.7.6); only Reverso
  remains experimental. Keep the rate-limit/unofficial warning.

`doc/free-endpoints.md`:
- Update the status line/date: DeepL web, MyMemory, Mozhi, Yandex web and Bing web are implemented;
  Reverso is research.
- In §3, after the research bullets, add an `Implementation in qtranslate (2026-11-09)` note with the
  two-step handshake, the markers, the urlencoded POST and fields, the `MUID` cookie isolation, the
  token cache/refresh, the 1000 ms/5000 UTF-16 assumptions, the response shape, the error mapping, the
  opt-in gate and the tests. Correct the `Format:` bullet from the ambiguous "form-data" to
  `application/x-www-form-urlencoded` and record the multipart rejection observation. State that a
  live smoke is manual and was recorded as passed or not performed with the reason, never claimed
  without a run.
- In §8 items 2-4, name Bing web as the fourth gated service, add its 1000 ms pause and 5000
  UTF-16-unit chunk and mark it implemented; in §9 item 5, mark Bing web implemented. Leave the
  fallback item 6 and §10 "What not to do" unchanged.

`AGENTS.md`: change "all 30 Windows tests and all 35 Linux tests" to "all 32 Windows tests and all 37
Linux tests".

Do not touch `README.md`, `README.ru.md`, `doc/handoff.md`, the root `plan.md` or the
`07-fallback-chain` plans; the final provider/fallback pass belongs to `07-fallback-chain`/the lead.
The READMEs must list Bing web as an unofficial, fragile fallback in the lead's final pass, not here.

Verification: check the changed section numbers and cross-references, all relative Markdown links,
the absence of stale "Bing web ... research" statements in the updated documents, and consistency of
the 32/37 counts and the 37-tests/29-shared statement with `ctest -N` from `02-bing-web-service`.
`git diff --stat` must show only `doc/architecture.md`, `doc/free-endpoints.md` and `AGENTS.md`. No
commit is made.

## Steps

1. Update `doc/architecture.md`: §3.1 tree, fixtures and test list; §3.2 source/test lists and counts;
   §4.5 the `id()` example and the queue users; new §4.7.6 for `BingWebService`; §4.8 the language
   mapping; §4.9 the fourth opt-in consumer; §8 the two test rows and the updated language/settings
   rows; §4.14 the 37/29 counts; §9.2 item 8.
2. Update `doc/free-endpoints.md`: the status line, the §3 `Format:` bullet and the implementation
   note, §8 items 2-4, and §9 item 5.
3. Update `AGENTS.md` to 32 Windows/37 Linux.
4. Verify links, section references, counts, the absence of stale "Bing web ... research" claims and a
   documentation-only diff; record the outcome.

## Done when

- `doc/architecture.md` §3.1 lists `bingwebservice`/`bingwebparser`, the `bingweb/` fixtures and both
  test targets; §3.2 lists both sources and both tests and says 32 Windows/37 Linux; §4.5 has
  `bing-web` in the `id()` example and `BingWebService` with 1000 ms in the queue paragraph; the new
  §4.7.6 documents the two-step handshake, the markers, the urlencoded fields, the browser UA as
  per-request emulation, the explicit `MUID` cookie and manual jar attributes, the token cache and
  one-time 401 refresh (HTTP or body 401, 403 as a block), the 1000 ms/5000 UTF-16 assumptions, the
  response shape, the body-first error table and the tests; §4.8 has the Bing mapping paragraph;
  §4.9 names Bing as the fourth opt-in consumer; §8 has both new rows and the updated
  `tst_languages`/`tst_settingsdialog` rows; §4.14 says 37 tests, 29 shared and "the other 29 tests";
  §9.2 item 8 names Bing as implemented with only Reverso experimental.
- `doc/free-endpoints.md` has the updated status line (date 2026-11-09), the §3 implementation note
  with the request/markers/urlencoded fields/UA/cookie/cache/refresh/pause/chunk/response/errors/gate
  and tests, the corrected "form-data" wording with the multipart rejection observation, and Bing
  marked implemented in §8 items 2-4 and §9 item 5; §8 item 6 and §10 are unchanged.
- `AGENTS.md` says 32 Windows/37 Linux.
- The updated sections contain no stale "Bing web ... research/planned" claim and no 30/35, 27-shared
  or "other 27" count; `ctest -N` reports 37 Linux tests, the 29-shared split matches
  `tests/CMakeLists.txt`, and the Windows 32 is recorded as derived, not run. The historical 0.12.0
  sentence in §9.2 is intentionally unchanged and is noted in the report.
- All relative links in `doc/architecture.md` and `doc/free-endpoints.md` resolve; no live smoke is
  claimed as passed without a run.
- Only `doc/architecture.md`, `doc/free-endpoints.md` and `AGENTS.md` are changed; no code, tests, UI,
  `.ts`, CMake, README, `doc/handoff.md`, root plan or `07-fallback-chain` file is touched; no commit
  is made.

## Risks

- The docs must match the implementation: the id is `bing-web`, the POST is urlencoded, the token is
  cached for the page-reported lifetime, the cookie is explicit, and the 32/37 counts are the actual
  targets. A stale count, a multipart claim or a claimed live smoke is worse than a "not performed"
  note.
- The 401 rule is subtle: HTTP 401 and a body `statusCode` 401 trigger the single refresh; a 403 is a
  block without refresh. Document both explicitly and do not copy the upstream plugin's 403-refresh
  behaviour into the docs if the implementation follows the node.
- The browser User-Agent must be described as per-request protocol emulation, not as a secret and not
  as a change to `NetworkManager::makeRequest()` or `AppInfo::userAgent()`.
- The historical 0.12.0 sentence in §9.2 and the migration-policy statements stay unchanged; only the
  current counts and status change.
- `README.md`, `README.ru.md`, `doc/handoff.md`, the root plan and `07-fallback-chain` intentionally
  keep their current wording until the final pass; do not "fix" them here.
- A live smoke is not performed by this task; record it as passed only if `02` actually ran it,
  otherwise as not performed with the reason.

## Outcome

Done. The active documentation now describes the implemented Bing web service; no code, tests, UI,
translations or CMake changed, and `README.md`, `README.ru.md`, `doc/handoff.md`, the root plan and
`07-fallback-chain` were not edited here.

- `doc/architecture.md`: §3.1 lists `bingwebservice.h/.cpp`/`bingwebparser.h/.cpp`, the `bingweb/`
  fixtures and both tests; §3.2 lists both sources and tests and says 32 Windows/37 Linux; §4.5 has
  `bing-web` in the `id()` example and `BingWebService` with 1000 ms among the queue users; the new
  §4.7.6 documents the two-step handshake, the markers, the urlencoded fields, the browser UA, the
  explicit `MUID` and manual jar attributes, the token+MUID cache and the one-time 401 refresh (403
  as a block), the 1000 ms/5000 UTF-16 assumptions, the response, the error table and the tests;
  §4.8 has the Bing mapping paragraph; §4.9 names Bing as the fourth opt-in consumer; §8 adds
  `tst_bingwebparser`/`tst_bingwebservice` and the updated `tst_languages`/`tst_settingsdialog`
  rows; §4.14 says 37 tests, 29 shared and "the other 29 tests"; §9.2 item 8 names Bing as
  implemented with only Reverso research.
- `doc/free-endpoints.md`: the status line, the §3 `Format:` bullet, the 2026-11-09 implementation
  note, §8 items 2-4 (four gated services, Bing 1000 ms and 5000 UTF-16) and §9 item 5.
- `AGENTS.md`: 32 Windows/37 Linux.

Verification: all relative links in both documents resolve (0 broken), `git diff --check` is clean,
`ctest -N` reports 37 Linux tests (29 shared plus 8 Linux-only), and the only changed files are
`doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md` and this plan. The historical 0.12.0
sentence in §9.2 (33 tests) is intentionally left unchanged. The live smoke was performed in
`02-bing-web-service` and passed (`Hallo Welt`, detected `en`); this task did not rerun it. Windows
Debug/Release and its 32 tests were not performed (no MSVC). No real problems were found.
