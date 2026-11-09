---
kind: task
status: done
detailed: true
depends: [02-reverso-service]
---

# Reverso settings

## Task

Expose the already-implemented `Reverso` service in the Services page of `SettingsDialog` and
complete the two new UI strings in all 14 `.ts` files. This task changes no service behaviour and
adds no `AppSettings`/INI field, key or URL: Reverso has no key and no editable URL, and the free-web
gate is already generic. The group is the last free-service group on the page, after `Bing`, and is a
warning plus an opt-in hint only.

## Steps

1. **Declare the member and the update slot.** In `src/ui/settingsdialog.h`:
   - add `void updateReversoControls();` after `updateBingControls();` (line 74) and before
     `updateProxyControls();` (line 75), so the gate-aware update methods stay in the existing
     `Google, DeepL, MyMemory, Mozhi, Yandex, Bing` order;
   - add `QGroupBox *m_reversoGroup = nullptr;` after `m_bingGroup` (line 129) and before the blank
     line and `m_proxyMode` (line 131), so the free-service group members stay in one block.

   `QGroupBox` is already forward-declared at line 11; there is no new member type and no include
   change in the header.

2. **Build the group in `buildUi()`.** In `src/ui/settingsdialog.cpp`, insert the group after the
   Bing `connect()` (lines 412-413) and before `servicesLayout->addStretch();` (line 415), so the
   Services page order becomes Google, DeepL, the LLM group, Free web services, MyMemory, Mozhi,
   Yandex, Bing, Reverso, stretch:

   ```cpp
   m_reversoGroup = new QGroupBox(tr("Reverso"), servicesContent);
   m_reversoGroup->setObjectName(QStringLiteral("reversoGroup"));
   auto *reversoLayout = new QVBoxLayout(m_reversoGroup);
   auto *reversoHint = new QLabel(
       tr("Reverso is an unofficial endpoint and needs no key. It supports only English, French, "
          "German, Spanish, Italian and Russian and cannot detect the source language. Enable free "
          "web services above to use it. It may stop working, apply rate limits or block requests. "
          "Long text is sent in several requests. The text is sent to a third party."),
       m_reversoGroup);
   reversoHint->setObjectName(QStringLiteral("reversoHint"));
   reversoHint->setWordWrap(true);
   reversoLayout->addWidget(reversoHint);
   servicesLayout->addWidget(m_reversoGroup);
   connect(m_allowFreeServices, &QCheckBox::toggled, this,
           &SettingsDialog::updateReversoControls);
   ```

   The hint must stay **one** `tr()` call with concatenated literals: splitting it into several
   messages would raise the `.ts` count above 313 and break the count criterion. Use a
   `QVBoxLayout` with the single label and no `QFormLayout`: there is nothing to configure, so no
   `QLineEdit`, key, password or URL may appear. `QGroupBox`, `QLabel` and `QVBoxLayout` are already
   included in `settingsdialog.cpp`; do not add an `AppSettings` field, an INI key, a service call or
   a `[[nodiscard]]` helper. The local `reversoHint` pointer is enough; there is nothing to update
   on the label, so no member is needed for it.

3. **Follow the gate.** Add after `updateBingControls()` (lines 697-700):

   ```cpp
   void SettingsDialog::updateReversoControls()
   {
       m_reversoGroup->setEnabled(m_allowFreeServices->isChecked());
   }
   ```

   Then call `updateReversoControls();` at the very end of `loadValues()`, after
   `updateBingControls();` (line 579). The `toggled` connection from step 2 covers both later
   directions; the explicit call covers the initial `AppSettings{}` state, because
   `m_allowFreeServices->setChecked(false)` on an already unchecked box emits no signal. Do not call
   `hide()`/`setVisible()` on the group or the hint — it must stay visible but disabled. Leave
   `settings()` (lines 582-620) and `validate()` (line 714) untouched: there is no Reverso value to
   save or check, and no cache reset is added.

