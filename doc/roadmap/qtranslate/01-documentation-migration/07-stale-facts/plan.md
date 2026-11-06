---
kind: task
status: done
detailed: true
depends: [02-architecture-core, 03-architecture-runtime, 04-handoff-and-signing, 05-provider-and-proofreading-notes]
---

# Fix stale references and facts after the migration

## Task

Tasks 02-06 produced English documents but deferred four statements that no longer match the
repository or contradict other documents: the `src/`/`tests/` tree in `doc/architecture.md` 3.1-3.3,
the pre-migration claims in 11.1-11.2, the settings-tab name in `doc/handoff.md`, and the list of
unsigned releases in `doc/code-signing.md`. Correct only those facts, and repoint the one stale
`doc/plan.md` comment in `src/platform/selectiongrabber_win.cpp`. The comment fix is the only code
change in the stage and is approved by the user; every other edit is documentation, plus the policy
update in step 3 that records the exception. Do not translate new material, change any other part of
the migration policy, or touch any other code. The audit sources are the working tree,
`CMakeLists.txt`, `tests/CMakeLists.txt`, `doc/releases/`, and `i18n/`; read them, never modify
them.

Line numbers below are from `d22976f`; re-locate the text if the files have moved.

## Steps

1. `doc/architecture.md` 3.1 (the tree at lines 121-216) — make it match the repository. It is still
   the 0.11.0 layout and misses what was added or renamed later. Add or repair:
   - `src/ai/openaichat.h/.cpp` — a new `ai/` directory next to `core/`, `net/`, `services/`; the
     OpenAI-compatible Chat Completions transport;
   - `src/core/uilanguages.h/.cpp` — UI language codes, native names, and resource names;
   - `src/services/deeplwebparser.h/.cpp`, `deeplwebservice.h/.cpp`, `openaicompatibleservice.h/.cpp`;
   - `src/platform/autostart_stub.cpp` and `secretprotector_stub.cpp` next to the other stubs;
   - `tests/`: `tst_appinfo.cpp`, `tst_deeplwebparser.cpp`, `tst_deeplwebservice.cpp`,
     `tst_hotkeymanager.cpp`, `tst_openaichat.cpp`, `tst_openaicompatibleservice.cpp`,
     `tst_translations.cpp`, `tst_uilanguages.cpp`;
   - `doc/releases/`: the four release notes (`0.9.0`-`0.11.0`), not just `0.11.0.md`;
   - `i18n/`: `qtranslate_*.ts` for all shipped languages, not only `qtranslate_ru_RU.ts`;
   - `doc/roadmap/qtranslate/`: the seven stage directories
     `01-documentation-migration/`-`07-dictionary/`, with their contents collapsed as `...`; do not
     list individual task directories, they change with every plan.
   Keep the existing annotation style. Verify every path in the finished tree with `ls` or
   `test -e`, and check every file against the source lists in `CMakeLists.txt:22-42` and
   `tests/CMakeLists.txt`. Do not invent paths.

2. `doc/architecture.md` 3.2-3.3 — complete the mapping to the current targets:
   - add `uilanguages` to the `qtranslate_core` core list in the 3.2 table (line 225);
   - add `tst_translations` and `tst_uilanguages` to the tests row (line 227); the table already
     lists `tst_appinfo`, the DeepL web tests, `tst_hotkeymanager`, `tst_openaichat`, and
     `tst_openaicompatibleservice`;
   - the lead-in says 3.1 "is the target" (line 220); if that no longer fits the corrected tree,
     change only that sentence to say that 3.1 shows the current layout and the table maps it to
     the targets. Do not restructure the section;
   - 3.3 line 231 says `qtranslate_core` is built "from `core/`, `net/`, `services/`"; add `ai/`
     (`CMakeLists.txt:22-23`).

