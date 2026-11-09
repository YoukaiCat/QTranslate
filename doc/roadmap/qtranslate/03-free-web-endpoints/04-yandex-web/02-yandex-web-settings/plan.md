---
kind: task
status: in-progress
detailed: true
depends: [01-yandex-web-service]
---

# Yandex settings group and translations

## Task

Expose the implemented `yandex-web` service in the Services page and translate the new strings. The
group is a free/unofficial warning and an opt-in hint only: Yandex has no key, no per-service URL and
no `AppSettings` field, and the gate is the existing `translation/allowFreeServices`. This task
changes no service, parser or translation behaviour; those belong to `01-yandex-web-service`.

In `src/ui/settingsdialog.h`, declare `void updateYandexControls();` after `updateMozhiControls()` and
add `QGroupBox *m_yandexGroup = nullptr;` after the Mozhi members (`m_mozhiHint`), so the Mozhi block
stays together. In `src/ui/settingsdialog.cpp`, add a `Yandex` `QGroupBox` after the `Mozhi` group and
before the stretch of the services page, so the `Free web services` gate checkbox and both existing
service groups stay above it. The group has objectName `yandexGroup` and one word-wrapped `QLabel`
with objectName `yandexHint`. The hint says that the Yandex web endpoint is unofficial and needs no
key, that it may stop working, apply rate limits or block requests, that free web services above must
be enabled, that long text is sent in several requests, and that the text is sent to a third party.
There is no `QLineEdit`, key, password or URL field. The group is visible but disabled while
`translation/allowFreeServices` is off; add `updateYandexControls()` next to `updateMozhiControls()`,
connect it to the gate checkbox, and call it at the end of `loadValues()` so the default-off dialog
starts disabled. `settings()` and `validate()` are unchanged.

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

## Steps

1. **Declare the widget and the update slot.** In `src/ui/settingsdialog.h`:
   - add `void updateYandexControls();` directly after `updateMozhiControls();` (line 72), so the
     three gate-aware update methods stay together;
   - add `QGroupBox *m_yandexGroup = nullptr;` after `QLabel *m_mozhiHint = nullptr;` (line 123),
     i.e. after the whole Mozhi member block and before `m_proxyMode`, so the Mozhi fields stay in one
     block.

   `QGroupBox` is already forward-declared at line 11 and there is no new member type; no include
   changes in the header.

2. **Build the group in `buildUi()`.** In `src/ui/settingsdialog.cpp`, insert the group after the
   Mozhi `textChanged` connection (lines 382-383) and before `servicesLayout->addStretch();`
   (line 385), so the Services page order becomes Google, DeepL, the LLM group, Free web services,
   MyMemory, Mozhi, Yandex, stretch:

   ```cpp
   m_yandexGroup = new QGroupBox(tr("Yandex"), servicesContent);
   m_yandexGroup->setObjectName(QStringLiteral("yandexGroup"));
   auto *yandexLayout = new QVBoxLayout(m_yandexGroup);
   auto *yandexHint = new QLabel(
       tr("Yandex web is an unofficial endpoint and needs no key. Enable free web services above "
          "to use it. It may stop working, apply rate limits or block requests. Long text is sent "
          "in several requests. The text is sent to a third party."),
       m_yandexGroup);
   yandexHint->setObjectName(QStringLiteral("yandexHint"));
   yandexHint->setWordWrap(true);
   yandexLayout->addWidget(yandexHint);
   servicesLayout->addWidget(m_yandexGroup);
   connect(m_allowFreeServices, &QCheckBox::toggled, this,
           &SettingsDialog::updateYandexControls);
   ```

   The hint must stay **one** `tr()` call with concatenated literals: splitting it into several
   messages would raise the `.ts` count above 309 and break the count criterion. Use a
   `QVBoxLayout` with the single label and no `QFormLayout`: there is nothing to configure, so no
   `QLineEdit`, key, password or URL may appear. `QGroupBox`, `QLabel` and `QVBoxLayout` are already
   included in `settingsdialog.cpp` (lines 15, 18 and 26); do not add a `SettingsStore`/`AppSettings`
   field, an INI key, a service call or a `[[nodiscard]]` helper.

3. **Follow the gate.** Add after `updateMozhiControls()` (lines 651-657):

   ```cpp
   void SettingsDialog::updateYandexControls()
   {
       m_yandexGroup->setEnabled(m_allowFreeServices->isChecked());
   }
   ```

   Then call `updateYandexControls();` at the very end of `loadValues()`, after
   `updateMozhiControls();` (line 547). The `toggled` connection from step 2 covers both later
   directions; the explicit call covers the initial `AppSettings{}` state, because
   `m_allowFreeServices->setChecked(false)` on an already unchecked box emits no signal. Do not call
   `hide()`/`setVisible()` on the group or the hint — it must stay readable while disabled. Leave
   `settings()` (lines 550-...) and `validate()` untouched: there is no Yandex value to save or
   check.

