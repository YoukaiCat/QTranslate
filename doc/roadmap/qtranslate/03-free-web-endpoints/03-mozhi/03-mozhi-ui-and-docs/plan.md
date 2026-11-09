---
kind: task
status: done
detailed: true
depends: [02-mozhi-settings]
---

# Mozhi settings UI, translations and docs

## Task

Expose Mozhi in the Services page, translate the new strings, and bring the architecture,
free-endpoints and test-count documents in line. This task changes no service, parser or translation
behaviour; those are done in `01-mozhi-service` and `02-mozhi-settings`. It keeps the stage
decisions: no hidden instance rotation, no key field, the default instance
`https://mozhi.adminforge.de`, the default engine `google`, and the seven documented engine ids.

The group is a `Mozhi` `QGroupBox` placed after the `Free web services` group and after the
`MyMemory` group, so the gate checkbox stays above both. It has an editable `QLineEdit` with
objectName `mozhiInstance` (prefilled with `settings.mozhi.instance`, placeholder
`https://mozhi.adminforge.de`), a `QComboBox` with objectName `mozhiEngine` (the seven entries from
`mozhiEngineIds()`, in that order, display names Google, DeepL, DuckDuckGo, LibreTranslate, MyMemory,
Reverso and Yandex), a word-wrapped `QLabel` with objectName `mozhiEffectiveInstance` showing
`Effective instance: <normalizedMozhiInstance(text)>`, a word-wrapped `QLabel` with objectName
`mozhiInstanceWarning` that is visible only when `isValidMozhiInstance(text)` is false and says that
a base http(s) instance URL (without credentials, a query or a fragment) is required and the default
instance will be used, and a word-wrapped `mozhiHint` that says that public instances are
unofficial, may stop working or rate-limit requests, that some engines may be disabled on an
instance, that a self-hosted instance's terms and stability are the user's responsibility, that the
opt-in gate above must be enabled, that long text is sent in several requests, and that the text
goes to the selected instance and engine. The group is visible but disabled while
`translation/allowFreeServices` is off and follows the gate checkbox via `updateMozhiControls()`;
call it at the end of `loadValues()` so the default-off dialog starts disabled. Typing a valid but
uncanonical URL (a trailing slash) is not a warning; the effective label shows the canonical value.
`settings()` returns the normalized instance and the selected engine so the dialog's copy is already
effective; `validate()` adds no new failure condition because normalization replaces invalid input.

`tst_settingsdialog` gets a `mozhiGroup` case: find `mozhiGroup`, `mozhiHint`, `mozhiInstance`,
`mozhiEngine`, `mozhiEffectiveInstance` and `mozhiInstanceWarning`; with `AppSettings{}` the group is
disabled, the engine is `google`, the instance shows the default and the warning is hidden; checking
`allowFreeServices` enables the group and unchecking disables it; the combo has seven entries with
the documented data ids and display names; typing `ftp://host` (or an empty field, or
`https://host/?engine=google`) shows the warning and the effective default, typing
`http://127.0.0.1:8080/mozhi/` hides the warning and shows `http://127.0.0.1:8080/mozhi`;
`dialog.settings().mozhi` carries the normalized values. Add a second case that loads an
`AppSettings` with a custom instance and engine and verifies the widgets.

The new `SettingsDialog` messages go into all 14 `.ts` files: the group title `Mozhi`, the
`Instance URL:` and `Engine:` labels, `Effective instance: %1`, the instance warning, and the long
`mozhiHint`. The engine names are product names and are not wrapped in `tr()`. Run `lupdate` over
`src` with `--no-obsolete` including `src/platform/hotkeymanager_win.cpp` in the scan (the CMake
`QTRANSLATE_LUPDATE_EXTRA_SOURCES` already does this on Linux), fill every new message in all 14
`i18n/qtranslate_*.ts`, and keep 0 new/0 vanished; `tst_translations` must pass. Record the actual
message count in the outcome instead of guessing; it should be about 301 → 307.

`doc/architecture.md`:
- §3.1 adds `mozhiservice.h/.cpp` and `mozhiparser.h/.cpp` to the services tree, `mozhi` to the
  fixtures line and `tst_mozhiparser`/`tst_mozhiwebservice` to the tests tree.
- §3.2 adds the Mozhi sources and tests to the target lists and changes the counts to 28 Windows and
  33 Linux.
- §4.5 adds `mozhi` to the `id()` example and says that `DeepLWebService`, `MyMemoryService` and
  `MozhiService` use the queue, with MyMemory at 350 ms and Mozhi at 500 ms.
- A new §4.7.4 `MozhiService — free multi-engine proxy` after §4.7.3 documents the editable instance,
  the engine list/default, the GET `/api/translate` query, the opt-in gate, `auto` and the
  `UnsupportedLanguage` behaviour, the 5000 UTF-16 units via `TextChunker::split`, the 500 ms
  assumption, the same-origin redirect policy, the response fields (`translated-text`, `detected`,
  `source_language`, `target_transliteration`), the error table (429/403/rate-limit/status-code-403/429,
  413/414, 5xx, HTML, invalid language, disabled engine, JSON error, malformed/missing text,
  timeout) and the tests.
- §4.8 notes that Mozhi reuses the Google column (`zh-CN`, `zh-TW`, `iw`, `jw`, `tl`, `no`, `pt`)
  and that `mozhiCodeOf()` is the single fix point; no `LanguageInfo` column is added.
- §4.9 notes that Mozhi is the second real opt-in consumer; the gate logic is unchanged.
- §4.11 adds `mozhi` to the `AppSettings` group list and the `mozhi/instance` and `mozhi/engine`
  rows to the file-key table with the defaults, the normalization rules, the engine ids and the
  statement that the format version stays 1.
