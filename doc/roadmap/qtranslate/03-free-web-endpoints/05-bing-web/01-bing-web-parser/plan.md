---
kind: task
status: in-progress
detailed: true
---

# Bing web parser and language codes

## Task

Add the pure parsing layer for the unofficial Bing web endpoint and the `bing-web` language branch.
This task implements the fragile part of the service first: the tolerant extraction of the translator
page markers and the translation/error JSON parsing, both covered by saved fixtures. It does not add
`BingWebService`, registration, UI, translations or documentation; those are `02-bing-web-service`,
`03-bing-web-settings` and `04-bing-web-docs`.

The endpoint protocol is recorded in `doc/free-endpoints.md` §3 and was observed against
`www.bing.com` on 2026-11-09:

- The token page is `GET https://www.bing.com/translator`. Its JavaScript contains
  `IG:"37FB0FCDC77243C2907280DAAE269406"`, several `data-iid="translator.5023"` values (use the first),
  `params_AbusePreventionHelper = [1794213498414,"4IBUzXA07HO4LX54xGX3PZ8dJ3lvORZQ",3600000];`,
  and the JSON fields `"muid":"2612E977FA2564BD360BF17BFB0B65E8"`,
  `"sid":"21BECFA43B5769DE0F1FD7A83A796890"` and `"tid":"6af1867a9bed4b758387cea43d18ff12"`.
  The third helper element is the token lifetime in milliseconds (one hour in the observation).
- The translate response is a JSON array such as
  `[{"translations":[{"text":"Hallo Welt","to":"de"}],"usedLLM":true,"detectedLanguage":{"language":"en"}}]`.
  `translations[].text` is the result, `detectedLanguage.language` is the detected source, and extra
  fields are tolerated. The endpoint also reports errors as a JSON object under HTTP 200, for example
  `{"statusCode":401,"errorMessage":""}` for a rejected token or `{"statusCode":205,"errorMessage":""}`
  for a request it does not accept. `statusCode` may be a number or a string.

The parser is not an HTML parser and never executes JavaScript: it searches for the documented
markers, returns a fixed failure when one is absent, and never puts raw HTML, the body, the text or a
token into an error message.

## Steps

1. **Add the `bing-web` language branch.** In `src/core/languages.cpp`, next to `kYandexWeb`, add
   `constexpr QLatin1String kBingWeb("bing-web");`, a `kBingWebCodes` whitelist, a
   `kBingWebOverrides` table and a `bingWebCodeOf()` helper, and call it from `serviceCodeOf()`.
   Do not add a `LanguageInfo` column and do not change the table.

   The whitelist contains Bing's **service** codes after the overrides, observed from the upstream
   plugin's `BingLanguageMapper` translation catalogue on 2026-11-09:

   ```cpp
   constexpr const char *kBingWebCodes[] = {
       "af", "am", "ar", "az", "be", "bg", "bn", "bs", "ca", "cs", "cy", "da",
       "de", "el", "en", "et", "eu", "fa", "fi", "fil", "fr", "ga", "gu", "ha",
       "he", "hi", "hmn", "hr", "ht", "hu", "hy", "id", "is", "it", "ja", "jv",
       "ka", "km", "kn", "ko", "lo", "lt", "lv", "mk", "mn", "mr", "ms", "mt",
       "my", "nb", "ne", "nl", "pl", "pt", "pt-PT", "ro", "ru", "si", "sk",
       "sl", "sm", "so", "sq", "sr-Cyrl", "sv", "sw", "ta", "te", "th", "tr",
       "uk", "ur", "vi", "yo", "zh-Hans", "zh-Hant", "zu",
   };

   // Bing uses "pt" for Brazilian Portuguese and keeps "pt-PT" for Portugal;
   // Serbian is only accepted qualified by script. Norwegian is "nb".
   constexpr CodeOverride kBingWebOverrides[] = {
       {"pt-BR", "pt"},
       {"sr", "sr-Cyrl"},
   };
   ```

   `bingWebCodeOf(const LanguageInfo &info)` starts from `info.code`, applies the override, returns
   the whitelisted pointer when the result is in `kBingWebCodes`, and `nullptr` otherwise. The
   whitelist therefore contains `pt` and `sr-Cyrl`, not `pt-BR` and `sr`. `zh-Hans`, `zh-Hant`, `he`,
   `fil`, `jv`, `hmn`, `nb` and `sm` need no override. `ceb`, `haw`, `kk`, `pa`, `ps`, `sd`, `ky`,
   `ku`, `tg`, `tk`, `ug`, `mi`, `mg`, `gd`, `rw`, `st`, `su`, `sn`, `ny`, `xh`, `ig`, `or`, `co`,
   `fy`, `lb`, `la` and `eo` are not in the observed catalogue and return `nullptr`.
   `fromServiceCode()` and `supported()` then reverse the mapping for free. Add one synonym for a
   code Bing can return but not accept:
   `{"bing-web", "sr-Latn", "sr"}` in `kSynonyms`; a detected `sr-Cyrl` already reverses through
   `bingWebCodeOf()`.

   In `tests/tst_languages.cpp`, add `const QString bingWeb = QStringLiteral("bing-web");`.
   - Forward rows: `en`→`en`, `ru`→`ru`, `de`→`de`, `zh-Hans`→`zh-Hans`, `zh-Hant`→`zh-Hant`,
     `pt-BR`→`pt`, `pt-PT`→`pt-PT`, `nb`→`nb`, `sr`→`sr-Cyrl`, `he`→`he`, `fil`→`fil`,
     `jv`→`jv`, `hmn`→`hmn`, `ceb`→empty, `kk`→empty, `pa`→empty, `haw`→empty, `auto`→empty.
   - Reverse rows: `pt`→`pt-BR`, `pt-PT`→`pt-PT`, `nb`→`nb`, `sr-Cyrl`→`sr`, `sr-Latn`→`sr`,
     `zh-Hans`→`zh-Hans`, `zh-Hant`→`zh-Hant`, `he`→`he`, `xx`→empty.
   - `roundTrip()`: add `bingWeb` to the service list. Every declared Bing pair round-trips; no new
     exception is needed, and the existing `pt-PT`/`zh-Hant` source exception for the other services
     stays.
   - `supportedLists()`: assert the `bing-web` source and target lists are equal, non-empty, contain
     `ru`, `uk`, `tr`, `zh-Hans`, `zh-Hant`, `pt-BR`, `pt-PT`, `nb`, `sr`, `he`, `fil` and `jv`, and
     exclude `auto`, `ceb`, `kk`, `pa` and `haw`.

