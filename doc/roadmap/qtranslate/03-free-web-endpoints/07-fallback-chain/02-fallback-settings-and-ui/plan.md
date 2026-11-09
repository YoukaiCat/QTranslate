---
kind: task
status: done
detailed: true
depends: [01-fallback-manager]
---

# Fallback setting, dialog and translations

## Task

Expose the manager fallback from `01-fallback-manager` as a user setting: the
`translation/fallbackService` INI key, a gate-aware **Fallback service** group in the Services page,
the `AppController` wiring, the two settings tests, the 14 `.ts` files, and the live smoke. This
task changes no service policy: the triggers, the one-hop rule, the primary-error rule and the cache
behaviour are already implemented and tested in `01-fallback-manager`.

`AppSettings::TranslationSettings` has no fallback field yet (`src/core/settings.h:29-40`), and
`SettingsStore` does not read or write one (`src/core/settings.cpp:361-381`, `:505-515`). The
Services page builds the free-web group at `src/ui/settingsdialog.cpp:301-314`, then the MyMemory,
Mozhi, Yandex, Bing and Reverso groups; `loadValues()` ends with the four gate updates
(`:592-596`) and `settings()` maps the widgets back to `AppSettings` (`:599-637`). `AppController`
constructs the dialog with the active service's language lists (`src/app/appcontroller.cpp:688-699`)
and applies the saved settings in `applySettings()` (`:541-664`).

Design decisions:

- `translation/fallbackService` is a string, default empty (empty means no fallback). The valid
  values are the five opt-in ids: `mymemory`, `mozhi`, `yandex-web`, `bing-web`, `reverso`. It is
  trimmed and lowercased on load/save; an unknown id or a non-opt-in id (for example `google` or
  `deepl`) becomes empty, so a stale INI cannot break startup. Do not bump `settingsVersion`: the
  key is optional and an old file loads the default, like the `[mozhi]` group.
- Add a pure `QStringList fallbackServiceIds()` helper next to `mozhiEngineIds()`
  (`src/core/settings.h:116-119`, `src/core/settings.cpp:227-235`) and use it in `normalize()`
  (`settings.cpp:128-211`). This is a second guard only: the manager still ignores an unknown or
  non-opt-in id at request time. The helper and the registered services must stay in sync when a
  service is added or removed.
- `SettingsDialog` must still know nothing about services. Add a constructor parameter
  `const QList<QPair<QString, QString>> &fallbackServices = {}` after `autostartApproved` (before
  `secretStorageFallback`, so the existing four-argument test calls keep compiling); `AppController`
  builds the list from `m_translations->services()` filtered by `requiresOptIn()` and passes each
  `(id, displayName)` pair. The one existing five-argument test call in `tst_settingsdialog.cpp`
  (`secretStorageHint()`, `:715-716`) must be updated to pass the list before its `true`.
- The dialog adds a `QGroupBox` titled `tr("Fallback service")` after the free-web group (before
  MyMemory), object name `fallbackGroup`, with a `QComboBox` object name `fallbackService` containing
  `tr("None")` with empty data and one item per registered opt-in service (display name as the
  label, id as the data), plus a word-wrapped `tr()` hint object name `fallbackHint`. The hint states
  that a retryable failure (no connection, timeout, rate limit, quota or invalid key) sends the text
  once more to the configured fallback, that free web services must be enabled, that `None` turns it
  off, and that the text is sent to a third party. The combo is disabled while
  `m_allowFreeServices` is off, but the remembered value is kept and saved; re-enabling the gate
  reuses it. Connect the gate toggle to the new `updateFallbackControls()` and call it at the end of
  `loadValues()`.
- `loadValues()` selects the combo with `findData(settings.translation.fallbackService)` and falls
  back to index 0 (`None`); `settings()` writes `m_fallbackService->currentData().toString()` back.
  `validate()` needs no change.
- `AppController::createTranslation()` calls `m_translations->setFallbackService(...)` at startup
  (after `setFreeServicesAllowed`, `appcontroller.cpp:229`); `applySettings()` calls it when the
  value changes (the manager clears its cache). The gate-off branch already calls
  `setFreeServicesAllowed(false)`, which clears the cache in `01-fallback-manager`; do not add a
  second reset. `showSettings()` builds the opt-in list described above.
