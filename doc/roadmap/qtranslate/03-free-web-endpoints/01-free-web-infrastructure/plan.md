---
kind: stage
status: in-progress
base: fee1bcf
---

# Free web service infrastructure

## Goal

Provide the shared foundation for the stage-3 free services without adding an endpoint: an explicit
opt-in gate for no-key services with a clear error and UI, and a reusable serialized per-host request
queue extracted from the accepted DeepL free-web implementation. Both pieces are independent and are
built by the two children of this stage.

## What to do, what is touched

The opt-in gate: `translation/allowFreeServices` (default `false`),
`TranslationService::requiresOptIn()`, the central `TranslationManager` check, the new
`TranslationError::ServiceUnavailable`, the Services-tab checkbox and warning, the picker filtering
and active-service switch in `AppController`, tests, and the documentation for the gate and the INI
key. The queue: the `QueuedWebService` base, the `DeepLWebService` refactor, the new
`tst_queuedwebservice`, the unchanged `tst_deeplwebservice` as the regression gate, and the
architecture notes for the shared queue. No free service is added, no endpoint is enabled, and the
DeepL `free-web` mode stays outside the gate.

## Done when

- `01-opt-in-gate` is done: the setting defaults to off and persists; `requiresOptIn()` plus the
  manager refusal with `ServiceUnavailable` and "Enable free web services in Settings → Services."
  work; the pickers filter gated services and the controller switches to Google when the active
  service becomes gated; the UI warning and the 14 `.ts` files are complete; the architecture and
  `doc/free-endpoints.md` describe the gate.
- `02-queued-web-service` is done: `QueuedWebService` holds the serialized queue and the rate pause;
  `DeepLWebService` uses it with no behaviour change; `tst_deeplwebservice` stays green; the new
  `tst_queuedwebservice` covers ordering, the pause, aborting a queued reply, deleting the active
  reply, and `noteRequestSent()`; `doc/architecture.md` §4.7.1 describes the shared queue.
- No service is added and no endpoint is enabled; Google, DeepL (including `free-web`), and the LLM
  service behave as before except for the new gate infrastructure.
- Debug and Release build without warnings on Linux; the full suite passes; the Windows build and
  its tests (23 existing plus the new platform-neutral `tst_queuedwebservice`, i.e. 24) are recorded
  as not performed.

## Depends on

None technically. The two children are independent: the gate has no real consumer until
`02-mymemory`, and the queue extraction does not need the gate. `02-mymemory`, `03-mozhi`,
`04-yandex-web`, `05-bing-web`, `06-reverso` depend on this stage; `07-fallback-chain` depends on all
of them. The numbering of the children is the order in this plan, not a hard dependency.

## Risks and what is needed from the user

The gate has no real opt-in service until `02-mymemory`, so its end-to-end picker check lands there;
the DeepL queue refactor must stay behaviour-preserving and is gated by `tst_deeplwebservice`; all new
`tr()` strings must be complete in the 14 `.ts` files, including `→`. No live smoke or new user
access is needed because no endpoint is added. The user has already accepted the stage-level rule that
free services are opt-in and are not the only translation method.
