---
kind: stage
status: in-progress
base: c4d3eed
depends: [01-free-web-infrastructure]
---

# Mozhi service

## Goal

Add Mozhi as an opt-in free service: one integration gives access to several translation engines
through a public or self-hosted instance, and the user can point it at an instance they trust. This is
the second service required by the stage (`MyMemory and Mozhi work with rate limits`) and a natural
second fallback after MyMemory. The work is split into three tasks so the service, the settings
storage and the UI/documentation each have their own acceptance.

## What to do, what is touched

- `01-mozhi-service` (task, detailed): the pure `MozhiParser`, `MozhiService`, the `mozhi` language
  branch in `languages.cpp`, registration in `AppController`, fixtures and the new
  `tst_mozhiparser`/`tst_mozhiwebservice`. No `[mozhi]` settings keys, UI, translations or docs.
- `02-mozhi-settings` (task): `MozhiSettings` in `AppSettings`, the `[mozhi] instance` and
  `[mozhi] engine` keys in `SettingsStore` with normalization, `MozhiService::applySettings` and the
  cache reset on a change; `tst_settings` and a settings-path case in `tst_mozhiwebservice`. No new
  UI or docs.
- `03-mozhi-ui-and-docs` (task): the `Mozhi` group in `SettingsDialog` (instance field, engine combo,
  effective value, warning), `tst_settingsdialog`, the 14 `.ts` files, `doc/architecture.md`,
  `doc/free-endpoints.md` and the `AGENTS.md` test counts. No service behaviour change.

Decisions carried through all children:

- The default instance is `https://mozhi.adminforge.de` and the default engine is `google`. The
  instance URL is editable and there is no hidden instance rotation: the same text must not be sent
  to a different third party behind the user's back.
- The engine combo is Google, DeepL, DuckDuckGo, LibreTranslate, MyMemory, Reverso, Yandex with the
  lower-case ids `google`, `deepl`, `duckduckgo`, `libretranslate`, `mymemory`, `reverso`, `yandex`.
  A live instance can disable an engine; "Engine does not exist or has been disabled." becomes
  `ServiceUnavailable` with a clear message.
- The source may be `auto`, because the default `google` engine accepts it; if the instance rejects
  the pair, the reply is `UnsupportedLanguage` with a clear message, never a guessed language. The
  language codes are the Google-style codes the instance expects: `zh-Hans`→`zh-CN`,
  `zh-Hant`→`zh-TW`, `he`→`iw`, `jv`→`jw`, `nb`→`no`, `fil`→`tl`, `pt-BR`→`pt`; this is the Google
  column reused, with one `mozhiCodeOf()` function as the single fix point instead of a
  `LanguageInfo` column. A protocol check on 2026-11-09 confirmed the public instance accepts `iw`,
  `jw`, `tl`, `no`, `zh-CN`, `zh-TW` and `pt` and rejects the internal forms, so no separate
  override is needed today.
- Text is split with `TextChunker::split` at 5000 UTF-16 code units and the chunks are sent
  sequentially inside one reply; the edge whitespace is restored. The 500 ms pause between requests
  to the configured host is a conservative assumption because the doc gives no Mozhi-specific value;
  it stays a named constant, and the docs call it an assumption. `QueuedWebService` still guarantees
  one active request per host.
- Errors: HTTP 429/403 and a rate-limit body become `RateLimited`; HTTP 5xx without a recognised body
  becomes `Network`; an HTML page, a disabled engine or a JSON error becomes
  `ServiceUnavailable`; a missing/empty `translated-text` or malformed JSON becomes `BadResponse`;
  an invalid-language text becomes `UnsupportedLanguage`; HTTP 413/414 becomes `TooLong`; timeout and
  `abort()` produce `Timeout`/`Cancelled`. The request URL contains the text, so messages and logs
  never contain the text, the full URL or the body.
- Requests use a same-origin redirect policy: a redirect to another host is refused, not followed,
  because it would send the text to a different third party.
