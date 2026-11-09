---
kind: task
status: done
detailed: true
---

# Manager fallback policy

## Task

Teach `TranslationManager` to retry a failed primary request exactly once on a configured opt-in
free service. This task is the first child of `07-fallback-chain`: it implements the manager-level
policy and its test matrix only. The `translation/fallbackService` key, the settings-dialog combo,
the translations and the documentation are `02-fallback-settings-and-ui` and `03-fallback-docs`.

The fallback is deliberately one hop, not an arbitrary list. It covers "official API → free
service": the text goes to the active provider and, on a retryable failure, to exactly one
configured fallback. There is no second hop, no loop, and no code inside the services. Official
Google/DeepL stay primary; the fallback is opt-in, and disabling the free-services gate disables it
even if a fallback id is still configured.

`TranslationManager` currently owns the active service, the LRU cache (200 entries), the
`freeServicesAllowed()` gate and the per-channel cancellation (`src/core/translationmanager.h:31-64`,
`src/core/translationmanager.cpp:26-120`, `:184-227`). The gated refusal is at
`translationmanager.cpp:200-203`; the pre-request `Auth` path is at `:204-210`; the cache is at
`:211-222` and `:247-281`.

## Steps

1. **Add the fallback state and API.** In `src/core/translationmanager.h`, after the
   `freeServicesAllowed()` block (`:42-45`), declare:

   ```cpp
   // A service that is tried once when the active service fails with a
   // retryable error. Empty means no fallback. The id is resolved at request
   // time; an unknown or non-opt-in service is ignored. Changing it clears the
   // cache.
   QString fallbackService() const { return m_fallbackService; }
   void setFallbackService(const QString &id);
   ```

   Add `QString m_fallbackService;` next to `m_freeServicesAllowed`, and a private
   `TranslationService *fallbackFor(TranslationService *primary) const;`. Move
   `setFreeServicesAllowed()` from the inline body to `translationmanager.cpp` (keep the public
   signature) so it can clear the cache; do not change its comment. Keep the setter free of
   settings knowledge: it trims the id, stores it, and does not validate it against the registry.

2. **Implement the setters and the resolver.** In `src/core/translationmanager.cpp`:

   - `setFallbackService(const QString &id)`: `const QString normalized = id.trimmed().toLower();`
     if it equals `m_fallbackService`, return; store it and call `clearCache()` (the language setters
     use the same pattern at `:168-182`). Lowercasing here matches the service ids and the settings
     normalization; `fallbackService()` returns the normalized value.
   - `setFreeServicesAllowed(bool allowed)`: if the value is unchanged, return; store it; if it
     changed from `true` to `false`, call `clearCache()`, so a cached fallback result cannot be
     served after the text was disabled for free services. Turning it on needs no clear.
   - `fallbackFor(TranslationService *primary) const`: return `nullptr` when
     `m_fallbackService.isEmpty()`, `!m_freeServicesAllowed`, the id is not registered
     (`service(id)`), the resolved service is `primary` (compare the pointer and the id), or the
     service does not `requiresOptIn()`. Otherwise return the service. Resolve at failure time, not
     at request start, so turning the gate off or changing the fallback while the primary is in
     flight is honoured.
   - Keep the `qtranslate.translation` logging category (`:8`). Add a file-local
     `bool isRetryable(TranslationError)` with exactly `Auth`, `QuotaExceeded`, `RateLimited`,
     `ServiceUnavailable`, `Network`, `Timeout` returning true, and a fixed
     `QString errorName(TranslationError)` (`auth`, `quota`, `rate-limited`, `unavailable`,
     `network`, `timeout`, `cancelled`, `bad-response`, `too-long`, `unsupported-language`,
     `empty-text`, `none`) for the log lines. Do not use user text, URLs or response bodies.

