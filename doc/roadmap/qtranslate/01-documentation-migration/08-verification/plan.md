---
kind: task
status: done
detailed: true
depends: [01-archive-legacy-plan, 02-architecture-core, 03-architecture-runtime, 04-handoff-and-signing, 05-provider-and-proofreading-notes, 06-llm-dictionary-and-i18n, 07-stale-facts]
---

# Verify the documentation migration

## Task

Run the final gate for stage 1 after task 07 fixed the stale facts: check roadmap frontmatter, all
relative links, English prose, the archive marker, the corrected directory tree and policy wording,
the user-approved comment exception (the only code change), and the user's spot-check of
`doc/architecture.md` and `doc/handoff.md`. This task closes the stage; it does not add features or
translate new material.

## Steps

1. Frontmatter and map index: check the root `doc/roadmap/qtranslate/plan.md` (`kind: root`,
   `status: in-progress`, `lang: en`), the stage (`kind: stage`), and every child node (`kind: task`,
   valid `status`, `detailed: true` where steps exist). The stage now has eight children: tasks
   01-07 `done` and this verification. Confirm `doc/roadmap/README.md` lists the `qtranslate` map
   with its status, one-line description, and link. Use the lead's tree overview or a grep over
   `doc/roadmap/**/plan.md`; fix only map metadata errors, never goals or criteria.
2. Link check: extract relative Markdown links from `doc/**/*.md`, `AGENTS.md`, `README.md`,
   `README.ru.md`, and `i18n/README.md`; resolve each against its file's directory; allow external
   URLs and in-page anchors. This includes `doc/roadmap/**/plan.md` of done nodes: an illustrative
   link there still has to resolve. Skip inline code spans and fenced blocks when extracting links,
   so path examples do not count as links. Pay special attention to the
   root → `legacy-plan.md`, the map README → map root, the archive → `../../...`, the stage → child
   tasks, and design-note cross-links. Use a one-off shell or Python command in `/tmp`; do not add
   scripts to the repo.
3. Cyrillic scan: run `grep -nP '[\x{0400}-\x{04FF}]'` over active docs. Allowed: `README.ru.md`,
   `doc/roadmap/qtranslate/legacy-plan.md`, quoted Russian UI labels (for example «Сервисы»,
   «Приостановить горячие клавиши», installer language names), and native language names in
   `i18n/README.md`. Everything else must be translated; review headings and prose manually because
   grep cannot distinguish quotes.
4. Old-plan references: grep `doc/plan.md` and `](plan.md)` over the Markdown documentation,
   `AGENTS.md`, the READMEs, and `src/`. Allowed: the archive, `doc/architecture.md` 11 (the
   migration policy and procedure), release history, map nodes, and the journal. The comment at
   `src/platform/selectiongrabber_win.cpp:248` must now point to
   `doc/roadmap/qtranslate/legacy-plan.md`; if it still says `doc/plan.md`, treat it as a regression
   of task 07 and fix that comment line only.
5. No unintended code changes: `git diff --name-only` against the stage base `94d3ec2` and the
   working tree must contain only `doc/**`, `AGENTS.md`, `i18n/README.md`,
   `README.md`/`README.ru.md` if a link changed, and `src/platform/selectiongrabber_win.cpp` (the
   user-approved comment line). `git diff -- src/` must show exactly one changed comment line and no
   other source, test, build, packaging, or CI change. If any other code file changed, stop and
   report; do not revert silently.
6. English completeness: confirm the title and all sections of `doc/architecture.md`,
   `doc/handoff.md`, all design notes, `i18n/README.md`, and `AGENTS.md` are English; check that
   architecture section 10 points to the active roadmap and section 11 is the English policy, and
   that the archive body is intentionally Russian. Confirm `AGENTS.md` points to the active roadmap,
   names the English docs and the archive, and states that there is no Jira key. Confirm
   `doc/handoff.md` records the known manual gaps, its release process references the roadmap and
   the archive, and the two READMEs are in sync.
7. Re-check the facts fixed by task 07, because a regression here would reopen the stage: every path
   in the architecture 3.1 tree exists and the tree covers the files in `CMakeLists.txt` and
   `tests/CMakeLists.txt`; 3.2 lists `tst_translations`, `tst_uilanguages`, and `uilanguages`; 11.1
   and 11.2 no longer claim that the docs are Russian, that `doc/plan.md` is active, or that the
   archive "will be" created; 11.2, 11.6 step 10, 11.7, 11.8, and the 11.9 "Code comments" and
   "Verification" rows record the same approved comment exception; `doc/handoff.md` uses "Text
   checking" for the planned tab; `doc/code-signing.md` lists `v0.11.0` as unsigned. If a new
   factual discrepancy appears, correct it in the affected document, re-run the link and Cyrillic
   checks, and record it in the outcome; do not expand into new translation work.