- §8 adds `tst_mozhiparser` and `tst_mozhiwebservice` rows and updates `tst_languages`,
  `tst_settings` and `tst_settingsdialog`; the new `tst_mozhiwebservice` row also includes the
  `applySettings()` cases added in `02-mozhi-settings`.
- §4.14 changes 31→33, 23 shared→25 shared and "the other 23 tests" to "the other 25 tests"; the
  historical 0.12.0 sentence stays unchanged. The 26→28 Windows count is in §3.2.
- §9.2 item 8 (free web endpoints) stops listing Mozhi with the unimplemented endpoints and names it
  as the second implemented opt-in free service; this is a one-line consistency fix beyond the
  enumerated sections.

`doc/free-endpoints.md`: update the status line and date, add an "Implementation in qtranslate" note
in §5 (request/response, instance/engine, defaults, 500 ms, 5000 units, `auto`, errors, gate, tests),
mark Mozhi implemented in §8 items 2-4 and §9 item 3, and keep the instance/engine bullets and the
privacy rules.

`AGENTS.md`: change the build/test sentence from 26/31 to 28/33. Do not touch `README.md`,
`README.ru.md`, `doc/handoff.md`, the root `plan.md` or the `07-fallback-chain` plans; the final
provider/fallback documentation and counters stay with `07-fallback-chain`/the lead.

**Verification and manual checks.** Build Debug and Release on Linux with `-Wall -Wextra`; run the
full suite with `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`; expect 33/33
and zero `warning:` lines. Check the Services page offscreen at the default gate-off state and after
checking the gate; check that the pickers include Mozhi only with the gate on and that an INI
`translation/service=mozhi` with the gate off keeps Google active and logs the existing warning.
Record the optional live smoke (the default instance and one self-hosted or alternate public
instance, a short `en→ru`, a long multi-chunk text and a disabled-engine case) as passed or as not
performed with the reason (a public instance may be down or rate-limited). Record Windows
Debug/Release and its 28 tests as not performed (no MSVC).

## Steps

1. **Add the `Mozhi` group to `SettingsDialog`.** In `src/ui/settingsdialog.h`, add the private
   method `void updateMozhiControls();` after `updateMyMemoryControls();` and the members after
   `m_mymemoryGroup`:

   ```cpp
   QGroupBox *m_mozhiGroup = nullptr;
   QLineEdit *m_mozhiInstance = nullptr;
   QComboBox *m_mozhiEngine = nullptr;
   QLabel *m_mozhiEffectiveInstance = nullptr;
   QLabel *m_mozhiInstanceWarning = nullptr;
   QLabel *m_mozhiHint = nullptr;
   ```

   In `src/ui/settingsdialog.cpp`, add `<QHash>` to the includes. In `buildUi()`, after
   `servicesLayout->addWidget(myMemoryGroup);` (`:326`) and before `servicesLayout->addStretch();`
   (`:331`), create the group and add it to `servicesLayout`:

   ```cpp
   m_mozhiGroup = new QGroupBox(tr("Mozhi"), servicesContent);
   m_mozhiGroup->setObjectName(QStringLiteral("mozhiGroup"));
   auto *mozhiLayout = new QVBoxLayout(m_mozhiGroup);
   auto *mozhiForm = new QFormLayout;
   m_mozhiInstance = new QLineEdit(m_mozhiGroup);
   m_mozhiInstance->setObjectName(QStringLiteral("mozhiInstance"));
   m_mozhiInstance->setPlaceholderText(MozhiSettings{}.instance);
   mozhiForm->addRow(tr("Instance URL:"), m_mozhiInstance);
   m_mozhiEngine = new QComboBox(m_mozhiGroup);
   m_mozhiEngine->setObjectName(QStringLiteral("mozhiEngine"));
   const QHash<QString, QString> engineNames = {
       {QStringLiteral("google"), QStringLiteral("Google")},
       {QStringLiteral("deepl"), QStringLiteral("DeepL")},
       {QStringLiteral("duckduckgo"), QStringLiteral("DuckDuckGo")},
       {QStringLiteral("libretranslate"), QStringLiteral("LibreTranslate")},
       {QStringLiteral("mymemory"), QStringLiteral("MyMemory")},
       {QStringLiteral("reverso"), QStringLiteral("Reverso")},
       {QStringLiteral("yandex"), QStringLiteral("Yandex")},
   };
   for (const QString &id : mozhiEngineIds())
       m_mozhiEngine->addItem(engineNames.value(id, id), id);
   mozhiForm->addRow(tr("Engine:"), m_mozhiEngine);
   mozhiLayout->addLayout(mozhiForm);
   m_mozhiEffectiveInstance = new QLabel(m_mozhiGroup);
   m_mozhiEffectiveInstance->setObjectName(QStringLiteral("mozhiEffectiveInstance"));
   m_mozhiEffectiveInstance->setWordWrap(true);
   m_mozhiEffectiveInstance->setTextFormat(Qt::PlainText);
   mozhiLayout->addWidget(m_mozhiEffectiveInstance);
   m_mozhiInstanceWarning = new QLabel(
       tr("Enter a base http(s) instance URL without credentials, a query or a fragment. "
          "The default instance will be used."),
       m_mozhiGroup);
   m_mozhiInstanceWarning->setObjectName(QStringLiteral("mozhiInstanceWarning"));
   m_mozhiInstanceWarning->setWordWrap(true);
   m_mozhiInstanceWarning->setVisible(false);
   mozhiLayout->addWidget(m_mozhiInstanceWarning);
   m_mozhiHint = new QLabel(
       tr("Mozhi is a proxy to public translation engines. Instances are unofficial: they may "
          "stop working or rate-limit requests, and some engines may be disabled on an instance. "
          "A self-hosted instance's terms and stability are your responsibility. Enable free web "
          "services above to use Mozhi. Long text is sent in several requests. The text is sent "
          "to the selected instance and engine."),
       m_mozhiGroup);
   m_mozhiHint->setObjectName(QStringLiteral("mozhiHint"));
   m_mozhiHint->setWordWrap(true);
   mozhiLayout->addWidget(m_mozhiHint);
   servicesLayout->addWidget(m_mozhiGroup);
   connect(m_allowFreeServices, &QCheckBox::toggled, this,
           &SettingsDialog::updateMozhiControls);
   connect(m_mozhiInstance, &QLineEdit::textChanged, this,
           &SettingsDialog::updateMozhiControls);
   ```

   The engine display names are product names and use `QStringLiteral`, not `tr()`; the item data
   is the id. The order comes from `mozhiEngineIds()`, so the combo cannot drift from the settings
   helper. The placeholder uses `MozhiSettings{}.instance`, so the default URL literal stays only in
   `MozhiSettings`; the effective label shows user input, so give it
   `m_mozhiEffectiveInstance->setTextFormat(Qt::PlainText)` and do not interpret the typed URL as
   rich text. The group has exactly one `QLineEdit` and no key, password or other secret field. Do
   not add a `[mozhi]` field, a settings-store change or a service change.

