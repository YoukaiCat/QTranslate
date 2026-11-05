---
kind: task
status: todo
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
