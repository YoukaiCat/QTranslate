---
kind: stage
status: blocked
base: 80fefa2
---

# Proofreading (old stage 15)

## Goal

Add a separate text-checking mode that finds spelling, grammar, and punctuation errors, shows them in a list or highlight, and replaces the text with corrections. It must not be mixed with translation or its settings.

## What to do, what is touched

A separate settings tab ("Text checking") and independent `proofreading/*` keys; a provider-independent `ProofreadingService`/`ProofreadingReply`/`ProofreadingIssue` contract with UTF-16 offsets; LanguageTool for grammar and punctuation; Hunspell/Nuspell and Yandex Speller for spelling; an error list with "Replace", "Replace all" and "Ignore", undo in the main window; replacement in an external application with confirmation; check-before-translation off by default. The optional LLM checker through a diff is delivered by stage 6 against the same contract (see the boundaries below). The old plan's stage 15 and `doc/proofreading.md` list the details.

The work is split into seven children. Each one has its own result and gate; later children extend the same contract, settings group and main-window flow.

1. [01-proofreading-core](01-proofreading-core/plan.md) — the value types, the reply/service interface, the manager with request channels, cancellation and the online gate, the offset-preserving text splitter, and the `proofreading/*` settings group; a fake-provider test, no network and no UI.
2. [02-languagetool-parser](02-languagetool-parser/plan.md) — the LanguageTool `matches[]` parser (UTF-16 offsets, replacements, rule id, category mapping, error classification), fixtures, and the `languagetool` language branch in `Languages`; `tst_languagetoolparser`.
3. [03-languagetool-service](03-languagetool-service/plan.md) — `LanguageToolService` on `QueuedWebService`: the public endpoint and a self-hosted URL, key, language and category filtering, 20 000-character chunking with an offset shift, cancellation and error mapping; registered in `AppController`; `tst_languagetoolservice`.
4. [04-text-checking-tab](04-text-checking-tab/plan.md) — the separate "Text checking" tab (enabled, provider, language, endpoint, key, categories, check before translation, automatic replacement, online-services switch), `tst_settingsdialog`, all 14 `.ts`.
5. [05-main-window-check-and-replace](05-main-window-check-and-replace/plan.md) — the Check button, issue highlighting in the source field, the issue list with Replace/Ignore, Replace all with a preview, undo through the document, copying the corrected text, the request states, and the check-before-translation confirmation in the main window; `tst_proofreadingpanel`.
6. [06-spelling-providers](06-spelling-providers/plan.md) — Yandex Speller (remote, gated) and Hunspell/Nuspell (local, with dictionaries and packaging) as spelling providers; each provider adds its languages, settings row and tests.
7. [07-external-selection-proofreading](07-external-selection-proofreading/plan.md) — a fifth global hotkey action across Windows, X11, the Wayland portal and the CLI, a confirmation window for the captured selection, and in-place replacement through `WindowActivation`; `proofreading/hotkey` is added here.

Boundaries recorded by the split:

- The LLM checker stays in stage 6, whose `proofread` action owns the diff and the confirmation; stage 5 only reserves the `llm` provider id in the settings, so stage 6 implements it against the same `ProofreadingService` interface without reworking the settings group.
- Online providers are gated by a proofreading-scoped switch (`proofreading/allowOnlineServices`, default `false`) rather than by `translation/allowFreeServices`: the checking mode must not share or change translation settings, and a local engine (Hunspell/Nuspell) stays available with the switch off. A self-hosted LanguageTool URL also needs the switch, so one rule covers every remote endpoint and no host-name heuristic is needed.
- `proofreading/hotkey` is added by child 07, when a hotkey exists to register, so no dead setting is written in child 01 or shown in child 04.
- The reply object replaces the design note's `check(request, callback)` signature to match `TranslationReply` (`src/core/translationservice.h`): ownership, `finished()` outside `check()` and `abort()` → `Cancelled` stay identical to the translation side. `TranslationError` is reused instead of a second error enum, so `ui/translationerrortext` and the existing wording apply.

## Done when

- The separate tab and settings do not affect translation.
- LanguageTool finds spelling, grammar, and punctuation issues for the main languages.
- Hunspell/Nuspell/Yandex Speller work for spelling.
- Single and bulk replacement plus replacing the text with the corrected version work and undo is preserved.
- Rate-limit, key, and network errors are clear and the application does not hang.
- Tests and README/architecture are updated.

## Depends on

None technically; follows stage 4 in the old order and is independent. The optional LLM provider already exists.

## Risks and what is needed from the user

Proofreading APIs have keys and limits; text may be private; automatic replacement is risky. The user must choose the providers and defaults, provide keys where needed, and approve the privacy and replacement rules. The default must keep automatic replacement off.

- The provider set is a user decision: Hunspell/Nuspell adds a native library plus dictionaries on both platforms and changes packaging, while Yandex Speller sends the text to a third party. `06-spelling-providers` is planned around both; if the user drops one, that child shrinks to the remaining provider instead of being replaced.
- The online gate defaults to off, so the first run with LanguageTool shows a "needs online text checking" state until the switch is enabled. This is the mechanism for the privacy rule above and is reported with the split for approval, not decided silently.
- Automatic replacement stays off, `proofreading/checkBeforeTranslation` stays off, and every replacement in another application needs explicit confirmation; single and bulk replacement in the main window are always one undo step.
- LanguageTool rule false positives and language-dependent punctuation are the user-visible risk of the first provider; the UI always shows the message and the suggested replacements instead of applying them silently.

## Open questions (blocked since 2026-11-09)

The split is written but not accepted; three decisions belong to the user:

1. Provider set for the stage: all three providers (as written; Hunspell/Nuspell needs a native
   library and packaged dictionaries on both platforms), LanguageTool + Yandex Speller (Hunspell
   deferred, `## Done when` changed), or LanguageTool only.
2. The proofreading-scoped online switch `proofreading/allowOnlineServices`, default `false`, as the
   privacy gate for every remote endpoint including a self-hosted LanguageTool. Alternatives: reuse
   `translation/allowFreeServices` (couples checking to translation) or no gate.
3. Whether `07-external-selection-proofreading` (a fifth global hotkey across Windows, X11, the
   Wayland portal and the CLI) stays in this stage or moves to a later stage. `doc/proofreading.md`
   §10 lists it, the stage `## Done when` does not.
