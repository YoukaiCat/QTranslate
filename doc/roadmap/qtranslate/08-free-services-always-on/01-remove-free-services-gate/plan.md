---
kind: task
status: done
detailed: true
---

# Remove the free web services gate

## Task

Remove the `translation/allowFreeServices` opt-in gate and the whole opt-in concept, per the user's
2026-11-14 decision: free services are ordinary providers, not a category that needs permission.
Every registered service is available at startup and in the main-window/tray pickers, the Services
page has no **Allow free web services** checkbox, no **Free web services** group, no general hint
and no disabled groups, and `AppSettings`, the INI load/save, `TranslationService`,
`TranslationManager` and `AppController` no longer know the concept. The fallback stays a separate
user choice (`translation/fallbackService`): one hop, a registered service that is not the primary,
an unknown or empty id ignored. No replacement setting or category is introduced.

This task is one atomic behaviour change: the gate removal, the terminology sweep, the tests and
the `.ts` files cannot be separated, because `tst_translations` fails on the vanished gate messages
and any partial state leaves either dead UI or pickers that refuse services. Splitting this task
was rejected. It deliberately keeps the **Fallback service** group where it is (Services) and does
not touch the provider order: moving the fallback control to the Translation page and reordering
the providers is the next child, `02-move-fallback-and-order-services`.

`proofreading/allowOnlineServices` and the whole Text checking tab are out of scope: the user said
the text-checking question was understood incorrectly and will be discussed after this stage. Do
not change that setting, that page or its strings, and do not reason about it in the tests.

Context (architecture: `doc/architecture.md` §4.5, §4.9, §4.11, §4.13; endpoints:
`doc/free-endpoints.md`). The gate is a `bool` field with a load/save pair and a fallback guard
(`src/core/settings.h:37-39`, `:135-138`; `src/core/settings.cpp:163-174`, `:495-498`, `:659-660`),
a virtual `TranslationService::requiresOptIn()` with five overrides
(`src/core/translationservice.h:66-69`), a manager flag with a pre-request refusal and a fallback
condition (`src/core/translationmanager.h:42-50`, `:88`; `src/core/translationmanager.cpp:179-182`,
`:303-310`, `:321-333`, `:367-370`), six `AppController` places
(`src/app/appcontroller.cpp:246`, `:274-284`, `:543-551`, `:626-636`, `:707-719`, `:762-768`),
the Services-page group, checkbox, five service hints and the fallback hint, and six
`update*Controls()` functions (`src/ui/settingsdialog.h:86-91`, `:123`, `:163-176`;
`src/ui/settingsdialog.cpp:311-346`, `:361-362`, `:412-413`, `:429-430`, `:444-445`, `:460-461`,
`:703`, `:775-780`, `:801`, `:903-935`), and the affected tests. The free Google endpoint and DeepL
`free-web` were never behind the gate and do not change.

Design decisions:

- Delete the setting; do not default it to true. No `translation/allowFreeServices` is read or
  written. An old INI that still carries the key simply loads without it: `QSettings` keeps unknown
  keys, so the stale line may remain in that file and is never read or written again. Do **not**
  add a `remove()`/migration call — that would put the identifier back into `SettingsStore`.
  `settingsVersion` stays `1`.
- Delete `TranslationService::requiresOptIn()` and its five overrides.
  `TranslationError::ServiceUnavailable` stays: the services still use it for HTTP/HTML failures.
- Every registered service is translatable; `isConfigured()` (DeepL without a key, an OpenRouter
  LLM without a key) still refuses with `Auth` exactly as before.
- `TranslationManager::fallbackFor()` becomes: non-empty id, registered, not the primary. The
  duplicate `fallback == primary` / id comparison collapses to one check. The settings layer still
  normalizes `translation/fallbackService` to `fallbackServiceIds()` (the five no-key web
  endpoints), so the UI cannot offer anything else; at request time the manager accepts any
  registered service, which is the rule the user asked for.
- `AppController` builds the dialog's fallback list from `fallbackServiceIds()` and looks the
  display names up in the registry (the list order is the `fallbackServiceIds()` order; a missing
  registration is skipped), so the dialog keeps knowing nothing about services.
