---
kind: task
status: done
detailed: true
---

# Remove the online text checking gate

## Task

Remove `proofreading/allowOnlineServices` and the whole idea of a second privacy gate, per the
user's 2026-11-14 decision: the **Enable text checking** checkbox is the only consent, so every
configured provider — the public LanguageTool endpoint and a self-hosted one alike — may check as
soon as the feature is enabled. The setting, its INI pair, the `ProofreadingManager` flag and its
refusal, the **Online checking** group on the **Text checking** page, the `AppController` sync and
`ProofreadingService::isLocal()` (its only caller is the removed branch) go away. No replacement
gate, per-provider consent, allow-list or category is introduced.

This is one task because the field, the load/save pair, the manager flag, the dialog group, the
`AppController` sync, the virtual `isLocal()` and the tests break the build or leave dead UI if
separated, and the `.ts` change is atomic with the strings.

The same task closes the consent hole the deletion would otherwise open: the popup check
(`AppController::proofreadSelectionInPlace()`, reached by the `ProofreadSelection` hotkey and by the
`--proofread-selection` command) is now gated on `proofreading.enabled`. The refusal happens before
the selection grab, so nothing is copied and no request is made while the feature is off.

Out of scope, in the next children: `proofreading/checkBeforeTranslation` and
`proofreading/autoReplace` with the main-window pre-translation flow
(`02-remove-pre-translation-flow`), and the second hotkey with the check-and-replace flow
(`04-check-and-replace-hotkey`). Documentation (`doc/proofreading.md`, `doc/architecture.md`,
`doc/handoff.md`, both READMEs, `AGENTS.md`, the root plan) is the lead's pass after the code and is
not touched here.

Context (architecture: `doc/architecture.md` §4.11 for the settings keys, §4.15 for the checking
module; feature notes: `doc/proofreading.md` §5). The
gate is a `bool` field with a load/save pair and a manager branch
(`src/core/settings.h:77`; `src/core/settings.cpp:550-553`, `:694-696`), a manager flag/API
(`src/core/proofreadingmanager.h:35-38`, `:57`; `.cpp:128-133`, `:157-160`), a virtual
`ProofreadingService::isLocal()` and its `LanguageToolService` override
(`src/core/proofreadingservice.h:66-70`; `src/services/languagetoolservice.h:28`, `.cpp:371-374`),
two `AppController` calls (`src/app/appcontroller.cpp:288`, `:654`), the **Online checking** group
and its three widgets (`src/ui/settingsdialog.h:152-154`; `src/ui/settingsdialog.cpp:530-544`,
`:549-551`, `:729`, `:799`, `:884`, `:891`) and five test files.

Design decisions:

- Delete the setting; do not default it to `true`. No migration and no `remove()` call: a stale
  `allowOnlineServices` key in an old INI is never read or written again and stays inert in that
  file. `settingsVersion` stays `1`.
- Delete `ProofreadingService::isLocal()` instead of keeping it for a future local engine. After the
  manager branch goes, nothing calls it, and an interface method with no caller is dead weight; a
  local engine can reintroduce it together with its caller. Rejected: keeping it with a comment.
- The manager keeps the rest of its pre-network order unchanged: empty text → `EmptyText`, no active
  service → `ServiceUnavailable`, `isConfigured()` false → `Auth` (with the provider hint when it
  has one), unsupported/`auto` language → `UnsupportedLanguage`, otherwise the provider. Only the
  gate branch disappears; the `cancel()`/channel/logging behaviour is untouched.
- The dialog keeps the enable switch, the intro hint, the **Provider** group (provider, checking
  language, LanguageTool URL, hint, URL warning) and the **Issues** group. Only the **Online
  checking** group goes. `updateProofreadingControls()` keeps disabling the Provider/Issues groups
  and the endpoint row when the switch is off; delete only the two Online-group lines and reword its
  comment, which still talks about a switch that is not a disabling gate.
- Keep the endpoint hint sentence `A self-hosted server keeps the text on your machine; the public
  service has rate limits and sees the checked text.` It describes the endpoint and what the user is
  consenting to, not a gate.
- Do not change the **Text checking** intro hint (`Finds spelling, grammar, and punctuation errors in
  a separate mode and does not change translation settings.`): it already describes the standalone
  feature and says nothing about checking before a translation.
- The refusal string in `AppController` is exactly
  `Enable text checking in Settings → Text checking.` It replaces the removed manager sentence, and
  child `04` reuses the same literal in the same translation context for the check-and-replace
  action, so write it once, exactly like this.
