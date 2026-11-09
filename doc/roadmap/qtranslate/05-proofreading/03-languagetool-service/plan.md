---
kind: task
status: todo
depends: [01-proofreading-core, 02-languagetool-parser]
---

# LanguageTool service

## Task

Add `LanguageToolService` on `QueuedWebService` and wire it into `AppController` through the
`ProofreadingManager` from `01-proofreading-core`. This is the first real provider: it checks text
with the public LanguageTool API or a self-hosted server, sends the configured premium key, splits
long text at the documented 20 000-character request limit, shifts the chunk offsets, filters the
issues by the requested categories, cancels cleanly and maps every error with a fixed message. The
settings tab, the `.ts` strings and the main-window UI are `04-text-checking-tab` and
`05-main-window-check-and-replace`.

The service follows the established shape of the free web services: `id()` is `languagetool`,
`displayName()` is `LanguageTool`, `isConfigured()` is true (the public endpoint needs no key and a
wrong premium key is an `Auth` error from the response), `isLocal()` is false and
`supportsAutoLanguage()` is true. `languages()` is `Languages::supported("languagetool", …)` from
`02-languagetool-parser`. `applySettings()` reads `proofreading.endpoint` and `proofreading.apiKey`
only; an empty endpoint means the default `https://api.languagetool.org/v2/check`, and
`setEndpoint(const QUrl &)` is the test seam (an empty or invalid URL falls back to the default).
The constructor sets the per-host pause explicitly from the public limit (about 20 requests/min,
75 000 characters/min) instead of inheriting the 2000 ms base default; the exact constant is fixed
when the task is detailed and must be a named constant with a comment.

The request is `POST <endpoint>` with `Content-Type: application/x-www-form-urlencoded` and
`Accept: application/json`; the body carries `text`, `language` (the mapped LanguageTool code or
`auto`) and `enabledOnly=false`, plus `apiKey` and `username` when the configured key is set. The
text is split with `TextChunker::splitWithOffsets(text, 20000)`, the chunks are sent sequentially
inside one reply with `noteRequestSent()` pacing, every issue's `offset` is shifted by its chunk
offset and issues whose `category` is not in `request.categories` (empty = all) are dropped. A failed
chunk fails the whole reply with no partial issue list, and a cancelled or aborted reply stops the
active request and finishes with `Cancelled`.

## Done when

- `src/services/languagetoolservice.{h,cpp}` is in `qtranslate_core`, derives from
  `QueuedWebService`, returns id `languagetool`, display name `LanguageTool`, `isConfigured() ==
  true`, `isLocal() == false`, `supportsAutoLanguage() == true`, a non-empty language list without
  `auto`, the default endpoint and `setEndpoint()` with the per-URL fallback; `applySettings()` reads
  only the endpoint and the key and clears nothing else.
- The request is a urlencoded `POST` to the configured endpoint with the documented body, the
  LanguageTool form encoding (spaces and non-ASCII percent-encoded, `+` never used for a space), the
  optional key, and the JSON `Accept` header; `auto` is sent as `auto` and an explicit source and a
  language outside the supported list are rejected with `UnsupportedLanguage` before any request.
- Long text is split at 20 000 UTF-16 units with `TextChunker::splitWithOffsets`, the chunks are
  sent one at a time in the same queue slot with the pause, each chunk's issue offsets are shifted so
  they address the original text, and the joined issue list is sorted by `offset` with no duplicate
  and no out-of-range offset; a failed middle chunk fails the whole reply without a partial list.
- Category filtering keeps only the requested categories (`spelling`, `grammar`, `punctuation`,
  `style`; an empty request list keeps all) and never changes the offsets.
- Errors: HTTP 401/403 → `Auth` with a fixed "check the LanguageTool key" message, 413 → `TooLong`,
  429 → `RateLimited`, 400 → `BadResponse`, 5xx → `Network`, an HTML/interstitial page →
  `ServiceUnavailable`, malformed JSON → `BadResponse`, a timeout → `Timeout`, a closed server →
  `Network`, and a cross-origin redirect is refused; no message or log line contains the text, an
  issue message, a replacement, the URL or the key. `abort()` finishes with `Cancelled` and stops the
  active request.
- `AppController` owns a `ProofreadingManager`, registers the service, calls its `applySettings()`
  on startup and on every settings change, and keeps `setOnlineServicesAllowed()` in sync with
  `proofreading/allowOnlineServices`; picking a provider that is not registered keeps the first
  service active and logs a fixed warning. No UI, `.ts` or documentation change is made in this
  child.
- `tests/tst_languagetoolservice.cpp` (new, platform-neutral, local `QTcpServer` modelled on the
  `FakeHttpServer` in `tests/tst_mozhiwebservice.cpp`, `setRateLimitMs(0)`) covers the request
  shape, the language mapping, chunking with offset shift, category filtering, every error above,
  the pause with `setRateLimitMs(100)`, queue serialization, cancel/abort and the message/log hygiene;
  `ctest -N` shows 42 Linux tests and would show 37 on Windows. Debug and Release build on Linux
  without warnings and the full suite passes; Windows is recorded as not performed.

## Depends on

`01-proofreading-core` for the manager, the reply and the settings group; `02-languagetool-parser`
for the response parser, the category mapping and the language codes.