- Strings: exactly three new messages — `Fallback service`, `None` and the hint — so the `.ts` count
  moves from 313 to 316. No new test target: Linux stays at 39 tests, Windows at 34.

## Steps

1. **Settings data model** — add the field to `src/core/settings.h`:
   - `TranslationSettings` (`:29-40`) gets `QString fallbackService;` (empty default).
   - Declare `QStringList fallbackServiceIds();` next to `mozhiEngineIds()` (`:116-119`), with a
     comment that the list is a settings-layer guard and must track the registered opt-in services.

2. **`fallbackServiceIds()` and normalization** — `src/core/settings.cpp`:
   - Implement `fallbackServiceIds()` next to `mozhiEngineIds()` (`:227-235`) returning
     `{"mymemory", "mozhi", "yandex-web", "bing-web", "reverso"}` in that order.
   - In `normalize()` (`:128-211`), trim and lowercase `fallbackService`. If the result is empty,
     keep it empty (valid: no fallback). If it is not in `fallbackServiceIds()`, log a fixed
     `qCWarning(lcSettings)` that does not echo the typed value and set it to empty; otherwise store
     the normalized id. Do not touch `settingsVersion`.

3. **`SettingsStore` persistence** — `src/core/settings.cpp`:
   - `load()`: read `translation/fallbackService` inside the existing `translation` group
     (`:361-381`) with an empty-string default.
   - `save()`: write the normalized value inside the `translation` group (`:505-515`), so the key is
     always present and a stale INI is cleaned on the next save.

4. **`SettingsDialog` signature** — `src/ui/settingsdialog.h`:
   - Add `const QList<QPair<QString, QString>> &fallbackServices = {}` after `autostartApproved`
     and before `secretStorageFallback` in the constructor (`:27-29`). Store it in a new
     `QList<QPair<QString, QString>> m_fallbackServices` member, and add `<QList>`/`<QPair>` to the
     header includes so the signature compiles independently of `QStringList`'s transitive includes.
   - Add forward-declared `QGroupBox *m_fallbackGroup = nullptr;`, `QComboBox *m_fallbackService =
     nullptr;`, `QLabel *m_fallbackHint = nullptr;` next to the other free-web members
     (`:119-132`), and declare `void updateFallbackControls();` next to the other
     `update*Controls()` methods (`:69-76`).

5. **`SettingsDialog` UI** — `src/ui/settingsdialog.cpp`, `buildUi()`:
   - After the free-web group (`:301-314`) and before MyMemory (`:316`), create
     `m_fallbackGroup = new QGroupBox(tr("Fallback service"), servicesContent)` with object name
     `fallbackGroup`.
   - Inside it: `m_fallbackService = new QComboBox`, object name `fallbackService`, with
     `addItem(tr("None"), QString())` first, then one `addItem(displayName, id)` per
     `m_fallbackServices` entry in the given order.
   - Add a word-wrapped `QLabel` hint, object name `fallbackHint`, using one `tr()` string that
     names the retryable failure kinds, requires the free-web gate, explains `None`, and warns that
     the text is sent to a third party.
   - Append the group to `servicesLayout` and `connect(m_allowFreeServices, &QCheckBox::toggled,
     this, &SettingsDialog::updateFallbackControls)`.

6. **`SettingsDialog` values and enabling** — `src/ui/settingsdialog.cpp`:
   - Implement `updateFallbackControls()`: `m_fallbackService->setEnabled(
     m_allowFreeServices->isChecked())` and keep the current index untouched (remembered value).
   - In `loadValues()`, select `m_fallbackService->findData(settings.translation.fallbackService)`
     and fall back to index `0` when it is `-1`; call `updateFallbackControls()` at the end next to
     the four existing gate updates (`:592-596`). Do not clear the stored value when the gate is
     off.
   - In `settings()` (`:599-637`), set
     `result.translation.fallbackService = m_fallbackService->currentData().toString()`.
   - Leave `validate()` unchanged.

7. **`AppController` wiring** — `src/app/appcontroller.cpp`:
   - In `showSettings()` where the dialog is constructed (`:688-699`), build a
     `QList<QPair<QString, QString>>` from `m_translations->services()`, keeping services whose
     `requiresOptIn()` is true, preserving registration order, and pass it to the dialog.
   - In `createTranslation()`, after the existing `setFreeServicesAllowed(...)` call (`:229`), call
     `m_translations->setFallbackService(settings.translation.fallbackService)`.
   - In `applySettings()`, call `setFallbackService(...)` when the value differs from the current
     manager value; rely on the manager's own cache clear. Do not add a second reset in the gate-off
     branch.

