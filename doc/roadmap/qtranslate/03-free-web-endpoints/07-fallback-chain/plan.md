---
kind: task
status: todo
depends: [01-free-web-infrastructure, 02-mymemory, 03-mozhi, 04-yandex-web, 05-bing-web, 06-reverso]
---

# Fallback chain: official first, free service second

## Task

Connect the free services to the official ones so a failed official request can be retried once on a
free fallback, and finish the stage: the user-facing provider documentation, the architecture
tables, and the final test gate. The stage requires that "official Google/DeepL remain primary and
free services are fallbacks"; until this task the free services are separate opt-in choices only.

The fallback is a manager-level policy, not code inside each service. `TranslationManager` gets a
configured fallback service id (`translation/fallbackService`, empty by default) and a
`setFallbackService(const QString &id)`/`fallbackService()` pair. The settings dialog adds a
**Fallback service** combo after the free-services group, with `None` plus every registered service
whose `requiresOptIn()` is true; the combo is disabled while the free-services gate is off. The
fallback is attempted only when the user configured one and the free-services gate is on. The
manager tries it exactly once, only for `Auth`, `QuotaExceeded`, `RateLimited`, `ServiceUnavailable`,
`Network`, and `Timeout` from the primary service (including the pre-request `Auth` path when the
active service is not configured). It does not fall back on success, `EmptyText`, `TooLong`,
`UnsupportedLanguage`, `Cancelled`, or `BadResponse`: those are either the user's input or a defect
that a different provider would hide. There is no second hop and no loop; if the fallback itself
fails, the caller receives the **primary** error, and the fallback error is logged without text.
The free service's result is returned with its own `serviceId`; the manager caches it under the
normal key and clears the cache when the fallback setting changes, because a different fallback can
produce a different result.

The fallback shape is deliberately one hop, not an arbitrary list: it covers "official API → free
service", and Mozhi itself proxies to several engines, so a user who wants the "free web → Mozhi"
step can configure Mozhi. Google and DeepL already have their own explicit free modes; those are
modes of the same service, chosen by the user, and are not a second automatic hop. This keeps the
privacy story simple: the text goes to the active provider, and on a retryable failure to exactly one
configured fallback. Both are named in the warning.

Tests extend `tests/tst_translationmanager.cpp` with two fake services and cover every trigger, every
non-trigger, the pre-request `Auth` path, the fallback-failure path, a missing/disabled fallback, a
fallback equal to the active service, abort while the fallback is running, cache clearing on a
fallback change, and the no-loop guarantee. `tests/tst_settingsdialog.cpp` covers the combo and its
disabled state. No automated test contacts a real provider; the manual smoke uses a deliberately
invalid official key and records the result.

## Done when

- `translation/fallbackService` is loaded/saved/normalized (empty means no fallback); the settings
  dialog has a `fallbackService` combo with `None` and the registered opt-in services, disabled while
  `translation/allowFreeServices` is off; `tst_settings` and `tst_settingsdialog` cover the default,
  the round trip, and the disabled state.
- The manager tries the configured fallback exactly once on `Auth`, `QuotaExceeded`, `RateLimited`,
  `ServiceUnavailable`, `Network`, and `Timeout`, including a primary service that is not configured
  before any request; it does not try it on success, `EmptyText`, `TooLong`, `UnsupportedLanguage`,
  `Cancelled`, or `BadResponse`, and it never tries the active service as its own fallback.
- A successful fallback returns its translation with the fallback's `serviceId`, caches it normally,
  and logs that the fallback was used without the text; a failed fallback returns the primary error
  and logs the fallback error; no loop or repeated fallback is possible.
- Cancelling a request while the fallback runs aborts the fallback request and returns `Cancelled`;
  changing `fallbackService` clears the cache; changing the gate off while a fallback is configured
  disables it and the next request uses the primary only.
- The privacy warning states that a retryable failure sends the text to the configured fallback, and
  the docs explain that the fallback is opt-in, one hop, and never the only translation method.
- `tst_translationmanager` covers all trigger/non-trigger cases, the pre-request `Auth` path, the
  fallback-failure path, abort, cache clearing, and no-loop; the full suite passes in Debug and
  Release without warnings.
- `doc/architecture.md` §4.9 and §4.11 (key table) describe the fallback; `doc/free-endpoints.md`
  §8/§9 marks the fallback and the service statuses; both READMEs have a providers/fallback section
  that lists Google, DeepL, OpenRouter/Local LLM as primary and MyMemory, Mozhi, Yandex web, Bing web,
  and Reverso as opt-in free fallbacks (MyMemory is a free API; the others are unofficial), with the
  privacy and instability warning.
- A manual smoke is recorded: with an intentionally invalid official key (or an unconfigured official
  service), the fallback translates a short text and the log/UI shows the fallback service; if the
  network or the free endpoint is unavailable, the check is recorded as not performed with the reason.
- The stage's `## Done when` is traced in the outcome: MyMemory and Mozhi work with rate limits;
  Yandex/Bing/Reverso return clear errors; 429/403 are clear and do not hang; official services stay
  primary; tests and documentation are updated.

## Risks

- Automatic fallback changes where the text is sent. Keep it opt-in, default `None`, one hop, and
  visible in the warning; never fall back when the free-services gate is off, even if the INI still
  names a fallback.
- Returning the primary error when the fallback also fails keeps the official service's reason
  visible, but the fallback error must still be logged for diagnosis. Do not concatenate raw response
  bodies or the text into the message.
- The pre-request `Auth` path is not a service reply; it is easy to return it before the fallback can
  run. Implement it through the same managed `Reply` or a dedicated `startWithError` path and test it,
  otherwise "DeepL without a key → free service" silently does not work.
- The fallback must not mask programming errors. `BadResponse`, `UnsupportedLanguage`, and `TooLong`
  are excluded deliberately; do not add them "for robustness" without a new decision.
- The combo lists only opt-in services, but the INI can contain any id. Normalize unknown ids to
  empty and ignore an id that is not registered, so a stale setting cannot break startup.
- `clearCache()` on a fallback change is required; otherwise a cached primary result hides the new
  fallback and a cached fallback result survives disabling.
- The final documentation and test gate belongs here; the lead owns `doc/handoff.md` and the release
  decision. Do not start a release or push.