3. **Refactor `TranslationManager::Reply` to dispatch the primary result.** Keep the existing
   smart-retry and cache behaviour (`translationmanager.cpp:26-120`), but split
   `onInnerFinished()` into: read the inner result and `deleteLater()` (unchanged); if
   `m_smartRetry`, finish as today; on a non-ok result call the new `onPrimaryError(result)`; on a
   success run the existing smart-retry condition and `finishResult(result)` as today. Add:

   - `void startWithError(const TranslationResult &result)` for the pre-request paths. It must
     defer through `QMetaObject::invokeMethod(this, [this, result] { if (!isFinished())
     onPrimaryError(result); }, Qt::QueuedConnection)`, so `finished()` is never emitted from inside
     `translate()` (the same reason `TranslationReply::createFinished()` defers,
     `src/core/translationservice.cpp:10-17`). A caller that deletes or aborts the reply before the
     queued call is a no-op, like `earlyErrorCanBeCancelled()` already relies on.
   - `void onPrimaryError(const TranslationResult &result)`: if `m_fallbackStarted` is false, the
     error is retryable and `m_manager->fallbackFor(m_service)` returns a service, then store
     `m_primaryError = result`, set `m_fallbackStarted = true`, log
     `Primary service <id> failed (<error kind>); trying fallback <id>`, call
     `fallback->translate(m_request)`, parent the reply to `this`, connect its `finished` to
     `onFallbackFinished()`, and return. If `translate()` returns `nullptr`, log the failure and
     `finishResult(m_primaryError)` after making sure `m_primaryError` is set (a `nullptr` from the
     fallback is a defect, not a retryable error). Otherwise finish with `result`.
   - `void onFallbackFinished()`: take `m_inner`, null it, return early if it is missing or the outer
     reply is finished, read the result and `deleteLater()` the inner reply. On success log
     `Fallback <id> succeeded` and call `finishResult(result)`; the result keeps the fallback's
     `serviceId`, and `finishResult()` stores it under the normal `m_cacheKey` (the primary key).
     On failure log `Fallback <id> failed (<error kind>)` (do not log `errorMessage` or any text) and
     `finishResult(m_primaryError)`. Do not call `onPrimaryError()` again: the one-hop rule is
     enforced by `m_fallbackStarted` and by this branch.
   - Keep `abortImpl()` as it is (`:42-51`): `m_inner` is now either the primary or the fallback
     reply, so cancelling the outer reply aborts whichever request is running.
   - Do not apply the smart secondary-language retry to the fallback: the fallback is attempted once
     and its successful result is returned as-is, even when `detectedSourceLang` equals the primary
     language. Record this in a comment: one failure is one fallback request, not two.

4. **Route the pre-request `Auth` path through the managed reply.** In `translate()`
   (`translationmanager.cpp:184-227`), replace the `!m_active->isConfigured()` branch
   (`:204-210`) with the same message construction plus a `Reply` that starts with the error:

   ```cpp
   TranslationResult result;
   result.serviceId = serviceId;
   result.error = TranslationError::Auth;
   result.errorMessage = hint.isEmpty()
       ? QStringLiteral("%1 needs an API key").arg(m_active->displayName())
       : hint;
   auto *managed = new Reply(this, m_active, request, cacheKey(serviceId, request));
   reply = managed;
   current(channel) = reply;
   managed->startWithError(result);
   return reply;
   ```

   Keep the check before the cache lookup, as today: an unconfigured primary never serves a cached
   result. The other pre-checks (`EmptyText`, `TooLong`, the gate refusal) stay
   `TranslationReply::createFinished()` and never fall back: `EmptyText`/`TooLong` are excluded by
   policy and the gate refusal means no fallback is allowed. Keep `serviceId` on every early error.

