---
kind: stage
status: todo
depends: [01-endpoint-research]
---

# DuckDuckGo as a standalone provider

## Goal

Add DuckDuckGo as a no-key translation provider with its own `TranslationService`, the protocol and
language codes recorded by `../01-endpoint-research`, an id in the fallback list, tests and UI
strings — or drop the stage together with both children when that research finds no key-less,
cookie-free public route. The stage is a candidate: it is worked only after the `viable` verdict.

The work is split into two tasks because the parser and service have their own acceptance (fixtures, a
local `QTcpServer`, chunking, pacing, error mapping) and the settings group with the translated strings
has another, exactly as `03-free-web-endpoints/06-reverso` and `04-yandex-web` are split. DuckDuckGo has
no key and no user-editable endpoint in the stage's scope, so no `[duckduckgo]` INI key is planned; if
the research shows that one is unavoidable, the key and its normalization belong to `02-duckduckgo-ui`.

## What to do, what is touched

- `01-duckduckgo-service` (task): `src/services/duckduckgoparser.*` and
  `src/services/duckduckgoservice.*` on `QueuedWebService`, the `duckduckgo` branch in
  `src/core/languages.cpp` (`serviceCodeOf`, a code table and, when the route reports it,
  `fromServiceCode` through `detectedSourceLang`), registration in `AppController`
  (`createTranslation()`, `applySettings()`, the member), `fallbackServiceIds()` in
  `src/core/settings.cpp`, the new sources in the root `CMakeLists.txt`, fixtures and
  `tests/tst_duckduckgoparser.cpp`/`tests/tst_duckduckgoservice.cpp` with their `tests/CMakeLists.txt`
  entries. Protocol, codes, size limit and pause come only from
  `../01-endpoint-research/notes/duckduckgo-libretranslate.md`; nothing is guessed.
- `02-duckduckgo-ui` (task): a `DuckDuckGo` group on the Services page with a hint that carries the
  unofficial warning, the observed language coverage and whether auto-detect works, and the
  long-text-in-several-requests sentence; the 14 `i18n/*.ts` files through the `update_translations`
  target; a `tst_settingsdialog` case.
- Append the service at the end of the provider order (after `reverso`, and after `libretranslate` if
  that provider is added first) so the existing pickers and the fallback order do not shift.
- `doc/free-endpoints.md`, `doc/architecture.md`, both READMEs, `doc/handoff.md` and the test and
  message counts in `AGENTS.md` are the lead's pass after the code; this stage does not edit them.
- If the verdict is `not viable`, the lead drops this stage and both children, and the research note is
  the recorded reason; no half-implemented service is left in `src/`.

## Done when

- With a `viable` verdict: `DuckDuckGo` appears in the main-window and tray service pickers and in the
  fallback combo; it translates with an explicit source and with auto-detect when the route supports
  it; it fills `detectedSourceLang` when the response carries one; it rejects an unsupported pair with
  `UnsupportedLanguage` before any request; long text is split at the observed limit and the chunks are
  sent sequentially inside one queue slot with the service pause; errors are mapped like the other
  no-key services (`403`/`429` or a rate-limit body → `RateLimited`, an oversized request → `TooLong`,
  `5xx` → `Network`, an HTML page, a cookie challenge or a provider error object →
  `ServiceUnavailable`, malformed or empty → `BadResponse`, timeout/abort → `Timeout`/`Cancelled`); a
  cross-origin redirect is refused; no text, body, cookie or full URL reaches a log line or an error
  message.
- With a `viable` verdict: the parser and service tests pass offline against fixtures and a local
  `QTcpServer`, `tst_languages` covers the code table and `auto`, `tst_settingsdialog` covers the group,
  all 14 `.ts` files carry the new strings, Debug and Release build without warnings on Linux and the
  full suite passes; Windows is reviewed by diff and its tests are recorded as not performed.
- With a `not viable` verdict: this stage and `01-duckduckgo-service`/`02-duckduckgo-ui` are `dropped`
  with the research note as the reason, and `git status` shows no new service file.

## Depends on

`../01-endpoint-research` decides whether the stage runs and supplies the protocol.
`02-duckduckgo-ui` depends on `01-duckduckgo-service`, because the hint has to name the display name and
the observed coverage.

## Risks and what is needed from the user

- The likely outcome is `not viable`. DuckDuckGo's translation is a search/frontend feature; if every
  route needs JavaScript, a `vqd`-style token or a cookie taken from a page, the stage is dropped even
  when the protocol can be reproduced, because the stage rule forbids scraping and cookie replay for a
  new provider.
- The existing `mozhi` provider already reaches a `duckduckgo` engine through a public instance. If that
  is the only working route, the standalone provider adds nothing, and the note must say so; do not
  re-implement Mozhi's engine under a new name.
- The terms of use may forbid automated use. The research records them and the lead tells the user; a
  dropped provider is a normal result and does not need a workaround.
- Live smoke tests may be blocked from this network and are recorded as not performed, never as broken.