- Gate placement in `proofreadSelectionInPlace()`: right after `if (!m_proofreading) return;` and
  before the `m_grabber->isBusy()` guard, so a disabled feature never touches the clipboard, never
  logs a grab conflict and never reaches the network. `HotkeyManager::activated` and
  `executeCommand()` both call this one method, so one check covers the hotkey and the CLI.
- Keep `proofreading/hotkey`, the `ProofreadingSelection` action and the popup flow otherwise
  byte-identical: the second hotkey and the check-and-replace flow are child `04`.

## Steps

1. **Settings model** — `src/core/settings.h:67-80`, `src/core/settings.cpp:544-555`, `:685-698`:
   - Delete `ProofreadingSettings::allowOnlineServices` (`settings.h:77`).
   - Delete the load block (`settings.cpp:550-553`) and the save block (`:694-696`).
   - Keep `enabled`, `provider`, `language`, `endpoint`, `categories`, `checkBeforeTranslation`,
     `autoReplace` and `hotkey` exactly as they are: the first two booleans belong to child `02`,
     and removing them here would grow this task. `settingsVersion` stays `1`, no `remove()`.

2. **Provider interface** — `src/core/proofreadingservice.h:66-70`,
   `src/services/languagetoolservice.h:28`, `src/services/languagetoolservice.cpp:371-374`:
   - Delete the virtual `bool isLocal() const { return false; }` and its comment.
   - Delete the `bool isLocal() const override;` declaration and the `isLocal()` definition.
   - Nothing else in the interface changes; `id()`, `displayName()`, `isConfigured()`,
     `configurationError()`, `languages()`, `supportsAutoLanguage()`, `applySettings()` and
     `check()` stay.

3. **Manager** — `src/core/proofreadingmanager.h:35-38`, `:57`,
   `src/core/proofreadingmanager.cpp:128-133`, `:157-160`:
   - Delete `onlineServicesAllowed()`, `setOnlineServicesAllowed()`, `m_onlineServicesAllowed` and
     the comment above them.
   - Delete the branch `else if (!m_active->isLocal() && !m_onlineServicesAllowed)` with
     `tr("Enable online text checking in Settings → Text checking.")` (`:157-160`). The following
     language branch and the `else` that calls `m_active->check(request)` stay adjacent.
   - Do not touch `cancel()`/`cancelAll()`, the reply logging or the `Channel` enum.

4. **Settings dialog** — `src/ui/settingsdialog.h:152-154`, `src/ui/settingsdialog.cpp:530-544`,
   `:549-551`, `:729`, `:799`, `:875-892`:
   - Delete the whole `m_proofreadingOnlineGroup` block: group box, `m_proofreadingAllowOnline`
     checkbox, `m_proofreadingOnlineWarning` label, the two `addWidget()` calls and the
     `textCheckingLayout->addWidget(m_proofreadingOnlineGroup)` line. Nothing replaces it.
   - Delete the connect of `m_proofreadingAllowOnline`'s `toggled` signal to
     `updateProofreadingControls()` (`:550-551`). Keep the connects of the enable switch, provider
     and endpoint.
   - `loadValues()`: delete `m_proofreadingAllowOnline->setChecked(...)` (`:729`).
   - `settings()`: delete `result.proofreading.allowOnlineServices = ...` (`:799`).
   - `updateProofreadingControls()` (`:875-892`): delete
     `m_proofreadingOnlineGroup->setEnabled(enabled)` (`:884`) and
     `m_proofreadingOnlineWarning->setVisible(!m_proofreadingAllowOnline->isChecked())` (`:891`);
     keep the Provider/Issues group disabling, the provider/endpoint enabling and the endpoint
     warning. Reword the comment (`:877-879`) to: the mode switch disables the page except itself.
   - Header: delete the three members (`:152-154`). Keep every remaining object name: the tests
     locate the widgets by name.

5. **`AppController`** — `src/app/appcontroller.cpp:288`, `:654`, `:1095-1104`:
   - Delete `m_proofreading->setOnlineServicesAllowed(settings.proofreading.allowOnlineServices)`
     (`:288`) and the same call in `applySettings()` (`:653-655`). Keep the rest of the
     `if (m_proofreading)` block (`:656-661`), including the provider switch and the
     `closeProofreadingPopup()` when the mode is turned off.
   - In `proofreadSelectionInPlace()`, after `if (!m_proofreading) return;`, add:
     read the current settings (same pattern as the rest of the method), and if
     `!settings.proofreading.enabled`, call
     `showProofreadingMessage(tr("Enable text checking in Settings → Text checking."))` and return.
     Do not start the grab, do not capture the foreground window, do not cancel other replies.
   - `updateProofreadingUi()` (`:890-898`) keeps `setProofreadingAvailable(available && enabled)`
     and stays otherwise untouched; `setProofreadingOptions()` belongs to child `02`.

