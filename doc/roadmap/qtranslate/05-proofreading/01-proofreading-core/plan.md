---
kind: task
status: in-progress
detailed: true
---

# Proofreading core contract and settings

## Task

Define the provider-independent proofreading layer in `qtranslate_core` and the `proofreading/*`
settings group, and prove both with a fake provider. No network service, no UI, no `AppController`
wiring: those are `02-languagetool-parser`, `03-languagetool-service`, `04-text-checking-tab` and
`05-main-window-check-and-replace`. This child is the contract every later child builds on, so it is
done first and reviewed on its own.

The design note `doc/proofreading.md` §4 fixes the data structures, the interface and the UTF-16
`offset`/`length` rule; `doc/architecture.md` §4.15 repeats them and §4.5/§4.9 show the
reply/service/manager pattern this layer must follow. Two deviations are deliberate and recorded in
the stage body: the interface returns a reply object instead of taking a `std::function` callback
(ownership and cancellation identical to `TranslationReply`, `src/core/translationservice.h:12`),
and the error type is the existing `TranslationError` (`src/core/translationtypes.h:16`) instead of a
second enum, so `ui/translationerrortext` and the established wording apply unchanged.

Nothing in this child talks about a specific provider: no endpoint, no HTTP, no parser, no
`tr()` UI strings. The only translatable strings are the manager's fixed refusal messages (no
provider, missing key, the online gate), and the gate one names the "Text checking" tab that
`04-text-checking-tab` adds; they are added to the 14 `.ts` files there (the group is off by default
and no provider is registered yet, so the transient state is harmless).

## Steps

1. **Add the value types in `src/core/proofreadingtypes.h`.**
   - `struct ProofreadingIssue` exactly as `doc/proofreading.md` §4: `int offset = 0`, `int length = 0`,
     `QString message`, `QStringList replacements`, `QString ruleId`, `QString category`,
     `int severity = 0`. Comment that `offset`/`length` count `QChar` (UTF-16) units, as in `QString`,
     and that a surrogate pair is two units.
   - `struct ProofreadingResult { QVector<ProofreadingIssue> issues; QString correctedText;
     QString serviceId; TranslationError error = TranslationError::None; QString errorMessage;
     bool ok() const { return error == TranslationError::None; } }`. `correctedText` stays empty for
     a provider that returns issues (LanguageTool) and is filled only by a provider
     that returns ready text (the stage-6 LLM).
   - `struct ProofreadingRequest { QString text; QString language; QStringList categories; }`;
     `language` is an internal code or `Languages::autoCode()` ("auto"), `categories` uses the values
     from `proofreadingCategoryIds()` and an empty list means all four. Document both rules in the
     header, because the service and the UI rely on them.
   - Include `core/translationtypes.h` for `TranslationError` (do not copy the enum). Add
     `Q_DECLARE_METATYPE(ProofreadingRequest)` and `Q_DECLARE_METATYPE(ProofreadingResult)`, as
     `translationtypes.h:44` does, so a future signal or `QSignalSpy` case needs no change.
   - Add `src/core/proofreadingtypes.h` to the `qtranslate_core` list next to `translationtypes.h`
     (`CMakeLists.txt:38-70` lists headers explicitly; the type file has no `.cpp`).

2. **Add the reply and service interface in `src/core/proofreadingservice.{h,cpp}`.**
   - `class ProofreadingReply : public QObject` with the same surface as `TranslationReply`
     (`src/core/translationservice.h:12-47`): `static ProofreadingReply *createFinished(const
     ProofreadingResult &, QObject *parent = nullptr)`, `const ProofreadingResult &result() const`,
     `bool isFinished() const`, `void abort()`, `signals: void finished()`, and the protected
     `finish(const ProofreadingResult &)` / `virtual void abortImpl()`. Copy the implementation of
     `src/core/translationservice.cpp` statement for statement so the semantics are identical:
     exactly one `finished()`, never emitted from inside `check()`, `abort()` finishes with
     `Cancelled` first and then stops the work, a repeated `finish()` is ignored, and
     `createFinished()` uses `Qt::QueuedConnection` so the caller can connect.
   - `class ProofreadingService : public QObject`:
     ```cpp
     virtual QString id() const = 0;                    // "languagetool" (stage 6 adds "llm")
     virtual QString displayName() const = 0;
     virtual bool isConfigured() const = 0;
     virtual QString configurationError() const { return {}; }
     virtual QStringList languages() const = 0;         // internal codes, without "auto"
     virtual bool supportsAutoLanguage() const { return false; }
     // Sends the text to a remote service; the manager refuses it while
     // proofreading/allowOnlineServices is false. A future local engine would
     // return true; no provider of this stage does.
     virtual bool isLocal() const { return false; }
     virtual void applySettings(const AppSettings &settings) = 0;
     virtual ProofreadingReply *check(const ProofreadingRequest &request) = 0;
     ```
     Follow `translationservice.h:49-77` for the comments and the `struct AppSettings;` forward
     declaration (the interface must not include `core/settings.h`).
   - Add both files to `qtranslate_core` after `popupplacement.*` and before `settings.*` in
     `CMakeLists.txt:38-70`, keeping the block readable.

