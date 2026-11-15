---
kind: task
status: done
detailed: true
depends: [01-ai-tab, 03-llm-check-service]
---

# Selecting the LLM provider in the checking settings

## Task

Finish the provider selection on the **Text checking** page now that the `llm` service is registered:
choosing it must hide the LanguageTool URL field and its warnings — the server, model and key are the
single `[llm]` configuration on the AI tab — and show a hint that says where they live. Nothing else
changes: the language list, the category checkboxes and the two hotkeys already work for any
registered provider, and the window and popup take the active service from `ProofreadingManager`, so
the same issue list, preview, Replace/Ignore/Replace all and both hotkeys serve both providers.

The page keeps the LanguageTool URL value while another provider is selected, so switching back does
not lose a self-hosted endpoint: `settings()` still returns `m_proofreadingEndpoint->text()`, and the
stored `proofreading/endpoint` is not cleared when `provider=llm` (the LLM service never reads it).

Only one message is added, and it is the one `01-ai-tab` already introduced
(`The OpenAI-compatible provider is configured on the AI tab.`): the Text checking page reuses it, so
the `.ts` count stays 383 and no new translation work is needed here.

## Steps

Three files change: `src/ui/settingsdialog.h`, `src/ui/settingsdialog.cpp` and
`tests/tst_settingsdialog.cpp`, plus one slot extended in `tests/tst_settings.cpp`. No target is added,
so the counts stay 53 Linux / 47 Windows. `doc/` is the lead's pass (see Risks).

1. **`src/ui/settingsdialog.h` — two members and one forward declaration.**
   - Add `class QFormLayout;` to the forward-declaration block (`settingsdialog.h:10-20`); the header
     needs no include, the `.cpp` already includes `<QFormLayout>` (`settingsdialog.cpp:15`).
   - Next to the proofreading page members (`settingsdialog.h:144-155`) add
     `QFormLayout *m_proofreadingProviderForm = nullptr;` after `m_proofreadingProvider`, and
     `QLabel *m_proofreadingLlmHint = nullptr;` after `m_proofreadingEndpointWarning`. The layout is a
     local in `buildUi()` today (`settingsdialog.cpp:538`) and `QFormLayout::setRowVisible()` needs the
     layout object; the label is a page widget that `updateProofreadingControls()` must toggle, so it
     is a member like `m_proofreadingEndpointWarning`.
   - Comment the two members in the style of the file: the form is kept for the LanguageTool-only row,
     the hint points at the AI tab and is shown only for `llm`.

2. **`buildUi()` — store the form, build the hint.**
   - `:538`: `m_proofreadingProviderForm = new QFormLayout(m_proofreadingProviderGroup);` and use the
     member for the five `addRow()` calls at `:565-569`.
   - After the warning row (`:557-564`, added at `:569`) create the hint and add it as the last row:

     ```cpp
     // The LLM provider is configured on the AI tab; the sentence is the one the
     // Services page already shows, so it is translated once (01-ai-tab).
     m_proofreadingLlmHint = new QLabel(
         tr("The OpenAI-compatible provider is configured on the AI tab."),
         m_proofreadingProviderGroup);
     m_proofreadingLlmHint->setObjectName(QStringLiteral("proofreadingLlmHint"));
     m_proofreadingLlmHint->setWordWrap(true);
     m_proofreadingLlmHint->setVisible(false);
     ```

     and `m_proofreadingProviderForm->addRow(m_proofreadingLlmHint);` after
     `addRow(m_proofreadingEndpointWarning)`. A widget-only row is what the two existing hint rows are,
     so the layout needs no column change.
   - **The `tr()` argument must be byte-identical to `settingsdialog.cpp:320`.** One character of
     difference makes `lupdate` produce a second message and the `.ts` count 384; the same context
     (`SettingsDialog`) and the same source are what keep it at 383. Do not reflow the sentence across
     string literals differently either — keep it on one line exactly as at `:320`.
   - Object names, tab order and the AI page are untouched; no widget is added outside the Provider
     group.

