---
kind: task
status: todo
detailed: true
depends: [01-proofreading-core]
---

# LanguageTool response parser

## Task

Turn a LanguageTool `/v2/check` response into `ProofreadingIssue` values and add the `languagetool`
branch to `Languages`, without any network code. This is the parser of the only provider of the
stage (user decision of 2026-11-09); the HTTP service, the endpoint, the chunking, the category
filtering and the `AppController` registration are `03-languagetool-service`, and the settings UI is
`04-text-checking-tab`. This child adds `src/services/languagetoolparser.{h,cpp}`, fixtures,
`tst_languagetoolparser`, the language mapping and `tst_languages` rows; it changes no settings, no
UI, no `.ts` file and no documentation.

The parser follows the pure-function style of `MyMemoryParser` (`src/services/mymemoryparser.cpp`):
no logging, no network, fixed English messages, and a result type that carries either issues or an
error. The signature is
`ProofreadingResult parseResponse(const QByteArray &body, int textLength)`: `textLength` is the
UTF-16 length of the checked text (the chunk, when `03-languagetool-service` splits long text) and
lets the parser reject an issue that is outside it instead of handing the UI a broken range. The
parser returns the issues in the server order, never filters by the requested categories (the
service applies "empty = all" once, per the `01-proofreading-core` outcome) and never shifts offsets
(the service adds the chunk offset). A second pure helper,
`QString categoryFor(const QString &issueType, const QString &categoryId)`, exposes the category
mapping so `tst_languagetoolparser` can check every row directly.

Facts confirmed against the public API on 2026-11-09 (recorded here so the implementation does not
need to re-discover them):

- `POST https://api.languagetool.org/v2/check` with `text` and `language` returns HTTP 200 and a JSON
  object with `software`, `warnings`, `language` and `matches[]`; a match has `message`,
  `shortMessage`, `replacements[].value`, `offset`, `length`, `context.{text,offset,length}`,
  `sentence`, `type` and `rule.{id,description,issueType,category.{id,name}}`.
- Offsets are UTF-16 code units. `text=😀 This are a test.` returned `offset=3`, `length=8` for
  `This are` — the emoji counts as two units (a code-point count would give 2), which matches
  `ProofreadingIssue`/`QString` exactly, so no conversion is added.
- Observed `rule.issueType` values include `grammar`, `misspelling`, `whitespace` (for
  `COMMA_PARENTHESIS_WHITESPACE`), `typographical` (for `CONSECUTIVE_SPACES` and
  `UPPERCASE_SENTENCE_START`) and `style`; observed `rule.category.id` values include `GRAMMAR`,
  `TYPOS`, `TYPOGRAPHY`, `REDUNDANCY` and `CASING`. The complete `issueType` vocabulary is the
  ITS 2.0 list from `ITSIssueType` (`misspelling`, `typographical`, `whitespace`, `grammar`,
  `duplication`, `inconsistency`, `style`, `register`, `non-conformance`, `locale-violation`,
  `locale-specific-content`, `uncategorized`, `other`, …), not the two-value guess.
- An empty `text` is HTTP 200 with an empty `matches`; HTTP 413 (text over 20 000 characters) and
  HTTP 400 (unknown language) return **plain-text** bodies (`Error: …`), and a 400 can repeat a
  submitted credential value in its body (a live credential attempt did). Therefore nothing from a
  response body may reach `errorMessage` or a log line.

The category mapping is a documented product simplification, not a LanguageTool field:
`rule.category.id` is checked first because it is more precise (`CASING` is reported as
`typographical`, but it is a style issue, not punctuation), then `rule.issueType`, then `grammar`:

| `rule.category.id` | `category` |
|---|---|
| `TYPOS` | `spelling` |
| `PUNCTUATION`, `TYPOGRAPHY` | `punctuation` |
| `CASING`, `STYLE`, `REDUNDANCY`, `REGISTER` | `style` |
| `GRAMMAR`, `MISC`, `SEMANTICS`, `CONFUSED_WORDS`, `COLLOCATIONS` | `grammar` |

| `rule.issueType` | `category` |
|---|---|
| `misspelling` | `spelling` |
| `typographical`, `whitespace` | `punctuation` |
| `style`, `register`, `non-conformance`, `locale-violation`, `locale-specific-content`, `inconsistency` | `style` |
| every other value, and an absent field | `grammar` |