4. **Add `tst_settingsdialog::yandexGroup`.** In `tests/tst_settingsdialog.cpp`, add the case after
   `mozhiGroupShowsCustomSettings()` (line 513). No new test target is needed: `tst_settingsdialog`
   already exists and is the right home; `QCheckBox`, `QGroupBox`, `QLabel` and `QLineEdit` are
   already included (lines 7-12), and add `<QLayout>` for the order assertion.

   ```cpp
   void yandexGroup()
   {
       SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                             {QStringLiteral("de"), QStringLiteral("en"),
                              QStringLiteral("ru")},
                             true);
       auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("yandexGroup"));
       auto *hint = dialog.findChild<QLabel *>(QStringLiteral("yandexHint"));
       auto *mozhiGroup = dialog.findChild<QGroupBox *>(QStringLiteral("mozhiGroup"));
       auto *checkbox = dialog.findChild<QCheckBox *>(QStringLiteral("allowFreeServices"));
       QVERIFY(group);
       QVERIFY(hint);
       QVERIFY(mozhiGroup);
       QVERIFY(checkbox);

       QCOMPARE(group->title(), QStringLiteral("Yandex"));
       QVERIFY(!checkbox->isChecked());
       QVERIFY(!group->isHidden());
       QVERIFY(!group->isEnabled());
       QVERIFY(hint->wordWrap());
       QVERIFY(hint->text().contains(QStringLiteral("unofficial")));
       QVERIFY(hint->text().contains(QStringLiteral("no key")));
       QVERIFY(hint->text().contains(QStringLiteral("rate")));
       QVERIFY(hint->text().contains(QStringLiteral("block")));
       QVERIFY(hint->text().contains(QStringLiteral("free web services")));
       QVERIFY(hint->text().contains(QStringLiteral("several requests")));
       QVERIFY(hint->text().contains(QStringLiteral("third party")));
       QVERIFY(group->findChildren<QLineEdit *>().isEmpty());
       QVERIFY(!dialog.settings().translation.allowFreeServices);

       const QLayout *servicesLayout = group->parentWidget()->layout();
       QVERIFY(servicesLayout);
       QVERIFY(servicesLayout->indexOf(mozhiGroup) >= 0);
       QVERIFY(servicesLayout->indexOf(mozhiGroup) < servicesLayout->indexOf(group));

       checkbox->setChecked(true);
       QVERIFY(group->isEnabled());
       QVERIFY(dialog.settings().translation.allowFreeServices);

       checkbox->setChecked(false);
       QVERIFY(!group->isEnabled());
       QVERIFY(!dialog.settings().translation.allowFreeServices);
   }
   ```

   Notes: `isHidden()` reflects explicit visibility, so `!group->isHidden()` proves the group is
   shown-but-disabled; `isVisible()` is false for every child of a never-shown dialog and would pass
   for the wrong reason. The `servicesLayout->indexOf(mozhiGroup) < indexOf(group)` assertion is the
   only check of the required "after `Mozhi`" order. The three-language target set mirrors the Mozhi
   case; do not add a `validate()` assertion unless the dialog is built with an explicit
   `translation.primaryLang`/`secondaryLang` pair from that list, because `AppSettings{}` uses the
   system language and the default pair can collapse when the target list is too short. This case
   must not create `SettingsStore`, call a service or show the dialog.

5. **Regenerate and translate the 14 `.ts` files.** From a configured tree run
   `cmake --build build/linux-release --target update_translations` (the target also exists in
   `build/linux-debug`; it regenerates the `lupdate` project from the current CMake and rewrites all
   files in `QTRANSLATE_TS_FILES`, `CMakeLists.txt:154-169`). On Linux
   `QTRANSLATE_LUPDATE_EXTRA_SOURCES` (`CMakeLists.txt:175-177`) keeps
   `src/platform/hotkeymanager_win.cpp` in the scan, so the Windows-only hotkey message must still
   be present (currently `i18n/qtranslate_ru_RU.ts:132`; the line may move). If `lupdate` reports
   obsolete entries, rerun
   `lupdate -no-obsolete -project build/linux-release/.lupdate/qtranslate_lupdate_project.json`; no
   message is removed, so there must be 0 vanished.

   The two new `SettingsDialog` source strings are exactly:

   ```
   Yandex
   Yandex web is an unofficial endpoint and needs no key. Enable free web services above to use it. It may stop working, apply rate limits or block requests. Long text is sent in several requests. The text is sent to a third party.
   ```

   Fill both in all 14 `i18n/qtranslate_*.ts` (ru_RU, es, de, fr, pt_BR, it, pl, uk, tr, zh_CN, ja,
   ko, ar, hi). `Yandex` is a product name and stays `Yandex` in every file; translate the whole hint
   as one message, keeping all five points (unofficial/no key, the gate, rate limits/blocks, several
   requests, third party) and adding no `%` placeholder. The new entries arrive as
   `type="unfinished"`; after filling there must be no `unfinished`, empty or vanished message. Count
   `<message` in one file: the baseline is 307, so the expected result is 309; record the actual
   number in the outcome. The `<location>` churn in all 14 files is expected — do not revert it or
   hand-edit locations.