2. **Implement the gate and effective-value behaviour.** Add `updateMozhiControls()` near
   `updateMyMemoryControls()` (`:582`):

   ```cpp
   void SettingsDialog::updateMozhiControls()
   {
       m_mozhiGroup->setEnabled(m_allowFreeServices->isChecked());
       const QString text = m_mozhiInstance->text();
       m_mozhiEffectiveInstance->setText(
           tr("Effective instance: %1").arg(normalizedMozhiInstance(text)));
       m_mozhiInstanceWarning->setVisible(!isValidMozhiInstance(text));
   }
   ```

   Use the pure helpers from `core/settings.h` (`isValidMozhiInstance()`,
   `normalizedMozhiInstance()`, `normalizedMozhiEngine()`, `mozhiEngineIds()`); do not copy the URL
   rules or the default `https://mozhi.adminforge.de` literal into the dialog. In `loadValues()`,
   after `m_allowFreeServices->setChecked(...)` (`:450`), load the Mozhi values without a transient
   warning:

   ```cpp
   {
       const QSignalBlocker blocker(m_mozhiInstance);
       m_mozhiInstance->setText(settings.mozhi.instance);
   }
   const int mozhiEngine =
       m_mozhiEngine->findData(normalizedMozhiEngine(settings.mozhi.engine));
   m_mozhiEngine->setCurrentIndex(mozhiEngine >= 0 ? mozhiEngine : 0);
   ```

   Call `updateMozhiControls()` at the end of `loadValues()` (after `updateMyMemoryControls()` at
   `:485`), so `AppSettings{}` starts with the group disabled and the default instance shown. In
   `settings()`, after the DeepL assignment (`:511`) and before the LLM assignments, add:

   ```cpp
   result.mozhi.instance = normalizedMozhiInstance(m_mozhiInstance->text());
   result.mozhi.engine = normalizedMozhiEngine(m_mozhiEngine->currentData().toString());
   ```

   `validate()` is unchanged: an invalid instance is replaced by the default in `settings()` and is
   not a new failure condition. Do not touch `SettingsStore`, `MozhiService`, the parser or
   `AppController`.

3. **Add the main `mozhiGroup` case to `tests/tst_settingsdialog.cpp`.** The includes already have
   `QCheckBox`, `QComboBox`, `QGroupBox`, `QLabel`, `QLineEdit` and `settings.h`, so no new include
   is needed. Add a `mozhiGroup` case after `myMemoryGroup`:

   - Find `mozhiGroup`, `mozhiHint`, `mozhiInstance`, `mozhiEngine`, `mozhiEffectiveInstance` and
     `mozhiInstanceWarning`; `QVERIFY` all of them.
   - `QCOMPARE(group->title(), QStringLiteral("Mozhi"))`.
   - With `AppSettings{}` the gate checkbox is off, `QVERIFY(!group->isEnabled())`; the instance
     text is `https://mozhi.adminforge.de`; `QCOMPARE(engine->currentData().toString(),
     QStringLiteral("google"))`; the effective label is exactly
     `Effective instance: https://mozhi.adminforge.de`; `QVERIFY(warning->isHidden())`.
   - `hint->wordWrap()`, `warning->wordWrap()`, `effective->wordWrap()`; the hint contains
     `unofficial`, `rate-limit`, `self-hosted`, `several requests` and `selected instance`; the
     warning contains `http(s)` and `default instance`.
   - `group->findChildren<QLineEdit *>()` has exactly one element and it is `mozhiInstance`; there is
     no key or password field.
   - The combo has seven entries. Compare `engine->itemData(i).toString()` with
     `mozhiEngineIds().at(i)` and `engine->itemText(i)` with
     `Google`, `DeepL`, `DuckDuckGo`, `LibreTranslate`, `MyMemory`, `Reverso`, `Yandex`.
   - Check `allowFreeServices` enables the group; unchecking disables it again.
   - For each of `ftp://host`, an empty string and `https://host/?engine=google`:
     `instance->setText(...)`, `QVERIFY(!warning->isHidden())`, the effective label shows the
     default instance, `dialog.settings().mozhi.instance` is the default, and `dialog.validate()`
     still succeeds (normalization, not a new validation error). Use `isHidden()`, not
     `isVisible()`: the dialog is not shown in this test, so `isVisible()` is always false.
   - `instance->setText(QStringLiteral("http://127.0.0.1:8080/mozhi/"))`: the warning is hidden, the
     effective label is exactly
     `Effective instance: http://127.0.0.1:8080/mozhi`, and
     `dialog.settings().mozhi.instance` is `http://127.0.0.1:8080/mozhi` (no trailing slash).
   - Select `deepl` in the combo and assert `dialog.settings().mozhi.engine` is `deepl`; the default
     is still `google` before the change.
   - Run the target offscreen:
     `QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux-release -R tst_settingsdialog
     --output-on-failure` (and the Debug tree).

