---
kind: stage
status: done
base: 5e6f1cb
depends: [09-proofreading-standalone]
---

# LLM as a text-checking provider

## Goal

Make the configured OpenAI-compatible provider a second text-checking provider, selectable in the
existing checking window and popup exactly like LanguageTool: one feature for the user, one issue
list, the same preview/replace/ignore flow. The LLM settings move to a separate AI tab; there is no
extra consent gate and no separate AI actions UI.

## What to do, what is touched

- An **AI** settings tab with base URL, key, model and a connection check, reusing `[llm]`; the
  OpenAI-compatible translation service uses the same configuration, so translation and checking share
  one provider setup. The Services page keeps a pointer if the group moves.
- A proofreading provider with id `llm` in `proofreadingProviderIds()` and the provider list; the
  request asks for structured edits (`original`, `replacement`, message) and the offsets are computed
  locally, with a diff fallback for a plain corrected text.
- Language handling: the checking language is passed to the provider; auto-detect is supported where
  the model can do it; unsupported pairs produce the existing `UnsupportedLanguage` error.
- Errors: no key, no connection, timeout, quota or rate limit, model not found, malformed response —
  mapped to the existing `ProofreadingError` values and shown in the current window and popup.
- No text, keys or full responses in the log; no new `proofreading/*` consent key and no extra
  confirmation: choosing the provider and starting a check is the consent, like any other provider.
- Tests: request and response parsing against a mock server, error mapping, settings, provider
  selection, and the existing window and popup flow; `.ts` strings for the provider and the AI tab.
- Documentation is the lead's pass.

The work is split into four children. Each has its own result and gate, and the translation path is
touched only where the provider configuration moves.

1. [01-ai-tab](01-ai-tab/plan.md) (done) — move the OpenAI-compatible provider group from Services to
   its own **AI** tab, keep `[llm]` the one configuration, leave a pointer on Services, update the
   dialog tests and the 14 `.ts`; the translation service is provably untouched. Detailed.
2. [02-llm-check-response](02-llm-check-response/plan.md) — the prompt and the response module: local
   UTF-16 offsets, structured edits, the category filter and the diff fallback for a plain corrected
   text; pure functions and `tst_llmproofreadingparser`.
3. [03-llm-check-service](03-llm-check-service/plan.md) — `LlmProofreadingService` (id `llm`) on the
   existing `OpenAiChat` transport, error mapping onto `TranslationError`, cancellation, clean logs,
   `proofreadingProviderIds()` and the `AppController` registration; `tst_llmproofreadingservice`.
4. [04-llm-check-ui](04-llm-check-ui/plan.md) — selecting `llm` on the Text checking page: the
   LanguageTool URL row disappears, the AI-tab hint appears, the stored URL survives, and the choice
   persists; dialog and settings tests, no new `.ts` message.

Boundaries recorded by the split:

- **The transport that is reused already exists.** `OpenAiChat` (`serialize`, `parse`,
  `errorForHttpStatus`, `chatCompletionsUrl`, `parseModels`) is shared with the translation service;
  this stage adds `stripCodeFence` to it and nothing else. The HTTP send/error glue stays in each
  service because the reply bases differ (`ProofreadingReply` vs `TranslationReply`) and the messages
  have their own translation context. A new `OpenAiChatCall` abstraction was considered and rejected:
  it would refactor a reviewed, working translation path for no regression value, and the unreviewed
  `stash@{0}` draft that tried it is neither used nor trusted.
- **One configuration, no new keys.** `[llm]` (`baseUrl`, `apiKey`, `model`) serves translation and
  checking; `proofreading/endpoint` stays LanguageTool-only and is neither read by the LLM provider
  nor cleared when it is selected. No `proofreading/*` consent key and no extra confirmation (user
  decision of 2026-11-15).
- **The provider is a normal checking provider.** Id `llm`, display name the untranslated `LLM` (like
  `LanguageTool`, `Bing`, `DuckDuckGo`), the full `llm` language catalogue with Auto-detect, the same
  `ProofreadingIssue`/`ProofreadingResult` contract and the same window, popup and hotkeys. The
  checking window, the popup and both hotkeys need no provider-specific code.
- **One request per check, no chunking.** An edit split across a chunk boundary is worse than a size
  error; `413` and `finish_reason: "length"` map to `TooLong`, and a text that exceeds the model
  context is reported rather than silently truncated. Recorded as a limitation.
- **Translations.** The 14 `.ts` files are at **383** messages after `01-ai-tab`; the checking service,
  the parser and the settings hint add no `tr()` — their messages are technical English details under
  the translated `ProofreadingErrorText` titles, exactly as LanguageTool's. `04-llm-check-ui` reuses
  the existing pointer sentence instead of adding a near duplicate, so the stage leaves the message
  count where it is.