3. **Add the offset-preserving splitter to `src/core/textchunker.{h,cpp}`.**
   ```cpp
   struct Part
   {
       QString text;
       int offset = 0; // UTF-16 index of text inside the unsplit input
   };
   QVector<Part> splitWithOffsets(const QString &text, int maxLength);
   ```
   Implement it as a loop over `split(text, maxLength)` with a running offset: because `split()`
   guarantees that joining the parts gives back the input exactly
   (`src/core/textchunker.h:7-14`), the offset is the code-unit index of the part and no separator
   arithmetic is needed. Add `#include <QVector>`. Do not touch `split()`/`splitUtf8()` and do not
   duplicate the boundary search; providers use this helper so their chunk offsets cannot drift.
   Extend `tests/tst_textchunker.cpp` (existing target, no new one) with: empty text → empty result,
   one part at the exact limit, three parts whose `offset + text.size()` equals the next offset and
   whose last part ends the input, a surrogate pair never split (join equality), a `\r\n` never
   split, and `maxLength` below one code point. This helper is the "merging offsets when splitting
   long text" item of `doc/proofreading.md` §7.

4. **Add the manager in `src/core/proofreadingmanager.{h,cpp}`.**
   - `enum class Channel { MainWindow, InPlace };` — the two streams of the stage-5 scenarios; a new
     request in a channel cancels the unfinished one of the same channel only, as in
     `TranslationManager` (`src/core/translationmanager.h:24`). There is no popup channel: the
     external scenario has a confirmation window and reuses `InPlace`.
   - Registry identical in shape to `TranslationManager::registerService()`: takes ownership, the
     first registered service becomes active, `services()`, `service(const QString &id)`,
     `activeService()`, `setActiveService(const QString &id)` (ignored for an unknown id, logs a fixed
     warning), `signals: void activeServiceChanged(const QString &id)`.
   - `bool onlineServicesAllowed() const` / `void setOnlineServicesAllowed(bool)`; no cache and no
     fallback policy in v1, because a recheck is cheap and the design asks for neither.
   - `ProofreadingReply *check(const ProofreadingRequest &request, Channel channel)`: abort the
     previous unfinished reply of the channel first (it finishes `Cancelled`), then run the checks in
     this order and return a `ProofreadingReply::createFinished()` for each failure without calling
     the service:
     * `request.text.trimmed().isEmpty()` → `EmptyText` with an empty message;
     * no active service → `ServiceUnavailable` with the fixed
       `tr("No text checking provider is available.")`;
     * `!service->isConfigured()` → `Auth` with `service->configurationError()` (empty message falls
       back to the generic `tr("The text checking provider needs a key.")`);
     * `!service->isLocal() && !onlineServicesAllowed()` → `ServiceUnavailable` with
       `tr("Enable online text checking in Settings → Text checking.")`;
     * `language == Languages::autoCode()` and `!service->supportsAutoLanguage()`, or a code that is
       not in `service->languages()` → `UnsupportedLanguage` with the same
       `language "%1" is not supported` wording the translation services use.
     For a valid request call `service->check(request)` and keep a `QPointer<ProofreadingReply>` per
     channel; the caller owns the reply (do not delete it in the manager).
   - `cancel(Channel)`, `cancelAll()`; `cancelAll()` is for shutdown, as in `TranslationManager`.
   - Logging: `Q_LOGGING_CATEGORY(lcProofreading, "qtranslate.proofreading", QtWarningMsg)` and log
     only the provider id, the channel name, the error kind, the request language/categories and the
     elapsed time. The text, the issue messages, the replacements, a URL and a key are never logged;
     use a local `errorName(TranslationError)` helper like `translationmanager.cpp:39` instead of the
     enum value.
   - Add `src/core/proofreadingmanager.{h,cpp}` and `proofreadingservice.*` to `qtranslate_core`.

