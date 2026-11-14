---
kind: stage
status: in-progress
base: 2650def
---

# Free web services always on

## Goal

Remove the opt-in gate for the free web services. On 2026-11-14 the user reversed the 2026-11-08
decision: every service must be available by default, and the **Allow free web services** checkbox
must disappear. The user also expected the free Google endpoint and DeepL `free-web` to be affected
by the checkbox; in fact they were never behind the gate, which only disabled MyMemory, Mozhi,
Yandex web, Bing web, Reverso and the fallback.

Remove the gate completely rather than defaulting it to on: no setting, no `requiresOptIn()`, no
disabled service groups and no hint telling the user to enable anything. Keep the per-service
warnings about unofficial endpoints, rate limits, blocks and third-party text, and keep the fallback
as a separate selectable service.

## What to do, what is touched

`src/core/settings.h`/`settings.cpp` (the field, INI load/save, the `fallbackServiceIds()` comment
and order),
`src/core/translationservice.h` (`requiresOptIn()`), `src/core/translationmanager.h`/`.cpp`
(`setFreeServicesAllowed()`, the pre-request refusal, the fallback gate and cache clearing),
`src/app/appcontroller.cpp` (startup, pickers, settings change, fallback list and dialog argument),
`src/ui/settingsdialog.h`/`.cpp` (the checkbox, the **Free web services** group, the group enabling
functions and the hint sentences), the five service headers and their property tests, and the
affected tests (`tst_settings`, `tst_settingsdialog`, `tst_translationmanager`, `tst_mymemoryservice`,
`tst_mozhiwebservice`, `tst_yandexwebservice`, `tst_bingwebservice`, `tst_reversoservice`). The 14
`.ts` files lose the checkbox, hint and error strings. Documentation is the lead's pass after the
tasks.

Two placement decisions from 2026-11-14 belong to the stage. The **Fallback service** control moves
from the Services page to the Translation page as a `Fallback service:` form row with its hint and
no group box (`objectName` `fallbackService` stays), because a fallback is a translation behaviour,
not a provider setting. The providers are shown in one order everywhere — Google, Yandex, Bing,
DeepL, OpenAI-compatible, MyMemory, Mozhi, Reverso — in the Services page, in the main-window and
tray pickers (registration order in `AppController`) and in the fallback options
(`fallbackServiceIds()`, i.e. Yandex, Bing, MyMemory, Mozhi, Reverso).

`proofreading/allowOnlineServices` and the Text checking tab are **not** touched: the user said the
text-checking question was understood incorrectly and will be discussed after this stage. That is a
deferred user decision, not part of this stage.

## Children

The stage has two sequential children:

1. [01-remove-free-services-gate](01-remove-free-services-gate/plan.md) (task, detailed) — remove
   the gate and the whole opt-in concept and terminology, update the tests and the 14 `.ts` files.
2. [02-move-fallback-and-order-services](02-move-fallback-and-order-services/plan.md) (task,
   detailed, `depends` on `01`) — move the **Fallback service** control to the Translation page as
   a form row and put the providers into one order everywhere, with its tests and the `.ts` label
   change.

Why two tasks: each is an independently acceptable, green result with its own diff and criteria. The
gate removal cannot be split further — a partial state leaves either the setting and the checkbox
alive while the manager ignores them (dead, misleading UI) or pickers and hints promising a gate that
no longer exists, and the `.ts` update cannot be separated because `tst_translations` fails on the
vanished gate messages. The placement/order change is presentation-only, can be reviewed on its own,
and depends on the gate-free hint and the gate-free Services page, so it goes second.

## Done when

- `translation/allowFreeServices` is gone from `AppSettings`, the INI load/save, `TranslationService`,
  `TranslationManager` and `AppController`; `requiresOptIn()` and `freeServicesAllowed()` no longer
  exist; every registered service is available at startup and in the main-window/tray pickers, and
  the fallback works without any gate.
- The opt-in concept is gone: no `opt-in` vocabulary, no **Free web services** category, no general
  group or general hint, and no replacement setting, allow-list or category. The only remaining
  `free web` strings are DeepL's own `free-web` mode (`Free web (unofficial, no key)`,
  `DeepL (free web)`, the DeepL service header comment). The per-service unofficial-endpoint,
  rate-limit, block and third-party warnings stay in the individual service hints.
- The **Fallback service** control is a `Fallback service:` form row on the Translation page with its
  hint and no group box (`objectName` `fallbackService` kept), and a test proves it is on the
  Translation page, not on Services. The hint keeps the failure list, `None` and the third-party
  warning and mentions no enabling.
- The providers are in one order everywhere — Google, Yandex, Bing, DeepL, OpenAI-compatible,
  MyMemory, Mozhi, Reverso — in the Services page, in the main-window/tray pickers (registration
  order in `AppController`) and in the fallback options (`fallbackServiceIds()`: Yandex, Bing,
  MyMemory, Mozhi, Reverso). Google stays the default active service.
- `proofreading/allowOnlineServices`, the Text checking tab, its strings and its tests are
  byte-identical; the text-checking question is recorded as deferred by the user.
- Linux Debug and Release build without warnings and all 45 tests pass; the 39 Windows tests are
  recorded as not performed (no MSVC here). The `.ts` files are regenerated with no vanished
  messages and `tst_translations` passes; the final count is 379.
- `doc/architecture.md`, `doc/free-endpoints.md`, `doc/handoff.md`, both READMEs and the root plan
  describe the always-on behavior, the Translation-page fallback row and the provider order.