2. **Add the parser header and source.** Create `src/services/bingwebparser.h/.cpp` with this API
   (fixed English messages, `serviceId` empty in every result):

   ```cpp
   namespace BingWebParser {

   struct TokenPage
   {
       QString ig;       // IG:"..."
       QString iid;      // data-iid="..." (the first one)
       QString key;      // params_AbusePreventionHelper[0]
       QString token;    // params_AbusePreventionHelper[1]
       QString muid;     // "muid":"..." (optional)
       QString sid;      // "sid":"..." (optional, diagnostic only)
       QString tid;      // "tid":"..." (optional, diagnostic only)
       qint64 lifetimeMs = 3600000; // params_AbusePreventionHelper[2]
   };

   // Extracts the markers from the translator page. Returns false for a page without
   // the required markers, a truncated page, a malformed helper array or a JSON body
   // passed by mistake; errorMessage is a fixed literal without the body.
   bool parseTranslatorPage(const QByteArray &html, TokenPage *page, QString *errorMessage);

   // The MUID value from the GET's Set-Cookie header values; empty when absent.
   // The name is matched case-insensitively and exactly, so MUIDB is not MUID.
   QString muidFromSetCookieValues(const QList<QByteArray> &setCookieValues);

   // True for a JSON object with statusCode 401 (number or string): a rejected
   // token that the service may refresh exactly once. 403 is not a token rejection.
   bool isTokenRejection(const QByteArray &body);

   // A 2xx translate body: the first array element with a non-empty translations
   // array is used; translations[].text is joined and detectedLanguage.language is
   // mapped through Languages::fromServiceCode("bing-web", ..., Source).
   TranslationResult parseResponse(const QByteArray &body);

   // Recognises an HTML/interstitial page, a provider error object and a plain-text
   // rate-limit/block body. Returns TranslationError::None when the body is not a
   // recognised error.
   TranslationResult parseErrorBody(const QByteArray &body);

   TranslationError errorForHttpStatus(int status);

   } // namespace BingWebParser
   ```

   Include `core/translationtypes.h`, `core/languages.h`, `QByteArray`, `QJsonArray`,
   `QJsonDocument`, `QJsonObject`, `QJsonParseError`, `QJsonValue`, `QList` and `QString`.
   `errorResult()` returns a result with `serviceId` empty, like `MozhiParser`/`YandexWebParser`.

   Add `src/services/bingwebparser.cpp/.h` to `qtranslate_core` in `CMakeLists.txt` before
   `deeplparser.cpp` (alphabetically) and before the service file that `02` adds.

