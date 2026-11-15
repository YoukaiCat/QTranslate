---
kind: task
status: todo
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

## Done when

- With the provider combo on `llm`, the `LanguageTool URL:` row, its hint and its warning are hidden,
  and a word-wrapped label with objectName `proofreadingLlmHint` shows the AI-tab sentence; with
  `languagetool` the row is visible again and the warning follows the typed value as before.
- The visibility follows the provider immediately (the existing `currentIndexChanged` connections),
  works while the mode is enabled, and the endpoint text is preserved across a switch in both
  directions and still returned by `SettingsDialog::settings()`.
- `tst_settingsdialog` covers the two states — the endpoint row hidden for `llm` with the hint visible
  and `settings().proofreading.provider == "llm"`, the row visible again for `languagetool` with the
  URL intact — using a provider list that holds `languagetool` and `llm` with `languages` and
  `supportsAutoLanguage` from the real services.
- `tst_proofreadingmanager`, `tst_proofreadingwindow`, `tst_proofreadingpopup` and
  `tst_proofreading_selection_x11` pass unchanged: the popup and the second hotkey are
  provider-agnostic, and their coverage plus the service/manager tests is the automated proof that
  the LLM provider reaches them.
- `.ts` files stay at 383 messages with no unfinished or vanished entry; exactly one
  `<source>The OpenAI-compatible provider is configured on the AI tab.</source>` per file, translated
  consistently with the AI tab title.
- Debug and Release build without warnings on the host and in `qtranslate-trixie`; 53/53 Linux tests
  in both; Windows 47 (derived) reviewed by diff and recorded as not performed.

## Risks

- **A hidden field that still saves.** `setRowVisible(false)` hides the widgets but `settings()` reads
  `m_proofreadingEndpoint->text()` unconditionally — that is deliberate (the URL survives a provider
  switch). Do not clear the field or the stored value on a provider change.
- **The warning can reappear on the wrong provider.** `updateProofreadingControls()` computes the
  `proofreadingEndpointWarning` visibility from the text alone; it must also require
  `languagetool`, or an invalid URL typed earlier pops back while `llm` is selected.
- **`QFormLayout::setRowVisible()` needs Qt 6.4+** and the project minimum is 6.5, so it is allowed;
  the row must be addressed through a stored layout pointer (the layout is a local in `buildUi()`
  today) or through the two widgets it contains.
- **Do not add a second hint string.** Reusing the AI-tab sentence keeps the `.ts` count and stops
  translators from maintaining two near-identical messages; if the wording has to change, change it
  in `01-ai-tab` and retranslate there.
- **The live run is still the user's.** No key, no server and no model are part of this task; the
  mock server from `03-llm-check-service` stays the automated gate, and a real check with a key or a
  local model is recorded as not performed.