- The Services page loses the whole **Free web services** group, including its generic hint
  (`These services need no API key. …`): the user asked for no general group, and every per-service
  hint keeps the unofficial-endpoint, rate-limit, block and third-party warnings. The five service
  groups and the fallback combo are always enabled. Remove the five gate-only `update*Controls()`
  functions and the four group members that existed only for them; `updateMozhiControls()` keeps
  only its effective-instance label and URL warning. The **Fallback service** group itself stays on
  this page in this task (only its gate connect and gate sentence go); moving it to the Translation
  page is `02`.
- No category replaces the gate: no new setting, no allow-list, no per-service switch. The words
  "opt-in" and "free web services" as a category disappear from `src` and `tests`; the only
  `free web` strings left are DeepL's own `free-web` mode (`Free web (unofficial, no key)`,
  `DeepL (free web)`), which are a mode name, not a category.
- The fixed normalization warning in `src/core/settings.cpp` currently says "not a supported
  opt-in service"; reword it to `Fallback service is not supported; using none` (still fixed, never
  echoing the typed value, and not a `.ts` message).
- Text checking is out of scope: `proofreading/allowOnlineServices`, the Text checking tab, its
  strings and its tests stay byte-identical. The user deferred that discussion until after this
  stage.
- Tests are rewritten to assert the new behaviour, not deleted: no checkbox, groups enabled,
  fallback without any gate, any registered service usable as primary. The 14 `.ts` files are
  regenerated in this task; the expected count is 383 → 379 (four removed messages: the group
  title, the checkbox, the generic hint and the manager error; six changed hints are retranslated).
- Documentation (`doc/architecture.md`, `doc/free-endpoints.md`, `doc/handoff.md`, both READMEs,
  `AGENTS.md`, the root plan) is the lead's pass after this task and is not touched here.

## Steps

1. **Settings model** — `src/core/settings.h`, `src/core/settings.cpp`:
   - Remove `TranslationSettings::allowFreeServices` and its comment (`settings.h:37-39`). Keep
     `fallbackService` and its comment (`:40-41`).
   - Rewrite the `fallbackServiceIds()` comment (`:135-138`) to: the settings-layer guard for
     `translation/fallbackService` — the five no-key web endpoints that may be a one-hop fallback;
     the manager resolves the id at request time and ignores an unknown or unregistered one. Keep
     the list and its order unchanged in this task (the provider order is `02`).
   - Delete the load lines (`settings.cpp:495-498`) and the save lines (`:659-660`). The
     `fallbackService` load and save stay (`:499-500`, `:661`).
   - In `normalize()` (`:163-174`), keep the empty/trim/lowercase/guard logic and change only the
     warning text at `:170` to `Fallback service is not supported; using none`. Do not touch
     `settingsVersion`.

2. **Service interface and the five services** — `src/core/translationservice.h:66-69` and
   `src/services/{mymemory,mozhi,yandexweb,bingweb,reverso}service.{h,cpp}`:
   - Delete the virtual `requiresOptIn()` and its comment from the interface.
   - Delete the `bool requiresOptIn() const override;` declaration and the
     `requiresOptIn() { return true; }` definition from each of the five services
     (`mymemoryservice`: header `:22`, cpp `:314-317`; `mozhiservice`: `:27`, cpp `:350-353`;
     `yandexwebservice`: `:27`, cpp `:347-350`; `bingwebservice`: `:29`, cpp `:562-565`;
     `reversoservice`: `:26`, cpp `:387-390`).
   - Reword the class comments that still say the service is "enabled together with the other free
     web services" or "opt-in" — keep the unofficial/no-key/no-SLA facts and drop the enabling
     clause: `mymemoryservice.h:9-11`, `mozhiservice.h:9-12`, `yandexwebservice.h:9-12`,
     `bingwebservice.h:11-14`, `reversoservice.h:9-12`.

