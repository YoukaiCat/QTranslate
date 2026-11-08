---
kind: task
status: in-progress
detailed: true
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

## Steps

1. Add the `QueuedWebService` base in `qtranslate_core`.
   - Create `src/services/queuedwebservice.h`: `class QueuedWebService : public TranslationService`
     with `Q_OBJECT`; a public nested `class Reply : public TranslationReply` with
     `using TranslationReply::TranslationReply;` and `virtual void start() = 0;`; a public
     `explicit QueuedWebService(QObject *parent = nullptr);`, `void setRateLimitMs(int ms);`,
     `int rateLimitMs() const;`, `void noteRequestSent();`; a protected `void enqueue(Reply *reply);`;
     and private `schedule()`/`startNext()` plus the members `QList<QPointer<Reply>> m_queue`,
     `QPointer<Reply> m_active`, `QElapsedTimer m_lastRequest`, `QTimer *m_timer = nullptr`, and
     `int m_rateLimitMs = 2000;`.
   - Create `src/services/queuedwebservice.cpp`: move `enqueue()`, `schedule()`, `startNext()`, and
     `noteRequestSent()` from `deeplwebservice.cpp:375-433` verbatim; the constructor creates the
     single-shot `m_timer` as a child of the service and connects `timeout` to `schedule()` (from
     `deeplwebservice.cpp:267-270`); `setRateLimitMs()` keeps `qMax(0, ms)`. `<QTimer>` is included
     in the `.cpp`, the header only forward-declares it.
   - Keep the base free of endpoints, retry delays, code conversion, settings, and the opt-in gate:
     it includes only `core/translationservice.h`, `QElapsedTimer`, `QList`, and `QPointer`. The
     2000 ms default is the moved DeepL value: the existing tests set the pause explicitly, so a
     missing constructor line would not be caught, and a later service overrides the value in its
     own constructor (MyMemory 350 ms, Yandex 750 ms, Reverso 600 ms).
   - Document the contract in the header: one instance per host, serialized replies, the pause is
     measured from the last network request reported by `noteRequestSent()`, a multi-chunk reply
     holds the slot and paces itself with `rateLimitMs()`, and `setRateLimitMs(0)` is for tests only.

2. Move `DeepLWebService` onto the base without changing its behaviour.
   - `src/services/deeplwebservice.h`: derive from `QueuedWebService` and include
     `services/queuedwebservice.h`; remove `<QElapsedTimer>`, `<QList>`, `<QPointer>`, the
     `class QTimer;` forward declaration, the `enqueue`/`schedule`/`startNext`/`noteRequestSent`
     declarations, `setRateLimitMs()`, and the members `m_rateLimitMs`, `m_queue`, `m_active`,
     `m_lastRequest`, and `m_timer`. Keep `m_endpoint`, `m_retryDelayMs`, `m_network`,
     `m_serviceId`, `m_englishVariant`, `setEndpoint()`, `setRetryDelayMs()`, `defaultEndpoint()`,
     and `toWebCode()`.
   - `src/services/deeplwebservice.cpp:57`: declare
     `class DeepLWebService::Reply : public QueuedWebService::Reply` and mark `void start() override`
     (`:77`); the reply constructor is unchanged because the base provides the default
     `TranslationReply` constructor, and the `~Reply()` override stays.
   - `src/services/deeplwebservice.cpp:262`: initialise `QueuedWebService(parent)` instead of
     `TranslationService(parent)` and drop the timer creation and connection (`:267-270`); delete
     `DeepLWebService::setRateLimitMs()` (`:342-345`) and the queue block (`:375-433`).
   - Do not touch `Reply::send()`: it calls `m_service->noteRequestSent()` before
     `m_network->nam()->post()` (`:137-139`), and the 429 path calls `send()` again (`:174-181`), so
     the pause is measured from the actual request, including the retry. Keep the endpoint, the
     retry delay, `toWebCode()`, and all parsing in `DeepLWebService`.
   - `DeepLService::setWebRateLimitMs()` (`deeplservice.cpp:393-397`) and `tst_deeplservice.cpp:194`
     must keep compiling against the inherited public `setRateLimitMs(int)`; do not change their
     signatures. `tst_deeplwebservice` needs no edit: it includes `services/deeplwebservice.h`,
     constructs the same `DeepLWebService`, and calls the inherited setter.

