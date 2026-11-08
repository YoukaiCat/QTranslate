---
kind: task
status: todo
---

# Queued web service base

## Task

Extract the serialized per-host queue from the accepted `DeepLWebService` into a reusable
`QueuedWebService` base so MyMemory, Mozhi, Yandex, Bing, and Reverso do not copy the same queue and
rate-pause code. This is a behaviour-preserving refactor of the DeepL free-web path, not a new
service and not a change to the opt-in gate; `tst_deeplwebservice` is the regression gate.

The base lives in `src/services/queuedwebservice.h/.cpp` and derives from `TranslationService`. It
has a nested abstract `Reply : public TranslationReply` that declares `virtual void start() = 0;`
and public `void setRateLimitMs(int ms)`, `int rateLimitMs() const`, and
`void noteRequestSent()`; the latter is called by replies before every network request, including
retries and subsequent chunks. `enqueue(Reply *reply)` is protected. The queue code moves verbatim
from `DeepLWebService` (`deeplwebservice.h:43-60`, `deeplwebservice.cpp:375-433`):
`QList<QPointer<Reply>> m_queue`, `QPointer<Reply> m_active`, `QElapsedTimer m_lastRequest`, a
single-shot `QTimer m_timer`, `schedule()`, `startNext()`, and the `finished`/`destroyed`
connections that clear `m_active` and reschedule. A reply that is finished while queued is skipped;
a reply deleted while active is cleaned up by `QPointer`/the `destroyed` handler; the timer is a
child of the service, so no callback runs after destruction.

The contract is: one service instance per host, requests are serialized, the pause is measured from
the last *network* request (`noteRequestSent()`), and a reply holds the slot until it finishes, so a
multi-chunk protocol sends its chunks sequentially inside one slot and must pace itself with
`rateLimitMs()`. `setRateLimitMs(0)` disables the pause for tests only. `DeepLWebService` derives
from the base, its nested `Reply` derives from `QueuedWebService::Reply` and keeps its `start()` as
`override`, and the duplicate queue members/methods and `setRateLimitMs()` are removed. The endpoint,
the retry delay, `toWebCode()`, and all parsing stay in `DeepLWebService`; `DeepLService` calls
`m_web->setRateLimitMs()` in tests, so the inherited signature must stay identical.

`CMakeLists.txt` adds `src/services/queuedwebservice.cpp/.h` to `qtranslate_core`;
`tests/CMakeLists.txt` adds `qtranslate_add_test(tst_queuedwebservice)`. The new
`tests/tst_queuedwebservice.cpp` uses a fake service/reply and covers: two replies start one at a
time, a 100 ms rate limit delays the second start by at least the remaining pause, a reply aborted
while queued is skipped (its `finished` arrives with `Cancelled`), deleting the active reply starts
the next one, and `noteRequestSent()` restarts the pause. Use `QTRY_VERIFY_WITH_TIMEOUT` and
`QElapsedTimer`; no network and no fixed long sleeps. `doc/architecture.md` §4.5 gets the shared-queue
paragraph and §4.7.1 says that `DeepLWebService` uses the shared `QueuedWebService` queue with the
2 s pause unchanged; `doc/free-endpoints.md` §8 item 3 (per-host rate limiting) is marked as
implemented infrastructure. No README or `doc/handoff.md` change.

## Done when

- `QueuedWebService` exists under `src/services/`, is listed in `CMakeLists.txt` in
  `qtranslate_core`, and provides the nested `Reply`, `enqueue()`, `setRateLimitMs()`,
  `rateLimitMs()`, and `noteRequestSent()` contract described above.
- `DeepLWebService` derives from `QueuedWebService`; its nested `Reply` derives from
  `QueuedWebService::Reply`; the queue members, `schedule()`, `startNext()`, `noteRequestSent()`, and
  the duplicated `setRateLimitMs()` are gone; the endpoint, retry delay, `toWebCode()`, and parsing
  are unchanged.
- `tst_deeplwebservice` stays green without changing its expectations, including serialization with
  a rate limit (`:363`), one retry after 429 (`:265`), giving up after two 429 (`:283`), 403
  (`:298`), and timeout (`:350`); only includes or constructor calls may be adapted to the class
  hierarchy.
- The new `tst_queuedwebservice` passes and covers ordering, the rate pause, aborting a queued reply,
  deleting the active reply, and `noteRequestSent()`; it uses no network.
- Debug and Release build without warnings on Linux; the full suite passes with the new test (the
  gate task adds no test target, so Linux grows from 28 to 29); the Windows build and the 23 Windows
  tests are recorded as not performed.
- `doc/architecture.md` §4.5/§4.7.1 and `doc/free-endpoints.md` §8 item 3 describe the shared queue
  and the unchanged 2 s DeepL pause; the READMEs and `doc/handoff.md` are unchanged.
- The gate setting, `TranslationError::ServiceUnavailable`, the settings UI, and `AppController` are
  untouched by this task; no service is added and no endpoint is enabled.

## Risks

- The refactor touches the accepted DeepL free-web path. Move the queue code verbatim, keep the
  `QPointer`/`destroyed` cleanup and the `m_active == reply` checks, and use
  `tst_deeplwebservice` as the regression gate. Do not move DeepL's retry delay, endpoint, or code
  conversion into the base: they are service-specific.
- `noteRequestSent()` is called by replies and by tests; making it public is a small API concession.
  Do not let a reply call it before the actual request is handed to `QNetworkAccessManager`, or the
  pause will be measured from the wrong moment.
- A multi-chunk reply holds the queue slot until it finishes. It must pace its own chunks with
  `rateLimitMs()` and call `noteRequestSent()` before each request; do not enqueue each chunk as a
  separate top-level reply, or cancellation and the per-request result assembly become inconsistent.
- The base must not depend on the gate or on any service settings; it only serializes replies and
  tracks the last request. Keep it in `qtranslate_core` and do not link it to UI code.
- Windows is not available here. Keep the code portable (no platform calls) and record the missing
  Windows build honestly; do not claim the 23 Windows tests passed.
- No live smoke test is needed: the task adds no endpoint and the tests use a fake service. The
  DeepL live endpoint remains a manual smoke, not a CTest.