3. **Implement `parseTranslatorPage()`.** Use tolerant string searches, not
   `QRegularExpression` or an HTML parser:
   - `IG:"` is the `IG` marker; the value is the bytes up to the next `"`.
   - `data-iid="` is the IID marker; the value is the bytes up to the next `"`. The live page has
     several occurrences (`translator.5023`, `translator.5026`, …); use the first.
   - `params_AbusePreventionHelper = ` is the helper marker; the array ends at the next `;`. Parse
     that substring with `QJsonDocument` and require an array of at least two elements. Element 0 is
     the key: if it is a JSON number, format the integer digits with
     `QString::number(static_cast<qint64>(value.toDouble()))` (never `QVariant::toString()`, which
     can switch to scientific notation and corrupt a 13-digit key); if it is a string, use it.
     Element 1 is the token string. Element 2, when it is a non-negative number, is `lifetimeMs`;
     otherwise keep the 3600000 default. Trim all values and reject empty key/token.
   - `"muid"`, `"sid"` and `"tid"` are optional JSON fields in the page. Find `"<name>":`, skip
     ASCII whitespace, expect `"`, and read up to the next `"`. A missing field leaves the value
     empty; it is not a failure, because the service may get `MUID` from `Set-Cookie`.
   - Return `false` with the fixed message `the token page is missing required parameters` when
     `IG`, `IID`, the key or the token is absent/empty, the helper JSON is malformed or shorter than
     two elements, or the page is truncated. The message must not contain `<`, `{`, `}`, the page,
     or any extracted value.
   - A JSON translate response passed to this function has none of the markers and therefore fails
     with the same message; test that case explicitly.

4. **Implement `muidFromSetCookieValues()`.** For each value, split on `;` and `,`, trim each
   segment, split at the first `=`, compare the name case-insensitively with `MUID` (the exact name,
   so `MUIDB` does not match), and return the first value trimmed. Return an empty string when no
   value contains `MUID`. This handles a single `Set-Cookie` value and a Qt-merged list of cookies.

5. **Implement the translation and error parsing.**
   - `isTokenRejection()`: parse a JSON object and read `statusCode`; accept a number or a string;
     return true only when its trimmed text is `401`. Return false for a 403, an HTML body, a
     malformed body and a missing status.
   - `parseErrorBody()` order:
     - a trimmed body starting with `<` → `ServiceUnavailable` with
       `the endpoint returned an HTML page`;
     - a JSON object with `statusCode` and/or `errorMessage`: `401` → `RateLimited` with
       `the endpoint rejected the token`; `403`/`429` → `RateLimited` with
       `the endpoint reported a rate limit`; `413` → `TooLong`; `5xx` → `Network`; any other
       status or an `errorMessage` without a status → `ServiceUnavailable` with
       `the endpoint returned an error`; a missing/empty pair → `None`;
     - a JSON array, an empty body or malformed JSON → `None`;
     - a plain-text body → the same keyword classification: `rate limit`, `too many requests`,
       `quota`, `status code: 429`/`403` → `RateLimited`; `captcha`, `blocked`, `forbidden`,
       `access denied`, `banned`, `robot`, `unavailable` → `ServiceUnavailable`; anything else →
       `None` so the HTTP status decides.
   - `parseResponse()` first returns the result of `parseErrorBody()` when it reports an error, so a
     200 provider error object is not treated as a success. Otherwise parse the JSON: a malformed
     document, a non-array root, an empty array, no element with a non-empty `translations` array, a
     non-object translation, or a missing/non-string/empty `text` becomes `BadResponse` with
     `no translated text in the response`. Use the first array element with at least one translation,
     join all `translations[].text` values in order, read the optional
     `detectedLanguage.language` string and map it with
     `Languages::fromServiceCode("bing-web", ..., Languages::Direction::Source)`; an unknown or
     missing language stays empty. Ignore `to`, `usedLLM` and every other field. On success leave
     `serviceId` and `errorMessage` empty.
   - `errorForHttpStatus()` maps `401`/`403`/`429` → `RateLimited`, `413` → `TooLong`, `5xx` →
     `Network`, and every other non-2xx status → `BadResponse`. Every message is a fixed English
     literal and contains no body, text, URL, `<`, `{`, `}` or token.