8. Close the residual contradictions left after task 07; these are documentation edits, no new
   translation, and no other policy changes. Line numbers are from `45a2cf6` (after task 07); locate
   by heading if they have shifted.
   - `doc/architecture.md` 11.7, last bullet (line 1820): "The migration is documentation-only. A
     build is not required. If a change accidentally touches code or tests, run ..." now
     contradicts the approved comment line. Rephrase: the migration is documentation-only except
     the one approved comment line in `src/platform/selectiongrabber_win.cpp`, which needs no
     build; any other code or test change requires the relevant Debug and Release checks and all
     23 tests.
   - 11.9, "Verification" row reason (line 1848): replace "Documentation-only migration" with a
     wording that names the exception, for example "Documentation-only migration, except the one
     approved comment line". Keep the decision cell and the rejected alternative unchanged.
   - 11.10, "Mixed language during migration" (line 1854): "The architecture document is mixed
     until stage 1 finishes" is false now. Put it in the past tense and keep the risk and the
     mitigation: the document was mixed while the migration ran; keeping the migration in one stage
     and prioritising the architecture and handoff limited that period.
   - 11.11, "Archive name and location" (line 1868): "the user can change it before stage 1 moves
     the file" is stale. State it as the accepted decision: the archive is
     `doc/roadmap/qtranslate/legacy-plan.md`, and stage 1 moved the file there.
   - `doc/roadmap/qtranslate/01-documentation-migration/01-archive-legacy-plan/plan.md:18`: the
     two illustrative Markdown links in the blockquote (the roadmap as `plan.md` and the
     architecture as `../../architecture.md`) quote the archive header, where those paths are
     relative to the archive, not to the task directory; from the task directory the architecture
     path does not resolve and would break the link check. Replace both links with inline code that
     contains only the paths (not the link markup), which keeps the quoted header text exact and
     leaves no link syntax on the line. Change nothing else on that line and do not edit the done
     task's text or outcome; record the choice in this task's `## Outcome`.
   - Do not rewrite any other policy paragraph or table row.
9. Ask the user to spot-check `doc/architecture.md` and `doc/handoff.md`; wait for the result. Apply
   terminology or fact fixes only to the affected documents, then re-run the link and Cyrillic
   checks. Record the request and result in the task outcome.
10. Update the current-state paragraph of `doc/handoff.md` (the two sentences after the date line):
    it still says the documents "are being translated in stage 1". State that the roadmap is active
    and the active documentation is English. Do not rewrite release history or the manual gaps.
11. Send the result to review, apply the remarks, then commit locally with the repository message
    style, including the `## Outcome` of this node and any metadata or fact fixes. In the outcome,
    state that the only code change is a comment line and that Windows Debug/Release builds and the
    23 tests were not run because the environment has no Windows/MSVC toolchain; do not claim they
    passed. Do not push.

## Done when

- Roadmap frontmatter is valid and every node has a kind and status; task nodes with steps have
  `detailed: true`; `doc/roadmap/README.md` lists the map with status, description, and link.
- Every relative Markdown link in active docs and in `doc/roadmap/**/plan.md`, including done
  nodes, resolves; the archive links and roadmap links are checked explicitly.
- Cyrillic remains only in the allowed files, quoted UI labels, and native language names; no
  Russian prose or headings remain in active docs.
- No active document points to `doc/plan.md` except the archive, architecture 11 policy/procedure,
  release history, map nodes, and the journal; the comment in
  `src/platform/selectiongrabber_win.cpp:248` points to the archive.
- `git diff --name-only` shows only documentation, `AGENTS.md`, `i18n/README.md`, and the
  user-approved comment change in `src/platform/selectiongrabber_win.cpp`; `git diff -- src/` shows
  that one comment line, and no other code, test, build, packaging, or CI changes exist.
- The task 07 facts still hold: the 3.1 tree matches the repository and the CMake source lists;
  11.1-11.2 are accurate; 11.2, 11.6-11.9 carry the approved comment exception; the handoff uses
  "Text checking" for the tab; `code-signing.md` includes `v0.11.0`; the source comment points to
  the archive.
- The residual §11 contradictions are closed: 11.7's last bullet, the 11.9 "Verification" reason,
  the 11.10 "Mixed language" risk, and the 11.11 archive item match the approved exception and the
  finished migration; no other policy text changed.
- The link check covers `doc/roadmap/**/plan.md`, including done nodes, and passes; the illustrative
  links at `01-archive-legacy-plan/plan.md:18` are inline code paths with no link markup and no
  longer break it.
- The outcome records the Windows Debug/Release builds and the 23 tests as not performed (no
  Windows/MSVC in this environment), not as passed.
- The user has spot-checked `doc/architecture.md` and `doc/handoff.md`, or their requested changes
  are applied and re-checked.