5. **Add `ProofreadingSettings` to `src/core/settings.{h,cpp}`.**
   - `struct ProofreadingSettings` next to the other groups (`settings.h:44-80`):
     ```cpp
     bool enabled = false;
     QString provider = QStringLiteral("languagetool"); // the only id of this stage
     QString language = QStringLiteral("auto");
     QString endpoint;                                  // empty: the provider default
     QString apiKey;
     QStringList categories;                            // the four ids; set in the AppSettings constructor
     bool checkBeforeTranslation = false;
     bool autoReplace = false;
     bool allowOnlineServices = false;
     ```
     Initialise `categories` to `proofreadingCategoryIds()` in `AppSettings`' constructor
     (`settings.cpp`), so the default is the explicit four and a fresh `AppSettings` and a loaded
     file agree; a member initialiser cannot call the helper. The request-level rule "empty = all"
     stays for a caller, but the settings layer never stores an empty list (an empty or unknown INI
     value is normalised to the four).
   - Add `ProofreadingSettings proofreading;` to `AppSettings` after `llm` (`settings.h:106`), so the
     INI group order becomes `[app]`, `[hotkeys]`, `[translation]`, `[google]`, `[deepl]`,
     `[mozhi]`, `[llm]`, `[proofreading]`, `[network]`, … `kCurrentVersion` stays `1`; the group is
     optional and an old file loads the defaults without a rewrite, like `[mozhi]`
     (`settings.cpp:195-205`, `1575-1584` in `doc/architecture.md`).
   - Pure helpers next to `mozhiEngineIds()` (`settings.h:118`): `QStringList
     proofreadingCategoryIds()` returning `{"spelling", "grammar", "punctuation", "style"}` in that
     order, and `QStringList proofreadingProviderIds()` returning `{"languagetool"}`. The provider
     list holds only ids that a registered service can answer, so the settings store and the UI have
     no dead values; stage 6 appends `llm` together with its service, and the dropped
     `06-spelling-providers` would have appended its ids the same way. The settings, the manager and
     the tests share both lists instead of copying strings.
   - Normalisation in `normalize()` (`settings.cpp:165-205` is the model):
     * `provider`: trimmed and lowercased; when not in `proofreadingProviderIds()`, reset to
       `languagetool` with a fixed warning that does not echo the value;
     * `language`: `auto` or a code accepted by `Languages::isKnown()`, otherwise `auto` with a fixed
       warning;
     * `endpoint`: trimmed; an empty value stays empty; a non-empty value must be an absolute
       `http`/`https` URL with a host, without user info, query or fragment — otherwise reset to empty
       with a fixed warning. Add `bool isValidProofreadingEndpoint(const QString &)` and
       `QString normalizedProofreadingEndpoint(const QString &)` next to `isValidMozhiInstance()`
       (`settings.h:123-125`) so the tab and the test use the same rule. The value is the complete
       check URL (`https://api.languagetool.org/v2/check` or a self-hosted equivalent); the provider
       default is applied by the service, not stored.
     * `categories`: split on `,` if it arrives as a string, trim and lowercase each item, keep the
       known ones in `proofreadingCategoryIds()` order; an empty or entirely unknown list becomes all
       four with a fixed warning. Document that empty means all, so a hand-edited
       `categories=` cannot silently disable checking.
   - Load and save the group in `load()`/`save()` (`settings.cpp:356-470`, `508-598`): the booleans
     and strings with `QSettings::value`, the secret through `readSecret`/`writeSecret`
     (`settings.cpp:68-119`) with the old value, so an unchanged key keeps its token and a transient
     keyring failure does not drop it — same as `llm.apiKey`.
   - Do not add `proofreading/hotkey` here; `07-external-selection-proofreading` adds the key when it
     registers the hotkey.

