# Spelling, grammar, and punctuation checking

Date: 2026-11-05. Status: **roadmap stage 5 (old stage 15), in progress; code not implemented yet**. The
first version ships **LanguageTool only** (user decision of 2026-11-09); Yandex Speller and
Hunspell/Nuspell are deferred and kept here as the longer-term design.

## 1. Goal

Add a separate text-checking mode to qtranslate:

- find spelling, grammar, and punctuation errors;
- show them in a list and with highlighting;
- accept replacements one by one or all at once;
- replace the source text with the corrected text;
- optionally check the text before translation.

This is not part of translation: checking must be a separate action, a separate settings tab, and a
separate set of settings.

## 2. Scenarios

1. **Check text in the main window.** The user clicks "Check" or presses the configured shortcut;
   errors are highlighted in the source field, and a list appears on the right/below.
2. **Replace with the corrected text.** The "Replace all" / "Apply corrections" button replaces the
   text in the field while preserving formatting and allowing undo.
3. **Check the selection in another application.** Similar to Ctrl+Shift+Q: capture the selection,
   check it, show candidates, and replace the selection with the corrected text. For an external
   application, protection against active-window changes and a warning that undo depends on the
   application are needed.
4. **Check before translation.** The "Check before translation" option (off by default): show errors
   and ask for confirmation instead of silently changing the text.
5. **Spelling only.** A lightweight offline mode without grammar and punctuation.

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
    virtual void check(const ProofreadingRequest &request,
                       std::function<void(ProofreadingResult)> callback) = 0;
};
```

Details:

- `offset`/`length` are in UTF-16 (`QString`), not bytes; do not split surrogate pairs.
- Split long text by sentences/paragraphs, merge offsets taking separators into account;
- Cancel a stale request on a new click;
- Do not block the UI; network — through the existing `NetworkManager` with proxy and timeout;
- API keys — through DPAPI, as with DeepL/Google;
- Do not write the checked text to the log.

For text replacement:

- In the main window — through `QTextDocument`/the cursor so that undo works;
- In an external application — through the same mechanism as Ctrl+Shift+Q, with active-window
  checking;
- Before "replace all", show a preview/diff if there is more than one correction;
- After replacement, recalculate the offsets or request the check again.

## 5. Settings

A separate **"Text checking"** tab (do not mix it with "Translation"):

- `proofreading/enabled` — enable the mode;
- `proofreading/provider` — `languagetool` in the first version; `llm` arrives with stage 6;
- `proofreading/language` — checking language (`auto` or a specific one);
- `proofreading/endpoint` — URL of your own LanguageTool/LLM;
- `proofreading/categories` — spelling, grammar, punctuation, style;
- `proofreading/checkBeforeTranslation` — `false` by default;
- `proofreading/autoReplace` — `false` by default (do not change text silently);
- `proofreading/allowOnlineServices` — `false` by default; remote checking is opt-in;
- `proofreading/hotkey` — a separate shortcut, empty by default.

The first version sends no credentials: the public LanguageTool API and a self-hosted URL work
without a key, and the premium `username`/`apiKey` mode is deferred to a later task.

The tab must be separate, as requested; the settings must be independent of translation and must not
affect the selected translation service.

## 6. UI

- A "Check" button next to "Translate" (or a separate panel).
- Highlighting issues in the source field; clicking selects the fragment.
- List: message, rule, replacement options, "Replace", "Ignore" buttons.
- "Replace all" / "Apply corrections" with a preview.
- "Copy corrected text".
- States: checking, no errors, provider unavailable, request limit.
- For an external selection: a separate window/panel, like the popup, with replacement confirmation.

## 7. Tests

- The LanguageTool parser on fixtures: several errors, replacements, offset/length, UTF-16 and
  surrogate pairs, an empty response, 429/403/5xx;
- Merging offsets when splitting long text;
- Hunspell/Nuspell: dictionary, case, punctuation around the word; *(deferred)*
- Yandex Speller: success, empty response, limit; *(deferred)*
- Settings: saving/loading, the online gate, a separate tab;
- UI (offscreen): the list, replacing one/all, undo, request cancellation;
- Manual smoke: LanguageTool cloud/self-hosted, replacement in Notepad/a browser.

## 8. Risks

- Rule false positives; the user must confirm replacements;
- Punctuation rules depend on language and style;
- LanguageTool is Java; the cloud API is simpler, self-hosting is heavier;
- The public API has limits and does not guarantee availability;
- An LLM may rewrite the meaning; only a diff + confirmation;
- Replacement in an external application is not always undone by undo;
- The checked text goes to the provider; local Hunspell/Nuspell are more private.

## 9. Priorities

1. **LanguageTool** — grammar and punctuation for the main languages.
2. **Hunspell/Nuspell** — lightweight offline spelling.
3. **Yandex Speller** — free spelling for ru/uk/en.
4. **LLM** — optional, for style/tone, not as the only checker.

## 10. Done criteria

The first version implements LanguageTool and the online gate; the Hunspell/Nuspell and Yandex
Speller items below are the long-term design and are not part of stage 5 (user decision of
2026-11-09). The stage criteria live in [`roadmap/qtranslate/05-proofreading/plan.md`](roadmap/qtranslate/05-proofreading/plan.md).

- [ ] There is a separate "Text checking" tab and independent settings.
- [ ] Checking finds errors and shows them in a list/with highlighting.
- [ ] It is possible to replace one error, all errors, and the text with the corrected one.
- [ ] Replacement in the main window is undone by undo; for an external application there is
  confirmation.
- [ ] LanguageTool works through the cloud and a self-hosted URL, without credentials in the first
  version; premium `username`/`apiKey` support is deferred.
- [ ] Hunspell/Nuspell and Yandex Speller work at least for spelling. *(deferred)*
- [ ] Network/limit/key errors are shown clearly and the application does not hang.
- [ ] Tests and documentation are updated.