- **Test counts.** Two new targets (`tst_llmproofreadingparser`, `tst_llmproofreadingservice`):
  Linux 51 → **53**, Windows 45 → **47** (derived; no MSVC here). Windows builds and tests stay
  recorded as not performed.
- **The live check is the user's.** The automated gate is the mock `QTcpServer`; a real key, a local
  Ollama/llama.cpp server and the privacy/cost acceptance are the user's, as the stage risks record.

## Done when

- The AI tab exists, `[llm]` is edited there, and the OpenAI-compatible translation still works from
  the same configuration.
- The checking provider combo lists the LLM provider next to LanguageTool; a check with it fills the
  same issue list, preview and Replace/Ignore/Replace all flow; the popup and the second hotkey work
  with it.
- Errors are clear and the application does not hang; a missing key or an unreachable server produces
  a message, not a silent failure.
- No new consent gate; no text or secrets in the log; LanguageTool behaviour is unchanged.
- Debug and Release build without warnings and all Linux tests pass; Windows by diff; the user runs
  the live check with a real key or a local model (the tests use a mock server).

## Depends on

Stages 5 and 9 for the checking window, popup and issue model; the OpenAI-compatible transport
already exists for translation.

## Risks and what is needed from the user

- Model output may not be valid JSON; the diff fallback must keep the result usable.
- Offsets from the model are unreliable; compute them locally.
- Cost and privacy: the text goes to the provider; the user configures the key and the model and
  accepts the cost. No extra gate, per the user's 2026-11-15 decision.
- Live verification needs the user's key or a local Ollama/llama.cpp server; tests use a mock.

## Outcome

Accepted on 2026-11-16 over `5e6f1cb..HEAD` without the unrelated tray commits (`0052458`,
`1cbe165`, `be3fa92`, `6a0cddd`). The stage delivered, in four task commits, the AI tab (`be1e5f9`),
the response module (`5dcc390`), the `llm` checking provider (`c5b10f0`) and the provider-aware Text
checking page (`e70bf38`); the acceptance fix `5210d43` separated the parser counters.

What exists now: the **AI** tab edits the single `[llm]` block (`baseUrl`, `apiKey`, `model`) that
serves both the OpenAI-compatible translation service and the new checking provider; the Services page
keeps a pointer. `LlmProofreadingService` (id `llm`, untranslated display name `LLM`) sits next to
LanguageTool in `proofreadingProviderIds()`, registered right after `m_languageTool` and before
`setActiveService()`, so LanguageTool stays the default. It sends one request per check over the
existing `OpenAiChat` transport (`temperature 0`, no `max_tokens`, `systemPrompt()` with the internal
language code), parses the answer locally (`src/services/llmproofreadingparser.*`: UTF-16 offsets,
forward-only search, surrogate-pair guard, category filter, coarse diff for a plain corrected text,
offsets shifted by the trimmed leading whitespace) and maps every failure onto the existing
`ProofreadingError` values. The checking window, the popup, the issue panel and both hotkeys needed no
provider-specific code. An empty answer, a clean `edits` array and a whitespace-only difference are a
successful "no issues" result; `Malformed`, unplaceable edits and `finish_reason: "length"`/`413` are
reported as errors. No consent key was added, `proofreading/enabled` remains the only gate, and the
logs carry counters, statuses and timings only.

Verification: host Debug and Release 53/53 with 0 warnings, `qtranslate-trixie` Release 53/53 (a clean
build), `.ts` 383 messages in each of the 14 files with no new `tr()`, Windows 47 by diff (no MSVC).
The acceptance review found no regression in the LanguageTool path, the translation provider or the
settings normalisation, and confirmed every stage criterion.

Deferred, with the price of a full fix:

- **No chunking.** One request per check: text past the model context, or an answer truncated without
  `finish_reason: "length"` on providers that do not set it, becomes `TooLong` or a parse of a
  truncated answer instead of a split check with recomputed offsets.
- **`validate()` stays unconditional while the URL row is hidden.** A user who typed an invalid
  LanguageTool URL and switched to `llm` is blocked by an error about a field they cannot see; skipping
  the check instead would let `normalizedProofreadingEndpoint()` erase the stored endpoint. A real fix
  changes that normalisation contract and needs a new `tr()` and a 14-file `.ts` pass.
- **No test target for the `AppController` wiring.** Registration order and `applySettings()` are
  covered only by reading the diff, the dialog combo test and the manual path — the same gap `01` and
  `02` recorded.
- **The live provider is unverified.** The automated gate is a mock `QTcpServer`; a real key or a local
  Ollama/llama.cpp model, the cost and the privacy acceptance are the user's.
- **Model output quirks stay handled, not fixed:** a non-empty bare array of objects inside an ordinary
  sentence still reads as an edits document, a valid JSON envelope without `edits` is `Malformed`, and
  an unknown category falls back to `grammar`.