4. **Add the custom-values case to `tests/tst_settingsdialog.cpp`.** Add a second case (for example
   `mozhiGroupShowsCustomSettings`) with a hand-built `AppSettings`:
   `translation.allowFreeServices = true`, `mozhi.instance =
   "https://mozhi.example.org/proxy"`, `mozhi.engine = "libretranslate"`. Verify that the group is
   enabled, `mozhiInstance->text()` is the custom URL, the combo current data is
   `libretranslate` and its text is `LibreTranslate`, the warning is hidden, and the effective label
   is `Effective instance: https://mozhi.example.org/proxy`. Then set the field to the same URL with
   a trailing slash and assert that the warning stays hidden, the effective label loses the slash and
   `dialog.settings().mozhi.instance` is canonical. The dialog only formats the values; this case
   must not make a network request. With the two new cases `tst_settingsdialog` has 24 passed
   functions (20 existing plus `initTestCase`/`cleanupTestCase` plus the two new cases); record the
   actual count.

5. **Update all 14 `.ts` files.** Run the CMake target
   `cmake --build build/linux-release --target update_translations` (repeat in `build/linux-debug`
   only if its translations are rebuilt); it regenerates the lupdate project from the current CMake,
   includes `src/platform/hotkeymanager_win.cpp` through
   `QTRANSLATE_LUPDATE_EXTRA_SOURCES`, and rewrites all files in `QTRANSLATE_TS_FILES`. If the
   result contains obsolete entries, rerun
   `lupdate -no-obsolete -project build/linux-release/.lupdate/qtranslate_lupdate_project.json`;
   no message is removed, so there must be 0 vanished. The six new `SettingsDialog` source strings
   are exactly:

   ```
   Mozhi
   Instance URL:
   Engine:
   Effective instance: %1
   Enter a base http(s) instance URL without credentials, a query or a fragment. The default instance will be used.
   Mozhi is a proxy to public translation engines. Instances are unofficial: they may stop working or rate-limit requests, and some engines may be disabled on an instance. A self-hosted instance's terms and stability are your responsibility. Enable free web services above to use Mozhi. Long text is sent in several requests. The text is sent to the selected instance and engine.
   ```

   Translate all six in every `i18n/qtranslate_*.ts` (ru_RU, es, de, fr, pt_BR, it, pl, uk, tr,
   zh_CN, ja, ko, ar, hi). Keep `%1` in the effective-instance message exactly once; keep the
   product names `Mozhi`, `Google`, `DeepL`, `DuckDuckGo`, `LibreTranslate`, `MyMemory`, `Reverso`
   and `Yandex` untranslated. Do not add the engine display names to `.ts`: they are
   `QStringLiteral` product names. Count `<message` occurrences in one file: the baseline is 301,
   so the expected result is 307; record the actual number. Confirm the Windows-only hotkey message
   (`i18n/qtranslate_ru_RU.ts:132`, `src/platform/hotkeymanager_win.cpp`) is still present.
   `tst_translations` must find no new, unfinished, empty or vanished messages; run
   `xvfb-run -a ctest --test-dir build/linux-release -R tst_translations --output-on-failure`.

6. **Update `doc/architecture.md` §3.1 and §3.2.** In §3.1:
   - Add `mozhiservice.h/.cpp` (free Mozhi API, no key, opt-in) and `mozhiparser.h/.cpp` (pure
     parsing) to the `src/services/` block after `mymemoryparser.h/.cpp` (`:213-214`).
   - Change the fixtures line (`:246`) to
     `saved Google/DeepL/MyMemory/Mozhi responses (google/, deepl/, mymemory/, mozhi/)`.
   - Add `tst_mozhiparser.cpp` and `tst_mozhiwebservice.cpp` to the `tests/` tree before
     `tst_mymemoryparser.cpp` (`:262`), with the service line marked `service against a local HTTP
     server` like the other service tests.
   In §3.2 (`:288-290`):
   - Add `mozhiparser` and `mozhiservice` to the `qtranslate_core` list before `mymemoryparser`.
   - Add `tst_mozhiparser` and `tst_mozhiwebservice` to the tests list before `tst_mymemoryparser`.
   - Change `26 tests on Windows and 31 on Linux` to `28 tests on Windows and 33 on Linux`.

