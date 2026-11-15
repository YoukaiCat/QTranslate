---
kind: stage
status: done
base: 3f393df
depends: [01-endpoint-research, 02-duckduckgo]
---

# LibreTranslate as a standalone provider

## Goal

Add LibreTranslate as a provider that sends text to a LibreTranslate server the user points the
application at: a base-URL setting and an optional API key, the language codes and auto-detect of that
server, an id in the fallback list, tests and UI strings. User decision 2026-11-15: a self-hosted
server works without a key, and a hosted instance such as `libretranslate.com` works with a key, so
the key is an optional stored secret. The research found no public key-less instance; the default
base URL stays empty.

LibreTranslate is an AGPL-3.0 server. QTranslate stays GPL-3.0-or-later and adds only an HTTP client,
no LibreTranslate code, no server binary and no container. The stage does not start or bundle a
server: the user runs one (Docker, pip or the project's own image) and enters its base URL, as
`doc/local-translation.md` §2 and §7 already record.

The work is split into two tasks because the parser and service have their own acceptance (fixtures, a
local `QTcpServer`, chunking, pacing, error mapping) and the base-URL setting, the shared URL helpers
and the translated strings have another, like `03-free-web-endpoints/03-mozhi`.

## What to do, what is touched

- `01-libretranslate-service` (task): `src/services/libretranslateparser.*` and
  `src/services/libretranslateservice.*` on `QueuedWebService`, the `libretranslate` branch and code
  table in `src/core/languages.cpp`, registration in `AppController`, `fallbackServiceIds()`, the root
  `CMakeLists.txt`, fixtures and `tests/tst_libretranslateparser.cpp`/
  `tests/tst_libretranslateservice.cpp`. Protocol from
  `../01-endpoint-research/notes/duckduckgo-libretranslate.md`: `POST <base>/translate` with a JSON
  `q`/`source`/`target` body, `api_key` when a key is set, the `translatedText` result and the
  optional `detectedLanguage.language`/`confidence`; `setApiKey()` alongside `setBaseUrl()`, and a
  `401`/`403` key error → `Auth`.
- `02-libretranslate-settings-ui` (task): `LibreTranslateSettings` with the base URL and the API key
  in `src/core/settings.h`, the `[libretranslate] baseUrl` and `apiKey` keys with normalization in
  `src/core/settings.cpp`, `applySettings()` and the cache reset on a change in `AppController`, the
  `LibreTranslate` group on the Services page (base-URL field, key field with password echo,
  effective-value label, invalid-input warning, hint), `tst_settings`, a `tst_settingsdialog` case and
  the 14 `.ts` files.
- The key is optional and is stored as a secret: DPAPI on Windows, libsecret or the `0600` fallback on
  Linux, through the same `SecretProtector` path as the other provider keys. An empty key is valid and
  omits `api_key`; the key never appears in the INI plaintext when a protector is available, in a log
  line or in an error message.
- Base-URL rules: extract the Mozhi rules into shared helpers instead of copying them —
  `isValidServiceBaseUrl()`/`normalizedServiceBaseUrl()` (absolute `http(s)`, a host, no credentials,
  no query, no fragment, a path prefix kept, a trailing slash removed) — and keep
  `isValidMozhiInstance()`/`normalizedMozhiInstance()` as thin wrappers so the Mozhi settings UI and its
  tests stay unchanged.
- Default base URL: empty, because no public key-less instance answered. `isConfigured()` returns false
  and `configurationError()` tells the user to enter the base URL of their server. The user can enter
  a self-hosted URL without a key or a hosted URL with a key; no request goes to an instance the user
  did not configure.
- Append `libretranslate` at the end of the provider order (after `duckduckgo` when that provider is
  added) so the existing pickers and the fallback order do not shift.
- `doc/`, both READMEs, `doc/handoff.md` and the counts in `AGENTS.md` are the lead's pass after the
  code.

## Done when

- `LibreTranslate` appears in the service pickers and in the fallback combo. With a base URL set it
  translates against a local `QTcpServer` that serves the recorded fixtures, sends the configured
  source (or `auto` when the server supports detection) and the target codes of the observed table,
  fills `detectedSourceLang` from `detectedLanguage.language`, chunks long text at the observed limit,
  paces requests, maps errors like the other no-key services and keeps the text, the body and the full
  URL out of logs and messages. With a key set it sends `api_key` in the body; without a key it omits
  it; a server that rejects the key reports `Auth` with a message that names the key, not the text.
- Without a base URL the service reports the configured error instead of sending a request, and the
  settings page says what to enter.
- The base URL is validated and normalized by the shared helpers; a cross-origin redirect is refused;
  a change of the base URL clears the translation cache and does not abort an active reply.
- Parser, service, settings, languages and dialog tests pass offline; the 14 `.ts` files carry the new
  strings; Debug and Release build without warnings on Linux and the full suite passes; Windows is
  reviewed by diff and recorded as not performed.
- The repository and the packages gain no AGPL code, no server binary and no container.

## Depends on

`../01-endpoint-research` for the verdict and the protocol. `../02-duckduckgo` is an ordering
dependency only: the second provider starts after the first is accepted. If `02-duckduckgo` is dropped,
the lead removes it from this list and this stage follows `01-endpoint-research` directly.
`02-libretranslate-settings-ui` depends on `01-libretranslate-service`.

## Risks and what is needed from the user

- The key is optional and stored as a secret (user decision 2026-11-15). A hosted instance's account,
  quota, terms and availability are the user's responsibility; the key is sent only to the server the
  user configured and never logged. If the server rejects the key, the error names the key, not the
  text.
- The value of the provider depends on the user running a server. Quality is Argos-level and lower than
  Google/DeepL, and the language matrix of an instance is whatever models it has installed. The hint
  must say that the base URL points at a server the user trusts and that its terms and availability are
  the user's responsibility.
- Request and rate limits are server configuration (`--req-limit`, `--api-keys`), not a published
  public quota. The pause and the chunk size stay named conservative assumptions unless the research
  measured them.
- Live smoke tests need a reachable server. Without one they are recorded as not performed, and the
  fixtures plus the local `QTcpServer` stay the automated gate.

## Outcome

Both children are done: `01-libretranslate-service` (`49a37a0`) and
`02-libretranslate-settings-ui` (`b047766`), plus the acceptance comment fix `df69d70`. The service
posts to `<base>/translate` with `q`/`source`/`target` and an optional `api_key`, sends
`Accept-Language: en` so error bodies stay English and `Auth` is classified on every system locale,
maps 51 language codes with `fil`→`tl` and `pt-PT`→`pt`, uses a 5000-unit chunk and a 500 ms pause as
named assumptions, and keeps the base URL empty by default. The settings store
`[libretranslate] baseUrl` and `apiKey` (a secret through `SecretProtector`), extract the shared
service-base-URL helpers from Mozhi, and show the group last on the Services page with the URL, the
key, the effective URL, a warning and one hint. Verification: host Debug/Release and trixie Release
50/50, 0 warnings; `.ts` 382 with 0 unfinished or vanished; `tst_settingsdialog` 36, `tst_settings`
84, `tst_libretranslateparser` 31, `tst_libretranslateservice` 40; Windows not performed. The live
smoke against a real server was not performed; the tests use fixtures and a local `QTcpServer`. Known
gap: `LibreTranslateService::isConfigured()` duplicates the URL predicate and does not reject
userinfo, a query or a fragment on a direct `setBaseUrl()`; the settings path normalizes, so no user
path reaches it.
