---
kind: stage
status: in-progress
base: 8d0efd7
depends: [01-free-web-infrastructure, 02-mymemory, 03-mozhi, 04-yandex-web, 05-bing-web, 06-reverso]
---

# Fallback chain: official first, free service second

## Goal

Connect the free services to the official ones so a failed official request is retried exactly once
on a configured opt-in free fallback, and finish the stage: the `translation/fallbackService`
setting and its settings-dialog combo, the manager policy, the tests, and the user-facing provider
documentation. Official Google/DeepL stay primary; the fallback is opt-in, disabled with the
free-services gate, never loops, and never hides the primary error.

The original node was a single task. It is split here into three children because the manager policy
with its trigger/non-trigger matrix, the settings key and dialog with the 14 `.ts` files, and the
documentation each have their own acceptance and can be reviewed independently.

## What to do, what is touched

- `01-fallback-manager` (task, detailed): the `TranslationManager` fallback API and the one-hop
  `Reply` policy, covered by an extended `tst_translationmanager`. No settings key, UI,
  translations or docs.
- `02-fallback-settings-and-ui` (task): `translation/fallbackService` in `AppSettings`/
  `SettingsStore`, the gate-aware **Fallback service** combo in `SettingsDialog`, the `AppController`
  wiring, `tst_settings`/`tst_settingsdialog`, the 14 `.ts` files, and the live smoke. No service
  policy change.
- `03-fallback-docs` (task): `doc/architecture.md` §4.9/§4.11 and `doc/free-endpoints.md` §8/§9.
  Documentation only, executed by the lead; no code.

Decisions carried through all children:

- The fallback is a manager-level policy, not code inside each service. `TranslationManager` gets a
  configured fallback service id (`translation/fallbackService`, empty by default) and a
  `setFallbackService(const QString &id)`/`fallbackService()` pair. The setting is attempted only
  when the user configured one and the free-services gate is on.
- The manager tries the fallback exactly once, only for `Auth`, `QuotaExceeded`, `RateLimited`,
  `ServiceUnavailable`, `Network`, and `Timeout` from the primary service (including the
  pre-request `Auth` path when the active service is not configured). It does not fall back on
  success, `EmptyText`, `TooLong`, `UnsupportedLanguage`, `Cancelled`, or `BadResponse`: those are
  either the user's input or a defect that a different provider would hide. There is no second hop
  and no loop; if the fallback itself fails, the caller receives the **primary** error and the
  fallback error is logged without text.
- The fallback must be a registered service with `requiresOptIn() == true`, must not be the reply's
  primary service, and must not be used while the gate is off, even if the INI still names one. An
  unknown or non-opt-in id is ignored.
- The fallback result is returned with its own `serviceId` and cached under the normal primary cache
  key; the manager clears the cache when the fallback setting changes and when the gate is turned
  off, because a different fallback (or no fallback) can produce a different result. The fallback
  does not get the smart secondary-language retry: one failure is one fallback request.
- The settings dialog adds a **Fallback service** group after the free-services group, with `None`
  plus every registered service whose `requiresOptIn()` is true; the combo is disabled while the
  free-services gate is off, but the remembered value stays in the INI. `AppController` builds the
  list from the registered services, so the dialog still knows nothing about services.
- The fallback shape is deliberately one hop, not an arbitrary list: it covers "official API → free
  service", and Mozhi itself proxies to several engines, so a user who wants the "free web → Mozhi"
  step can configure Mozhi. Google and DeepL already have their own explicit free modes; those are
  modes of the same service, chosen by the user, and are not a second automatic hop. This keeps the
  privacy story simple: the text goes to the active provider, and on a retryable failure to exactly
  one configured fallback. Both are named in the warning.
- The privacy rule is unchanged: text, keys and cookies are never logged. The fallback log lines
  name the services and the error kinds only; the dialog hint states that a retryable failure sends
  the text to the configured fallback and that the text goes to a third party.
- Tests extend the existing `tst_translationmanager`, `tst_settings` and `tst_settingsdialog`; no new
  test target is added, so Linux stays at 39 tests and Windows at 34 (31 shared plus 3 Windows-only).
  The `.ts` message count moves from 313 to 316 (the group title, `None`, and the hint).
  `README.md`, `README.ru.md`, `doc/handoff.md`, the root plan and the parent stage stay with the
  lead; both READMEs' provider/fallback section is the lead's final pass.

## Done when

- `01-fallback-manager` is done: the manager has the fallback id API; it tries the configured opt-in
  fallback exactly once on `Auth`, `QuotaExceeded`, `RateLimited`, `ServiceUnavailable`, `Network`,
  and `Timeout`, including a primary that is not configured before any request; it does not try it
  on success, `EmptyText`, `TooLong`, `UnsupportedLanguage`, `Cancelled`, or `BadResponse`, never
  uses the reply's primary service as its own fallback, and never loops. A successful fallback
  returns its translation with the fallback's `serviceId`, caches it under the normal key and logs
  the fallback without the text; a failed fallback returns the primary error and logs the fallback
  error; cancelling while the fallback runs aborts it and returns `Cancelled`. Changing the fallback
  and turning the gate off clear the cache. `tst_translationmanager` covers the matrix and is green;
  Linux is 39/39 in Debug and Release without warnings; Windows 34 is derived, not run.
