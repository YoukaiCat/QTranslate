---
kind: task
status: done
detailed: true
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
- §8: add `tst_yandexwebparser` and `tst_yandexwebservice` rows, update the `tst_languages` row with
  the Yandex codes (`zh`, `no`, `pt`, `he`) and the `tst_settingsdialog` row with the `Yandex` group.
- §4.14: change 33 tests (25 shared plus 8 Linux platform tests) to 35 (27 shared plus 8) and "the
  other 25 tests" to "the other 27 tests". Leave the historical 0.12.0 sentence in §9.2 unchanged.
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
- In §8 items 2-4, name Yandex web as the third gated service, add its 750 ms pause and
  5000 UTF-16-unit chunk and mark it implemented; in §9 item 4, mark Yandex web implemented while
  keeping "important for the Russian audience".
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

## Steps

1. **Update `doc/architecture.md` §3.1.** In the `src/services/` block, after
   `mozhiparser.h/.cpp` (`:216`), add `yandexwebservice.h/.cpp` (unofficial Yandex web endpoint, no
   key, opt-in) and `yandexwebparser.h/.cpp` (pure parsing for the endpoint), with the comment column
   aligned like the neighbouring lines. Change the fixtures line (`:248`) to
   `saved Google/DeepL/MyMemory/Mozhi/Yandex responses (google/, deepl/, mymemory/, mozhi/, yandexweb/)`.
   Add `tst_yandexwebparser.cpp` and `tst_yandexwebservice.cpp` to the `tests/` tree after
   `tst_windowactivation_linux.cpp` (`:282`), keeping the alphabetic order and marking the service
   test `service against a local HTTP server`, as `tst_googletranslateservice`/`tst_mozhiwebservice`
   are marked. Use the existing `tests/fixtures/yandexweb/` directory for the new fixtures path.