4. **Add `tst_settingsdialog::reversoGroup`.** In `tests/tst_settingsdialog.cpp`, add the case after
   `bingGroup()` (which ends at line 602) and before `llmModels()` (line 604). No new test target is
   needed: `tst_settingsdialog` already exists and is the right home; `QCheckBox`, `QGroupBox`,
   `QLabel`, `QLayout` and `QLineEdit` are already included (lines 7-13).

   ```cpp
   void reversoGroup()
   {
       SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                             {QStringLiteral("de"), QStringLiteral("en"),
                              QStringLiteral("ru")},
                             true);
       auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("reversoGroup"));
       auto *hint = dialog.findChild<QLabel *>(QStringLiteral("reversoHint"));
       auto *bingGroup = dialog.findChild<QGroupBox *>(QStringLiteral("bingGroup"));
       auto *checkbox = dialog.findChild<QCheckBox *>(QStringLiteral("allowFreeServices"));
       QVERIFY(group);
       QVERIFY(hint);
       QVERIFY(bingGroup);
       QVERIFY(checkbox);

       QCOMPARE(group->title(), QStringLiteral("Reverso"));
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
       QVERIFY(hint->text().contains(QStringLiteral("English")));
       QVERIFY(hint->text().contains(QStringLiteral("source language")));
       QVERIFY(group->findChildren<QLineEdit *>().isEmpty());
       QVERIFY(!dialog.settings().translation.allowFreeServices);

       const QLayout *servicesLayout = group->parentWidget()->layout();
       QVERIFY(servicesLayout);
       QVERIFY(servicesLayout->indexOf(bingGroup) >= 0);
       QVERIFY(servicesLayout->indexOf(bingGroup) < servicesLayout->indexOf(group));

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
   for the wrong reason. The `{de, en, ru}` target set mirrors the `bingGroup` case and avoids the
   unrelated default-pair validation failing. The `servicesLayout->indexOf(bingGroup) <
   indexOf(group)` assertion is the only check of the required order after `Bing`; "before the
   stretch" is not asserted, because a widget added after `addStretch()` would still have a larger
   index, but the stretch would sit above it. The visual order (Reverso directly under Bing, stretch
   last) is guaranteed by the code placement in step 2. This case must not create `SettingsStore`,
   call a service or show the dialog.

5. **Regenerate and translate the 14 `.ts` files.** From a configured tree run
   `cmake --build build/linux-release --target update_translations` (the target also exists in
   `build/linux-debug`; it regenerates the `lupdate` project from the current CMake and rewrites all
   files in `QTRANSLATE_TS_FILES`, `CMakeLists.txt:158-173`). On Linux
   `QTRANSLATE_LUPDATE_EXTRA_SOURCES` (`CMakeLists.txt:179-181`) keeps
   `src/platform/hotkeymanager_win.cpp` in the scan, so the Windows-only hotkey message must still be
   present (currently `i18n/qtranslate_ru_RU.ts:132`; the line may move). If `lupdate` reports
   obsolete entries, rerun
   `lupdate -no-obsolete -project build/linux-release/.lupdate/qtranslate_lupdate_project.json`; no
   message is removed, so there must be 0 vanished.

   The two new `SettingsDialog` source strings are exactly:

   ```
   Reverso
   Reverso is an unofficial endpoint and needs no key. It supports only English, French, German, Spanish, Italian and Russian and cannot detect the source language. Enable free web services above to use it. It may stop working, apply rate limits or block requests. Long text is sent in several requests. The text is sent to a third party.
   ```

   Fill both in all 14 `i18n/qtranslate_*.ts` (ru_RU, es, de, fr, pt_BR, it, pl, uk, tr, zh_CN, ja,
   ko, ar, hi). `Reverso` is a product name and stays `Reverso` in every file; translate the whole
   hint as one message, keeping the six languages, the no-auto-detection statement, the gate, the
   rate limits/blocks, the several requests and the third party. Add no `%` placeholder, no
   leading/trailing space and no symbol. The new entries arrive as `type="unfinished"`; after
   filling there must be no `unfinished`, empty or vanished message. Count `<message` in every file:
   the baseline is 311, so the expected result is 313. The `<location>` churn in all 14 files is
   expected — the new `settingsdialog.cpp` lines shift the existing locations, and
   `appcontroller.cpp`/`languages.cpp` locations are stale after `01`/`02`; do not revert it or
   hand-edit locations.

   Note that `ReversoService::displayName()` returns `QStringLiteral("Reverso")`
   (`src/services/reversoservice.cpp:375`), so `Reverso` is currently absent from every `.ts`; do not
   convert that call to `tr()` — it would add a third message and break the 313 count. The `Reverso`
   entry at `src/ui/settingsdialog.cpp:349` is a Mozhi engine display name, not this group; do not
   touch it.

6. **Build, test and check the boundary.** Build both existing Linux trees
   (`cmake --build build/linux-debug`, `cmake --build build/linux-release`) with `-Wall -Wextra` and
   confirm zero `warning:` lines. Run
   `QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux-release -R "tst_settingsdialog|tst_translations"
   --output-on-failure` and the same `-R "tst_settingsdialog|tst_translations"` commands in
   `build/linux-debug`; `tst_settingsdialog` reports 27 passed (26 before plus the new case; the 26
   include QTest's implicit `initTestCase` and `cleanupTestCase`, so there are 24 named cases before
   and 25 after), and `tst_translations` passes 4/4. Run the full suite in both trees:
   `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure` and the Debug tree, 39/39.
   `ctest --test-dir build/linux-release -N` must still show 39 tests (no new test target). Record
   Windows Debug/Release and its 34 tests as derived from `tests/CMakeLists.txt` and not performed
   (no MSVC). Check `git diff --stat`: only `src/ui/settingsdialog.h`, `src/ui/settingsdialog.cpp`,
   `tests/tst_settingsdialog.cpp` and the 14 `i18n/qtranslate_*.ts` may change. Leave commit and push
   to the lead; nothing is needed from the user for this task (an optional visual check of the
   Services page is covered by the offscreen case).

## Done when

- The Services page has a gate-aware `Reverso` group after `Bing`, with the unofficial/no-key
  warning, the six-language limitation, the no-auto-detection statement and the opt-in hint; it has
  no key, URL or other input field.
- `updateReversoControls()` disables the group while `translation/allowFreeServices` is off and
  enables it when the checkbox is on; `settings()` exposes no new field.
- `tst_settingsdialog` has a `reversoGroup()` case that covers the title, the hint text, the gate
  state, the missing input fields and the position after the Bing group; the existing cases stay
  green; `tst_settingsdialog` reports 27 passed (26 before plus the new case).
- All 14 `.ts` files contain the two new messages (311 → 313), `tst_translations` passes, and no
  `.ts` file has an unfinished, vanished or empty translation.
- Debug and Release build on Linux without warnings; the full suite still passes 39/39 and `ctest
  -N` still shows 39 Linux tests / 34 on Windows (no new target); Windows Debug/Release and its 34
  tests are recorded as not performed (no MSVC).
- No service, `AppController`, parser, `doc/`, `AGENTS.md`, README, `doc/handoff.md`, root-plan or
  `07-fallback-chain` file is changed; commit and push are the coordinator/lead's.

## Risks

- The dialog is not shown in the test. Use `isHidden()` for the group's explicit visibility and
  `isEnabled()` for the gate state; `isVisible()` is false for every child of a hidden dialog and
  would pass for the wrong reason.
- The hint is the only user-facing privacy/instability warning for this service; it must mention the
  unofficial status, the six-language limit, the no-auto-detection statement, the gate, rate
  limits/blocks, several requests for long text and the third party. Keep it one `tr()` message —
  splitting it adds extra `.ts` messages and breaks the 313 count.
- `Reverso` is a product name and stays untranslated in every `.ts`; `ReversoService::displayName()`
  returns `QStringLiteral("Reverso")` (`src/services/reversoservice.cpp:375`), so do not move it to
  `tr()` or the count becomes 314.
- `lupdate` on Linux can mark the Windows-only hotkey message vanished unless the scan includes
  `src/platform/hotkeymanager_win.cpp`; run the CMake translation target and check
  `tst_translations`.
- The `.ts` count is the acceptance signal: 311 → 313. If `lupdate` reports a different number,
  check whether another `tr()` string was added or an old message vanished, and record the actual
  number instead of adjusting the expectation. The `<location>` churn in all 14 files is expected
  because `settingsdialog.cpp` shifted and the `appcontroller.cpp`/`languages.cpp` locations are
  stale from `01`/`02`; do not revert it or hand-edit locations.
- `Reverso` is the last free-service group on the page. Insert it before `addStretch()`: a widget
  added after the stretch is still found by the order assertion (its index is larger), but the
  stretch would sit above it and push the group to the bottom of the Services page.
- `tst_settingsdialog` counts include the implicit `initTestCase`/`cleanupTestCase`, so 26 now means
  24 named cases; the new case makes 27 passed, not 26.
- `tst_settingsdialog` is the same target; the new case must not add a test target or change the 39
  Linux / 34 Windows target count.
- Windows Debug/Release and its 34 tests are unavailable on this Linux machine; record them as not
  performed, not as passed.
- No user input is needed for this task; the optional visual check of the Services page is covered
  by the offscreen case.

## Outcome

Done. The Services page now lists a `Reverso` group after `Bing`, gated by the shared
`translation/allowFreeServices` checkbox, and the two new strings are translated in all 14 `.ts`
files. The full Linux suite passes 39/39 in Debug and Release and both configurations build
without warnings. Windows/MSVC is unavailable on this machine, so its build and its 34 tests are
recorded as not performed.

- `src/ui/settingsdialog.{h,cpp}` adds `m_reversoGroup`, `updateReversoControls()` and the
  `Reverso` group: one `QGroupBox` with a single wrapped `reversoHint` label in a `QVBoxLayout`,
  no `QLineEdit`, key or URL. The group sits between the Bing group and `servicesLayout->addStretch()`,
  so it is the last service block and the stretch stays last. The hint is one `tr()` with
  concatenated literals, and it names the unofficial status, the missing key, the six supported
  languages, the lack of auto-detection, the gate, the possible rate limits/blocks and the third
  party. `updateReversoControls()` is connected to `toggled` and called at the end of
  `loadValues()`, so the group is shown but disabled on the initial `AppSettings{}` state. The
  group stays visible when disabled; `settings()` and `validate()` are untouched, and no
  `AppSettings`/INI field or cache reset was added.
- `tests/tst_settingsdialog.cpp` adds `reversoGroup()` between `bingGroup()` and `llmModels()`. It
  checks the title, the hint text, `isHidden()` (shown but disabled) and `isEnabled()` for the gate,
  the absent `QLineEdit`, the order after the Bing group and the toggle in both directions. The
  case is part of the existing target, so no new target and no new test count beyond 27.
- All 14 `.ts` files now have 313 `<message>` entries (311 before), with no `unfinished`, `empty` or
  `vanished` entry. `Reverso` stays untranslated in every file; the hint is translated as one
  message with the same meaning in each language, and `Reverso` remains the display name in
  `ReversoService::displayName()` rather than a new `tr()` call. The Mozhi engine entry named
  `Reverso` in `src/ui/settingsdialog.cpp:349` was not touched. The `<location>` churn in all 14
  files is expected: `settingsdialog.cpp` moved and the `appcontroller.cpp`/`languages.cpp`
  locations were stale from `01`/`02`.
- Verification: `build/linux-debug` and `build/linux-release` build with zero warnings
  (`-Wall -Wextra`); `tst_settingsdialog` 27/27 and `tst_translations` 4/4 in both trees; the full
  suites pass 39/39 in both; `ctest -N` still shows 39. No service, `AppController`, parser,
  `doc/`, `AGENTS.md`, README, `doc/handoff.md`, root-plan or `07-fallback-chain` file was changed.
  The visual check of the Services page was not done by hand; the offscreen case covers it.
- Known gap: `AGENTS.md` still states 32 Windows/37 Linux tests, which is already deferred to
  `04-reverso-docs`. No real problems were left unfixed because of cost.