3. `doc/architecture.md` 11 — correct the stale facts and record the user-approved code exception
   (lines are from `d22976f`).
   - 11.1 (line 1660): it opens with "The project documentation is currently Russian and
     authoritative" and names `doc/plan.md`. After stage 1 this is false. Rewrite that sentence in
     the past tense and mark it as the state before the migration. Keep the
     `README.md`/`README.ru.md`/release-notes sentence, the summary of the archive contents, and the
     no-Jira sentence.
   - 11.2 (line 1668): "The old plan will be archived as
     `doc/roadmap/qtranslate/legacy-plan.md` in stage 1" is done; state that stage 1 archived it
     there. Keep the decision sentence, the active-map and map-list sentences, and the stage order.
     Then qualify "Do not change product code during the migration" with the one user-approved
     exception: the stale `doc/plan.md` reference in the comment at
     `src/platform/selectiongrabber_win.cpp:248` is repointed to the archive, and nothing else in
     the code changes.
   - 11.6 step 10 (line 1770): "verify ... that no code changed" becomes "that no code changed
     except the one approved comment line in `src/platform/selectiongrabber_win.cpp`". Leave steps
     1-9 alone.
   - 11.7 (line 1777): "Code comments and source files are not touched" gets the same exception;
     any other documentation change that would require a code comment change stays deferred to a
     separate task.
   - 11.8 (line 1786): the diff check allows documentation, `AGENTS.md`, and the approved comment
     line in `src/platform/selectiongrabber_win.cpp`; no other source, test, build, or packaging
     file changes.
   - 11.9 (line 1796): in the "Code comments" row, change the decision to "one approved comment
     line; everything else unchanged"; keep the reason (a separate task avoids unrelated code churn)
     and add that the stale path would otherwise point to a deleted file; keep the rejected
     alternative "Translate now". Do not reword the remaining rows.
   - 11.9 (line 1804): in the "Verification" row, change the decision to "Link check, English
     review, frontmatter check, no code changes except the one approved comment line"; keep the
     reason and the rejected alternative.
   - Do not touch 11.3-11.5, 11.10, or 11.11; 11.6 changes only step 10. This is the only policy
     change, and it records the user's approval. Do not turn the section into a changelog or add
     dates.
   - If another sentence in 11.1-11.2 asserts that the migration is still pending, fix it the same
     way; if a fix would require a further policy change, report it in the outcome instead.

4. `doc/handoff.md:165` — the planned settings tab is called **"Text checking"** in the canonical
   sources: `doc/architecture.md` 4.15 ("A separate \"Text checking\" settings tab") and
   `doc/proofreading.md:111` plus the checklist at line 166. Replace the **Proofreading** label with
   **Text checking**. Keep the section heading `## Proofreading`, the feature name, and the
   `ProofreadingService`/`ProofreadingIssue` identifiers; the feature is still called proofreading,
   only the tab is not.

5. `doc/code-signing.md:3` — add `v0.11.0` to the unsigned releases. The release exists (root plan
   `## History`, `doc/releases/0.11.0.md`) and its notes state that the installer is not code-signed
   (line 31); `doc/architecture.md` 9.2 says there is no certificate in the project. Keep
   "Status: not implemented" and say nothing about future releases.

6. `src/platform/selectiongrabber_win.cpp:248` — the user-approved exception to the documentation-only
   rule and the only code change in stage 1. In the comment that reads "... (see the stage 3 notes
   in doc/plan.md)", replace `doc/plan.md` with `doc/roadmap/qtranslate/legacy-plan.md`. Keep the
   rest of the comment, including the old stage number 3: the archive contains those notes. Change
   only this comment line; do not reformat, rename, or touch any other source, test, build,
   packaging, or CI file. The change cannot alter behavior, but this Linux environment has no
   Windows/MSVC toolchain or Qt, so do not run or claim a Windows Debug/Release build or the 23
   tests; the outcome must state that they were not performed. The stage goal and the root
   `## Done when` record this exception, so no further approval is needed.

7. Re-check the result: every relative link in the touched files resolves; `git diff --check` is
   clean; no Cyrillic was added; `git diff --name-only` contains only `doc/architecture.md`,
   `doc/handoff.md`, `doc/code-signing.md`, and `src/platform/selectiongrabber_win.cpp`;
   `git diff -- src/` shows exactly one changed comment line and nothing else.

8. Send the diff to review. The reviewer must confirm that the source diff is a single comment line
   and that no behavior changed; apply the remarks, then commit locally with the repository message
   style. Do not push.

## Done when

- 3.1 lists the actual `src/`, `tests/`, `doc/releases/`, `i18n/`, and roadmap tree; every listed
  path exists and every file from `CMakeLists.txt` and `tests/CMakeLists.txt` is represented (or
  covered by an explicit `...`).
- 3.2 lists all 23 tests, including `tst_translations` and `tst_uilanguages`, and lists
  `uilanguages` for `qtranslate_core`; 3.3 names `ai/` as part of `qtranslate_core`.
- 11.1 describes the pre-migration state in the past tense; 11.2 records the archive as done and
  carries the one user-approved code exception; 11.6 step 10, 11.7, 11.8, and the 11.9 "Code
  comments" and "Verification" rows match that exception; every other part of 11 is unchanged.
- `doc/handoff.md` names the planned tab "Text checking" while the feature stays Proofreading; the
  identifiers are unchanged.
- `doc/code-signing.md` lists `v0.9.0`, `v0.9.1`, `v0.10.0`, and `v0.11.0` as unsigned.
- The comment at `src/platform/selectiongrabber_win.cpp:248` points to
  `doc/roadmap/qtranslate/legacy-plan.md` and keeps the stage 3 reference.
