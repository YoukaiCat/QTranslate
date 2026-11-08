---
kind: task
status: todo
depends: [01-free-web-infrastructure]
---

# Mozhi service

## Task

Add Mozhi as an opt-in free service: one integration gives access to several translation engines
through a public or self-hosted instance, and the user can point it at an instance they trust. This
is the second service required by the stage (`MyMemory and Mozhi work with rate limits`) and a
natural second fallback after MyMemory.

Use `doc/free-endpoints.md` §5 for the protocol, §8 for the rules, and §9 for the priority. The
service is `mozhi` and implements `TranslationService` with `requiresOptIn() == true`. It sends `GET
https://<instance>/api/translate?engine=<engine>&from=<src>&to=<dst>&text=...` through
`QueuedWebService` and parses `translated-text`, `detected`, `source_language`, and
`target_transliteration` with a new pure parser. The settings group has an instance URL (default
`https://mozhi.adminforge.de`) and an engine combo (Google, DeepL, DuckDuckGo, LibreTranslate,
MyMemory, Reverso, Yandex; default `google`), plus a warning that public instances are unofficial and
that a self-hosted instance's terms and stability are the user's responsibility. The URL is editable
because public instances disappear; do not rotate instances silently behind the user's back, because
that would send the same text to a different third party. An empty or non-`http(s)` URL is replaced
by the default during settings normalization, the UI shows the effective value, and a warning explains
why the typed value was not kept.

The source language may be `auto` if the instance/engine supports it; if the endpoint rejects an
auto pair, return `UnsupportedLanguage` with a clear message instead of guessing. The language codes
are the Google-style internal mapping used by the other free services, with a small Mozhi-specific
override only where an engine needs it. Text is split with `TextChunker::split` at 5000 UTF-16 code
units and sent sequentially inside one reply; the edge whitespace is restored when concatenating.
The doc gives no Mozhi-specific pause, so keep a conservative 500 ms between requests to the
configured host as a named constant and document it as an assumption (the instance proxies to an
engine with its own limit). The shared queue still guarantees no parallel request to the host.

Errors: HTTP 429/403 and a rate-limit body become `RateLimited`; HTTP 5xx becomes `Network`; an HTML
page, a missing `translated-text`, or a JSON error body becomes `ServiceUnavailable`/`BadResponse`
with a short message; a timeout and `abort()` stop the request. The request URL contains the text, so
the error and log messages must not include the URL or the text. Tests use fixtures and a local
`QTcpServer` pointed to by the instance setting; no real instance is contacted by CTest. A manual
live smoke against the default instance is optional and recorded honestly.

## Done when

- `MozhiService` and `MozhiParser` exist under `src/services/`, are listed in `CMakeLists.txt`, and
  return the internal `mozhi` id with `requiresOptIn() == true`; the service is not active by
  default.
- `[mozhi] instance` and `[mozhi] engine` are loaded/saved/normalized; the default instance is
  `https://mozhi.adminforge.de` and the default engine is `google`; an empty, malformed, or
  non-http(s) URL falls back to the default with a warning and the UI shows the effective value.
- The settings dialog has a Mozhi group with the instance URL, the engine combo, and the
  public-instance warning; the opt-in gate hides or refuses the service as described in
  `01-free-web-infrastructure`; `tst_settings` and `tst_settingsdialog` cover the defaults and the
  round trip.
- `tst_mozhiwebparser` covers a normal response with `translated-text`/`detected`, an empty
  translation, a JSON error, an HTML body, and malformed JSON; each case produces the documented
  error and a message without the text or URL.
- `tst_mozhiwebservice` with a local `QTcpServer` verifies the URL path `/api/translate`, the query
  fields `engine`, `from`, `to`, `text`, URL encoding of spaces and non-ASCII text, the configured
  instance, the engine change, and the response body.
- Long text is split into 5000-character chunks at paragraph/sentence boundaries and sent
  sequentially with the 500 ms pause (`setRateLimitMs(0)` in tests); the result restores the edge
  whitespace and matches the concatenation of the chunk translations; an unsupported language
  returns `UnsupportedLanguage` before any request.
- HTTP 429/403 and a rate-limit body return `RateLimited`; HTTP 5xx returns `Network`; an HTML/blocked
  response returns `ServiceUnavailable` without raw HTML; a timeout and `abort()` finish with
  `Timeout`/`Cancelled` and stop the request.
- Requests to the configured host are serialized through `QueuedWebService`; a second translation
  waits for the active one and for the pause; cancelling a queued request does not send it; the log
  never contains the text, the full URL, or the response body.
- `doc/architecture.md` §4.5/§4.9/§4.11 and `doc/free-endpoints.md` §5/§8/§9 are updated; both
  READMEs list Mozhi with the instance/engine note; the manual live smoke is recorded as passed or
  as not performed with the reason (a public instance may be down).
- Debug and Release build without warnings; the full suite passes with the new tests; Windows is
  recorded as not performed when MSVC is unavailable.

## Risks

- Public Mozhi instances are unstable and may disappear or rate-limit without notice. The user can
  change the URL; do not add an automatic instance rotation, which would hide where the text goes.
  A self-hosted instance is the reliable option and is mentioned in the docs.
- Mozhi is a proxy: the stability, limits, and terms depend on the instance and the selected engine.
  A `google` engine through Mozhi is not the official Google API and must keep the unofficial
  warning.
- The doc gives no Mozhi-specific pause or chunk limit. The 500 ms pause and 5000-character chunk are
  conservative assumptions; keep them in named constants, test them with `setRateLimitMs(0)`, and
  record any measured limit from a live smoke instead of hard-coding a different value silently.
- The instance URL is user input. Validate it before saving or using it, never log the full query,
  and do not follow redirects to a different host without documenting it.
