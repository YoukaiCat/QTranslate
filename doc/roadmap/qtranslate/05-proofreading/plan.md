---
kind: stage
status: todo
---

# Proofreading (old stage 15)

## Goal

Add a separate text-checking mode that finds spelling, grammar, and punctuation errors, shows them in a list or highlight, and replaces the text with corrections. It must not be mixed with translation or its settings.

## What to do, what is touched

A separate settings tab and independent `proofreading/*` keys; `ProofreadingService` and `ProofreadingIssue` with UTF-16 offsets; LanguageTool for grammar and punctuation; Hunspell/Nuspell and Yandex Speller for spelling; optional LLM through a diff; an error list with "Replace", "Replace all", and undo in the main window; replacement in an external application with confirmation; check-before-translation off by default. The old plan's stage 15 and `doc/proofreading.md` list the details.

## Done when

- The separate tab and settings do not affect translation.
- LanguageTool finds spelling, grammar, and punctuation issues for the main languages.
- Hunspell/Nuspell/Yandex Speller work for spelling.
- Single and bulk replacement plus replacing the text with the corrected version work and undo is preserved.
- Rate-limit, key, and network errors are clear and the application does not hang.
- Tests and README/architecture are updated.

## Depends on

None technically; follows stage 4 in the old order and is independent. The optional LLM provider already exists.

## Risks and what is needed from the user

Proofreading APIs have keys and limits; text may be private; automatic replacement is risky. The user must choose the providers and defaults, provide keys where needed, and approve the privacy and replacement rules. The default must keep automatic replacement off.