3. Register the new files and the test target.
   - `CMakeLists.txt:55-58`: add `src/services/queuedwebservice.cpp` and
     `src/services/queuedwebservice.h` to `qtranslate_core` after `openaicompatibleservice`, keeping
     the services block alphabetical.
   - `tests/CMakeLists.txt`: add `qtranslate_add_test(tst_queuedwebservice)` after the
     `tst_popupwindow` block (`:37-44`) and before `tst_settings` (`:45`). The target is
     platform-neutral and needs no extra sources, `Qt::Widgets`, or `QT_QPA_PLATFORM`.

4. Add `tests/tst_queuedwebservice.cpp`.
   - Includes: `core/translationtypes.h`, `services/queuedwebservice.h`, `QElapsedTimer`, `QTest`,
     `QTimer`, and `<memory>`; use `QTEST_GUILESS_MAIN`. No `QTcpServer`, no `NetworkManager`, and
     no `FakeHttpServer`.
   - Fake service: `class FakeQueuedService : public QueuedWebService` implements `id()`,
     `displayName()`, `isConfigured() == true`, empty language lists, a no-op `applySettings()`, and
     `translate()` creates a `FakeReply`, calls `enqueue()` (protected and accessible from the
     subclass), and returns it. Give it `nextChunks`/`nextHoldMs`/`ReplyStats *nextStats` fields and
     a shared `const QElapsedTimer *clock` so each test controls the reply it creates.
   - Fake reply: `class FakeReply : public QueuedWebService::Reply` with `start() override` that
     records the start time, calls `m_service->noteRequestSent()` before each simulated request, and
     finishes through `QTimer::singleShot(holdMs, this, ...)` with the reply as the context object,
     so deleting the reply cancels the timer. For `chunks > 1` it waits `m_service->rateLimitMs()`
     between chunks and calls `noteRequestSent()` again, modelling the multi-chunk contract. Record
     the start count and time, every note time, the finish time (before `finish()`), and the error
     in a `ReplyStats` owned by the test. Keep a `QPointer<QueuedWebService>` to the service.
   - Five slots, using `QTRY_VERIFY_WITH_TIMEOUT(..., 5000)` and the shared `QElapsedTimer`, with
     "at least" assertions and a ~10% tolerance like `tst_deeplwebservice:384`; no fixed long
     sleeps and no upper bounds:
     - `startsRepliesOneAtATime`: rate 0, the first reply holds ~60 ms; both start once, the second
       starts after (`>=`) the first finished, and both results are `None`.
     - `rateLimitDelaysTheNextReply`: rate 100 ms, the first holds ~10 ms; the second start is at
       least ~90 ms after the first reply's last note.
     - `abortedQueuedReplyIsSkipped`: with the first reply active, `abort()` the queued second one;
       its `result().error` is `Cancelled` and its start count stays 0; the first still finishes.
     - `deletingTheActiveReplyStartsTheNext`: delete the active reply (`unique_ptr::reset()`); the
       queued reply starts through the `destroyed` handler and finishes with `None`.
     - `noteRequestSentRestartsThePause`: a two-chunk reply with rate 100 ms and hold ~10 ms records
       two notes at least ~90 ms apart and holds the slot; a queued reply starts at least ~90 ms
       after the second note, never between the chunks.
   - Own every reply with `std::unique_ptr<TranslationReply>` and reset the service in `cleanup()`.
     Do not change `tst_deeplwebservice` and do not re-test the DeepL endpoint, its retry delay, or
     the parser.

5. Update the documentation.
   - `doc/architecture.md` §4.5, after the opt-in gate paragraph (`:659-670`): add a "Shared request
     queue" paragraph — `QueuedWebService` in `qtranslate_core`, the nested `Reply`, protected
     `enqueue()`, one active reply, the pause measured from `noteRequestSent()` (including retries
     and later chunks), a multi-chunk reply that holds the slot and paces itself with
     `rateLimitMs()`, the 2000 ms default, no endpoint/settings/gate/UI knowledge, and
     `DeepLWebService` as the first user.
   - `doc/architecture.md` §4.7.1 (`:817-820`): say that host requests are serialized by the shared
     `QueuedWebService` with the unchanged 2 s pause and that the 429 path keeps its 3 s retry; add
     `tst_queuedwebservice` to the tests line (`:823`).
   - `doc/architecture.md` §3.1 (`:206-213`, `:244-272`): add `queuedwebservice.h/.cpp` to
     `src/services/` and `tst_queuedwebservice.cpp` to `tests/`.
   - `doc/architecture.md` §3.2 (`:282-284`): add `queuedwebservice` to the `qtranslate_core`
     services list, add `tst_queuedwebservice` to the test list, and change the counts to
     "24 tests on Windows and 29 on Linux".
   - `doc/free-endpoints.md` §8 item 3 (`:125-126`): mark per-host rate limiting as implemented
     infrastructure with `QueuedWebService`, keeping the service-specific pauses.
   - Do not touch the READMEs, `doc/handoff.md`, the `.ts` files, or `tst_translations`: there is no
     new `tr()` string and no UI change.