3. **`updateProofreadingControls()` (`settingsdialog.cpp:937-950`) — the visibility rules.**
   - Read the provider id from the combo, which is already filled from the registered services
     (`appcontroller.cpp:788-795`, so `LLM` is already in the list) and already connected:
     `const bool llmSelected = m_proofreadingProvider->currentData().toString() ==
     QStringLiteral("llm");`
   - Hide the whole `LanguageTool URL:` row, not just the field, and swap the two hints:

     ```cpp
     m_proofreadingProviderForm->setRowVisible(m_proofreadingEndpoint, !llmSelected);
     m_proofreadingEndpointHint->setVisible(!llmSelected);
     m_proofreadingLlmHint->setVisible(llmSelected);
     ```

     `setRowVisible(QWidget *, bool)` hides the auto-created label together with the field; it is Qt
     6.4+ and the project minimum is 6.5, so no version guard is needed. Keep the existing
     `setEnabled` calls as they are — a disabled, hidden field is harmless and the mode switch still
     owns the enabled state.
   - Gate the warning on the provider as well:
     `m_proofreadingEndpointWarning->setVisible(!llmSelected && !endpoint.isEmpty() &&
     !isValidProofreadingEndpoint(endpoint));`. Without it, an invalid URL typed on LanguageTool pops
     back as soon as the text changes while `llm` is selected.
   - Only `llm` hides the row. Any other id — including the `other` provider of `proofreadingTab()` —
     keeps today's behaviour, and a hypothetical third provider gets no LLM hint.
   - **No new signal or slot is needed.** Both directions are already wired: `currentIndexChanged` →
     `updateProofreadingControls()` (`:601-602`), `textChanged` → the same slot (`:603-604`), and
     `loadValues()` ends with an explicit call (`:779`), so the state is right when the dialog opens
     with a stored `llm`. Keep the `updateProofreadingLanguage()` connection (`:599-600`) first; it
     rebuilds the language list before the visibility is recomputed.

4. **The load, save and validate paths stay as they are — verify, do not "fix".**
   - `loadValues()` (`:758-779`) sets the provider before the endpoint text (`:761-770`), so the row is
     hidden first and the URL is then written into an invisible field; it is still in the widget, so
     the later `updateProofreadingControls()` at `:779` shows the correct state. Do not move the
     `setText()` or clear the field.
   - `settings()` (`:831-835`) keeps returning
     `normalizedProofreadingEndpoint(m_proofreadingEndpoint->text())` regardless of the selection: that
     is what preserves a self-hosted URL across a switch and what saves it again while `llm` is
     selected. `SettingsStore` already keeps `proofreading/endpoint` for any provider
     (`src/core/settings.cpp:240-249` normalises it without looking at `proofreading/provider`), so
     nothing changes in `src/core/`.
   - `validate()` (`:1050-1059`) stays unconditional. A URL typed on LanguageTool and made invalid
     cannot be accepted by hiding the field: the dialog refuses until the value is fixed or cleared.
     That is deliberate — skipping the check would let `settings()` store an empty endpoint, because
     `normalizedProofreadingEndpoint()` maps an invalid value to `{}` — and step 6 pins it with a test
     so it is not "simplified" later.

