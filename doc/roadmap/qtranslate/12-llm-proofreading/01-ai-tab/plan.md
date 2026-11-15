---
kind: task
status: done
detailed: true
---

# AI settings tab

## Task

Move the "OpenAI-compatible provider" group from the Services page to its own **AI** tab (approved by
the user on 2026-11-15) and leave everything else about the provider as it is. The settings stay the
single `[llm]` group (`baseUrl`, `apiKey`, `model`): translation and text checking share one
configuration, so this task adds no key, no consent switch and no second provider entry.

Why the move only: stage 12 makes the same provider a text-checking backend, and the checking
settings must point at one place for the model, the server and the key. The group is already wired to
`OpenAiCompatibleService` through `AppController`; moving the widgets does not touch that path, which
is the point of this task — it must be provable that translation behaviour did not change.

The connection check stays the existing **Load models** button (`loadLlmModels`, `GET /key` for
OpenRouter plus `GET /models`): it already reports whether the server answers and the key is accepted,
so no new button, endpoint or string is added for it.

## Steps

1. **Create the AI page** in `src/ui/settingsdialog.cpp`.

   - In `buildUi()`, immediately after `m_tabs->addTab(servicesPage, tr("Services"));` (`:499`),
     insert the page and **move** the group block `:313-358` into it verbatim:

     ```cpp
     // The OpenAI-compatible provider is configured here for translation and
     // text checking alike; the Services page keeps only a pointer (user
     // decision of 2026-11-15).
     auto *aiPage = new QWidget(m_tabs);
     auto *aiLayout = new QVBoxLayout(aiPage);

     auto *llmGroup = new QGroupBox(tr("OpenAI-compatible provider"), aiPage);
     // ... the block from :313-358, with servicesContent -> aiPage ...

     aiLayout->addStretch();
     m_tabs->addTab(aiPage, tr("AI"));
     ```

     Move, do not copy: the `llmGroup` construction, the `llmForm` rows, `m_llmBaseUrl`,
     `m_llmApiKey`, `m_llmModel`, `m_loadLlmModels`, `m_llmStatus`, `llmHint` and the
     `connect(m_loadLlmModels, ...)` lambda (`:351-357`) all travel together. Every `objectName`
     stays: `llmGroup`, `llmBaseUrl`, `llmApiKey`, `llmModel`, `loadLlmModels`, `llmStatus`,
     `llmHint`.

   - The tab order becomes General, Hotkeys, Translation, Services, **AI**, Text checking, Network.
     The page goes after Services and before Text checking on purpose: it is a provider configuration,
     and the checking provider points at it (child `04-llm-check-ui`).

2. **Leave a pointer on the Services page.** Where the group used to sit — after
   `servicesLayout->addWidget(deepLGroup);` (`:311`), before the MyMemory group (`:360`) — add:

   ```cpp
   auto *llmMovedHint = new QLabel(
       tr("The OpenAI-compatible provider is configured on the AI tab."), servicesContent);
   llmMovedHint->setObjectName(QStringLiteral("llmMovedHint"));
   llmMovedHint->setWordWrap(true);
   servicesLayout->addWidget(llmMovedHint);
   ```

   No member and no retranslation hook: nothing updates it. The same message is reused by
   `04-llm-check-ui` on the Text checking page, so it is translated once.

3. **Do not touch the data path.** `loadValues()` (`:730-733`), `settings()` (`:807-809`) and
   `showLlmModels()` (`:859-869`) read the members and keep working unchanged. `settingsdialog.h`
   needs no edit in this task. If a member has to change, the move went wrong: fix the move instead.

