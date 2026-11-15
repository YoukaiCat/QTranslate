---
kind: task
status: done
detailed: true
depends: [01-duckduckgo-service]
---

# DuckDuckGo settings group and translations

## Task

Add the `DuckDuckGo` group to the Services page of `SettingsDialog`, translate its hint in all 14
`.ts` files and cover the group with a `tst_settingsdialog` case. The `duckduckgo` service, its
parser, its language column and its fallback id exist (commit `e0b411b`, node
`01-duckduckgo-service`); this task changes no service, parser, language or settings-storage file.
The provider has no key and no user-editable endpoint (research note §2.3, §4), so the group has no
input field, no `AppSettings`/INI key, no normalization, no `applySettings()` and no `settings()` or
`validate()` change. The only code changes are one group in `src/ui/settingsdialog.cpp` and the test
edits in `tests/tst_settingsdialog.cpp`.

The group follows `Reverso` (`reversoGroup`, `src/ui/settingsdialog.cpp:422-434`) and sits directly
before `servicesLayout->addStretch()` (`:436`), so it is the last block on the page and the page
order stays Google, Yandex, Bing, DeepL, OpenAI-compatible, MyMemory, Mozhi, Reverso, DuckDuckGo.
`03-libretranslate` is a later stage and will append its own group after this one, so nothing here
needs to reserve a place for it.

The group is one `QGroupBox` with objectName `duckduckgoGroup` and one word-wrapped `QLabel` with
objectName `duckduckgoHint` in a `QVBoxLayout`, exactly like `Reverso`. The title is
`tr("DuckDuckGo")` for consistency with the neighbouring groups (`Bing`, `Reverso`, `MyMemory`,
`Mozhi` are all `tr()` with the identical product name as the translation), but the product name is
not translated: every `.ts` gets `<translation>DuckDuckGo</translation>`.

The hint carries, in one `tr()` message: the unofficial status and that no key is needed, that the
source language can be detected, that the target languages follow `Bing Translate` and only part of
that list was confirmed against the endpoint (the probe confirmed fifteen codes; the implementation
inherits the Bing catalogue plus `es`, so no unverified code may be promised in the UI), that the
endpoint may stop working, rate-limit or block requests, and that long text is sent in several
requests. It must **not** contain a "the text is sent to a third party" sentence: user decision
2026-11-15 removed those sentences from every provider hint (stage `10-provider-hint-cleanup`), and
the provider-specific warnings stay.

## Steps

1. **Add the group.** In `src/ui/settingsdialog.cpp`, insert the block between
   `servicesLayout->addWidget(reversoGroup);` (`:434`) and `servicesLayout->addStretch();` (`:436`):

   ```cpp
    auto *duckDuckGoGroup = new QGroupBox(tr("DuckDuckGo"), servicesContent);
    duckDuckGoGroup->setObjectName(QStringLiteral("duckduckgoGroup"));
    auto *duckDuckGoLayout = new QVBoxLayout(duckDuckGoGroup);
    auto *duckDuckGoHint = new QLabel(
        tr("DuckDuckGo is an unofficial endpoint and needs no key. It can detect the source "
           "language. Its target languages follow Bing Translate, and some of them were "
           "confirmed against the endpoint. It may stop working, apply rate limits or block "
           "requests. Long text is sent in several requests."),
        duckDuckGoGroup);
    duckDuckGoHint->setObjectName(QStringLiteral("duckduckgoHint"));
    duckDuckGoHint->setWordWrap(true);
    duckDuckGoLayout->addWidget(duckDuckGoHint);
    servicesLayout->addWidget(duckDuckGoGroup);
   ```

   Requirements that the test and the `.ts` count depend on:

   - **one** `tr()` call with concatenated literals for the hint; splitting it into two messages
     makes the `.ts` count 377 instead of 376 and breaks the acceptance count;
   - no `QFormLayout`, no `QLineEdit`, `QComboBox`, `QPushButton` or `QCheckBox` — there is nothing
     to configure, and the test asserts the group has no input field;
   - no member in `src/ui/settingsdialog.h`: unlike `m_reversoGroup` and `m_mozhiHint` (`:155`)
     nothing is enabled, hidden or re-read later, so the local pointers are enough. No new
     `connect()`, no call from `loadValues()`, no `updateDuckDuckGoControls()`, and `settings()`,
     `validate()` and `applySettings()` stay untouched;
   - `setObjectName` is exactly `duckduckgoGroup` and `duckduckgoHint` (lowercase, like
     `mymemoryGroup`/`mozhiGroup`);
   - leave the provider-order comment at `:219-220` as it is: the new group keeps the same order the
     comment names (`AppController::createTranslation()` and `fallbackServiceIds()` already register
     `duckduckgo` after `reverso`, `src/core/settings.cpp:315`).