6. **Build, test and check the boundary.** Build both existing Linux trees
   (`cmake --build build/linux-debug`, `cmake --build build/linux-release`) with `-Wall -Wextra` and
   confirm zero `warning:` lines. Run
   `QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux-release -R tst_settingsdialog
   --output-on-failure` and `-R tst_translations` in Debug and Release;
   `tst_settingsdialog` reports 25 passed (24 before plus the new case), and `tst_translations`
   passes. Run the full suite in both trees:
   `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure` and the Debug tree, 35/35.
   `ctest --test-dir build/linux-release -N` must still show 35 tests (no new test target).
   Record Windows Debug/Release and its 30 tests as not performed (no MSVC). Check
   `git diff --stat`: only `src/ui/settingsdialog.h`, `src/ui/settingsdialog.cpp`,
   `tests/tst_settingsdialog.cpp` and the 14 `i18n/qtranslate_*.ts` may change. Leave commit and push
   to the lead; nothing is needed from the user for this task (an optional visual check of the
   Services page is covered by the offscreen case).

## Done when

- The Services page has a `Yandex` group (`yandexGroup`) after `Mozhi` and before the stretch, with
  the title `Yandex` and a word-wrapped `yandexHint`, and no `QLineEdit`, key, password or URL field;
  no new `AppSettings` field, INI key, service call, `settings()` value or `validate()` condition is
  added.
- The hint is one `tr()` message that says the endpoint is unofficial and needs no key, that free web
  services above must be enabled, that it may stop working, apply rate limits or block requests, that
  long text is sent in several requests and that the text is sent to a third party.
- The group is visible and disabled while `translation/allowFreeServices` is off; checking the gate
  enables it, unchecking disables it again; `updateYandexControls()` runs at the end of
  `loadValues()`, so the default-off dialog starts disabled.
- `tst_settingsdialog::yandexGroup` covers the title, the visible/disabled default, both gate
  directions, the word wrap, the hint keywords, the absent `QLineEdit`, the order after `mozhiGroup`
  and `settings().translation.allowFreeServices`; the target runs offscreen, the new case passes,
  `tst_settingsdialog` reports 25 passed and every other case still passes (record the actual
  number).
- All 14 `i18n/qtranslate_*.ts` contain `Yandex` (untranslated) and the hint translated; the message
  count goes from 307 to 309 (record the actual number); the Windows-only hotkey message from
  `src/platform/hotkeymanager_win.cpp` is still present; `tst_translations` finds no new, unfinished,
  empty or vanished messages.
- Debug and Release build on Linux without `warning:` lines and pass 35/35; `ctest -N` shows 35 and
  no new test target is added; Windows Debug/Release and its 30 tests are recorded as not performed
  (no MSVC); no commit is made.
- Only `src/ui/settingsdialog.h`, `src/ui/settingsdialog.cpp`, `tests/tst_settingsdialog.cpp` and
  the 14 `i18n/qtranslate_*.ts` are changed; no service, parser, settings store, `AppController`,
  CMake, `doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md`, README, `doc/handoff.md`, root
  plan or `07-fallback-chain` file is touched.

## Risks

- `lupdate` on Linux can mark the Windows-only hotkey message vanished unless the scan includes
  `src/platform/hotkeymanager_win.cpp`; run the CMake translation target and check
  `tst_translations`.
- The dialog is not shown in the test. Use `isHidden()` for the group's explicit visibility and
  `isEnabled()` for the gate state; `isVisible()` is false for every child of a hidden dialog and
  would pass for the wrong reason.
- Do not add a key or URL field: Yandex web has no secret and no user-configurable endpoint. The
  group must not look like an enabled service while the gate is off, and the hint must stay readable
  (disabled, not hidden).
- The hint is the only user-facing privacy/instability warning for this service; it must mention the
  unofficial status, rate limits/blocks, the gate, several requests for long text and the third
  party. Do not shorten it to a generic "unofficial" line, and keep it one `tr()` message — splitting
  it adds extra `.ts` messages and breaks the 309 count.
- The Mozhi case needed a non-default language set because `AppSettings{}` takes the system language
  and the pair can collapse when the target list is short; use an explicit valid set
  (`{de, en, ru}`) in the Yandex case. Do not assert `validate()` there unless the pair is set
  explicitly, because the test would then depend on the host locale.
- `lupdate` rewrites the `<location>` lines in all 14 `.ts` because the dialog gains lines. This
  churn is expected; do not revert it or hand-edit locations. Only the two messages and their
  translations are substantive.
- `Yandex` is already used as the untranslated Mozhi engine display name via
  `QStringLiteral("Yandex")` (`settingsdialog.cpp:350`); do not move that to `tr()` — it would add
  another message to the `SettingsDialog` context and change the count.
- The `.ts` count is the acceptance signal: 307 → 309. If `lupdate` reports a different number,
  check whether another `tr()` string was added or an old message vanished, and record the actual
  number instead of adjusting the expectation.
- Windows Debug/Release and its 30 tests are unavailable on this Linux machine; record them as not
  performed, not as passed.
