---
kind: stage
status: done
base: 94d3ec2
---

# Documentation migration and English baseline

## Goal

Replace the flat Russian plan with the tree roadmap, translate all active project documentation to English, preserve the old plan as a Russian historical archive, and update project references. No product code changes except the user-approved one-line comment fix in `07-stale-facts`.

## What to do, what is touched

Create the stage directory and tasks; move `doc/plan.md` to `doc/roadmap/qtranslate/legacy-plan.md` and add an English archive header with updated relative links; translate `doc/architecture.md` sections 1-10 and keep section 11 except the approved exception; translate `doc/handoff.md` and record the known manual gaps from the old plan, `doc/code-signing.md`, `doc/dictionary.md`, `doc/free-endpoints.md`, `doc/llm-actions.md`, `doc/local-translation.md`, `doc/proofreading.md`, and `i18n/README.md`; update design-note stage references to the new roadmap numbering with old numbers in parentheses; update `AGENTS.md` with the English documentation policy, roadmap location, archive location, release process, and the absence of a Jira key; search for `doc/plan.md` references and update active documents; update `README.md`/`README.ru.md` only if a link changed; record the user-approved one-line comment exception in `doc/architecture.md` 11.2 and 11.6-11.9; verify links, frontmatter, and English prose. The roadmap README and the root plan already exist; the lead creates the stage and task nodes.

## Done when

- `doc/roadmap/README.md` lists the `qtranslate` map and the root plan has valid frontmatter.
- `doc/plan.md` is moved to `legacy-plan.md` with a clear English archive header and working relative links.
- `doc/architecture.md` is fully English and section 10 points to the roadmap.
- `doc/handoff.md` is fully English, records the known manual gaps, and its release process references the roadmap and archive.
- The design notes and `i18n/README.md` are fully English.
- `AGENTS.md` points to the roadmap and English docs and states there is no Jira key.
- No active document points to `doc/plan.md` except the archive and historical notes.
- All relative Markdown links in `doc/` resolve.
- No Cyrillic remains in active docs except allowed files and quoted Russian UI strings.
- `git diff --name-only` contains only documentation, `AGENTS.md`, and the user-approved
  comment-only change in `src/platform/selectiongrabber_win.cpp`; there are no other code changes.
- The user has spot-checked `doc/architecture.md` and `doc/handoff.md`.

## Depends on

None.

## Risks and what is needed from the user

The translation is large (roughly 4,000 lines), so split it into tasks and start with the architecture and handoff; translation may change technical nuance, so keep the Russian archive and ask the user to spot-check; moving the old plan can break relative links, so update them and run a link check; the architecture document is mixed until the stage finishes, so keep the migration in one stage. The stage also carries one approved code change — a single comment line in `src/platform/selectiongrabber_win.cpp`; Windows Debug/Release builds and the 23 tests cannot be run in this Linux environment, so the outcomes must record them as not performed.

The user has decided to continue on `main` and approved the archive name and location `doc/roadmap/qtranslate/legacy-plan.md`. The user also approved the one-line comment fix in `src/platform/selectiongrabber_win.cpp` despite the documentation-only rule. There is no Jira key and the lead must not ask for one. Local commits follow `AGENTS.md`; do not push without an explicit user request.

## Outcome

Done. The flat Russian plan is archived as `doc/roadmap/qtranslate/legacy-plan.md`, the tree roadmap is in
place, and the active documentation is English. The stage was delivered in eight task commits on `main`
(`a194f23`, `5a57dfd`, `aab459b`, `7195339`, `9216ff7`, `4e34ff1`, `45a2cf6`, `b53f412`), preceded by the
preparation and planner-split commits `94d3ec2` (the stage base) and `99ac5af`.

- Tasks 01-06 moved the archive and translated the architecture, handoff, code-signing, provider,
  proofreading, LLM, dictionary, and i18n documents; stage references use the new roadmap numbers with the
  old plan numbers in parentheses. Task 07 fixed the stale facts found during the work (the 3.1 repository
  tree, the 3.2 source and test lists, §11.1, the code-signing unsigned-release list, the handoff "Text
  checking" tab name), applied the user-approved one-line comment fix at
  `src/platform/selectiongrabber_win.cpp:248`, and recorded the comment exception in §11.2 and §11.6-11.9.
  Task 08 was the final gate: frontmatter, links, Cyrillic, old-plan references, English completeness, and
  code scope; it closed the four residual §11 contradictions and updated the handoff current-state
  paragraph.
- Deviation from the original plan: the stage was split into eight tasks instead of seven when the user
  chose to fix the stale source comment in this stage; `07-verification` became `07-stale-facts` plus
  `08-verification`. Task 07 also fixed two pre-existing tree errors (`resources/qtranslate.qrc` did not
  exist; `resources/icons` listed `tray-*.png` instead of `app.ico`/`app.png`).
- The READMEs were not changed: no link in them pointed to the old plan, so there was nothing to repoint.
- Acceptance review of the integrated stage found no code, link, or fact regression. It requested two
  consistency fixes, both applied: §11.11 "Branch" now records the decision to stay on `main`, and two
  non-UI Russian quotes in the done task plans were paraphrased in English, so no non-quoted Russian prose
  remains there. The only other Cyrillic is in allowed files, native language names, quoted UI/test
  strings, and the `Провайдеры перевода` section name of `README.ru.md`.
- The only code change in the stage is the user-approved comment line in
  `src/platform/selectiongrabber_win.cpp`; it does not change behavior. Windows Debug/Release builds and the
  23 tests were not performed because this Linux environment has no Windows/MSVC toolchain and the
  installed Qt 6.10.2 lacks `Qt6LinguistTools`; they are recorded as not performed, not as passed.
- Deferred, pre-existing documentation gaps found during the review and not changed here: `architecture.md`
  §4.14 says "the other 14 tests are shared" while the suite has 20 shared tests, the §8 test table
  describes 20 of the 23 tests, and `README.md` calls Linux support "in development" while stage 2 is
  `todo`. These come from before the migration and do not block it. Commit `99ac5af` has a Russian body;
  rewriting local history for it was not considered worth the churn.
- User decisions: continue on `main`; archive at `doc/roadmap/qtranslate/legacy-plan.md`; the one-line
  comment exception; no Jira key; the spot-check of `doc/architecture.md` and `doc/handoff.md` was
  confirmed with "OK, everything is correct".
- Status: done. The next node is `02-linux` (stage, `todo`, no tasks yet); the root map stays
  `in-progress`. All commits are local; nothing was pushed.
