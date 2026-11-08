---
kind: task
status: done
detailed: true
---

# Byte splitter and MyMemory language codes

## Task

Add the two shared prerequisites for `02-mymemory-service` without adding the service, UI or
documentation: a UTF-8 byte-aware split in `core/textchunker` and the MyMemory language-code branch
in `core/languages`. Both are independently reviewable and keep the existing `split()` and the
existing service tables unchanged.

`TextChunker::split()` cuts by UTF-16 code units, which is wrong for the 500-byte MyMemory limit on
Cyrillic, Chinese and emoji. Add `splitUtf8(text, maxBytes)` as a separate function: each part's
`toUtf8().size()` is at most `maxBytes`, joining the parts gives back the text exactly, cuts prefer a
blank line, a line break, a sentence end, then any whitespace in the second half of the byte window,
and a UTF-8 sequence, a surrogate pair and `\r\n` are never split. The effective minimum is one code
point (4 bytes); the service will use 500. Do not change `split()` or `cutLength()`.

MyMemory uses ISO 639-1/RFC 3066 codes and has regional/script variants. Add a `mymemory` branch to
`serviceCodeOf()` and a small `kMyMemoryOverrides` table in `src/core/languages.cpp`:
`zh-Hans`→`zh-CN`, `zh-Hant`→`zh-TW`, `nb`→`no`, `fil`→`tl`; every other language uses its
internal code. The catalogue follows Google's; return `nullptr` when `info.google == nullptr`. Do
not add a `LanguageInfo` column, a settings field or a per-service table outside `languages.cpp`.
`fromServiceCode()` reverses the codes through the same function.

## Steps

1. **Declare `splitUtf8` in `src/core/textchunker.h`.** Add
   `QStringList splitUtf8(const QString &text, int maxBytes);` next to `split()`. The comment must
   state: the limit applies to `part.toUtf8().size()`; joining the parts gives back `text` exactly;
   cuts prefer a blank line, a line break, a sentence end, then any whitespace, looked for in the
   second half of the byte window; a UTF-8 sequence, a surrogate pair and `\r\n` are never split;
   the effective limit is `qMax(maxBytes, 4)`; an empty text returns an empty list.

2. **Implement `splitUtf8` in `src/core/textchunker.cpp`.** Add a separate code-point walk; do not
   change `split()` or `cutLength()`. Walk UTF-16 code points from `from`, count UTF-8 bytes (1 for
   ASCII, 2 for U+0080..U+07FF, 3 for U+0800..U+FFFF excluding surrogates, 4 for a valid surrogate
   pair), and stop before the next code point would exceed the limit. Remember the last boundary in
   the second half of the byte window: a blank line (`\n` or `\r\n`), a line break, a sentence end
   (reuse the existing `isSentenceEnd`/`isFullWidthSentenceEnd` helpers), then any whitespace other
   than the middle of `\r\n`. Cut at the last such boundary, otherwise at the last fitting code
   point. If the cut lands between `\r` and `\n`, move it back one code point unless that would make
   the part empty. After choosing the length, shrink it defensively while
   `text.mid(from, n).toUtf8().size() > limit`, then append the part and continue. Ensure at least
   one code point per part so a tiny limit cannot loop forever.

3. **Extend `tests/tst_textchunker.cpp`.** Add a `verifyUtf8Invariants(text, parts, limit)` helper
   that asserts `parts.join(QString()) == text`, every part non-empty, every part
   `toUtf8().size() <= limit`, `!part.front().isLowSurrogate()`, `!part.back().isHighSurrogate()`,
   and no boundary between `\r` and `\n`. Add cases:
   - empty text → empty list;
   - short text and text exactly at the limit → one part;
   - Latin text with spaces and a sentence end → natural cuts within the limit;
   - Cyrillic text (2 bytes per character) with spaces and a sentence end;
   - Chinese text (3 bytes per character) with full-width sentence ends;
   - emoji (4-byte code points) with limits that would split a surrogate pair;
   - CRLF text with a limit that would split `\r\n`;
   - a word longer than the limit → a hard cut at a code-point boundary;
   - mixed large text (Latin, Cyrillic, Chinese, emoji, paragraphs, CRLF) with the 500-byte limit;
   - `maxBytes = 1` → effective 4, and no code point is split.
   Keep the existing `split()` tests unchanged; they are the regression gate.

4. **Add the MyMemory language codes in `src/core/languages.cpp`.** Add
   `constexpr QLatin1String kMyMemory("mymemory");` next to the other service constants, a
   `kMyMemoryOverrides` array (`{"zh-Hans", "zh-CN"}`, `{"zh-Hant", "zh-TW"}`, `{"nb", "no"}`,
   `{"fil", "tl"}`), and `const char *mymemoryCodeOf(const LanguageInfo &info)`: return `nullptr`
   when `info.google == nullptr`, the override when the internal code matches, otherwise
   `info.code`. Add the `mymemory` branch to `serviceCodeOf()`; do not add a `LanguageInfo` member.
   No `kSynonyms` entries are needed because `fromServiceCode()` reverses through `serviceCodeOf()`
   for both directions. Do not touch the Google, DeepL or LLM code paths.

