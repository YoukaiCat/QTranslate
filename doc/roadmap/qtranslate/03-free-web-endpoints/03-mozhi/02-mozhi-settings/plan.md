---
kind: task
status: done
detailed: true
depends: [01-mozhi-service]
---

# Mozhi instance and engine settings

## Task

Add the `[mozhi] instance` and `[mozhi] engine` settings, their defaults and normalization, and
connect them to `MozhiService` without changing the UI. This task owns the INI format change and the
settings tests; the service and parser are `01-mozhi-service`, and the settings group with
translations and documentation is `03-mozhi-ui-and-docs`.

`AppSettings` gets a `MozhiSettings` value with `instance = "https://mozhi.adminforge.de"` and
`engine = "google"`. `SettingsStore` loads and saves `[mozhi] instance` and `[mozhi] engine`.
`normalize()` replaces an empty, malformed, non-http(s), credential-bearing, query-bearing or
fragment-bearing instance with the default and logs a warning; a valid path prefix is kept and a
trailing slash is removed. It replaces an unknown engine with `google` and logs a warning. The engine
ids are `google`, `deepl`, `duckduckgo`, `libretranslate`, `mymemory`, `reverso`, `yandex`, in that
order; the comparison is case-insensitive and the stored value is the canonical lower-case id. The
settings format version stays 1: the new keys have defaults and an old file without `[mozhi]` loads
with the defaults, so no migration and no version bump are needed. Do not rewrite the file just
because the keys are absent.

The same pure helpers are shared with the UI in `03`, so the dialog does not duplicate the rules:
`mozhiEngineIds()`, `isValidMozhiInstance()`, `normalizedMozhiInstance()` and
`normalizedMozhiEngine()` in `src/core/settings.h/.cpp`. `isValidMozhiInstance()` accepts only a
non-empty `http`/`https` URL with a host, no user-info, no query and no fragment; a path is allowed.
`normalizedMozhiInstance()` returns the canonical `QUrl::toString(QUrl::FullyEncoded)` form with the
trailing slash removed, or the default. The helpers themselves do not log; `SettingsStore::normalize()`
logs a fixed warning without echoing the typed value. `MozhiService::applySettings()` reads the two
values and calls `setInstance()`/`setEngine()`; `MozhiService::defaultInstance()`/`defaultEngine()`
delegate to `MozhiSettings{}` so the default literal exists only once. `AppController` clears the
translation cache when either value changes, because a cached translation from the old instance or
engine must not be reused. An in-flight request finishes on the old instance; it is not aborted.

`tst_settings` covers the defaults, the round trip, the instance normalization rows, the engine
normalization rows and an existing old file without `[mozhi]`. `tst_mozhiwebservice` adds a case that
applies an `AppSettings` with a local-server instance and a non-default engine and verifies the
request path and `engine` query. No new test target is added, so the counts stay 33 Linux / 28
Windows.

## Steps

1. **Add `MozhiSettings` and the shared helpers to `src/core/settings.h/.cpp`.** In
   `src/core/settings.h`, add `<QStringList>`, a `MozhiSettings` struct after `DeepLSettings` with
   `QString instance = QStringLiteral("https://mozhi.adminforge.de");` and
   `QString engine = QStringLiteral("google");`, and `MozhiSettings mozhi;` to `AppSettings` after
   `deepl`. Keep `kCurrentVersion` at `1`. Declare the pure helpers after `AppSettings`:

   ```cpp
   QStringList mozhiEngineIds();
   bool isValidMozhiInstance(const QString &instance);
   QString normalizedMozhiInstance(const QString &instance);
   QString normalizedMozhiEngine(const QString &engine);
   ```

   Implement them in `src/core/settings.cpp` (add `<QUrl>`), with the default literal only in
   `MozhiSettings`:
   - `mozhiEngineIds()` returns `google`, `deepl`, `duckduckgo`, `libretranslate`, `mymemory`,
     `reverso`, `yandex`, in that order. The UI task `03` uses the same list for its combo; the
     display labels stay in the dialog because they are product names, not translated strings.
   - `isValidMozhiInstance()` trims the text and parses it with `QUrl(text, QUrl::StrictMode)`.
     It requires `isValid()`, scheme `http` or `https`, a non-empty `host()`, an empty
     `userInfo()`, `!hasQuery()` and `!hasFragment()`. A port and a path are allowed. Do not use
     `QUrl::fromUserInput()`: a bare `host`, malformed text, `ftp://...` and `file://...` must
     fall back to the default rather than be guessed into an http URL.
   - `normalizedMozhiInstance()` returns `MozhiSettings{}.instance` when the input is invalid.
     Otherwise it returns `QUrl::toString(QUrl::FullyEncoded)` with every trailing `/` chopped:
     `https://host/` becomes `https://host`, `https://host/mozhi/` becomes
     `https://host/mozhi`, and a non-empty path prefix is kept. Surrounding whitespace is trimmed
     as part of the canonical form.
   - `normalizedMozhiEngine()` trims and lowercases the input, returns it when it is in
     `mozhiEngineIds()`, and otherwise returns `MozhiSettings{}.engine`; empty and unknown ids
     therefore become `google`.
   - The helpers are pure: no logging, no `tr()`, no settings access. `SettingsStore` owns the
     warning, so the dialog can call the same helpers without producing log noise.

