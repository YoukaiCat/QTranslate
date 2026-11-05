---
kind: task
status: todo
detailed: true
depends: [01-archive-legacy-plan]
---

# Translate architecture sections 1-4.7

## Task

Translate the first part of `doc/architecture.md` to English: the title and sections 1-4.7 (goals and requirements, general scheme, source structure, and modules 4.1-4.7). Keep section 11 untouched and leave 4.8-10 for task 03. This part is the reference for the core services and build layout. Technical identifiers, code blocks, tables, paths, settings keys, endpoints, and numbers stay unchanged.

## Steps

1. Translate the title and section 1: 1.1 functional requirements, 1.2 non-functional requirements, 1.3 technology stack, 1.4 platform matrix (currently lines 9-65). In 1.3 and 1.4 use “roadmap stage 2 (old stage 11)” for Linux.
2. Translate section 2, general scheme, and 2.1, thread model (currently lines 66-117). Keep diagrams and class names.
3. Translate section 3, source structure (currently lines 118-295):
   - 3.1 directory tree: translate annotations after `#` but keep the tree characters and file names. Update the `doc/` subtree to the migrated layout: `architecture.md`, `handoff.md`, `code-signing.md`, `dictionary.md`, `free-endpoints.md`, `llm-actions.md`, `local-translation.md`, `proofreading.md`, `releases/0.11.0.md`, and `roadmap/{README.md,qtranslate/{plan.md,legacy-plan.md,journal.md,01-documentation-migration/plan.md}}`. Do not audit the rest of the tree;
   - 3.2 CMake target table, 3.3 build, 3.4 packaging/CI/release: translate prose and preserve target names, test names, CMake/Qt/Inno flags, artifact names, URLs, and versions.
4. Translate section 4 through 4.7.2 (currently lines 296-746): 4.1 `main.cpp`/`SingleInstance`, 4.2 `AppController`, 4.3 `HotkeyManager`, 4.4 `SelectionGrabber`, 4.5 translation types/service interface, 4.6 `GoogleTranslateService`, 4.7 `DeepLService`, 4.7.1 `free-web`, 4.7.2 `OpenAiCompatibleService`. Preserve class/method/signal names, WinAPI identifiers, settings keys, API URLs, JSON field names, status codes, and log strings. Translate comments inside fenced snippets; keep string literals and identifiers.
5. Stage references in this range: completed old stages become “old plan stage N” (for example current lines 589 and 613 refer to old plan stages 2 and 6). Linux uses “roadmap stage 2 (old stage 11)” (current lines 38, 40, 49). Do not renumber completed old stages as if they were new roadmap stages.
6. Keep quoted Russian UI labels only where the label itself matters; otherwise translate them. List the kept labels in the outcome.
7. Do not touch sections 4.8-10 or section 11. Do not change the document structure or reflow tables unnecessarily.
8. Check that links in the changed range resolve. Do not commit.

## Done when

- The title and sections 1-4.7 are English; headings `## 1`-`## 4` and `### 4.1`-`### 4.7.2` are English.
- The `doc/` subtree in 3.1 shows the migrated roadmap/archive layout and no `doc/plan.md`.
- Technical identifiers, paths, settings keys, API endpoints, code blocks, tables, and numbers are unchanged.
- Old completed stage references are marked “old plan stage N”; Linux uses “roadmap stage 2 (old stage 11)”.
- No Cyrillic remains in the changed range except quoted Russian UI labels and native language names (list them).
- Section 4.8 onward and section 11 are untouched; the file is intentionally mixed until task 03.
- Only `doc/architecture.md` changed; no build required.

## Risks

- The diff is large (about 750 lines); translate section by section to avoid dropping paragraphs or reflowing the tree.
- The `doc/` tree is easy to get wrong; compare it with architecture §11.3, which has the authoritative layout.
- Russian UI labels can be legitimate; do not replace them with guessed English labels when the label itself is the subject.
- Do not touch section 11: it is the English migration policy and stays as is.
- Other parts of the directory tree (for example `src/ai`) may already be stale; note them in the outcome but do not audit the whole tree in this task.
