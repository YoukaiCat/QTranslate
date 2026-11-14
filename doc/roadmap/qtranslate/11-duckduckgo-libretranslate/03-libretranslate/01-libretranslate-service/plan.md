---
kind: task
status: todo
---

# LibreTranslate parser and service

## Task

Implement the LibreTranslate protocol recorded in
`../../01-endpoint-research/notes/duckduckgo-libretranslate.md` as a pure parser plus a
`QueuedWebService`, register the service, add its language column and the fallback id, and cover it
with fixtures and local-server tests. No settings key, no UI, no `.ts` and no `doc/` change: the INI
key, the settings group and the strings belong to `02-libretranslate-settings-ui`.

The service id is `libretranslate` and the display name is the product name `LibreTranslate`,
untranslated. The id is a top-level service id; it is unrelated to the `mozhi/engine` id of the same
name, and the two namespaces never interact. It sends `POST <base>/translate` with
`Content-Type: application/json` and the observed
JSON body (`q`, `source`, `target`, and `format` only if the research shows it matters), and reads
`translatedText` plus the optional `detectedLanguage.language` and `confidence`. The base URL is
injected through `setBaseUrl()` and `defaultBaseUrl()` so the tests can point it at a local
`QTcpServer`; this task keeps the seam and the empty-default behaviour, and
`02-libretranslate-settings-ui` wires `applySettings()`. `isConfigured()` returns false with a
`configurationError()` that names the base URL when the value is empty; when the research found a
public default, that default is returned by `defaultBaseUrl()` and the service is configured out of the
box.

Language handling: a `libreTranslateCodeOf()` table in `src/core/languages.cpp` with the codes the
`/languages` endpoint returns, mapping the internal catalogue and refusing every internal code that has
no LibreTranslate code. `auto` is sent as `auto` only when the server supports detection, and a
`detectedLanguage.language` is mapped back through `Languages::fromServiceCode`; an unsupported pair
finishes with `UnsupportedLanguage` before any request.

Chunking and pacing are named constants taken from the research, or documented conservative assumptions
when the research had no measurement, in the style of `MozhiService`'s header comment. Chunks are sent
sequentially inside one reply, edge whitespace is restored, and a failed chunk fails the whole reply
without a partial translation.

Errors are classified body-first, because LibreTranslate returns `{"error": "..."}` with a non-2xx
status and may answer `200` for a translation that carries no text: `403`/`429` and a rate-limit body →
`RateLimited`; an oversized request (`413`) → `TooLong`; `5xx` without a recognised body → `Network`; an
HTML page or an unknown server error → `ServiceUnavailable`; malformed JSON, an empty or missing
`translatedText` → `BadResponse`; timeout and `abort()` → `Timeout`/`Cancelled`. The base URL is user
input, so every request uses `SameOriginRedirectPolicy`, and no message or log line contains the text,
the body or the full URL; network errors use `NetworkManager::describeNetworkError(request, false)`.

Register the service in `AppController::createTranslation()` behind the existing providers and in
`applySettings()`, append `libretranslate` to `fallbackServiceIds()` in `src/core/settings.cpp` (and
update the exact-list assertion at `tests/tst_settings.cpp:122`), add the two sources to the root
`CMakeLists.txt`, and add the two tests to `tests/CMakeLists.txt`.

## Done when

- `LibreTranslate` is selectable; with `setBaseUrl()` pointing at a local `QTcpServer` it translates
  against the recorded fixtures, sends the configured source or `auto`, and fills `detectedSourceLang`
  from a `detectedLanguage.language` fixture.
- With an empty base URL and no public default, `isConfigured()` is false, `configurationError()` names
  the base URL, and `translate()` reports that error without sending a request.
- `tst_libretranslateparser` and `tst_libretranslateservice` cover the response fields, the `error`
  body with several statuses, the chunk split and its order, the pause with `setRateLimitMs(0)` in the
  timing-sensitive cases, `auto`, the unsupported pair, abort, timeout and the same-origin redirect
  refusal; `tst_languages` covers the code table and an unsupported code.
- `libretranslate` is in `fallbackServiceIds()` and can be chosen as the fallback against the local
  fixture.
- The log and every error message stay free of the text, body and full URL, checked by a test case.
- Debug and Release build without warnings on Linux and the full suite passes; Windows is reviewed by
  diff and recorded as not performed.