4. **Update `tests/tst_settingsdialog.cpp`.**

   a. `providerOrder()` (`:867-895`): remove `QStringLiteral("llmGroup")` from `names` (the list is
      the Services page order) and add after the loop:

      ```cpp
      auto *llmGroup = dialog.findChild<QGroupBox *>(QStringLiteral("llmGroup"));
      auto *servicesScroll = dialog.findChild<QScrollArea *>(QStringLiteral("servicesScroll"));
      auto *movedHint = dialog.findChild<QLabel *>(QStringLiteral("llmMovedHint"));
      QVERIFY(llmGroup && servicesScroll && movedHint);
      QVERIFY(!servicesScroll->widget()->isAncestorOf(llmGroup));
      QCOMPARE(movedHint->parentWidget(), servicesScroll->widget());
      auto *deepLGroup = dialog.findChild<QGroupBox *>(QStringLiteral("deepLGroup"));
      QVERIFY(deepLGroup);
      QVERIFY(layout->indexOf(movedHint) > layout->indexOf(deepLGroup));
      ```

      The pointer must be reachable through the Services layout after the DeepL group, and the group
      itself must not live on the Services page any more — that is the regression this case protects.

   b. `proofreadingTab()` (`:1107-1110`): the tab count and the tail indices change.

      ```cpp
      QCOMPARE(tabs->count(), 7);
      QCOMPARE(tabs->tabText(3), QStringLiteral("Services"));
      QCOMPARE(tabs->tabText(4), QStringLiteral("AI"));
      QCOMPARE(tabs->tabText(5), QStringLiteral("Text checking"));
      QCOMPARE(tabs->tabText(6), QStringLiteral("Network"));
      ```

   c. New `aiTab()` after `llmModels()` (`:897-940`), with a non-default `[llm]` configuration:

      ```cpp
      AppSettings settings;
      settings.llm.baseUrl = QStringLiteral("http://127.0.0.1:11434/v1");
      settings.llm.apiKey = QStringLiteral("llm-key");
      settings.llm.model = QStringLiteral("local-model");
      SettingsDialog dialog(settings, {QStringLiteral("en")},
                            {QStringLiteral("de"), QStringLiteral("en")}, true);

      auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("settingsTabs"));
      auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("llmGroup"));
      QVERIFY(tabs && group);
      QCOMPARE(group->title(), QStringLiteral("OpenAI-compatible provider"));
      QVERIFY(tabs->indexOf(group->parentWidget()) >= 0);
      QCOMPARE(tabs->tabText(tabs->indexOf(group->parentWidget())), QStringLiteral("AI"));

      auto *url = dialog.findChild<QLineEdit *>(QStringLiteral("llmBaseUrl"));
      auto *key = dialog.findChild<QLineEdit *>(QStringLiteral("llmApiKey"));
      auto *model = dialog.findChild<QComboBox *>(QStringLiteral("llmModel"));
      QVERIFY(url && key && model);
      QCOMPARE(url->text(), QStringLiteral("http://127.0.0.1:11434/v1"));
      QCOMPARE(key->text(), QStringLiteral("llm-key"));
      QCOMPARE(model->currentText(), QStringLiteral("local-model"));

      // The group still edits the one [llm] configuration.
      url->setText(QStringLiteral("http://127.0.0.1:8080/v1"));
      key->setText(QStringLiteral("new-key"));
      model->setCurrentText(QStringLiteral("other-model"));
      const AppSettings result = dialog.settings();
      QCOMPARE(result.llm.baseUrl, QStringLiteral("http://127.0.0.1:8080/v1"));
      QCOMPARE(result.llm.apiKey, QStringLiteral("new-key"));
      QCOMPARE(result.llm.model, QStringLiteral("other-model"));
      ```

      The `servicesTab()` case (`:361-431`) and `llmModels()` (`:897-940`) stay untouched: they find
      the widgets by object name and are the proof that loading, saving and the model list did not
      change.