5. **Extend the fakes in `tests/tst_translationmanager.cpp`.** The test file already has
   `FakeReply`/`FakeService` (`:15-79`) and a `request()` helper (`:81-84`). Make the fake reply carry
   the emitting service's id instead of the hardcoded `"fake"`: give `FakeReply` a `QString
   serviceId` (constructor or a `complete(text, serviceId)` overload) and have
   `FakeService::translate()` set it from `m_id`, so a fallback result can be asserted to carry the
   fallback's `serviceId`. Keep the default fake id `"fake"` so the existing
   `checksBeforeRequest()` expectation (`:258`) and the other cases stay valid. Add a small helper
   that registers a primary (non-opt-in, configured by default) and a fallback (`optIn = true`),
   sets `manager.setFallbackService(fallback->id())` and `manager.setFreeServicesAllowed(true)`, and
   returns the pair. The file is `QTEST_GUILESS_MAIN` (`:488`), so no widgets or event loop setup is
   needed beyond `QSignalSpy::wait`.

6. **Add the trigger and non-trigger matrix.** In `tests/tst_translationmanager.cpp`, after the
   existing cache tests, add:

   - `fallbackOnRetryableErrors_data()`/`fallbackOnRetryableErrors()`: six rows, one for each
     `Auth`, `QuotaExceeded`, `RateLimited`, `ServiceUnavailable`, `Network`, `Timeout`. Complete the
     primary reply with the error, assert the fallback service received exactly one request, the
     outer result is ok, `translatedText` is the fallback text and `serviceId` is the fallback id;
     the primary received one request.
   - `noFallbackOnNonRetryableErrors_data()`/`noFallbackOnNonRetryableErrors()`: rows for a
     successful primary result (no fallback, the primary result is returned), `EmptyText` and
     `TooLong` pre-checks (zero primary requests, zero fallback requests), `UnsupportedLanguage`,
     `Cancelled` and `BadResponse` from the primary reply (the fallback received no request and the
     primary result is returned). Use `QSignalSpy::wait` for the pre-check rows.
   - `fallbackOnUnconfiguredPrimary()`: primary `configured = false`, fallback configured; the
     manager must not call the primary's `translate()`, must call the fallback once, and must return
     the fallback result; wait for the fallback request with
     `QTRY_COMPARE(fallback->requests.size(), 1)` because the pre-request `Auth` path is deferred,
     then complete it and assert `finished()` arrives after the call (the path is not synchronous).
     This pins the pre-request `Auth` path from step 4.
   - `fallbackIsIgnoredWhenInvalid_data()`/`fallbackIsIgnoredWhenInvalid()`: rows for an empty id, an
     unregistered id, a registered non-opt-in service (the primary itself or a second regular fake),
     and `fallbackService = primary->id()`. With a `Network` primary failure, the fallback must not
     run and the result must be the primary error.
   - `fallbackRequiresTheGate()`: configure the fallback, leave the gate off, fail the primary with
     `Network`; the fallback must not run and the primary error is returned. Then turn the gate on,
     make a new request and fail the primary again; the fallback runs. Also turn the gate off while
     the primary request is in flight, then fail it: the resolver must see the gate off and not fall
     back.

7. **Add the failure, abort, cache, gate and no-loop cases.** In the same file:

   - `fallbackFailureReturnsPrimaryError()`: primary `Network` with message `offline`, fallback
     `Auth`/`BadResponse` with its own message; the outer result must be the primary error and
     `serviceId` the primary id, the fallback message must not replace it, and the fallback was
     called once.
   - `abortDuringFallback()`: fail the primary, leave the fallback reply unfinished, call
     `manager.cancel(Channel::MainWindow)`; the outer reply is `Cancelled`, the fallback fake's
     `abortCount` is 1, and the fallback result is not cached (a later identical request calls the
     primary again).
   - `fallbackResultIsCached()`: complete the fallback successfully, then repeat the same request;
     the second reply is served from the cache, neither service receives a new request, and the
     result keeps the fallback `serviceId`.
   - `changingFallbackClearsCache()`: with a cached result, call
     `manager.setFallbackService(other->id())`; the next identical request calls the primary again
     (a cache miss). Also assert `fallbackService()` returns the trimmed value and that setting the
     same value again does not clear the cache.
   - `disablingTheGateClearsTheFallbackCache()`: cache a fallback result, call
     `manager.setFreeServicesAllowed(false)`, repeat the request; the primary is called again (the
     cache was cleared) and no fallback runs.
   - `noSecondHopOrSmartRetry()`: the fallback fails with `Network`; the primary and the fallback
     were each called exactly once, no third service is involved, and the result is the primary
     error. Separately, with `primaryLanguage = "ru"`, `secondaryLanguage = "en"`, `sourceLang =
     "auto"` and a fallback success whose `detectedSourceLang` is `"ru"`, assert the fallback was
     called once (no secondary-language retry) and the fallback text is returned.

8. **Add the log-hygiene case.** Add `logIsClean()` using the `qInstallMessageHandler` pattern from
   `tests/tst_bingwebservice.cpp`: configure the fallback, use a text containing `TOPSECRET`, fail
   the primary with `Network` and the fallback with `BadResponse`; capture the warnings and assert
   they name the service ids and the error kinds but contain neither `TOPSECRET` nor a URL, a
   response body or the cache key. Restore the previous handler at the end. The manager must log
   through `qCWarning(lcTranslation)` so the line is visible by default; never log `errorMessage`,
   `m_request.text` or the cache key.

9. **Build, run the targeted tests and verify the scope.** Configure and build Linux Debug and
   Release with `-Wall -Wextra` (`cmake -S . -B build/linux-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug`
   and `build/linux-release`/`Release`), then run
   `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`. Run
   `tst_translationmanager` on its own in both configurations, and keep `tst_settings`,
   `tst_settingsdialog` and `tst_translations` green (they do not change in this task). `ctest -N`
   must stay at 39 Linux tests; Windows would stay at 34. `git diff --stat` may show only
   `src/core/translationmanager.h`, `src/core/translationmanager.cpp` and
   `tests/tst_translationmanager.cpp`. Do not touch `src/core/settings.*`, `src/ui/*`,
   `src/app/*`, the 14 `.ts` files or `doc/`; those belong to `02-fallback-settings-and-ui` and
   `03-fallback-docs`. Windows Debug/Release and its 34 tests are recorded as not performed (no
   MSVC); commit and push stay with the coordinator/lead.

## Done when

- `TranslationManager` exposes `fallbackService()`/`setFallbackService()`; empty means no fallback;
  the setter trims and clears the cache only when the value changes; `setFreeServicesAllowed(false)`
  clears the cache; an unknown, non-opt-in or self fallback is ignored at request time.
- The manager tries the configured opt-in fallback exactly once on `Auth`, `QuotaExceeded`,
  `RateLimited`, `ServiceUnavailable`, `Network` and `Timeout`, including the pre-request `Auth` path
  when the primary is not configured; it does not fall back on success, `EmptyText`, `TooLong`,
  `UnsupportedLanguage`, `Cancelled` or `BadResponse`.
- A successful fallback returns its translation with the fallback's `serviceId`, caches it under the
  primary cache key and logs the fallback use without the text; a failed fallback returns the primary
  error with the primary `serviceId`, logs the fallback error kind and cannot loop or take a second
  hop. The fallback does not get the smart secondary-language retry.
- Cancelling while the fallback runs aborts the fallback request and returns `Cancelled`; changing
  the fallback or turning the gate off clears the cache, so the next request is a primary request
  again.
- `tst_translationmanager` covers the trigger matrix, the non-trigger rows, the pre-request `Auth`
  path, the invalid-fallback rows, the fallback-failure path, abort, both cache clears, the no-loop
  rule, the no-smart-retry rule and the log hygiene; all existing cases stay green.
- Linux Debug and Release build without warnings and the full suite passes 39/39; Windows
  Debug/Release and its 34 tests are recorded as not performed (no MSVC).
- Only `src/core/translationmanager.h`, `src/core/translationmanager.cpp` and
  `tests/tst_translationmanager.cpp` are changed; no settings, UI, app, `.ts` or `doc/` file is
  touched; commit and push are the coordinator/lead's.

## Risks

- The pre-request `Auth` path is not a service reply. Returning it through
  `TranslationReply::createFinished()` would skip the fallback and silently break "DeepL without a
  key → free service". Route it through the managed `Reply` and keep `startWithError()` deferred, so
  the "`finished()` is never emitted from inside `translate()`" contract still holds.
- Resolve the fallback at failure time, not at request start. Otherwise turning the gate off or
  changing the fallback while the primary is in flight would still send the text to the old
  fallback.
- `BadResponse`, `UnsupportedLanguage` and `TooLong` are excluded deliberately; do not add them
  "for robustness" without a new decision. `Cancelled` must never trigger a fallback.
- Cache the fallback result under the primary cache key (the normal key) and clear the cache on a
  fallback change and on a gate-off. Otherwise a cached primary result hides the new fallback, and a
  cached fallback result survives disabling.
- The log must not contain the text, a URL, a response body or the cache key. Log only the service
  ids and the fixed error kind; do not log `errorMessage`, even though most services guarantee a
  fixed message.
- The fake reply currently hardcodes `serviceId = "fake"`; without making it carry the emitting
  service id, the "fallback result keeps its own `serviceId`" assertion would be meaningless or
  wrong. Keep the default id `"fake"` to avoid churning the existing cases.
- The test file is the context risk. Keep the helper small and the cases independent; if the matrix
  grows past a manageable size, report `DOES NOT FIT` instead of dropping the pre-request `Auth`,
  abort, cache or log-hygiene cases.
- Windows Debug/Release and its 34 tests are unavailable on this Linux machine; record them as not
  performed, not as passed. Do not start a release or push.

## Outcome

Done. `TranslationManager` implements the manager-level one-hop fallback and
`tst_translationmanager` covers the policy; only `src/core/translationmanager.h`,
`src/core/translationmanager.cpp` and `tests/tst_translationmanager.cpp` changed. No settings, UI,
app, `.ts`, `doc/`, CMake or other test file was touched; no new test target. Commit and push stayed
with the coordinator/lead.

- API: `fallbackService()` returns the normalized id, `setFallbackService()` trims and lowercases
  it and clears the cache only when the value changes. `setFreeServicesAllowed()` moved from the
  header into the `.cpp` with the same signature and clears the cache when the gate goes off.
  `fallbackFor()` resolves at failure time and returns `nullptr` for an empty id, the gate off, an
  unregistered id, the reply's primary service (pointer and id) and a service without
  `requiresOptIn()`. The setter keeps no settings knowledge.
- Reply policy: `Reply` gained `startWithError()` (a deferred `onPrimaryError()` so `finished()` is
  never emitted inside `translate()`), `onPrimaryError()` (retryable errors only, `m_fallbackStarted`
  blocks a second hop, a `nullptr` from the fallback finishes with the primary error) and
  `onFallbackFinished()`. A successful fallback result keeps its own `serviceId`, is cached under the
  primary `m_cacheKey` and is logged as `Fallback <id> succeeded`; a failed fallback logs the error
  kind and returns `m_primaryError`. `abortImpl()` is unchanged and aborts whichever inner reply is
  running. The fallback never gets the smart secondary-language retry.
- Pre-request `Auth`: an unconfigured primary now goes through the managed `Reply` with the same
  message as before, so "DeepL without a key → free service" works; `EmptyText`, `TooLong` and the
  gate refusal stay finished immediately and never fall back.
- Tests: `FakeReply` carries its service id (default still `"fake"`), and a `withFallback()` helper
  registers a configured primary plus an opt-in fallback. New cases: the six retryable triggers;
  success/`EmptyText`/`TooLong`/`UnsupportedLanguage`/`Cancelled`/`BadResponse` non-triggers; the
  pre-request `Auth` path (deferred, no primary request); the invalid rows (empty, unregistered,
  primary itself, non-opt-in); the gate including a gate-off while the primary is in flight; fallback
  failure returning the primary error and message; abort during the fallback (abort count 1, nothing
  cached); the fallback result cached under the primary key; the trimmed id and both cache clears; the
  no-second-hop and no-smart-retry rules; and `logIsClean()` with `TOPSECRET` in the text, the primary
  message, the fallback message and a URL. `tst_translationmanager` is 49 cases.
- Verification: Linux Debug and Release build with zero warnings; the full suite passes 39/39 in both
  configurations and `ctest -N` stays 39; `tst_translationmanager` is 49/49 in both, repeated 20
  times; `tst_settings`, `tst_settingsdialog` and `tst_translations` stay green. The review repeated
  the full suite four times and ran the manager test under valgrind (no invalid access, no definite
  leak). Windows Debug/Release and its 34 tests were **not performed** (no MSVC on this machine), not
  passed. One `tst_cli` run failed once and passed in all repeats; it is a known local flake unrelated
  to the change.
- Review: no accepted findings. One real but deferred issue: clearing the cache when the gate or the
  fallback changes does not stop an already running fallback, so its successful result can be cached
  after `clearCache()` and served by the next request while the gate is off. This is the cache
  generation defect already recorded in `doc/roadmap/qtranslate/journal.md` (it also affects language
  and service-mode changes); it needs the generation counter in `TranslationManager` plus a test, not
  a local patch here. No other real problem was left unfixed. Next: `02-fallback-settings-and-ui`
  (the INI key, dialog, `AppController`, `.ts`) and then `03-fallback-docs`; the READMEs,
  `doc/handoff.md`, root plan and stage acceptance stay with the lead.
