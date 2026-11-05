---
kind: task
status: done
detailed: true
depends: [01-archive-legacy-plan]
---

# Translate provider and proofreading design notes

## Task

Translate `doc/free-endpoints.md`, `doc/local-translation.md`, and `doc/proofreading.md` to English and add current roadmap status references. These are the design notes for old stages 13-15, now roadmap stages 3-5. No code changes.

## Steps

1. `doc/free-endpoints.md` (149 lines): translate the title, status, introduction, and sections 1-10 (DeepL web, Yandex web, Bing web, Reverso, Mozhi, MyMemory, the DeepL API free tier, integration, priorities, and what not to do). Preserve endpoint URLs, headers, status codes, payload formats, rate limits, licensing notes, and the link to `ahatem/QTranslate`. Status: “roadmap stage 3 (old stage 13)”.
2. `doc/local-translation.md` (171 lines): translate the title, status, and sections 1-7 (why, engine options including 2.1 ONNX Runtime GenAI and llama.cpp, how Argos works, native mode, limitations, PoC, decision). Preserve model names, licenses, formats (`.argosmodel`, CTranslate2, SentencePiece), sizes, and the recommendation. Status: “roadmap stage 4 (old stage 14)”.
3. `doc/proofreading.md` (171 lines): translate the title, status, and sections 1-10 (goal, scenarios, providers, architecture, settings, UI, tests, risks, priorities, done criteria). Preserve provider names (LanguageTool, Hunspell/Nuspell, Yandex Speller), `ProofreadingIssue` fields, UTF-16 offsets, settings keys, test names, and the link from `dictionary.md`. Status: “roadmap stage 5 (old stage 15)”.
4. Translate comments inside code blocks; keep identifiers, JSON fields, and literals.
5. Check cross-links: `dictionary.md` ↔ `proofreading.md`, `architecture.md` §4.15/§9.2, and local links. Do not add new design decisions.
6. Do not commit.

## Done when

- All three files are fully English except quoted Russian UI labels and identifiers (list kept labels).
- Each status names the new roadmap stage and the old stage in parentheses.
- Endpoints, provider names, licenses, model formats, settings keys, field names, test names, and priorities are unchanged.
- All relative links resolve; only these three files changed; no build required.

## Risks

- These notes contain licensing and provider terms; do not simplify or reinterpret them.
- Keep the distinction between implemented (DeepL web) and research or planned endpoints.
- Local translation compares engines; preserve the recommendation and caveats.
- Proofreading offsets are UTF-16; do not change units or field semantics.

## Outcome

Translated `doc/free-endpoints.md` (149 → 148 lines), `doc/local-translation.md` (171 → 175 lines), and `doc/proofreading.md` (171 → 174 lines) to English. Each status now names the roadmap stage with the old stage in parentheses: stage 3 (old 13), stage 4 (old 14), and stage 5 (old 15). No code changes.

- The notes are fully English; only typographic characters (`—`, `→`, `↔`, `–`) remain non-ASCII. Kept Russian UI labels: none. Quoted UI labels use the English source strings already used in `doc/architecture.md` ("Text checking", "Check", "Translate", "Replace all", "Apply corrections") and planned English labels for buttons and states.
- Preserved against `HEAD`: endpoint URLs and payload examples, headers, status codes, rate limits, provider and model names, licenses, formats (`.argosmodel`, CTranslate2, SentencePiece), sizes, settings keys, `ProofreadingIssue` fields, UTF-16 offset semantics, test names, priorities, recommendations, and caveats. Code-block comments were translated; identifiers and literals were not.
- Links: the incoming links from `doc/dictionary.md:76`, `doc/architecture.md` §4.15 and §9.2, and `doc/handoff.md` resolve; no document links to anchors in these notes, so the translated headings break nothing. `legacy-plan.md` links to the same file names and stays valid.
- Verification: no Cyrillic in the three files; all backticked spans match `HEAD` exactly; numeric tokens match except English digit grouping and the added stage numbers; heading counts and order, list numbering, tables, and code fences match; `git diff --check` is clean. No build was required.
- Review found no issues.
- Found but not fixed, out of scope: `doc/handoff.md:165` calls the planned settings tab "Proofreading", while `doc/proofreading.md:111` and `doc/architecture.md:1252` use "Text checking"; task 04 introduced that mismatch. The stale `doc/plan.md` reference in `src/platform/selectiongrabber_win.cpp:248` remains a known deferred item.