6. **Add the parser fixtures.** Create `tests/fixtures/bingweb/` with a trimmed
   `bing_translator_page.html` that contains the observed markers from the 2026-11-09 page (fake
   values are fine; no live token or cookie) and these variants:
   `translator_page_no_muid.html` (same page without `muid`/`sid`/`tid`),
   `translator_page_missing_ig.html`, `translator_page_missing_iid.html`,
   `translator_page_truncated.html`, `translator_page_bad_helper.html`,
   `translator_page_short_helper.html`, `response.json` (the live shape with `usedLLM` and a detected
   language), `response_multi.json`, `response_no_detected.json`, the detected-language rows
   (`response_pt.json`, `response_pt_pt.json`, `response_nb.json`, `response_sr_cyrl.json`,
   `response_sr_latn.json`, `response_zh_hans.json`, `response_zh_hant.json`, `response_he.json`,
   `response_unknown_language.json`, `response_missing_detected.json`), the error bodies
   (`error_401.json`, `error_401_string.json`, `error_403.json`, `error_429.json`,
   `error_205.json`, `error_500.json`, `error_message_only.json`, `error_unknown.json`), and the bad
   responses (`response_empty_array.json`, `response_empty_translations.json`,
   `response_missing_text.json`, `response_non_string_text.json`, `response_missing_translations.json`,
   `response_object.json`, `response_string.json`, `response_malformed.json`, `response_html.html`,
   `rate_limit.txt`, `blocked.txt`, `plain.txt`). Use the `fixture()` helper and the
   `QTRANSLATE_FIXTURES_DIR` pattern from `tests/tst_mozhiparser.cpp`.

   The saved page is a trimmed reproduction of the marker shape, not the full 600 KB live page; do
   not commit a live page, a real token or a real `MUID`. The fixture contains a `SECRET` sentinel in
   the body text so the message-hygiene assertion can prove it is not leaked.

7. **Add `tst_bingwebparser`.** Create `tests/tst_bingwebparser.cpp` with
   `Q_DECLARE_METATYPE(TranslationError)` and cover:
   - the full token-page extraction (`IG`, first IID, key digits, token, MUID, SID, TID,
     `lifetimeMs == 3600000`); the optional fields present and absent; the key as a JSON number and
     as a string; a wrong/missing marker, a truncated page, a malformed/short helper and a JSON
     translate body → `false` with the fixed message;
   - `muidFromSetCookieValues()` with a single `MUID` value, a `MUIDB` value before the real one, a
     merged multi-cookie value, a case-insensitive name and an absent value;
   - `parseResponse()` with the live shape, `usedLLM`/`to`/extra fields and a second array element
     ignored, multiple translations joined, the detected-language rows above, and every bad response
     above → `BadResponse`;
   - `isTokenRejection()` for numeric/string 401, 403, HTML and malformed bodies;
   - `parseErrorBody()` for HTML, the 401/403/429/205/500/unknown/message-only objects, the plain-text
     rate-limit and block bodies, and the plain-text "not an error" fallback;
   - `errorForHttpStatus()` for 401/403/429, 413, 500/503 and 400;
   - a helper that asserts every error message is non-empty and contains no `SECRET`, no `<`, no
     `{`, no `}` and no `://`.
   Add `qtranslate_add_test(tst_bingwebparser)` to `tests/CMakeLists.txt` in the platform-neutral
   block after `tst_appinfo`, so the list stays alphabetical; no new platform conditional.

8. **Build, count and verify.** Configure/build Linux Debug and Release with `-Wall -Wextra`; run
   `tst_bingwebparser` and `tst_languages`, then the full suite with
   `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`. `ctest -N` must show 36
   Linux tests (35 + the new target); Windows would be 31 (30 + 1). Record 36/31 in the outcome.
   Windows/MSVC is unavailable: record the Windows build and its 31 tests as not performed.
   `git diff --stat` may show only `src/core/languages.cpp`, `tests/tst_languages.cpp`,
   `src/services/bingwebparser.*`, `CMakeLists.txt`, `tests/tst_bingwebparser.cpp`,
   `tests/CMakeLists.txt` and the new fixtures. Do not touch the service, `AppController`, the
   settings UI, `.ts`, `doc/`, `AGENTS.md`, the READMEs, `doc/handoff.md`, the root plan or
   `07-fallback-chain`. Leave commit and push to the coordinator/lead.

## Done when