2. **Cover the group in `tst_settingsdialog`.** Three edits in `tests/tst_settingsdialog.cpp`; no
   new test target and no new include (`QGroupBox`, `QLabel`, `QLayout` and `QLineEdit` are already
   included).

   a. Add a case after `reversoGroup()` (`:636-660`) and before `fallbackRowOnTranslationPage()`
      (`:662`):

   ```cpp
    void duckduckgoGroup()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en"),
                               QStringLiteral("ru")},
                              true);
        auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("duckduckgoGroup"));
        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("duckduckgoHint"));
        auto *reverso = dialog.findChild<QGroupBox *>(QStringLiteral("reversoGroup"));
        QVERIFY(group);
        QVERIFY(hint);
        QVERIFY(reverso);

        QCOMPARE(group->title(), QStringLiteral("DuckDuckGo"));
        QVERIFY(!group->isHidden());
        QVERIFY(group->isEnabled());
        QVERIFY(hint->wordWrap());
        QVERIFY(hint->text().contains(QStringLiteral("unofficial")));
        QVERIFY(hint->text().contains(QStringLiteral("no key")));
        QVERIFY(hint->text().contains(QStringLiteral("source language")));
        QVERIFY(hint->text().contains(QStringLiteral("Bing")));
        QVERIFY(hint->text().contains(QStringLiteral("several requests")));
        QVERIFY(!hint->text().contains(QStringLiteral("third party")));
        QVERIFY(group->findChildren<QLineEdit *>().isEmpty());

        const QLayout *servicesLayout = group->parentWidget()->layout();
        QVERIFY(servicesLayout);
        QVERIFY(servicesLayout->indexOf(reverso) >= 0);
        QVERIFY(servicesLayout->indexOf(reverso) < servicesLayout->indexOf(group));
        // The stretch must stay last, so the group is the last block on the
        // Services page rather than a widget pushed below the stretch.
        QVERIFY(servicesLayout->itemAt(servicesLayout->count() - 1)->spacerItem());
    }
   ```

   The hint assertions are substrings only — never `QCOMPARE` on the whole hint, which would break
   on every wording tweak. `!group->isHidden()` (not `isVisible()`) is the visibility check: the
   dialog is never shown, so every child of it is `isVisible() == false` and the assertion would pass
   for the wrong reason.

   b. Add `"duckduckgoGroup"` to the list in `freeWebGateIsGone()` (`:440-441`), so the
      "every free provider group exists and is enabled" check keeps covering all groups:

   ```cpp
        for (const char *name : {"mymemoryGroup", "mozhiGroup", "yandexGroup", "bingGroup",
                                 "reversoGroup", "duckduckgoGroup"}) {
   ```

   c. Append `QStringLiteral("duckduckgoGroup")` after `QStringLiteral("reversoGroup")` in the
      `names` list of `providerOrder()` (`:769-774`), keeping `deepLGroup` formatting intact.

3. **Regenerate the `.ts` sources after the code edit.** First copy the 14 `i18n/qtranslate_*.ts` to
   `/tmp/opencode/ts-before/` (step 5 compares against that copy). Then, from the repository root:

   ```
   cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build/linux-release --target update_translations
   ```

   Reconfigure first: the existing tree must already know the two new test targets from
   `01-duckduckgo-service`, and `update_translations` regenerates its `lupdate` project from the
   current `CMakeLists.txt` (`QTRANSLATE_TS_FILES` at `:171-186`,
   `QTRANSLATE_LUPDATE_EXTRA_SOURCES` at `:188-194`, `qt_add_translations` at `:196-200`).

   **Do not call `lupdate` directly and do not run this target inside the `qtranslate-trixie`
   container.** The `lupdate` in `PATH` on this host is Qt **5.15.8** (`/usr/bin/lupdate`, a
   qtchooser wrapper) and would downgrade every `.ts`; the Qt 6 tools live in
   `/usr/lib/qt6/bin/lupdate` (6.10.2), and the CMake target is the only supported entry point. The
   trixie image has Qt 6.8.2, whose `lupdate` would also rewrite the files.

   Expect in each of the 14 files: **376** `<message>` entries (374 + `DuckDuckGo` + the hint), the two
   new sources in the `SettingsDialog` context, both as `type="unfinished"`, 0 `type="vanished"`, and
   `<location>` churn (the file grows by the inserted block, so later locations shift down by
   that amount; `lupdate` also re-sorts messages by location). If anything shows up as vanished, rerun
   `lupdate -no-obsolete -project build/linux-release/.lupdate/qtranslate_lupdate_project.json` and
   check the count again; if an unexpected message changed, stop and report instead of accepting it.

