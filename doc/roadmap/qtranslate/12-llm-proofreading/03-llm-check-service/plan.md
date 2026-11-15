---
kind: task
status: todo
depends: [02-llm-check-response]
---

# LLM text-checking service

## Task

Implement `LlmProofreadingService` — the checking provider with id `llm` — on top of the existing
`OpenAiChat` transport, register it next to `LanguageToolService`, and let the checking settings
accept the id. The issue list, the preview, Replace/Ignore/Replace all, the checking window, the popup
and both hotkeys are provider-agnostic: once the service answers with `ProofreadingIssue` values, they
work unchanged. This task proves exactly that and adds no UI of its own (`04-llm-check-ui` adapts the
settings page).

New `src/services/llmproofreadingservice.{h,cpp}`:

```cpp
class LlmProofreadingService : public ProofreadingService
{
public:
    explicit LlmProofreadingService(NetworkManager *network, QObject *parent = nullptr);

    QString id() const override;                 // "llm"
    QString displayName() const override;        // "LLM", untranslated
    bool isConfigured() const override;
    QString configurationError() const override;
    QStringList languages() const override;      // Languages::supported("llm", Source)
    bool supportsAutoLanguage() const override;  // true
    void applySettings(const AppSettings &settings) override;
    ProofreadingReply *check(const ProofreadingRequest &request) override;
};
```

Decisions already taken by the split; keep them when detailing:

- **Transport reuse is `OpenAiChat`, not a new abstraction.** The service uses
  `OpenAiChat::chatCompletionsUrl()`, `OpenAiChat::serialize()`, `OpenAiChat::parse()`,
  `OpenAiChat::errorForHttpStatus()` and `OpenAiChat::stripCodeFence()` (from
  `02-llm-check-response`). The HTTP glue mirrors `OpenAiCompatibleService::Reply`
  (`src/services/openaicompatibleservice.cpp:157-319`) but stays in this service: the reply bases
  differ (`ProofreadingReply` vs `TranslationReply`) and the messages belong to a
  `LlmProofreadingService` translation context. **No `OpenAiChatCall` extraction**, and the
  unreviewed `stash@{0}` draft is neither used nor trusted. `src/ai/openaichat.*` and
  `src/services/openaicompatibleservice.*` must not change in this task beyond the
  `stripCodeFence` move of `02-llm-check-response`.
- **One `[llm]` configuration.** `applySettings()` reads `settings.llm.baseUrl/apiKey/model` exactly
  like `OpenAiCompatibleService::applySettings()` (`:372-393`), including the default base URL for an
  empty value and `OpenAiChat::chatCompletionsUrl()` for the endpoint. `proofreading/endpoint` stays
  LanguageTool-only and is not read here.
- **Names and languages.** `displayName()` returns the untranslated literal `LLM`, like
  `LanguageTool`, `Bing`, `DuckDuckGo`. `languages()` is `Languages::supported("llm",
  Direction::Source)` (the catalogue is the Google one, `src/core/languages.cpp:426-428`), and
  `supportsAutoLanguage()` is true, so no `languages.cpp` change is needed.
