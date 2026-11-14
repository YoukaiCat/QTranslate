---
kind: stage
status: todo
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