4. **Translate the two messages by hand in all 14 files.** Fill `DuckDuckGo` with the identical
   product name in every file (`ru_RU`, `es`, `de`, `fr`, `pt_BR`, `it`, `pl`, `uk`, `tr`, `zh_CN`,
   `ja`, `ko`, `ar`, `hi`), remove `type="unfinished"`, and translate the hint as one message.

   The source string is exactly:

   ```
   DuckDuckGo is an unofficial endpoint and needs no key. It can detect the source language. Its target languages follow Bing Translate, and some of them were confirmed against the endpoint. It may stop working, apply rate limits or block requests. Long text is sent in several requests.
   ```

   Rules for the translation:

   - `DuckDuckGo` and `Bing` stay untranslated product names in every language;
   - reuse the wording of the existing `Bing web is an unofficial endpoint` / `Reverso is an
     unofficial endpoint` hint in **the same file** where the meaning is identical (the
     "unofficial/no key" opener and the two closing sentences "It may stop working, apply rate
     limits or block requests. Long text is sent in several requests."). Only adjust a pronoun if the
     reused sentence no longer agrees with its subject;
   - the middle two sentences are new and must keep all three facts: the source language can be
     detected; the target languages follow Bing Translate; only part of that list was confirmed
     against the endpoint. Do not enumerate the probe's codes and do not say that every Bing
     language works;
   - no `%` placeholder, no leading or trailing space, no `→`/`✓`/emoji, and no "the text is sent to a
     third party" sentence.

   `ru_RU` reference (the last two sentences are the Reverso/Bing tail of that file, verbatim):

   ```
   DuckDuckGo — неофициальный эндпоинт, ключ не нужен. Он определяет исходный язык. Набор целевых языков такой же, как у Bing Translate, и часть из них проверена на эндпоинте. Он может перестать работать, ограничивать частоту запросов или блокировать их. Длинный текст отправляется несколькими запросами.
   ```

   Then compile the catalogues once: `cmake --build build/linux-release --target
   release_translations` and check that `lrelease` reports no unfinished message for the 14 files
   (`Generated translation file … (376 translations)`), before moving to step 5.

5. **Verify.** Build and test in this order:

   - host Debug and Release: `cmake -S . -B build/linux-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug`
     and the existing `build/linux-release`; `cmake --build` each and grep the logs for
     `warning:` — `-Wall -Wextra` must stay clean;
   - `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure` and the same for
     `build/linux-debug`: **48/48** (the count does not change — the new case lives in the existing
     `tst_settingsdialog` target), `ctest -N` still reports 48;
   - `tst_settingsdialog` must report **35** passes (34 before plus the new case; the 34 are 32 named
     cases plus QTest's implicit `initTestCase`/`cleanupTestCase`), and `tst_translations` 4/4;
   - `.ts` checks:

     ```
     for f in i18n/*.ts; do printf '%s %s\n' "$f" "$(rg -c '<message' "$f")"; done   # 376 each
     rg -n 'type="unfinished"|type="vanished"' i18n                                 # no output
     rg -c '<source>DuckDuckGo</source>' i18n/*.ts                                  # 1 per file
     rg -n -A1 '<source>DuckDuckGo is an unofficial endpoint' i18n                  # 14 translations
     ```

     plus a throwaway `python3 - <<'PY'` here-document (no file in the repository) that, for each of
     the 14 files, asserts: 376 messages; every `<source>` of `/tmp/opencode/ts-before/<file>` still
     present (nothing vanished); the two new sources present once; no unfinished, vanished or empty
     translation; the `DuckDuckGo` translation equals the source; and the new hint translation ends
     with the last two sentences of the `Bing web is an unofficial endpoint` translation of the same
     file. Print every new translation and read all 14;
   - Release build in the container (host stays responsive):
     `docker run --rm -v "$PWD:/src:ro" -w /src qtranslate-trixie bash -c 'cmake -S /src -B
     /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/build --parallel &&
     xvfb-run -a ctest --test-dir /tmp/build --output-on-failure'` — 0 warnings, 48/48. This also
     proves that trixie's `lrelease` 6.8.2 accepts the `.ts` written by lupdate 6.10.2;
   - boundary: `rg -n "third party" src/ui/settingsdialog.cpp` finds nothing;
     `git status --porcelain` shows only `src/ui/settingsdialog.cpp`,
     `tests/tst_settingsdialog.cpp` and the 14 `i18n/qtranslate_*.ts` (plus the roadmap and
     documentation files the lead maintains). No `src/services`, `src/core`, `src/app`,
     `CMakeLists.txt`, `tests/CMakeLists.txt`, `doc/`, `AGENTS.md`, `README*.md` or
     `doc/handoff.md` change.
   - Windows Debug/Release and its 40 tests have no MSVC here: record them as **not performed**, not
     as passed. The changed strings and the test are shared code, so a Windows build sees the same
     sources.

## Done when

- The Services page shows a `DuckDuckGo` group as the last block, after `Reverso` and above the
  stretch, with one word-wrapped `duckduckgoHint` label and no input field; the dialog's object names
  are `duckduckgoGroup` and `duckduckgoHint`.
- The hint states the unofficial status, that no key is needed, that the source language can be
  detected, that the target languages follow Bing Translate with only part of the list confirmed
  against the endpoint, that the endpoint may stop working, rate-limit or block requests, and that
  long text is sent in several requests; it contains no "third party" sentence and promises no
  unverified language code.
- `tst_settingsdialog::duckduckgoGroup()` passes and checks the title, the hint substrings, the
  absence of `third party`, the absent `QLineEdit`, `!isHidden()`, the position after `Reverso` and
  the stretch staying last; `freeWebGateIsGone()` and `providerOrder()` include the group. No new
  test target, and `tst_settingsdialog` reports 35 passes.
- All 14 `.ts` files have 376 `<message>` entries, the two new sources, no unfinished, vanished or
  empty translation, and a hand-written hint translation that keeps `DuckDuckGo` and `Bing`
  untranslated; `release_translations` and `tst_translations` pass.
- Debug and Release build with zero warnings on the host and in `qtranslate-trixie`, all 48 Linux
  tests pass in both host configurations (48/48 in the container as well), `ctest -N` still shows
  48, and Windows is recorded as not performed (no MSVC).
- Only `src/ui/settingsdialog.cpp`, `tests/tst_settingsdialog.cpp` and the 14 `i18n/qtranslate_*.ts`
  change in code; no service, parser, language, settings-storage, `AppController`, CMake or
  documentation file is touched.

## Risks

- `isVisible()` is false for every child of a never-shown dialog; use `isHidden()` for explicit
  visibility and `isEnabled()` for the state, as `reversoGroup()` and `bingGroup()` do.
- The group must be inserted **before** `servicesLayout->addStretch()`. A widget added after the
  stretch still has a larger layout index, so the order assertion alone would pass while the page
  shows the block pushed to the bottom; the `itemAt(count - 1)->spacerItem()` check guards that.
- The hint must stay one `tr()` with concatenated literals. Two `tr()` calls mean 377 messages and
  two half-hints in the UI; the count check in step 5 catches it.
- `lupdate` on this host resolves through `qtchooser` to Qt **5.15** and would rewrite the `.ts` in
  an older format and mark messages vanished. Only the `update_translations` target (Qt 6.10.2) is
  safe, and it must not run in the trixie image.
- `<location>` churn in all 14 files is expected (the C++ file grows by the inserted block); do not
  revert it and never retype a `<source>` by hand — a hand-written source disconnects the translation
  from the executable and the UI silently falls back to English.
- The hint is the only warning about this route. Wording that promises the full Bing list, or a
  list of the fifteen probed codes, over-claims: the implementation inherits the Bing catalogue and
  the endpoint answers `400 {"error":"invalid_to"}` for anything it does not accept. Do not weaken
  the unofficial/rate-limit/block sentences either.
- Translating the product name `DuckDuckGo` (or `Bing` inside the hint) is a defect: the `.ts`
  translation must equal the source name in all 14 files, like `Bing`, `Reverso` and `MyMemory`.
- `tst_settingsdialog` counts include QTest's implicit `initTestCase`/`cleanupTestCase`, so the new
  case makes 35 passes (33 named cases plus those two), not 33; the target count (48 Linux, 40
  Windows) must not move.
- Nothing is needed from the user: no key, no endpoint, no live check. A live smoke against
  `duckduckgo.com` stays out of scope (the parser/service tests are offline, and the group is a
  static label); an optional visual check of the Services page in Russian is covered by the
  offscreen case.

## Outcome

Done. `src/ui/settingsdialog.cpp:436-449` holds the `duckduckgoGroup` block between
`servicesLayout->addWidget(reversoGroup);` and `servicesLayout->addStretch();`, so the Services page
order is Google, Yandex, Bing, DeepL, OpenAI-compatible, MyMemory, Mozhi, Reverso, DuckDuckGo and the
stretch stays last. The group is a `QGroupBox` with the untranslated `DuckDuckGo` title, one
`QVBoxLayout` and one word-wrapped `QLabel` (`duckduckgoGroup` / `duckduckgoHint`); the hint is a
single `tr()` with concatenated literals and matches step 1 verbatim, so it carries the unofficial
status, the missing key, the source-language detection, the Bing Translate target list with only part
of it confirmed against the endpoint, the possible shutdown, rate limits and blocks, and the long text
in several requests, and no "third party" sentence. There is no input field, no member in
`src/ui/settingsdialog.h`, no `connect()`, no `updateDuckDuckGoControls()` and no `loadValues()`,
`settings()`, `validate()` or `applySettings()` change: the provider has no key and no user-editable
endpoint, so it needs no `AppSettings`/INI key either.

`tests/tst_settingsdialog.cpp` gains `duckduckgoGroup()` after `reversoGroup()` — substrings only,
`isHidden()`/`isEnabled()`, word wrap, the absent `QLineEdit`, the absent `third party`, the position
after `reversoGroup` and `itemAt(count - 1)->spacerItem()` — and `duckduckgoGroup` joins the loop of
`freeWebGateIsGone()` and the `names` list of `providerOrder()`. No new test target and no new
include, so the suite stays at 48 Linux targets while `tst_settingsdialog` reports 35 passes.

The 14 `i18n/qtranslate_*.ts` were regenerated through the `update_translations` target (Qt 6.10.2
`lupdate`; never the Qt 5.15 `lupdate` in `PATH`, never the trixie image) and both new messages were
translated by hand, then `release_translations` compiled them: **376** `<message>` per file, no
`unfinished`, no `vanished`, no empty translation, `DuckDuckGo` and `Bing` untranslated in every
language, and the closing two sentences identical to the `Bing web is an unofficial endpoint` tail of
the same file.

Checks: host Debug and Release build warning-free and pass 48/48 (`ctest -N` 48), `tst_settingsdialog`
35 and `tst_translations` 4/4; a Release build and the suite in `qtranslate-trixie` (Qt 6.8.2) are
warning-free and 48/48, which also proves trixie's `lrelease` accepts the `.ts` written by lupdate
6.10.2. Windows Debug/Release and its 40 tests are recorded as **not performed** (no MSVC here); the
changed strings and the test are shared sources.

What turned out on the way:

- The `<location>` churn in the `.ts` is larger than the inserted block alone: `i18n/` was last
  regenerated at `589dbb5`, and `e0b411b` moved lines in `appcontroller.cpp`, `settings.cpp` and
  `languages.cpp`. No `<source>` and no existing `<translation>` changed, so only locations were
  refreshed along with the two new messages.
- `xvfb-run -a ctest` inside `qtranslate-trixie` hung three times when `docker run` piped the
  container's stdout to the host (Xvfb started, `ctest` never appeared); redirecting the log inside
  the container passes the same suite in 25 s. Verification commands for that image should redirect
  the log inside the container or wrap `ctest` in `timeout`, otherwise a hang looks like a failing
  test.
- Deferred to the lead's documentation pass (this task does not edit `doc/` or the READMEs): the
  Services page group list in `doc/architecture.md` does not name DuckDuckGo yet, and
  `doc/free-endpoints.md` still says "five no-key web services" and omits DuckDuckGo from its
  integration list.
