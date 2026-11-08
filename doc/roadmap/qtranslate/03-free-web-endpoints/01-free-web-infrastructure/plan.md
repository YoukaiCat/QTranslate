---
kind: stage
status: done
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

## Outcome

Accepted on 2026-11-08 over `fee1bcf..fb531fa`. Both children are `done`: `01-opt-in-gate`
(`8ea3d0a`) and `02-queued-web-service` (`e13c294`), with the journal and count updates in
`01e8b72`/`fb531fa`.

- **Gate:** `translation/allowFreeServices` defaults to `false`; `TranslationService::requiresOptIn()`
  and the central `TranslationManager` refusal with `ServiceUnavailable` and "Enable free web
  services in Settings → Services." work without calling the service; the Services tab has the
  checkbox and warning; `AppController` filters the pickers and switches to Google in memory when the
  active service becomes gated, keeping the INI value; five strings are complete in all 14 `.ts`.
  The `lupdate`-vanished Windows-only hotkey message was fixed with `hotkeymanager_win.cpp` as an
  extra `qt_add_translations` source and a `tst_translations` guard.
- **Queue:** `QueuedWebService` in `qtranslate_core` holds the serialized per-host queue and the
  2000 ms default pause; the moved body matches the old DeepL code after the class rename;
  `DeepLWebService` derives from it and keeps its endpoint, retry delay, `toWebCode()`, chunking, and
  parser; `tst_deeplwebservice` and `tst_deeplservice` are unchanged and green; the new
  platform-neutral `tst_queuedwebservice` covers ordering, the pause, aborting a queued reply,
  deleting the active reply, and the multi-chunk slot/pacing contract.
- **Verification:** Astra Linux 1.8.7 (Qt 6.10.2/GCC 12.2) and `qtranslate-trixie` (Qt 6.8.2/GCC
  14.2), Debug and Release, zero warnings, 29/29; the new queue test ran 40 times without a flake;
  Clang 16 Debug is clean; `update_translations` finds 299 messages, 0 new, 0 vanished.
- **Docs:** `doc/architecture.md` §3.1/§3.2/§4.5/§4.7.1/§4.14/§8, `doc/free-endpoints.md` §8,
  `AGENTS.md`, both READMEs, `doc/handoff.md`, and the root plan use the 24/29 counts and describe
  the gate and the shared queue.

No service is added and no endpoint is enabled; Google, DeepL (including `free-web`), and the LLM
service behave as before. Windows Debug/Release and its 24 tests were not performed (no MSVC); a real
display/150% UI check was not repeated (the dialog test is offscreen). No real code problems were
deferred. The next node is `02-mymemory`.