5. **`tests/tst_settingsdialog.cpp` — three new slots (36 → 39), placed after `proofreadingTab()`
   (`:1135-1225`).** A shared provider list at the top of each slot, built from the real catalogues so
   the languages match the services without linking them (the target links `qtranslate_core` and
   `src/ui` only, `tests/CMakeLists.txt:99-106`):

   ```cpp
   const QList<SettingsDialog::ProofreadingProvider> providers = {
       {QStringLiteral("languagetool"), QStringLiteral("LanguageTool"),
        Languages::supported(QStringLiteral("languagetool"), Languages::Direction::Source), true},
       {QStringLiteral("llm"), QStringLiteral("LLM"),
        Languages::supported(QStringLiteral("llm"), Languages::Direction::Source), true},
   };
   ```

   Widgets are looked up with `findChild` by object name as the neighbouring slots do, and visibility
   is checked with `isHidden()`, the pattern already used at `:1354` and `:1362`; the dialog is not
   shown, since a child hidden through `setRowVisible()`/`setVisible(false)` is `isHidden()` even
   before the first `show()`.

   a. `proofreadingLlmProviderHidesLanguageToolRow()` — starting from `provider=languagetool`,
      `enabled=true`, `language=en`, `endpoint=http://127.0.0.1:8081/v2/check`:
      - `findChild` for `proofreadingEnabled`, `proofreadingProvider`, `proofreadingLanguage`,
        `proofreadingEndpoint`, `proofreadingEndpointHint`, `proofreadingEndpointWarning`,
        `proofreadingLlmHint` — all non-null; `llmHint->wordWrap()`;
        `llmHint->parentWidget() == providerGroup`.
      - On open: `!endpoint->isHidden()`, `!endpointHint->isHidden()`,
        `llmHint->isHidden()`, `settings().proofreading.provider == "languagetool"`.
      - The row label disappears too: take the group's layout with
        `qobject_cast<QFormLayout *>(providerGroup->layout())` and assert
        `form->labelForField(endpoint)->isHidden()` in the `llm` state and not-hidden after the switch
        (the file already reads `QFormLayout` items this way at `:1290-1294`).
      - `provider->setCurrentIndex(provider->findData(QStringLiteral("llm")))`, then: current data
        `llm`; `endpoint->isHidden()`; `endpointHint->isHidden()`; `endpointWarning->isHidden()`;
        `!llmHint->isHidden()`; `llmHint->text() == QStringLiteral("The OpenAI-compatible provider is
        configured on the AI tab.")`; `endpoint->text()` unchanged; `settings().proofreading.provider
        == "llm"`; `settings().proofreading.endpoint == the URL`; `validate(&error)` true with an empty
        `error`; the language list still offers `en` and Auto-detect.
      - Back to index 0: `!endpoint->isHidden()`, `!endpointHint->isHidden()`,
        `llmHint->isHidden()`, the URL still in the field, `settings().proofreading.provider ==
        "languagetool"`. This is the "follows the combo without reopening" criterion — the dialog is
        never shown and never re-created.

   b. `proofreadingLlmProviderFromSettings()` — `provider=llm`, `enabled=true`, `language=de`,
      `endpoint=http://127.0.0.1:11435/v2/check` in `AppSettings`: on open the combo is on `llm`, the
      row and both LanguageTool hints are hidden, `proofreadingLlmHint` is visible, the field keeps the
      URL, `settings().proofreading.provider == "llm"` and `settings().proofreading.endpoint` is the
      URL. This covers the `loadValues()` ordering of step 4 (provider first, text second).

   c. `proofreadingLlmProviderKeepsTheLanguageToolWarning()` — `provider=languagetool`, enabled:
      `endpoint->setText(QStringLiteral("not a URL"))` → warning visible, `validate()` false with
      `Enter a valid http(s) LanguageTool URL, or leave the field empty for the public endpoint.`;
      switch to `llm` → warning hidden, row hidden, `validate()` still false with the same message
      (the pinned decision of step 4); switch back → warning visible again; `setText()` the valid URL →
      warning hidden, switch to `llm`, `validate()` true and
      `settings().proofreading.endpoint == the URL`.

6. **`tests/tst_settings.cpp` — extend `proofreadingLlmProviderRoundTrip()` (`:513-531`), no new
   slot.** Set `settings.proofreading.endpoint = QStringLiteral("https://lt.example.invalid/v2/check")`
   next to `provider=llm`, and after the reload assert
   `loaded.current().proofreading.endpoint` is that URL and
   `raw.value("proofreading/endpoint").toString()` holds it: the stored LanguageTool endpoint is not
   dropped by the provider id. The rest of the slot (the provider value and the INI key) stays.

7. **Verification.**
   - Host, Debug and Release: `cmake --build build/linux-debug` and
     `cmake --build build/linux-release`, `0` warnings under `-Wall -Wextra`, then
     `xvfb-run -a ctest --test-dir build/linux-debug --output-on-failure` and the same for
     `build/linux-release`: **53/53** each, `ctest -N` 53 targets.
   - Targeted while iterating:
     `ctest --test-dir build/linux-debug -R "tst_settingsdialog|tst_settings|tst_proofreadingwindow|tst_proofreadingpopup|tst_proofreadingmanager" --output-on-failure`.
   - Container, bounded as in `02`/`03`:
     ```sh
     docker run --rm -v "$PWD":/src -w /src qtranslate-trixie:latest bash -c '
       cmake -S . -B build/trixie-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr &&
       cmake --build build/trixie-release &&
       timeout 420 xvfb-run -a ctest --test-dir build/trixie-release --timeout 45 --output-on-failure'
     ```
     Release **53/53**, `0` warnings; `xvfb-run` flakes there (`01`, `02`), so bound it and fall back to
     the host run.
   - `.ts`: `for f in i18n/*.ts; do printf '%s %s\n' "$f" "$(rg -c '<message' "$f")"; done` → **383**
     in each of the 14 files; `rg -c 'configured on the AI tab' i18n/*.ts` → **1** per file;
     `lupdate`/`lrelease` are not run.
   - `rg -n 'configured on the AI tab' src/ui/settingsdialog.cpp` → exactly two `tr(` lines (`:320`
     and the new one), and `rg -n 'proofreadingLlmHint' src tests` shows the label, the visibility line
     and the tests.
   - Windows: not performed (no MSVC). No target is added, so the count stays **47**; the change is
     reviewed by diff and touches no `WIN32` code.
   - Hygiene: `git status` shows only the four files above plus the roadmap; `git diff --check` clean.