6. Build, test, and report.
   - Native Linux (Astra 1.8.7, Qt 6.10.2, GCC 12): configure `build/linux-debug` with
     `cmake -S . -B build/linux-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug` and use the existing
     `build/linux-release`; build both and require zero `warning:` lines with `-Wall -Wextra`.
   - Run `tst_queuedwebservice`, `tst_deeplwebservice`, and `tst_deeplservice` individually in both
     configurations (`ctest --test-dir build/linux-debug -R <name> --output-on-failure`), then the
     full suite with `xvfb-run -a ctest ...`: 29/29.
   - Repeat in `qtranslate-trixie:latest` (Qt 6.8.2, GCC 14) with `build/trixie-debug` and
     `build/trixie-release`: the targeted tests and 29/29, no warnings. If the image or the container
     run is unavailable, record it; the native gate is mandatory.
   - Optional: a Clang 16 build in `build/linux-clang-debug` stays warning-free; not a gate.
   - Check the diff: only `src/services/queuedwebservice.*`, `src/services/deeplwebservice.h/.cpp`,
     `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/tst_queuedwebservice.cpp`, and the two docs
     change; the DeepL endpoint string, `m_retryDelayMs`, `toWebCode()`, and the parser are not in
     the diff.
   - Windows: not available (no MSVC/Qt); record its Debug/Release build and tests as not performed,
     and note that the platform-neutral target makes a Windows run 24, not 23. No live DeepL smoke:
     no endpoint or protocol changes and the test is local.

## Done when

- `QueuedWebService` exists under `src/services/`, is listed in `CMakeLists.txt` in
  `qtranslate_core`, and provides the nested `Reply`, `enqueue()`, `setRateLimitMs()`,
  `rateLimitMs()`, and `noteRequestSent()` contract described above; the 2000 ms default pause is
  inherited unchanged by `DeepLWebService`.
- `DeepLWebService` derives from `QueuedWebService`; its nested `Reply` derives from
  `QueuedWebService::Reply`; the queue members, `schedule()`, `startNext()`, `noteRequestSent()`, and
  the duplicated `setRateLimitMs()` are gone; the endpoint, retry delay, `toWebCode()`, and parsing
  are unchanged.
- `tst_deeplwebservice` stays green without changing its expectations, including serialization with
  a rate limit (`:363`), one retry after 429 (`:265`), giving up after two 429 (`:283`), 403
  (`:298`), and timeout (`:350`); only includes or constructor calls may be adapted to the class
  hierarchy.
- The new `tst_queuedwebservice` passes and covers ordering, the rate pause, aborting a queued reply,
  deleting the active reply, `noteRequestSent()` restarting the pause, and a multi-chunk reply that
  holds its slot and paces its chunks with `rateLimitMs()`; it uses no network.
- Debug and Release build without warnings on Linux; the full suite passes with the new test (the
  gate task adds no test target, so Linux grows from 28 to 29). Windows is not available here, so its
  build and tests are recorded as not performed; the new test is platform-neutral, so a Windows run
  would have 24 tests, not 23.
- `doc/architecture.md` §4.5/§4.7.1, its §3.1/§3.2 file and test lists (24 Windows / 29 Linux), and
  `doc/free-endpoints.md` §8 item 3 describe the shared queue and the unchanged 2 s DeepL pause; the
  READMEs and `doc/handoff.md` are unchanged.
- No new `tr()` string is added; the `.ts` files and `tst_translations` are unchanged.
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
  Windows build honestly; do not claim the 23 pre-existing Windows tests passed, and note that the
  new platform-neutral target would make a Windows run 24.
- The base's default pause is the moved 2000 ms so the accepted DeepL behaviour does not depend on a
  constructor line that the tests would not catch (they set the value explicitly). A later service
  must call `setRateLimitMs()` with its own host pause in its constructor; forgetting it silently
  gives that host a 2 s pause.
- The timing assertions compare recorded timestamps in the "at least" direction with a ~10% tolerance
  (like `tst_deeplwebservice:384`); never assert an upper bound or use fixed sleeps. The test must
  not depend on a real network, `QNetworkAccessManager`, or a display.
- No live smoke test is needed: the task adds no endpoint and the tests use a fake service. The
  DeepL live endpoint remains a manual smoke, not a CTest.