`severity` stays `0`: the v2 response has no severity field and `rule.confidence` (0…1) is a
confidence, not a severity; it is not mapped. `issue.message` is the provider's display text — it may
quote words from the checked text, which is expected and is not a leak; only `errorMessage` and any
log line are held to the "no text" rule, and this parser does not log at all.

`Languages` gains the `languagetool` service id in `supported()`, `toServiceCode()` and
`fromServiceCode()` (`src/core/languages.cpp:299-320` and `:371-411`), using the `longCode` values of
the public `/v2/languages` response of 2026-11-09. The internal code maps to the main-region variant
so a rule set is deterministic (the design note uses `ru-RU`): `en`→`en-US`, `de`→`de-DE`,
`fr`→`fr-FR`, `es`→`es-ES`, `it`→`it-IT`, `nl`→`nl-NL`, `sv`→`sv-SE`, `fa`→`fa-IR`, `pt-BR`→`pt-BR`,
`pt-PT`→`pt-PT`, `zh-Hans`→`zh-CN`, `ru`→`ru-RU`, `uk`→`uk-UA`, `pl`→`pl-PL`, `ja`→`ja-JP`,
`da`→`da-DK`, `el`→`el-GR`, `ro`→`ro-RO`, `sk`→`sk-SK`, `sl`→`sl-SI`, `ta`→`ta-IN`, `km`→`km-KH`,
`be`→`be-BY`, `ca`→`ca-ES`, `gl`→`gl-ES`, `ga`→`ga-IE`, `fil`→`tl-PH`, `nb`→`nb`, `ar`→`ar`,
`eo`→`eo`. LanguageTool does not list `zh-Hant`, `hi`, `ko`, `tr`, `vi`, `sr`, `hr`, `cs` or `hu`, so
those internal codes are absent from its catalogue rather than mapped to an approximate language.
`auto` is excluded from `supported()` and handled by the service as the request language `auto`.

## Steps

1. **Add `src/services/languagetoolparser.{h,cpp}`.**
   - Header: `namespace LanguageToolParser` with the three declarations and comments that state the
     four-value `category` contract, the UTF-16 rule, the fixed-message rule and the "the parser
     returns every match, the service filters and shifts" boundary. Include
     `core/proofreadingtypes.h`; do not include `QJson*` in the header.
   - `QString categoryFor(const QString &issueType, const QString &categoryId)`: implement the two
     tables from the task text, comparing trimmed, case-insensitive values; return one of
     `spelling`, `grammar`, `punctuation`, `style` (`QStringLiteral`), default `grammar`.
   - `ProofreadingResult parseResponse(const QByteArray &body, int textLength)`:
     * `body.trimmed().startsWith('<')` → `ServiceUnavailable` with
       `the endpoint returned an HTML page` (the wording of `mymemoryparser.cpp:63-65`);
     * `QJsonDocument::fromJson()` failure or a non-object root → `BadResponse` with
       `response is not a LanguageTool response`;
     * `matches` must exist and be an array; anything else → `BadResponse` with
       `no matches array in the response`;
     * for each entry: it must be an object (otherwise `BadResponse` with
       `a match is not an object`), `offset` and `length` must be integral JSON numbers
       (`isDouble()` and a whole value), `offset >= 0`, `length > 0` and
       `offset + length <= textLength` → otherwise `BadResponse` with
       `a match has an invalid range` (one fixed message for both a bad range and a bad type, and no
       body content); `message` is the string `message`, falling back to `shortMessage`;
       both absent/empty → `BadResponse` with `a match has no message`; `replacements` is an array
       whose object elements contribute their string `value` in order (other elements are skipped, a
       missing array is an empty list); `rule.id` → `ruleId` (empty when absent);
       `rule.issueType` and `rule.category.id` feed `categoryFor()`; `severity` stays `0`;
     * set `serviceId = "languagetool"` on success and on every error; leave `correctedText` empty
       and `errorMessage` empty on success; a valid response with an empty `matches` array is a
       successful result with no issues; ignore `software`, `warnings`, `language`, `context`,
       `sentence` and `type` (the `context.offset` must never be used instead of the top-level
       `offset`).
   - `TranslationError errorForHttpStatus(int status)`: 401 and 403 → `Auth`; 413 → `TooLong`;
     429 → `RateLimited`; 5xx → `Network`; everything else → `BadResponse`. Do not classify by body
     text and do not add a message helper — the service owns the `HTTP %1` wording, as
     `src/services/mymemoryservice.cpp:203` does.
   - A file-local `errorResult(TranslationError, const QString &)` helper like
     `mymemoryparser.cpp:14-20`; no logging category; no `tr()`.
   - Add `src/services/languagetoolparser.cpp` and `.h` to `qtranslate_core` in `CMakeLists.txt`
     between `googletranslateservice.*` (line 62) and `mozhiparser.*` (line 63).

