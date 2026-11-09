---
kind: task
status: done
detailed: true
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

## Steps

1. **Declare the widget and the update slot.** In `src/ui/settingsdialog.h`:
   - add `void updateBingControls();` directly after `updateYandexControls();` (line 73) and before
     `updateProxyControls();` (line 74), so the gate-aware update methods stay together in the
     existing `Google, DeepL, MyMemory, Mozhi, Yandex` order;
   - add `QGroupBox *m_bingGroup = nullptr;` after `QGroupBox *m_yandexGroup = nullptr;` (line 126)
     and before the blank line and `m_proxyMode` (line 128), so the free-service group members stay
     in one block.

   `QGroupBox` is already forward-declared at line 11 and there is no new member type; no include
   changes in the header.

2. **Build the group in `buildUi()`.** In `src/ui/settingsdialog.cpp`, insert the group after the
   Yandex `connect()` (lines 397-398) and before `servicesLayout->addStretch();` (line 400), so the
   Services page order becomes Google, DeepL, the LLM group, Free web services, MyMemory, Mozhi,
   Yandex, Bing, stretch:

   ```cpp
   m_bingGroup = new QGroupBox(tr("Bing"), servicesContent);
   m_bingGroup->setObjectName(QStringLiteral("bingGroup"));
   auto *bingLayout = new QVBoxLayout(m_bingGroup);
   auto *bingHint = new QLabel(
       tr("Bing web is an unofficial endpoint and needs no key. Enable free web services above "
          "to use it. It may stop working, apply rate limits or block requests. Long text is sent "
          "in several requests. The text is sent to a third party."),
       m_bingGroup);
   bingHint->setObjectName(QStringLiteral("bingHint"));
   bingHint->setWordWrap(true);
   bingLayout->addWidget(bingHint);
   servicesLayout->addWidget(m_bingGroup);
   connect(m_allowFreeServices, &QCheckBox::toggled, this,
           &SettingsDialog::updateBingControls);
   ```

   The hint must stay **one** `tr()` call with concatenated literals: splitting it into several
   messages would raise the `.ts` count above 311 and break the count criterion. It is the Yandex
   sentence with `Bing` as the subject, so it is a new `.ts` message (the source differs) and must not
   be replaced by a runtime `%1` built from the Yandex string. Use a `QVBoxLayout` with the single
   label and no `QFormLayout`: there is nothing to configure, so no `QLineEdit`, key, password or URL
   may appear. `QGroupBox` (line 15), `QLabel` (line 18) and `QVBoxLayout` (line 26) are already
   included in `settingsdialog.cpp`; do not add a `SettingsStore`/`AppSettings` field, an INI key, a
   service call or a `[[nodiscard]]` helper. The local `bingHint` pointer is enough; there is nothing
   to update on the label, so no member is needed for it.

3. **Follow the gate.** Add after `updateYandexControls()` (lines 676-679):

   ```cpp
   void SettingsDialog::updateBingControls()
   {
       m_bingGroup->setEnabled(m_allowFreeServices->isChecked());
   }
   ```

   Then call `updateBingControls();` at the very end of `loadValues()`, after
   `updateYandexControls();` (line 563). The `toggled` connection from step 2 covers both later
   directions; the explicit call covers the initial `AppSettings{}` state, because
   `m_allowFreeServices->setChecked(false)` on an already unchecked box emits no signal. Do not call
   `hide()`/`setVisible()` on the group or the hint — it must stay readable while disabled. Leave
   `settings()` (lines 566-604) and `validate()` untouched: there is no Bing value to save or check.

