---
kind: stage
status: dropped
---

# Spelling providers

## Dropped

Dropped on 2026-11-09 by the user's decision: the first version of the checking mode ships
LanguageTool only, and Yandex Speller and Hunspell/Nuspell are deferred. The directory is kept as
the design record; nothing below is part of stage 5 or gates it, and no child was started. A later
stage can revive the node (renumbering is the lead's call) if offline or no-key spelling is wanted.

## Goal

Add the two spelling-only providers of `doc/proofreading.md` §3 so the checking mode is not limited
to LanguageTool: Yandex Speller (remote, no key, ru/uk/en) and Hunspell/Nuspell (a local C/C++ engine
with dictionaries, fully offline and the privacy-preserving option). Each provider is a
`ProofreadingService` from `01-proofreading-core`, is selectable in the **Text checking** tab and
reuses the manager, the panel and the replacement flow from `05-main-window-check-and-replace`; no
translation code is involved.

## What to do, what is touched

Two independent children, each with its own result, test target and settings row:

- **Yandex Speller** — a `QueuedWebService` provider with its own `yandex-speller` branch in
  `Languages` (ru, uk, en and the codes the endpoint accepts), a parser with fixtures, the
  `proofreading/allowOnlineServices` gate (the provider is remote, so it is refused while the switch
  is off) and `tst_yandexspellerparser`/`tst_yandexspellerservice`. The endpoint returns spelling
  suggestions per word (`code`, `word`, `s`), so the parser builds `ProofreadingIssue` values with
  `category = spelling` and the suggestions as `replacements`; the per-host pause and a documented
  daily limit are named constants.
- **Hunspell/Nuspell** — a local provider (`isLocal() == true`, no network, works with the switch
  off) behind an optional CMake dependency detected like libsecret (`pkg_check_modules` for
  hunspell/nuspell; a build without it still configures and reports the provider as unavailable).
  Dictionary discovery, per-language loading, word extraction that keeps punctuation and case,
  suggestions as replacements and the locale-to-dictionary mapping are in the child; on Linux the
  system dictionary directories are used, on Windows the bundled or user-provided dictionaries, and
  the licence and source of every shipped dictionary are recorded.

Each child adds its provider id to `proofreadingProviderIds()` and its row/language list to the
settings page, and its own test target. The stage does not change the manager, the panel or the
translation settings.

## Done when

- Yandex Speller checks ru/uk/en text, returns spelling issues with suggestions, is refused while
  `proofreading/allowOnlineServices` is off, and shows clear HTTP/limit/empty-response errors with
  tests against a local server.
- Hunspell/Nuspell checks text fully offline with a loaded dictionary, returns spelling issues with
  suggestions, works with the online switch off, and reports a missing engine or dictionary as a
  clear provider-unavailable state instead of failing silently.
- Both providers are selectable in the **Text checking** tab, their language list follows the
  provider, and switching between LanguageTool and a spelling provider needs no restart.
- The build without the native library still configures, builds and passes the suite; the packaged
  builds and the README/doc facts are reported to the lead.
- Debug and Release build on Linux without warnings and the full suite passes; the count grows by the
  new platform-neutral test targets (each +1 Linux and +1 Windows); Windows is recorded as not
  performed.

## Depends on

`01-proofreading-core` for the contract and the settings group, `03-languagetool-service` for the
accepted service pattern and the `AppController` registration point. The UI row and the language
rebuild extend `04-text-checking-tab`, and the user-visible flow is
`05-main-window-check-and-replace`.

## Risks and what is needed from the user

- Hunspell/Nuspell is a new native dependency and a dictionary-distribution question: which languages
  are shipped, from where, under which licence, and how large the artifacts become. The child is
  planned around a small default set; the exact set and the bundling decision are the user's and are
  reported with the split.
- Yandex Speller sends the text to a third party and has a documented daily limit; it stays behind
  the online gate and never becomes a default.
- A build without the native library must not lose the other providers; keep the optional dependency
  optional and the provider state explicit.