2. **Add the `languagetool` branch to `src/core/languages.cpp`.**
   - Declare `constexpr QLatin1String kLanguageTool("languagetool");` next to the other service ids
     (`languages.cpp:154-162`).
   - Add a `constexpr CodeOverride kLanguageToolCodes[]` table after the `CodeOverride` definition
     (line 166) and near `kMyMemoryOverrides` (line 172), with the 30 pairs from the task text and a
     comment that these are the `longCode` values of `https://api.languagetool.org/v2/languages` as
     of 2026-11-09 and that the regional variant is the deliberate default. Sort it by internal code
     and keep one pair per line.
   - Add `const char *languageToolCodeOf(const LanguageInfo &info)` that scans the table and returns
     `nullptr` when the internal code is absent (the `reversoCodeOf()` shape,
     `languages.cpp:290-297`).
   - Add `if (serviceId == kLanguageTool) return languageToolCodeOf(info);` in `serviceCodeOf()`
     (`languages.cpp:299-320`). No synonyms are needed: every value is unique, so
     `fromServiceCode()` reverses it and `auto` is not one of them.
   - Do not change `languages.h`, the catalogue, `closest()` or any other service branch.

3. **Add the fixtures in `tests/fixtures/languagetool/`.**
   Synthetic responses only (no live text), shaped from the 2026-11-09 check; use `SECRET` in
   `context.text`/`sentence` so a leak is visible:
   - `normal.json` — four matches: `misspelling`/`TYPOS` with three replacements, `grammar`/`GRAMMAR`
     with one, `whitespace`/`TYPOGRAPHY` with one, `style`/`REDUNDANCY` with an empty `replacements`;
     `software`, `warnings` and `language` present to prove they are ignored;
   - `empty_matches.json` — `"matches": []`;
   - `emoji.json` — a match after an emoji, with the same text used by the test;
   - `missing_offset.json`, `bad_range.json` (`offset + length` past the text), `zero_length.json`,
     `missing_message.json`, `match_not_object.json`, `no_matches_array.json`,
     `matches_not_array.json`, `malformed.json`, `html.html`;
   - `casing.json` — `issueType: typographical`, `category.id: CASING`;
   - `missing_issue_type.json` — no `issueType`, `category.id: TYPOS`;
   - `unknown_issue_type.json` — an `issueType` outside the table;
   - `context_offset_differs.json` — `context.offset` smaller than the top-level `offset`, so using
     the wrong field fails the test.
   Keep every fixture small and hand-written; do not copy real user text.

4. **Add `tests/tst_languagetoolparser.cpp`.**
   - Copy the `fixture()` and `verifyFixedMessage()` helpers from
     `tests/tst_mymemoryparser.cpp:10-30` (`verifyFixedMessage` must reject `SECRET`, `<` and `://`).
   - `categoryFor_data`/`categoryFor` rows for every table entry, the `CASING` case, a missing
     `issueType` with a known category, an unknown pair and two empty strings.
   - `parseResponse` success cases: `normal.json` (count, order, every field, ordered replacements,
     `serviceId == "languagetool"`, `ok()`, empty `correctedText`, `severity == 0`), `empty_matches`,
     `context_offset_differs` (the top-level offset is used), `missing_issue_type` (spelling from the
     category), `unknown_issue_type` (grammar).
   - UTF-16: the `emoji.json` text passed to `parseResponse(body, text.size())`; assert
     `text.mid(issue.offset, issue.length)` equals the expected word and record the live evidence
     (`😀 This are a test.` → offset 3, length 8) in a comment.
   - Error cases: `html.html` → `ServiceUnavailable`; `malformed.json`, `no_matches_array`,
     `matches_not_array`, `match_not_object`, `missing_offset`, `bad_range`, `zero_length`,
     `missing_message` → `BadResponse`; every case calls `verifyFixedMessage()` and
     `QVERIFY(!result.errorMessage.contains(QStringLiteral("SECRET")))`; `serviceId` is set on the
     error result too.
   - `errorForHttpStatus_data`/`errorForHttpStatus` rows: 401 and 403 → `Auth`, 413 → `TooLong`,
     404 → `BadResponse`, 429 → `RateLimited`, 500 and 503 → `Network`, 400 → `BadResponse`.
   - The parser has no logging category, so there is no message handler test; instead assert that the
     checked text and the `SECRET` context never appear in `errorMessage`.
   - Register `qtranslate_add_test(tst_languagetoolparser)` in `tests/CMakeLists.txt` after
     `qtranslate_add_test(tst_languages)` (line 34) and before `tst_mozhiparser` (line 35);
     platform-neutral, no display and no server.

