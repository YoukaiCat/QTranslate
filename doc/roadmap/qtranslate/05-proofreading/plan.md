---
kind: stage
status: in-progress
base: eb91f6b
---

# Proofreading (old stage 15)

## Goal

Add a separate text-checking mode that finds spelling, grammar, and punctuation errors, shows them in a list or highlight, and replaces the text with corrections. It must not be mixed with translation or its settings.

## What to do, what is touched

A separate settings tab ("Text checking") and independent `proofreading/*` keys; a provider-independent `ProofreadingService`/`ProofreadingReply`/`ProofreadingIssue` contract with UTF-16 offsets; LanguageTool for spelling, grammar and punctuation — the only provider of this stage (user decision of 2026-11-09); an error list with "Replace", "Replace all" and "Ignore", undo in the main window; replacement in an external application with confirmation; check-before-translation off by default. The optional LLM checker through a diff is delivered by stage 6 against the same contract (see the boundaries below). The old plan's stage 15 and `doc/proofreading.md` list the details.

The work is split into six active children; the `06` slot is a dropped record of the spelling providers that the user deferred on 2026-11-09. Each child has its own result and gate; later children extend the same contract, settings group and main-window flow.

1. [01-proofreading-core](01-proofreading-core/plan.md) — the value types, the reply/service interface, the manager with request channels, cancellation and the online gate, the offset-preserving text splitter, and the `proofreading/*` settings group; a fake-provider test, no network and no UI.
2. [02-languagetool-parser](02-languagetool-parser/plan.md) — the LanguageTool `matches[]` parser (UTF-16 offsets, replacements, rule id, category mapping, error classification), fixtures, and the `languagetool` language branch in `Languages`; `tst_languagetoolparser`.
3. [03-languagetool-service](03-languagetool-service/plan.md) — `LanguageToolService` on `QueuedWebService`: the public endpoint and a self-hosted URL, key, language and category filtering, 20 000-character chunking with an offset shift, cancellation and error mapping; registered in `AppController`; `tst_languagetoolservice`.
4. [04-text-checking-tab](04-text-checking-tab/plan.md) — the separate "Text checking" tab (enabled, provider, language, endpoint, key, categories, check before translation, automatic replacement, online-services switch), `tst_settingsdialog`, all 14 `.ts`.
5. [05-main-window-check-and-replace](05-main-window-check-and-replace/plan.md) — the Check button, issue highlighting in the source field, the issue list with Replace/Ignore, Replace all with a preview, undo through the document, copying the corrected text, the request states, and the check-before-translation confirmation in the main window; `tst_proofreadingpanel`.
6. [06-spelling-providers](06-spelling-providers/plan.md) — **dropped** on 2026-11-09: Yandex Speller and Hunspell/Nuspell are not part of the first version. The directory stays as the deferred design record and is not work.
7. [07-external-selection-proofreading](07-external-selection-proofreading/plan.md) — a fifth global hotkey action across Windows, X11, the Wayland portal and the CLI, a confirmation window for the captured selection, and in-place replacement through `WindowActivation`; `proofreading/hotkey` is added here.

Boundaries recorded by the split:

- The LLM checker stays in stage 6, whose `proofread` action owns the diff and the confirmation. Stage 5 does not reserve a provider id for it: the settings accept only the provider that ships, and stage 6 adds `llm` to `proofreadingProviderIds()` together with its service, so no dead value is stored or shown before then.
- Online checking is gated by a proofreading-scoped switch (`proofreading/allowOnlineServices`, default `false`, approved by the user on 2026-11-09) rather than by `translation/allowFreeServices`: the checking mode must not share or change translation settings. With the LanguageTool-only scope every shipped provider is remote, so the switch governs the whole mode; the service-level `isLocal()` seam stays in the contract for a future offline engine but no provider uses it now. A self-hosted LanguageTool URL also needs the switch, so one rule covers every endpoint and no host-name heuristic is needed.
- `proofreading/hotkey` is added by child 07, when a hotkey exists to register, so no dead setting is written in child 01 or shown in child 04.
- The reply object replaces the design note's `check(request, callback)` signature to match `TranslationReply` (`src/core/translationservice.h`): ownership, `finished()` outside `check()` and `abort()` → `Cancelled` stay identical to the translation side. `TranslationError` is reused instead of a second error enum, so `ui/translationerrortext` and the existing wording apply.

## Done when

- The separate tab and settings do not affect translation.
- LanguageTool finds spelling, grammar, and punctuation issues for the main languages, through the public API and a self-hosted URL, and the key is stored like the other secrets.
- Online checking is opt-in: `proofreading/allowOnlineServices` defaults to off, and the UI explains why the checked text goes to the provider.
- Single and bulk replacement plus replacing the text with the corrected version work and undo is preserved.
- Checking the selection in another application shows the candidates and replaces the text only after an explicit confirmation.
- Rate-limit, key, and network errors are clear and the application does not hang.
- Tests and README/architecture are updated.

## Depends on

None technically; follows stage 4 in the old order and is independent. Stage 6 implements the optional LLM checker against this stage's interface; its OpenAI-compatible translation provider already exists.

## Risks and what is needed from the user

Proofreading APIs have keys and limits; text may be private; automatic replacement is risky. The user must provide a key where a premium LanguageTool is used and approve the privacy and replacement rules. The default must keep automatic replacement off.

- The provider set was decided by the user on 2026-11-09: **LanguageTool only**. Yandex Speller and Hunspell/Nuspell are deferred, and `06-spelling-providers` is `dropped` with its design kept. Do not add a provider or a native dependency to this stage; a later stage can revive the dropped node.
- The online gate was approved on 2026-11-09 and stays off by default, so the first run of the checking mode shows a "needs online text checking" state until the switch is enabled. Do not reuse `translation/allowFreeServices`.
- Automatic replacement stays off, `proofreading/checkBeforeTranslation` stays off, and every replacement in another application needs explicit confirmation; single and bulk replacement in the main window are always one undo step.
- The public LanguageTool API has limits (about 20 requests/min, 75 000 characters/min, 20 000 characters per request) and no availability guarantee; for regular use the self-hosted URL is the answer. Rule false positives and language-dependent punctuation are the user-visible risk: the UI always shows the message and the suggested replacements instead of applying them silently.
- The external-selection scenario (`07`) is part of the stage; its fifth hotkey touches the frozen `HotkeyManager` API across Windows, X11 and the Wayland portal and comes last.
