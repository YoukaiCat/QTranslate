---
kind: task
status: todo
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

## Done when

- `MozhiSettings` and `AppSettings::mozhi` exist with the documented defaults; `[mozhi] instance` and
  `[mozhi] engine` are loaded and saved; the format version stays `1` and an old file without the
  group loads with the defaults.
- An empty, malformed, non-http(s), credential-bearing, query-bearing or fragment-bearing instance is
  normalized to `https://mozhi.adminforge.de` with a fixed `qCWarning` that does not echo the typed
  value; a valid URL keeps its path prefix and loses a trailing slash; the canonical value is what
  the settings return and what is written to the INI.
- An empty or unknown engine is normalized to `google` with a warning; the seven accepted ids are
  exposed in the documented order through `mozhiEngineIds()`; a case-insensitive id becomes the
  canonical lower-case value.
- `MozhiService::applySettings()` uses the normalized instance and engine; `AppController` clears the
  translation cache when either changes and does not abort an active request; no UI or translation
  change is part of this task.
- `tst_settings` covers the defaults, a round trip with a local-server instance and a non-default
  engine, the invalid-instance rows, the engine rows and an old file without `[mozhi]`;
  `tst_mozhiwebservice` proves that `applySettings()` changes the request path and the `engine`
  query.
- Debug and Release build without warnings on Linux; the full suite passes 33/33; Windows
  Debug/Release and its 28 tests are recorded as not performed (no MSVC); the settings group,
  translations, `doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md`, README, `doc/handoff.md`,
  the root plan and `07-fallback-chain` are not touched here.

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
