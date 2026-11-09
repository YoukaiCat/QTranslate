---
kind: task
status: todo
depends: [02-bing-web-service]
---

# Bing settings group and translations

## Task

Expose the implemented `bing-web` service in the Services page and translate the new strings. The
group is a free/unofficial warning and an opt-in hint only: Bing has no key, no per-service URL and no
`AppSettings` field, and the gate is the existing `translation/allowFreeServices`. This task changes
no service, parser or translation behaviour; those belong to `02-bing-web-service`.

In `src/ui/settingsdialog.h`, declare `void updateBingControls();` after `updateYandexControls()` and
add `QGroupBox *m_bingGroup = nullptr;` after `m_yandexGroup`, so the free-service groups stay
together. In `src/ui/settingsdialog.cpp`, add a `Bing` `QGroupBox` after the `Yandex` group and before
the stretch of the services page, so the `Free web services` gate checkbox and the existing groups
stay above it. The group has objectName `bingGroup` and one word-wrapped `QLabel` with objectName
`bingHint`. The hint is one `tr()` message and says that the Bing web endpoint is unofficial and
needs no key, that it may stop working, apply rate limits or block requests, that free web services
above must be enabled, that long text is sent in several requests, and that the text is sent to a
third party. There is no `QLineEdit`, key, password or URL field. The group is visible but disabled
while `translation/allowFreeServices` is off; `updateBingControls()` is connected to the gate checkbox
and called at the end of `loadValues()` so the default-off dialog starts disabled. `settings()` and
`validate()` are unchanged.

Extend `tests/tst_settingsdialog.cpp` with a `bingGroup` case mirroring `yandexGroup`: find
`bingGroup`, `bingHint`, `allowFreeServices` and `yandexGroup`; assert the title `Bing`, the default
gate-off state, both gate directions, word wrap, the hint keywords (`unofficial`, `no key`, `rate`,
`block`, `free web services`, `several requests`, `third party`) and that the group has no
`QLineEdit`; assert the order after `yandexGroup`. Use a valid language set such as `{de, en, ru}`
when constructing the dialog, like the Yandex case, so the unrelated default-pair validation does not
fail the test. Use `isHidden()`/`isEnabled()`, not `isVisible()`, because the dialog is not shown.

Run the `update_translations` CMake target so `lupdate` rewrites all 14 `i18n/qtranslate_*.ts` with
the two new `SettingsDialog` messages (`Bing` and the hint), translate both in every file, and keep
0 new/0 unfinished/0 vanished. Keep the Windows-only hotkey message from
`src/platform/hotkeymanager_win.cpp` in the scan. The current message count is 309; the expected
result is 311, and the actual number is recorded in the outcome. `tst_translations` must pass. The
`<location>` churn caused by the `languages.cpp`/`appcontroller.cpp` changes from `01`/`02` is
expected; do not revert it or hand-edit locations.

This task does not touch `doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md`, the READMEs,
`doc/handoff.md`, the root plan or `07-fallback-chain`; `04-bing-web-docs` and the lead own them.

## Done when

- The Services page has a `Bing` group (`bingGroup`) after `Yandex` and before the stretch, with the
  title `Bing` and a word-wrapped `bingHint`, and no `QLineEdit`, key, password or URL field; no new
  `AppSettings` field, INI key, service call, `settings()` value or `validate()` condition is added.
- The hint is one `tr()` message that says the endpoint is unofficial and needs no key, that free web
  services above must be enabled, that it may stop working, apply rate limits or block requests, that
  long text is sent in several requests and that the text is sent to a third party.
- The group is visible and disabled while `translation/allowFreeServices` is off; checking the gate
  enables it, unchecking disables it again; `updateBingControls()` runs at the end of `loadValues()`,
  so the default-off dialog starts disabled.
- `tst_settingsdialog::bingGroup` covers the title, the visible/disabled default, both gate
  directions, the word wrap, the hint keywords, the absent `QLineEdit`, the order after `yandexGroup`
  and `settings().translation.allowFreeServices`; `tst_settingsdialog` passes with 26 cases (25
  before plus the new case; record the actual number) and every other case still passes.
- All 14 `i18n/qtranslate_*.ts` contain `Bing` (untranslated) and the hint translated; the message
  count goes from 309 to 311 (record the actual number); the Windows-only hotkey message from
  `src/platform/hotkeymanager_win.cpp` is still present; `tst_translations` finds no new, unfinished,
  empty or vanished messages.
- Debug and Release build on Linux without `warning:` lines and pass 37/37; `ctest -N` shows 37 and
  no new test target is added; Windows Debug/Release and its 32 tests are recorded as not performed
  (no MSVC); no commit is made.
- Only `src/ui/settingsdialog.h`, `src/ui/settingsdialog.cpp`, `tests/tst_settingsdialog.cpp` and the
  14 `i18n/qtranslate_*.ts` are changed; no service, parser, settings store, `AppController`, CMake,
  `doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md`, README, `doc/handoff.md`, root plan or
  `07-fallback-chain` file is touched.

## Risks

- `lupdate` on Linux can mark the Windows-only hotkey message vanished unless the scan includes
  `src/platform/hotkeymanager_win.cpp`; run the CMake translation target and check
  `tst_translations`.
- The dialog is not shown in the test. Use `isHidden()` for the group's explicit visibility and
  `isEnabled()` for the gate state; `isVisible()` is false for every child of a hidden dialog and
  would pass for the wrong reason.
- Do not add a key or URL field: Bing web has no secret and no user-configurable endpoint. The group
  must not look like an enabled service while the gate is off, and the hint must stay readable
  (disabled, not hidden).
- The hint is the only user-facing privacy/instability warning for this service; it must mention the
  unofficial status, the gate, rate limits/blocks, several requests for long text and the third
  party. Keep it one `tr()` message — splitting it adds extra `.ts` messages and breaks the 311
  count.
- `Bing` is a product name and stays untranslated in every `.ts`; the hint is translated as one
  message with no `%` placeholder. The new entries arrive as `type="unfinished"`; after filling there
  must be no unfinished, empty or vanished message.
- The `.ts` count is the acceptance signal: 309 → 311. If `lupdate` reports a different number, check
  whether another `tr()` string was added or an old message vanished, and record the actual number
  instead of adjusting the expectation.
- Windows Debug/Release and its 32 tests are unavailable on this Linux machine; record them as not
  performed, not as passed.