2. **Update `doc/architecture.md` §3.2.** In the `qtranslate_core` row (`:292`), add
   `yandexwebparser` and `yandexwebservice` after `queuedwebservice`. In the tests row (`:294`), add
   `tst_yandexwebparser` and `tst_yandexwebservice` after `tst_openaicompatibleservice`, and change
   `28 tests on Windows and 33 on Linux` to `30 tests on Windows and 35 on Linux`. The split is 27
   shared tests plus 3 Windows-only and 8 Linux-only tests; `ctest -N --test-dir build/linux-release`
   already reports 35 (the service node's build), and the Windows 30 is derived from the conditional
   `qtranslate_add_test` branches in `tests/CMakeLists.txt`, not run here.

3. **Update `doc/architecture.md` §4.5.** Append `"yandex-web"` to the `id()` comment (`:644`). In
   the shared-queue paragraph (`:692-693`), name `YandexWebService` among the `QueuedWebService`
   users and add its 750 ms pause: `DeepLWebService`, `MyMemoryService`, `MozhiService` and
   `YandexWebService`, with MyMemory at 350 ms, Mozhi at 500 ms and Yandex at 750 ms. Keep the
   2000 ms default and the rest of the paragraph, including the `noteRequestSent()` explanation.

4. **Add `doc/architecture.md` §4.7.5.** Insert a `### 4.7.5.` heading with the title
   `YandexWebService — unofficial Yandex web endpoint` (the class name in backticks, as in the
   §4.7.4 heading) between the end of §4.7.4 (`:1011`) and `### 4.8. Languages` (`:1013`). Follow the
   §4.7.4 layout — an intro paragraph, a fenced request block, bullets, then an error table — and
   cover exactly:
   - Intro: id `yandex-web`, display name `Yandex`, no key (`isConfigured() == true`), the third real
     opt-in consumer after MyMemory (§4.7.3) and Mozhi (§4.7.4), `requiresOptIn() == true`,
     `TranslationManager` refusal and the hidden pickers while `translation/allowFreeServices` is
     off; there is no key or URL field and no `[yandex]` settings group, so `applySettings()` is a
     no-op and no `AppSettings`/INI key or cache reset is added; `AppController` registers it after
     Mozhi.
   - Request block: `POST https://api.browser.yandex.com/instaserp/translate`,
     `Content-Type: application/x-www-form-urlencoded`, `Accept: application/json`, and the fields
     `text`, `brandID=int`, `statLang=<target>`, `targetLang=auto`, `locale=<target>`,
     `clid=2270494`, `disable=serp`, `use_llm_srv=0`; both form names and values are percent-encoded
     as UTF-8 (`QUrl::toPercentEncoding`), so a space is `%20` and a literal `&`, `=`, `+` or `%` in
     the text cannot break the body. The endpoint is fixed (`defaultEndpoint()`); `setEndpoint()` is
     only the test seam for a local `QTcpServer`.
   - Browser User-Agent: `browserUserAgent()` returns a fixed desktop Chrome/YaBrowser string, set
     on this request only, after `NetworkManager::makeRequest()`; the shared `makeRequest()`,
     `AppInfo::userAgent()` and every other service keep the application User-Agent. Describe it as
     protocol emulation, not a claim that the application is a browser and not a secret; the value is
     not logged as one, and there is no captcha solver, cookie harvesting or block bypass.
   - Source and target: the protocol has no source-language field; every request sends
     `targetLang=auto`, the target is sent in `statLang` and `locale`, an explicit source is
     validated through `Languages::toServiceCode()` but never sent, `auto` is accepted, and the
     endpoint detects the source and returns it in `from`. An unsupported source or target finishes
     with `UnsupportedLanguage` before any request.
   - Languages: point to §4.8; state the observed 71-code whitelist, the overrides
     `zh-Hans`/`zh-Hant`→`zh`, `nb`→`no`, `pt-BR`→`pt` and unchanged `he`; `pt-PT` is not declared
     (`closest()` maps it to `pt-BR`); `yandexWebCodeOf()` is the single fix point and no
     `LanguageInfo` column is added.
   - Chunking and pacing: at most 5000 UTF-16 code units per request via `TextChunker::split`,
     sequential chunks inside one `QueuedWebService` slot, a 750 ms pause, edge whitespace removed
     before the request and restored at the joints, whitespace-only parts not sent, and a failed
     chunk failing the whole reply without a partial result. Both 5000 units and 750 ms are
     conservative assumptions, not measured limits; the pause follows `free-endpoints.md` §8.
   - Redirects: `SameOriginRedirectPolicy` on every request, so a cross-origin redirect is refused
     and the text cannot silently move to another host.
   - Response: a JSON array whose first element is an object; `text` is required and is the
     translation, `from` is mapped through `Languages::fromServiceCode()` into `detectedSourceLang`,
     and `to`/extra fields and a second element are tolerated and ignored. A missing or unknown
     `from` leaves `detectedSourceLang` empty and is not an error. An empty array, a non-object first
     element, a JSON object or string, malformed JSON, or a missing/whitespace-only `text` is
     `BadResponse`.
   - Errors: the body is classified before the HTTP status and also for 2xx responses (a 200 with an
     error body is not a success). Use this table:

     | Condition | `TranslationError` |
     |---|---|
     | a rate-limit body (`rate limit`, `too many requests`, `quota`, `status code: 429`/`403`) | `RateLimited` |
     | HTTP 403/429 without a recognised body | `RateLimited` |
     | HTTP 413 | `TooLong` |
     | HTTP 5xx without a recognised body | `Network` |
     | an HTML page, a captcha/blocked body, or a JSON `error` | `ServiceUnavailable` |
     | malformed JSON, a missing/whitespace-only `text`, an empty/non-object response, an unknown non-2xx status | `BadResponse` |
     | a network error | `Network` |
     | timeout / `abort()` | `Timeout` / `Cancelled` |

     State that messages are fixed literals and never contain the text, the body or the URL, and
     that network errors use `NetworkManager::describeNetworkError(request, false)`, never
     `QNetworkReply::errorString()`.
   - Tests: `tst_yandexwebparser` (saved fixtures), `tst_yandexwebservice` (a local `QTcpServer`),
     the `yandex-web` branch in `tst_languages`, and the `Yandex` group in `tst_settingsdialog`.
     CTest never contacts the real endpoint; a live smoke is manual only and is recorded as not
     performed with the reason (the endpoint may return a captcha/403), never as passed.

5. **Update `doc/architecture.md` §4.8 and §4.9.** After the Mozhi paragraph (`:1034-1037`), add a
   Yandex paragraph: the service keeps the internal ISO 639-1 code and declares only the codes of its
   observed 71-language catalogue, with the overrides `zh-Hans`/`zh-Hant` → `zh`, `nb` → `no`,
   `pt-BR` → `pt`; `he` is already sent as `he`, unlike Google/Mozhi's `iw`. `pt-PT` is deliberately
   not declared (one Portuguese code; `closest()` maps `pt-PT` to `pt-BR`). `yandexWebCodeOf()` is
   the single fix point, and no `LanguageInfo` column is added or table column changed. A detected
   `zh` reverses to `zh-Hans` (the first table match), `no` to `nb`, `pt` to `pt-BR`; `zh-Hant` is a
   known limitation because it is sent as `zh`, so Traditional Chinese is served as Simplified.

   After the Mozhi paragraph in §4.9 (`:1122-1125`), add: Yandex web is the third real service with
   `requiresOptIn() == true` (§4.7.5); the gate logic is unchanged; `AppController` hides it from the
   pickers while the gate is off and, if `translation/service=yandex-web` is configured with the gate
   off, keeps Google active and logs that the configured service needs free web services to be
   enabled.

6. **Update `doc/architecture.md` §8.** Add two rows after `tst_mozhiwebservice` (`:1749`):
   `tst_yandexwebparser` (the array response, `text`/`from`, extra fields and a second element
   ignored, `from` mapping, empty/non-object/malformed bodies, HTML/captcha, JSON and plain-text
   rate-limit/block bodies, the status mapping, fixed messages without the text/body/URL) and
   `tst_yandexwebservice` (the POST form, content type, all fields, UTF-8 percent-encoding, the
   browser UA on this request only, mapped target codes, constant `targetLang=auto`, `auto` source,
   5000-unit chunks, 750 ms pacing, serialization, cancellation, a refused cross-origin redirect,
   HTTP/HTML/JSON/rate-limit/timeout errors, messages without the text or URL).

   Extend the `tst_languages` row (`:1753`) with the Yandex codes (`zh`, `no`, `pt`, `he`, and
   `zh` reversing to `zh-Hans`), and the `tst_settingsdialog` row (`:1767`) with the Yandex group
   (hint and gate). Do not touch the other rows.

7. **Update `doc/architecture.md` §4.14 and §9.2 item 8.** In §4.14, change
   `33 tests (25 shared plus 8 Linux platform tests)` (`:1483`) to
   `35 tests (27 shared plus 8 Linux platform tests)`, and `the other 25 tests are shared` (`:1525`)
   to `the other 27 tests are shared`. Leave the historical 0.12.0 sentence in §9.2 (`:1931`) and
   the migration-policy sentence in §11.9 (`:2102`, `all 28 tests`) unchanged — they are historical
   statements outside this task's sections; note them in the report instead. In §9.2 item 8
   (`:1955-1959`), add Yandex web as the third implemented opt-in free service (§4.7.5) and leave
   only Bing web and Reverso experimental; keep the rate-limit/unofficial-warning sentence and the
   `free-endpoints.md` link.

8. **Update `doc/free-endpoints.md` status and §2.** Change the status line (`:3-5`) to say DeepL
   web, MyMemory, Mozhi and Yandex web are implemented (MyMemory, Mozhi and Yandex as gated no-key
   services) and Bing web and Reverso are research; keep the date 2026-11-09, it is already current.

   In §2 (`:53-63`), after the existing research bullets, add an `Implementation in qtranslate
   (2026-11-09)` note: the fixed `POST` and the form fields, the UTF-8 percent-encoding, the browser
   UA on this request only and why (protocol emulation, `AppInfo::userAgent()` unchanged), the
   `targetLang=auto`/no-source-field rule (the target goes to `statLang`/`locale`, an explicit source
   is validated only), the 71-code whitelist and the overrides (`zh-Hans`/`zh-Hant`→`zh`, `nb`→`no`,
   `pt-BR`→`pt`, `he` unchanged; `pt-PT` not declared), 5000 UTF-16 units and the 750 ms assumption
   (both named assumptions), the `[{"text","from","to"}]` response with `text` required and `from`
   mapped back, the error mapping (a rate-limit body or a bare 403/429 → `RateLimited`, 413 →
   `TooLong`, 5xx without a recognised body → `Network`, an HTML/captcha/blocked body →
   `ServiceUnavailable`, malformed/empty/missing `text` and an unknown non-2xx status →
   `BadResponse`, timeout/abort → `Timeout`/`Cancelled`, body-first), `SameOriginRedirectPolicy`, the
   opt-in gate and the tests (`tst_yandexwebparser`, `tst_yandexwebservice`, `tst_languages`,
   `tst_settingsdialog`); CTest never contacts a real endpoint, a live smoke is manual only. Also
   rewrite the existing `Limits` bullet (`:61-62`), which currently claims that qtranslate falls back
   to Mozhi `engine=yandex`: keep the research note's 750 ms/fallback finding but state that the
   `yandex-web` service does not implement that fallback and that the fallback chain belongs to
   `07-fallback-chain`. Do not claim a live smoke.

9. **Update `doc/free-endpoints.md` §8 and §9.** In §8 item 2 (`:181-183`), name Yandex web
   (`src/services/yandexweb*`) with MyMemory and Mozhi as the three real services behind the gate.
   In item 3 (`:184-186`), move Yandex from "planned" to implemented with 750 ms (Reverso 600 ms
   stays planned). In item 4 (`:187-188`), add Yandex web's 5000 UTF-16-unit chunks as the same
   conservative assumption. In §9 item 4 (`:201`), mark Yandex web implemented and keep "important
   for the Russian audience". Leave the fallback item 6 and §10 "What not to do" unchanged, and keep
   the privacy/unofficial rules.

10. **Update `AGENTS.md`.** At `:39`, change "all 28 Windows tests and all 33 Linux tests must pass"
    to "all 30 Windows tests and all 35 Linux tests must pass". Change nothing else in the file; in
    particular, do not touch the CI/platform notes above it.

11. **Verify and record.** No code, test, `.ts`, CMake or build file changes, so no rebuild or new
    test is required; use the existing `build/linux-release` tree. Run `ctest -N --test-dir
    build/linux-release` and confirm 35 tests; derive the Windows 30 (27 unconditional plus the 3
    Windows-only branches in `tests/CMakeLists.txt`) and record it as derived, not run (no MSVC), as
    `01`/`02` did. Check the edited sections for stale strings: there must be no `28 tests on
    Windows`, `33 on Linux`, `33 tests (25 shared`, `other 25 tests`, `Yandex 750 ms ... planned`,
    or `Yandex web, Bing web, and Reverso remain experimental` in the updated sections, and
    `doc/free-endpoints.md` must not claim the `engine=yandex` fallback as implemented. Check every
    relative Markdown link in the two changed documents with a read-only loop (skip `http(s)://`
    and `#` anchors). Confirm that `§4.7.5`, `§4.8` and `§9.2 item 8` cross-references are
    consistent. Run `git diff --stat` and `git status --porcelain`: only `doc/architecture.md`,
    `doc/free-endpoints.md` and `AGENTS.md` may change, with no other modifications. Do not commit.

## Done when

- `doc/architecture.md` §3.1 lists `yandexwebservice`/`yandexwebparser`, the `yandexweb/` fixtures
  and both test targets; §3.2 lists both sources and both tests and says 30 Windows/35 Linux; §4.5
  has `yandex-web` in the `id()` example and `YandexWebService` with 750 ms in the queue paragraph;
  the new §4.7.5 documents the fixed `POST` form and fields, the UTF-8 percent-encoding, the browser
  UA as per-request protocol emulation, the no-source-field/`targetLang=auto` rule, the language
  whitelist and overrides, 5000 UTF-16 units and 750 ms as assumptions, `SameOriginRedirectPolicy`,
  the array response with `text`/`from`, the body-first error table and the tests; §4.8 has the
  Yandex mapping paragraph including the `zh-Hant`→`zh` limitation and the `zh`→`zh-Hans` reverse;
  §4.9 names Yandex as the third opt-in consumer; §8 has both new rows and the updated
  `tst_languages`/`tst_settingsdialog` rows; §4.14 says 35 tests, 27 shared and "the other 27 tests";
  §9.2 item 8 names Yandex as implemented with only Bing/Reverso experimental.
- `doc/free-endpoints.md` has the updated status line (date 2026-11-09), the §2 implementation note
  with the request/fields/UA/no-source/mapping/chunking/response/errors/gate/tests, an explicit
  statement that the Mozhi `engine=yandex` fallback is not this service but `07-fallback-chain`, the
  corrected `Limits` bullet, Yandex marked implemented in §8 items 2-4 and §9 item 4, and the
  existing privacy/unofficial rules; §8 item 6 and §10 are unchanged.
- `AGENTS.md` says 30 Windows/35 Linux.
- The updated sections contain no stale 28/33 count, no "25 shared"/"other 25", and no "Yandex web
  ... research/planned" claim; `ctest -N` reports 35 Linux tests, the 27-shared split matches
  `tests/CMakeLists.txt`, and the Windows 30 is recorded as derived, not run. The historical 0.12.0
  sentence in §9.2 and the §11.9 migration sentence are intentionally unchanged and are noted in the
  report.
- All relative links in `doc/architecture.md` and `doc/free-endpoints.md` resolve; no live smoke is
  claimed as passed (it is recorded as not performed with the endpoint-blocking reason).
- Only `doc/architecture.md`, `doc/free-endpoints.md` and `AGENTS.md` are changed; no code, tests,
  UI, `.ts`, CMake, README, `doc/handoff.md`, root plan or `07-fallback-chain` file is touched; no
  commit is made.

## Risks

- The docs must match the code: the id is `yandex-web`, the endpoint is fixed, there is no source
  field, `targetLang=auto` is constant, `pt-PT` is not declared, the 5000/750 values are assumptions,
  and the 30/35 counts are the actual targets. A stale count or a claimed live smoke is worse than a
  "not performed" note.
- Do not claim the Mozhi `engine=yandex` fallback as part of this service. The original Yandex
  research note mentions it, but the fallback chain is `07-fallback-chain`; `doc/free-endpoints.md`
  §2 must say so.
- The browser User-Agent must be described as per-request protocol emulation, not as a secret and
  not as a change to `NetworkManager::makeRequest()` or `AppInfo::userAgent()`.
- The historical 0.12.0 sentence in §9.2 (`:1931`), the §11.9 migration-policy sentence (`:2102`,
  `all 28 tests`) and the old release notes stay unchanged; only the current counts and status
  change. The §11.9 sentence is outside this task's sections; note it in the report instead of
  editing it.
- `README.md`, `README.ru.md`, `doc/handoff.md`, the root plan and `07-fallback-chain` intentionally
  keep their current wording until the final pass; do not "fix" them here.
- A live smoke is not performed by this task; record it as not performed with the reason (the
  endpoint may return a captcha/403) instead of claiming success.

## Outcome

Done by the lead as a documentation-only task on 2026-11-09; no code, tests, UI, translations, or
CMake changed.

- `doc/architecture.md`: §3.1/§3.2 list `yandexwebservice`/`yandexwebparser`, the `yandexweb/`
  fixtures, both tests, and the 30/35 counts; §4.5 has `yandex-web` and `YandexWebService` with
  750 ms; new §4.7.5 documents the protocol, the browser UA as per-request emulation, the
  no-source/`targetLang=auto` rule, the 71-code whitelist and overrides, the 5000 UTF-16/750 ms
  assumptions, `SameOriginRedirectPolicy`, the array response, the body-first error table, the gate
  and the tests; §4.8 has the Yandex mapping and the `zh-Hant` limitation; §4.9 names the third
  opt-in consumer; §8 has both new rows and the updated `tst_languages`/`tst_settingsdialog`; §4.14
  says 35/27; §9.2 item 8 leaves only Bing/Reverso experimental.
- `doc/free-endpoints.md`: the status line, the §2 implementation note, the corrected `Limits`
  bullet (the Mozhi `engine=yandex` fallback is not this service), §8 items 2-4, and §9 item 4.
- `AGENTS.md`: 30/35.
- Verification: `ctest -N` reports 35; no stale 28/33 or "25 shared" strings remain in the updated
  sections; the relative links resolve; the historical §9.2 0.12.0 sentence and the §11.9 migration
  sentence are intentionally unchanged.
- Not performed: a live smoke (the endpoint may return a captcha/403); the Windows 30 is derived
  from the conditional test branches, not run.