- The `bing-web` language branch maps `pt-BR`→`pt`, `pt-PT`→`pt-PT`, `sr`→`sr-Cyrl`, `nb`→`nb`,
  `zh-Hans`/`zh-Hant` unchanged, `he`/`fil`/`jv`/`hmn`/`sm` unchanged and the observed catalogue
  codes; `ceb`, `kk`, `pa`, `haw` and every other code outside the whitelist are not declared;
  `fromServiceCode()` reverses `pt`→`pt-BR`, `pt-PT`→`pt-PT`, `sr-Cyrl`/`sr-Latn`→`sr`,
  `nb`→`nb`, `zh-Hans`/`zh-Hant` unchanged; `toServiceCode("bing-web", ..., Source/Target)` is empty
  for an unsupported code; no `LanguageInfo` column is added, and `tst_languages` covers the forward,
  reverse, round-trip and supported-list rows.
- `BingWebParser` exists under `src/services/`, is listed in `qtranslate_core` and has the API above.
  `parseTranslatorPage()` extracts `IG`, the first `data-iid`, the helper key/token/lifetime, `MUID`,
  `SID` and `TID` from the saved page, preserves a numeric key's digits, treats `MUID`/`SID`/`TID` as
  optional, and returns the fixed failure for a page without the markers, a truncated page, a
  malformed helper or a JSON translate body.
- `muidFromSetCookieValues()` finds the exact `MUID` name in single and merged `Set-Cookie` values,
  does not match `MUIDB`, and returns empty when absent.
- `parseResponse()` reads the first array element with a non-empty `translations` array, joins
  `translations[].text`, reads `detectedLanguage.language`, maps it through the `bing-web` branch,
  tolerates `to`, `usedLLM`, extra fields and a second element, and returns `BadResponse` for a
  malformed document, a non-array root, an empty array, no translations, a non-object translation and
  a missing/non-string/empty `text`.
- `parseErrorBody()`, `isTokenRejection()` and `errorForHttpStatus()` implement the documented
  mapping: HTML → `ServiceUnavailable`; a provider error object 401/403/429 → `RateLimited`, 413 →
  `TooLong`, 5xx → `Network`, any other status/message → `ServiceUnavailable`; `isTokenRejection()`
  is true only for a 401 body; plain-text rate-limit/block bodies are classified; a JSON array,
  malformed/empty body and an unrecognised plain text return `None`; unknown non-2xx statuses are
  `BadResponse`; every message is a fixed literal without the page, text, body, URL or token.
- `tst_bingwebparser` covers all the cases above with the saved fixtures; the message-hygiene helper
  passes; `tst_languages` covers the Bing rows; the new target is registered in
  `tests/CMakeLists.txt` and is platform-neutral.
- Debug and Release build on Linux without warnings; `ctest -N` shows 36 Linux tests and would show
  31 on Windows; the full suite passes 36/36; Windows Debug/Release and its 31 tests are recorded as
  not performed (no MSVC).
- No service, `AppController`, settings UI, `.ts`, `doc/`, `AGENTS.md`, README, `doc/handoff.md`,
  root-plan or `07-fallback-chain` file is changed; no commit is made.

## Risks

- Bing's HTML is the most fragile part of the stage. A marker can move, change quoting or disappear
  between sessions. Use tolerant string searches with a clear fixed failure; do not add a general
  HTML parser, a JavaScript engine or a regex that assumes one exact surrounding script. If the live
  page no longer contains the markers, the parser is still correct: it reports the fixed error and
  the service will return `ServiceUnavailable`.
- The helper key is a JSON number (`1794213498414`). `QVariant::toString()`/`QJsonValue::toVariant()`
  can render a large double in scientific notation; convert through `qint64` explicitly and test the
  exact digits.
- `MUID` and `MUIDB` are different cookies. Match the name exactly; a prefix match would send a
  wrong cookie and fail the handshake in a way that is hard to diagnose.
- `statusCode` arrives as both a number and a string, and errors arrive under HTTP 200. The token
  rejection is a 401 body; a 403 is a block per the node's rule, even though the upstream plugin
  refreshes on a 403. Do not conflate them; `isTokenRejection()` is the single fix point if the live
  behaviour changes.
- `QJsonDocument` is only used on the helper array and the response, never on the whole page. A
  truncated page must not crash or read out of bounds; use `indexOf` results with explicit checks.
- The fixture is a trimmed reproduction, not the full live page. Keep it small, fake and stable; do
  not commit a real token, cookie or the 600 KB page. A live smoke belongs to `02-bing-web-service`,
  not to this parser task.
- Windows Debug/Release and its 31 tests are unavailable on this Linux machine; record them as not
  performed, not as passed.