5. **Extend `tests/tst_languages.cpp`.**
   - Add `const QString languageTool = QStringLiteral("languagetool");` to the namespace constants.
   - Add `languagetool` to the service loop in `roundTrip()` (lines 269-288); no exception is needed
     because every LT code is unique and `zh-Hant` is not in its catalogue.
   - `toServiceCode_data`: rows for `en`→`en-US`, `de`→`de-DE`, `fr`→`fr-FR`, `es`→`es-ES`,
     `it`→`it-IT`, `nl`→`nl-NL`, `sv`→`sv-SE`, `fa`→`fa-IR`, `ru`→`ru-RU`, `uk`→`uk-UA`,
     `pl`→`pl-PL`, `pt-BR`→`pt-BR`, `pt-PT`→`pt-PT`, `zh-Hans`→`zh-CN`, `ja`→`ja-JP`, `ar`→`ar`,
     `fil`→`tl-PH`, `nb`→`nb`, plus empty results for `zh-Hant`, `hi`, `ko`, `tr`, `vi`, an unknown
     code and `auto`.
   - `fromServiceCode_data`: `ru-RU`→`ru`, a mixed-case row, `zh-CN`→`zh-Hans`, `tl-PH`→`fil`,
     `en-US`→`en`, `de-DE`→`de`, `pt-PT`→`pt-PT`, and empty for `auto`/an unknown code.
   - `supportedLists`: both directions equal; the set equals the 30 expected internal codes; it
     contains `ru`, `uk`, `en`, `de`, `fr`, `es`, `pl`, `pt-BR`, `pt-PT`, `zh-Hans`, `ja`, `ar`,
     `fil`, `nb`; it does not contain `auto`, `zh-Hant`, `hi`, `ko`, `tr`, `vi`, `sr`, `cs`, `hu`.
   - The existing `closest()` cases stay as they are; no new case is required.

6. **Build, count and verify.** Configure and build Linux Debug and Release with `-Wall -Wextra`
   (`build/linux-debug`, `build/linux-release`), run `tst_languagetoolparser`, `tst_languages` and
   then `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`. `ctest -N` must show
   41 Linux tests (40 + the new platform-neutral target); Windows would be 36 (35 + 1) and is
   recorded as not performed (no MSVC). `grep -c '<message' i18n/qtranslate_ru_RU.ts` must stay 316.
   `git diff --stat` may show only `src/services/languagetoolparser.{h,cpp}`,
   `src/core/languages.cpp`, `CMakeLists.txt`, `tests/tst_languagetoolparser.cpp`,
   `tests/tst_languages.cpp`, `tests/CMakeLists.txt` and `tests/fixtures/languagetool/*`; no
   service, settings, UI, `.ts`, documentation, README, `doc/handoff.md` or roadmap file beyond this
   node's outcome is touched. Leave commit and push to the lead.

## Done when

- `src/services/languagetoolparser.{h,cpp}` exists, is in `qtranslate_core` between
  `googletranslateservice.*` and `mozhiparser.*`, has no network and no logging, and exposes
  `categoryFor()`, `parseResponse(const QByteArray &, int textLength)` and
  `errorForHttpStatus(int)`.
- `parseResponse()` returns every match in server order with `offset`, `length`, `message`
  (`shortMessage` as the fallback), ordered `replacements`, `ruleId`, `category` and
  `severity == 0`; `serviceId` is `languagetool` on success and on errors; `correctedText` and
  `errorMessage` are empty on success; a valid empty `matches` array is a successful result with no
  issues; `software`, `warnings`, `language`, `context`, `sentence` and `type` are ignored, and
  `context.offset` is never used instead of the top-level `offset`.
- The category mapping matches the two tables exactly and is covered by the data-driven test:
  `misspelling`/`TYPOS` → `spelling`; `whitespace`/`TYPOGRAPHY` and `typographical`/`TYPOGRAPHY` →
  `punctuation`; `typographical`/`CASING`, `style`, `register` → `style`; `grammar`/`GRAMMAR` and an
  unknown/missing pair → `grammar`. No other value is returned.
