---
kind: task
status: in-progress
detailed: true
---

# Reverso parser and language codes

## Task

Add the pure parsing layer for the unofficial Reverso endpoint and the `reverso` language branch.
This task is the first child of `06-reverso` because the protocol shape and the six-language set are
the only risky parts of the node. It adds no `ReversoService`, registration, UI, translations or
documentation; those are `02-reverso-service`, `03-reverso-settings` and `04-reverso-docs`.

The protocol is recorded in `doc/free-endpoints.md` §4 and was checked against the upstream
`reverso-services` plugin on 2026-11-09 (`ReversoLanguageMapper.kt`, `ReversoModels.kt`,
`ReversoClient.kt`, `ReversoTranslatorService.kt` and `ReversoServicesTest.kt`, branch `develop`):

- The request is `POST https://cps.reverso.net/api2/TranslateText` with a JSON object:
  `source`, `direction` (`<source>-<target>`), `deviceId` (`"0"`), `uiLang` (`"en"`), `origin`
  (`"chromeextension"`), `accessToken` (`""`), `appId` (`"0"`). The upstream model sends the
  numeric-looking fields as strings, and so does this plan.
- A success body is a flat object such as
  `{"error":false,"success":true,"translation":"La banque est fermee.","directionFrom":"en","directionTo":"fr"}`.
  `translation` is the result; `directionFrom`/`directionTo` are two-letter codes in the observed
  sample; extra fields are tolerated.
- A provider failure is a flat object such as
  `{"error":true,"success":false,"message":"Too many requests"}`. It arrives with HTTP 200, so the
  body is classified before the status.
- The upstream mapper declares exactly six languages — English, French, German, Spanish, Italian and
  Russian — and rejects `auto` before any request. `ReversoTranslatorService` validates the source
  and target individually and has no direction-pair table, so the six languages form a full cross
  product of 36 directions. The direction value is `<source>-<target>`, for example `en-fr`.

The parser does not parse HTML, does not touch the network and never puts the body, the text, the
URL or an extracted value into a message.

## Steps

1. **Add the `reverso` language branch.** In `src/core/languages.cpp`, next to `kBingWeb`, add
   `constexpr QLatin1String kReverso("reverso");` and the observed catalogue

   ```cpp
   constexpr const char *kReversoCodes[] = {
       "en", "fr", "de", "es", "it", "ru",
   };
   ```

   Add `reversoCodeOf(const LanguageInfo &info)` that starts from `info.code`, returns the
   whitelisted pointer when the code is in `kReversoCodes`, and `nullptr` otherwise. Unlike Yandex
   and Bing it needs no `CodeOverride` table: every internal code equals Reverso's code. Call it from
   `serviceCodeOf()` for `kReverso`. Do not add a `LanguageInfo` column and do not change
   `kLanguages`; `fromServiceCode()` and `supported()` then reverse the mapping for free. Do not add
   a synonym. `auto` is not in `kLanguages`, so `Languages::toServiceCode("reverso", "auto", ...)`
   returns empty, and child `02` will return a dedicated `UnsupportedLanguage` message for `auto`
   before any request.

2. **Add the parser header and source.** Create `src/services/reversoparser.h/.cpp` with this API
   (`serviceId` is empty in every result, like `MozhiParser`/`YandexWebParser`):

   ```cpp
   namespace ReversoParser {

   struct Response
   {
       TranslationResult result; // translatedText/detectedSourceLang/error; serviceId empty
       QString directionFrom;    // raw directionFrom, empty when absent or not a string
       QString directionTo;      // raw directionTo, empty when absent or not a string
   };

   // Parses a documented JSON body. A provider error (`error == true` or
   // `success == false`) is classified first; otherwise `success` must be true
   // and `translation` a non-empty string. Every message is a fixed literal
   // except the validated short provider message of a `success: false` body;
   // the raw body, the text and a URL are never included.
   Response parseResponse(const QByteArray &body);

   // Recognises an HTML page, a provider error object and a plain-text
   // rate-limit/block body. Returns TranslationError::None when the body is not
   // a recognised error, so the caller can fall back to the HTTP status.
   TranslationResult parseErrorBody(const QByteArray &body);

   // Error for a non-2xx HTTP status: 401/403/429 are a rate limit, 413 means
   // the request was too long, 5xx is a network error, anything else is a bad
   // response.
   TranslationError errorForHttpStatus(int status);

   } // namespace ReversoParser
   ```

   Include `core/translationtypes.h`, `core/languages.h`, `QByteArray`, `QJsonDocument`,
   `QJsonObject`, `QJsonParseError`, `QJsonValue`, `QString`. A small `errorResult()` helper returns
   a result with an empty `serviceId` and the given error and message.

   Add `src/services/reversoparser.cpp/.h` to `qtranslate_core` in `CMakeLists.txt` after
   `src/services/queuedwebservice.*` and before `src/services/yandexwebparser.*`, so the list stays
   alphabetical. The service files are added by child `02`.

