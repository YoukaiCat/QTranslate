---
kind: task
status: todo
---

# DuckDuckGo parser and service

## Task

Implement the DuckDuckGo protocol recorded in
`../../01-endpoint-research/notes/duckduckgo-libretranslate.md` as a pure parser plus a
`QueuedWebService`, register the service, add its language column and the fallback id, and cover it
with fixtures and local-server tests. No settings key, no UI, no `.ts` and no `doc/` change: the group
and the strings belong to `02-duckduckgo-ui`.

The service id is `duckduckgo` and the display name is the product name `DuckDuckGo`, untranslated,
like `Google` and `Reverso`. It follows the observed request shape exactly — method, URL, headers,
body or query, and the response field that carries the translation. `isConfigured()` returns true;
`applySettings()` stays empty unless the research found a value to read, in which case this task only
keeps the seam and `02-duckduckgo-ui` owns the INI key.

Language handling: a `duckDuckGoCodeOf()` table in `src/core/languages.cpp` with the codes the route
actually accepts, plus a `fromServiceCode` path through `kSynonyms` or the same table when the response
reports a detected language. `auto` is accepted only when the route accepts it; otherwise `translate()`
finishes with `UnsupportedLanguage` and the same explicit-source message style as `ReversoService` —
never a guessed language.

Chunking and pacing are named constants taken from the research. When the research gives only an
assumption, keep it as a conservative assumption and say so in the header comment, as
`MozhiService` does. Chunks are sent sequentially inside one reply, edge whitespace is restored, and a
failed chunk fails the whole reply without a partial translation.

Errors are classified body-first, as in `BingWebService` and `ReversoService`: `403`/`429` and a
rate-limit body → `RateLimited`; an oversized request → `TooLong`; `5xx` without a recognised body →
`Network`; an HTML page, a cookie challenge or a provider error object → `ServiceUnavailable`;
malformed, empty or missing translation → `BadResponse`; unknown non-2xx → `BadResponse`; timeout and
`abort()` → `Timeout`/`Cancelled`. Every request uses `SameOriginRedirectPolicy`. No message or log
line contains the text, the body, a cookie or the full URL, and network errors use
`NetworkManager::describeNetworkError(request, false)`, never `QNetworkReply::errorString()`.

Register the service in `AppController::createTranslation()` behind the existing providers and in
`applySettings()`, append `duckduckgo` to `fallbackServiceIds()` in `src/core/settings.cpp` (and update
the exact-list assertion at `tests/tst_settings.cpp:122`), add the two sources to the root
`CMakeLists.txt`, and add the two tests to `tests/CMakeLists.txt`.

## Done when

- `DuckDuckGo` is selectable, translates against a local `QTcpServer` that serves the recorded fixtures,
  and fills `detectedSourceLang` when the fixture carries a detected language.
- `tst_duckduckgoparser` and `tst_duckduckgoservice` cover the response fields, every documented error
  shape, the chunk split and the chunk order, the pause with `setRateLimitMs(0)` in the timing-sensitive
  cases, `auto` when supported (and its rejection when not), abort, timeout and the same-origin redirect
  refusal; `tst_languages` covers the code table, an unsupported code and `auto`.
- `duckduckgo` is in `fallbackServiceIds()`, appears in the fallback combo, and a translation with it as
  the fallback succeeds against the local fixture.
- The log and every error message stay free of the text, body, cookie and full URL, checked by a test
  case.
- Debug and Release build without warnings on Linux and the full suite passes; Windows is reviewed by
  diff and recorded as not performed.