- Offsets are UTF-16: with the emoji fixture `text.mid(offset, length)` selects the expected word,
  and the live evidence (`😀 This are a test.` → offset 3, length 8) is recorded in the test or the
  outcome. A match with a bad type, a negative `offset`, a non-positive `length` or
  `offset + length > textLength` is `BadResponse` with the fixed message, never a clamped range.
- Error handling: an HTML body is `ServiceUnavailable`; malformed JSON, a non-object root, a missing
  or non-array `matches`, a non-object match and a match without a message are `BadResponse`; 401/403
  → `Auth`, 413 → `TooLong`, 429 → `RateLimited`, 5xx → `Network`, 400 and other statuses →
  `BadResponse`. Every `errorMessage` is a fixed literal that contains no checked text, no
  `context`/`sentence` content, no `SECRET`, no URL and no key; the parser does not log.
- `Languages::supported("languagetool", Direction::Source)` and the target list are equal, contain
  the 30 mapped internal codes and no `auto`; `toServiceCode()` returns the documented `longCode`
  values (`en-US`, `de-DE`, `zh-CN`, `ru-RU`, `tl-PH`, …) and an empty string for `zh-Hant`, `hi`,
  `ko`, `tr`, `vi`, an unknown code and `auto`; `fromServiceCode()` reverses the mapped codes,
  case-insensitively; `tst_languages` has the forward/reverse rows and the `roundTrip()` loop covers
  `languagetool`.
- `tst_languagetoolparser` is new, platform-neutral, registered after `tst_languages`, covers all the
  cases above with the fixtures, and `ctest -N` shows 41 Linux tests (40 + 1) and would show 36 on
  Windows (35 + 1). Debug and Release build on Linux without `-Wall -Wextra` warnings and the full
  suite passes 41/41; Windows Debug/Release and its 36 tests are recorded as not performed (no MSVC).
- No settings, service, UI, `.ts` (316 messages stay), documentation, README, `doc/handoff.md` or
  later-child file changes; no commit.

## Depends on

`01-proofreading-core` for `ProofreadingIssue`/`ProofreadingResult` and the category ids, and for the
"empty = all" rule that the service, not the parser, applies.

## Risks

- **The category mapping is a product simplification.** LanguageTool has ~27 ITS issue types and
  dozens of category ids; the contract has four buckets. The category-first order is deliberate
  (`CASING` is reported as `typographical` but is a style issue), and the table is fixed here so the
  UI filter (child 04) and the parser agree. If a live response shows a category id that is clearly
  misfiled, change the table and the test in one place; do not spread the mapping over the service.
- **UTF-16 is confirmed for the public server, not for every self-hosted build.** A very old
  LanguageTool that counted code points would produce smaller offsets after a surrogate pair and the
  range check cannot detect it (the offset is still inside the text). The emoji fixture and the
  recorded live check cover the supported public API; a self-hosted mismatch is a documented
  limitation, not a conversion heuristic.
- **A response body can echo credentials.** A live 400 repeated the submitted credential value in
  `Error: …`. The parser must never copy a body into `errorMessage`, and the test asserts it.
  Stage 5 sends no credentials (user decision of 2026-11-09), so the application never submits a
  key; the parser still must not surface a body that a proxy or a future server could fill with
  anything.
- **The response can contain fields that look useful but are not.** `warnings.incompleteResults` is
  ignored (the contract has no field for it), `rule.confidence` is not a severity, and
  `language.detectedLanguage` is not meaningful for an explicit `language` request (a live check
  returned `en-US` for German text). Do not map any of them: `ProofreadingResult` has no
  detected-language field, so the parser discards `language` entirely.
- **Offsets are chunk-local.** The parser is called once per request and returns the offsets it
  received; `03-languagetool-service` adds the chunk offset and sorts the merged list. Do not sort,
  de-duplicate or clamp here, and do not assume the whole document.
- **Overlapping and duplicate matches are kept.** LanguageTool can return two rules on the same
  range; the UI must be able to show both, so the parser preserves the array.
- **403 is ambiguous.** It can be a rejected key or a blocked client; the stage's service mapping
  (401/403 → `Auth`) is kept here. If the service later distinguishes a block, it changes the
  service, not the parser's status table.
- **Windows is not built here.** Record the count and the build as not performed, never as passed.