6. **Write `tests/tst_proofreadingmanager.cpp`** and register it in the platform-neutral block of
   `tests/CMakeLists.txt` between `tst_popupwindow` (line 43) and `tst_queuedwebservice` (line 51).
   A local fake subclass of `ProofreadingService` with settable `languages()`,
   `supportsAutoLanguage()`, `isLocal()`, `isConfigured()`, `configurationError()`, a `check()`
   counter and an `abortImpl()` counter; its `check()` records the request and returns
   `ProofreadingReply::createFinished(result)` or a reply finished by a queued `QMetaObject::invokeMethod`,
   so the "not inside `check()`" rule is observable. Cover:
   - registry: first registered is active, `service()` by id, `setActiveService()` emits
     `activeServiceChanged` once, an unknown id is ignored and keeps the active service;
   - pre-network checks with zero provider calls: empty and whitespace-only text → `EmptyText`;
     no provider → `ServiceUnavailable`; unconfigured → `Auth` with the provider's
     `configurationError`; a remote provider with the switch off → `ServiceUnavailable` with exactly
     `Enable online text checking in Settings → Text checking.`, while a fake service with
     `isLocal() == true` answers with the switch off (the seam for a future offline engine; no such
     provider ships here); `auto` on a provider without auto support and an unknown code →
     `UnsupportedLanguage` mentioning the code; every pre-network reply arrives asynchronously
     (`QSignalSpy` count is 0 right after `check()`);
   - result pass-through: `issues`, `correctedText`, `serviceId` and `ok()` arrive unchanged, and the
     request the fake received keeps the text, the language and the categories;
   - channels: a second `check()` in `MainWindow` finishes the first with `Cancelled` and calls
     `abortImpl()` once, an `InPlace` request started in between is not cancelled; `cancel()` of an
     empty channel is a no-op; `cancelAll()` finishes every open reply;
   - caller-owned replies: deleting the returned reply before it finishes is not a crash and the
     manager does not use it afterwards (the `QPointer` path);
   - hygiene: install a `qInstallMessageHandler` for one success and one failure with `TOPSECRET` in
     the text and assert that no collected line contains the text, an issue message, a replacement or
     a key. Restore the handler in `cleanup()`.
   Add the `ProofreadingSettings` cases to `tests/tst_settings.cpp` (`tst_settings.cpp:78-125` is the
   model): the defaults (provider `languagetool`, language `auto`, empty endpoint and key, all four
   categories, the four flags false), a full round trip including `apiKey` through the test codec,
   the raw INI group name `[proofreading]`, corrupt provider/language/endpoint/categories values
   falling back with the fixed rules, an absent group loading the defaults without writing it,
   `kCurrentVersion` staying `1`, and proofreading changes not touching `translation.*`.
   No new fixture files are needed.

7. **Build, count and verify.** Configure and build Linux Debug and Release with `-Wall -Wextra`
   (`cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release`,
   `cmake --build build/linux-release`), run `tst_proofreadingmanager`, `tst_settings` and
   `tst_textchunker`, then `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`.
   `ctest -N` must show 40 Linux tests (39 + the new platform-neutral target); Windows would be 35
   (34 + 1) and is recorded as not performed (no MSVC). `grep -c '<message' i18n/qtranslate_ru_RU.ts`
   must stay 316: no `.ts`, UI, documentation, README, `doc/handoff.md`, root-plan or later-child file
   is touched. `git diff --stat` may show only `src/core/proofreading*.{h,cpp}`,
   `src/core/textchunker.*`, `src/core/settings.*`, `CMakeLists.txt`,
   `tests/tst_proofreadingmanager.cpp`, `tests/tst_settings.cpp`, `tests/tst_textchunker.cpp` and
   `tests/CMakeLists.txt`. Leave commit and push to the lead.

## Done when

- `src/core/proofreadingtypes.h` defines `ProofreadingRequest`, `ProofreadingIssue` and
  `ProofreadingResult` exactly as `doc/proofreading.md` §4 (plus `serviceId`, `error` and
  `errorMessage` reused from `TranslationError`), with `offset`/`length` documented as UTF-16 code
  units, `categories` documented as "empty = all", and both value types declared as metatypes.
- `src/core/proofreadingservice.{h,cpp}` defines `ProofreadingReply` with the same semantics as
  `TranslationReply` (one `finished()`, never from inside `check()`, `abort()` → `Cancelled` first,
  ignored repeated `finish()`, `createFinished()` queued) and `ProofreadingService` with `id()`,
  `displayName()`, `isConfigured()`, `configurationError()`, `languages()` without `auto`,
  `supportsAutoLanguage()`, `isLocal()`, `applySettings()` and `check()` returning the caller-owned
  reply; the interface includes no settings header and knows no provider.
- `TextChunker::splitWithOffsets()` returns parts that join back to the input, with exact UTF-16
  offsets, and the new `tst_textchunker` cases cover empty text, an exact-limit part, three parts, a
  surrogate pair, `\r\n` and a too-small limit.