3. **Implement `parseErrorBody()`.** It is the single place that decides whether a body is a
   recognised error; `parseResponse()` calls it first.

   - Trim leading ASCII whitespace. An empty body returns `None`.
   - A body whose first non-space byte is `<` is an HTML/blocked page:
     `ServiceUnavailable` with `the endpoint returned an HTML page`.
   - A body that starts with `{` and parses as a JSON object is a provider body. Read the optional
     `error` and `success` booleans. It is an error when `error == true`, or when `success` is a
     boolean and `false`. Read the optional `message` string, trim it, and validate it: it must be
     1–200 characters, contain no `<`, `>`, `{`, `}` or `://`, and no `\r`, `\n` or other control
     character. A message that fails validation is discarded. Classify the validated message
     case-insensitively:
     - `too many`, `rate limit`, `rate-limit`, `ratelimit` or `quota` → `RateLimited` with the fixed
       literal `the endpoint reported a rate limit` (do not echo the provider message for a rate
       limit);
     - otherwise `ServiceUnavailable` with the validated message, or with
       `the endpoint reported an error` when the message is missing or invalid.
     A JSON object with neither an error flag nor `success == false` returns `None`; a JSON array, a
     malformed JSON body or any other root type returns `None` so the HTTP status decides.
   - Any other non-empty body is plain text. Lowercase it and classify:
     - a rate-limit keyword from the list above → `RateLimited` with the fixed literal;
     - `captcha`, `blocked`, `forbidden`, `access denied`, `banned`, `robot` or `unavailable` →
       `ServiceUnavailable` with `the endpoint is blocked or unavailable`;
     - otherwise `None`.
   - Every fixed message is an English literal without the body, the text, a URL, `<`, `{` or `}`.
   The accepted provider message is the only variable text and is limited to the validated short
   `message` field above.

4. **Implement `parseResponse()`.** It returns a `Response` whose `result.serviceId` is empty.

   - Call `parseErrorBody()` first. When it reports an error (or `UnsupportedLanguage`, which the
     parser does not produce today but `TranslationResult` can carry), copy it into `response.result`
     and return. This makes a provider error under HTTP 200 an error, not a translation, and still
     exposes the documented `directionFrom`/`directionTo` only on a success body.
   - Parse the body with `QJsonDocument::fromJson`. A parse error, a non-object root and an empty
     body are `BadResponse` with `the response is not valid JSON`.
   - Read `success`. A missing or non-boolean value is `BadResponse` with
     `the response has no success flag`. A `success == false` that `parseErrorBody()` did not catch
     (for example, because the body was classified before this function and the object is
     inconsistent) is `ServiceUnavailable` with the same message rule as step 3; the success path
     requires `success == true`.
   - Read `translation`. It must be a string and must not be empty after trimming; a missing,
     non-string, empty or all-whitespace value is `BadResponse` with
     `no translated text in the response`.
   - Read `directionFrom` and `directionTo` when they are strings and store the raw values in
     `Response`; a missing or non-string value stays empty and is not a failure. Set
     `result.detectedSourceLang` to
     `Languages::fromServiceCode("reverso", directionFrom, Languages::Direction::Source)`; an
     unknown or absent code stays empty. `directionTo` is exposed for the service and the tests but
     is not used to build the result.
   - On success set `result.translatedText` to the `translation` value, leave `error` as `None` and
     `errorMessage` empty, and tolerate every other field (`error`, `id`, extra provider fields).

5. **Implement `errorForHttpStatus()`.** Map `401`, `403` and `429` to `RateLimited`, `413` to
   `TooLong`, every `5xx` to `Network`, and every other non-2xx status to `BadResponse`. Use the same
   fixed literals as the sibling parsers (`the endpoint reported a rate limit`,
   `the request is too long for the endpoint`, `the endpoint is temporarily unavailable`,
   `the endpoint returned an unexpected status`). A `None` or 2xx status is not expected by the
   caller; return `BadResponse` for it rather than `None`.

