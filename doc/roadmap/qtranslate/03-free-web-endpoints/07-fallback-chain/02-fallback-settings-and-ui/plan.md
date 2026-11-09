---
kind: task
status: todo
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
- The constructor parameter order matters: `AppController` passes seven arguments positionally and
  one test passes `secretStorageFallback` as the fifth. Add the list before
  `secretStorageFallback` and update both call sites; otherwise a `bool`/list mismatch is a compile
  error or, worse, a silently wrong argument.
- Keep the fallback value when the gate is off. Clearing it would lose the user's choice; the policy
  is "remember it, do not use it".
- MyMemory rejects `auto` with `UnsupportedLanguage`; a smoke that leaves `sourceLang=auto` would
  look like a fallback failure. Use Mozhi or an explicit source for the smoke.
- The `.ts` hint is long; translate it in all 14 files and do not let `lupdate` mark the Windows-only
  hotkey message as vanished. A missing translation fails `tst_translations`.
- The smoke needs network access and an available free endpoint; record it as not performed with the
  reason when unavailable, never as passed. Do not start a release or push.