2. **Read, normalize and save the `[mozhi]` group in `SettingsStore`.** In `src/core/settings.cpp`:
   - `load()`: after the `deepl` group and before `llm`, read
     `loaded.mozhi.instance = settings.value("instance", loaded.mozhi.instance).toString();` and
     the same for `engine`. The second argument is the `MozhiSettings` default, so an absent group
     loads as the defaults and must not log. Do not call `save()` from `load()`: loading an old
     file leaves the file untouched.
   - `save()`: after the `deepl` group, write `out.beginGroup("mozhi")`,
     `out.setValue("instance", normalized.mozhi.instance)`,
     `out.setValue("engine", normalized.mozhi.engine)`, `out.endGroup()`. `save()` already calls
     `normalize()` on its copy, so the INI receives the canonical value, not the raw one.
   - `normalize()`: after the Google/DeepL value checks, replace an invalid instance and warn with
     the exact fixed literal:

     ```cpp
     if (!isValidMozhiInstance(settings.mozhi.instance)) {
         qCWarning(lcSettings) << "Mozhi instance is not a base http(s) URL; using the default";
         settings.mozhi.instance = defaults.mozhi.instance;
     } else {
         settings.mozhi.instance = normalizedMozhiInstance(settings.mozhi.instance);
     }
     ```

     A trailing slash, a case-only scheme/host change or surrounding whitespace is a silent
     canonicalization, not a warning. For the engine, lower-case the trimmed value for the
     membership check; if it is not in `mozhiEngineIds()`, log exactly
     `"Mozhi engine is not supported; using the default"` and assign `defaults.mozhi.engine`;
     otherwise assign `normalizedMozhiEngine(settings.mozhi.engine)`. An absent key already loads
     as `google` and does not warn; an explicitly empty value does warn.
   - Both warnings are fixed literals: no `%1`, no `qPrintable` of the input, no URL, user name or
     password. Keep `AppSettings::kCurrentVersion` at `1` and the `settingsVersion` check
     unchanged: a new group with defaults needs no migration, and bumping the version would make
     older builds reject the file.

3. **Apply the settings in `MozhiService`.** In `src/services/mozhiservice.cpp`, include
   `core/settings.h` and replace the empty `applySettings()`:

   ```cpp
   void MozhiService::applySettings(const AppSettings &settings)
   {
       setInstance(QUrl(normalizedMozhiInstance(settings.mozhi.instance)));
       setEngine(normalizedMozhiEngine(settings.mozhi.engine));
   }
   ```

   The store has already normalized the app path; normalizing again keeps one rule set when the
   service is given a hand-built `AppSettings` (as the tests do) and prevents an invalid URL from
   ever reaching a request. Change `defaultInstance()`/`defaultEngine()` to
   `return QUrl(MozhiSettings{}.instance);` and `return MozhiSettings{}.engine;`, so the literal
   exists only in `MozhiSettings`. Leave `setInstance()` (empty/invalid URL fallback),
   `setEngine()` (trim and empty fallback), `endpointUrl()`, `translate()` and the parser
   unchanged. `applySettings()` only writes the two members: the queue and the active reply are
   untouched, and a reply already constructed keeps the endpoint/engine it copied, so it is not
   aborted.