- `ProofreadingManager` registers services (first active, ownership taken), exposes
  `onlineServicesAllowed()`/`setOnlineServicesAllowed()`, and `check(request, channel)`:
  - finishes `EmptyText`, `ServiceUnavailable` (no provider), `Auth` (unconfigured),
    `ServiceUnavailable` with `Enable online text checking in Settings → Text checking.` (remote
    provider while the switch is off) and `UnsupportedLanguage` (`auto` unsupported or an unmapped
    code) before any provider call, always through `createFinished()` so `finished()` is asynchronous;
  - aborts the previous unfinished reply of the same channel (`Cancelled`) and leaves the other
    channel alone; `cancel()` and `cancelAll()` work; a caller-deleted reply is safe;
  - keeps `serviceId`, `issues`, `correctedText` and `ok()` intact, and logs only ids, counts, the
    channel, the language/categories and the error kind — never the text or an issue message.
- `ProofreadingSettings` is part of `AppSettings` after `llm`; its INI group is `[proofreading]`
  with `enabled`, `provider`, `language`, `endpoint`, `apiKey` (secret), `categories`,
  `checkBeforeTranslation`, `autoReplace` and `allowOnlineServices`. Defaults are `enabled` false,
  `languagetool`, `auto`, an empty endpoint and key, all four categories, and the three other flags
  `false`.
  Load/save round-trip, the secret goes through the injected codec, `provider` is lowercased and
  validated, `language` falls back to `auto`, `endpoint` is validated as an absolute `http(s)` URL
  without user info/query/fragment (empty allowed) and `categories` is normalized to the known four
  in order (empty or unknown → all). `kCurrentVersion` stays `1`, the group is optional, and no
  translation setting is read or written.
- `proofreadingCategoryIds()` and `proofreadingProviderIds()` are the single source of the id
  strings — the provider list contains only `languagetool` in this stage — and
  `isValidProofreadingEndpoint()`/`normalizedProofreadingEndpoint()` are the single endpoint rule;
  no `proofreading/hotkey` key is added.
- `tst_proofreadingmanager` covers every case in step 6, is platform-neutral and registered in the
  platform-neutral block of `tests/CMakeLists.txt`; `tst_settings` and `tst_textchunker` carry the
  new cases; `ctest -N` shows 40 Linux tests (39 + 1) and would show 35 on Windows (34 + 1).
- Debug and Release build on Linux without warnings with `-Wall -Wextra` and the full suite passes
  40/40; Windows Debug/Release and its 35 tests are recorded as not performed (no MSVC). No `.ts`,
  UI, `doc/`, `AGENTS.md`, README, `doc/handoff.md`, root-plan or later-child file changes; no
  commit.

## Risks

- **UTF-16 vs code points.** The design fixes `offset`/`length` in `QString` units, but LanguageTool
  is a Java server and the real response encoding must be confirmed in `02-languagetool-parser` with
  a fixture that has an emoji before the match. Keep the documented rule here; if the live API counts
  code points, the parser converts and the contract does not change.
- **Reusing `TranslationError`.** `EmptyText`'s comment says "nothing to translate" and
  `ServiceUnavailable` says "blocked or temporarily unavailable"; both read acceptably for a check,
  and reuse keeps one error vocabulary, one `ui/translationerrortext` and one set of translations.
  Do not add a parallel enum and do not rename the existing one.
- **The gate is a new user-visible key.** `proofreading/allowOnlineServices` is not in
  `doc/proofreading.md` §5; it is the stage's privacy mechanism and was approved by the user on
  2026-11-09 (default `false`). Do not reuse `translation/allowFreeServices`, because the stage
  requires checking not to affect translation settings. The `isLocal()` seam keeps a future offline
  engine usable with the switch off; no such provider ships in this stage, so do not add a provider
  id that no registered service can answer.
- **Only LanguageTool ships.** `06-spelling-providers` is `dropped` (user decision of 2026-11-09)
  and stage 6 adds `llm` together with its service. Keep `proofreadingProviderIds()` at
  `{"languagetool"}` and let the UI and the manager treat a missing provider as unavailable instead
  of pre-seeding ids for later stages; a stored value that is not in the list is reset with the fixed
  warning already specified.
- **Empty categories mean all.** The alternative (empty = check nothing) would make a hand-edited
  file silently useless. The parser and the UI must both treat an empty list as all four; the
  manager passes the request through unchanged, so the service applies the rule once.
- **Ownership.** The manager owns the services and the caller owns the reply, exactly as
  `TranslationManager` does. Do not delete the reply in `cancel()`/`cancelAll()`, and keep the
  per-channel `QPointer` so a caller-deleted reply is skipped.
- **No cache, no fallback.** Deliberate for v1: a recheck is a user action and the design asks for
  neither. Do not add a cache keyed on the text (it would store private text in memory) and do not
  add a provider fallback.
- **Windows is not built here.** Record the Windows count and build as not performed, never as
  passed.
