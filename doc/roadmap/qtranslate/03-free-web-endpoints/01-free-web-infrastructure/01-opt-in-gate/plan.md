---
kind: task
status: in-progress
detailed: true
---

# Opt-in gate for free web services

## Task

Stage 3 adds no-key endpoints that are not official Google/DeepL APIs. This task adds the policy that
makes them explicit: a `translation/allowFreeServices` setting (default `false`), a
`TranslationService::requiresOptIn()` capability, and a central `TranslationManager` check with a
clear `ServiceUnavailable` error. The Services tab gets the checkbox and warning; `AppController`
hides gated services from the pickers and switches to Google if the active service becomes gated. The
new `TranslationError::ServiceUnavailable` is also used by the later service tasks for a blocked or
HTML endpoint, so it gets a UI title and complete `.ts` translations.

The gate is enforced once in the manager, not in every service, so a service cannot forget it. The
DeepL `free-web` mode is an explicit mode with its own warning and stays outside this switch. No
service is added here; the first real opt-in service is the sibling `02-mymemory`, which also does
the first end-to-end picker check. The queue extraction is the separate sibling
`02-queued-web-service` and is not part of this task.

## Steps

1. Add the setting and the error kind.
   - `src/core/settings.h:28-36`: add `bool allowFreeServices = false;` to `TranslationSettings`
     with a comment that it gates services whose `requiresOptIn()` is true.
   - `src/core/settings.cpp`: read it in the `translation` group of `load()` (`:301-317`) and write it
     in `save()` (`:434-442`). It is a plain bool; do not add a version migration, the default
     `false` is the safe value for existing files. Check that `normalize()` (`:127-194`) leaves it as
     is.
   - `src/core/translationtypes.h:16-28`: add `ServiceUnavailable` after `RateLimited` with the
     comment "the endpoint is blocked or temporarily unavailable; the HTML/403 case of a web
     endpoint". It must not be used for the existing official-API paths.
   - `src/ui/translationerrortext.cpp:8-40`: add a case returning the title
     "The translation service is unavailable" (translatable, `QT_TRANSLATE_NOOP`). The switch must
     stay exhaustive so the compiler catches a missing value.
   - Run `lupdate` and fill the new strings in all 14 `i18n/qtranslate_*.ts` files; for the title and
     the manager message keep `→` exactly. `tst_translations` is the gate.

2. Add the opt-in capability and enforce it in the manager.
   - `src/core/translationservice.h:51-72`: add
     `virtual bool requiresOptIn() const { return false; }` with a comment: "The service sends text
     to an unofficial or free endpoint and must be explicitly enabled; the manager refuses it while
     `translation/allowFreeServices` is false." Do not override it in Google, DeepL, or the LLM
     service.
   - `src/core/translationmanager.h/.cpp`: add `bool freeServicesAllowed() const` and
     `void setFreeServicesAllowed(bool allowed)` with a member. In `translate()`
     (`translationmanager.cpp:184-223`) insert the check after the max-length check and before the
     `isConfigured()` check: when the active service requires opt-in and the gate is off, return
     `TranslationReply::createFinished` with `ServiceUnavailable` and
     `tr("Enable free web services in Settings → Services.")`. Do not cache this error and do not
     call the service. Keep the existing `Auth` behaviour for unconfigured official services.
   - `tests/tst_translationmanager.cpp`: extend `FakeService` (`:43-73`) with an `optIn` constructor
     flag and `requiresOptIn()`. Add tests: `optInServiceIsRefusedWhenDisabled` (no request reaches
     the service, error is `ServiceUnavailable`, the message contains "Settings"),
     `optInServiceWorksWhenEnabled` (the usual reply and cache path), and
     `regularServiceIsNotAffectedByTheGate`. Use the existing `FakeReply` helpers.
   - `src/app/appcontroller.cpp`: in `createTranslation()` (`:213-240`) call
     `m_translations->setFreeServicesAllowed(settings.translation.allowFreeServices)` before
     selecting the active service. If the configured service is registered but
     `requiresOptIn()` and the gate is off, do not activate it: log a warning and leave the first
     registered (Google) active. In `applySettings()` (`:511-597`) update the flag when it changes;
     if it is turned off while the active service requires opt-in, activate `google` and log it.
     Call `updateServiceSelectors()` after the gate change so enabling it shows the services without
     a restart. Do not rewrite `translation/service` in the INI, so re-enabling the gate and
     restarting restores the user's choice; document that in the task report.
   - `src/app/appcontroller.cpp:468-487` (`updateServiceSelectors()`): skip services whose
     `requiresOptIn()` is true and the gate is off, so a gated service is not selectable in the main
     window or tray. The active-service switch above guarantees that the picker always has a valid
     active non-gated service.
   - No service overrides `requiresOptIn()` in this task; the gate is exercised by the fake service
     until the sibling `02-mymemory` lands. The switch behaviour must be visible in the settings
     dialog even before the first real free service exists.

3. Put the switch in the settings dialog.
   - `src/ui/settingsdialog.h/.cpp`: add a `QCheckBox *m_allowFreeServices` and a word-wrapped hint
     label. Add a group box "Free web services" at the end of the Services page
     (`settingsdialog.cpp:177-303`, after the LLM group and before `addStretch()`), so the existing
     `QScrollArea` keeps the page usable at 150% scaling. Object names: `allowFreeServices` for the
     checkbox and `freeWebHint` for the hint.
   - Checkbox text: `tr("Allow free web services")`. Hint text: "These services need no API key. Most
     are unofficial: they may stop working, apply rate limits or block requests, and the text is
     sent to a third party. The Google and DeepL APIs remain the recommended choice."
   - Load the value in `loadValues()` near the translation controls (`:417-418`) and write it in
     `settings()` next to the other `result.translation.*` lines (`:467-471`). Do not touch
     `updateDeepLControls()` or the DeepL warning; the DeepL free-web mode stays a separate explicit
     mode.
   - `tests/tst_settingsdialog.cpp`: find the checkbox by object name, check the default is unchecked,
     include it in `showsCurrentValues`/`returnsEditedValues`, and verify the round trip.
   - `tests/tst_settings.cpp`: add the field to `defaults()` and `roundTrip()`.
   - Run `lupdate`/`lrelease` after the new `tr()` strings and keep all 14 `.ts` files complete.