4. **Clear the translation cache on an effective change in `AppController`.** In
   `src/app/appcontroller.cpp::applySettings()`, after the existing
   `if (m_mozhi) m_mozhi->applySettings(newSettings);` (around line 605), add:

   ```cpp
   if (oldSettings.mozhi.instance != newSettings.mozhi.instance
       || oldSettings.mozhi.engine != newSettings.mozhi.engine) {
       m_translations->clearCache();
   }
   ```

   `SettingsStore` emits normalized old/new values, so a trailing slash or a case-only edit that
   normalizes to the same value does not drop the cache, while a real instance/engine change does.
   Do not call `cancel()`/`cancelAll()`: an active request belongs to the old instance and finishes
   there. Do not call `updateServiceSelectors()`: `Mozhi` has a constant `displayName()` and the
   language lists do not depend on the engine. The constructor already calls
   `m_mozhi->applySettings(settings)` (lines 232-237), so no startup wiring is added. There is no
   `AppController` test target here (tests link only `qtranslate_core`), so this two-line wiring is
   checked by inspection, like the existing LLM reset at lines 608-612; the outcome must not claim
   it is covered by a test.

5. **Extend `tests/tst_settings.cpp` (existing target, no CMake change).**
   - `defaults()`: assert `settings.mozhi.instance == "https://mozhi.adminforge.de"`,
     `settings.mozhi.engine == "google"` and `mozhiEngineIds()` equals the seven documented ids in
     order. The existing `settingsVersion` assertion stays.
   - Add `mozhiInstanceIsNormalized_data()`/`mozhiInstanceIsNormalized()` with `input`, `expected`
     and `valid` columns: the default row; empty and whitespace; `mozhi.adminforge.de` (no
     scheme); `ftp://host`; `https://` (no host); `http://[::1` (malformed); `https://host:bad`;
     `https://user:pass@host`; `https://user@host`; `https://host/?engine=google`;
     `https://host/#fragment`; `http://127.0.0.1:8080/mozhi/`; `https://host/Mozhi/`;
     `  https://host/mozhi  `. Assert `isValidMozhiInstance(input)` against
     `valid` and `normalizedMozhiInstance(input)` against `expected`; invalid rows expect the
     default, valid rows the canonical form with no trailing slash and the path prefix kept.
   - Add `mozhiEngineIsNormalized_data()`/`mozhiEngineIsNormalized()`: all seven ids,
     `DEEPL` becomes `deepl`, `  Google ` becomes `google`, empty becomes `google`, whitespace
     becomes `google`, `unknown` becomes `google` and `auto` becomes `google`.
   - `roundTrip()`: set `settings.mozhi.instance = "http://127.0.0.1:8080/mozhi/"` (trailing
     slash) and `settings.mozhi.engine = "deepl"`, then assert the saved and loaded value is
     `http://127.0.0.1:8080/mozhi` and `deepl`. Read the INI with a fresh `QSettings` and assert
     `mozhi/instance` and `mozhi/engine` contain the canonical values, so the canonical form is
     what is written, not only what is loaded.
   - `corruptValuesAreNormalized()`: add a raw
     `mozhi/instance = https://user:secret@example.invalid/?engine=google#fragment` and
     `mozhi/engine = unknown-engine`; after `load()` both are the defaults. This exercises the
     `SettingsStore` warning path; the input must not appear in the log, which is a code-review
     check because the code uses fixed literals.
   - Add `mozhiGroupIsOptional()`: write a version-1 INI with an `[app]` value and no `[mozhi]`;
     load; assert `settingsVersion == 1`, the two defaults, and that the file bytes are unchanged
     (no load-time rewrite).

6. **Extend `tests/tst_mozhiwebservice.cpp` (existing target, no CMake change).** Add
   `#include "core/settings.h"`.
   - `appliesSettings()`: set the handler to `echoResponse`; build `AppSettings settings;` with
     `settings.mozhi.instance = "http://127.0.0.1:<port>/mozhi"` and
     `settings.mozhi.engine = "deepl"`; call `m_service->applySettings(settings)`; assert
     `instance()` and `engine()`, then translate `Hello` from `en` to `ru`; wait for `finished()`;
     assert the result is successful and the request path is `/mozhi/api/translate` with
     `engine=deepl`. Then call `applySettings()` with an invalid `AppSettings`
     (`ftp://example.invalid/x`, `unknown`) and assert it falls back to
     `MozhiService::defaultInstance()` and `"google"`; do not translate after that fallback, so
     the test never contacts the real instance.
   - `applySettingsDoesNotAbortActiveRequest()`: make the handler return
     `FakeHttpServer::Response{200, {}, false}`; start a translation; wait until
     `m_server->requests.size() == 1` with `QTRY_COMPARE`; check the sent request still has
     `/api/translate` and `engine=google`; call `applySettings()` with a changed instance prefix
     and `engine=deepl`; `QVERIFY(!reply->isFinished())`; `reply->abort()`; compare the result
     with `Cancelled`. This proves the settings change does not cancel the reply and the sent
     request keeps its copied endpoint/engine.
   - Leave `init()`, `cleanup()`, `properties()` and `defaults()` as they are; they must keep
     passing with the `MozhiSettings` defaults. No new test target, so the counts stay 33 Linux /
     28 Windows.