4. **Add `tst_settingsdialog::bingGroup`.** In `tests/tst_settingsdialog.cpp`, add the case after
   `yandexGroup()` (which ends at line 558) and before `llmModels()` (line 560). No new test target is
   needed: `tst_settingsdialog` already exists and is the right home; `QCheckBox`, `QGroupBox`,
   `QLabel`, `QLayout` and `QLineEdit` are already included (lines 7-13).

   ```cpp
   void bingGroup()
   {
       SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                             {QStringLiteral("de"), QStringLiteral("en"),
                              QStringLiteral("ru")},
                             true);
       auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("bingGroup"));
       auto *hint = dialog.findChild<QLabel *>(QStringLiteral("bingHint"));
       auto *yandexGroup = dialog.findChild<QGroupBox *>(QStringLiteral("yandexGroup"));
       auto *checkbox = dialog.findChild<QCheckBox *>(QStringLiteral("allowFreeServices"));
       QVERIFY(group);
       QVERIFY(hint);
       QVERIFY(yandexGroup);
       QVERIFY(checkbox);

       QCOMPARE(group->title(), QStringLiteral("Bing"));
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
       QVERIFY(servicesLayout->indexOf(yandexGroup) >= 0);
       QVERIFY(servicesLayout->indexOf(yandexGroup) < servicesLayout->indexOf(group));

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
   for the wrong reason. The `servicesLayout->indexOf(yandexGroup) < indexOf(group)` assertion is the
   only check of the required order after Yandex; "before the stretch" is not asserted, because a
   widget added after `addStretch()` would still have a larger index, but the stretch would sit above
   it. The visual order (Bing directly under Yandex, stretch last) is guaranteed by the code placement
   in step 2. The three-language target set mirrors the Yandex case; do not add a `validate()`
   assertion unless the dialog is built with an explicit
   `translation.primaryLang`/`secondaryLang` pair from that list, because `AppSettings{}` uses the
   system language and the default pair can collapse when the target list is too short. This case
   must not create `SettingsStore`, call a service or show the dialog.

5. **Regenerate and translate the 14 `.ts` files.** From a configured tree run
   `cmake --build build/linux-release --target update_translations` (the target also exists in
   `build/linux-debug`; it regenerates the `lupdate` project from the current CMake and rewrites all
   files in `QTRANSLATE_TS_FILES`, `CMakeLists.txt:156-171`). On Linux
   `QTRANSLATE_LUPDATE_EXTRA_SOURCES` (`CMakeLists.txt:177-179`) keeps
   `src/platform/hotkeymanager_win.cpp` in the scan, so the Windows-only hotkey message must still be
   present (currently `i18n/qtranslate_ru_RU.ts:132`; the line may move). If `lupdate` reports
   obsolete entries, rerun
   `lupdate -no-obsolete -project build/linux-release/.lupdate/qtranslate_lupdate_project.json`; no
   message is removed, so there must be 0 vanished.

   The two new `SettingsDialog` source strings are exactly:

   ```
   Bing
   Bing web is an unofficial endpoint and needs no key. Enable free web services above to use it. It may stop working, apply rate limits or block requests. Long text is sent in several requests. The text is sent to a third party.
   ```

   Fill both in all 14 `i18n/qtranslate_*.ts` (ru_RU, es, de, fr, pt_BR, it, pl, uk, tr, zh_CN, ja,
   ko, ar, hi). `Bing` is a product name and stays `Bing` in every file; translate the whole hint as
   one message, keeping all five points (unofficial/no key, the gate, rate limits/blocks, several
   requests, third party) and adding no `%` placeholder, no leading/trailing space and no symbol. The
   new entries arrive as `type="unfinished"`; after filling there must be no `unfinished`, empty or
   vanished message. Count `<message` in every file: the baseline is 309, so the expected result is
   311; record the actual number in the outcome. The `<location>` churn in all 14 files is expected —
   `appcontroller.cpp` (for example `DeepL is not available.` is listed at line 702 but is at line
   708) and `languages.cpp` locations are stale after `01`/`02`; do not revert it or hand-edit
   locations.

   Note that `BingWebService::displayName()` returns `QStringLiteral("Bing")`
   (`src/services/bingwebservice.cpp:554`), so `Bing` is currently absent from every `.ts`; do not
   convert that call to `tr()` — it would add a third message and break the 311 count.

6. **Build, test and check the boundary.** Build both existing Linux trees
   (`cmake --build build/linux-debug`, `cmake --build build/linux-release`) with `-Wall -Wextra` and
   confirm zero `warning:` lines. Run
   `QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux-release -R tst_settingsdialog
   --output-on-failure` and the same `-R tst_settingsdialog` / `-R tst_translations` commands in
   `build/linux-debug`; `tst_settingsdialog` reports 26 passed (25 before plus the new case; the 25
   include QTest's implicit `initTestCase` and `cleanupTestCase`, so there are 23 named cases before
   and 24 after), and `tst_translations` passes. Run the full suite in both trees:
   `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure` and the Debug tree, 37/37.
   `ctest --test-dir build/linux-release -N` must still show 37 tests (no new test target). Record
   Windows Debug/Release and its 32 tests as derived from `tests/CMakeLists.txt` and not performed (no
   MSVC). Check `git diff --stat`: only `src/ui/settingsdialog.h`, `src/ui/settingsdialog.cpp`,
   `tests/tst_settingsdialog.cpp` and the 14 `i18n/qtranslate_*.ts` may change. Leave commit and push
   to the lead; nothing is needed from the user for this task (an optional visual check of the
   Services page is covered by the offscreen case).

## Done when

- The Services page has a `Bing` group (`bingGroup`) inserted after `yandexGroup` and before
  `servicesLayout->addStretch()`, with the title `Bing` and a word-wrapped `bingHint`, and no
  `QLineEdit`, key, password or URL field; no new `AppSettings` field, INI key, service call,
  `settings()` value or `validate()` condition is added and no `[bing]` settings group appears.
- The hint is one `tr()` message with the exact source
  `Bing web is an unofficial endpoint and needs no key. Enable free web services above to use it. It may stop working, apply rate limits or block requests. Long text is sent in several requests. The text is sent to a third party.`
  and it covers all five points (unofficial/no key, the gate, rate limits/blocks, several requests,
  third party) without a `%` placeholder.
- The group is visible and disabled while `translation/allowFreeServices` is off; checking the gate
  enables it, unchecking disables it again; `updateBingControls()` runs at the end of `loadValues()`,
  so the default-off dialog starts disabled; the hint is never hidden.
- `tst_settingsdialog::bingGroup` covers the title, the visible/disabled default
  (`isHidden()`/`isEnabled()`), both gate directions, the word wrap, the seven hint keywords, the
  absent `QLineEdit`, the order after `yandexGroup` and
  `settings().translation.allowFreeServices`; the target runs offscreen, the new case passes,
  `tst_settingsdialog` reports 26 passed (25 before plus the new case; 23 named cases plus
  `initTestCase`/`cleanupTestCase` before, 24 named cases after — record the actual number) and every
  other case still passes.
- All 14 `i18n/qtranslate_*.ts` contain `Bing` (untranslated) and the hint translated as one message;
  the message count goes from 309 to 311 (record the actual number); the Windows-only hotkey message
  from `src/platform/hotkeymanager_win.cpp` is still present; every file has 0 unfinished, 0 empty
  and 0 vanished messages; `tst_translations` finds no new, unfinished, empty or vanished message and
  the app build runs `lrelease` over all 14 files without errors.
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
  count. Do not build it at runtime from the Yandex string: the `.ts` sources must differ.
- `Bing` is a product name and stays untranslated in every `.ts`; the hint is translated as one
  message with no `%` placeholder. The new entries arrive as `type="unfinished"`; after filling there
  must be no unfinished, empty or vanished message. `BingWebService::displayName()` already returns
  the untranslated `QStringLiteral("Bing")` (`src/services/bingwebservice.cpp:554`); do not move it to
  `tr()` or the count becomes 312.
- The `.ts` count is the acceptance signal: 309 → 311. If `lupdate` reports a different number, check
  whether another `tr()` string was added or an old message vanished, and record the actual number
  instead of adjusting the expectation. Two known sources of churn are allowed: the shifted
  `settingsdialog.cpp` locations and the stale `appcontroller.cpp`/`languages.cpp` locations from
  `01`/`02`.
- The `.ts` at HEAD are stale for `appcontroller.cpp` (for example `DeepL is not available.` is
  listed at line 702 but is at line 708) and `languages.cpp`, so `lupdate` rewrites many
  `<location>` lines. This churn is expected; do not revert it or hand-edit locations, and do not
  treat it as a lost translation.
- `Bing` is the last free-service group on the page. Insert it before `addStretch()`: a widget added
  after the stretch is still found by the order assertion (its index is larger), but the stretch would
  sit above it and push the group to the bottom of the Services page.
- `tst_settingsdialog` counts include the implicit `initTestCase`/`cleanupTestCase`, so 25 now means
  23 named cases; the new case makes 26 passed, not 27.
- Windows Debug/Release and its 32 tests are unavailable on this Linux machine; record them as not
  performed, not as passed.

## Outcome

Done on 2026-11-09. The Services page exposes the implemented `bing-web` service as a gate-aware
`Bing` group, and the two new strings are complete in all 14 `.ts` files. No service, parser,
`AppSettings`/INI key, `settings()` value, `validate()` condition or `AppController` path changed.

- **UI:** `src/ui/settingsdialog.h` declares `updateBingControls()` after `updateYandexControls()`
  and `m_bingGroup` after `m_yandexGroup`. `buildUi()` creates the `Bing` group (`bingGroup`) after
  the Yandex group and before the stretch, with one `QVBoxLayout` and a single word-wrapped
  `bingHint` label; the hint is one `tr()` message with the planned text and no `%` placeholder.
  The group has no `QLineEdit`, key, password or URL field. `updateBingControls()` enables the
  group from `m_allowFreeServices`, is connected to the gate `toggled` and runs at the end of
  `loadValues()`, so the default-off dialog starts visible but disabled. The hint is never hidden.
- **Test:** `tst_settingsdialog::bingGroup` covers the title, the visible/disabled default
  (`isHidden()`/`isEnabled()`), both gate directions, word wrap, the seven hint keywords, the absent
  `QLineEdit`, the order after `yandexGroup` and `settings().translation.allowFreeServices`. It uses
  `{de, en, ru}` and does not call `validate()`, so it does not depend on the host locale; it also
  fails if the `loadValues()` call is removed, because `AppSettings{}` leaves the group enabled
  otherwise. `tst_settingsdialog` reports 26 passed (25 before plus the new case; 24 named cases
  plus `initTestCase`/`cleanupTestCase`).
- **Translations:** the CMake `update_translations` target rewrote all 14 `i18n/qtranslate_*.ts`
  (`lupdate` reported 311 source texts: 2 new and 309 already existing). Every file has 311 messages
  (actual count; baseline 309), 0 unfinished, 0 vanished and no empty translations; `lrelease`
  builds all 14 with 311 complete translations. `Bing` stays untranslated everywhere and the hint is
  translated as one message with all five points (unofficial/no key, the free-web-services gate,
  rate limits/blocks, several requests, third party), without a `%` placeholder or edge whitespace.
  The Windows-only `HotkeyManager` message "The key combination is not supported." from
  `src/platform/hotkeymanager_win.cpp` remains in every file; `lupdate` prints "Qualifying with
  unknown namespace/class ::HotkeyManager" for it, which is a scan diagnostic, not a dropped entry.
- **Location churn:** besides the shift caused by the new lines in `settingsdialog.cpp`, `lupdate`
  brought the stale `<location>` lines of `appcontroller.cpp` and `languages.cpp` up to date. In
  `fr` and `it` two existing Yandex translations changed only by re-encoding `'` as `&apos;`
  (lupdate's writer), not in content. A structural comparison with HEAD shows exactly two added
  messages and no existing translation changed.
- **Verification:** Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2), Debug and Release: zero `warning:`
  lines with `-Wall -Wextra`, `ctest -N` 37, `tst_settingsdialog` 26 passed, `tst_translations`
  passed, the full suite 37/37 in both trees. An additional fresh trixie Release build in
  `qtranslate-trixie:latest` (Qt 6.8.2, GCC 14.2) was warning-free and passed 37/37; that container
  needs the known external `/etc/passwd`/`/etc/group` mount for `dbus-daemon`, otherwise only
  `tst_hotkeymanager_wayland` fails in `initTestCase` ("dbus-daemon did not print a session
  address") because the run uid has no passwd entry. No new test target was added. Windows
  Debug/Release and its 32 tests were **not performed**, not passed (no MSVC).
- **Review:** no remarks to accept or reject. No real problem was deferred because of cost.
- **Not performed and deferred by design:** the optional manual visual check of the Services page
  (covered by the offscreen case), Windows, and the documentation updates (`doc/architecture.md`,
  `doc/free-endpoints.md`, `AGENTS.md`, READMEs, `doc/handoff.md`, the root plan and
  `07-fallback-chain`), which belong to `04-bing-web-docs` and the lead. No known limitation of the
  group itself.
