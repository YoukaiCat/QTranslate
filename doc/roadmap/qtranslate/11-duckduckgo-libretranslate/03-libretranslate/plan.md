---
kind: stage
status: todo
depends: [01-endpoint-research, 02-duckduckgo]
---

# LibreTranslate as a standalone provider

## Goal

Add LibreTranslate as a provider that sends text to a LibreTranslate server the user points the
application at, usually a self-hosted one: a base-URL setting, the language codes and auto-detect of
that server, an id in the fallback list, tests and UI strings. The stage is a candidate and starts
only after `../01-endpoint-research` returns `viable`; a `not viable` verdict drops it and both
children with the research note as the reason.

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
  `q`/`source`/`target` body (and `format` only if the research shows it matters), the
  `translatedText` result and the optional `detectedLanguage.language`/`confidence`.
- `02-libretranslate-settings-ui` (task): `LibreTranslateSettings` with the base URL in
  `src/core/settings.h`, the `[libretranslate] baseUrl` key with normalization in
  `src/core/settings.cpp`, `applySettings()` and the cache reset on a change in `AppController`, the
  `LibreTranslate` group on the Services page (base-URL field, effective-value label, invalid-input
  warning, hint), `tst_settings`, a `tst_settingsdialog` case and the 14 `.ts` files.
- Base-URL rules: extract the Mozhi rules into shared helpers instead of copying them —
  `isValidServiceBaseUrl()`/`normalizedServiceBaseUrl()` (absolute `http(s)`, a host, no credentials,
  no query, no fragment, a path prefix kept, a trailing slash removed) — and keep
  `isValidMozhiInstance()`/`normalizedMozhiInstance()` as thin wrappers so the Mozhi settings UI and its
  tests stay unchanged.
- Default base URL: when the research finds a public instance that answers without a key, default to
  it with the Mozhi-style unofficial and self-hosting warning and no hidden rotation; otherwise the
  value starts empty, `isConfigured()` returns false and `configurationError()` tells the user to enter
  the base URL of their server. The value is always user-editable, and no request goes to an instance
  the user did not configure.
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
  URL out of logs and messages.
- Without a base URL (when the research finds no public default) the service reports the configured
  error instead of sending a request, and the settings page says what to enter.
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

- A hosted instance may require an API key (`libretranslate.com` does). That path is outside the
  key-less rule and this stage. If the research finds no key-less instance and the user still wants the
  provider, an `api_key` field is a separate user decision — a stored secret, secret storage and a key
  protection path. Ask; do not add it silently.
- The value of the provider depends on the user running a server. Quality is Argos-level and lower than
  Google/DeepL, and the language matrix of an instance is whatever models it has installed. The hint
  must say that the base URL points at a server the user trusts and that its terms and availability are
  the user's responsibility.
- Request and rate limits are server configuration (`--req-limit`, `--api-keys`), not a published
  public quota. The pause and the chunk size stay named conservative assumptions unless the research
  measured them.
- Live smoke tests need a reachable server. Without one they are recorded as not performed, and the
  fixtures plus the local `QTcpServer` stay the automated gate.
