---
kind: task
status: todo
depends: [01-yandex-web-service]
---

# Yandex settings group and translations

## Task

Expose the implemented `yandex-web` service in the Services page and translate the new strings. The
group is a free/unofficial warning and an opt-in hint only: Yandex has no key, no per-service URL and
no `AppSettings` field, and the gate is the existing `translation/allowFreeServices`. This task
changes no service, parser or translation behaviour; those belong to `01-yandex-web-service`.

In `src/ui/settingsdialog.h`, add `QGroupBox *m_yandexGroup = nullptr;` after `m_mozhiGroup` and
declare `void updateYandexControls();` after `updateMozhiControls()`. In
`src/ui/settingsdialog.cpp`, add a `Yandex` `QGroupBox` after the `Mozhi` group and before the
stretch of the services page, so the `Free web services` gate checkbox and both existing service
groups stay above it. The group has
objectName `yandexGroup` and one word-wrapped `QLabel` with objectName `yandexHint`. The hint says
that the Yandex web endpoint is unofficial and needs no key, that it may stop working, rate-limit
requests or block them, that free web services above must be enabled, that long text is sent in
several requests, and that the text is sent to a third party. There is no `QLineEdit`, key, password
or URL field. The group is visible but disabled while `translation/allowFreeServices` is off; add
`updateYandexControls()` next to `updateMyMemoryControls()`, connect it to the gate checkbox, and
call it at the end of `loadValues()` so the default-off dialog starts disabled. `settings()` and
`validate()` are unchanged.

Extend `tests/tst_settingsdialog.cpp` with a `yandexGroup` case, mirroring `myMemoryGroup`: find
`yandexGroup`, `yandexHint` and `allowFreeServices`; assert the title `Yandex`, the default gate-off
state, both gate directions, word wrap, the hint keywords (`unofficial`, `rate`, `free web
services`, `third party`, `several requests`) and that the group has no `QLineEdit`. Use a valid
language set (for example `{de, en, ru}`) when constructing the dialog, like the Mozhi case, so the
unrelated default-pair validation does not fail the test. Use `isHidden()`/`isEnabled()`, not
`isVisible()`, because the dialog is not shown.

Run the `update_translations` CMake target so `lupdate` rewrites all 14 `i18n/qtranslate_*.ts` with
the two new `SettingsDialog` messages (`Yandex` and the hint), translate both in every file, and keep
0 new/0 unfinished/0 vanished. Keep the Windows-only hotkey message from
`src/platform/hotkeymanager_win.cpp` in the scan. The current message count is 307; the expected
result is 309, and the actual number is recorded in the outcome. `tst_translations` must pass.

This task does not touch `doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md`, the READMEs,
`doc/handoff.md`, the root plan or `07-fallback-chain`; `03-yandex-web-docs` and the lead own them.

## Done when

- The Services page has a `Yandex` group (`yandexGroup`) after `Mozhi` with a word-wrapped
  `yandexHint`, no key, password or URL field, and no new `AppSettings` field or INI key.
- The group is disabled while `translation/allowFreeServices` is off, enabled when the checkbox is
  checked and disabled again when it is unchecked; the default-off dialog starts disabled because
  `updateYandexControls()` runs at the end of `loadValues()`.
- `tst_settingsdialog::yandexGroup` covers the title, the default state, both gate directions, the
  hint word wrap and keywords, and the absence of a text field; the target runs offscreen and the
  dialog's other cases still pass.
- All 14 `i18n/qtranslate_*.ts` contain `Yandex` and the hint translated; the Windows-only hotkey
  message is preserved; the message count goes from 307 to 309 (record the actual number);
  `tst_translations` finds no new, unfinished, empty or vanished messages.
- Debug and Release build on Linux without warnings and pass 35/35; Windows Debug/Release and its 30
  tests are recorded as not performed; no service, parser, settings-store, `AppController` or
  documentation file is changed; no commit is made.

## Risks

- `lupdate` on Linux can mark the Windows-only hotkey message vanished unless the scan includes
  `src/platform/hotkeymanager_win.cpp`; run the CMake translation target and check
  `tst_translations`.
- The dialog is not shown in the test. Use `isHidden()` for the hint and `isEnabled()` for the group;
  `isVisible()` is false for every child of a hidden dialog and would pass for the wrong reason.
- Do not add a key or URL field: Yandex web has no secret and no user-configurable endpoint. The
  group must not look like an enabled service while the gate is off.
- The hint is the only user-facing privacy/instability warning for this service; it must mention the
  unofficial status, rate limits/blocks, the gate, several requests for long text and the third
  party. Do not shorten it to a generic "unofficial" line.
- The Mozhi case needed a non-default language set because `AppSettings{}` collapses the language
  pair and `validate()` fails for an unrelated reason; use an explicit valid set in the Yandex case.
- The `.ts` `<location>` churn from `lupdate` is expected; do not revert it or hand-edit locations.
- Windows Debug/Release and its 30 tests are unavailable on this Linux machine; record them as not
  performed, not as passed.
