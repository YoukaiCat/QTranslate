# QTranslate roadmap journal

## Where we left off

Current node: `01-documentation-migration/01-archive-legacy-plan` (task, `done`). The old flat plan is archived at `doc/roadmap/qtranslate/legacy-plan.md` with an English header, and active references point to the roadmap or the archive. Next: start the coordinator for `02-architecture-core`; tasks 04-06 are also unblocked, and 07 waits for all of them. Stage 1 remains `in-progress` with `base: 94d3ec2`.

No question is waiting for the user. The working tree is clean after the task commit; the commits stay local (no push).

## Journal

- 2026-11-05 — Roadmap preparation. Created stage nodes 01-07 from the root plan, added this journal, set the root to `status: in-progress`, `branch: main`, `base: ef09e8c`. User decisions: continue on `main`; keep the archive at `doc/roadmap/qtranslate/legacy-plan.md`. Rejected: a separate `roadmap/qtranslate` branch (local commits only, no need to isolate); another archive location (the architecture and migration plan already assume the proposed path). No Jira key: personal project, commit messages follow the repository style without one.
- 2026-11-05 — Committed the roadmap preparation as `94d3ec2` (architecture section 11, map root, seven stage nodes, journal, map README). Local only; no push. Stage 1 set to `in-progress` with `base: 94d3ec2`.
- 2026-11-05 — The planner split stage 1 into seven tasks: archive and references first; then architecture sections 1-4.7 and 4.8-10; handoff with code-signing; provider and proofreading notes; LLM, dictionary, and i18n; verification last. Dependencies: 03 after 02, 04-06 after 01, 07 after all. Committed as `99ac5af`.
- 2026-11-05 — The session ended while the coordinator for `01-archive-legacy-plan` was working; its edits stayed uncommitted in the working tree. A new coordinator is finishing the task from that state instead of redoing it.
- 2026-11-05 — Task `01-archive-legacy-plan` done and committed locally. Moved `doc/plan.md` to `doc/roadmap/qtranslate/legacy-plan.md` with `git mv`, added the English archive header, rewrote the six relative links, and repointed active references in the root plan, map README, `AGENTS.md`, `doc/handoff.md`, `doc/code-signing.md`, and `doc/dictionary.md`. Verification: the old-path grep finds only allowed places; `git diff --name-only` is documentation-only; no build. The interrupted work was verified and finished, not redone. No push.