## Done when

- With the provider combo on `llm` in an open dialog, the `LanguageTool URL:` row (its label and its
  field), its hint and its warning are hidden, and a word-wrapped label with objectName
  `proofreadingLlmHint` shows the AI-tab sentence; with `languagetool` the row, the hint and the
  warning behaviour are back. The switch happens on `currentIndexChanged`, without reopening the dialog
  and with the mode enabled, and a stored `llm` opens the page in that state.
- The `LanguageTool URL` value survives a switch in both directions and is still returned by
  `SettingsDialog::settings()` while `llm` is selected; an invalid URL typed on LanguageTool stays
  hidden under `llm` and still blocks `validate()` instead of being silently dropped, and the warning
  cannot reappear while `llm` is selected.
- `SettingsStore` keeps `proofreading/provider=llm` together with a non-empty `proofreading/endpoint`
  across a save and reload; `tst_settings` asserts both values and the INI key.
- `tst_settingsdialog` covers the three states (switching from LanguageTool, opening on `llm` from
  settings, and the invalid URL under `llm`) with a provider list built from
  `Languages::supported("languagetool"|"llm", Source)`: it grows from 36 to 39 slots, and every case is
  a visibility or value assertion, not a widget-emitting one.
- `tst_proofreadingmanager`, `tst_proofreadingwindow`, `tst_proofreadingpopup` and
  `tst_proofreading_selection_x11` pass unchanged: the popup and the second hotkey are
  provider-agnostic, and their coverage plus the service/manager tests is the automated proof that the
  LLM provider reaches them.
- `.ts` files stay at 383 messages with no unfinished or vanished entry; exactly one
  `<source>The OpenAI-compatible provider is configured on the AI tab.</source>` per file, translated
  consistently with the AI tab title, and `lupdate`/`lrelease` are not run.
- Debug and Release build without warnings on the host and in `qtranslate-trixie`; 53/53 Linux tests
  in both; Windows 47 (derived) reviewed by diff and recorded as not performed.

## Risks

- **A hidden field that still saves.** `setRowVisible(false)` hides the widgets but `settings()` reads
  `m_proofreadingEndpoint->text()` unconditionally — that is deliberate (the URL survives a provider
  switch). Do not clear the field or the stored value on a provider change, and do not move the
  `setText()` in `loadValues()` behind a visibility check.
- **The warning can reappear on the wrong provider.** `updateProofreadingControls()` computes the
  `proofreadingEndpointWarning` visibility from the text alone today; it must also require that the row
  is shown (the provider is not `llm`), or an invalid URL typed earlier pops back while `llm` is
  selected.
- **The sentence must match the AI tab byte for byte.** A retyped or reflowed
  `tr("The OpenAI-compatible provider is configured on the AI tab.")` is a second `.ts` message (384)
  and a new translation for 14 files — and a new `tr()` string is the lead's decision, not this task's.
- **`QFormLayout::setRowVisible()` needs Qt 6.4+** and the project minimum is 6.5, so it is allowed;
  the row must be addressed through a stored layout pointer (the layout is a local in `buildUi()`
  today), or through the two widgets it contains — the member in step 1 is the cheap way, and it also
  lets the tests read the row label.
- **Not hiding the row is worse than hiding it late.** Hiding the field but leaving the `LanguageTool
  URL:` label visible would show an empty caption row; hiding only the field would leave its hint text
  talking about a server the provider does not use.
