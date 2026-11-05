---
kind: task
status: done
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

## Outcome

Translated the title and sections 1-4.7.2 of `doc/architecture.md` to English. Sections 4.8-10 and 11 are untouched byte for byte, so the document stays intentionally mixed until task 03. Technical identifiers, paths, settings keys, endpoints, JSON fields, numbers, code blocks, and class/method/signal names are unchanged; comments inside snippets were translated and string literals kept.

- The ASCII diagram in section 2 keeps its frames, arrows, and module names; Russian annotations inside it were translated, and the pre-existing one-character offset of the `main.cpp` box was fixed so the borders line up.
- The `doc/` subtree in 3.1 now shows the migrated layout: `architecture.md`, `handoff.md`, `code-signing.md`, `dictionary.md`, `free-endpoints.md`, `llm-actions.md`, `local-translation.md`, `proofreading.md`, `releases/0.11.0.md`, and `roadmap/{README.md,qtranslate/{plan.md,legacy-plan.md,journal.md,01-documentation-migration/plan.md}}`. There is no `doc/plan.md`.
- Stage references: Linux in 1.3/1.4 is `roadmap stage 2 (old stage 11)`; 4.6 uses `old plan stage 2` and `old plan stage 6`.
- Kept Russian UI labels: none. The quoted labels were replaced with their English source strings from `src/ui/settingsdialog.cpp` and `src/ui/trayicon.cpp`: `Hotkeys`, `Check key`, `Load models`, `Pause hotkeys`, and `Restore clipboard after copying the selection`. No Cyrillic remains before `### 4.8.`.
- Verification: the tail from `### 4.8.` is identical to `HEAD`; heading, table row/column, list, blockquote, and code-fence counts match; `git diff --check` is clean; there are no relative Markdown links in the changed range and the existing links elsewhere in the file resolve; only `doc/architecture.md` changed; no build was required.

Found but not fixed, because the task does not audit the rest of the tree:

- The 3.1 tree still omits `src/ai/openaichat.*`, `core/uilanguages`, `services/{deeplwebparser,deeplwebservice,openaicompatibleservice}`, the `platform/` stubs (`autostart_stub.cpp`, `secretprotector_stub.cpp`), and the newer tests (`tst_appinfo`, `tst_deeplwebparser`, `tst_deeplwebservice`, `tst_hotkeymanager`, `tst_openaichat`, `tst_openaicompatibleservice`, `tst_translations`, `tst_uilanguages`).
- The 3.2 test list does not mention `tst_translations` and `tst_uilanguages` even though they exist.
- `src/platform/selectiongrabber_win.cpp:248` still points to the removed `doc/plan.md`; section 11.7 defers code-comment changes to a separate task.
- Section 11.1 still describes `doc/plan.md` as an active document; section 11 is the untouched migration policy.

Remaining for the stage: sections 4.8-10 in task 03.
