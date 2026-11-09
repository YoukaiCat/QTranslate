---
kind: task
status: todo
depends: [01-proofreading-core]
---

# LanguageTool response parser

## Task

Turn a LanguageTool `/v2/check` response into `ProofreadingIssue` values and add the `languagetool`
branch to `Languages`, without any network code. The parser is the fragile layer of the first
provider: `matches[]` gives `offset`, `length`, `message`, `replacements`, `rule`, `context`, and
`doc/proofreading.md` §3/§7 requires the offsets to select the same `QString` text, the categories to
be one of the four contract values, and the error bodies (HTML, JSON `message`, 429/403/5xx) to be
classified with fixed messages. This child adds `src/services/languagetoolparser.{h,cpp}`, synthetic
fixtures and `tst_languagetoolparser`, plus the language codes; it does not add the service, the
settings UI, `.ts` strings or documentation.

The parser follows the pure-function style of `MyMemoryParser`/`BingWebParser`:
`parseResponse(const QByteArray &, const QStringList &requestedCategories)` returning
`ProofreadingResult` with `serviceId = "languagetool"`, and `errorForHttpStatus(int)` for the status
mapping. The category mapping is fixed here and documented in the header: `rule.issueType`
`misspelling` → `spelling`, `typographical` → `punctuation`, `grammar` → `grammar`,
`style`/`register`/`non-conformance` → `style`, everything else and a missing `issueType` →
`grammar`; `severity` stays `0` because the v2 response has no severity field. Issues whose mapped
category is not in the requested list are dropped by the service, not by the parser, so the parser
always returns the full response.

`Languages` gains the `languagetool` service id in `supported()`, `toServiceCode()` and
`fromServiceCode()` (`src/core/languages.h:33-42`), using the documented `/v2/languages` codes for
the project's languages (`en-US`, `en-GB`, `de-DE`, `ru-RU`, `uk-UA`, `fr`, `es`, `pl-PL`, `pt-BR`,
`pt-PT`, `zh-CN`, `ja-JP`, `ar`, …) with `auto` excluded from `supported()`. The exact list is taken
from the language table in `doc/proofreading.md` §3 and the live `/v2/languages` response, and is
recorded in the child's outcome; an undocumented code must not be invented.

## Done when

- `src/services/languagetoolparser.{h,cpp}` parse a success fixture with several matches: every
  issue carries `offset`, `length`, `message`, the ordered `replacements`, `ruleId` and the mapped
  `category`, and `serviceId` is `languagetool`; `correctedText` stays empty.
- A fixture with an emoji or another surrogate pair before the match proves that `offset`/`length`
  select the same substring of the `QString` input; a fixture with a match at the start and one at
  the end covers the boundaries; an empty `matches` array is a successful result with no issues.
- Malformed JSON, an HTML page, a non-object body and a missing `matches` array are `BadResponse`
  with a fixed message; HTTP 401/403 map to `Auth`, 413 to `TooLong`, 429 to `RateLimited`, 5xx to
  `Network` and a JSON `message` from the server is classified without echoing the checked text; no
  message or log line contains the text, a replacement, the URL or a key.
- `Languages::supported("languagetool", Direction::Source/Target)` returns a non-empty list without
  `auto`, `toServiceCode()`/`fromServiceCode()` round-trip the documented codes, and `auto` is not a
  supported code; `tst_languages` has the forward/reverse rows.
- `tests/tst_languagetoolparser.cpp` (new, platform-neutral, registered after `tst_languages` and
  before `tst_mozhiparser` in `tests/CMakeLists.txt`) covers the cases above with fixtures under
  `tests/fixtures/` that contain `SECRET` sentinels where a text would appear; `ctest -N` shows 41
  Linux tests and would show 36 on Windows. Debug and Release build on Linux without warnings and the full suite passes; Windows is
  recorded as not performed. No service, settings, UI, `.ts`, documentation or README file changes.

## Depends on

`01-proofreading-core` for `ProofreadingIssue`/`ProofreadingResult` and the category ids.
