# Dictionary: entries, meanings, and glossaries

Date: 2026-11-05. Status: **roadmap stage 7 (old stage 17); design, code not implemented**. See
roadmap stage 7 in [`plan.md`](roadmap/qtranslate/plan.md).

## 1. Goal

A separate "Dictionary" mode for a word or a short phrase:

- part of speech, meanings, usage examples, synonyms, and transcription;
- translations in a language pair if the selected source supports them;
- quick invocation from the main window, the popup, and by a hotkey;
- a local CSV glossary for your own terminology;
- no automatic text replacement and no effect on translation settings.

The dictionary **is not** a `TranslationService` and must not appear in the list of translation
services. It is a separate `DictionaryService` with its own cache, settings, and UI.

## 2. What is included

- `DictionaryRequest` / `DictionaryEntry` / `DictionaryDefinition`;
- `DictionaryService` with an asynchronous `lookup()` and cancellation of stale requests;
- sources:
  - **LLM** via the already available OpenAI-compatible transport (`[llm]`), a JSON schema;
  - **Google gtx `dt=bd`** — unofficial, only for short words and phrases;
  - **CSV glossary** — a local file, configurable term and meaning columns;
  - **Wiktionary** via the MediaWiki API — free, no key, but article formats differ;
- a dictionary panel in the main window and a compact card in the popup;
- a hotkey, empty by default;
- `[dictionary]` settings: source, article language, CSV path, display options;
- an in-memory cache keyed by `source|language|model|text`; long texts are not cached.

## 3. What is not included

- translation replacement and automatic insertion into the input field;
- a full offline dictionary for all languages;
- OCR/ASR/TTS;
- storing request history and texts on disk;
- training own models.

## 4. Data model

```cpp
struct DictionaryRequest
{
    QString text;              // word or short phrase
    QString language;          // article language, internal code
    QString targetLanguage;    // language of meanings/translation, if the source supports it
};

struct DictionaryDefinition
{
    QString text;
    QString example;           // may be empty
};

struct DictionaryEntry
{
    QString word;              // base form
    QString partOfSpeech;      // noun | verb | adjective | ...
    QString phonetic;          // IPA or empty
    QList<DictionaryDefinition> definitions;
    QStringList synonyms;
};

struct DictionaryResult
{
    QList<DictionaryEntry> entries;
    QString sourceId;
    QString errorMessage;      // without the source text and keys
    TranslationError error = TranslationError::None;
};
```

Replacement offsets are not needed: the dictionary only shows the entry. If word highlighting in the
text appears later, UTF-16 offsets are used, as in [`proofreading.md`](proofreading.md).

## 5. Sources

### 5.1. LLM (the main one for any language)

Uses the same `[llm]` settings as translation, but a separate system prompt and cache.

Prompt (a schema, not the final text):

```json
{
  "entries": [
    {
      "word": "canonical form",
      "part_of_speech": "noun | verb | adjective | phrase | ...",
      "phonetic": "/fəˈnɛtɪk/ or null",
      "definitions": [
        {"text": "definition in the article language", "example": "usage example or null"}
      ],
      "synonyms": ["..."]
    }
  ]
}
```

Rules:

- up to 3 entries for different meanings/parts of speech, with 1–3 definitions each;
- `entries: []` if the word is not found;
- the response is JSON only, without a Markdown wrapper;
- the user's text is data, not instructions;
- the response is validated; on error "could not parse the entry" is shown instead of raw JSON;
- transcription only if the model is confident, otherwise empty.

The LLM can hallucinate. The UI needs an "AI" mark, and for terms — the ability to cross-check with
the CSV glossary or Wiktionary.

### 5.2. Google gtx `dt=bd`

`translate.googleapis.com/translate_a/single?...&dt=bd` returns dictionary entries for short
queries: parts of speech, meanings, examples, synonyms. The format is undocumented and may change.

- words and short phrases only, not paragraphs;
- an unofficial source, rate limit, and a warning in settings;
- a separate parser with fixtures;
- on error — fallback to LLM or Wiktionary.

### 5.3. CSV glossary

A local file that the user selects in settings:

```csv
term,meaning,part_of_speech,example
API,software interface,noun,The API returns JSON.
```

- columns are configurable; encoding UTF-8 (auto-detection if needed);
- case-insensitive search, exact match and word-prefix match;
- the file does not leave the machine;
- read errors are shown once, without log spam;
- the glossary can be used as a hint during translation, but only when explicitly enabled.

### 5.4. Wiktionary

MediaWiki API, free and without a key. Articles differ by language and templates, so a careful parser
and a fallback are needed. For the MVP, English and Russian Wiktionary are enough; the rest — via
the LLM.

## 6. UI

- In the main window — a "Dictionary" tab/panel next to translation so that the word and the entry
  are visible at the same time.
- In the popup — a "Dictionary" button for the selected word; the card must not cover the
  translation.
- The hotkey is empty by default; the user assigns it in settings.
- Entry: word, transcription, part of speech, meanings, examples, synonyms, source.
- Buttons: "Copy", "Open in main window", "Refresh".
- For multiple sources — a switch or a comparison, but without turning the dictionary into a
  translation comparison.
- Long text: show "The dictionary is available for words and short phrases" instead of a request.

## 7. Settings

```ini
[dictionary]
enabled=true
source=llm              ; llm | google | csv | wiktionary
articleLanguage=auto    ; auto = the language of the source text/system
targetLanguage=ru
csvPath=
csvTermColumn=term
csvMeaningColumn=meaning
```

Keys and the base URL are taken from `[llm]`; the dictionary does not need a separate key. If the
LLM is not configured, the `llm` source is unavailable, and the UI offers Google/CSV/Wiktionary.

## 8. Cache and network

- An in-memory cache, at most 200 entries; the key includes the source, language, model, and text.
- Changing the model/base URL/source clears the cache.
- Requests are cancelled on a new word; cancellation must not break translation.
- The text, response, and keys are not written to the log. Technical errors — only the code/status.
- For Google — a rate limit and one retry on `429`; for LLM — common transport errors.

## 9. Risks

- hallucinations and incorrect etymology;
- different Wiktionary formats;
- the unofficial Google `dt=bd` may change;
- LLM cost and free-model limits;
- privacy: the text goes to the selected provider;
- prompt injection in the word/phrase (text is data, not instructions);
- a CSV with incorrect encoding or delimiter.

## 10. Tests

- the LLM entry parser: valid JSON, a Markdown wrapper, an empty list, broken fields;
- CSV: quotes, commas inside values, BOM, different columns;
- Google `dt=bd`: fixtures, short/long text, `429`;
- cache: hit, clearing on a model change, no caching of errors;
- offscreen UI: the panel, the "Copy" button, source fallback, RTL;
- manual smoke: OpenRouter, a local model, CSV, Wiktionary.

## 11. Done criteria

- [ ] The dictionary opens from the main window and the popup without affecting translation.
- [ ] The LLM source returns a structured entry and handles errors correctly.
- [ ] The CSV glossary works locally and does not send data over the network.
- [ ] Google/Wiktionary — at least one additional source with a fallback.
- [ ] Cache, cancellation, DPAPI, and the absence of text in logs are covered by tests.
- [ ] README, architecture, and the plan are updated.
