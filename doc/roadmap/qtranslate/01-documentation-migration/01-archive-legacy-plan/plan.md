---
kind: task
status: todo
detailed: true
---

# Archive the old flat plan and update references

## Task

Move the Russian flat plan from `doc/plan.md` to `doc/roadmap/qtranslate/legacy-plan.md`, mark it as a historical archive in English, and repoint every active reference. This is migration step 1 of `doc/architecture.md` §11.6 and the first child of stage 1. The archive body stays Russian and otherwise unchanged; only its header and relative links change. No product code.

## Steps

1. Move the file with history: `git mv doc/plan.md doc/roadmap/qtranslate/legacy-plan.md`. Confirm `git status` shows a rename, not an add/delete pair.
2. Prepend an English archive header before the original `# qtranslate — план реализации`:
   - title `# qtranslate — legacy implementation plan (Russian archive)`;
   - a short blockquote: this is the historical Russian flat plan of stages 1-17, kept for history and rationale; it is not the active plan; the active roadmap is [`plan.md`](plan.md) and the architecture is [`../../architecture.md`](../../architecture.md);
   - explain that relative links in the body were rewritten from `X.md` to `../../X.md` after the move and that stage numbers below are the old flat-plan numbers;
   - a horizontal rule before the original Russian title. Keep the original body verbatim.
3. Rewrite the archive's relative links: `architecture.md` → `../../architecture.md`; `free-endpoints.md`, `local-translation.md`, `proofreading.md`, `llm-actions.md`, and `dictionary.md` → `../../…`. Current link lines are 4, 1343, 1376, 1404, 1435, and 1466; re-grep after the edit.
4. Update `doc/roadmap/qtranslate/plan.md` line 14 only: the archive link becomes `legacy-plan.md`, and the sentence becomes present tense (“is in `legacy-plan.md`; stage 1 moved it there”). Do not change the root goal, `## Done when`, `## History`, or `## Stages`.
5. Update `doc/roadmap/README.md` line 3: the archive now exists at `qtranslate/legacy-plan.md`; link it and remove “will be archived”.
6. Update `AGENTS.md`:
   - line 11: design docs are English and authoritative (`doc/architecture.md`, `doc/handoff.md`, the design notes in `doc/`); the active roadmap is `doc/roadmap/qtranslate/plan.md`; the old plan is archived at `doc/roadmap/qtranslate/legacy-plan.md`, is Russian, and is historical only; `README.md` and `README.ru.md` stay in sync;
   - state that there is no Jira key: this is a personal project and commit messages follow the repository style without a Jira number;
   - mention the roadmap and archive in the release/versioning guidance where the old plan was implied. Keep the file concise; do not duplicate architecture §11.
7. Repoint active references to the old path:
   - `doc/handoff.md` lines 228 and 243: `doc/plan.md` → `doc/roadmap/qtranslate/plan.md`; point history to `doc/roadmap/qtranslate/legacy-plan.md`;
   - `doc/code-signing.md` line 141: the same replacement;
   - `doc/dictionary.md` line 4: `[plan.md](plan.md)` → `[plan.md](roadmap/qtranslate/plan.md)`. Leave the Russian prose and the stage number for task 06.
8. Grep `doc/plan.md` and `](plan.md)`. Remaining hits must be only the archive, `doc/architecture.md` §11 policy/history, release history, the map nodes, and the journal. Active documents must point to the roadmap or the archive.
9. Check every changed relative link resolves from its file. Do not translate the archive body and do not commit.

## Done when

- `doc/plan.md` no longer exists and `doc/roadmap/qtranslate/legacy-plan.md` exists; git records a rename.
- The archive has an English historical header, its Russian body is otherwise unchanged, and every relative link resolves (`../../architecture.md`, the design notes, and `plan.md`).
- The root plan, `doc/roadmap/README.md`, and `AGENTS.md` point to the active roadmap and the archive; `AGENTS.md` states the English documentation policy and the absence of a Jira key.
- Active documents do not reference `doc/plan.md`; only the archive, architecture §11 policy/history, release history, map nodes, and the journal may mention it.
- `git diff --name-only` contains only `doc/`, `AGENTS.md`, and the moved plan; no `src/`, `tests/`, `CMakeLists.txt`, `packaging/`, or build files.
- No build is required (documentation-only).

## Risks

- `git mv` must preserve history; do not copy and delete.
- Relative links from the new depth are the main breakage; check every `../../` target, not only the listed lines.
- Updating the root plan must be limited to the reference sentence; its goal, `## Done when`, and `## Stages` are fixed.
- `AGENTS.md` is loaded in every session; stale paths there affect all future work, so grep it last.
- The archive must remain clearly non-active; do not translate or edit its Russian body.