5. **Regenerate and translate the 14 `.ts` files.** Copy `i18n/*.ts` to `/tmp/opencode/ts-before/`
   first, then from the repository root:

   ```
   cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build/linux-release --target update_translations
   ```

   **Do not call `lupdate` from `PATH`** (Qt 5.15, `/usr/bin/lupdate`) and **do not run the target in
   `qtranslate-trixie`** (Qt 6.8.2): both rewrite every file with their own version. Only the Qt 6.10.2
   CMake target is correct, as stage 11 recorded.

   Expected in each file: **384** `<message>` entries (382 + 2). The two new `SettingsDialog`
   sources are `AI` and `The OpenAI-compatible provider is configured on the AI tab.`; everything
   else must only be `<location>` churn — the dialog body moves by roughly 40 lines, so later
   locations in `settingsdialog.cpp` shift. If any other message arrives as `unfinished` or
   `vanished`, stop and report instead of accepting it.

   Translate both by hand in all 14 files (`ru_RU`, `es`, `de`, `fr`, `pt_BR`, `it`, `pl`, `uk`,
   `tr`, `zh_CN`, `ja`, `ko`, `ar`, `hi`), drop `type="unfinished"` and never retype a `<source>`.
   `AI` may stay `AI` where that abbreviation is the normal one in the target language (ru "AI",
   zh "AI", ja "AI" are all acceptable); follow the terminology the file already uses for the
   provider (for example how `llmHint` renders "OpenAI-compatible"). The pointer sentence keeps the
   tab name exactly as that file translates `AI`, so the two read as one. Then
   `cmake --build build/linux-release --target release_translations` and check that `lrelease`
   reports no unfinished message and `Generated translation file … (384 translations)`.

6. **Verify.**

   - host Debug and Release builds with `-Wall -Wextra`: no `warning:` line;
   - `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure` and the same for
     `build/linux-debug`: **50/50**, no new target, `ctest -N` still reports 50;
   - `tst_settingsdialog` grows by `aiTab()` (record the measured case count), `tst_settings` and
     `tst_openaicompatibleservice` are **not edited** and stay green — that is the proof the
     translation configuration is untouched;
   - `git status --porcelain` shows only `src/ui/settingsdialog.cpp`,
     `tests/tst_settingsdialog.cpp` and the 14 `i18n/qtranslate_*.ts` (plus the roadmap files the
     lead maintains). Nothing under `src/services/`, `src/core/`, `src/app/` or `src/ui/*.h`;
   - the 14-file check:

     ```
     for f in i18n/*.ts; do printf '%s %s\n' "$f" "$(rg -c '<message' "$f")"; done   # 384 each
     rg -n 'type="unfinished"|type="vanished"' i18n                                  # no output
     rg -c '<source>The OpenAI-compatible provider is configured on the AI tab.</source>' i18n/*.ts  # 1 each
     rg -c '<source>AI</source>' i18n/*.ts                                           # 1 each
     ```

   - Release build and the suite in `qtranslate-trixie` (Qt 6.8.2) as stage 11 ran it:
     `0` warnings, 50/50, which also proves trixie's `lrelease` accepts the catalogues;
   - Windows Debug/Release are **not performed** (no MSVC here), reviewed by diff; the Windows target
     count stays 44.

## Done when

- The settings dialog has seven tabs; the fifth is **AI**, holds the `llmGroup` with `llmBaseUrl`,
  `llmApiKey`, `llmModel`, `loadLlmModels`, `llmStatus` and `llmHint`, and Services no longer holds
  any of those widgets.
- The Services page shows `llmMovedHint` after the DeepL group, word-wrapped, with the sentence that
  points at the AI tab.
- `[llm]` is still the only configuration: `loadValues()`, `settings()` and `showLlmModels()` behave
  as before, `tst_settings` and `tst_openaicompatibleservice` pass without an edit, and no file under
  `src/services/`, `src/core/` or `src/app/` changes.
- `tst_settingsdialog` covers the tab count, the tab order, the AI page membership of the group, the
  pointer on Services and the unchanged round trip of the three fields.
- The 14 `.ts` files have 384 messages each, both new messages are translated, and no entry is
  unfinished or vanished; `release_translations` and `tst_translations` pass.
- Debug and Release build without warnings and all 50 Linux tests pass on the host and in
  `qtranslate-trixie`; Windows is recorded as not performed.

## Risks

- **A copy instead of a move** would leave two sets of fields with the same object names; the dialog
  would silently edit one and save the other. The `providerOrder()` assertions are the guard.
