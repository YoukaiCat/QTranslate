---
kind: task
status: done
detailed: true
depends: [01-archive-legacy-plan]
---

# Translate handoff and code-signing docs

## Task

Translate `doc/handoff.md` and `doc/code-signing.md` to English, update their references to the roadmap and archive, and record the manual checks that were never completed as known gaps in the handoff. Facts, hashes, tags, URLs, file sizes, test counts, and vendor requirements stay unchanged. These two files are the release/state reference for future work.

## Steps

1. `doc/handoff.md` (281 lines):
   - translate the title and all sections: Git, fullscreen hotkeys, releases, localization, code signing and UI Automation, free web endpoints, local offline translation, proofreading, LLM actions, distribution size, the 0.10.0 release, the next release, user rules, and commands/gotchas;
   - preserve commit hashes (`a6482e7`, `f50c1da`), tags, release URLs, Actions run URLs, file sizes, 23/23 test counts, paths, INI keys, class names, and log strings;
   - update stage references to the new roadmap numbering with the old numbers in parentheses; completed localization stays “old stage 12” or “localization (old stage 12, completed)”;
   - add a `## Known manual gaps` section with the never-completed checks from the old plan status table and architecture §9.2: tray behaviour; selection capture in Word, VS Code, and Telegram; the busy-hotkey tray notification; Win+V clipboard history; a real second monitor with a different scale; autostart after a real Windows reboot; real successful DeepL and Google Cloud API keys; SOCKS5 and an external authenticated proxy. Say they are known gaps, do not block new work, and link to the archive and architecture §9.2;
   - release process lines 228 and 243: replace `doc/plan.md` with `doc/roadmap/qtranslate/plan.md`; use `legacy-plan.md` only for history. Keep `doc/architecture.md` and `doc/releases/<version>.md`;
   - add one short current-state line: the active roadmap is `doc/roadmap/qtranslate/plan.md`, the old plan is archived at `legacy-plan.md`, and active docs are being translated in stage 1. Do not claim the migration is complete.
2. `doc/code-signing.md` (145 lines):
   - translate the title and sections 1-5: signature benefits, options 2.1-2.5, what to sign, CI integration, and decision/next steps;
   - preserve vendor names (SignPath Foundation, Azure Artifact Signing, OV/EV, GitHub attestations), certificate requirements, CI steps, `signtool`, URLs, and licensing notes;
   - step 5 at line 141: replace `doc/plan.md` with the active roadmap and point history to `legacy-plan.md`;
   - keep the link to `architecture.md` §4.4/§9.2.
3. Translate comments inside fenced snippets, but keep identifiers and literals. Keep quoted Russian UI labels only where the label itself matters.
4. Check that links in both files resolve from `doc/`. Do not commit.

## Done when

- Both files are fully English except quoted Russian UI labels (list them).
- `doc/handoff.md` has a `## Known manual gaps` section with the gaps listed above and a statement that they do not block new work.
- Release process steps point to the roadmap and archive; neither file references `doc/plan.md` except as history in the archive.
- Stage numbers use the new roadmap numbering with old numbers in parentheses for pending work and “old stage N” for completed old work.
- Facts (hashes, tags, URLs, sizes, test counts, vendor terms) are unchanged; all links resolve.
- Only these two files changed; no build required.

## Risks

- The handoff is long and full of exact numbers; do not paraphrase hashes, URLs, or sizes.
- Known gaps must come from the old plan status table and architecture §9.2, not from guesses; do not add new gaps.
- The handoff is the release handoff; do not claim the documentation migration is complete or that releases are signed.
- Code-signing recommendations are user decisions; translate them, do not change the recommendation.

## Outcome

Translated `doc/handoff.md` (281 → 319 lines) and `doc/code-signing.md` (145 → 154 lines) to English. `doc/handoff.md` gained the current-state note at the top and `## Known manual gaps`; `doc/code-signing.md` step 5 now points to the active roadmap and the archive.

- Stage references: future work uses `roadmap stage N (old stage M)` (3↔13, 4↔14, 5↔15, 6↔16, 7↔17); completed localization is `old stage 12, completed`; the known gaps cite the old stage numbers.
- Kept Russian strings: `Привет, мир` (the OpenRouter and LLM smoke-test output, `doc/handoff.md:72` and `:189`) and `Провайдеры перевода` (the Russian README.ru section name paired with `Translation providers`, `:25`). German `Automatische Erkennung` is kept as the example of a cut-off label; it is not Cyrillic. Russian UI labels elsewhere were replaced with the English source strings: `Pause hotkeys`, `Services`, `Settings → Hotkeys`, `Proofreading`.
- `## Known manual gaps` lists the eight gaps from the old plan status table and architecture §9.2 (tray; Word/VS Code/Telegram capture; busy-hotkey notification; Win+V; second monitor; autostart after a reboot; real DeepL/Google Cloud keys; SOCKS5/authenticated proxy), says they do not block new work, and links the archive and §9.2.
- References: the historical `Release 0.10.0` step links the archived plan; `Next release` and code-signing step 5 link the active `roadmap/qtranslate/plan.md`; history in code-signing points to `roadmap/qtranslate/legacy-plan.md`. No reference to `doc/plan.md` remains in either file. Paths are relative to `doc/`; `doc/releases/<version>.md` became `releases/<version>.md`.
- Facts were checked mechanically against `HEAD`: hashes, tags, URLs, byte counts, sizes, test counts, INI keys, class names and log strings are unchanged. Only the two documents changed; all relative links resolve; `git diff --check` is clean; no build was needed.
- Review found no issues. The coordinator found one ambiguous translation in `code-signing.md` §2.2 — “the application is reviewed manually” for “заявку рассматривают вручную” — and it was fixed to `the request is reviewed manually`.
- Found but not fixed, out of scope: `doc/code-signing.md:3` still lists only `v0.9.0`, `v0.9.1` and `v0.10.0` as unsigned although `v0.11.0` is released; this is a pre-existing fact list, not a translation error.