7. **Update `doc/architecture.md` §4.5 and add §4.7.4.** In §4.5, change the `id()` example
   (`:640`) to include `"mozhi"`, and the queue paragraph (`:688-689`) to say that
   `DeepLWebService`, `MyMemoryService` and `MozhiService` use `QueuedWebService`, with MyMemory at
   350 ms and Mozhi at 500 ms. Keep the 2000 ms default and the rest of the paragraph.

   Insert a new `### 4.7.4.` heading `MozhiService — free multi-engine proxy` between §4.7.3 and
   §4.8 (`:938`/`:940`). It must document:
   - The service id `mozhi`, display name `Mozhi`, no key (`isConfigured() == true`), the public or
     self-hosted instance and `requiresOptIn() == true`; it is the second real opt-in consumer after
     MyMemory.
   - The editable settings: `[mozhi] instance` (default `https://mozhi.adminforge.de`) and
     `[mozhi] engine` (default `google`), with the seven ids `google`, `deepl`, `duckduckgo`,
     `libretranslate`, `mymemory`, `reverso`, `yandex`; no hidden instance rotation and no key
     field, so the text goes only to the instance the user configured.
   - The request:
     `GET <instance>/api/translate?engine=<engine>&from=<src>&to=<dst>&text=<text>` with
     `Accept: application/json`; the instance is a base URL, a path prefix is kept
     (`https://host/mozhi` → `https://host/mozhi/api/translate`), and settings reject credentials,
     a query and a fragment.
   - The language mapping: the Google column (`zh-Hans`→`zh-CN`, `zh-Hant`→`zh-TW`, `he`→`iw`,
     `jv`→`jw`, `nb`→`no`, `fil`→`tl`, `pt-BR`→`pt`), `mozhiCodeOf()` as the single fix point.
     `auto` is sent as `auto` (the default `google` engine accepts it); a pair the instance rejects
     becomes `UnsupportedLanguage`, never a guessed language. Note that an engine can pass an
     upstream error through `translated-text` with HTTP 200 (observed with `engine=mymemory` and
     `auto`), so `auto` depends on the engine and the default `google` is the supported choice.
   - Chunking: 5000 UTF-16 code units via `TextChunker::split`, sequential chunks inside one
     `QueuedWebService` slot, a 500 ms pause, edge whitespace restored, whitespace-only parts not
     sent, a failed chunk failing the whole reply without a partial result. State that 5000 units
     and 500 ms are conservative assumptions, not measured limits; HTTP 413/414 (`TooLong`) is the
     signal that the percent-encoded URL got too long.
   - `SameOriginRedirectPolicy` on every request: a cross-origin redirect is refused, so the text
     cannot silently move to another host.
   - Response fields: `translated-text` (required), `detected`, `source_language`,
     `target_transliteration`; `detected` fills `detectedSourceLang` through
     `Languages::fromServiceCode()`, falling back to a non-`auto` `source_language`.
   - The error table:

     | Condition | `TranslationError` |
     |---|---|
     | HTTP 403/429, a rate-limit body, or `status code: 403`/`429` | `RateLimited` |
     | HTTP 413/414 | `TooLong` |
     | HTTP 5xx without a recognised body | `Network` |
     | an HTML page, a disabled engine, or a JSON `error` | `ServiceUnavailable` |
     | an invalid-language body | `UnsupportedLanguage` |
     | malformed JSON, a missing/empty `translated-text`, an unknown non-2xx status | `BadResponse` |
     | timeout / `abort()` | `Timeout` / `Cancelled` |

     The body is classified before the HTTP status (Mozhi reports rate limits and disabled engines
     as plain text even with HTTP 200 or 500). Messages are fixed literals and never contain the
     text, the body or the full URL; network errors use
     `NetworkManager::describeNetworkError(request, false)`, never `QNetworkReply::errorString()`.
   - `applySettings()` reads the normalized `[mozhi]` values; `AppController` clears the translation
     cache when the effective instance or engine changes and does not abort an in-flight reply.
   - Tests: `tst_mozhiparser` and `tst_mozhiwebservice` (local `QTcpServer`), the `mozhi` branch in
     `tst_languages`, and the settings/UI cases in `tst_settings`/`tst_settingsdialog`; CTest never
     contacts a real instance, and a live smoke is manual only.