7. **Build and verify on Linux; record Windows honestly.** Configure and build Debug and Release
   with the manual CMake flow from `AGENTS.md` (`build/linux-debug`, `build/linux-release`,
   `-Wall -Wextra`), with zero `warning:` lines. Run `tst_settings` and `tst_mozhiwebservice` in
   both, then `ctest -N` (33 tests) and the full
   `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure` (33/33), and the Debug
   suite as well. Run `tst_mozhiwebservice` a few times to catch timing flakes, although this task
   adds no new timing. Check `git diff --stat` that only `src/core/settings.*`,
   `src/services/mozhiservice.cpp`, `src/app/appcontroller.cpp`, `tests/tst_settings.cpp` and
   `tests/tst_mozhiwebservice.cpp` changed, and that no `SettingsDialog`, `.ts`, documentation,
   README, `doc/handoff.md`, root plan or `07-fallback-chain` file is touched. Windows
   Debug/Release and its 28 tests are recorded as not performed (no MSVC); do not commit.

## Done when

- `MozhiSettings` exists with `instance = "https://mozhi.adminforge.de"` and
  `engine = "google"`, `AppSettings::mozhi` is part of the aggregate, and
  `mozhiEngineIds()`, `isValidMozhiInstance()`, `normalizedMozhiInstance()` and
  `normalizedMozhiEngine()` are declared in `src/core/settings.h` and implemented in
  `src/core/settings.cpp`; the default literal lives only in `MozhiSettings`.
- `SettingsStore` loads and saves `[mozhi] instance` and `[mozhi] engine`; a file without the group
  loads the defaults, logs no warning and is not rewritten; `settingsVersion` stays `1`; a save
  writes the canonical instance and engine, so a trailing slash and a mixed-case engine do not
  survive a round trip.
- Every invalid instance row (empty, whitespace, bare host, malformed, non-http(s), user info,
  query, fragment) becomes `https://mozhi.adminforge.de` with the fixed `qCWarning(lcSettings)`
  that contains no part of the input; a valid `http`/`https` URL keeps its path prefix and port and
  loses every trailing `/`; surrounding whitespace is trimmed.
- The seven engine ids are exposed in the documented order; an empty or unknown engine becomes
  `google` with a fixed warning; `DEEPL`, `  Google ` and the canonical lowercase ids are accepted
  and stored as the canonical lowercase id.
- `MozhiService::applySettings()` applies the normalized instance and engine;
  `defaultInstance()`/`defaultEngine()` delegate to `MozhiSettings`; an active or queued reply is
  not aborted and keeps the endpoint/engine it captured.
- `AppController` calls `TranslationManager::clearCache()` when the effective instance or engine
  changes, does not cancel the active reply, and does not clear the cache for a change that
  normalizes away; this is verified by inspection because no `AppController` test target exists,
  and the outcome records that instead of claiming a test.
- `tst_settings` covers the defaults, the seven ids, the instance and engine normalization rows,
  the round trip with canonical INI values, the corrupt-value fallback and an old file without
  `[mozhi]`; `tst_mozhiwebservice` covers `applySettings()` with a local `QTcpServer` (path and
  `engine` query) and the non-abort behaviour.
- No new test target is added: `ctest -N` is 33 on Linux and would be 28 on Windows; Debug and
  Release build without warnings and pass 33/33 on Linux; Windows Debug/Release and its 28 tests
  are recorded as not performed (no MSVC).
- `SettingsDialog`, the `.ts` files, `doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md`,
  `README.md`, `README.ru.md`, `doc/handoff.md`, the root plan and `07-fallback-chain` are not
  touched; no commit is made.

## Risks

- The instance URL is user input. Do not log the typed value (it can contain credentials); log only
  the fixed warning and the effective default. Refuse user-info, query and fragment instead of trying
  to repair them, and never send the text to a URL the user did not confirm.
- The default instance is `https://mozhi.adminforge.de`; the UI and the service must use one source
  of truth. Put the literal in `MozhiSettings` and let `normalizedMozhiInstance()`/the dialog use the
  same value; do not duplicate the default in `settingsdialog.cpp` or `mozhiservice.cpp`.
- `settingsVersion` must stay `1`. A new group with defaults does not need a migration; bumping the
  version would make older builds reject the file and is not justified. Test the old-file case.
- Clearing the cache on an instance/engine change is required; otherwise the same text returns the
  old engine's translation. Do not abort an active request, because the user may be waiting for it
  and a partial abort adds no value.
