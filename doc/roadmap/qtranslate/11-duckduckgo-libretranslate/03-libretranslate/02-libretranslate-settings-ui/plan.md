---
kind: task
status: done
detailed: true
depends: [01-libretranslate-service]
---

# LibreTranslate base URL setting, settings group and translations

## Task

Store the LibreTranslate base URL and the optional API key, connect them to the service, expose them
on the Services page, translate the new strings and cover the settings and the dialog with tests. No
parser, protocol or error-mapping change: those are `01-libretranslate-service` (done, `49a37a0`).

Settings: `LibreTranslateSettings` in `src/core/settings.h` with `baseUrl` (empty by default; research
§1.5 found no public key-less instance) and `apiKey` (empty by default, optional); `SettingsStore`
loads and saves `[libretranslate] baseUrl` and normalizes it with the shared base-URL helpers, and
stores `apiKey` as a secret through `SecretProtector` (`readSecret`/`writeSecret`, as the DeepL, Google
and LLM keys do). An empty base URL is valid and means "not configured": the service then reports
`configurationError()`. An empty key is valid and omits `api_key`. The settings format version stays 1,
because the new keys have defaults and an old file without the group loads with them; the file is not
rewritten only because the keys are absent.

Shared helpers: extract `isValidServiceBaseUrl()`/`normalizedServiceBaseUrl()` in
`src/core/settings.cpp` (declared in `src/core/settings.h`) from the Mozhi rules — absolute `http(s)`,
a host, no credentials, no query, no fragment, a path prefix kept, a trailing slash removed — and keep
`isValidMozhiInstance()`/`normalizedMozhiInstance()` as thin wrappers with unchanged behaviour, so
`tst_settings` and `03-free-web-endpoints/03-mozhi` stay valid. The helpers do not log;
`SettingsStore::normalize()` logs a fixed warning that never echoes the typed value.

Wiring: `LibreTranslateService::applySettings()` re-normalizes like `MozhiService::applySettings()`
(`src/services/mozhiservice.cpp:360-364`), reads both values and calls `setBaseUrl()`/`setApiKey()`;
`AppController` clears the translation cache when the base URL changes and does not abort an active
reply, like the Mozhi instance change. A key change does not abort an active reply and does not clear
the cache: `TranslationManager` caches only successful results
(`src/core/translationmanager.cpp:238-240`), and they came from the same server. The provider stays at
the end of the provider order.

UI: a `LibreTranslate` `QGroupBox` with objectName `libretranslateGroup` appended at the end of the
Services page, after the `Reverso` and `DuckDuckGo` groups, so the page follows the provider order. It
has a `QFormLayout` with an editable `QLineEdit` with objectName `libretranslateBaseUrl` prefilled with
the setting and the **example** `http://localhost:5000` as placeholder — the default is empty, so the
placeholder is an example, not a value; the labels `Server URL:` and `API key:` are the existing
`SettingsDialog` strings (`src/ui/settingsdialog.cpp:338-339`), so translators see no near duplicate. A
`QLineEdit` with objectName `libretranslateApiKey` in password echo is prefilled with the stored key and
has the placeholder `Optional; only a server that requires a key`. A word-wrapped `QLabel` with
objectName `libretranslateEffectiveUrl` shows `Effective base URL: <normalized>` and is hidden while the
field is empty ("not configured"), a word-wrapped `QLabel` with objectName
`libretranslateUrlWarning` is visible only for a **non-empty invalid** value, and a word-wrapped
`QLabel` with objectName `libretranslateHint` says the provider talks to a LibreTranslate server,
usually self-hosted, that the base URL must point at a server the user trusts, that its terms,
availability, rate and text limits and installed languages are the user's responsibility (a language
without a model is rejected), that the key is optional and only for a server that requires one, that
quality is lower than Google/DeepL, and that long text is sent in several requests. The hint carries no
"the text is sent to a third party" sentence (user decision 2026-11-15). `settings()` returns the
normalized value; `validate()` adds no new failure because normalization handles invalid input.

New source strings (six, see step 9): `LibreTranslate`, `Optional; only a server that requires a key`,
`Effective base URL: %1`, the warning, the hint (all in the `SettingsDialog` context) and the
`configurationError()` message of `01-libretranslate-service` in a new `LibreTranslateService` context.

Tests and translations: `tst_settings` covers the empty default of both values, the base-URL round trip
and normalization rows of the shared helpers, the key round trip through the secret protector and the
unchanged Mozhi rows; `tst_settingsdialog` covers the group, both fields, the effective label, the
warning and the hint; `tst_libretranslateservice` gains one `applySettings()` case, the only automated
proof that the settings reach `setBaseUrl()`/`setApiKey()`. The new strings go through the
`update_translations` target and are translated by hand in all 14 `i18n/qtranslate_*.ts` files;
`lrelease` reports no unfinished or vanished message for them. `AGENTS.md`, `doc/` and both READMEs are
the lead's pass.

If the shared helper extraction, the dialog and the 14 `.ts` files grow beyond a task, report
`НЕ УКЛАДЫВАЕТСЯ` instead of splitting silently: settings and UI could then become two nodes.

## Steps

