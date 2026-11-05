---
kind: stage
status: in-progress
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