8. **Update `doc/architecture.md` §4.8, §4.9 and §4.11.** In §4.8, after the MyMemory paragraph
   (`:956-959`), add: Mozhi reuses the Google column — `zh-Hans`→`zh-CN`, `zh-Hant`→`zh-TW`,
   `he`→`iw`, `jv`→`jw`, `nb`→`no`, `fil`→`tl`, `pt-BR`→`pt`; `mozhiCodeOf()` returns
   `info.google` and is the single fix point if an engine later needs a different code; no
   `LanguageInfo` column is added and the table's columns are unchanged.

   In §4.9, after the MyMemory paragraph (`:1039-1042`), add: Mozhi is the second real service with
   `requiresOptIn() == true` (§4.7.4); the gate logic is unchanged; `AppController` hides it from
   the pickers while the gate is off and, if `translation/service=mozhi` is configured with the gate
   off, keeps Google active and logs that the configured service needs free web services to be
   enabled.

   In §4.11, add `mozhi` to the `AppSettings` group list (`:1124`) after `deepl`. Add two rows to
   the file-key table after `deepl/englishVariant` (`:1192`):

   ```
   | `mozhi/instance` | string | `https://mozhi.adminforge.de`; a base `http`/`https` URL without credentials, a query or a fragment; trailing slashes are removed |
   | `mozhi/engine` | `google` \| `deepl` \| `duckduckgo` \| `libretranslate` \| `mymemory` \| `reverso` \| `yandex` | `google` |
   ```

   After the `settingsVersion` paragraph (`:1205`), state that the `[mozhi]` group was added
   without a version bump: `settingsVersion` stays `1`, an old file without the group loads the
   defaults and is not rewritten, and an invalid instance/engine is replaced by the default with a
   fixed warning that does not echo the typed value.

9. **Update `doc/architecture.md` §8 and §4.14.** In §8, add two rows after
   `tst_mymemoryservice` (`:1658`):

   ```
   | `tst_mozhiparser` | Parsing Mozhi fixtures: `translated-text`, `detected`, `source_language`, `target_transliteration`, `auto`, HTML/JSON/plain-text errors, the HTTP status mapping and fixed messages without the body or URL |
   | `tst_mozhiwebservice` | Mozhi against a local HTTP server: the `GET /api/translate` query, an instance path prefix, `engine`/`from`/`to`/`text`, Google-style codes, `auto`, 5000-unit chunks, 500 ms pacing, serialization, cancellation, HTTP/HTML/JSON/timeout errors, a refused cross-origin redirect, `applySettings()` and the non-abort behaviour |
   ```

   Update the `tst_languages` row (`:1662`) to include the Mozhi codes (`zh-CN`, `zh-TW`, `iw`,
   `jw`, `no`, `tl`, `pt`), the `tst_settings` row (`:1675`) to include the `[mozhi]` defaults,
   normalization and canonical round trip, and the `tst_settingsdialog` row (`:1676`) to include the
   Mozhi group (instance, engine, effective value, warning and gate). In §4.14, change
   `31 tests (23 shared plus 8 Linux platform tests)` (`:1394`) to
   `33 tests (25 shared plus 8 Linux platform tests)`, and `the other 23 tests are shared`
   (`:1436`) to `the other 25 tests are shared`. Leave the historical 0.12.0 sentence at `:1840`
   and the migration sentences unchanged.

10. **Update the remaining status documents.** In `doc/architecture.md` §9.2 item 8
    (`:1864-1867`), replace the sentence that groups Mozhi with the unimplemented endpoints with one
    that names Mozhi as the second implemented opt-in free service (§4.7.4) and leaves only Yandex
    web, Bing web and Reverso as experimental; keep the rate-limit/unofficial-warning sentence.

    In `doc/free-endpoints.md`:
    - Change the top status line (`:3-5`) to date 2026-11-09 and say that DeepL web, MyMemory and
      Mozhi are implemented (MyMemory and Mozhi as gated no-key services) and the other endpoints
      are research.
    - In §5 (`:86-94`), after the existing bullets, add an "Implementation in qtranslate
      (2026-11-09)" note with the request path and headers, the base-instance rule, the editable
      instance/default `https://mozhi.adminforge.de` and engine/default `google` with the seven ids,
      no instance rotation, the Google-style language codes, `auto`, 5000 UTF-16 units with the
      500 ms assumption, the response fields, the error mapping, the same-origin redirect policy,
      the opt-in gate and the tests. Keep the existing Engines/Instances/Pro/Con bullets.
    - In §8 item 2 (`:145-146`), say MyMemory and Mozhi are implemented as the first two gated
      services; in item 3 (`:147-149`) add Mozhi 500 ms; in item 4 (`:150-151`) add Mozhi 5000
      UTF-16 code units. Do not change the fallback item 6.
    - In §9 item 3 (`:163`), mark Mozhi as implemented, keeping "one integration, many engines,
      self-hosting possible".
    - Do not touch `README.md`, `README.ru.md`, `doc/handoff.md`, the root `plan.md` or the
      `07-fallback-chain` plans.

    In `AGENTS.md:39`, change "26 Windows tests and all 31 Linux tests" to "28 Windows tests and
    all 33 Linux tests".

11. **Build, test and record.** With the trees configured per `AGENTS.md`
    (`build/linux-debug`, `build/linux-release`, `-Wall -Wextra`), run the targeted tests first:
    `tst_settingsdialog` under `QT_QPA_PLATFORM=offscreen`, `tst_translations`, `tst_settings`,
    `tst_mozhiparser`, `tst_mozhiwebservice`, `tst_translationmanager` and `tst_languages`. Run
    `cmake --build` for both configurations and capture the output; there must be zero `warning:`
    lines. `ctest -N` must show 33 tests in each tree. Run the full suite in both:
    `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure` and the Debug tree;
    expect 33/33. Check the Services page offscreen through the new `tst_settingsdialog` cases
    (defaults, effective value, seven engine ids/names, warning, gate toggle). Confirm the picker
    gate in `AppController::updateServiceSelectors()` (`:494-496`, the
    `requiresOptIn() && !freeServicesAllowed` filter), by `tst_mozhiwebservice::properties`
    (`requiresOptIn() == true`) and by the generic
    `tst_translationmanager::gatedRealServiceIsRefused`. Optionally run the app offscreen with a
    temporary `XDG_CONFIG_HOME` whose `qtranslate/qtranslate.ini` has
    `[translation] allowFreeServices=false` and `service=mozhi`, and check that Google stays active
    and the log contains "Configured service needs free web services to be enabled"; record this as
    passed or not performed. The optional live smoke (default instance, one self-hosted or
    alternate public instance, a short `en→ru`, a long multi-chunk text and a disabled engine) is
    recorded as passed or as not performed with the reason. Windows Debug/Release and its 28 tests
    are recorded as not performed (no MSVC). Check `git diff --stat`: only
    `src/ui/settingsdialog.h/.cpp`, `tests/tst_settingsdialog.cpp`, the 14 `i18n/qtranslate_*.ts`,
    `doc/architecture.md`, `doc/free-endpoints.md` and `AGENTS.md` may change. Do not commit.

## Done when

- The Services page has a `Mozhi` group after `Free web services` and `MyMemory` with
  `mozhiInstance` (the only `QLineEdit` in the group, prefilled from `settings.mozhi.instance` and
  placeholder `https://mozhi.adminforge.de`), `mozhiEngine` (seven entries whose data are
  `mozhiEngineIds()` in order and whose display names are Google, DeepL, DuckDuckGo,
  LibreTranslate, MyMemory, Reverso, Yandex), `mozhiEffectiveInstance`,
  `mozhiInstanceWarning` and `mozhiHint`; there is no key or password field. The group is visible but
  disabled while `translation/allowFreeServices` is off, follows the gate checkbox, and the
  default-off dialog starts disabled.