- Engine ids are case-sensitive on the wire; normalize to the canonical lower-case form, and do not
  silently accept an arbitrary string (a typo would become an "Engine does not exist" error at
  runtime).
- Windows Debug/Release and its 28 tests are unavailable on this Linux machine; record them as not
  performed, not as passed.
- `QUrl` is lenient: use `StrictMode` and explicit scheme/host/user-info/query/fragment checks,
  never `fromUserInput()`. A validator that "fixes" a bare host or drops a query would send text
  somewhere the user did not confirm.
- There is no `AppController` test target. The cache reset is a two-line wiring change that mirrors
  the LLM block; do not claim `tst_settings` or `tst_mozhiwebservice` covers it. The service-level
  behaviour (values applied, active reply not aborted) is covered in `tst_mozhiwebservice`.

## Outcome

Done on 2026-11-09. `MozhiSettings` lives in `src/core/settings.h` after `DeepLSettings`, is part of
`AppSettings` after `deepl`, and is the only place that holds the default instance and engine
literals. `mozhiEngineIds()`, `isValidMozhiInstance()`, `normalizedMozhiInstance()` and
`normalizedMozhiEngine()` are pure, do not log and are ready for the `03` dialog. The helpers use
`QUrl(..., QUrl::StrictMode)`, require `http`/`https` with a host, no user info, no query and no
fragment, keep a port and a path prefix, and remove every trailing slash; `fromUserInput()` is not
used. `kCurrentVersion` stays `1`.

`SettingsStore` loads and saves `[mozhi] instance` and `[mozhi] engine`; an absent group loads the
defaults without a warning and `load()` does not rewrite the file, an explicitly empty or malformed
value falls back with the fixed warnings `Mozhi instance is not a base http(s) URL; using the
default` / `Mozhi engine is not supported; using the default`, without echoing the typed value. A
trailing slash, scheme/host case or surrounding whitespace is a silent canonicalization, and
`save()` writes the canonical values. `MozhiService::applySettings()` applies the normalized values
(so a hand-built `AppSettings` cannot send an invalid URL) and `defaultInstance()`/`defaultEngine()`
delegate to `MozhiSettings`. `AppController` clears the translation cache when the effective
instance or engine changes, without cancelling the active reply; this wiring has no test target and
was checked by inspection only.

`tst_settings` covers the defaults, the seven engine ids in order, the instance and engine
normalization rows, a round trip that checks the raw INI for canonical values, the corrupt-value
fallback and an old version-1 file without `[mozhi]` whose bytes do not change. `tst_mozhiwebservice`
covers `applySettings()` with a local server (path prefix plus `engine=deepl`), the invalid-value
fallback that never contacts the real instance, and the non-abort behaviour where the in-flight
reply keeps its captured endpoint and engine. No new test target: 33 Linux / 28 Windows.

Verification: Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2) and `qtranslate-trixie:latest` (Qt 6.8.2,
GCC 14.2), Debug and Release: zero `warning:` lines, targeted `tst_settings` and
`tst_mozhiwebservice` passed, `ctest -N` is 33, the full suite is 33/33 in all four configurations,
and `tst_mozhiwebservice` passed 5/5 repeats per configuration. The trixie runs needed read-only
`/etc/passwd` and `/etc/group` mounts from the host for `dbus-daemon` under `--user`; that is an
environment workaround, not repository content. Windows Debug/Release and its 28 tests were not
performed (no MSVC).

Review found one real issue that was not fixed here: a reply started before the settings change can
finish after `AppController::clearCache()` and repopulate the cache, because
`TranslationManager::cacheKey()` does not include the instance or engine and
`Reply::finishResult()` stores a successful result unconditionally. The reported scenario is a
translation still in flight while the user changes the instance or engine; the old result then
becomes the cached result for the same service and text. A truthful fix needs a cache-generation
guard in `TranslationManager` (increment in `clearCache()`, remember it in `Reply`, skip
`storeCached()` for an older generation) and a `tst_translationmanager` case, which is outside this
task's declared six-file boundary and conflicts with the planned "do not abort the active request"
behaviour. It is recorded as deferred, not fixed. The same pre-existing gap exists for
`google.mode`, `deepl.mode` and `deepl.englishVariant`, which are not part of the cache key either;
that predates this task and was left untouched. The SettingsDialog, `.ts` files,
`doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md`, both READMEs, `doc/handoff.md`, the root
plan and `07-fallback-chain` were not touched; a live smoke against the default instance was not
performed.
