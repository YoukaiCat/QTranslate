---
kind: stage
status: in-progress
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
- **Translations.** The 14 `.ts` files go from 382 to **384** messages (`AI` and the AI-tab pointer
  sentence, both in `01-ai-tab`); the checking service and parser add no `tr()` — their messages are
  technical English details under the translated `ProofreadingErrorText` titles, exactly as
  LanguageTool's. `04-llm-check-ui` reuses the pointer sentence instead of adding a near duplicate.
- **Test counts.** Two new targets (`tst_llmproofreadingparser`, `tst_llmproofreadingservice`):
  Linux 50 → **52**, Windows 44 → **46** (derived; no MSVC here). Windows builds and tests stay
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