- The effective label shows `Effective instance: <normalizedMozhiInstance>`; the warning is shown
  only for an invalid instance (empty, `ftp://host`, a URL with a query) and the effective label
  then shows the default; a valid trailing-slash URL is silently canonicalized and does not warn.
  `settings()` returns the normalized instance and the selected engine; `validate()` has no new
  failure condition.
- `tst_settingsdialog` covers the default state, both gate directions, the seven engine data ids and
  display names, the effective value, the three invalid inputs, the trailing-slash canonicalization,
  the `settings().mozhi` round trip, `validate()` still passing, and a custom
  `AppSettings` instance/engine shown correctly.
- All 14 `i18n/qtranslate_*.ts` contain the six new `SettingsDialog` messages translated, with
  `%1` preserved; the message count goes from 301 to 307 (the actual number is recorded in the
  outcome); the Windows-only hotkey message is preserved; `tst_translations` finds no new,
  unfinished, empty or vanished messages and passes.
- `doc/architecture.md` §3.1/§3.2 list the Mozhi sources, fixtures and
  `tst_mozhiparser`/`tst_mozhiwebservice`, and say 28 Windows/33 Linux; §4.5 has `mozhi` in the
  `id()` example and the queue users with 350/500 ms; the new §4.7.4 documents the instance/engine,
  the GET query, `auto`/`UnsupportedLanguage`, 5000 UTF-16 units, the 500 ms assumption, the
  same-origin policy, the response fields, the error table and the tests; §4.8 documents the Google
  codes and `mozhiCodeOf()` without a new column; §4.9 names the second opt-in consumer; §4.11 has
  the group and both keys with the defaults, normalization and `settingsVersion == 1`; §8 has the
  two new rows and the updated `tst_languages`/`tst_settings`/`tst_settingsdialog` descriptions;
  §4.14 says 33 tests, 25 shared and "the other 25 tests"; §9.2 item 8 names Mozhi as implemented.
  The historical 0.12.0 sentence is unchanged.
- `doc/free-endpoints.md` has the updated status line/date, the §5 implementation note, Mozhi marked
  implemented in §8 items 2-4 and §9 item 3, and the instance/engine/privacy rules are kept;
  `AGENTS.md` says 28/33.
- `README.md`, `README.ru.md`, `doc/handoff.md`, the root `plan.md` and `07-fallback-chain` are not
  edited; no service, parser, `MozhiSettings`, `SettingsStore`, `AppController` or translation
  behaviour is changed; no new test target is added, so `ctest -N` stays 33 Linux/28 Windows.
- Debug and Release build on Linux without `warning:` lines and pass 33/33; the settings group and
  the picker gate are recorded as passed or as not performed with the reason; the optional live
  smoke is recorded as passed or as not performed (a public instance may be down or rate-limited);
  Windows Debug/Release and its 28 tests are recorded as not performed (no MSVC).
- Only `src/ui/settingsdialog.h/.cpp`, `tests/tst_settingsdialog.cpp`, the 14
  `i18n/qtranslate_*.ts`, `doc/architecture.md`, `doc/free-endpoints.md` and `AGENTS.md` are
  changed; no commit is made.

## Risks

- The new `SettingsDialog` strings must be translated in all 14 `.ts`; `lupdate` on Linux can mark
  the Windows-only hotkey message vanished unless the scan includes
  `src/platform/hotkeymanager_win.cpp`. Keep that source in the scan and run `tst_translations`.
- The effective instance and the warning must use `isValidMozhiInstance()`/
  `normalizedMozhiInstance()` from `settings.h`, not a second copy of the URL rules in the dialog.
  A duplicated rule would let the UI show one value while the service uses another, and the
  `https://mozhi.adminforge.de` literal would exist in two places.
- The dialog returns the normalized values; if it returned the raw text, an invalid URL would only
  be repaired in `SettingsStore::save()`, and `dialog.settings()` would not match what the service
  gets. Keep the round-trip test.
- The group must stay readable while disabled and must not look like an enabled service when the
  gate is off. Keep `updateMozhiControls()` at the end of `loadValues()` and on the gate toggle.
  `updateMozhiControls()` is also connected to `textChanged`, so the effective label and warning
  follow the field without a separate signal path.
- `tst_settingsdialog` does not show the dialog. Use `isHidden()` for the warning, not
  `isVisible()`; `isVisible()` is false for every child of a hidden dialog and would make the test
  pass for the wrong reason.
- `lupdate` rewrites the `<location>` lines in all 14 `.ts` because the dialog gains lines. This
  churn is expected; do not revert it and do not hand-edit locations. Only the six messages and
  their translations are substantive.
- The engine display names are product names and must stay outside `tr()`; wrapping them would add
  seven messages and make the count and the “not translated” rule wrong. The order must come from
  `mozhiEngineIds()`, not a second hard-coded list.
- The docs must stay consistent with the code: 500 ms, 5000 UTF-16 units, `auto`, the Google-style
  codes, the engine ids, the error table, the same-origin redirect policy and 33/25/28. The current
  tree is still at 31 tests/23 shared/26 Windows and 301 messages; the settings task did not update
  the counts. A stale count or a claimed live smoke is worse than a not-performed note.
