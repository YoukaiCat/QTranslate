# Spelling, grammar, and punctuation checking

Date: 2026-11-14. Status: **implemented in roadmap stages 5 and 9 (old stage 15)**. The first version
ships **LanguageTool only** (user decision of 2026-11-09); Yandex Speller and Hunspell/Nuspell are
deferred and kept here as the longer-term design. Stage 9 (2026-11-14) made checking a standalone
feature in its own window, removed the pre-translation check, the automatic-replacement setting and
the online gate, and added the second external-selection hotkey.

## 1. Goal

Add a separate text-checking mode to qtranslate:

- find spelling, grammar, and punctuation errors;
- show them in a list and with highlighting in a separate checking window;
- accept replacements one by one or all at once;
- replace the source text with the corrected text;
- check a selection in another application by hotkey, with a popup result or an immediate replacement.

This is not part of translation: checking is a separate window, a separate action, a separate
settings tab, and a separate set of settings. The translation window contains no checking UI.

## 2. Scenarios

1. **Check text in the checking window.** The user opens **Check text…** from the tray, types or
   pastes text, and clicks "Check" (or presses Ctrl+Return/Ctrl+Enter); errors are highlighted in the
   window's editor and a list appears on the right/below. The window is non-modal and independent of
   the translation window.
2. **Replace with the corrected text.** The "Replace all" / "Apply corrections" button replaces the
   text in the checking window's editor while preserving formatting and allowing undo.
3. **Check the selection in another application.** Two independent hotkeys capture the selection
   like Ctrl+Shift+Q: one checks it and shows the candidate result in a popup with Replace
   selection/Copy/Cancel; the other checks it and immediately replaces the selection with the
   corrected text, without a confirmation window. For an external application, protection against
   active-window changes and a warning that undo depends on the application are needed; if the target
   window changed or cannot be pasted into, the corrected text goes to the clipboard.
4. **Spelling only.** A lightweight offline mode without grammar and punctuation.

## 3. Providers

| Provider | What it checks | Languages | Access | Notes |
|---|---|---|---|---|
| **LanguageTool** | spelling, grammar, punctuation, style | 25+ languages, including ru, uk, en, de, fr, es, pl, pt, zh, ja, ar | HTTP API or a self-hosted Java server | The best multilingual option; LGPL-2.1; a public API with limits |
| **Hunspell / Nuspell** | spelling only | many languages, dictionaries separately | C/C++ library | Easy to embed; no grammar/punctuation |
| **Yandex Speller** | spelling, typos, case | ru, uk, en, and others | HTTP without a key | Free, but does not check commas/grammar |
| **LLM** | spelling, punctuation, style, tone | many languages | API or a local model | Returns corrected text rather than a list of errors; a diff and confirmation are needed |

LanguageTool HTTP:

```http
POST https://api.languagetool.org/v2/check
Content-Type: application/x-www-form-urlencoded

text=...&language=ru-RU
```

The response contains `matches[]`: `offset`, `length`, `message`, `replacements`, `rule`, `context`.
This is convenient for highlighting and replacements. The public API has limits (about 20
requests/min, 75,000 characters/min, 20,000 characters per request); for regular use a dedicated
server is better.

For Russian/Ukrainian, LanguageTool covers grammar and punctuation better than Hunspell;
Hunspell/Nuspell — spelling only. Yandex Speller is a cheap option for ru/uk/en spelling.

## 4. Architecture

A new module in `qtranslate_core`:

```cpp
struct ProofreadingIssue
{
    int offset = 0;          // in QChar/UTF-16, as in Qt
    int length = 0;
    QString message;
    QStringList replacements;
    QString ruleId;
    QString category;        // spelling | grammar | punctuation | style
    int severity = 0;
};

struct ProofreadingResult
{
    QVector<ProofreadingIssue> issues;
    QString correctedText;   // filled when the provider returned ready text
};

class ProofreadingService
{
public:
    virtual ~ProofreadingService() = default;
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual bool isConfigured() const = 0;
    virtual ProofreadingReply *check(const ProofreadingRequest &request) = 0;
};
```

The implementation returns a caller-owned `ProofreadingReply` with the same semantics as
`TranslationReply` (`finished()` outside `check()`, `abort()` → `Cancelled`), and `ProofreadingManager`
owns the registry and the `Window`/`InPlace` channels. There is no online gate: `proofreading/enabled`
is the only consent, and every configured provider may check once the feature is on.

Details:

- `offset`/`length` are in UTF-16 (`QString`), not bytes; do not split surrogate pairs.
- Split long text by sentences/paragraphs, merge offsets taking separators into account;
- Cancel a stale request on a new click;
- Do not block the UI; network — through the existing `NetworkManager` with proxy and timeout;
- No credentials: the public LanguageTool API and a self-hosted URL work without a key; the premium
  `username`/`apiKey` mode is deferred;
- Do not write the checked text to the log.

For text replacement:

- In the checking window — through `QTextDocument`/the cursor so that undo works;
- In an external application — through the same mechanism as Ctrl+Shift+Q, with active-window
  checking. The popup hotkey asks for confirmation; the second hotkey replaces immediately. A changed
  foreground window or no target copies the corrected text to the clipboard;