- All relative links in the touched documents resolve; `git diff --check` is clean; no Cyrillic was
  added.
- `git diff --name-only` shows only the three documents and `src/platform/selectiongrabber_win.cpp`;
  `git diff -- src/` touches only the `doc/plan.md` reference in that comment, with no other source,
  test, build, packaging, or CI changes.
- The outcome records that Windows Debug/Release builds and the 23 tests were not performed because
  the environment has no Windows/MSVC toolchain; it does not claim they passed.
- The task is committed locally after review; nothing is pushed.

## Risks

- The 3.1 tree is easy to get wrong from memory; audit it against the files and the two CMake
  source lists, not against the prose of other documents.
- 11 is the user's migration policy. The only policy change is the user-approved comment exception in
  11.2, 11.6 step 10, 11.7, 11.8, and the 11.9 row; every other paragraph keeps its wording. Do not
  add further exceptions.
- `doc/releases/0.11.0.md` only says the installer is not signed; do not turn the release list into
  a signing roadmap or claim a date.
- The only code change is a comment line, but it is still code: the reviewer must confirm that no
  other line changed and that behavior is unaffected. Windows Debug/Release and the 23 tests are
  unavailable here, so the outcome must record them as not performed and must never claim a pass.
  Do not slip in any other "one-line" code change.

## Outcome

Fixed the four stale facts and the one user-approved source-comment line; no behavior change.

- `doc/architecture.md` 3.1 now matches the repository: added `src/ai/openaichat`, `src/core/uilanguages`,
  the DeepL web and OpenAI-compatible services, the `autostart`/`secretprotector` stubs, eight tests,
  the four release notes, all 14 shipped `i18n/qtranslate_*.ts` files plus `i18n/README.md`, and the
  seven roadmap stage directories with their contents collapsed. Two pre-existing tree errors were
  also fixed: the nonexistent `resources/qtranslate.qrc` line was removed, and `resources/icons` now
  lists the real `app.ico` and `app.png` instead of `tray-*.png`. The tree keeps its architectural
  scope and still does not list root files such as `README.md`, `AGENTS.md`, or `docs/screenshots/`.
- 3.2 adds `uilanguages` to `qtranslate_core` and `tst_translations`/`tst_uilanguages` to the tests
  (23 tests in total); its lead-in now says that 3.1 shows the current layout and the table maps it to
  the targets. 3.3 names `ai/` as part of `qtranslate_core`.
- 11.1 describes the pre-migration state in the past tense, 11.2 records the archive as done and adds
  the approved comment exception, and 11.6 step 10, 11.7, 11.8, and the 11.9 "Code comments" and
  "Verification" rows carry the same exception. 11.3-11.5, 11.10, 11.11, and 11.6 steps 1-9 are
  unchanged. 11.7 keeps the sentence "The migration is documentation-only", which is now qualified by
  the exception above; the task forbids rewriting further parts of 11, so it is left as is.
- `doc/handoff.md` calls the planned settings tab "Text checking" while the section heading, feature
  name, and identifiers stay Proofreading. `doc/code-signing.md` lists `v0.11.0` among the unsigned
  releases and keeps "Status: not implemented".
- The comment at `src/platform/selectiongrabber_win.cpp:248` points to
  `doc/roadmap/qtranslate/legacy-plan.md` and keeps the stage 3 reference; `git diff -- src/` is
  exactly that one comment line.
- Verification: every path in the 3.1 tree exists (132 entries) and every source from
  `CMakeLists.txt` (73 files, including the stubs) and every test from `tests/CMakeLists.txt` is
  represented; 23 tests appear in 3.2; all relative links in the touched documents and a full-document
  scan resolve; `git diff --check` is clean; no Cyrillic was added; only the three documents,
  `src/platform/selectiongrabber_win.cpp`, and the planner's stage replan files changed. This commit
  also carries that replan: the split of `07-verification` into `07-stale-facts` and
  `08-verification`, the stage/root/journal updates, and the exception wording in the root and stage
  plans.
- Review found no further defects; one accepted remark was fixed: 11.1 no longer says that stage 1
  completed, because the stage and `08-verification` are still open. Out of scope and not changed:
  11.10/11.11 still contain pre-migration statements, and
  `01-archive-legacy-plan/plan.md:18` has an old link that only resolves from the archive.
- Windows Debug/Release builds and the 23 tests were not performed: this Linux environment has no
  Windows/MSVC toolchain or Qt. They are not claimed as passed; the code change is a comment and does
  not alter behavior.
- Committed locally; nothing was pushed.