5. **Extend `tests/tst_languages.cpp`.** Add `mymemory` rows to `toServiceCode_data`:
   `en`→`en`, `ru`→`ru`, `pt-BR`→`pt-BR`, `pt-PT`→`pt-PT`, `zh-Hans`→`zh-CN`,
   `zh-Hant`→`zh-TW`, `nb`→`no`, `fil`→`tl`, `he`→`he`, `jv`→`jv`; an unknown code → empty. Add
   reverse rows to `fromServiceCode_data`: `zh-CN`→`zh-Hans`, `zh-TW`→`zh-Hant`, `no`→`nb`,
   `tl`→`fil`, `pt-BR`→`pt-BR`. Add `mymemory` to `roundTrip` and assert in `supportedLists` that
   its list equals the Google list, contains `ru`/`pt-BR`/`zh-Hans`, and excludes `auto`. Keep the
   existing Google/DeepL/LLM rows unchanged.

6. **Verify.** Configure and build Debug and Release on Linux without warnings (`-Wall -Wextra`);
   run `tst_textchunker` and `tst_languages`, then the full suite. No new test targets are added, so
   the counts stay 29 Linux / 24 Windows; Windows is not performed (no MSVC). Do not touch the
   service, settings UI, `.ts` files, `doc/architecture.md`, `doc/free-endpoints.md`, `AGENTS.md` or
   any README. Leave commit and push to the coordinator/lead.

## Done when

- `TextChunker::splitUtf8` exists in `src/core/textchunker.h/.cpp`, and `TextChunker::split()` and
  `cutLength()` are unchanged; the existing `tst_textchunker` cases still pass.
- `tst_textchunker` covers empty text, a short/exact-limit text, Latin, Cyrillic, Chinese, emoji,
  CRLF, a word longer than the limit, mixed large text and a tiny limit; every part is at most the
  effective byte limit, the join equals the input, and no surrogate pair or `\r\n` is split.
- `src/core/languages.cpp` has the `mymemory` branch and `kMyMemoryOverrides` table; no
  `LanguageInfo` column, settings field or service table outside `languages.cpp` is added; Google,
  DeepL and LLM mappings are unchanged.
- `tst_languages` covers the forward and reverse MyMemory codes, `supported`, `roundTrip` and an
  unknown code; the MyMemory list equals the Google catalogue and excludes `auto`.
- Debug and Release build without warnings on Linux; `tst_textchunker` and `tst_languages` are
  green and the full suite passes 29/29; Windows and its 24 tests are recorded as not performed (no
  MSVC).
- No service, settings UI, `.ts`, architecture/free-endpoints or `AGENTS.md` change is part of this
  task.

## Risks

- `TextChunker` is shared by DeepL and the LLM path; keep `split()` byte-identical and add
  `splitUtf8` without refactoring `cutLength()`. A wrong edit here would change accepted DeepL
  chunking.
- The byte walk can split a surrogate pair or `\r\n` at the edges: count a valid pair as one code
  point, reject a boundary between `\r` and `\n`, and keep the final `text.mid(...).toUtf8().size()`
  check as the safety net for lone surrogates.
- The MyMemory language catalogue is assumed to follow Google's and is not live-verified. The
  override table is the single fix point if a later live smoke shows an invalid pair; do not add a
  per-service `LanguageInfo` column or a settings field.
- `splitUtf8` has no production caller until `02-mymemory-service`. This is accepted so the core
  change can be reviewed alone; do not add a temporary call site or duplicate the splitter in the
  service.
- The effective minimum of 4 bytes changes the semantics of a limit below 4; document it and test it
  with `maxBytes = 1`. The service always uses 500, so this only affects the test contract.
- Windows Debug/Release and the 24 tests are unavailable on this Linux machine; record them as not
  performed, not as passed.

## Outcome

Done on 2026-11-08. `TextChunker::splitUtf8` and the MyMemory language-code branch are added with
their tests; `TextChunker::split()`, `cutLength()` and the Google, DeepL and LLM mappings are
unchanged, and no service, settings, translation or documentation file is part of the task.