- The §9.2 item 8 sentence is outside the enumerated §3.1/§3.2/§4.5/§4.7.4/§4.8/§4.9/§4.11/§8/§4.14
  list; it is included only because it would contradict the new §4.7.4. If the lead prefers the
  strict section list, drop that one sentence edit rather than leaving the document inconsistent.
- The optional live smoke may fail because a public instance is down or rate-limited; record it as
  not performed rather than claiming success. Windows Debug/Release and its 28 tests are unavailable
  on this Linux machine; record them as not performed, not as passed.
- Do not add the README/handoff/root/`07` edits here; duplicating the provider/fallback tables would
  create two sources of truth, and the final pass belongs to `07-fallback-chain`/the lead.

## Outcome

Done on 2026-11-09. The Services page has a `Mozhi` group (`mozhiGroup`) after `Free web services`
and `MyMemory`, so the gate checkbox stays above both groups. The group contains the only text
field `mozhiInstance` (prefilled from `settings.mozhi.instance`, placeholder `MozhiSettings{}.instance`),
the `mozhiEngine` combo built in `mozhiEngineIds()` order with the seven product names as
`QStringLiteral` display names and the ids as item data, the `Qt::PlainText` word-wrapped
`mozhiEffectiveInstance`, the word-wrapped `mozhiInstanceWarning` and the word-wrapped `mozhiHint`.
There is no key, password or other secret field. `updateMozhiControls()` follows the gate checkbox
and the field text, `loadValues()` ends with it so the default-off dialog starts disabled, and
`settings()` returns the normalized instance and engine. `validate()` has no new failure condition;
an invalid instance is replaced by the default in `settings()`. No service, parser,
`MozhiSettings`, `SettingsStore`, `AppController` or CMake file was changed.

`tst_settingsdialog` gained `mozhiGroup` and `mozhiGroupShowsCustomSettings` and reports 24 passed
functions. They cover the default gate-off state, both gate directions, the seven engine data ids
and display names, `isHidden()` for the warning, the three invalid inputs (`ftp://host`, an empty
field, a query) with the default effective value and `settings().mozhi`, `validate()` still passing,
the trailing-slash canonicalization, the `deepl` selection and a custom `AppSettings` with
`https://mozhi.example.org/proxy` and `libretranslate`. The first case passes `{de, en, ru}` as the
target languages instead of the two-language set used by older cases because the default
`AppSettings{}` primary/secondary languages collapse into one entry with `{de, en}` and `validate()`
then fails for a reason unrelated to Mozhi; the Mozhi checks are not weakened by that.

All 14 `i18n/qtranslate_*.ts` contain the six new `SettingsDialog` messages translated, with `%1`
exactly once in the effective-instance message and the product names untranslated. The message count
is 307 in every file (baseline 301); `lupdate` reported 0 new and 307 existing and no
unfinished/obsolete/vanished messages. The Windows-only hotkey message from
`src/platform/hotkeymanager_win.cpp` is still present in `qtranslate_ru_RU.ts` (now at line 133
because of the new messages). The `<location>` churn in all 14 files is expected from `lupdate` and
was not reverted.

`doc/architecture.md` now lists `mozhiservice`/`mozhiparser`, the `mozhi/` fixtures and the two new
tests in §3.1 and §3.2 with the 28 Windows/33 Linux counts; §4.5 has `mozhi` in the `id()` example
and the three queue users with 350/500 ms; the new §4.7.4 documents the instance/engine settings, the
GET query, the Google-style codes and `mozhiCodeOf()`, `auto`/`UnsupportedLanguage`, 5000 UTF-16
units via `TextChunker::split`, the 500 ms assumption, the same-origin redirect policy, the response
fields, the error table, `applySettings()` and the tests; §4.8 documents the Google column without a
new `LanguageInfo` column; §4.9 names Mozhi as the second opt-in consumer; §4.11 has the `mozhi`
group, both file keys and the `settingsVersion == 1` statement; §8 has the two new rows and the
updated `tst_languages`/`tst_settings`/`tst_settingsdialog` descriptions; §4.14 says 33 tests, 25
shared and "the other 25 tests"; §9.2 item 8 names Mozhi as the second implemented opt-in free
service. `doc/free-endpoints.md` has the 2026-11-09 status line, the §5 implementation note, Mozhi
implemented in §8 items 2-4 and §9 item 3; the instance/engine/privacy rules are kept. `AGENTS.md`
says 28 Windows/33 Linux. `README.md`, `README.ru.md`, `doc/handoff.md`, the root `plan.md` and the
`07-fallback-chain` plans are not edited, and no commit was made by the programmer.

Verification on Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2), Debug and Release: both build with zero
`warning:` lines, `ctest -N` shows 33 in each tree, the full suite is 33/33 in both, and
`tst_settingsdialog` is 24/24 with `tst_translations` passing. The picker gate was confirmed by
`AppController::updateServiceSelectors()` (`requiresOptIn() && !freeServicesAllowed`), by
`tst_mozhiwebservice::properties` (`requiresOptIn() == true`) and by
`tst_translationmanager::gatedRealServiceIsRefused`. An offscreen run with a temporary
`XDG_CONFIG_HOME`, `service=mozhi` and `allowFreeServices=false` kept Google active and logged
`Configured service needs free web services to be enabled: "mozhi"`; the picker itself was checked
by code and the log, not by UI introspection. The optional live smoke was not performed because a
public instance may be down or rate-limited. Windows Debug/Release and its 28 tests were not
performed (no MSVC). Trixie was not rerun for this UI/docs-only task.

Review found no issues. One note for the final pass: `doc/handoff.md:184` still lists Mozhi as
research; that file is outside this task and belongs to `07-fallback-chain`/the lead.