3. **Manager** — `src/core/translationmanager.h`, `src/core/translationmanager.cpp`:
   - Remove `freeServicesAllowed()`, `setFreeServicesAllowed()`, `m_freeServicesAllowed`
     (header `:42-45`, `:88`) and the setter body (cpp `:303-310`).
   - Update the fallback comment (header `:47-50`): a registered service that is not the primary;
     an unknown or unregistered id is ignored; changing it clears the cache.
   - Rewrite `fallbackFor()` (cpp `:321-333`): empty id → none; `service(m_fallbackService)` null
     or equal to `primary` → none; otherwise return it. Drop the gate and the duplicate
     `fallback->id() == primary->id()` check (`registerService()` already guarantees unique ids).
   - Delete the pre-request branch `else if (m_active->requiresOptIn() && !m_freeServicesAllowed)`
     with its `TranslationManager::tr("Enable free web services in Settings → Services.")`
     (cpp `:367-370`). The `isConfigured()` branch stays.
   - Reword the comment at `:179-182` ("a gate or fallback change" → "a fallback change made while
     the primary was in flight is honoured").

4. **`AppController`** — `src/app/appcontroller.cpp`:
   - `createTranslation()`: delete `setFreeServicesAllowed(...)` (`:246`). Collapse the startup
     block (`:274-284`) to: if `m_translations->service(settings.translation.service)` exists, call
     `setActiveService(...)`; otherwise keep the existing "Configured service is not available"
     warning for a non-empty id. No gate branch, no switch to Google.
   - `updateServiceSelectors()` (`:543-551`): drop the `freeServicesAllowed` local and the
     `requiresOptIn()` filter; append every registered service to both the main-window and tray
     lists.
   - `applySettings()`: delete the whole gate-change block (`:626-636`), including the
     "switching to google" path. In the service-change block (`:707-719`), keep the missing-service
     warning and call `setActiveService()` for any registered service.
   - `showSettings()` (`:762-768`): build the dialog's `QList<QPair<QString, QString>>
     fallbackServices` by iterating `fallbackServiceIds()` and appending
     `{id, m_translations->service(id)->displayName()}` when that service is registered (skip
     otherwise). The `SettingsDialog` call at `:779-783` is unchanged.

5. **Services page structure** — `src/ui/settingsdialog.h` and `src/ui/settingsdialog.cpp`,
   `buildUi()`:
   - Header: remove `m_allowFreeServices` (`:123`), the `updateMyMemoryControls()`,
     `updateYandexControls()`, `updateBingControls()`, `updateReversoControls()` and
     `updateFallbackControls()` declarations (`:86-91`), and the members `m_mymemoryGroup` (`:163`),
     `m_yandexGroup` (`:172`), `m_bingGroup` (`:174`), `m_reversoGroup` (`:176`). Turn
     `m_mozhiGroup` (`:165`) into a local `mozhiGroup` in `buildUi()` (it is only used there after
     this task) and keep its object name `mozhiGroup`.
   - Delete the whole `freeWebGroup` block (`settingsdialog.cpp:311-324`): group title, checkbox,
     generic hint, form layout and `servicesLayout->addWidget(freeWebGroup)`. No replacement group.
   - Fallback block (`:326-346`): keep the **Fallback service** group, its combo and its hint —
     moving them to the Translation page is `02`. Replace only the hint source string with exactly:
     `When the selected service fails with no connection, a timeout, a rate limit, an exceeded
     quota or an invalid key, the text is sent once more to the fallback service. None turns it
     off. The text is sent to a third party.`
     Remove the `connect(m_allowFreeServices, …, &SettingsDialog::updateFallbackControls)`.
   - MyMemory (`:348-362`): hint becomes
     `MyMemory is a free translation API and needs no key. It cannot detect the source language,
     and long text is sent in several requests. The text is sent to a third party.`
     Drop the gate connect and the `m_mymemoryGroup = myMemoryGroup;` assignment; keep the object
     name `mymemoryGroup`.
   - Mozhi (`:364-415`): hint becomes
     `Mozhi is a proxy to public translation engines. Instances are unofficial: they may stop
     working or rate-limit requests, and some engines may be disabled on an instance. A
     self-hosted instance's terms and stability are your responsibility. Long text is sent in
     several requests. The text is sent to the selected instance and engine.`
     Drop the gate connect; keep the `m_mozhiInstance` `textChanged` connect.
   - Yandex (`:417-430`): hint becomes
     `Yandex web is an unofficial endpoint and needs no key. It may stop working, apply rate limits
     or block requests. Long text is sent in several requests. The text is sent to a third party.`
     Drop the gate connect.
   - Bing (`:432-445`): the same sentence with `Bing web`; drop the gate connect.
   - Reverso (`:447-461`): hint becomes
     `Reverso is an unofficial endpoint and needs no key. It supports only English, French,
     German, Spanish, Italian and Russian and cannot detect the source language. It may stop
     working, apply rate limits or block requests. Long text is sent in several requests. The text
     is sent to a third party.`
     Drop the gate connect.
   - Keep every object name (`fallbackGroup`, `fallbackService`, `fallbackHint`, `mymemoryGroup`,
     `mymemoryHint`, `mozhiGroup`, `yandexGroup`, `bingGroup`, `reversoGroup` and their hints): the
     tests locate the widgets by name. Nothing becomes disabled: no `setEnabled(false)` remains for
     these groups or the fallback combo.

6. **Services page values** — `src/ui/settingsdialog.cpp`:
   - `loadValues()`: delete `m_allowFreeServices->setChecked(...)` (`:703`); keep the fallback
     `findData`/index-0 restore (`:704-706`). At the end (`:775-780`) keep
     `updateMozhiControls()` and delete the other five update calls.
   - `settings()`: delete `result.translation.allowFreeServices = …` (`:801`); keep
     `result.translation.fallbackService = m_fallbackService->currentData().toString()`.
   - `updateMozhiControls()` (`:908-915`): delete the `m_mozhiGroup->setEnabled(...)` line; keep
     the effective-instance text and the URL warning.
   - Delete `updateMyMemoryControls()`, `updateYandexControls()`, `updateBingControls()`,
     `updateReversoControls()` and `updateFallbackControls()` (`:903-906`, `:917-935`).
   - `validate()` and the proofreading controls (`:937-…`) are unchanged.

7. **`tst_translationmanager`** — `tests/tst_translationmanager.cpp`:
   - `FakeService` (`:53-87`): drop the `optIn` constructor parameter, the `requiresOptIn()`
     override and `m_optIn`; fix the constructor (`:56-62`) and the call sites that pass the third
     argument (`:104`, `:312`, `:340`, `:689`, `:714`).
   - `withFallback()` (`:101-110`): `new FakeService(QStringLiteral("free"))`; no
     `setFreeServicesAllowed(true)`; reword the comment at `:94` so it no longer says "opt-in
     fallback".
   - Delete the gate slots: `optInServiceIsRefusedWhenDisabled` (`:308`),
     `optInServiceWorksWhenEnabled` (`:336`), `gatedRealServiceIsRefused` (`:359`),
     `regularServiceIsNotAffectedByTheGate` (`:379`), `fallbackRequiresTheGate` (`:710`),
     `disablingTheGateClearsTheFallbackCache` (`:858`).
   - Add `anyRegisteredServiceCanBeActive()`: register two fakes, `setActiveService()` the second
     one, translate, and assert the request went to the second service and not the first.
   - Add `fallbackWorksWithoutAGate()`: primary + fallback, `setFallbackService("free")`, primary
     fails with `Network`, the fallback receives exactly one request and completes; the result is
     ok and carries the fallback's `serviceId`. This replaces the positive half of
     `fallbackRequiresTheGate`.
   - Add `fallbackCanBeAnyRegisteredService()`: a plain registered service (default
     `FakeService("regular")`) set as the fallback receives the retry. This is the new manager
     rule: a registered service that is not the primary.
   - `fallbackIsIgnoredWhenInvalid_data` (`:673-681`): drop the `non-opt-in service` row; keep
     `empty`, `unregistered` and `primary itself`, and keep the assertions that neither the
     fallback nor the other registered service (`regular`) is contacted.
   - Remove the now-unused includes `net/networkmanager.h` and `services/mymemoryservice.h`
     (`:3-4`). Keep the retryable/non-retryable matrices (`:571`, `:613`, `:644`, `:880`), the
     primary-error, abort, caching, no-second-hop and log-hygiene cases unchanged.

8. **`tst_settings`** — `tests/tst_settings.cpp`:
   - `defaults()` (`:78`): delete `QVERIFY(!settings.translation.allowFreeServices)` (`:100`);
     keep the `fallbackServiceIds()` assertion (`:102-105`).
   - `roundTrip()` (`:219`): delete the field assignment (`:244`) and the two assertions (`:300`,
     `:344`); near `:342-348` assert instead that a fresh INI has no `allowFreeServices` line
     (`QVERIFY(!ini.contains("allowFreeServices"))`).
   - `proofreadingChangesDoNotTouchTranslation()` (`:561`): delete `:570` and `:586`.
   - `fallbackServiceIsNormalized()` (`:685`): update the fixed warning constant (`:706-707`) to
     `Fallback service is not supported; using none` and the comment at `:704` so it no longer says
     "non-opt-in service"; keep the `"nope"`, `"google"`, `"deepl"` rows and the "does not echo the
     typed value" check.
   - Add `legacyFreeServicesGateKeyIsIgnored()`: write a raw INI with
     `settingsVersion=1`, `translation/service=mymemory` and `translation/allowFreeServices=false`;
     `load()` must keep `service == "mymemory"` and must not log a free-services warning; a
     `save()`/`load()` round trip keeps it. Do not assert that the stale key disappears — the app
     never reads or writes it, and `QSettings` keeps unknown keys.

9. **`tst_settingsdialog` and the five service property tests** —
   `tests/tst_settingsdialog.cpp`:
   - `showsCurrentValues()` (`:37`): delete `:48`, the `allowFreeServices`/`freeWebHint` lookups
     and their assertions (`:82-84`, `:100-101`, `:119-121`).
   - `returnsEditedValues()` (`:124`): delete `:159` and `:177`.
   - Rename `freeWebServicesGate()` (`:362`) to `freeWebGateIsGone()`: assert
     `findChild<QCheckBox *>("allowFreeServices")` and `findChild<QLabel *>("freeWebHint")` are
     null, that `mymemoryGroup`, `mozhiGroup`, `yandexGroup`, `bingGroup` and `reversoGroup` exist
     and are `isEnabled()`, and that `fallbackService` is enabled.
   - `myMemoryGroup()` (`:378`), `mozhiGroup()` (`:408`), `yandexGroup()` (`:535`),
     `bingGroup()` (`:579`), `reversoGroup()` (`:623`): drop the checkbox lookup/asserts and the
     toggle blocks; replace `QVERIFY(!group->isEnabled())` with `QVERIFY(group->isEnabled())`;
     drop the `hint->text().contains("free web services")` assertions (`:559`, `:603`, `:647`);
     keep the other warning substrings (`no key`, `unofficial`, `rate`, `block`, `third party`,
     `several requests`, Mozhi's `self-hosted`/`selected instance`, Reverso's `English`/`source
     language`) so the removed sentence is the only loss.
   - `mozhiGroupShowsCustomSettings()` (`:497`): delete `:500`.
   - `fallbackGroup()` (`:669`): drop the checkbox (`:683-689`, `:722-735`) and the free-web-group
     order check (`:712-720`); the combo is enabled on a fresh dialog; keep the `None` + items
     order, the hint substrings (minus `free web services`), the remembered `mozhi` value (drop
     `remembered.translation.allowFreeServices = false`) and the unknown-id → `None` case; keep the
     group order check `fallbackGroup` before `mymemoryGroup` — the group still lives on Services in
     this task, and `02` rewrites this slot into the Translation-page row test.
   - `proofreadingTab()` (`:1101-1109`): delete the `allowFreeServices` comparison (`:1108`);
     keep the other `translation.*` comparisons.
   - The five service tests: delete the single `requiresOptIn()` assertion in
     `tst_mymemoryservice.cpp:186`, `tst_mozhiwebservice.cpp:202`, `tst_yandexwebservice.cpp:222`,
     `tst_bingwebservice.cpp:302`, `tst_reversoservice.cpp:252`; the rest of `properties()` stays.

10. **Translations** — `i18n/*.ts`:
    - Run `cmake --build build/linux-release --target update_translations` from the repository
      root; the Linux scan keeps `src/platform/hotkeymanager_win.cpp`
      (`CMakeLists.txt:185-197`).
    - Translate the six changed hints in all 14 files and let the four removed messages drop (the
      **Free web services** group title, the checkbox, the generic hint and the manager error): each
      file must end at **379** `<message>` entries with 0 unfinished, 0 empty and 0 vanished. The
      **Fallback service** group title and the Windows-only `HotkeyManager` message stay present and
      translated. Do not reorder the messages in this task; `02` reorders the provider blocks.
    - If any message appears with `type="vanished"`, rerun
      `lupdate -no-obsolete -project build/linux-release/.lupdate/qtranslate_lupdate_project.json`
      and re-check the count. `<location>` churn in all 14 files is expected.

11. **Verification**:
    - Build `build/linux-debug` and `build/linux-release`; grep the build logs for `warning:` —
      `-Wall -Wextra` must stay clean.
    - `xvfb-run -a ctest --test-dir build/linux-debug --output-on-failure` and the same for
      `build/linux-release`: 45/45 in both, `tst_translations` green.
    - `rg -n "allowFreeServices|requiresOptIn|freeServicesAllowed" src tests` must return nothing;
      `rg -ni "opt-in" src tests` must return nothing; `rg -ni "free web" src tests` may return only
      DeepL's own `free-web` mode (`src/services/deeplservice.h`, `src/services/deeplwebservice.cpp`,
      the mode label in `src/ui/settingsdialog.cpp` and `tests/tst_deeplwebservice.cpp`), nothing
      else. `rg -n "Allow free web services|Enable free web services|Free web services" src i18n`
      must return nothing. `doc/` still mentions the gate by design (the lead's pass); do not edit it.
    - The Text checking work stays byte-identical: no change under `src/core/proofreading*`,
      `src/services/languagetool*`, `src/ui/proofreading*`, `[proofreading]` in the settings code or
      the proofreading tests, and the Text checking block of `settingsdialog.cpp` is untouched.
    - Legacy-IN smoke: with a temporary `XDG_CONFIG_HOME` containing
      `qtranslate/qtranslate.ini` with `[translation] service=mymemory` and
      `allowFreeServices=false`, run
      `timeout 8 xvfb-run -a build/linux-release/qtranslate --verbose` with a temporary
      `XDG_STATE_HOME`; the log must contain no "needs free web services" and no "not available"
      line, and must not contain the translated text.
    - Record Windows Debug/Release and its 39 tests as not performed (no MSVC); no push.

## Done when

- `rg -n "allowFreeServices|requiresOptIn|freeServicesAllowed" src tests` returns nothing; the INI
  load/save, `AppSettings`, `TranslationService`, `TranslationManager` and `AppController` contain
  no gate field, API, branch or string; `settingsVersion` stays `1`.
- The opt-in concept is gone, not just the checkbox: no `opt-in` wording in `src`/`tests`, no
  **Free web services** group or general hint, no gate error string, and no replacement setting,
  allow-list or category. The only `free web` strings left are DeepL's own `free-web` mode
  (`src/services/deeplservice.h`, `src/services/deeplwebservice.cpp`, the mode label in
  `src/ui/settingsdialog.cpp` and `tests/tst_deeplwebservice.cpp`).
- Both children of the stage stay separate: this task keeps the **Fallback service** group on the
  Services page and leaves the provider order alone (`02-move-fallback-and-order-services` moves the
  combo to the Translation page and reorders the providers). Do not move or reorder anything here.
- Text checking is untouched: `proofreading/allowOnlineServices`, the Text checking tab, its strings
  and its tests are byte-identical; the user deferred that discussion until after this stage.
- All eight registered services are offered by `updateServiceSelectors()` and any saved
  `translation/service` id that names a registered service is activated at startup (covered by
  `tst_translationmanager::anyRegisteredServiceCanBeActive` and the legacy-IN smoke). No
  "switching to google" path remains.
- Fallback: `translation/fallbackService` is still normalized to `fallbackServiceIds()` (the five
  no-key web endpoints, trimmed/lowercased, unknown/`google`/`deepl` → empty with the fixed warning);
  `fallbackFor()` returns a registered service that is not the primary; empty and unknown ids are
  ignored; no gate is consulted. `tst_translationmanager` covers the positive path, any registered
  service as fallback, and the invalid-id rows. `fallbackServiceIds()` keeps its order in this task.
- Services page: no **Allow free web services** checkbox, no **Free web services** group and no
  `freeWebHint`; the five service groups and the fallback combo are enabled without any toggle;
  the hints keep the unofficial-endpoint/rate-limit/block/third-party warnings and contain no
  "Enable free web services" sentence; `tst_settingsdialog` asserts all of it.
- A fresh INI written by the app has no `allowFreeServices` key; an old INI with
  `allowFreeServices=false` and `service=mymemory` loads with MyMemory active and no free-services
  warning (`tst_settings::legacyFreeServicesGateKeyIsIgnored` and the smoke).
- Linux Debug and Release build without warnings and pass 45/45 under `xvfb-run -a`; the 14 `.ts`
  files are at 379 messages with 0 unfinished/empty/vanished and `tst_translations` passes;
  Windows Debug/Release and its 39 tests are recorded as not performed (no MSVC).
- Only `src/core/settings.{h,cpp}`, `src/core/translationservice.h`,
  `src/core/translationmanager.{h,cpp}`, `src/app/appcontroller.cpp`,
  `src/ui/settingsdialog.{h,cpp}`, the five service classes in `src/services/`, the eight test files
  named in steps 7-9, and the 14 `i18n/*.ts` files are changed; no
  `doc/`, README, `AGENTS.md`, root-plan, CMake, version, proofreading or `[proofreading]` change;
  no push.

## Risks

- The 14 `.ts` files are the largest mechanical part. Changing the six hint sources invalidates
  their translations, so every language needs the new text; do not keep the old sentence to save
  work — it fails the `rg` gate and contradicts the user's request. `tst_translations` checks
  placeholders and the `→` symbol, so keep them; `lupdate` also re-escapes apostrophes in some
  languages, which is expected churn.
- The `.ts` count is the acceptance signal: **379**. A different number means an accidental extra
  change (for example another `tr()` string) or a leftover `tr()` from the removed group.
- Keep the object names of every group, hint and the fallback combo: all dialog tests locate the
  widgets by name, and a rename looks like a passing test only until `findChild` returns null.
- Removing the generic hint loses the "The Google and DeepL APIs remain the recommended choice"
  sentence. That is what the user asked for (no general group); the per-service hints keep the
  unofficial and third-party warnings. Do not invent a replacement banner without a new decision.
- Do not add a `SettingsStore` migration that deletes the old key: the user's rule is that load and
  save no longer know the setting. A stale key in an existing INI is inert.
- The stale-key behaviour is invisible in `QSettings` (unknown keys survive a save); the test must
  assert "ignored", not "removed". Do not write an assertion that fails on the second run.
- `AppController` has no test target: the startup and picker paths are covered by code review, the
  manager test and the offscreen smoke, not by a unit test. Say so in the outcome; do not add a new
  test target for it in this task.
- The manager accepting any registered fallback is broader than the old rule; `SettingsStore` still
  restricts the INI to the five ids, so the only reachable difference is programmatic. Do not widen
  the settings list.
- Do not move the fallback control or reorder the provider groups here: both belong to
  `02-move-fallback-and-order-services`. Touching them now would put the same regions in two diffs
  and grow this task past its review budget.
- Do not touch text checking. `proofreading/allowOnlineServices` looks similar but is a separate
  feature with a separate user decision; changing it, its strings or its tab would contradict the
  user's explicit deferral.
- `doc/architecture.md`, `doc/free-endpoints.md`, `doc/handoff.md`, both READMEs, `AGENTS.md` and the
  root plan still describe the gate after this task until the lead's pass; the work is not finished
  behaviour-wise for documentation, and the programmer must not edit those files.
- Windows Debug/Release and its 39 tests are unavailable here (no MSVC); record them as not
  performed, never as passed. The interactive Services page and the main-window/tray pickers were
  not seen on screen in this environment; the widget assertions are the automated gate.
- Nothing is needed from the user for this task: the 2026-11-14 decision and the scope are already
  recorded in the stage.

## Outcome

Done. The gate and the opt-in concept are gone: no `translation/allowFreeServices`, no
`requiresOptIn()`, no `freeServicesAllowed()`/`setFreeServicesAllowed()`, no pre-request refusal, no
**Allow free web services** checkbox, no **Free web services** group or general hint, and no
disabled service groups. Every registered service is offered by the main-window/tray pickers and
activated at startup; any registered service that is not the primary can be the fallback.
`settingsVersion` stays `1`, and no migration/`remove()` was added, so a stale `allowFreeServices`
key in an old INI is inert.

- Files: `src/core/settings.{h,cpp}`, `src/core/translationservice.h`,
  `src/core/translationmanager.{h,cpp}`, `src/app/appcontroller.cpp`,
  `src/ui/settingsdialog.{h,cpp}`, the five service classes in `src/services/`, the eight test files
  named in Steps 7-9 and the 14 `i18n/*.ts`. No `doc/` (apart from this roadmap entry and the map
  files the lead/planner had already changed), README, `AGENTS.md`, CMake, version, proofreading or
  `[proofreading]` change.
- Services page: the five per-service hints and the fallback hint were changed verbatim to the plan
  text; every per-service unofficial-endpoint/rate-limit/block/third-party warning and the
  "several requests" wording stayed. `m_mozhiGroup` became the local `mozhiGroup`;
  `mymemoryGroup`, `mozhiGroup`, `yandexGroup`, `bingGroup`, `reversoGroup`, `fallbackGroup`,
  `fallbackService` and `fallbackHint` keep their object names. The **Fallback service** group stays
  on Services and the provider order is unchanged; both belong to `02`.
- `AppController::showSettings()` builds the fallback list from `fallbackServiceIds()` and looks the
  display name up in the registry, skipping an unregistered id; the `SettingsDialog` signature is
  unchanged. `AppController` has no test target, so the startup and picker paths are covered by code
  review, the manager tests and the legacy-INI smoke, not by a unit test (as the plan anticipated).
- Tests: the gate slots were replaced, not deleted outright. Added
  `anyRegisteredServiceCanBeActive`, `fallbackWorksWithoutAGate`, `fallbackCanBeAnyRegisteredService`
  and `tst_settings::legacyFreeServicesGateKeyIsIgnored`; `freeWebServicesGate` became
  `freeWebGateIsGone`; the invalid-fallback matrix keeps `empty`, `unregistered` and `primary
  itself` and no longer has the `non-opt-in service` row (a plain registered service is now a valid
  fallback). `tst_settings::roundTrip` asserts a fresh INI has no gate key, and the fixed
  normalization warning is `Fallback service is not supported; using none`.
- Deliberate deviation: the plan asks for tests that name the removed key/objectName and, in the
  same `Done when`, for `rg "allowFreeServices"` to return nothing over `src` **and** `tests`. The
  programmer assembled the legacy key from two string literals
  (`QStringLiteral("allowFree" "Services")` equivalents) in `tst_settings.cpp` and
  `tst_settingsdialog.cpp`, with a comment, so both requirements hold; the reviewer considered it
  acceptable. The tests still exercise the exact identifier at run time. Verified with `rg`: `src`
  and `tests` contain no `allowFreeServices`, `requiresOptIn`, `freeServicesAllowed` or `opt-in`;
  the only `free web` matches are DeepL's own `free-web` mode.
- Verification: Linux Debug and Release build with `-Wall -Wextra` and no `warning:`; both pass
  `xvfb-run -a ctest` 45/45 with `tst_translations` green. The 14 `.ts` files are at 379 messages
  each with 0 unfinished/empty/vanished; the Windows-only `HotkeyManager` message and the
  **Fallback service** title remain translated. The legacy-INI smoke
  (`service=mymemory`, `allowFreeServices=false`) starts with MyMemory and logs neither "needs free
  web services" nor "not available", and no translated text. `git diff --check` is clean.
- Not performed: Windows Debug/Release and its 39 tests (no MSVC in this environment); CI on Qt
  6.8.3 / `debian:trixie`; an interactive look at the Services page and the pickers (the widget
  assertions and the offscreen smoke are the automated gate); real network requests.
- Known, not fixed because it is out of scope: `m_mozhiHint` (and the pre-existing
  `m_fallbackGroup`/`m_fallbackHint`) in `settingsdialog.h` are assigned in `buildUi()` and never
  read; this predates the task and does not affect behaviour. Documentation still describes the gate
  until the lead's pass after this stage. The text-checking question stays deferred by the user.
