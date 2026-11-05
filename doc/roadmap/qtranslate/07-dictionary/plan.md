---
kind: stage
status: todo
---

# Dictionary and glossaries (old stage 17)

## Goal

Add a separate dictionary mode for words and short phrases: part of speech, meanings, usage examples, synonyms, and transcription. It must not be mixed with translation or its settings.

## What to do, what is touched

`DictionaryService`, `DictionaryRequest`, `DictionaryEntry`, and `DictionaryDefinition`; an LLM source through the shared `[llm]` transport with a JSON schema and validation; Google gtx `dt=bd` for short texts, a local CSV glossary, and Wiktionary; a dictionary panel in the main window, a popup card, and a separate hotkey; cache, cancellation of stale requests, and clear errors; `[dictionary]` settings independent of translation. The old plan's stage 17 and `doc/dictionary.md` list the details.

## Done when

- The dictionary opens from the main window and popup without affecting translation.
- The LLM source returns a structured article and handles format errors.
- The CSV glossary works locally and sends no data to the network.
- At least one additional source (Google or Wiktionary) works with a fallback.
- Cache, cancellation, DPAPI, and the absence of text in logs are covered by tests.
- README and architecture are updated.

## Depends on

Stage 6 for the LLM dictionary source; Google, CSV, and Wiktionary are independent. Follows stage 6 in the old order.

## Risks and what is needed from the user

Dictionary sources have different licences and data quality; LLM calls cost money and send text to a provider; Google's format is undocumented and may change. The user must choose the sources and licences, provide keys where needed, and approve the privacy rules.
