---
kind: root
status: in-progress
lang: en
source: doc/architecture.md
branch: main
base: ef09e8c
---

# QTranslate — Roadmap

Move the project from the flat Russian plan to the tree roadmap used by the lead agent, translate all active project documentation to English, and continue the remaining product work in the order of the old plan. Product code stays unchanged during the documentation migration except for one user-approved comment line in `src/platform/selectiongrabber_win.cpp`, where a stale `doc/plan.md` reference is repointed to the archive; later stages deliver the remaining features.

The active roadmap is this file. The detailed Russian history of the old flat plan is in [`legacy-plan.md`](legacy-plan.md); stage 1 moved it there. The documentation and roadmap policy is in [`doc/architecture.md`](../../architecture.md), section 11. This is a personal project with no Jira issue numbers; commit messages follow the repository's existing style without a Jira key.

## Done when

- Every stage in `## Stages` is `done` or `dropped` with a reason.
- Active project documentation is English; only `README.ru.md`, the archived `legacy-plan.md`, and quoted Russian UI strings remain in Russian.
- `AGENTS.md` points to the active roadmap and the English documentation; all relative Markdown links in `doc/` resolve.
- The old flat plan is preserved as a clearly marked Russian archive and is not mistaken for the active plan.
- The documentation migration changes no code except one user-approved comment line in
  `src/platform/selectiongrabber_win.cpp`, which repoints a stale `doc/plan.md` reference to the
  archive. Windows Debug/Release builds and the 23 tests that existed at that time were unavailable
  in the current Linux environment; the outcome records them as not performed, not as passed.
- After any other code changes, the application builds in Debug and Release without `/W4` warnings
  and all 28 Windows tests pass; on Linux the same holds with `-Wall -Wextra` and 33 tests.

## History

The old flat plan completed stages 1-12 and delivered releases 0.9.0, 0.9.1, 0.10.0, and 0.11.0. It covers the application skeleton, the translation core and Google Translate, global hotkeys and selection capture, the popup and main-window scenarios, settings and autostart, DeepL and Google Cloud, proxies, release polish, in-place replacement, the installer and CI, and localisation into 15 UI languages. After 0.11.0 the project also added fullscreen hotkey pause behaviour and the QTranslate branding.

Old stages 13 (free web endpoints) and 16 (LLM actions) are partially implemented. DeepL free web is a `free-web` mode of the DeepL service; MyMemory and Mozhi are implemented as opt-in free APIs; the OpenAI-compatible translation provider, OpenRouter by default, is implemented. Old stage 11 (Linux support) is implemented on `main` for 0.12.0: 33 Linux tests, X11 full, Wayland limited, trixie and native Astra packages. Old stages 14, 15, and 17 are design-only. The old plan records known manual gaps: tray behaviour, Word/VS Code/Telegram capture, Win+V clipboard history, a real second monitor, autostart after a real reboot, real DeepL/Google Cloud keys, and an external authenticated proxy. These gaps are recorded in `doc/handoff.md` and do not block new work. The detailed rationale and step-by-step history remain in the Russian archive.

## Stages

1. [Documentation migration and English baseline](01-documentation-migration/plan.md) — migrate to the tree roadmap and translate all active documentation to English; no code changes except one approved comment line.
2. [Linux support (old stage 11)](02-linux/plan.md) — Linux x86_64 build, X11 as the full target, Wayland limited with clear warnings.
3. [Free web endpoints (old stage 13)](03-free-web-endpoints/plan.md) — MyMemory, Mozhi, and other no-key endpoints as optional fallbacks.
4. [Local offline translation (old stage 14)](04-local-offline-translation/plan.md) — blocked: design only; Argos Translate through CTranslate2 is not being implemented now, and LLM providers cover the need.
5. [Proofreading (old stage 15)](05-proofreading/plan.md) — separate spelling, grammar, and punctuation checking mode.
6. [LLM actions (old stage 16)](06-llm-actions/plan.md) — summarise, rewrite, explain, pronounce, and structured proofreading.
7. [Dictionary and glossaries (old stage 17)](07-dictionary/plan.md) — dictionary mode, local glossaries, and additional sources.

The order follows the old plan. Stages 3-7 are independent except where they state a dependency.