6. **Tests** —
   - `tests/tst_settings.cpp`: delete `QVERIFY(!settings.proofreading.allowOnlineServices)` (`:153`),
     the assignment (`:429`) and the assertion (`:446`), and the assignment/assertion pair
     (`:595`, `:606`). Add `removedOnlineGateKeyIsIgnored()`: write a raw INI with
     `settingsVersion=1`, `proofreading/enabled=true` and `proofreading/allowOnlineServices=true`;
     after `load()` `enabled` is true, `checkBeforeTranslation`/`autoReplace` keep their defaults,
     no warning is collected (reuse the existing warning collector), and a `save()`/`load()` round
     trip still loads. Assert "ignored", not "removed": `QSettings` keeps unknown keys, so the stale
     line may remain in the file.
   - `tests/tst_proofreadingmanager.cpp`: delete the `local` flag and the `isLocal()` override from
     `FakeProofreadingService` (`:71`) and all ten `manager.setOnlineServicesAllowed(true)` calls
     (`:195`, `:238`, `:288`, `:323`, `:365`, `:405`, `:447`, `:461`, `:482`, `:501`). Replace
     `onlineGateRefusesRemoteProviders()` (`:267`) and `localProviderWorksWithTheGateOff()` (`:299`)
     with one `anyProviderChecksWithoutAGate()`: a remote fake checks without any flag, exactly one
     request reaches the service, and the reply carries its `serviceId`.
   - `tests/tst_languagetoolservice.cpp:310`: delete `QVERIFY(!m_service->isLocal())`.
   - `tests/tst_proofreadingpanel.cpp:232-234`: the neutral error case uses the literal
     `Enable online text checking in Settings → Text checking.` as an arbitrary failed-check message.
     Replace it with `No text checking provider is available.` (an existing manager message) so the
     removed gate sentence does not survive in a test. Do not otherwise touch this test here: its
     rewrite for the checking window is child `03`.
   - `tests/tst_settingsdialog.cpp`:
     - `proofreadingTab()` (`:877`): delete `settings.proofreading.allowOnlineServices = true`
       (`:887`) and `QVERIFY(result.proofreading.allowOnlineServices)` (`:959`).
     - `proofreadingHotkeyIsIndependentOfMode()` (`:974`): reword the comment (`:985-986`). The row
       stays editable in both modes because a combination can be assigned before the feature is
       enabled, while the action itself does nothing until the switch is on.
     - Rename `proofreadingGateAndEndpoint()` (`:992`) to `proofreadingTabHasNoGate()`: delete the
       `online`/`onlineWarning` lookups and every assertion about them (`:1015-1042`), assert that
       `findChild<QGroupBox *>("proofreadingOnlineGroup")`,
       `findChild<QCheckBox *>("proofreadingAllowOnline")` and
       `findChild<QLabel *>("proofreadingOnlineWarning")` return null, and keep the group-disabling
       checks for the Provider/Issues groups, the URL validation (invalid URL → warning + empty
       normalized endpoint, `validate()` error text) and the "the page never writes a
       `translation.*` value" block. `proofreadingOptionsGroup` assertions stay here until child
       `02` removes that group.

7. **Translations** — `i18n/*.ts`: run
   `cmake --build build/linux-release --target update_translations` and translate the one new
   message, `Enable text checking in Settings → Text checking.` (context `AppController`), in all 14
   files. The four removed messages drop: `Online checking` (group title), `Allow online text
   checking`, the online warning and the `ProofreadingManager` gate sentence. Expected count
   **379 → 376** `<message>` entries per file with 0 unfinished, 0 empty and 0 vanished. `<location>`
   churn is expected; if a message appears with `type="vanished"`, rerun with
   `lupdate -no-obsolete -project build/linux-release/.lupdate/qtranslate_lupdate_project.json`.
   The **Options** group strings stay in this task (child `02`).