1. **Add the settings struct and the shared helper declarations** in `src/core/settings.h`.

   - `LibreTranslateSettings` after `MozhiSettings` (`:54-58`):

     ```cpp
     struct LibreTranslateSettings
     {
         // Empty by default: research §1.5 found no key-less public instance, and a
         // default would send the text to a server the user did not enter.
         QString baseUrl;
         // Optional: a self-hosted server usually needs no key, a hosted instance does.
         QString apiKey;
     };
     ```

   - `LibreTranslateSettings libretranslate;` in `AppSettings` after `MozhiSettings mozhi;` (`:116`),
     before `LlmSettings llm;`. Nothing is added to `AppSettings::AppSettings()`: both members have
     in-class defaults and `kCurrentVersion` stays 1.
   - Extend the block comment at `:128-129` to name the third user ("shared by `SettingsStore`, the
     Mozhi settings UI and the LibreTranslate settings group") and add after
     `normalizedMozhiEngine()` (`:143`):

     ```cpp
     // The base-URL rules shared by the Mozhi instance field and the LibreTranslate
     // base URL: an absolute http(s) URL with a host, without user info, a query or a
     // fragment. normalizedServiceBaseUrl() trims surrounding whitespace, removes the
     // trailing slashes, keeps a path prefix and returns an empty string for an
     // invalid or empty value. They never log.
     bool isValidServiceBaseUrl(const QString &url);
     QString normalizedServiceBaseUrl(const QString &url);
     ```

2. **Extract the helpers** in `src/core/settings.cpp:335-358`, replacing the two Mozhi bodies:

   ```cpp
   bool isValidServiceBaseUrl(const QString &url)
   {
       const QString text = url.trimmed();
       if (text.isEmpty())
           return false;
       // Not fromUserInput(): a bare host or malformed text must not be guessed
       // into an http URL and silently sent somewhere the user did not confirm.
       const QUrl value(text, QUrl::StrictMode);
       return value.isValid()
           && (value.scheme() == QLatin1String("http")
               || value.scheme() == QLatin1String("https"))
           && !value.host().isEmpty() && value.userInfo().isEmpty() && !value.hasQuery()
           && !value.hasFragment();
   }

   QString normalizedServiceBaseUrl(const QString &url)
   {
       if (!isValidServiceBaseUrl(url))
           return {};
       QString text = QUrl(url.trimmed(), QUrl::StrictMode).toString(QUrl::FullyEncoded);
       while (text.endsWith(QLatin1Char('/')))
           text.chop(1);
       return text;
   }

   bool isValidMozhiInstance(const QString &instance)
   {
       return isValidServiceBaseUrl(instance);
   }

   QString normalizedMozhiInstance(const QString &instance)
   {
       const QString normalized = normalizedServiceBaseUrl(instance);
       return normalized.isEmpty() ? MozhiSettings{}.instance : normalized;
   }
   ```

   The Mozhi behaviour is bit-for-bit the old one: the old body trimmed the value and returned
   `MozhiSettings{}.instance` when `isValidMozhiInstance()` said no, and the new wrapper does exactly
   that. Every row of `tst_settings::mozhiInstanceIsNormalized` (`tests/tst_settings.cpp:162-205`) must
   stay green without an edit. The default is the *Mozhi* default only: `normalizedServiceBaseUrl()`
   returns an empty string, because LibreTranslate has no default server.

3. **Load, save and normalize `[libretranslate]`** in `src/core/settings.cpp`.

   - in `normalize()` after the Mozhi block (`:195-206`):

     ```cpp
     // The base URL is user input and may contain credentials, so the warning is
     // fixed and never echoes the typed value. An empty value is valid and means
     // "not configured": the service then reports configurationError().
     const QString libreTranslateBaseUrl = settings.libretranslate.baseUrl.trimmed();
     if (libreTranslateBaseUrl.isEmpty()) {
         settings.libretranslate.baseUrl.clear();
     } else if (!isValidServiceBaseUrl(libreTranslateBaseUrl)) {
         qCWarning(lcSettings)
             << "LibreTranslate base URL is not a base http(s) URL; the service stays unconfigured";
         settings.libretranslate.baseUrl.clear();
     } else {
         settings.libretranslate.baseUrl = normalizedServiceBaseUrl(libreTranslateBaseUrl);
     }
     ```

     The key is not trimmed here, like the other provider keys; the dialog trims it on save and the
     service trims it when it builds the body.

   - in `load()` after the `[mozhi]` group (`:517-522`) and before `[llm]` (`:524`):

     ```cpp
     settings.beginGroup(QStringLiteral("libretranslate"));
     loaded.libretranslate.baseUrl =
         settings.value(QStringLiteral("baseUrl"), loaded.libretranslate.baseUrl).toString();
     loaded.libretranslate.apiKey = readSecret(settings, QStringLiteral("apiKey"), m_codec);
     settings.endGroup();
     ```

     A file without the group keeps the empty defaults; `load()` never saves, so an old file is not
     rewritten.

   - in `save()` after the `[mozhi]` group (`:666-669`) and before `[llm]` (`:671`):

     ```cpp
     out.beginGroup(QStringLiteral("libretranslate"));
     out.setValue(QStringLiteral("baseUrl"), normalized.libretranslate.baseUrl);
     writeSecret(out, QStringLiteral("apiKey"), normalized.libretranslate.apiKey,
                 old.libretranslate.apiKey, m_codec);
     out.endGroup();
     ```

     `writeSecret()` gives the same guarantees as for the other keys: the token is protected when a
     codec is present, the INI never holds the plaintext key, and an unchanged secret is not
     re-protected.

4. **Wire the settings into the service and the cache.** Two files.

   - `src/services/libretranslateservice.cpp`: add `#include "core/settings.h"` to the include block
     (`:1-6`) — the header only forward-declares `AppSettings`
     (`src/core/translationservice.h:10`), so the file does not compile without it once the body reads
     the struct. Replace the empty `applySettings()` (`:422-427`):

     ```cpp
     void LibreTranslateService::applySettings(const AppSettings &settings)
     {
         // Re-normalized at the service boundary, as MozhiService does, so a caller
         // that skips SettingsStore::save() cannot send a request to an uncanonical
         // URL. An empty or invalid value clears the base URL and leaves the service
         // unconfigured; the key is optional and never changes isConfigured().
         setBaseUrl(QUrl(normalizedServiceBaseUrl(settings.libretranslate.baseUrl)));
         setApiKey(settings.libretranslate.apiKey);
     }
     ```

     `setBaseUrl()`/`setApiKey()` keep their documented meaning (the normalized URL and the raw key);
     `translate()` already captures the endpoint and the key when the reply is built, so no further
     change is needed for "a settings change does not abort an active reply".

   - `src/app/appcontroller.cpp`: after the Mozhi cache block (`:696-702`) add

     ```cpp
     // A cached translation from another server must not be reused. The active
     // reply keeps its captured endpoint and key, so it is not cancelled here; a
     // key change alone does not clear the cache, because the cached results came
     // from the same server.
     if (oldSettings.libretranslate.baseUrl != newSettings.libretranslate.baseUrl) {
         m_translations->clearCache();
     }
     ```

     `m_libreTranslate->applySettings(newSettings)` already exists (`:668-669`). No
     `updateServiceSelectors()` call: the selectors list every registered service regardless of
     `isConfigured()` (`src/app/appcontroller.cpp:544-563`).

5. **Build the group** in `src/ui/settingsdialog.h` and `src/ui/settingsdialog.cpp`.

   - Header: declare `void updateLibreTranslateControls();` after `updateMozhiControls();` (`:86`) and
     add the members after `m_mozhiHint` (`:155`):

     ```cpp
     QLineEdit *m_libreTranslateBaseUrl = nullptr;
     QLineEdit *m_libreTranslateApiKey = nullptr;
     QLabel *m_libreTranslateEffectiveUrl = nullptr;
     QLabel *m_libreTranslateUrlWarning = nullptr;
     ```

     The hint needs no member — nothing reads or updates it, as `mymemoryHint` and `duckduckgoHint`
     show (the `m_mozhiHint` member is the exception, not the rule).

   - `buildUi()`: insert between `servicesLayout->addWidget(duckDuckGoGroup);` (`:448`) and
     `servicesLayout->addStretch();` (`:450`):

     ```cpp
     auto *libreTranslateGroup = new QGroupBox(tr("LibreTranslate"), servicesContent);
     libreTranslateGroup->setObjectName(QStringLiteral("libretranslateGroup"));
     auto *libreTranslateLayout = new QVBoxLayout(libreTranslateGroup);
     auto *libreTranslateForm = new QFormLayout;
     m_libreTranslateBaseUrl = new QLineEdit(libreTranslateGroup);
     m_libreTranslateBaseUrl->setObjectName(QStringLiteral("libretranslateBaseUrl"));
     // The default is empty, so the placeholder is an example, not a value: the
     // field never names a server the user did not enter.
     m_libreTranslateBaseUrl->setPlaceholderText(QStringLiteral("http://localhost:5000"));
     libreTranslateForm->addRow(tr("Server URL:"), m_libreTranslateBaseUrl);
     m_libreTranslateApiKey = new QLineEdit(libreTranslateGroup);
     m_libreTranslateApiKey->setObjectName(QStringLiteral("libretranslateApiKey"));
     m_libreTranslateApiKey->setEchoMode(QLineEdit::Password);
     m_libreTranslateApiKey->setPlaceholderText(
         tr("Optional; only a server that requires a key"));
     libreTranslateForm->addRow(tr("API key:"), m_libreTranslateApiKey);
     libreTranslateLayout->addLayout(libreTranslateForm);
     m_libreTranslateEffectiveUrl = new QLabel(libreTranslateGroup);
     m_libreTranslateEffectiveUrl->setObjectName(QStringLiteral("libretranslateEffectiveUrl"));
     m_libreTranslateEffectiveUrl->setWordWrap(true);
     m_libreTranslateEffectiveUrl->setTextFormat(Qt::PlainText);
     m_libreTranslateEffectiveUrl->setVisible(false);
     libreTranslateLayout->addWidget(m_libreTranslateEffectiveUrl);
     m_libreTranslateUrlWarning = new QLabel(
         tr("Enter a base http(s) URL without credentials, a query or a fragment. The service "
            "stays unconfigured until then."),
         libreTranslateGroup);
     m_libreTranslateUrlWarning->setObjectName(QStringLiteral("libretranslateUrlWarning"));
     m_libreTranslateUrlWarning->setWordWrap(true);
     m_libreTranslateUrlWarning->setVisible(false);
     libreTranslateLayout->addWidget(m_libreTranslateUrlWarning);
     auto *libreTranslateHint = new QLabel(
         tr("LibreTranslate translates through a server you run or choose, usually a self-hosted "
            "one. The base URL must point at a server you trust: its terms, availability, rate "
            "and text limits and installed languages are your responsibility, and a language the "
            "server has no model for is rejected. The API key is optional and only a server that "
            "requires one needs it. Quality is lower than Google or DeepL. Long text is sent in "
            "several requests."),
         libreTranslateGroup);
     libreTranslateHint->setObjectName(QStringLiteral("libretranslateHint"));
     libreTranslateHint->setWordWrap(true);
     libreTranslateLayout->addWidget(libreTranslateHint);
     servicesLayout->addWidget(libreTranslateGroup);
     connect(m_libreTranslateBaseUrl, &QLineEdit::textChanged, this,
             &SettingsDialog::updateLibreTranslateControls);
     ```

     Each `tr()` above is **one** message with concatenated literals: splitting the warning or the
     hint in two makes the `.ts` count in step 8 wrong. `Qt::PlainText` keeps a `&` in a typed URL
     from being read as a mnemonic, as `m_mozhiEffectiveInstance` does (`:399`). The two reused labels
     (`Server URL:`, `API key:`) already exist at `:338-339`; do not add `Base URL:` or
     `Instance URL:` variants.

   - `updateLibreTranslateControls()` next to `updateMozhiControls()` (`:841-847`):

     ```cpp
     void SettingsDialog::updateLibreTranslateControls()
     {
         const QString text = m_libreTranslateBaseUrl->text();
         const QString normalized = normalizedServiceBaseUrl(text);
         const bool entered = !text.trimmed().isEmpty();
         m_libreTranslateEffectiveUrl->setText(tr("Effective base URL: %1").arg(normalized));
         // An empty field is valid and means "not configured": the hint says what to
         // enter. Only a non-empty invalid value warns, because normalization then
         // drops the typed value.
         m_libreTranslateEffectiveUrl->setVisible(entered && !normalized.isEmpty());
         m_libreTranslateUrlWarning->setVisible(entered && normalized.isEmpty());
     }
     ```

     `Effective instance: %1` (`:845`) is **not** reused: "instance" is the Mozhi term, and the
     Russian translation is "Действующий экземпляр: %1", which reads wrong for a server URL. This is
     the only new wording beyond the brief, and it is one `%1` message.

   - `loadValues()`: after the Mozhi engine block (`:662-664`):

     ```cpp
     {
         const QSignalBlocker blocker(m_libreTranslateBaseUrl);
         m_libreTranslateBaseUrl->setText(settings.libretranslate.baseUrl);
     }
     m_libreTranslateApiKey->setText(settings.libretranslate.apiKey);
     ```

     and add `updateLibreTranslateControls();` next to `updateMozhiControls();` (`:722`), so the
     initial hidden/shown state matches the loaded value.

   - `settings()`: after the Mozhi lines (`:751-752`):

     ```cpp
     result.libretranslate.baseUrl = normalizedServiceBaseUrl(m_libreTranslateBaseUrl->text());
     result.libretranslate.apiKey = m_libreTranslateApiKey->text().trimmed();
     ```

     An invalid typed value therefore becomes the empty "not configured" string, exactly as
     `normalizedProofreadingEndpoint()` does for the endpoint (`:759-760`); `validate()` stays as it
     is.

6. **Extend `tests/tst_settings.cpp`.** Five edits; `core/settings.h` and `QSettings` are already
   included.

   a. `defaults()` (`:99-160`), after the Mozhi engine assertions (`:131-132`):

      ```cpp
      QVERIFY(settings.libretranslate.baseUrl.isEmpty());
      QVERIFY(settings.libretranslate.apiKey.isEmpty());
      ```

   b. New `serviceBaseUrlIsNormalized_data()`/`serviceBaseUrlIsNormalized()` after
      `mozhiInstanceIsNormalized()` (`:197-205`), columns `input`, `expected`, `valid`, where
      `expected` is empty for every invalid row:

      - `https://libretranslate.example.org` → same, valid;
      - `http://127.0.0.1:5000` → same, valid;
      - `http://127.0.0.1:5000/` → `http://127.0.0.1:5000`, valid;
      - `http://127.0.0.1:5000/lt/` → `http://127.0.0.1:5000/lt`, valid;
      - `  https://host/lt  ` → `https://host/lt`, valid;
      - `` (empty), `   ` (whitespace), `libretranslate.example.org`, `ftp://host`, `https://`,
        `https://user:pass@host`, `https://host/?api_key=x`, `https://host/#fragment`,
        `http://[::1`, `https://host:bad` → empty, invalid.

      The test body:

      ```cpp
      QCOMPARE(isValidServiceBaseUrl(input), valid);
      QCOMPARE(normalizedServiceBaseUrl(input), expected);
      ```

      The existing `mozhiInstanceIsNormalized` rows stay untouched — they are the proof that the
      wrapper kept the old behaviour.

   c. `roundTrip()` (`:238-367`): set
      `settings.libretranslate.baseUrl = QStringLiteral("http://127.0.0.1:5000/lt/");` and
      `settings.libretranslate.apiKey = QStringLiteral("libretranslate-secret");` next to the LLM
      values (`:272-274`); after loading assert the normalized
      `result.libretranslate.baseUrl == QStringLiteral("http://127.0.0.1:5000/lt")` and the key
      `libretranslate-secret`; add to the raw-INI block (`:349-357`)
      `raw.value("libretranslate/baseUrl") == "http://127.0.0.1:5000/lt"`; add
      `QVERIFY(!ini.contains("libretranslate-secret"));` next to the other secret checks (`:363-366`).

   d. `corruptValuesAreNormalized()` (`:736-775`): add
      `raw.setValue("libretranslate/baseUrl", "https://user:secret@example.invalid/?q=1#f")` and,
      after `load()`, `QVERIFY(result.libretranslate.baseUrl.isEmpty());` — an uncanonical base URL is
      dropped to "not configured", and the warning text in step 3 must not contain the typed value.

   e. New `libretranslateGroupIsOptional()` after `mozhiGroupIsOptional()` (`:851-875`), modelled on
      it: an INI with `settingsVersion=1` and `app/startMinimized=false` and **no** `[libretranslate]`
      loads with `settingsVersion == 1`, `startMinimized == false` and both
      `libretranslate.baseUrl`/`apiKey` empty, and `QCOMPARE(readFile(path), before)` proves that
      loading does not rewrite an old file.

7. **Extend `tests/tst_settingsdialog.cpp` and `tests/tst_libretranslateservice.cpp`.** No new test
   target, no new include in the dialog test (`QGroupBox`, `QLineEdit`, `QLabel`, `QLayout` are already
   included).

   a. New `libreTranslateGroup()` after `duckduckgoGroup()` (`:662-694`): find `libretranslateGroup`,
      `libretranslateBaseUrl`, `libretranslateApiKey`, `libretranslateEffectiveUrl`,
      `libretranslateUrlWarning`, `libretranslateHint` and `duckduckgoGroup`, then

      ```cpp
      QCOMPARE(group->title(), QStringLiteral("LibreTranslate"));
      QVERIFY(!group->isHidden());
      QVERIFY(group->isEnabled());
      QVERIFY(baseUrl->text().isEmpty());
      QCOMPARE(apiKey->echoMode(), QLineEdit::Password);
      QVERIFY(apiKey->text().isEmpty());
      // Empty is valid and means "not configured": no warning, no effective URL.
      QVERIFY(effective->isHidden());
      QVERIFY(warning->isHidden());
      QVERIFY(hint->wordWrap());
      QVERIFY(warning->wordWrap());
      QVERIFY(effective->wordWrap());
      QVERIFY(hint->text().contains(QStringLiteral("self-hosted")));
      QVERIFY(hint->text().contains(QStringLiteral("you trust")));
      QVERIFY(hint->text().contains(QStringLiteral("optional")));
      QVERIFY(hint->text().contains(QStringLiteral("Google")));
      QVERIFY(hint->text().contains(QStringLiteral("several requests")));
      QVERIFY(!hint->text().contains(QStringLiteral("third party")));
      QCOMPARE(group->findChildren<QLineEdit *>().size(), 2);

      // A non-empty valid value shows the normalized effective URL.
      baseUrl->setText(QStringLiteral("http://127.0.0.1:8080/lt/"));
      QVERIFY(!effective->isHidden());
      QVERIFY(warning->isHidden());
      QVERIFY(effective->text().contains(QStringLiteral("http://127.0.0.1:8080/lt")));
      QVERIFY(!effective->text().contains(QStringLiteral("/lt/")));
      QCOMPARE(dialog.settings().libretranslate.baseUrl,
               QStringLiteral("http://127.0.0.1:8080/lt"));

      // A non-empty invalid value warns and is stored as "not configured".
      baseUrl->setText(QStringLiteral("ftp://host"));
      QVERIFY(effective->isHidden());
      QVERIFY(!warning->isHidden());
      QVERIFY(warning->text().contains(QStringLiteral("http(s)")));
      QVERIFY(dialog.settings().libretranslate.baseUrl.isEmpty());

      // The key is trimmed and never configures the provider.
      baseUrl->clear();
      apiKey->setText(QStringLiteral("  secret-key  "));
      QCOMPARE(dialog.settings().libretranslate.apiKey, QStringLiteral("secret-key"));
      QVERIFY(dialog.settings().libretranslate.baseUrl.isEmpty());

      // The group follows DuckDuckGo and stays above the stretch.
      const QLayout *servicesLayout = group->parentWidget()->layout();
      QVERIFY(servicesLayout);
      QVERIFY(servicesLayout->indexOf(duckDuckGo) >= 0);
      QVERIFY(servicesLayout->indexOf(duckDuckGo) < servicesLayout->indexOf(group));
      QVERIFY(servicesLayout->itemAt(servicesLayout->count() - 1)->spacerItem());
      ```

      Substring assertions only for the hint and the warning; the effective label is checked by
      containment of the normalized URL, never by the whole sentence. `isHidden()` is the right
      visibility check in a never-shown dialog (the dialog is constructed, not shown; see
      `tests/tst_settingsdialog.cpp:527-546` for the same pattern).

   b. `freeWebGateIsGone()` (`:440-441`): add `"libretranslateGroup"` to the loop — the group is
      always enabled, like the other free providers.

   c. `providerOrder()` (`:803-809`): append `QStringLiteral("libretranslateGroup")` after
      `QStringLiteral("duckduckgoGroup")`.

   d. `tests/tst_libretranslateservice.cpp`: new `appliesSettings()` after `configuration()`
      (`:351-389`, before `translation()` at `:391`), modelled on
      `tests/tst_mozhiwebservice.cpp:219-252`; `core/settings.h` is already included (`:2`):

      ```cpp
      AppSettings settings;
      settings.libretranslate.baseUrl =
          QStringLiteral("http://127.0.0.1:%1/lt/").arg(m_server->port());
      settings.libretranslate.apiKey = QStringLiteral("secret-key");
      m_service->applySettings(settings);
      QCOMPARE(m_service->baseUrl(),
               QUrl(QStringLiteral("http://127.0.0.1:%1/lt").arg(m_server->port())));
      QCOMPARE(m_service->apiKey(), QStringLiteral("secret-key"));
      QVERIFY(m_service->isConfigured());

      // An invalid URL is normalized away, so the service is unconfigured and
      // never contacts anything.
      settings.libretranslate.baseUrl = QStringLiteral("ftp://example.invalid/x");
      m_service->applySettings(settings);
      QCOMPARE(m_service->baseUrl(), QUrl());
      QVERIFY(!m_service->isConfigured());
      QCOMPARE(m_server->requests.size(), 0);

      // An empty key is valid and leaves the service configured.
      settings.libretranslate.baseUrl =
          QStringLiteral("http://127.0.0.1:%1").arg(m_server->port());
      settings.libretranslate.apiKey.clear();
      m_service->applySettings(settings);
      QVERIFY(m_service->apiKey().isEmpty());
      QVERIFY(m_service->isConfigured());
      ```

      The base URL carries the trailing slash on purpose: it proves that `applySettings()` normalizes
      and that the request path is `<base>/translate`, not `<base>//translate`.

8. **Regenerate and translate the `.ts` files.** First copy the 14 `i18n/qtranslate_*.ts` to
   `/tmp/opencode/ts-before/` (step 9 compares against that copy), then from the repository root:

   ```
   cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build/linux-release --target update_translations
   ```

   **Do not call `lupdate` directly and do not run this target inside `qtranslate-trixie`.** The
   `lupdate` in `PATH` on this host is Qt **5.15.8** (`/usr/bin/lupdate`, a qtchooser wrapper) and
   would downgrade every `.ts`; the Qt 6 tools live in `/usr/lib/qt6/bin/lupdate` (6.10.2), and the
   CMake target is the only supported entry point. The trixie image has Qt 6.8.2, whose `lupdate`
   would also rewrite the files.

   Expect in each of the 14 files **382** `<message>` entries (376 + 6): **five** new sources in the
   existing `SettingsDialog` context — `LibreTranslate`, `Optional; only a server that requires a key`,
   `Effective base URL: %1`, the warning and the hint — and **one** in a new `LibreTranslateService`
   context, `LibreTranslate needs the base URL of a LibreTranslate server.`, left over from
   `01-libretranslate-service`. All six arrive as `type="unfinished"`, 0 `type="vanished"`, plus
   `<location>` churn (the dialog source grows by the inserted block and `settings.cpp` gains lines,
   so later locations shift; `lupdate` also re-sorts by location). If anything shows up as vanished,
   rerun `lupdate -no-obsolete -project build/linux-release/.lupdate/qtranslate_lupdate_project.json`
   and check the count again; if an unexpected message changed, stop and report instead of accepting
   it.

9. **Translate the six messages by hand in all 14 files** (`ru_RU`, `es`, `de`, `fr`, `pt_BR`, `it`,
   `pl`, `uk`, `tr`, `zh_CN`, `ja`, `ko`, `ar`, `hi`), remove `type="unfinished"` and keep the
   sources byte-identical (never retype a `<source>`).

   - `LibreTranslate` — the product name, untranslated in every file
     (`<translation>LibreTranslate</translation>` equals the source), like `Bing`, `Reverso`,
     `MyMemory`, `DuckDuckGo`.
   - `LibreTranslate needs the base URL of a LibreTranslate server.` — the service hint that the
     translation manager shows as the `Auth` message; keep `LibreTranslate` untranslated.
   - `Optional; only a server that requires a key` — the key placeholder; keep it short, the sentence
     already says the key is optional.
   - `Effective base URL: %1` — keep `%1`; do not translate it to "instance".
   - The warning: keep the `http(s)` token recognisable and say that the service stays unconfigured.
   - The hint: reuse, from the same file, the wording of the existing tails where they mean the same —
     "Long text is sent in several requests." (the tail of the MyMemory, Reverso, Mozhi and DuckDuckGo
     hints) — and keep all facts: a LibreTranslate server the user runs or chooses, usually
     self-hosted; the base URL must point at a server the user trusts; its terms, availability, rate
     and text limits and installed languages are the user's responsibility; a language without a model
     is rejected; the key is optional and only a server that requires one needs it; quality is lower
     than Google or DeepL; long text goes in several requests. No placeholder, no emoji or `→`/`✓`, no
     "the text is sent to a third party" sentence.

   Reference for `ru_RU` (the tail is the MyMemory/Reverso tail of that file):

   ```
   LibreTranslate переводит через сервер, который вы запускаете или выбираете, обычно собственный. Базовый URL должен указывать на сервер, которому вы доверяете: его условия, доступность, ограничения частоты и длины текста и установленные языки — ваша ответственность, а язык, для которого на сервере нет модели, будет отклонён. Ключ API необязателен и нужен только серверу, который его требует. Качество ниже, чем у Google или DeepL. Длинный текст отправляется несколькими запросами.
   ```

   Then compile the catalogues once:
   `cmake --build build/linux-release --target release_translations`, and check that `lrelease`
   reports no unfinished message for the 14 files (`Generated translation file … (382
   translations)`), before moving to step 10.

10. **Verify.** Build and test in this order:

    - host Debug and Release: `cmake -S . -B build/linux-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug` and
      the existing `build/linux-release`; `cmake --build` each and check the logs for `warning:` —
      `-Wall -Wextra` must stay clean in both. The one compile error to expect on the way is the
      missing `#include "core/settings.h"` in `libretranslateservice.cpp` (step 4);
    - `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure` and the same for
      `build/linux-debug`: **50/50** — no new target, `ctest -N` still reports 50;
    - per-target counts: `tst_settingsdialog` **35 → 36** passes (the new case; the 35 are 33 named
      cases plus QTest's implicit `initTestCase`/`cleanupTestCase`), `tst_settings` grows by the new
      `libretranslateGroupIsOptional` case and the rows of the new data function (record the measured
      number), `tst_libretranslateservice` **39 → 40** cases, `tst_translations` 4/4;
    - `.ts` checks:

      ```
      for f in i18n/*.ts; do printf '%s %s\n' "$f" "$(rg -c '<message' "$f")"; done   # 382 each
      rg -n 'type="unfinished"|type="vanished"' i18n                                 # no output
      rg -c '<name>LibreTranslateService</name>' i18n/*.ts                           # 1 per file
      rg -c '<source>Effective base URL: %1</source>' i18n/*.ts                      # 1 per file
      ```

      plus a throwaway `python3` here-document (no file in the repository) that, for each of the 14
      files, asserts: 382 messages; every `<source>` of `/tmp/opencode/ts-before/<file>` is still
      present and unchanged (nothing vanished, no translation lost); the six new sources present once;
      no unfinished, vanished or empty translation; `LibreTranslate` equals the source; the hint
      translation contains the reused "several requests" tail of the same file. Print every new
      translation and read all 14;
    - Release build and suite in the container (host stays responsive):
      `docker run --rm -v "$PWD:/src:ro" -w /src qtranslate-trixie bash -c 'cmake -S /src -B
      /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/build --parallel &&
      timeout 900 xvfb-run -a ctest --test-dir /tmp/build --output-on-failure > /tmp/ctest.log 2>&1;
      tail -n 40 /tmp/ctest.log'` — 0 warnings, 50/50. This also proves that trixie's `lrelease` 6.8.2
      accepts the `.ts` written by lupdate 6.10.2. `xvfb-run` inside that image hung after `ctest`
      before: redirect the log inside the container and keep the `timeout`, otherwise a hang looks
      like a failing test;
    - boundary: `rg -n "third party" src/ui/settingsdialog.cpp` finds nothing;
      `rg -n "LibreTranslate" src/` shows the service, `appcontroller`, `languages`, `settings`,
      `settingsdialog` and the CMake lists only, with no new provider registration;
      `git status --porcelain` shows `src/core/settings.{h,cpp}`, `src/services/libretranslateservice.cpp`,
      `src/app/appcontroller.cpp`, `src/ui/settingsdialog.{h,cpp}`, `tests/tst_settings.cpp`,
      `tests/tst_settingsdialog.cpp`, `tests/tst_libretranslateservice.cpp` and the 14
      `i18n/qtranslate_*.ts` (plus the roadmap files the lead maintains). No `doc/` outside
      `doc/roadmap/`, no `AGENTS.md`, no README, no `doc/handoff.md`, no `CMakeLists.txt` and no
      `tests/CMakeLists.txt` change;
    - Windows Debug/Release have no MSVC here: they are **not performed**, not passed. The changed
      sources are shared, so a Windows build sees them. Note the target count for the lead: the
      shared/Windows-only split in `tests/CMakeLists.txt` is now 41 platform-neutral targets plus 3
      Windows-only ones, i.e. **44** Windows targets; `AGENTS.md` still records the pre-stage-11
      "37 shared, 40 Windows" and `01-libretranslate-service` recorded 42, so the number in the
      documentation pass needs a recount, not a guess.

## Done when

- `[libretranslate] baseUrl` and `apiKey` are loaded, saved and normalized; the key goes through
  `readSecret()`/`writeSecret()` and never appears in the INI plaintext; an empty base URL is preserved
  as "not configured", an empty key as "no key", and an invalid or credential-bearing base URL is
  dropped with a fixed warning that does not echo the typed value; `settingsVersion` stays 1 and a
  file without the group loads with the empty defaults and is not rewritten.
- `isValidServiceBaseUrl()`/`normalizedServiceBaseUrl()` live in `src/core/settings.h/.cpp` and
  `isValidMozhiInstance()`/`normalizedMozhiInstance()` are thin wrappers; every existing Mozhi row of
  `tst_settings` passes without an edit, and the new rows cover `http(s)`, host, credentials, query,
  fragment, path prefix, trailing slash and whitespace.
- `LibreTranslateService::applySettings()` applies both values through `setBaseUrl()`/`setApiKey()`
  and re-normalizes; `AppController` clears the translation cache when `libretranslate.baseUrl`
  changes and does not abort an active reply, and a key change clears nothing and aborts nothing.
- The Services page shows the `LibreTranslate` group after `Reverso` and `DuckDuckGo` and above the
  stretch, with the base-URL field (example placeholder, `Server URL:` label), the key field in
  password echo with the optional-key placeholder, the effective-value label, the warning and the hint;
  the effective label and the warning are hidden for an empty field, the effective label shows the
  normalized value for a valid one and the warning appears for an invalid one; `settings()` returns the
  normalized value and `validate()` still adds no failure.
- `tst_settings` covers the empty defaults, the round trip of the URL and the secret key, the
  normalization rows, the corrupt-value and optional-group cases; `tst_settingsdialog` covers the
  group, both fields, the echo mode, the effective label, the warning, the hint substrings, the absent
  "third party" sentence and the page order; `tst_libretranslateservice::appliesSettings()` proves the
  wiring. No new test target.
- The six new strings are in all 14 `.ts` files, translated by hand, with no unfinished, vanished or
  empty entry: 382 messages per file, `release_translations` and `tst_translations` green.
- Debug and Release build without warnings on the host and in `qtranslate-trixie`; all 50 Linux tests
  pass in both host configurations and in the container; Windows is reviewed by diff and recorded as
  not performed, with its target count recounted as 44 (not 40 or 42).

## Risks

- **The shared helper must not change Mozhi.** `normalizedMozhiInstance()` returns the Mozhi default
  for an invalid value while `normalizedServiceBaseUrl()` returns an empty string, so the wrapper keeps
  the default and only delegates the canonicalization. If `tst_settings::mozhiInstanceIsNormalized`
  needs an edit, the extraction is wrong: fix the code, not the expectation.
- **Empty is valid here, unlike Mozhi.** Do not copy the Mozhi warning ("The default instance will be
  used"): LibreTranslate has no default server, so an invalid value means "unconfigured" and the
  warning says so. Do not add a fallback URL either — research §1.5 and the stage forbid sending text
  to a server the user did not enter.
- **A missing `#include "core/settings.h"` in `libretranslateservice.cpp`** is the first compile error
  after step 4; `src/core/translationservice.h:10` only forward-declares `AppSettings`.
- **The `.ts` count is the acceptance number.** Six messages, not seven: the labels are reused
  (`Server URL:`, `API key:`), the base-URL placeholder is an untranslated `QStringLiteral`, and the
  warning and the hint are one `tr()` each. Splitting either, or adding a `Base URL:` label, gives 383+
  and a near-duplicate for translators.
- **`isHidden()` vs `isVisible()`:** every child of a never-shown dialog is `isVisible() == false`, so
  the visibility assertions use `isHidden()` (explicit `setVisible(false)`), as `mozhiGroup()` does.
- **`<location>` churn in all 14 files is expected** (the dialog source grows, `settings.cpp` gains
  lines). Never revert it and never retype a `<source>`, or the translation disconnects from the binary
  and the UI silently falls back to English.
- **`lupdate` from `PATH` is Qt 5.15** and would downgrade the `.ts`; only the `update_translations`
  target (Qt 6.10.2) is safe, and it must not run in the trixie image (Qt 6.8.2).
- **The base URL is user input and may carry credentials.** The log lines and the warning never echo
  it, the service keeps it out of messages (`01-libretranslate-service` already asserts that), and the
  key stays a secret through `SecretProtector`; a `401`/`403` message names the key, never the text.
- **A cached translation from another server would be wrong**, hence the cache clear on a base-URL
  change; a key change needs none because `TranslationManager` caches only successful results. The
  cache reset itself is code-reviewed, as with Mozhi — there is no `AppController` test target.
- **Nothing is needed from the user:** no key, no server, no live check. A live smoke against a real
  server stays out of scope (no server is part of the task) and is recorded as not performed; the
  fixtures plus the local `QTcpServer` remain the automated gate. An optional visual check of the
  Services page in Russian is covered by the offscreen dialog case.

## Outcome

Done with this commit, base `3f393df`. `LibreTranslateSettings` (`src/core/settings.h`) holds `baseUrl`
and `apiKey`, both empty by default, and `AppSettings.libretranslate` sits between `mozhi` and `llm`;
`kCurrentVersion` stays 1 and `AppSettings::AppSettings()` is untouched. `SettingsStore::normalize()`
trims `[libretranslate] baseUrl`, keeps an empty value as "not configured", drops an invalid one with
the fixed warning `LibreTranslate base URL is not a base http(s) URL; the service stays unconfigured`
(the typed value is never echoed) and canonicalizes a valid one; `apiKey` is not trimmed in the store
(the dialog and the service trim it) and goes through `readSecret()`/`writeSecret()`, so the INI never
holds the plaintext and an unchanged secret is not re-protected. `load()`/`save()` use the group
between `[mozhi]` and `[llm]`; a file without the group loads with the empty defaults and is not
rewritten, which the new `libretranslateGroupIsOptional()` proves byte for byte.

`isValidServiceBaseUrl()`/`normalizedServiceBaseUrl()` live in `src/core/settings.cpp`;
`isValidMozhiInstance()` is a direct delegation and `normalizedMozhiInstance()` keeps the Mozhi default
for an invalid value, so every pre-existing row of `mozhiInstanceIsNormalized` passes without an edit.
`LibreTranslateService::applySettings()` now includes `core/settings.h` and re-normalizes at the
service boundary before `setBaseUrl()`/`setApiKey()`; an empty or invalid URL leaves the service
unconfigured, and the key never changes `isConfigured()`. `AppController` clears the translation cache
only when `libretranslate.baseUrl` changes, keeps an active reply alive (the endpoint and key are
captured when the reply is built), clears nothing on a key change and calls no
`updateServiceSelectors()`.

The Services page gains `libretranslateGroup` after `duckduckgoGroup` and before the stretch: a form
with the existing `Server URL:`/`API key:` labels, the base URL field with the example placeholder
`http://localhost:5000`, the key field in password echo with the optional-key placeholder, the
word-wrapped `libretranslateEffectiveUrl` (`Qt::PlainText`, hidden while the field is empty) and
`libretranslateUrlWarning` (only for a non-empty invalid value), and the word-wrapped
`libretranslateHint` as one `tr()` that carries the self-hosted/trust/limits/languages facts, the
optional key, the quality note and the reused "several requests" tail, with no "third party"
sentence. `updateLibreTranslateControls()` is connected to `textChanged` and called from
`loadValues()`; `settings()` returns the normalized URL and the trimmed key; `validate()` is unchanged.

Tests: `tst_settings` 84 passes (68 before: 15 data rows of the new `serviceBaseUrlIsNormalized`, the
new `libretranslateGroupIsOptional`, and the round-trip/default/corrupt-value assertions),
`tst_settingsdialog` 36 (35 before), `tst_libretranslateservice` 40 (39 before), `tst_translations`
4/4. No new test target: `ctest -N` reports 50 on Linux, as before.

The 14 `i18n/qtranslate_*.ts` were regenerated through `update_translations` (Qt 6.10.2 `lupdate`),
the six messages were translated by hand and `release_translations` compiled them: **382** `<message>`
per file, 0 `unfinished`, 0 `vanished`, no empty translation, `LibreTranslate` equals its source, `%1`
kept, and the hint ends with the same "several requests" tail as the Mozhi/DuckDuckGo hint of the same
file. The `LibreTranslateService` context is new and carries the `configurationError()` message that
`01-libretranslate-service` had left for this pass.

Checks: host Debug and Release build warning-free and pass 50/50; a Release build and the suite in
`qtranslate-trixie` (Qt 6.8.2) are warning-free and 50/50, which also proves trixie's `lrelease`
accepts the catalogues written by lupdate 6.10.2. Windows Debug/Release are **not performed** (no
MSVC here), reviewed by diff; the Windows target count is now **44** (41 platform-neutral + 3
Windows-only), not 40 or 42.

What turned out on the way:

- The `<location>` churn in the `.ts` is larger than the inserted block: `i18n/` was last regenerated
  at `589dbb5`, and `e0b411b` and `49a37a0` moved lines in `appcontroller.cpp`, `settings.cpp` and
  `languages.cpp`. A script against `git show HEAD:` confirmed no `<source>` and no existing
  `<translation>` changed, so only locations were refreshed along with the six new messages.
- `LibreTranslateService::isConfigured()` still duplicates the URL predicate and does not check
  `userInfo`, a query or a fragment. It is unreachable through the settings because `applySettings()`
  normalizes first, and only tests call `setBaseUrl()` directly, so it is left as
  `01-libretranslate-service` wrote it; the comment above it now says so instead of pointing at this
  task in the future tense.
- Review found no defects. The only remark — the two stale comments above — was accepted and rewritten
  in this commit; the full suite was re-run after the comment-only edit.
- Deferred to the lead's documentation pass (this task does not edit `doc/` outside `doc/roadmap/`):
  `doc/architecture.md` does not name the LibreTranslate group on the Services page,
  `doc/free-endpoints.md` still says "five no-key web services" and omits LibreTranslate from its
  integration list, `AGENTS.md` still records 374 `.ts` messages and 40 Windows tests, and both
  READMEs and `doc/handoff.md` need the provider. The live smoke against a real LibreTranslate server
  was not performed: no server is part of the task, and the fixtures plus the local `QTcpServer` stay
  the automated gate.
