---
kind: stage
status: todo
---

# LLM actions (old stage 16)

## Goal

Add an optional LLM layer for text actions: translate (already implemented), proofread, summarise, rewrite, explain, and pronounce. The LLM supplements DeepL/Google/LanguageTool rather than replacing them.

## What to do, what is touched

An "AI" settings tab with base URL, model, key (DPAPI), timeout, and connection test; actions `translate`, `proofread`, `summarize`, `rewrite`, `explain`, `pronounce`; prompts with a fixed role, low temperature, and response validation; preview/diff and replacement only after confirmation; cache and cancellation of stale requests. The old plan's stage 16 and `doc/llm-actions.md` list the details.

## Done when

- The separate "AI" tab and independent settings exist.
- Summarise, rewrite, and explain work without changing text without confirmation.
- `proofread` returns structured edits or a diff.
- Key, quota, network, and model errors are clear and the application does not hang.
- Keys use DPAPI and text is not logged.
- Tests and documentation are updated.

## Depends on

Stage 5 for the `proofread` action; the other actions use the existing OpenAI-compatible provider. Follows stage 5 in the old order.

## Risks and what is needed from the user

LLM calls cost money, send text to a provider, and may produce unreliable output. The user must choose the providers, actions, defaults, and privacy rules, provide keys, and accept the cost. The default must not replace text without confirmation.
