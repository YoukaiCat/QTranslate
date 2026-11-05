---
kind: task
status: done
detailed: true
depends: [01-archive-legacy-plan]
---

# Translate LLM, dictionary, and i18n docs

## Task

Translate `doc/llm-actions.md`, `doc/dictionary.md`, and `i18n/README.md` to English, update their roadmap references, and keep the localization workflow exact. These are the design notes for old stages 16-17 plus the UI translation README. No code changes.

## Steps

1. `doc/llm-actions.md` (216 lines): translate the title, status, and sections 1-11 (goal, scope, out of scope, providers, architecture, UI, prompts and validation, risks, tests, priorities, done criteria). Preserve `OpenAiCompatibleService`, `src/ai/openaichat`, OpenRouter/OpenAI/local LLM naming, the JSON schema, error mapping, settings keys, and test names. Status: “roadmap stage 6 (old stage 16)”; the dictionary is “roadmap stage 7 (old stage 17)”.
2. `doc/dictionary.md` (207 lines): translate the title, status, and sections 1-11 (goal, scope, out of scope, data model, sources 5.1-5.4, UI, settings, cache and network, risks, tests, done criteria). Preserve `DictionaryService`, JSON/CSV formats, Google `dt=bd`, Wiktionary, settings keys, and test names. Header: “Roadmap stage 7 (old stage 17)”; keep the link to the active roadmap at `[plan.md](roadmap/qtranslate/plan.md)`.
3. `i18n/README.md` (72 lines): translate the title and sections (supported languages table, updating strings, translation rules, adding a language). Keep language codes, native names (`Русский`, `Español`, and the rest), `.ts`/`.qm`, `tr()`, `QT_TRANSLATE_NOOP`, `qt_add_translations`, `QTRANSLATE_TS_FILES`, `lupdate`/`lrelease`, placeholders, emoji, and the Qt 6.12 Hindi gap.
4. Translate comments and labels inside code blocks, but keep commands, identifiers, placeholders, and literal strings. Keep quoted Russian UI labels only where the label itself matters.
5. Check cross-links: `llm-actions.md` → `dictionary.md`, `dictionary.md` → `proofreading.md` and the roadmap; the `CMakeLists.txt` and `src/core/uilanguages.cpp` references are inline code, not links.
6. Do not commit.

## Done when

- All three files are fully English except quoted Russian UI labels and native language names.
- `llm-actions.md` and `dictionary.md` statuses use “roadmap stage 6/7 (old stage 16/17)”; `dictionary.md` links to the active roadmap and no longer to `plan.md`.
- Technical details (classes, JSON/CSV, providers, settings keys, tests, localization commands and rules) are unchanged.
- All relative links resolve; only these three files changed; no build required.

## Risks

- LLM and dictionary docs contain forward-looking designs; do not turn proposals into commitments or change priorities.
- The i18n table's native names are intentionally not English; keep them.
- `i18n/README.md` has commands with placeholders; preserve them byte-for-byte.
- Dictionary and proofreading share UTF-16 semantics; keep the cross-reference.

## Outcome

Translated `doc/llm-actions.md` (216 → 217 lines), `doc/dictionary.md` (207 → 208 lines), and `i18n/README.md` (72 lines) to English. No code changes.

- Statuses: `doc/llm-actions.md` says "roadmap stage 6 (old stage 16)"; `doc/dictionary.md` says "roadmap stage 7 (old stage 17)" and keeps the active-roadmap link `[plan.md](roadmap/qtranslate/plan.md)`.
- Fully English except native language names: the `i18n/README.md` table keeps `Русский` and `Українська`; kept Russian UI labels: none. Proposed UI labels use English source-style wording ("Translate", "Check", "Summarize", "Rewrite", "Explain", "Pronunciation", "Dictionary", "Copy", "Open in main window", "Refresh").
- Preserved against `HEAD`: `OpenAiCompatibleService`, `src/ai/openaichat`, `TranslationService`/`TranslationError`, `OpenAiChat`, `LlmProvider`/`LlmRequest`/`LlmResult`, `AiAction`/`AiActionRegistry`, `DictionaryService` and its structs, provider and model names (OpenAI, Gemini, OpenRouter, Groq, Together, Anthropic, Ollama, LM Studio, llama.cpp, GGUF, `openrouter/free`, `TeamRouter`), JSON/CSV formats, Google `dt=bd`, `translate.googleapis.com/translate_a/single`, MediaWiki API, Wiktionary, `[llm]`/`[dictionary]` keys and values, test names, `.ts`/`.qm`, `tr()`, `QT_TRANSLATE_NOOP`, `qt_add_translations`, `QTRANSLATE_TS_FILES`, `lupdate`, placeholders, emoji, commands, numbers, and priorities. Code-block comments were translated; identifiers and literals were not. The CSV sample meaning is English (`software interface`), and the cache-key description is English (`source|language|model|text`).
- Links: `llm-actions.md` → `dictionary.md` (three places); `dictionary.md` → `proofreading.md` and the active roadmap; all relative links resolve.
- Verification: Cyrillic remains only in the two native language names; backticked spans and numeric tokens match `HEAD` except the translated cache-key description, `2.5–5 GB`, and the added stage numbers; heading, list, table, code-fence, and checkbox counts match; `git diff --check` is clean. No build was required.
- Review found no issues.
- Found: the original `i18n/README.md` does not mention `lrelease`, so it was not added; `qt_add_translations` covers compilation. Remaining: verification task 07, including the deferred `handoff.md` "Proofreading"/"Text checking" mismatch and the old `doc/plan.md` reference in `src/platform/selectiongrabber_win.cpp:248`.