- Before "replace all", show a preview/diff if there is more than one correction;
- After replacement, recalculate the offsets or request the check again.

## 5. Settings

A separate **"Text checking"** tab (do not mix it with "Translation"):

- `proofreading/enabled` — enable the mode; the only switch and the consent for remote checking;
- `proofreading/provider` — `languagetool` in the first version; `llm` arrives with stage 6;
- `proofreading/language` — checking language (`auto` or a specific one);
- `proofreading/endpoint` — URL of your own LanguageTool/LLM;
- `proofreading/categories` — spelling, grammar, punctuation, style;
- `proofreading/hotkey` — check the selection and show the popup result, empty by default;
- `proofreading/replaceHotkey` — check and replace the selection immediately, empty by default.

There is no check-before-translation option, no automatic-replacement option and no second online
gate: text checking is a standalone action, the checking window does the manual replacement, and the
second hotkey does the immediate replacement. The first version sends no credentials: the public
LanguageTool API and a self-hosted URL work without a key, and the premium `username`/`apiKey` mode
is deferred to a later task.

The tab must be separate, as requested; the settings must be independent of translation and must not
affect the selected translation service.

## 6. UI

- A separate, non-modal **ProofreadingWindow** opened from the tray item **Check text…**; it has its
  own source editor and Check button and shares no widgets or text with the translation window. The
  translation window has no checking UI.
- Highlighting issues in the checking window's editor; clicking a row selects the fragment.
- List: message, rule, replacement options, "Replace", "Ignore" buttons.
- "Replace all" / "Apply corrections" with a preview.
- "Copy corrected text".
- States: checking, no errors, provider unavailable, request limit.
- For an external selection: the popup hotkey shows a separate confirmation window; the second
  hotkey replaces the selection immediately, with clipboard fallback when pasting is not possible.

## 7. Tests

- The LanguageTool parser on fixtures: several errors, replacements, offset/length, UTF-16 and
  surrogate pairs, an empty response, 429/403/5xx (`tst_languagetoolparser`);
- The service against a local server and the offset merge when splitting long text
  (`tst_languagetoolservice`);
- The manager, registry and `Window`/`InPlace` channels (`tst_proofreadingmanager`);
- Hunspell/Nuspell: dictionary, case, punctuation around the word; *(deferred)*
- Yandex Speller: success, empty response, limit; *(deferred)*
- Settings: saving/loading, the separate tab, both hotkeys (`tst_settings`,
  `tst_settingsdialog`);
- UI (offscreen): the separate checking window's list, replacing one/all, undo, request
  cancellation (`tst_proofreadingwindow`), the translation window without checking UI
  (`tst_mainwindow`), and the external popup (`tst_proofreadingpopup`);
- X11: real grabber + popup + paste (`tst_proofreading_selection_x11`);
- Manual smoke: LanguageTool cloud/self-hosted, replacement in Notepad/a browser.
- Linux 46 tests / Windows 40 tests; the checking window and the tray item are not exercised
  interactively in the headless environment.

## 8. Risks

- Rule false positives; the checking window confirms Replace all, while the second hotkey replaces
  the selection immediately by the user's explicit choice;
- Punctuation rules depend on language and style;
- LanguageTool is Java; the cloud API is simpler, self-hosting is heavier;
- The public API has limits and does not guarantee availability;
- An LLM may rewrite the meaning; only a diff + confirmation;
- Replacement in an external application is not always undone by undo;
- The checked text goes to the provider once `proofreading/enabled` is on; local Hunspell/Nuspell are
  more private.

## 9. Priorities

1. **LanguageTool** — grammar and punctuation for the main languages.
2. **Hunspell/Nuspell** — lightweight offline spelling.
3. **Yandex Speller** — free spelling for ru/uk/en.
4. **LLM** — optional, for style/tone, not as the only checker.

## 10. Done criteria

The first version implements LanguageTool as a standalone feature; the Hunspell/Nuspell and Yandex
Speller items below are the long-term design and are not part of stages 5/9 (user decision of
2026-11-09). The stage criteria live in
[`roadmap/qtranslate/09-proofreading-standalone/plan.md`](roadmap/qtranslate/09-proofreading-standalone/plan.md).

- [x] There is a separate "Text checking" tab, independent settings and a separate non-modal
  checking window; the translation window has no checking UI.
- [x] Checking finds errors and shows them in a list/with highlighting.
- [x] It is possible to replace one error, all errors, and the text with the corrected one.
- [x] Replacement in the checking window is undone by undo; for an external application there is a
  popup confirmation hotkey and a separate immediate-replace hotkey.
- [x] `proofreading/enabled` is the only consent; there is no second online gate.
- [x] LanguageTool works through the cloud and a self-hosted URL, without credentials in the first
  version; premium `username`/`apiKey` support is deferred.
- [ ] Hunspell/Nuspell and Yandex Speller work at least for spelling. *(deferred)*
- [x] Network/limit errors are shown clearly and the application does not hang.
- [x] Tests and documentation are updated.