8. **`tst_settings`** — `tests/tst_settings.cpp`:
   - Extend `roundTrip()` (`:198-324`): set `settings.translation.fallbackService =
     QStringLiteral("mymemory")`, assert it survives `save()`/`load()`, and assert the INI contains
     `translation/fallbackService=mymemory`.
   - Extend `defaults()` (`:78`) and/or `missingFileUsesDefaults()` (`:361`) with an empty
     `fallbackService`; keep the `settingsVersion` assertion in `defaults()` at `1`.
   - Add a normalization test (dedicated slot or an extension of `corruptValuesAreNormalized()`,
     `:374-413`): write raw `translation/fallbackService` values `"  Mozhi "` (→ `mozhi`),
     `"nope"` (→ empty) and `"google"`/`"deepl"` (→ empty); for the unknown values use
     `QTest::ignoreMessage(QtWarningMsg, …)` with the fixed warning text and assert the typed value
     is absent from the message.
   - Keep the existing `tst_settings` slots green; no new test executable or target.

9. **`tst_settingsdialog`** — `tests/tst_settingsdialog.cpp`:
   - Update the five-argument call at `secretStorageHint()` (`:715-716`) — it currently ends with
     `true, true` — by inserting the fallback list (empty `{}` is enough) between the two `true`
     values, so `autostartApproved` and `secretStorageFallback` keep their meaning.
   - Add a `fallbackGroup()` case that locates `fallbackGroup`/`fallbackService`/`fallbackHint` with
     `findChild<>` (like `reversoGroup()`, `:604-648`) and constructs the dialog with a fixed list
     (`{{"mymemory", "MyMemory"}, {"mozhi", "Mozhi"}}`): the combo has `None` at index 0 plus the
     supplied items in order; a fresh dialog defaults to `None`; toggling the free-services gate
     changes `isEnabled()` but keeps the selected index; a remembered id is restored even while the
     gate is off and is returned unchanged by `settings()`; an unknown configured id (`"nope"`)
     selects `None` and is returned as empty. Assert the group sits after the free-web group and
     before `mymemoryGroup` via the parent layout index.

10. **Translations** — `i18n/*.ts`:
    - Run `lupdate` with the same source list as `update_translations`, which must still include
      `src/platform/hotkeymanager_win.cpp` for the Linux scan.
    - Add exactly three messages (`Fallback service`, `None`, the hint) to all 14 files; the count
      must be 316 with no unfinished, empty or vanished entries, and the Windows-only hotkey message
      must remain translated. Run `lrelease`; finish with `tst_translations`.

11. **Live smoke (manual record)** — enable the free-web gate and set a configured fallback:
    - Use an unconfigured official primary (DeepL with an empty key) or an intentionally invalid
      official key, set the fallback to Mozhi (`sourceLang=auto`) or MyMemory with an explicit
      source (MyMemory rejects `auto`), translate a short text, and confirm that the result comes
      from the fallback and that the log/UI names the fallback service without the text.
    - Record the check as passed, or as not performed with the reason if the network or the free
      endpoint is unavailable. Do not add an automated test that contacts a real provider.

## Done when

- `translation/fallbackService` is loaded, saved and normalized: the default is empty; `mymemory`
  round-trips; leading/trailing spaces and case are normalized; an unknown id and a non-opt-in id
  become empty; `settingsVersion` stays `1`; a fixed warning is logged for an unknown id and does
  not echo the typed value.
- The Services page has the **Fallback service** group after the free-web group with `None` and the
  registered opt-in services, the gate-aware hint, and no other input fields; the combo is disabled
  while the gate is off and still shows and saves the remembered value; an unknown configured id
  shows `None` and is saved as empty. The dialog is still constructed without service knowledge.
- `AppController` passes the registered opt-in services to the dialog, applies the setting at
  startup and on change, and the gate-off path uses the primary only with a cleared cache.
