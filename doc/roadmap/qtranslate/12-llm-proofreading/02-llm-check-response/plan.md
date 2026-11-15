---
kind: task
status: todo
---

# LLM checking response: prompt, edits, offsets and diff fallback

## Task

Build the response module of the LLM text-checking provider: the system prompt, the parse of the model
answer into `ProofreadingIssue` values with locally computed UTF-16 offsets, and the fallback that
turns a plain corrected text into issues. Pure functions, no network, no widgets, so the risky part of
the feature (unreliable model output, offsets, surrogate pairs) is tested without a server before the
service exists (`02-languagetool-parser` set the same boundary in stage 5).

New `src/services/llmproofreadingparser.{h,cpp}` with a namespace-level API:

```cpp
namespace LlmProofreading {
enum class Status { Edits, PlainText, Malformed };

struct Parsed
{
    QVector<ProofreadingIssue> issues;
    QString correctedText;   // filled only for Status::PlainText
    Status status = Status::Malformed;
    int droppedEdits = 0;    // edits whose `original` was not found in the source
};

QString systemPrompt(const QString &languageName, const QStringList &categories);
Parsed parseResponse(const QString &source, const QString &content,
                     const QStringList &categories);
QVector<ProofreadingIssue> diffIssues(const QString &source, const QString &corrected);
}
```

Decisions already taken by the split; keep them when detailing:

- **Protocol.** The prompt asks for exactly one JSON object
  `{"edits":[{"original","replacement","message","category"}]}` and an empty array when nothing is
  wrong, with `original` an exact substring of the user text. A bare JSON array of edits is accepted
  too, and a fenced block or prose around the object is stripped before parsing (`OpenAiChat::
  stripCodeFence()` plus the first `{`/last `}` slice that
  `src/services/openaicompatibleservice.cpp:70-86` already uses).
- **Offsets are local.** For each edit, `source.indexOf(original, cursor)` with a cursor that only
  moves forward, so two identical fragments map in order; a fallback search from 0; an edit that is
  still not found is dropped and counted in `droppedEdits`. `length` is `original.size()` in QChar
  units; a surrogate pair counts two.
- **Categories.** The prompt names only the requested categories (`ProofreadingRequest::categories`;
  empty means all four). The parse maps the returned `category` onto `proofreadingCategoryIds()` and
  drops an edit whose category is known but not requested; an unknown or empty category is kept, so a
  sloppy model does not silently lose edits.
- **Fallback.** Content that is not a JSON object/array with `edits` is treated as the corrected text
  (`Status::PlainText`): trim the common prefix and suffix, emit one issue per changed line when both
  middle spans have the same number of line breaks, otherwise one issue over the whole middle span.
  Trimming never cuts a surrogate pair. `ruleId` is `llm` for parsed edits and `llm-diff` for the
  fallback; the fallback message is a plain English literal, like the messages LanguageTool returns
  (they are shown untranslated today), so this task adds no `tr()` and no `.ts` message.
- **Nothing user-visible is invented here.** `severity` stays 0, `message` is the model's own text
  (trimmed), and the module never logs the text: only counts.

## Done when

- `systemPrompt()` names the role, treats the user message as data (no instructions followed), asks
  for the JSON schema with the four field names, carries the language (`Languages::englishName()`;
  "detect the language automatically" for `auto`) and the requested categories, and contains no user
  text.
- `parseResponse()` returns `Status::Edits` for a JSON object, a fenced object, an object with prose
  around it, and a bare array; `Status::PlainText` for plain corrected text; `Status::Malformed` for
  an empty answer and for JSON that is neither an edits document nor text (for example `{"foo":1}`).
- Offsets and lengths are UTF-16 units and never split a surrogate pair: covered by Cyrillic text, an
  emoji before the error and an emoji at a diff boundary.
- Two identical fragments get consecutive offsets; an edit whose `original` is absent is dropped and
  counted; an edit with an empty `original` or an unchanged `replacement` is skipped.
- The category filter drops a known unrequested category and keeps an unknown one.
- `diffIssues()` returns no issues for equal texts, per-line issues when the line counts match, one
  issue otherwise, and `ProofreadingText::corrected(source, diffIssues(source, fixed))` equals
  `fixed` for every diff fixture.
- `OpenAiChat::stripCodeFence()` is the single implementation: moved out of the anonymous namespace of
  `src/services/openaicompatibleservice.cpp:28-41`, called from there and from the parser, with new
  `tst_openaichat` rows and `tst_openaicompatibleservice` passing without an edit.
- New `tst_llmproofreadingparser`; Linux becomes 51 tests, Windows 45 (derived). Debug and Release
  build without warnings on the host and in `qtranslate-trixie`, 51/51 in both; Windows not performed.
  No `.ts` change, no `tr()`, no `doc/` change outside `doc/roadmap/`.

## Risks

- **Offsets are the whole game.** `ProofreadingIssue` offsets feed highlighting and replacement; a
  one-unit error silently replaces the wrong characters. Every fixture asserts the offset, and the
  `ProofreadingText::corrected()` invariant is the cross-check.
- **A model that ignores the format.** The plain-text path must stay usable: no crash, no empty
  "No issues found" lie when the text actually changed — that is why `Status` distinguishes the three
  outcomes and the service (child `03-llm-check-service`) can report `BadResponse` for `Malformed`.
- **Do not over-engineer the diff.** A word-level LCS on 20 000 characters is O(n·m) and would be the
  slowest thing in the application; the prefix/suffix plus line split is coarse on purpose and the
  preview shows the result before it is applied.
- **Moving `stripMarkdownFence` changes a reviewed file.** Keep the body byte-identical and let
  `tst_openaicompatibleservice` (JSON/plain fallback, fenced answers) be the regression gate; if it
  needs an edit, the move changed behaviour.
