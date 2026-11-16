---
kind: task
status: done
detailed: true
depends: [12-llm-proofreading]
---

# Call the checking provider AI, like its settings tab

## Task

User report (2026-11-16): the settings tab is **AI**, but the checking provider that reads it is
labelled **LLM**, so the same thing has two names in the interface. Make it one name: the provider is
displayed as **AI** everywhere the user sees it.

Keep the internal id `llm` — it is the stored value of `proofreading/provider` and appears in logs and
tests; renaming it would need a settings migration for no user benefit. Only the display name and the
user-facing sentences change. "LLM" stays where it means the technical thing (the model kind, the
`[llm]` configuration block, the code names).

## Steps

1. `src/services/llmproofreadingservice.cpp:301` — `displayName()` returns `QStringLiteral("AI")`
   instead of `QStringLiteral("LLM")`. Nothing else in that class changes; `id()` stays `llm`.
2. `src/services/llmproofreadingservice.cpp:314` — the configuration error currently says "the LLM
   provider is configured on the AI tab"; with the new name it must not read "the AI provider is
   configured on the AI tab", which is circular. Use the parallel wording the settings hint already
   has, for example `the AI provider is configured on the settings AI tab` or
   `configure the AI provider on the AI tab` — pick one, keep it un-translated like now, and mention
   the choice in the outcome.
3. Tests: update the four places that assert the old name — `tests/tst_llmproofreadingservice.cpp:243`
   and the three provider tables in `tests/tst_settingsdialog.cpp` (`:1239`, `:1321`, `:1362`). The
   added rows must keep the id `llm` and use `AI` as the display name; no other expectation changes.
4. Check that nothing else shows the old name to the user:
   `grep -rn '"LLM"' src/ui/ src/services/ src/app/` must find only the technical uses
   (`Local LLM` for the translation service at `src/services/openaicompatibleservice.cpp:330` stays).
5. No `.ts` change is expected: the display name and the configuration error are not `tr()` strings.
   Confirm the 14 catalogues stay at 383 messages.

## Done when

- The provider combo on the **Text checking** page shows `AI`, and the settings tab and this name
  match; the stored `proofreading/provider=llm` value and the `[llm]` block are unchanged, so an
  existing configuration keeps working.
- The configuration error names the provider `AI` and reads sensibly next to the **AI** tab title.
- `grep` shows no other user-visible `LLM` provider name; `Local LLM` (the translation service) is
  untouched.
- All 53 Linux tests pass in Debug and Release with no warnings on the host and in
  `qtranslate-trixie`; `.ts` stays 383; Windows by diff.
- No new `tr()` string, no settings migration, no documentation edit outside `doc/roadmap/`.

## Risks

- Someone may expect the id to change too; it must not, or the stored selection breaks.
- The configuration error is un-translated today; keeping it English is consistent with the other
  technical details of this service, and translating it would add `.ts` messages.

## Outcome

Done as one coordinator run on 2026-11-16, the last code change before 0.14.0.

Changed:

- `src/services/llmproofreadingservice.cpp:301` — `displayName()` returns `AI`. The provider combo on
  the **Text checking** page now matches the tab title; its only consumer is the provider list built
  in `src/app/appcontroller.cpp:800`, so nothing else shows this name.
- `src/services/llmproofreadingservice.cpp:314` — `configurationError()` is now
  `the AI provider is configured on the settings AI tab`. Chosen over
  `configure the AI provider on the AI tab` because it keeps the declarative form of the neighbouring
  messages (`OpenAiCompatibleService::configurationError()` → "The provider needs a model."), and the
  word `settings` removes the circular "the AI provider is configured on the AI tab". The sentence
  reaches the user through `src/core/proofreadingmanager.cpp:147` and
  `ProofreadingErrorText::forResult()`, which renders it as
  "The text checking service rejected the request (the AI provider is configured on the settings AI
  tab)", which reads coherently. It stays a `QStringLiteral`, like before.
- `tests/tst_llmproofreadingservice.cpp:243` and the three provider tables in
  `tests/tst_settingsdialog.cpp:1239`, `:1321`, `:1362` — expectations updated to `AI`, id `llm` kept.
- Review found that `tests/tst_llmproofreadingservice.cpp:256` asserted only
  `contains("AI tab")`, which the old wording also satisfied, so the changed sentence was not fixed by
  any test. The assertion is now an exact `QCOMPARE` with the new sentence; one line, no source
  change.

Kept on purpose: the id `llm` (the stored `proofreading/provider` value, `src/services/settings.cpp`
`[llm]` block, log and test names), `Local LLM` for the translation service
(`src/services/openaicompatibleservice.cpp:330`), and the class/file names.

Verification: host Debug 53/53 and Release 53/53 with 0 warnings
(`build/coord-ai-debug`, `build/coord-ai-release`), `qtranslate-trixie` Release 53/53 with 0 warnings
(same command as releases), the 14 `i18n/*.ts` untouched at 383 messages each, `rg -n '"LLM"' src/`
empty, Windows reviewed by diff.

Left open, not fixed here (both outside this node):

- `README.md:294` and `README.ru.md:296` still say "the LLM provider is chosen there too" /
  "выбирается провайдер LLM", while the combo now says `AI`. Documentation is a separate pass.
- The hint shown under the provider combo is still
  `The OpenAI-compatible provider is configured on the AI tab.` (`src/ui/settingsdialog.cpp:564`), a
  third name for the same thing. It is a shared `tr()` string with the **Services** page, so changing
  it would touch all 14 catalogues and `tst_settingsdialog` — not worth it for a release-blocking
  rename. The comments at `src/ui/settingsdialog.cpp:561` and `:955` keep calling the provider "LLM"
  in prose; that is source, not interface.