- Splitter: `src/core/textchunker.h/.cpp`. Two new helpers, `codePointLength()` and
  `codePointSize()`, sit after `cutLength()` in the anonymous namespace; the diff of the file
  contains additions only, so `split()` and `cutLength()` are byte-identical. `splitUtf8()` walks
  UTF-16 code points from the start of the remaining text, adds 1/2/3/4 bytes per code point (a
  lone surrogate counts as a conservative 3) and stops before the next code point would exceed the
  limit. It remembers the last blank line, line break, sentence end (the existing
  `isSentenceEnd`/`isFullWidthSentenceEnd` helpers) and whitespace boundary once the byte prefix is
  past `limit/2`, then applies the same priority as `cutLength()`: blank line, line break, sentence
  end, whitespace, otherwise the last fitting code point. A rest that fits entirely becomes one
  part, so a text exactly at the limit is never cut; a cut that lands between `\r` and `\n` moves
  back one code point unless the part would become empty; a defensive loop re-checks the real
  `text.mid(from, length).toUtf8().size()` and shrinks without splitting the first code point, which
  keeps `maxBytes < 4` (effective 4) from looping. Per the plan, the whitespace boundary is only
  accepted in the second half of the byte window — stricter than `split()`, which accepts it
  anywhere; this was the plan's wording and does not affect `split()`.
- Languages: `src/core/languages.cpp` only. `kMyMemory("mymemory")`, a local `CodeOverride` struct,
  `kMyMemoryOverrides` (`zh-Hans`→`zh-CN`, `zh-Hant`→`zh-TW`, `nb`→`no`, `fil`→`tl`) and
  `mymemoryCodeOf()` returning `nullptr` when `info.google == nullptr`, the override when the
  internal code matches, otherwise `info.code`. The `mymemory` branch is the last one in
  `serviceCodeOf()`; `LanguageInfo`, `kSynonyms`, the settings and the service tables are untouched.
  `fromServiceCode()`, `supported()` and `closest()` reuse `serviceCodeOf()`, so the reverse mapping,
  the catalogue (equal to Google's) and the `nullptr` gate come for free. `he` and `jv` use the
  internal codes (`he`, `jv`), not Google's `iw`/`jw`, because the branch returns `info.code`.
  `info.google == nullptr` is unreachable with the current catalogue (every language has a Google
  code and `googleTargets == allCodes()`); the gate is defensive, as the plan intended.
- Tests: `tests/tst_textchunker.cpp` gains `verifyUtf8Invariants()` (join equals the input, parts
  non-empty and at most the effective byte limit, no low surrogate at the front, no high surrogate
  at the back, no boundary between `\r` and `\n`) and 11 cases: empty, short and exact-limit, Latin,
  Cyrillic, Chinese with full-width sentence ends, emoji with limits 1/4/5/6/7/9, CRLF with a cut
  exactly between `\r` and `\n`, a CRLF line break chosen as the boundary, a word longer than the
  limit, `maxBytes = 1`, and mixed large text (Latin, Cyrillic, CJK, emoji, `\r\n`) at 500 bytes.
  All existing `split()` cases are unchanged. `tests/tst_languages.cpp` adds 11 forward `mymemory`
  rows (including `he`/`jv`/unknown), 5 reverse rows, `mymemory` to `roundTrip`, and the
  `supportedLists` checks (the MyMemory list equals the Google list in both directions, contains
  `ru`/`pt-BR`/`zh-Hans`, excludes `auto`); the Google/DeepL/LLM rows are unchanged.
- Verification: Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2) and `qtranslate-trixie:latest` (Qt 6.8.2),
  Debug and Release: zero `warning:` lines with `-Wall -Wextra`, `tst_textchunker` 28 passed,
  `tst_languages` 97 passed, and the full suite 29/29 in each configuration. The trixie builds were
  out-of-tree in `/tmp` from a read-only mount, so no root-owned files were added to the repository.
  Windows/MSVC is unavailable here: the Windows Debug/Release build and its 24 tests were not
  performed, and the test count stays 29 Linux / 24 Windows because no target was added.
- Review: no blocker. The reviewer ran a 300 000-case random fuzz of `splitUtf8` (ASCII, Cyrillic,
  CJK, emoji, `\r\n`, `\t`, NBSP, U+3000, lone surrogates, NUL; `maxBytes` from -2 to 1200) plus
  the boundary, priority and `maxBytes < 4` paths without a single invariant violation, and confirmed
  that no file outside the five changed ones was touched. One accepted finding was comment-only: the
  `codePointSize()` comment said a lone surrogate "encodes as the three-byte replacement character",
  which is wrong; measured on Qt 6.10.2 it encodes as 0 bytes and on Qt 6.8 as `?` (1 byte), so the
  comment now calls 3 a conservative upper bound checked by the defensive loop. No code changed for
  it; the build and 29/29 were re-run afterwards.
- Not performed and deferred by design: `splitUtf8` has no production caller until
  `02-mymemory-service`; the MyMemory language catalogue is based on Google's and is not
  live-verified (the override table is the single fix point); no live smoke and no Windows run.
- Noted in passing, not part of this task: one full-suite run produced `tst_cli (Failed)` while a
  second build was compiling in parallel; it passed in isolation and in every repeated full run, so
  it is the test's 5-second process timeout under load, not a regression from this change.