- Tests use fixtures and a local `QTcpServer` pointed to by the instance setting; CTest never
  contacts a real instance. A manual live smoke against the default instance is optional and
  recorded honestly.
- `README.md`, `README.ru.md`, `doc/handoff.md`, the root plan and `07-fallback-chain` are not edited
  here; the final provider/fallback pass belongs to `07-fallback-chain`/the lead, as with MyMemory.

## Done when

- `01-mozhi-service` is done: `MozhiParser` and `MozhiService` exist, `mozhi` is registered behind
  the gate with the default instance and engine, and the parser/service tests cover the documented
  fields, errors, `auto`, 5000-unit chunks, 500 ms pacing and serialization.
- `02-mozhi-settings` is done: `[mozhi] instance` and `[mozhi] engine` are loaded, saved and
  normalized with the documented defaults and warnings; `tst_settings` covers the defaults, the round
  trip and the invalid values; `MozhiService` uses the settings and the cache is cleared on a change.
- `03-mozhi-ui-and-docs` is done: the Services page has the gate-aware `Mozhi` group with the
  instance field, the engine combo, the effective value and the warning; the 14 `.ts` files are
  complete; `doc/architecture.md`, `doc/free-endpoints.md` and the `AGENTS.md` counts describe the
  service.
- The overall Mozhi criteria hold: the service is behind `translation/allowFreeServices`, uses
  500 ms and 5000 UTF-16 units, allows `auto` and returns `UnsupportedLanguage` when the instance
  rejects the pair, maps every documented error, serializes the requests through `QueuedWebService`,
  and the log never contains the text, the full URL or the response body.
- The full suite passes 33/33 on Linux in Debug and Release without warnings; Windows Debug/Release
  and its 28 tests are recorded as not performed (no MSVC); the optional live smoke is recorded as
  not performed unless the user runs it. CTest never contacts the real endpoint.
- `README.md`, `README.ru.md`, `doc/handoff.md`, the root plan and `07-fallback-chain` are not edited
  here.

## Depends on

`01-free-web-infrastructure` (the gate and `QueuedWebService`). The children are ordered:
`02-mozhi-settings` depends on `01-mozhi-service`, and `03-mozhi-ui-and-docs` depends on
`02-mozhi-settings` (the UI needs the normalized settings helpers and the engine list). The order is
a real dependency, not only numbering.

## Risks and what is needed from the user

- Public Mozhi instances are unstable and may disappear or rate-limit without notice. The user can
  change the URL; do not add an automatic instance rotation, which would hide where the text goes. A
  self-hosted instance is the reliable option and is mentioned in the docs.
- Mozhi is a proxy: stability, limits and terms depend on the instance and the selected engine. A
  `google` engine through Mozhi is not the official Google API and must keep the unofficial warning.
- The doc gives no Mozhi-specific pause or chunk limit. The 500 ms pause and 5000 UTF-16 units are
  conservative assumptions; keep them in named constants, test them with `setRateLimitMs(0)`, and
  record any measured limit from a live smoke instead of changing them silently. An HTTP 413/414 is
  `TooLong` and is the signal that the percent-encoded URL got too long.
- The instance URL is user input. Validate it before saving or using it, never log the full query,
  and do not follow a cross-origin redirect: use `SameOriginRedirectPolicy` so the text cannot move to
  another host silently.
- Some engines are broken or disabled on a public instance (observed on 2026-11-09:
  `libretranslate` was disabled, `mymemory` rejected some language pairs, `deepl` returned an
  upstream "older version" message, and the default instance answered a valid request with an
  upstream 429). That is the instance's/engine's responsibility, not a reason to add workarounds; the
  error paths must stay clear and the default `google` engine remains the practical choice.
- Windows Debug/Release and its 28 tests are unavailable on this Linux machine; record them as not
  performed, not as passed. A live smoke may fail because a public instance is down or rate-limited;
  record it as not performed rather than claiming success.
