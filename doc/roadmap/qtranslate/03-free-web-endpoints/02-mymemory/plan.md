---
kind: task
status: todo
depends: [01-free-web-infrastructure]
---

# MyMemory service

## Task

Add MyMemory as the first real no-key service and the first consumer of the opt-in gate and the
shared per-host queue from `01-free-web-infrastructure`. MyMemory is a free API with a quota rather
than a scraped page, so it is the safest free service and the most predictable fallback. It still
needs an explicit switch because it is not an official Google/DeepL API and sends the text to a third
party.

Use `doc/free-endpoints.md` §6 for the protocol, §8 for the rules, and §9 for the priority. The
service is `mymemory` and implements `TranslationService` with `requiresOptIn() == true`; it is not
active by default. It uses `QueuedWebService` with a 350 ms pause, sends `GET
https://api.mymemory.translated.net/get?q=...&langpair=<src>|<dst>&mt=1`, and parses
`responseData.translatedText`, `responseStatus`, `responseDetails`, and `quotaFinished` through a new
pure parser. MyMemory does not support auto-detection: when the source language is `auto`, return
`UnsupportedLanguage` with a clear "choose a source language" message instead of guessing or sending
a bad pair. Long text is split into chunks whose UTF-8 encoding is at most 500 bytes, because the
limit is in bytes and a UTF-16 `TextChunker` cut is wrong for Cyrillic, Chinese, and emoji; add a
byte-aware split to `core/textchunker` with tests rather than duplicating it in the service. Chunks
are sent sequentially inside one reply, with the 350 ms pause between requests, and the leading and
trailing whitespace of every chunk is restored when the translations are concatenated.

Errors must be classified, not shown raw: HTTP 429, HTTP 403, and a rate-limit body become
`RateLimited`; `quotaFinished == true` and quota text become `QuotaExceeded`; an HTML page or an
unexpected JSON shape becomes `ServiceUnavailable`/`BadResponse` with a short message; HTTP 5xx
becomes `Network`. The request URL contains the text, so the error and log messages must not include
the full URL or the text; only the status and the service name. Tests use fixtures and the local
`QTcpServer` pattern from `tst_deeplwebservice` and `tst_googletranslateservice`; no real request is
made by CTest.

The settings entry is a single "MyMemory" group in the Services page with the free/no-key warning
and the opt-in hint (the gate itself lives in `01-free-web-infrastructure`); MyMemory is a free API
rather than a scraped page, but it is still opt-in because it is not an official Google/DeepL API.
There is no key and no per-service URL setting. `README`/`README.ru` and `doc/architecture.md` get the service description
and the language/limit notes.

## Done when

- `MyMemoryService` and `MyMemoryParser` exist under `src/services/`, are listed in `CMakeLists.txt`,
  and return the internal `mymemory` id with `requiresOptIn() == true`; Google, DeepL, and the LLM
  service are unchanged.
- `tst_mymemoryparser` covers a normal response, `detected` if present, `quotaFinished`, a non-200
  `responseStatus` with `responseDetails`, an HTML body, malformed JSON, and a missing
  `responseData.translatedText`; each case produces the documented `TranslationError` and a message
  without the text or URL.
- `tst_mymemoryservice` with a local `QTcpServer` verifies the request method and query fields
  (`q`, `langpair` with `|`, `mt=1`), URL encoding of spaces and non-ASCII text, the `mymemory`
  service id, the detected language if returned, and the response body/headers.
- A source language of `auto` returns `UnsupportedLanguage` with a clear message before any request;
  an unsupported target language is rejected with `UnsupportedLanguage`; a supported pair is sent
  with the exact MyMemory codes.
- Long text is split into chunks of at most 500 UTF-8 bytes at word/sentence boundaries without
  breaking a UTF-8 sequence or a surrogate pair; the chunks are sent sequentially, the requests are
  at least 350 ms apart (`setRateLimitMs(0)` in tests), and the result restores the edge whitespace
  and matches the concatenation of the chunk translations. `tst_textchunker` covers the byte split
  with Latin, Cyrillic, Chinese, emoji, CRLF, and empty text.
- HTTP 429/403 and a rate-limit body return `RateLimited`; `quotaFinished` returns `QuotaExceeded`;
  HTTP 5xx returns `Network`; an HTML/blocked response returns `ServiceUnavailable` without raw HTML;
  a timeout and `abort()` finish with `Timeout`/`Cancelled` and stop the request.
- Requests to the host are serialized through `QueuedWebService`; a second translation waits for the
  active one and for the rate pause; cancelling a queued request does not send it.
- The settings dialog shows a MyMemory group with the free/no-key warning and no key field; the
  opt-in gate hides or refuses the service as described in `01-free-web-infrastructure`;
  `tst_settingsdialog` covers the group and the gate interaction.
- `doc/architecture.md` §4.5/§4.9 and `doc/free-endpoints.md` §6/§9 are updated; both READMEs list
  MyMemory in the providers section; no text, key, or URL with the text is logged.
- Debug and Release build without warnings; the full suite passes with the two new tests; a manual
  live smoke (a short en→ru and a long multi-chunk text) is recorded as passed or as not performed
  with the reason. Windows is recorded as not performed when MSVC is unavailable.

## Risks

- The 500-byte limit is on the decoded `q` value, but the URL is percent-encoded and expands; keep the
  chunk limit on the UTF-8 bytes of the text and test the actual query length with a Cyrillic text.
  If MyMemory rejects a 500-byte chunk with a URL-length error, lower the limit to a safe value and
  record the measured behaviour.
- MyMemory requires an explicit source language. Do not silently send `auto` or guess a language:
  a clear `UnsupportedLanguage` is better than a wrong translation, and the fallback chain can try a
  service that supports auto-detection.
- The free quota and the `responseStatus` values are inconsistent in the wild. The parser must accept
  a numeric or string status and match the documented `quotaFinished`/`responseDetails` text; when
  the body is unclear, return `BadResponse`, never the raw HTML.
- MyMemory quality is mixed and the endpoint may rate-limit by IP. Automated tests must stay on
  fixtures; a live smoke is manual and may fail because of the quota.
- Adding a byte splitter to `TextChunker` changes a shared core file; keep the existing `split()`
  behaviour byte-identical and add `splitUtf8`/`splitBytes` as a separate function with its own
  tests.