- `doc/handoff.md` states the migration is complete and the active docs are English.
- The stage's `## Done when` is satisfied and the lead can mark stage 1 done.
- The task is committed locally after review; nothing is pushed.

## Risks

- Link checkers can produce false positives for anchors and external URLs; verify hits manually.
- Quoted UI strings are legitimate Cyrillic; classify each hit instead of deleting text mechanically.
- The user spot-check may take time; do not mark the task done on a pending request.
- If a code file other than `src/platform/selectiongrabber_win.cpp` changed, stop and report; the
  migration allows only that one comment line.
- The Windows Debug/Release builds and the 23 tests cannot be run in this environment; record them
  as not performed. The reviewer must confirm the source diff is a single comment line with no
  behavior change.
- The link check includes the plans of done nodes; an illustrative Markdown link there is checked
  too. Fix only the offending example line, not the done task's history.
- The four §11 spots are the only policy edits here; do not reword neighbouring paragraphs or
  table rows.

## Outcome

Done. The final gate for stage 1 passed; the only code change in the whole stage is the
user-approved comment line.

- Frontmatter: the root is `kind: root`, `status: in-progress`, `lang: en`; the stage is
  `kind: stage`; tasks 01-07 are `done` and this task is `done`; every task with steps has
  `detailed: true`; `doc/roadmap/README.md` lists the map with status, description, and link.
- Links: 92 Markdown links in 35 files (`doc/**/*.md`, `AGENTS.md`, `README.md`, `README.ru.md`, and
  `i18n/README.md`): the 63 relative links all resolve, while 27 external URLs and 2 pure anchors
  were allowed without fetching; inline code spans and fenced blocks were skipped. The
  root-to-archive, map README-to-root, archive `../../...`, and design-note cross-links were
  included.
- Cyrillic: only allowed occurrences remain — `README.ru.md`, the Russian `legacy-plan.md` archive,
  native language names in the i18n table and the architecture UI-language lists, the `Русский`
  language-switch label in `README.md`, and quoted Russian strings (`Русский`, `Привет, мир`,
  `Провайдеры перевода`, installer labels); no Russian prose or headings remain in active docs.
- Old-plan references: only the archive, architecture §11, release history, map nodes, the journal,
  and this task's own text mention `doc/plan.md`; the comment at
  `src/platform/selectiongrabber_win.cpp:248` points to `doc/roadmap/qtranslate/legacy-plan.md`.
- Code scope: `git diff --name-only 94d3ec2` lists only documentation, `AGENTS.md`, `i18n/README.md`,
  and `src/platform/selectiongrabber_win.cpp`; `git diff -- src/` is exactly one comment line with no
  behavior change.
- English completeness: `doc/architecture.md`, `doc/handoff.md`, the design notes, `i18n/README.md`,
  and `AGENTS.md` are English; §10 points to the active roadmap; §11 is the English policy; the
  archive body is intentionally Russian; `doc/handoff.md` records the known manual gaps and
  references the roadmap and archive in its release process; the READMEs are in sync.
- Task 07 facts re-checked: the §3.1 tree has 160 checked entries (28 `.h/.cpp` pairs expanded) and
  0 missing paths; all 74 source references from `CMakeLists.txt` (including the resource script) and
  all 23 tests from `tests/CMakeLists.txt` are represented; §3.2 lists `tst_translations`, `tst_uilanguages`, and
  `uilanguages`; §11.1-11.2 are accurate; the exception appears in 11.2, 11.6 step 10, 11.7, 11.8,
  and the 11.9 "Code comments" and "Verification" rows; `doc/handoff.md` uses "Text checking";
  `doc/code-signing.md` lists `v0.11.0` as unsigned.
- Residual contradictions closed: §11.7's last bullet now names the approved comment exception, the
  11.9 "Verification" reason is "Documentation-only migration, except the one approved comment
  line", 11.10 uses the past tense for the mixed-language period, and 11.11 states the archive
  location as accepted and moved. No other policy paragraph or table row changed.
- `01-archive-legacy-plan/plan.md:18`: the two illustrative links were replaced by inline code
  (`plan.md`, `../../architecture.md`); no link syntax remains on the line and the rest of the done
  task's text is unchanged. The choice is recorded here because the done node is included in the
  link pass.
- `doc/handoff.md`: the current-state paragraph now says the active documentation is English and the
  migration to the tree roadmap and English baseline is complete, instead of "being translated in
  stage 1"; release history and manual gaps are unchanged.
- User spot-check: requested for `doc/architecture.md` and `doc/handoff.md`; the user answered
  "OK, everything is correct" and requested no changes.
- Windows Debug/Release builds and the 23 tests were not performed: this Linux environment has no
  Windows/MSVC toolchain or Qt. They are not claimed as passed; the only code change is a comment
  and does not alter behavior.
- Committed locally; nothing was pushed.