- `02-fallback-settings-and-ui` is done: `translation/fallbackService` is loaded/saved/normalized
  (empty means no fallback, unknown and non-opt-in ids become empty); the settings dialog has the
  **Fallback service** group after the free-web group with `None` and the registered opt-in services,
  disabled while the gate is off and remembering its value; `tst_settings` covers the default, the
  round trip and the normalization, and `tst_settingsdialog` covers the contents, the order, the
  disabled state and the unknown-id cleanup. `AppController` passes the registered opt-in list to the
  dialog and applies the setting at startup and on change; the gate-off path uses the primary only.
  All 14 `.ts` files are complete at 316 messages and `tst_translations` passes. The live smoke is
  recorded: with an unconfigured official primary (empty key) or an intentionally invalid official
  key and the gate on, the fallback translates a short text and the log/UI names the fallback
  service; if the network or the free endpoint is unavailable, the check is recorded as not
  performed with the reason.
- `03-fallback-docs` is done: `doc/architecture.md` §4.9 (the fallback policy, the trigger and
  non-trigger lists, the one-hop rule, the pre-request `Auth` path, the primary error, the cache, the
  gate and the privacy) and §4.11 (the `translation/fallbackService` key row) describe the
  implemented behaviour; `doc/free-endpoints.md` §8 item 6 marks the fallback implemented and §9
  records it as the final step; all relative links resolve and no code is touched.
- The overall fallback criteria hold: official Google/DeepL remain primary; the fallback is opt-in,
  default `None`, one hop, never a loop, and never the only translation method; it is not used while
  the free-services gate is off; a fallback failure shows the primary error; the text never appears
  in a log or a message; changing the fallback or disabling the gate clears the cache.
- Debug and Release build on Linux without warnings and the full suite passes 39/39; Windows
  Debug/Release and its 34 tests are recorded as not performed (no MSVC); the live smoke is recorded
  as passed or as not performed with the reason; no push is made.
- Both READMEs list Google, DeepL, OpenRouter/Local LLM as primary and MyMemory, Mozhi, Yandex web,
  Bing web and Reverso as opt-in free fallbacks (MyMemory is a free API; the others are unofficial),
  with the privacy and instability warning; `doc/handoff.md` and the root plan are updated. This is
  the lead's final pass, not a child edit.
- The parent stage's `## Done when` is traced by the lead in the stage outcome: MyMemory and Mozhi
  work with rate limits; Yandex/Bing/Reverso return clear errors; 429/403 are clear and do not hang;
  official services stay primary; tests and documentation are updated.

## Depends on

`01-free-web-infrastructure` (the opt-in gate and `QueuedWebService`), `02-mymemory`, `03-mozhi`,
`04-yandex-web`, `05-bing-web` and `06-reverso` (the five opt-in services the combo lists and the
manager can retry on). All six are `done`. The three children are sequential:
`02-fallback-settings-and-ui` depends on `01-fallback-manager` for the manager API,
`03-fallback-docs` depends on both for the final behaviour and the strings.

## Risks and what is needed from the user

- Automatic fallback changes where the text is sent. Keep it opt-in, default `None`, one hop, and
  visible in the warning; never fall back when the free-services gate is off, even if the INI still
  names a fallback. The user already accepted this policy; do not broaden it without a new decision.
- Returning the primary error when the fallback also fails keeps the official service's reason
  visible, but the fallback error must still be logged for diagnosis. Do not concatenate raw
  response bodies or the text into the message.
- The pre-request `Auth` path is not a service reply; it is easy to return it before the fallback can
  run. Implement it through the same managed `Reply` and test it, otherwise "DeepL without a key →
  free service" silently does not work.
- The fallback must not mask programming errors. `BadResponse`, `UnsupportedLanguage`, and `TooLong`
  are excluded deliberately; do not add them "for robustness" without a new decision.
- The settings normalization uses a fixed list of the five opt-in ids, while the combo uses the
  registered services; keep the two in sync when a service is added or removed. The manager ignores
  an unknown or non-opt-in id at request time, so a stale setting cannot break startup.
- `clearCache()` on a fallback change and on a gate-off is required; otherwise a cached primary
  result hides the new fallback and a cached fallback result survives disabling.
- A live smoke needs network access and an available free endpoint. It is recorded, not worked
  around; a blocked endpoint or an offline machine means "not performed with the reason", never a
  fake pass. Windows Debug/Release and its 34 tests are unavailable on this Linux machine and are
  recorded as not performed, not as passed.
- The final documentation and test gate belongs here; the lead owns `doc/handoff.md` and the release
  decision. Do not start a release or push.