8. **Verification**:
   - Build `build/linux-debug` and `build/linux-release`; no `warning:` in either log
     (`-Wall -Wextra`). Use the `qtranslate-trixie` image for all task builds and tests; the lead
     runs the final Debug/Release check on the host after the stage (AGENTS.md).
   - `xvfb-run -a ctest --test-dir build/linux-debug --output-on-failure` and the same for
     `build/linux-release`: 45/45, `tst_translations` green, `tst_proofreadingmanager`,
     `tst_settings`, `tst_settingsdialog`, `tst_languagetoolservice` green.
   - `rg -n "allowOnlineServices|onlineServicesAllowed|isLocal" src` returns nothing. In `tests` the
     only match may be the gate key inside the new `removedOnlineGateKeyIsIgnored()`.
   - `rg -n "Allow online text checking|Enable online text checking|Online checking" src tests i18n`
     returns nothing; `rg -n "proofreadingOnlineGroup|proofreadingAllowOnline" src tests` returns
     nothing.
   - Manual/offscreen smoke of the gate: run the app with a temporary `XDG_CONFIG_HOME` whose INI
     has `[proofreading] enabled=false` (default) and invoke `--proofread-selection`; the app must
     show the new message and the log must contain no request line. Not a network test.
   - Record Windows Debug/Release and its 39 tests as not performed (no MSVC here); no push.

## Done when

- `rg -n "allowOnlineServices|onlineServicesAllowed|isLocal" src` returns nothing; the INI
  load/save, `AppSettings`, `ProofreadingService`, `LanguageToolService`,
  `ProofreadingManager`, `SettingsDialog` and `AppController` contain no gate field, API, branch or
  string, and `ProofreadingManager` has no `onlineServicesAllowed`/`setOnlineServicesAllowed`.
- `ProofreadingService` has no `isLocal()` and `LanguageToolService` no override; the manager's
  pre-network order is empty text → no provider → `isConfigured()` → language → provider, and
  `tst_proofreadingmanager::anyProviderChecksWithoutAGate` proves a remote provider checks with no
  extra flag.
- The **Text checking** page has the enable switch, the intro hint, the **Provider** and **Issues**
  groups only; `proofreadingOnlineGroup`, `proofreadingAllowOnline` and `proofreadingOnlineWarning`
  do not exist, and `tst_settingsdialog::proofreadingTabHasNoGate` asserts it.
- An old INI with `proofreading/allowOnlineServices=true` loads without that key and without a
  warning; `settingsVersion` stays `1`; the key is ignored, not migrated or deleted.
- While `proofreading.enabled` is off, `proofreadSelectionInPlace()` shows
  `Enable text checking in Settings → Text checking.` and returns before the selection grab; nothing
  is copied and no request is made. The CLI `--proofread-selection` shares that entry point and is
  gated the same way (covered by code review: `AppController` has no test target, and
  `tst_proofreading_selection_x11` does not build it).
- The **Options** group, `checkBeforeTranslation`, `autoReplace`, the main-window pre-translation
  flow and the second hotkey are untouched; the popup check, its replacement button, the manual
  main-window **Check** panel (still inside `MainWindow` until child
  `03-extract-proofreading-window` moves it out) and `proofreading/hotkey` behave as before.
- Linux Debug and Release build without warnings and pass 45/45 under `xvfb-run -a`; the 14 `.ts`
  files are at **376** messages with 0 unfinished/empty/vanished and `tst_translations` passes;
  Windows Debug/Release and its 39 tests are recorded as not performed.
- Only `src/core/settings.{h,cpp}`, `src/core/proofreadingservice.h`,
  `src/core/proofreadingmanager.{h,cpp}`, `src/services/languagetoolservice.{h,cpp}`,
  `src/ui/settingsdialog.{h,cpp}`, `src/app/appcontroller.cpp`,
  `tests/tst_settings.cpp`, `tests/tst_settingsdialog.cpp`, `tests/tst_proofreadingmanager.cpp`,
  `tests/tst_languagetoolservice.cpp`, `tests/tst_proofreadingpanel.cpp` and the 14 `i18n/*.ts`
  files change; no `doc/`, README, `AGENTS.md`, root-plan, CMake, version or translation-service
  change; no push.

## Risks

- The enable gate must sit before the grab. If it is placed after `m_grabber->grab()` or inside the
  callback, the app has already sent Ctrl+C and read the clipboard with the feature off — the exact
  reason the gate exists. Check the order in the diff, not only in the test.
- The gate is not unit-tested: `AppController` has no test target and the X11 integration test uses
  `ProofreadingPopupWindow` and `SelectionGrabber` directly. The offscreen gate smoke and the code
  review are the coverage; say so in the outcome instead of adding a new test target.