4. Document the gate.
   - `doc/architecture.md`: update the `TranslationService` interface snippet (`:629-642`) with
     `requiresOptIn()`; add a short paragraph to §4.5 about the opt-in gate; update §4.9
     (`:888-934`) with `setFreeServicesAllowed()` and the `ServiceUnavailable` gate check; add
     `translation/allowFreeServices` to the key table (`:1059-1094`).
   - `doc/free-endpoints.md` §8 (`:116-131`): mark item 1 (the common toggle) as implemented
     infrastructure and mention the `ServiceUnavailable` error kind. Do not touch §4.7.1 or the
     per-host queue description; that is `02-queued-web-service`.
   - Do not edit `README.md`/`README.ru.md` yet: the provider list changes in the service tasks and is
     consolidated in `07-fallback-chain`.
   - Do not edit `doc/handoff.md`; the lead owns it at the end of the stage.

5. Build, test, and report.
   - Linux (Astra, Qt 6.10.2) and the `qtranslate-trixie` container if available: Debug and Release
     with `-Wall -Wextra`, no `warning:` lines. The current Linux suite is 28 tests; this task adds
     no new test target (it extends `tst_translationmanager`, `tst_settings`, and
     `tst_settingsdialog`), so expect 28 (record the exact count). Run those tests and
     `tst_translations` individually, then the full suite.
   - Verify the UI manually under `QT_QPA_PLATFORM=offscreen` or a real session: the checkbox is
     unchecked by default, checking it and saving writes `[translation] allowFreeServices=true`, and
     the hint is readable in the scrollable Services page.
   - Windows is not available here (no MSVC); record the Windows build and the 23 Windows tests as
     not performed, not as passed. The changes are portable C++/Qt.
   - Report for the lead: the files, the test count, the exact INI key, the chosen switch behaviour
     when a free service is active and the gate is turned off, and the fact that DeepL `free-web`
     remains outside the gate. The queue base and `tst_queuedwebservice` are `02-queued-web-service`,
     not this task's result.

## Done when

- `translation/allowFreeServices` defaults to `false`, is loaded and saved in the `translation`
  group, and is exposed as an unchecked `allowFreeServices` checkbox with a `freeWebHint` warning on
  the scrollable Services page; `tst_settings` and `tst_settingsdialog` cover the default and the
  round trip.
- `TranslationService::requiresOptIn()` exists with a default of `false`; `TranslationManager`
  refuses an opt-in service while the gate is off with `ServiceUnavailable` and the message
  "Enable free web services in Settings → Services." without calling the service, and translates
  normally when the gate is on; non-opt-in services are unaffected. `tst_translationmanager` covers
  all three cases.
- The controller implements the picker filtering (opt-in services are absent while the gate is off)
  and the active-service switch to Google with a log entry; enabling the gate calls
  `updateServiceSelectors()` so a later real service appears without a restart, and a restart with
  the gate on restores a configured opt-in service. This task adds no opt-in service, so the first
  end-to-end check of the picker is part of the sibling `02-mymemory`; here the logic is reviewed and
  covered by the manager/settings tests.
- `TranslationError::ServiceUnavailable` is rendered with the title
  "The translation service is unavailable"; all 14 `i18n/qtranslate_*.ts` files are complete and
  `tst_translations` passes, with `→` preserved.
- `doc/architecture.md` §4.5/§4.9/§4.11 and `doc/free-endpoints.md` §8 item 1 describe the gate, the
  error kind, and the new INI key; the READMEs and `doc/handoff.md` are unchanged.
- Debug and Release build without warnings on Linux; the full suite passes; the Windows build and
  the 23 Windows tests are recorded as not performed.
- No service is added, no endpoint is enabled, and `QueuedWebService`/`tst_queuedwebservice` are not
  part of this task's diff.

## Risks

- Gating an already selected service can leave the INI pointing at a hidden service. The chosen
  behaviour is: switch the active service to Google in memory, log it, and keep the INI value so the
  choice returns when the gate is enabled again. If the coordinator prefers to rewrite
  `translation/service`, that is a settings migration decision and must be agreed, not added here.
- `ServiceUnavailable` changes an enum and the UI title switch; an unhandled value would compile with
  a warning or fall through. Search all `switch` statements in `src/` after the change; only
  `translationerrortext.cpp` is exhaustive today.
- The new `tr()` strings must be present in all 14 `.ts` files, including the `→` message. `lupdate`
  must be run from the repository root with the same file list as `CMakeLists.txt`; `lrelease` is
  part of the normal build, but `tst_translations` reads the `.ts` files directly.
- The Services page is already tall; add the new group inside the existing `QScrollArea` and do not
  change the dialog size or the `tst_settingsdialog` offscreen assumptions.
- The gate has no real opt-in service until the sibling `02-mymemory`; the picker's end-to-end
  behaviour is checked there. Do not add a temporary fake service to the application to make the
  check look complete.
- Windows is not available here. Keep the code portable (no platform calls) and record the missing
  Windows build honestly; do not claim the 23 Windows tests passed.
- No live smoke test or new user access is needed in this task: it adds no endpoint, so every check
  is local. The user has already accepted the stage-level rule that free services are opt-in and are
  not the only translation method.
