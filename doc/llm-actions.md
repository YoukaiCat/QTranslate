# LLM actions: translation, checking, summarization, and rewriting

Date: 2026-11-05. Status: **roadmap stage 6 (old stage 16); translation via an OpenAI-compatible
provider is implemented; the other actions and the dictionary are planned**. The dictionary has its
own design — [`dictionary.md`](dictionary.md).

## 1. Goal

Add an optional LLM layer for text actions:

- translation;
- spelling/grammar/punctuation checking;
- summarization;
- rewriting (tone, style, shortening, formalization);
- explanation of translations, idioms, and terms;
- transcription/pronunciation as IPA or a text description.

The LLM does not replace DeepL/Google/LanguageTool; it complements them for tasks that need context,
tone, and instruction following. The main translators and structured checkers remain specialized.

## 2. What is included

- `LlmProvider` — an OpenAI-compatible client:
  - cloud: OpenAI, Gemini (OpenAI-compatible endpoint), OpenRouter, Groq, and others;
  - local: Ollama, LM Studio, llama.cpp server;
  - settings for base URL, model, key, timeout. **Already implemented** for translation:
    the `llm` service and the `src/ai/openaichat.*` transport.
- `AiAction` — an action with its own prompt, limits, and result validation:
  - `translate`;
  - `proofread`;
  - `summarize`;
  - `rewrite`;
  - `explain`;
  - `pronounce` (text/IPA).
- UI: AI action menu/buttons, preview, copying, replacement with confirmation.
- Settings: a separate "AI" tab (or "LLM"), independent keys.
- Local mode: connection to `http://127.0.0.1:.../v1` without a mandatory cloud.
- Result cache for repeated identical requests (where possible).

## 3. What is not included

- ASR/TTS/OCR — these are separate engines (future tasks), not a text LLM.
- Automatic text replacement without confirmation.
- Replacing DeepL/Google by default.
- A single model "for everything": the user chooses the model and provider.
- Training/fine-tuning our own models.
- The dictionary and glossaries — a separate `DictionaryService`, not a translation action
  ([`dictionary.md`](dictionary.md)).

## 4. Providers

| Provider | Type | Pros | Cons |
|---|---|---|---|
| **OpenAI-compatible** (OpenAI, OpenRouter, Groq, Together) | cloud | a single API, many models | price, privacy, limits |
| **Gemini API** | cloud | good quality, a free tier with limits | a different API/format |
| **Anthropic API** | cloud | high-quality text | a different API, price |
| **Ollama / LM Studio** | local | private, no keys | requires your own server and hardware |
| **llama.cpp server** | local | embeds/sidecar, GGUF | model 2.5–5 GB, slower on CPU |

For the MVP one OpenAI-compatible client is enough: it covers the cloud and local servers.

## 5. Architecture

The implemented transport (`src/ai/openaichat.h`):

```cpp
namespace OpenAiChat {
struct Message { QString role; QString content; };
struct Request {
    QUrl endpoint;        // .../chat/completions
    QString apiKey;       // empty for a local server
    QString model;
    QList<Message> messages;
    double temperature = 0.2;
    int maxTokens = 0;
    QString referer;      // OpenRouter attribution, optional
    QString title;
};
struct Response {
    QString text;
    QString model;
    QString finishReason;
    int promptTokens = 0;
    int completionTokens = 0;
    TranslationError error = TranslationError::None;
    QString errorMessage;
};
QByteArray serialize(const Request &);
Response parse(const QByteArray &);
QUrl chatCompletionsUrl(const QString &baseUrl);
QUrl modelsUrl(const QString &baseUrl);
}
```

`OpenAiCompatibleService` (`src/services/openaicompatibleservice.*`) implements
`TranslationService` on top of this transport. It will also be the basis of `LlmProvider`; a separate
class per provider is not needed. OpenRouter is only the default address; local Ollama, LM Studio,
and llama.cpp work through the same `/v1/chat/completions` and do not require a key. For future
actions the following is proposed:

```cpp
struct LlmRequest
{
    QString action;          // translate | proofread | summarize | rewrite | explain
    QString text;
    QString sourceLang;
    QString targetLang;
    QString style;           // formal | informal | short | ...
    QString systemPrompt;
};

struct LlmResult
{
    QString text;
    QString model;
    int promptTokens = 0;
    int completionTokens = 0;
};

class LlmProvider
{
public:
    virtual ~LlmProvider() = default;
    virtual QString id() const = 0;
    virtual bool isConfigured() const = 0;
    virtual void chat(const QVector<ChatMessage> &messages,
                      std::function<void(LlmResult)> callback) = 0;
};
```