- `tst_settings` covers the default, the round trip and the normalization; `tst_settingsdialog`
  covers the combo contents and order, the default `None`, the disabled state, the remembered value
  and the unknown-id cleanup; the existing cases stay green. Linux is 39/39 in Debug and Release
  without warnings; Windows 34 is derived, not run.
- All 14 `.ts` files are complete at 316 messages (no unfinished, empty or vanished messages, and the
  Windows-only hotkey message stays), `update_translations` keeps
  `src/platform/hotkeymanager_win.cpp` in the Linux scan, and `tst_translations` passes.
- The live smoke is recorded: with the gate on and an unconfigured official primary (DeepL with an
  empty key) or an intentionally invalid official key, a short text translates through the fallback
  and the log/UI names the fallback service; use Mozhi (supports `auto`) or MyMemory with an explicit
  source language, because MyMemory rejects `auto`. If the network or the free endpoint is
  unavailable, record the check as not performed with the reason. No real provider is contacted by
  an automated test.
- Only `src/core/settings.{h,cpp}`, `src/ui/settingsdialog.{h,cpp}`, `src/app/appcontroller.cpp`,
  `tests/tst_settings.cpp`, `tests/tst_settingsdialog.cpp` and the 14 `i18n/*.ts` files are changed;
  no service policy, `doc/`, README, `doc/handoff.md` or root-plan file is touched; commit and push
  are the coordinator/lead's.

## Risks

- The static `fallbackServiceIds()` list is a settings-layer guard, while the combo is built from
  the registered services. A future service must be added to both, or the dialog would offer an id
  that `SettingsStore` normalizes away on save. Keep the two lists in sync and note it in a comment.
- The constructor parameter order matters: `AppController` passes its arguments positionally, and
  `secretStorageHint()` currently passes `secretStorageFallback` as the last argument of its
  five-argument call. Add the list before `secretStorageFallback` and update both call sites;
  otherwise a `bool`/list mismatch is a compile error or, worse, a silently wrong argument.
- Keep the fallback value when the gate is off. Clearing it would lose the user's choice; the policy
  is "remember it, do not use it".
- MyMemory rejects `auto` with `UnsupportedLanguage`; a smoke that leaves `sourceLang=auto` would
  look like a fallback failure. Use Mozhi or an explicit source for the smoke.
- The `.ts` hint is long; translate it in all 14 files and do not let `lupdate` mark the Windows-only
  hotkey message as vanished. A missing translation fails `tst_translations`.
- The smoke needs network access and an available free endpoint; record it as not performed with the
  reason when unavailable, never as passed. Do not start a release or push.

## Outcome

- Implemented `translation/fallbackService` in `AppSettings`/`SettingsStore`: empty default,
  the five valid ids, trimmed/lowercased, unknown or non-opt-in ids become empty with a fixed
  warning that does not echo the typed value; `settingsVersion` stays `1`. `fallbackServiceIds()` is
  the settings-layer guard; the manager's `fallbackFor()` remains the request-time guard.
- Added the gate-aware **Fallback service** group in the Services page after the free-web group and
  before MyMemory: `None` plus the supplied opt-in services, a word-wrapped hint, and a combo that is
  disabled while the gate is off but keeps and returns the remembered value. The dialog still
  receives no service knowledge.
- Wired `AppController`: `setFallbackService()` at startup and on change; the opt-in list is built
  from `m_translations->services()` and passed to the dialog. The gate-off branch keeps the existing
  cache clear and no second reset is added.
- Extended `tst_settings` (default, round trip, normalization) and `tst_settingsdialog` (contents,
  order, default, disabled state, remembered value, unknown id) without a new target.
- Added the three strings to all 14 `.ts` files: 316 messages, no unfinished/empty/vanished; the
  Windows-only hotkey source stayed in the `lupdate` scan and its message translated. The large
  `.ts` diff is `<location>` churn from `lupdate`; no translation content was lost.
- Verification: Linux Debug and Release 39/39 with no warnings; `tst_settings` 46,
  `tst_settingsdialog` 28, `tst_translations` 4. Windows 34 was not performed (no MSVC). The live
  smoke was not performed: there is no interactive GUI or real provider/free endpoint in this
  environment. The pre-existing cache-generation race in `TranslationManager` is not caused by this
  task and remains open.
- No service policy, docs, READMEs, `doc/handoff.md`, root plan or CMake was touched.
