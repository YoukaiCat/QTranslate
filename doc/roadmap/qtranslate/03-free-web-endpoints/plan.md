---
kind: stage
status: done
base: fee1bcf
---

# Free web endpoints (old stage 13)

## Goal

Add free, no-key web endpoints as optional services: MyMemory and Mozhi, plus Yandex web, Bing web, and Reverso where feasible. DeepL free web already exists. Official APIs remain the primary services; free endpoints are fallbacks and are marked as unofficial.

## What to do, what is touched

`TranslationService` implementations and response parsers; per-host rate limits; text splitting within provider limits; fallback from official API to free web to another service; fixture and local HTTP server tests plus manual smoke tests; privacy and instability warnings in settings and docs. The old plan's stage 13 and `doc/free-endpoints.md` list the details.

## Done when

- MyMemory and Mozhi work with rate limits.
- Yandex web, Bing web, and Reverso return clear errors.
- 429/403 responses are handled without hangs.
- Official Google/DeepL remain primary and free services are fallbacks.
- Tests and documentation are updated.

## Depends on

None technically; follows stage 2 in the old order and is independent. It may start earlier if Linux is blocked.

## Risks and what is needed from the user

Unofficial endpoints have no SLA, may change or block IPs, and may have terms that restrict automation. The user must choose which endpoints to include, accept the unofficial status and privacy warnings, and run or approve live smoke tests.

## Outcome

Accepted over `fee1bcf..HEAD`. All seven children are `done`: `01-free-web-infrastructure`, `02-mymemory`, `03-mozhi`, `04-yandex-web`, `05-bing-web`, `06-reverso` and `07-fallback-chain`. The whole range was reviewed as one unit and the `## Done when` criteria are met.

- **Rate limits:** MyMemory uses the shared `QueuedWebService` with a 350 ms pause and 500-byte UTF-8 chunks; Mozhi uses the same queue with 500 ms and 5000 UTF-16 units. One reply holds its queue slot, chunks are sequential, and `noteRequestSent()` paces every request, including retries. `tst_queuedwebservice`, `tst_mymemoryservice` and `tst_mozhiwebservice` cover ordering, pause, abort and deletion.
- **Clear errors:** Yandex web, Bing web and Reverso classify the body before the HTTP status and map rate limits/HTML blocks to `RateLimited`/`ServiceUnavailable` with fixed messages; malformed or missing fields are `BadResponse`; timeouts are `Timeout`. No message or log line contains the text, body, URL or token.
- **429/403:** All free services treat 403/429 and rate-limit bodies as `RateLimited` without a retry storm; Bing refreshes its token once on 401 and treats a second 401 or a 403 as a block; DeepL web keeps its one 429 retry. `tst_*webservice` and `tst_reversoservice` cover these paths.
- **Primary/fallback:** Google and DeepL remain the primary providers; the free services are opt-in and gated by `translation/allowFreeServices`. `TranslationManager` tries one configured opt-in fallback exactly once on `Auth`, `QuotaExceeded`, `RateLimited`, `ServiceUnavailable`, `Network` or `Timeout`; a failed fallback returns the primary error; the fallback is never retried and there is no second hop. The setting is `translation/fallbackService`, default `None`, and `AppController` passes the registered opt-in services to the dialog.
- **Tests/docs:** Linux Debug/Release build without warnings and the full suite passes 39/39 (31 shared, 8 Linux-only). `tst_translationmanager` is 50 cases. Windows Debug/Release and its 34 tests were **not performed** (no MSVC on this Linux machine). A live smoke was **not performed**: no interactive GUI and no real provider were available; recorded as not performed, not passed. `doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md`, both READMEs and `doc/handoff.md` are updated.
- **Acceptance fix:** one real defect was found in the whole-range review — the cache-generation race in `TranslationManager` (an already-running reply could store a result after `clearCache()` and serve it under a new instance/engine/mode). Fixed with a generation counter captured by `Reply` and checked in `finishResult()`, a cache clear for `google.mode`/`deepl.mode`/`deepl.englishVariant` in `AppController::applySettings()`, and `tst_translationmanager::resultArrivingAfterClearIsNotCached()`.
- No order/scope violation was found: the change set covers only the stage's code, tests, `.ts` files, docs, `AGENTS.md`, both READMEs and the lead-owned map/handoff files. No push was made.