- **Configured means model + endpoint.** `isConfigured()` is the translation rule
  (`src/services/openaicompatibleservice.cpp:349-353`): a non-empty model and a valid endpoint with a host. A
  missing key is not a configuration failure: a local server needs none. There is no key pre-check —
  the request goes out without an `Authorization` header and a remote server answers `401`, which maps
  to `Auth`. That avoids duplicating the local-host heuristic and keeps one code path. The
  configuration hint is a plain English literal that names the AI tab (a technical detail appended to
  the translated `ProofreadingErrorText` title, the same shape as LanguageTool's messages).
- **Request.** `OpenAiChat::Request` with `temperature = 0.0`, `maxTokens = 0` (not sent), the
  OpenRouter referer/title the translation service uses, the system prompt from
  `LlmProofreading::systemPrompt()` and one user message with `request.text.trimmed()` (leading and
  trailing whitespace dropped, interior text untouched, so offsets match the trimmed text the parser
  receives).
- **No chunking.** The whole text goes in one request: the checking window is a manual action, and an
  edit split across a chunk boundary is worse than a size error. `413` and `finish_reason: "length"`
  become `TooLong` (the parse already reports it, `src/ai/openaichat.cpp:137-139`), and the risk is
  recorded rather than worked around.
- **Errors** map onto the existing `TranslationError` values, with fixed messages that never echo the
  text, the key or the body: `401/403` → `Auth`; `402` → `QuotaExceeded`; `413` → `TooLong`; `429` →
  `RateLimited`; `404` → `BadResponse` ("the server or model was not found"); `400` → `BadResponse`
  ("the provider rejected the request; check the model name"); `5xx` → `Network`
  (`OpenAiChat::errorForHttpStatus()`); other `QNetworkReply` errors → `Network` with
  `NetworkManager::describeNetworkError()`; `OperationCanceledError`/`TimeoutError` → `Timeout` with
  the `NetworkManager::timeoutMs()` seconds; a `200` with no `choices` or an error object → the
  `OpenAiChat::parse()` error; `Status::Malformed` and "every returned edit failed to locate" →
  `BadResponse`.
- **Cancellation and logs.** `finished()` never fires inside `check()` (start on the next event-loop
  iteration), and `abortImpl()` aborts and deletes the HTTP reply, so a stale request ends as
  `Cancelled` through `ProofreadingManager`. The logging category is
  `qtranslate.service.llm-proofreading`; `qCDebug` lines carry character counts, issue counts, the
  dropped count, elapsed milliseconds, tokens and HTTP status only.

Wiring:

- `src/app/appcontroller.h`: forward-declare `LlmProofreadingService`, add the member next to
  `m_languageTool` (`:134`).
- `src/app/appcontroller.cpp`: create and `applySettings()` it in `createProofreading()`
  (`:292-306`), register it **after** `m_languageTool` so LanguageTool stays the first registered
  service (and therefore the default when the stored id is unknown), and apply the settings in
  `applySettings()` next to `m_languageTool` (`:670-671`). A running reply keeps its captured
  endpoint and key, so a settings change does not abort it.
- `src/core/settings.cpp:334-337`: `proofreadingProviderIds()` becomes
  `{"languagetool", "llm"}`. The comment in `src/core/settings.h:147-150` stays true.
- `CMakeLists.txt` (next to `languagetoolservice`, `:66-67`) and `tests/CMakeLists.txt`.

## Done when

- `LlmProofreadingService` implements the contract: id `llm`, `displayName()` `LLM`,
  `languages()` non-empty with `auto` supported, `isConfigured()` false without a model or with an
  invalid endpoint, `configurationError()` naming the AI tab, `applySettings()` reading `[llm]`.
- `check()` sends one Chat Completions request to `<baseUrl>/chat/completions` with the configured
  model, `temperature: 0`, no `max_tokens`, a system message from `systemPrompt()` and a user message
  equal to the trimmed text, plus `Authorization: Bearer <key>` only when a key is set.
- New `tests/tst_llmproofreadingservice.cpp` against a local `QTcpServer` covers: defaults and
  `applySettings()`; the request body and headers; two structured edits with locally computed offsets
  and `correctedText` empty; the plain-text fallback with `correctedText` filled; `401`, `402`, `413`,
  `429`, `502`, `404`, `400`, a `200` error object, a `200` without `choices`, `finish_reason:
  "length"`, `{"foo":1}` and edits that do not match the text; a transfer timeout; `abort()` →
  `Cancelled`; an unsupported language code rejected without a request; a log check proving neither
  the text nor the key appears in any captured message.
- `proofreadingProviderIds()` lists `languagetool` and `llm`; a stored `proofreading/provider=llm`
  survives `SettingsStore`, an unknown id still falls back to `languagetool`; `tst_settings` covers
  both.
- `tst_proofreadingmanager`, `tst_languagetoolservice`, `tst_languagetoolparser`,
  `tst_proofreadingwindow` and `tst_proofreadingpopup` pass without an edit — the issue list, the
  preview, Replace/Ignore/Replace all and the popup are provider-independent.
- Linux becomes 52 tests (the two new targets), Windows 46 (derived). Debug and Release build without
  warnings on the host and in `qtranslate-trixie`, 52/52 in both; Windows not performed. No `.ts`
  change and no `tr()` in this task.

## Risks

- **Double-checking the same text.** A check cancels only the unfinished request of its channel; a
  window check and a popup check can run at once. The service must therefore keep each HTTP reply on
  its own `ProofreadingReply` and never share state between replies.
- **The model answers 200 with prose.** The parser reports `Status::PlainText` and the diff keeps the
  result usable; reporting `BadResponse` for that case would throw away a usable correction.
- **"No issues" vs "unusable".** A response whose edits all fail to locate must be a `BadResponse`,
  not an empty result, or the user is told the text is clean when the model did change something.
- **The stashed draft is not a base.** `stash@{0}` is unreviewed; reusing `OpenAiChat` and reading the
  working `OpenAiCompatibleService::Reply` is the reference, and the stashed code is not cherry-picked.
- **Message wording is technical English.** The translated title comes from `ProofreadingErrorText`;
  the detail is a plain literal, exactly as LanguageTool's `language "xx" is not supported`. The
  `Auth` title says "rejected the request", which reads slightly off for an unconfigured provider;
  changing the shared `ProofreadingManager` mapping is out of scope.
- **A real model is still unverified.** The mock server proves the protocol, the mapping and
  cancellation; a live run with a real key or a local Ollama/llama.cpp server is the user's check and
  is recorded as not performed until then.