- Do not default `allowOnlineServices` to `true` or keep a hidden field: the user rejected the
  concept, and a hidden setting is the "dead setting and dead code" case already rejected in stage
  `08`.
- `isLocal()` removal and the test fake must land in the same commit: `FakeProofreadingService`
  declares an override, and removing the virtual without the fake breaks `tst_proofreadingmanager`
  compilation.
- Keep the object names of every surviving widget. `tst_settingsdialog` finds groups and inputs by
  name, and a rename looks green until `findChild` returns null.
- Do not touch the **Options** group or the two option booleans here. They are child `02`; touching
  them now puts the same regions into two diffs and makes the `.ts` count (376) meaningless as an
  acceptance signal.
- The `.ts` count is the scope check: **376** = 379 − 4 removed + 1 added. A different number means
  an extra `tr()` string or a leftover Online-group string. Do not keep the old English sentence to
  avoid translating the new one — `tst_translations` fails on unfinished messages.
- The dialog's `updateProofreadingControls()` still enables/disables the Provider, Issues and
  Options groups, the provider and endpoint rows and shows the endpoint warning after this task;
  deleting the whole function or the endpoint warning is out of scope and would break the
  URL-validation test.
- `doc/proofreading.md`, `doc/architecture.md`, `doc/handoff.md`, both READMEs, `AGENTS.md` and the
  root plan still describe the online gate until the lead's pass; the programmer must not edit them.
- Windows Debug/Release and its 39 tests are unavailable here (no MSVC); record them as not
  performed, never as passed. The page was not inspected interactively; the widget assertions are
  the automated gate.
- Nothing is needed from the user: the 2026-11-14 decision and the scope are recorded in the stage.

## Outcome

Done. `proofreading/enabled` is the only consent for text checking; no second gate, per-provider
consent or replacement setting was introduced.

- Settings, provider and manager: `allowOnlineServices` with its INI pair,
  `onlineServicesAllowed()`/`setOnlineServicesAllowed()` with the field, the `isLocal()` virtual and
  its `LanguageToolService` override, the **Online checking** group and the two `AppController`
  syncs are gone. `settingsVersion` stays `1`; a stale key is ignored, neither migrated nor removed.
- Consent gate: `AppController::proofreadSelectionInPlace()` reads the current settings right after
  the `m_proofreading` guard and returns with
  `Enable text checking in Settings → Text checking.` before the grab-busy check, the popup reset,
  the foreground-window capture and `SelectionGrabber::grab()`. The hotkey and
  `--proofread-selection` share this method, so both entry points are covered. `AppController` has
  no test target; the gate is covered by the offscreen smoke and the code review, as planned.
- Tests: `tst_settings` gained `removedOnlineGateKeyIsIgnored()` (raw INI with the stale key, no
  warning collected, `save()`/`load()` round trip; the stale line may stay in the file);
  `tst_proofreadingmanager` replaced its two gate tests with `anyProviderChecksWithoutAGate()`;
  `tst_settingsdialog::proofreadingGateAndEndpoint` became `proofreadingTabHasNoGate` and asserts
  the three removed widgets are absent while keeping the Provider/Issues/Options disabling, the URL
  validation and the "no `translation.*` value" check.
- One file beyond the plan's list: `tests/tst_proofreadingpopup.cpp` also carried the old gate
  sentence as an arbitrary error message and had to switch to the existing manager message
  `No text checking provider is available.` so the step 8 string check stays empty; its scenario is
  unchanged.
- Translations: all 14 `.ts` files are at 376 messages (379 − 4 removed + 1 new `AppController`
  message), 0 unfinished/empty/vanished, and the new message is translated in every file.
- Verification in the `qtranslate-trixie` container (debian:trixie, Qt 6.8.2): Linux Debug and
  Release build with zero `warning:` and pass 45/45 under `xvfb-run -a`; the offscreen smoke with
  `[proofreading] enabled=false` logs only the new message and no request line. Windows
  Debug/Release and its 39 tests are not performed (no MSVC here).
- Not touched: `checkBeforeTranslation`/`autoReplace` and the pre-translation flow (child `02`),
  the `ProofreadingPanel` in `MainWindow` (child `03`), the second hotkey (child `04`) and the
  documentation (the lead's pass).
- Environment note for repeat runs: in `qtranslate-trixie` under a non-root UID,
  `tst_hotkeymanager_wayland` needs `/etc/passwd` and `/etc/group` mounted, otherwise `dbus-daemon`
  fails on the missing password-database entry. That is a sandbox detail, not a project change.