6. **Add the parser fixtures.** Create `tests/fixtures/reverso/` with the saved bodies below. Use the
   `fixture()` helper and the `QTRANSLATE_FIXTURES_DIR` pattern from `tests/tst_mozhiparser.cpp`.
   The success fixture is the upstream sample shape with fake values; no live token, cookie or
   identifier is committed.

   - Success: `response.json` (the en→fr sample above), `response_de_es.json` (another pair and a
     non-ASCII translation), `response_no_directions.json` (both direction fields absent),
     `response_unknown_direction.json` (`directionFrom: "xx"`), `response_extra_fields.json`
     (`error: false`, `id`, a second unrelated field).
   - Bad success bodies: `response_missing_translation.json`, `response_empty_translation.json`,
     `response_whitespace_translation.json`, `response_non_string_translation.json`,
     `response_missing_success.json`, `response_non_bool_success.json`, `response_empty_object.json`,
     `response_array.json`, `response_string.json`, `response_malformed.json`, `response_empty.json`.
     Put a `SECRET` sentinel in the text of the malformed/bad fixtures where a leak would be visible.
   - Errors: `error_success_false.json` (`{"error":true,"success":false,"message":"Too many requests"}`),
     `error_success_false_generic.json` (`{"success":false,"message":"The text is not supported."}`),
     `error_flag_only.json` (`{"error":true}`),
     `error_flag_with_success.json` (`{"error":true,"success":true,"translation":"..."}`),
     `error_empty_message.json`,
     `error_message_html.json` (a `message` with `<html>` and `SECRET`),
     `error_message_control.json` (a `message` with a tab or newline),
     `error_message_url.json` (a `message` containing `://`),
     `error_long_message.json` (a message longer than 200 characters with `SECRET`).
   - Non-JSON bodies: `response_html.html` (an HTML block page with `SECRET`), `rate_limit.txt`,
     `blocked.txt`, `plain.txt`.

7. **Add `tst_reversoparser`.** Create `tests/tst_reversoparser.cpp` with
   `Q_DECLARE_METATYPE(TranslationError)` and cover:

   - the success sample: `translatedText`, `detectedSourceLang == "en"`, raw `directionFrom`/`directionTo`,
     no error and an empty `serviceId`; a second pair; a non-ASCII translation; extra fields
     tolerated; missing directions accepted with an empty detected language; an unknown
     `directionFrom` mapped to empty;
   - `success == false` with `"Too many requests"` → `RateLimited` with the fixed message;
     a generic short message → `ServiceUnavailable` with exactly that message; a missing/empty
     message → `ServiceUnavailable` with the generic literal; a message with markup, `://`,
     a control character or more than 200 characters → `ServiceUnavailable` with the generic
     literal; `error == true` with `success == true` → the error path, never the translation;
     `error == true` without `success` → the error path;
   - a malformed document, a non-object root, a JSON array, a JSON string and an empty body →
     `BadResponse`; a missing or non-boolean `success` → `BadResponse`; `success == true` with a
     missing, empty, whitespace-only or non-string `translation` → `BadResponse`;
   - an HTML body through both `parseResponse()` and `parseErrorBody()` → `ServiceUnavailable`;
     the plain-text rate-limit/block/neutral fixtures → `RateLimited`/`ServiceUnavailable`/`None`;
   - `errorForHttpStatus()` for 401/403/429, 413, 500/503, 400 and 204;
   - a helper that asserts every error message is non-empty and contains no `SECRET`, `<`, `{`, `}`
     or `://`.

   Add `qtranslate_add_test(tst_reversoparser)` to `tests/CMakeLists.txt` in the platform-neutral
   block after `tst_queuedwebservice` and before `tst_settings`, so the list stays alphabetical; do
   not add a platform conditional.

8. **Update `tst_languages`.** Add `const QString reverso = QStringLiteral("reverso");` to
   `tests/tst_languages.cpp`.

   - Forward rows (`toServiceCode`): `en`, `fr`, `de`, `es`, `it`, `ru` map to themselves in both
     directions; `auto`, `pt-BR`, `pt-PT`, `zh-Hans`, `uk`, `pl` and `xx` map to empty.
   - Reverse rows (`fromServiceCode`): `en`, `fr`, `de`, `es`, `it` and `ru` map to themselves
     in both directions; `xx` and `pt` map to empty.
   - `roundTrip()`: add `reverso` to the service list; every declared Reverso pair round-trips, and
     no new exception is needed.
   - `supportedLists()`: assert the `reverso` source and target lists are equal, have exactly six
     entries, contain all six codes, and exclude `auto`, `pt-BR`, `zh-Hans`, `uk` and `pl`.