The action layer:

- `AiActionRegistry` — the list of actions, names, icons, shortcuts.
- `AiAction` — builds the prompt, calls `LlmProvider`, validates the response.
- For `proofread` — ask for JSON with a list of edits (`offset`, `length`, `replacement`,
  `message`) or diff the source and the corrected text.
- For `translate` — a strict prompt "return only the translation, without explanations", language and
  length checks.
- For `summarize` — a length/format parameter.
- For `rewrite` — style/tone/audience.
- For `explain` — a response in a chat/panel, without replacing the text.

Settings (already used by translation):

```ini
[llm]
baseUrl=https://openrouter.ai/api/v1   ; or OpenAI/Ollama/llama.cpp/TeamRouter
model=openrouter/free
apiKey=                                 ; DPAPI; empty for a local server
```

`temperature` and `maxTokens` are currently set in the translation code (0.2, without max_tokens);
for AI actions they will become separate settings. Keys — via DPAPI, as with DeepL/Google. Do not
write text, keys, or full model responses to the log. Changing `baseUrl`/`model` clears the
translation cache.

## 6. UI

- A separate **"AI"** settings tab: provider, base URL, model, key, connection test.
- Actions menu/panel: "Translate", "Check", "Summarize", "Rewrite", "Explain", "Pronunciation".
- The result is in a separate panel with a diff/preview; replacement — on a button click.
- For an external selection — the same actions through the popup, with paste confirmation.
- Indication: whether the cloud or a local model is used; how many tokens/time.
- Errors: no key, no connection, limit, model not found, response not in the expected format.

## 7. Prompts and validation

- The system prompt fixes the role: "you are a translator/editor, return only the result".
- Language, tone, length, and glossary are passed as separate fields, not glued to the text.
- The default temperature is low (0–0.3).
- For translation — a check that the response is not empty and does not contain service explanations.
- For checking — a JSON schema + fallback to a diff.
- For summarization — a length limit and a check that it is not a translation.
- Do not treat model responses as trusted: do not execute commands from the text, do not log secrets.

## 8. Risks

- hallucinations and added meaning;
- unstable JSON format;
- cloud cost and limits;
- privacy: the text goes to the provider;
- local models require RAM/CPU/GPU and disk space;
- different models hold language/tone differently;
- prompt injections in the translated text (text is data, not instructions);
- keys and cookies must not end up in the logs.

## 9. Tests

- `tst_openaichat`: request serialization, parsing plain/array content, errors,
  `finish_reason`, model list, URL construction;
- `tst_openaicompatibleservice`: a mock OpenAI-compatible server, JSON/plain fallback,
  preserving leading and trailing spaces, 401/402/429/413/5xx, timeout, cancellation, model loading,
  a local server without a key, `openrouter/free`;
- `tst_settings`/`tst_settingsdialog`: DPAPI, base URL, model, the "Load models" button;
- prompt checks for "return only the translation";
- manual smoke: cloud + Ollama/llama.cpp, different models.

## 10. Priorities

1. ~~OpenAI-compatible provider + settings + `translate`~~ — done.
2. `summarize`/`rewrite`/`explain`/`pronounce` on the same transport.
3. `proofread` via LLM (in addition to LanguageTool).
4. Local llama.cpp/Ollama as a full-fledged provider (the transport is already implemented).
5. The dictionary — a separate `DictionaryService` ([`dictionary.md`](dictionary.md)).
6. Response streaming and cancellation (if needed).

## 11. Done criteria

- [x] A cloud OpenAI-compatible provider with a key and model works (OpenRouter).
- [x] A configurable base URL and model loading work; the local server is covered by tests,
      live smoke — after a local server becomes available.
- [ ] There is a separate "AI" tab and independent settings.
- [ ] The "summarize", "rewrite", "explain" actions produce a result and do not change the text
      without confirmation.
- [ ] `proofread` returns structured edits or a diff.
- [x] Key/limit/network/model errors are clear, and the application does not hang.
- [x] Keys via DPAPI, texts are not logged, tests and documentation are updated (for translation).