- **Renaming an object name** breaks `tst_settingsdialog::servicesTab()`/`llmModels()`, which are
  the proof that loading and saving are unchanged. Keep every name.
- **The tab index is asserted in a test.** Update the expectations; do not reorder the tabs to make
  the old numbers pass.
- **`<location>` churn in all 14 files is expected** because the block moves. Never retype a
  `<source>`, or the translation disconnects from the binary and the dialog silently falls back to
  English.
- **`lupdate` from `PATH` is Qt 5.15** and the trixie image has 6.8.2; only `update_translations`
  from the host Qt 6.10.2 build is safe.
- **The label is not a replacement for the AI tab.** The hint must be a pointer, not a second copy
  of the provider fields; `providerOrder()` asserts the group is not on the Services page.
- **Nothing is needed from the user:** no key, no server, no live check. The visual result is covered
  by the offscreen dialog case.

## Outcome

Done on 2026-11-15 (this commit, base `5e6f1cb`). The settings dialog has seven tabs — General,
Hotkeys, Translation, Services, **AI**, Text checking, Network — and `llmGroup` with `llmBaseUrl`,
`llmApiKey`, `llmModel`, `loadLlmModels`, `llmStatus` and `llmHint` was moved, not copied, from the
Services page to the new `aiPage`; every `objectName` and the single `connect(m_loadLlmModels, …)`
travelled with it. Services keeps `llmMovedHint` (`The OpenAI-compatible provider is configured on
the AI tab.`, word-wrapped, no member) after `deepLGroup`. `settingsdialog.h`, `loadValues()`,
`settings()`, `showLlmModels()` and `llmModelsRequested` are untouched, and nothing under
`src/services`, `src/core`, `src/app`, `src/net` or `src/ai` changed, so `[llm]` is still the one
configuration that translation uses and text checking will use.

Tests: `tst_settingsdialog` grew by `aiTab()` — 35 cases, `Totals: 37 passed` with
`initTestCase`/`cleanupTestCase`; `providerOrder()` no longer lists `llmGroup` among the Services
groups and asserts that the group is not a descendant of `servicesScroll->widget()` while
`llmMovedHint` sits in the Services layout after `deepLGroup`; `proofreadingTab()` expects seven tabs
with Services, AI, Text checking and Network at 3–6. `servicesTab()` and `llmModels()` were not
edited.

Translations: `update_translations` from the host Qt 6.10.2 tree (never `/usr/bin/lupdate`) brought
all 14 `.ts` files to 384 messages; `AI` and the pointer sentence are translated by hand in every
file, `release_translations` reports `384 finished and 0 unfinished` for each, and trixie's `lrelease`
accepts the same catalogues. The only other churn is `<location>`: no `<source>` and no existing
translation changed. `AI` is rendered `AI` (ru, pl, zh_CN, ja, ko), `ШІ` (uk), `KI` (de), `IA` (es,
fr, it, pt_BR), `YZ` (tr), `الذكاء الاصطناعي` (ar) and `एआई` (hi); the pointer sentence uses the same
word in each file.

Verification: host Release and Debug were rebuilt from scratch with `-Wall -Wextra` — 0 warnings,
50/50 tests each, `ctest -N` still 50 targets; `qtranslate-trixie` (Qt 6.8.2) Release — 0 warnings,
50/50. Windows Debug/Release were not performed (no MSVC) and stay at 44 targets. In the container
`xvfb-run -a` hangs after `ctest`, so the run used a manually started `Xvfb`; under
`--user $(id -u):$(id -g)` uid 1000 is absent from the image's `/etc/passwd` and
`tst_hotkeymanager_wayland` fails in `initTestCase` (`dbus-daemon did not print a session address`),
so the container run was made as root, as in CI.

Review found no defects. Deferred to the lead's documentation pass: `README.md:141` and
`README.ru.md:145` still send the user to `Settings → Services → OpenAI-compatible provider`, and
`AGENTS.md:58` still says 382 messages. `src/services/libretranslateservice.cpp:408` also says "382
entries per file"; that is a comment-only fix, left for a later task because this one must not touch
`src/services`.