9. **Build, count and verify.** Configure/build Linux Debug and Release with `-Wall -Wextra`; run
   `tst_reversoparser` and `tst_languages`, then the full suite with
   `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`. `ctest -N` must show 38
   Linux tests (37 + the new target); Windows would be 33 (32 + 1). Record 38/33 in the outcome.
   Windows/MSVC is unavailable: record the Windows build and its 33 tests as not performed.
   `git diff --stat` may show only `src/core/languages.cpp`, `tests/tst_languages.cpp`,
   `src/services/reversoparser.*`, `CMakeLists.txt`, `tests/tst_reversoparser.cpp`,
   `tests/CMakeLists.txt` and the new fixtures. Do not touch the service, `AppController`, the
   settings UI, `.ts`, `doc/`, `AGENTS.md`, the READMEs, `doc/handoff.md`, the root plan or
   `07-fallback-chain`. Leave commit and push to the coordinator/lead.

## Done when

- The `reverso` language branch maps only `en`, `fr`, `de`, `es`, `it` and `ru` to themselves in
  both directions; `auto`, `pt-BR`, `zh-Hans`, `uk`, `pl` and every other code map to empty;
  `supported("reverso", ...)` returns exactly those six codes in both directions; no
  `LanguageInfo` column, no override table and no synonym are added; `tst_languages` covers the
  forward, reverse, round-trip and supported-list rows.
- `ReversoParser` exists under `src/services/`, is listed in `qtranslate_core` and has the API above.
  `parseResponse()` reads `translation`, `directionFrom`, `directionTo`, `success`, `error` and
  `message`; a `success: true` body with a non-empty translation succeeds and maps `directionFrom`
  to `detectedSourceLang`; a missing, empty, whitespace-only or non-string translation is
  `BadResponse`; a missing/non-boolean `success` is `BadResponse`; `directionTo` is exposed and
  tolerated but does not change the result.
- `parseErrorBody()` classifies an HTML page, a `success: false` or `error: true` object and a
  plain-text rate-limit/block body; a rate-limit body is `RateLimited` with the fixed literal; a
  generic provider error is `ServiceUnavailable` with the short validated `message` or the fixed
  literal; a markup, control-character, overly long or empty message never reaches the result; a
  neutral or unrecognised body is `None`.
- `errorForHttpStatus()` maps 401/403/429 to `RateLimited`, 413 to `TooLong`, 5xx to `Network` and
  other non-2xx statuses to `BadResponse`; every message is a fixed English literal without the
  body, the text or a URL.
- `tst_reversoparser` covers all the cases above with the saved fixtures; the message-hygiene helper
  passes; the new target is platform-neutral and registered after `tst_queuedwebservice`.
- Debug and Release build on Linux without warnings; `ctest -N` shows 38 Linux tests and would show
  33 on Windows; the full suite passes 38/38; Windows Debug/Release and its 33 tests are recorded as
  not performed (no MSVC).
- No service, `AppController`, settings UI, `.ts`, `doc/`, `AGENTS.md`, README, `doc/handoff.md`,
  root-plan or `07-fallback-chain` file is changed; no commit is made.

## Risks

- Reverso supports six languages only, and the upstream mapper rejects `auto`. Do not widen the
  whitelist from memory: add a code only with an observed direction and a fixture. The full
  36-direction cross product is an inference from the upstream service, which validates the two
  languages individually; a live smoke checks one pair, and a rejected pair must surface as a clear
  provider error, not a silent source-text echo.
- The document's §4 field list omits the upstream `error` flag. Handling only `success` would let
  `{"error":true,"success":true,...}` look like a translation. Treat either flag as an error and pin
  it with a fixture. Keep the upstream models as the reference if the live shape differs.
- `success: false` arrives with HTTP 200 and is a normal error path. The short provider `message`
  is allowed only after the length, markup and control-character check; the raw body, an HTML page
  and the source text are never included. Do not relax the check to "show the message".
- `directionFrom`/`directionTo` are two-letter codes in the observed sample, but they are not used
  to build the result. Do not fail a success body because `directionTo` is missing, unknown or
  differs; the service logs only the request direction, which is safe.
- The fixtures must be small, fake and stable: no live page, token, cookie or identifier. `SECRET`
  sentinels in the HTML, markup-message and long-message fixtures prove the message hygiene.
- `QJsonDocument` is used only on a body, never on an HTML page; a malformed or truncated body must
  fail with `BadResponse`, not crash or read out of bounds.
- Windows Debug/Release and its 33 tests are unavailable on this Linux machine; record them as not
  performed, not as passed.
