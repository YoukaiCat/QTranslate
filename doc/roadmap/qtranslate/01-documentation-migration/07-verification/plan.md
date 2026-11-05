---
kind: task
status: todo
detailed: true
depends: [01-archive-legacy-plan, 02-architecture-core, 03-architecture-runtime, 04-handoff-and-signing, 05-provider-and-proofreading-notes, 06-llm-dictionary-and-i18n]
---

# Verify the documentation migration

## Task

Run the final gate for stage 1: check roadmap frontmatter, all relative links, English prose, the archive marker, the no-code-change rule, and the user's spot-check of `doc/architecture.md` and `doc/handoff.md`. This task closes the stage; it does not add features or translate new material.

## Steps

1. Frontmatter and map index: check the root `doc/roadmap/qtranslate/plan.md` (`kind: root`, `status: in-progress`, `lang: en`), the stage (`kind: stage`), and every child node (`kind: task`, valid `status`, `detailed: true` where steps exist). Confirm `doc/roadmap/README.md` lists the `qtranslate` map with its status, one-line description, and link. Use the lead's tree overview or a grep over `doc/roadmap/**/plan.md`; fix only map metadata errors, never goals or criteria.
2. Link check: extract relative Markdown links from `doc/**/*.md`, `AGENTS.md`, `README.md`, `README.ru.md`, and `i18n/README.md`; resolve each against its file's directory; allow external URLs and in-page anchors. Pay special attention to the root → `legacy-plan.md`, the map README → map root, the archive → `../../...`, the stage → child tasks, and design-note cross-links. Use a one-off shell or Python command in `/tmp`; do not add scripts to the repo.
3. Cyrillic scan: run `grep -nP '[\x{0400}-\x{04FF}]'` over active docs. Allowed: `README.ru.md`, `doc/roadmap/qtranslate/legacy-plan.md`, quoted Russian UI labels (for example «Сервисы», «Приостановить горячие клавиши», installer language names), and native language names in `i18n/README.md`. Everything else must be translated; review headings and prose manually because grep cannot distinguish quotes.
4. Old-plan references: grep `doc/plan.md` and `](plan.md)`. Allowed: the archive, `doc/architecture.md` §11 (the migration policy and history), release history, map nodes, and the journal. Active documents must point to the roadmap or archive.
5. No-code-change check: `git diff --name-only` against the stage base or working tree must contain only `doc/**`, `AGENTS.md`, `i18n/README.md`, and `README.md`/`README.ru.md` if a link changed. No `src/`, `tests/`, `CMakeLists.txt`, `packaging/`, `resources/`, `.github/`, or build files. If a code file changed, stop and report; do not revert silently.
6. English completeness: confirm the title and sections 1-10 of `doc/architecture.md`, `doc/handoff.md`, all design notes, `i18n/README.md`, and `AGENTS.md` are English; check that section 11 of the architecture is still the English policy and the archive body is intentionally Russian. Confirm `doc/handoff.md` records the known manual gaps and that the READMEs are in sync.
7. Ask the user to spot-check `doc/architecture.md` and `doc/handoff.md`; wait for the result. Apply terminology fixes only to the affected documents, then re-run the link and Cyrillic checks. Record the request and result in the task outcome.
8. Update the current-state paragraph of `doc/handoff.md` only if it still claims the migration is in progress; state that the roadmap is active and active docs are English. Do not rewrite release history.
9. Do not commit; report to the lead with the check results and any remaining risks.

## Done when

- Roadmap frontmatter is valid and every node has a kind and status; task nodes with steps have `detailed: true`; `doc/roadmap/README.md` lists the map with status, description, and link.
- Every relative Markdown link in active docs resolves; the archive links and roadmap links are checked explicitly.
- Cyrillic remains only in the allowed files, quoted UI labels, and native language names; no Russian prose or headings remain in active docs.
- No active document points to `doc/plan.md` except the archive, architecture §11 policy/history, release history, map nodes, and the journal.
- `git diff --name-only` shows only documentation, `AGENTS.md`, and `i18n/README.md`; no code, tests, build, packaging, or CI changes.
- The user has spot-checked `doc/architecture.md` and `doc/handoff.md`, or their requested changes are applied and re-checked.
- The stage's `## Done when` is satisfied and the lead can mark stage 1 done.

## Risks

- Link checkers can produce false positives for anchors and external URLs; verify hits manually.
- Quoted UI strings are legitimate Cyrillic; classify each hit instead of deleting text mechanically.
- The user spot-check may take time; do not mark the task done on a pending request.
- If a code file changed by accident, stop and report; the migration must remain documentation-only.