- **`validate()` on an invisible field.** Keeping the check unconditional means a user who typed an
  invalid URL and then switched to `llm` sees an error about a field they cannot see. The alternative —
  skipping the check — writes an empty endpoint (`normalizedProofreadingEndpoint()` maps invalid to
  `{}`), which is silent data loss. The first is recorded and pinned by a test.
- **Documentation is the lead's pass**, as the stage records: `doc/architecture.md` (the settings-dialog
  and test-table lines, §… about the checking hint), `doc/handoff.md` and the checking-provider tables
  are updated by the lead after this task, not in this diff.
- **The live run is still the user's.** No key, no server and no model are part of this task; the mock
  server from `03-llm-check-service` stays the automated gate, and a real check with a key or a local
  model is recorded as not performed.

## Outcome

Done on 2026-11-16 (base `c5b10f0`). Selecting `llm` on the **Text checking** page now hides the whole
`LanguageTool URL:` row — the auto-created label and the field — together with its hint and its
warning, and shows a word-wrapped `proofreadingLlmHint` label that sends the user to the AI tab;
switching back to `LanguageTool` restores the row, the hint and the warning without reopening the
dialog, and a stored `provider=llm` opens the page in that state. The stored URL and
`proofreading/endpoint` survive the switch in both directions.

Files: `src/ui/settingsdialog.h` (forward declaration `class QFormLayout;` and the two members
`m_proofreadingProviderForm`, `m_proofreadingLlmHint`), `src/ui/settingsdialog.cpp` (the provider form
is the member, the hint is created after the warning row and added as the last row, and
`updateProofreadingControls()` computes `llmSelected` from `currentData() == "llm"` and toggles
`setRowVisible(m_proofreadingEndpoint, !llmSelected)`, the endpoint hint, the LLM hint and
`!llmSelected && …` on the warning), `tests/tst_settingsdialog.cpp` (three slots) and
`tests/tst_settings.cpp` (one extended slot). `loadValues()`, `settings()` and `validate()` are
untouched, as step 4 prescribed.

Decisions where the plan was silent:

1. **`QFormLayout::labelForField()` returns `QWidget *`**, so the test keeps the row label in a
   `QWidget *` variable; the assertions are the ones the plan lists.
2. **The test provider catalogues use `{de, en, ru}`.** With `{de, en}` the dialog's own
   primary/additional-language rule rejects the fixture before the visibility assertions run (the
   dialog's secondary language is `ru`); the wider catalogue is what the real services advertise, so
   `validate()` stays exercised as it is.

Verification. Host Debug and Release: `0` warnings under `-Wall -Wextra`, **53/53** tests each
(`ctest -N` 53, no target added). `qtranslate-trixie` Release: **53/53** in the bounded command on the
first attempt, `0` warnings. `tst_settingsdialog` `Totals: 41 passed` (39 slots, three of them new:
`proofreadingLlmProviderHidesLanguageToolRow()`, `proofreadingLlmProviderFromSettings()` and
`proofreadingLlmProviderKeepsTheLanguageToolWarning()`), `tst_settings` `Totals: 85 passed`. The three
new slots assert the hidden field, the hidden row label, the swapped hints, the preserved URL in the
field and in `settings()`, the stored `llm` opening state, and the warning staying hidden under `llm`
while `validate()` still refuses the invalid URL — each fails if the corresponding line is removed.
`.ts`: **383** `<message>` and exactly one `configured on the AI tab` in each of the 14 files; the only
two `tr(` lines with that source are `settingsdialog.cpp:320` and `:568`, byte-identical; no
`lupdate`/`lrelease`. Windows: not built (no MSVC), **47** derived, no target added and no `WIN32`
code touched, reviewed by diff. `git diff --check` clean.

Review: no remarks, no defects. Recorded as a rough edge rather than fixed (plan decision, pinned by
`proofreadingLlmProviderKeepsTheLanguageToolWarning()`): `validate()` stays unconditional, so a user who
typed an invalid LanguageTool URL and then switched to `llm` gets an error about a field they cannot
see. The alternative — skipping the check under `llm` — would let
`normalizedProofreadingEndpoint()` turn the invalid value into `{}` and erase the stored endpoint.

Left open: no live check with a real key or a local model (the mock server from `03` remains the
automated gate); `doc/architecture.md`, `doc/handoff.md` and the stage's provider tables are the lead's
documentation pass, not part of this diff.
