---
kind: task
status: done
detailed: true
depends: [02-llm-check-response]
---

# LLM text-checking service

## Task

Implement `LlmProofreadingService` — the checking provider with id `llm` — on top of the existing
`OpenAiChat` transport, register it next to `LanguageToolService`, and let the checking settings accept
the id. The issue list, the preview, Replace/Ignore/Replace all, the checking window, the popup and both
hotkeys are provider-agnostic: once the service answers with `ProofreadingIssue` values they work
unchanged. This task proves exactly that and adds no UI of its own (`04-llm-check-ui` adapts the settings
page).

New `src/services/llmproofreadingservice.{h,cpp}`, shaped like `LanguageToolService`
(`src/services/languagetoolservice.h:16-35`, `.cpp:115-164, 337-420`):

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

## The response contract of `02-llm-check-response`

The parser landed as `5dcc390` and changed what the service may assume
(`src/services/llmproofreadingparser.h:16-36`). The mapping below is the core of this task; the entry
points are `OpenAiChat::parse()` (`src/ai/openaichat.cpp:89-159`) and
`LlmProofreading::parseResponse()` (`llmproofreadingparser.h:34-35`).

- **`Status::Edits` with an empty `issues` and `droppedEdits == 0` is a normal "nothing to fix"
  answer** — the model was asked for `{"edits":[]}` and complied. It is a successful check with zero
  issues, **not** `BadResponse`.
- **`Status::Edits` with an empty `issues` and `droppedEdits > 0` is `BadResponse`**: the model did
  return changes, but none of them could be placed in the text. Reporting "the text is clean" here would
  hide a real correction.
- **`Status::Malformed` is `BadResponse`** (empty answer, JSON that is not an edits document).
- **`Status::PlainText` is a successful check**; its issues come from `diffIssues()`. Zero issues is
  again valid: an answer that differs from the source only in surrounding whitespace is "no change"
  (parser decision 3 in the `02` Outcome).
- **A truncated answer must not reach the parser.** `max_tokens` is not sent, but a provider may still
  stop at its own limit; the JSON is then cut and nothing parses, so `parseResponse()` would classify it
  as `PlainText` and the diff would "correct" the whole text. The guard is `OpenAiChat::parse()`, which
  turns `finish_reason: "length"` into `TranslationError::TooLong`
  (`src/ai/openaichat.cpp:137-141`) **before** the content is interpreted; the service therefore gates on
  `!response.ok()` and never inspects the raw body or `finish_reason` itself. The same call already maps
  `content_filter` and `error` (`:142-151`), an empty answer (`:152-156`), an `error` object and a
  `choices`-less `200` (`:100-119`) to fixed errors.
- **`systemPrompt()` takes the internal language code** — `auto`, an empty code and every catalogue code
  are resolved inside the parser (`llmproofreadingparser.h:31-33`), so the service forwards
  `ProofreadingRequest::language` unchanged and must not call `Languages::englishName()`.
- **`Parsed::droppedEdits` is the only telemetry of the parser** — the module has no logging category and
  makes no log call. If a count is worth a log line, the service writes the number and never the text.

Decisions carried over from the split, corrected where the code has moved on:

- **Transport reuse is `OpenAiChat`, not a new abstraction.** The service uses
  `OpenAiChat::chatCompletionsUrl()`, `serialize()`, `parse()` and `errorForHttpStatus()`
  (`src/ai/openaichat.h:49-58`); `stripCodeFence()` already lives there (`:64-66`) and is only used by the
  parser. The HTTP glue mirrors `OpenAiCompatibleService::Reply`
  (`src/services/openaicompatibleservice.cpp:142-304`) but stays in this service: the reply bases differ
  (`ProofreadingReply` vs `TranslationReply`) and the messages have their own service. **No
  `OpenAiChatCall` extraction**, and the unreviewed `stash@{0}` draft is neither used nor trusted.
  `src/ai/openaichat.*` and `src/services/openaicompatibleservice.*` do not change in this task; the
  service only *calls* the two public statics `OpenAiCompatibleService::defaultBaseUrl()` /
  `defaultEndpoint()` (`src/services/openaicompatibleservice.h:40-41`) so the default provider URL has
  one spelling in the tree.
- **One `[llm]` configuration.** `applySettings()` reads `settings.llm.baseUrl/apiKey/model` exactly like
  `OpenAiCompatibleService::applySettings()` (`src/services/openaicompatibleservice.cpp:357-378`): an
  empty base URL falls back to the default one, the endpoint is `OpenAiChat::chatCompletionsUrl()`, and an
  unusable base URL falls back to the default endpoint. `proofreading/endpoint` stays LanguageTool-only
  and is not read here.
- **Configured means a model.** The endpoint always ends up valid because of that fallback, so
  `isConfigured()` is `!model.isEmpty() && endpoint.isValid() && !endpoint.host().isEmpty()` and in
  practice false only without a model (the translation rule, `openaicompatibleservice.cpp:334-338`). A
  missing key is **not** a configuration failure: a local server needs none, and a remote one answers
  `401`, which maps to `Auth`. Deliberately unlike `OpenAiCompatibleService::translate()`
  (`:389-396`), which pre-checks the key for non-local hosts; that heuristic is not duplicated here.
  `configurationError()` is a plain English literal that names the AI tab (the technical detail appended
  to the translated `ProofreadingErrorText` title, the same shape as LanguageTool's).
- **Names and languages.** `displayName()` is the untranslated literal `LLM`, like `LanguageTool`,
  `Bing`, `DuckDuckGo`; it is what the provider combo will show. `languages()` is
  `Languages::supported("llm", Direction::Source)` — the internal code *is* the service code for `llm`
  (`src/core/languages.cpp:426-428`), the catalogue is Google's — and `supportsAutoLanguage()` is true, so
  `languages.cpp` needs no change.
- **The request.** `OpenAiChat::Request`: `temperature = 0.0`, `maxTokens = 0` (not sent), the OpenRouter
  referer/title the translation service uses (`openaicompatibleservice.cpp:462-463`), a system message
  from `LlmProofreading::systemPrompt(request.language, request.categories)` and one user message equal to
  the trimmed text. `Authorization: Bearer` only when a key is set.
- **Offsets index the untrimmed text.** The message is trimmed (`temperature 0` plus a smaller prompt is
  worth it, and it avoids a spurious "the trailing newline was deleted" diff), but the window applies the
  issues to `m_sourceEdit->toPlainText()` (`src/ui/proofreadingwindow.cpp:154-160, 178-200`) and
  `AppController` applies `ProofreadingText::corrected(text, result.issues)` to the original
  (`src/app/appcontroller.cpp:1223, 1298`). The source of the parse is therefore the trimmed text and every
  returned issue gets the leading-whitespace length added to `offset` — the same shift LanguageTool does
  per chunk (`languagetoolservice.cpp:263-271`). Forgetting it shifts every highlight by the indentation
  of the checked text.
- **`ProofreadingResult::correctedText` is filled for the plain-text answer**, as its comment promises
  (`src/core/proofreadingtypes.h:29-31`): the model's answer re-wrapped in the source's surrounding
  whitespace, so it is the whole corrected text. For the `Edits` path it stays empty; the replacement text
  is `ProofreadingText::corrected(source, issues)`, which the window and the controller compute themselves.
- **No chunking.** The whole text goes in one request: a check is a manual action, and an edit split
  across a chunk boundary is worse than a size error. `413` and `finish_reason: "length"` are `TooLong`;
  the risk is recorded rather than worked around.
- **Errors** map onto the existing `TranslationError` values with fixed English messages that never echo
  the text, the key, the URL or the body: `401/403` → `Auth`; `402` → `QuotaExceeded`; `413` → `TooLong`;
  `429` → `RateLimited`; `404` → `BadResponse` ("the server or model was not found"); `400` →
  `BadResponse` ("the provider rejected the request; the model name may be wrong"); `5xx` → `Network`
  (`OpenAiChat::errorForHttpStatus()`, `openaichat.cpp:161-181`); other `QNetworkReply` errors →
  `Network` with `NetworkManager::describeNetworkError(reply, false)` (no URL in the message, as
  `languagetoolservice.cpp:248-252`); `OperationCanceledError`/`TimeoutError` → `Timeout` with the
  `NetworkManager::timeoutMs()` seconds; a `200` error object or a `choices`-less body → the
  `OpenAiChat::parse()` error, with `errorCode > 0` mapped through `errorForHttpStatus()` like
  `openaicompatibleservice.cpp:255-263`.
- **No `tr()` anywhere in this task.** The user-visible titles are the translated
  `ProofreadingErrorText` ones (`src/ui/proofreadingerrortext.cpp:8-58`, which already covers every value
  used here); everything this file adds is a technical English detail or the literal `LLM`, exactly like
  LanguageTool's `language "xx" is not supported`. The `.ts` count must stay at 383.
- **Cancellation and logs.** `finished()` never fires inside `check()` (start on the next event-loop
  iteration, `ProofreadingReply::createFinished()` for the pre-network refusals), and `abortImpl()` aborts
  and deletes the HTTP reply, so a stale request ends as `Cancelled` through `ProofreadingManager`
  (`src/core/proofreadingmanager.cpp:130-193`). The logging category is
  `qtranslate.service.llm-proofreading`; `qCDebug` lines carry character counts, issue counts,
  `droppedEdits`, elapsed milliseconds, tokens and the HTTP status only — never the text, an issue
  message, a replacement, the URL or the key.

## Steps

Ten files change: two new (`src/services/llmproofreadingservice.{h,cpp}`), one new test
(`tests/tst_llmproofreadingservice.cpp`), and edits in `src/core/settings.{h,cpp}`,
`src/app/appcontroller.{h,cpp}`, `CMakeLists.txt`, `tests/CMakeLists.txt` and `tests/tst_settings.cpp`.

1. **Header and the identity/settings half of the service.**
   - `src/services/llmproofreadingservice.h` (new): `#include "core/proofreadingservice.h"`, `#include
     <QUrl>`, `class NetworkManager;`; the class comment in the shape of
     `languagetoolservice.h:10-15` — the second checking provider, one Chat Completions request per
     check, offsets shifted back to the untrimmed text, messages fixed and never carrying the text or the
     key. The interface of `## Task` in that order, then `private: class Reply;` and the members
     `NetworkManager *m_network = nullptr; QUrl m_endpoint; QString m_apiKey; QString m_model;`.
   - `src/services/llmproofreadingservice.cpp` (new), project includes: the header,
     `ai/openaichat.h`, `core/languages.h`, `core/settings.h`, `net/networkmanager.h`,
     `services/llmproofreadingparser.h`, `services/openaicompatibleservice.h` (for `defaultBaseUrl()` /
     `defaultEndpoint()` only), plus `QElapsedTimer`, `QLoggingCategory`, `QMetaObject`,
     `QNetworkReply`, `QNetworkRequest`, `QPointer`, `QUrl`, `QVector`.
     `Q_LOGGING_CATEGORY(lcLlmProofreading, "qtranslate.service.llm-proofreading", QtWarningMsg)`; the
     verbose filter `qtranslate.*=true` (`src/app/logging.cpp:80`) already covers the new category.
   - Anonymous namespace: `const QString kServiceId = QStringLiteral("llm");`,
     `errorName(TranslationError)` (the switch of `languagetoolservice.cpp:50-79`, fixed names only),
     `messageForHttpStatus(TranslationError, int status)` with the literals of `## Task` (the status
     number is allowed in the text; the key, the URL and the body are not), and
     `finishedWithError(TranslationError, const QString &)` like `languagetoolservice.cpp:104-111`.
   - `applySettings()`: trim key and model; base URL = `settings.llm.baseUrl.trimmed()`, defaulted with
     `OpenAiCompatibleService::defaultBaseUrl()` when empty; endpoint =
     `OpenAiChat::chatCompletionsUrl(baseUrl)`, falling back to
     `OpenAiCompatibleService::defaultEndpoint()` when it has no host. Nothing else is read.
   - `id()` / `displayName()` / `isConfigured()` / `configurationError()` / `languages()` /
     `supportsAutoLanguage()` as in `## Task`.
   - `check()`:
     - `const QString source = request.text;` then the trim loop of
       `openaicompatibleservice.cpp:415-426` to get `begin`/`end`; `begin == end` →
       `finishedWithError(TranslationError::EmptyText, QString())` (the manager also refuses it at
       `proofreadingmanager.cpp:141-142`, but a direct call must not send an empty prompt).
     - Language: `request.language != Languages::autoCode() && !languages().contains(request.language)` →
       `UnsupportedLanguage` with `language "%1" is not supported`, no request (the shape of
       `languagetoolservice.cpp:391-403`). No service-code mapping: the internal code is passed to
       `systemPrompt()` as it is.
     - Build `OpenAiChat::Request` (endpoint, key, model, the two messages, `temperature = 0.0`, keep
       `maxTokens = 0`, referer/title), one `qCDebug` line with the source length, the language, the
       category count and the model, then `new Reply(...)` with the request, `source`, `begin`, `end` and
       `request.categories`, `reply->start()`, return it. Do not enqueue: `RequestQueue` exists for
       LanguageTool's chunk sequence, and there is exactly one request here.
2. **`Reply`: the HTTP exchange and the status mapping.**
   - `class LlmProofreadingService::Reply : public ProofreadingReply` with
     `~Reply() override { stopRequest(); }`, `start()` (`m_elapsed.start()` then
     `QMetaObject::invokeMethod(this, [this] { send(); }, Qt::QueuedConnection)`),
     `abortImpl()` (`qCDebug(...) << "Cancelled"; stopRequest();`), `send()`, `onFinished()`,
     `fail(TranslationError, const QString &)`, `stopRequest()`; members `QPointer<NetworkManager>
     m_network`, `OpenAiChat::Request m_request`, `QString m_source`, `int m_begin`, `int m_end`,
     `QStringList m_categories`, `QPointer<QNetworkReply> m_httpReply`, `QElapsedTimer m_elapsed`,
     `QElapsedTimer m_requestElapsed`. Copy the shape from
     `src/services/openaicompatibleservice.cpp:142-215` and `:286-304`.
   - `send()`: a null network fails with `Network` "network is not available"; the request is
     `m_network->makeRequest(m_request.endpoint)` with `Content-Type: application/json;charset=UTF-8`,
     `Accept: application/json`, `Authorization: Bearer <key>` only when the key is non-empty,
     `HTTP-Referer`/`X-Title` from the request; body `OpenAiChat::serialize(m_request)` posted through
     `m_network->nam()`.
   - `onFinished()`, in this order (the order of `openaicompatibleservice.cpp:217-284` is the reference):
     read status and error, `deleteLater()` the reply, return when `isFinished()`, log the status and the
     elapsed time; `OperationCanceledError`/`TimeoutError` → `Timeout` with the `timeoutMs()` seconds;
     non-2xx → `OpenAiChat::errorForHttpStatus(status)` with `messageForHttpStatus()`; any other network
     error → `Network` with `describeNetworkError(reply, false)`; then
     `const OpenAiChat::Response response = OpenAiChat::parse(body);` and `!response.ok()` → the
     `errorCode > 0` branch as in `openaicompatibleservice.cpp:255-263`, otherwise
     `fail(response.error, response.errorMessage)`. **This is where `finish_reason: "length"` becomes
     `TooLong`**; do not re-parse the body, do not read `finish_reason` by hand.
   - Then `const LlmProofreading::Parsed parsed = LlmProofreading::parseResponse(
     m_source.mid(m_begin, m_end - m_begin), response.text, m_categories);` and the mapping of
     `## The response contract`: `Malformed` → `BadResponse` "the model did not return the expected
     JSON"; `Edits` with no issues → `droppedEdits > 0` ? `BadResponse` "the model returned changes that
     do not match the text" : a successful empty result; `Edits` with issues and `PlainText` → success.
   - Build the result: `serviceId = kServiceId`, a copy of `parsed.issues` with `offset += m_begin` on
     every element, `correctedText` only for `PlainText` as
     `m_source.left(m_begin) + parsed.correctedText + m_source.mid(m_end)`; one `qCDebug` line with the
     issue count, `parsed.droppedEdits`, the elapsed time and the prompt/completion tokens; `finish()`.
   - `fail()`: `qCWarning` with `errorName()` and the fixed message, then `finish()` with that error.
3. **Wiring.**
   - `src/core/settings.cpp:334-337`: `proofreadingProviderIds()` returns `{"languagetool", "llm"}`; the
     guard at `:224-230` then keeps a stored `llm` and still falls back for an unknown id.
   - `src/core/settings.h:79`: the comment `// the only id of this stage` becomes
     `// "languagetool" | "llm"`. The comment at `:147-150` stays true as written.
   - `src/app/appcontroller.h`: `class LlmProofreadingService;` next to `class LanguageToolService;`
     (`:15`), and the member after `:136`: `LlmProofreadingService *m_llmProofreading = nullptr;` —
     **not** `m_llm`, which is the translation service (`:127`).
   - `src/app/appcontroller.cpp`: include `services/llmproofreadingservice.h` next to `:28`; in
     `createProofreading()` (`:292-306`) create it, `applySettings(settings)` and `registerService()` it
     **after** `m_languageTool`, so LanguageTool stays the first registered service and therefore the
     default when the stored id is unknown (`src/core/proofreadingmanager.cpp:102-105`); in
     `applySettings()` after the `m_languageTool` block (`:679-680`) add the same two lines for
     `m_llmProofreading`, so a changed `[llm]` reaches the checker without a restart. A running reply
     keeps its captured endpoint and key, so the change does not abort it. The provider combo of the
     settings dialog is filled from the registered services (`appcontroller.cpp:788-795`), so it lists
     `LLM` after this step; hiding the LanguageTool row is `04-llm-check-ui`.
   - `CMakeLists.txt:71-72`: add `src/services/llmproofreadingservice.cpp
     src/services/llmproofreadingservice.h` right after the `llmproofreadingparser` pair.
   - Gate: `cmake --build build/linux-debug` and `ctest --test-dir build/linux-debug -R
     "tst_proofreadingmanager|tst_languagetoolservice|tst_settings|tst_settingsdialog" --output-on-failure`
     (`tst_settings` fails on the provider-id assertion until step 6; that is expected).
4. **`tests/tst_llmproofreadingservice.cpp`, the normal paths.**
   - `tests/CMakeLists.txt:42-43`: `qtranslate_add_test(tst_llmproofreadingservice)` after
     `tst_llmproofreadingparser`.
   - New file: `Q_DECLARE_METATYPE(TranslationError)`, the `FakeHttpServer` of
     `tests/tst_openaicompatibleservice.cpp:23-104` (method, URL, headers, body, `respond`, `status`,
     `body` — no `location`/`setCookie`/`delayMs` needed), `jsonObject()`/`chatResponse(content,
     finishReason)` (`:106-130`), `fakeBaseUrl(port)` and `makeSettings(port, key, model)` (`:154-165`,
     base URL `http://127.0.0.1:<port>/api/v1`), `QTEST_GUILESS_MAIN`, `#include
     "tst_llmproofreadingservice.moc"` at the bottom. `init()` builds a fresh server, `NetworkManager`
     and service and applies the settings.
   - Slots:
     - `properties()`: id `llm`, `displayName()` `LLM`, `languages()` non-empty, contains `de` and `ru`
       and not `auto`, `supportsAutoLanguage()` true, `isConfigured()` true with the defaults and false
       once the model is cleared, `configurationError()` mentions the AI tab.
     - `appliesSettings()`: `/api/v1` becomes `/api/v1/chat/completions`; an empty base URL uses the
       default host; a base URL that is already a full endpoint is kept.
     - `sendsRequest()`: POST to `/api/v1/chat/completions`; body has the model, `temperature` `0`, **no
       `max_tokens`**, two messages, the first with role `system` and a prompt containing `"edits"`, the
       second with role `user` and exactly the trimmed text; `authorization` present with a key and
       absent without one; `http-referer`/`x-title` present.
     - `structuredEdits()`: text `"  Zeile eins\nZeile zwei"`, an answer with two edits →
       `error == None`, `serviceId == "llm"`, `correctedText` empty, offsets shifted by the two leading
       spaces, `length` in QChar units, `ruleId == "llm"`, one replacement each, and
       `ProofreadingText::corrected(request.text, result.issues)` equal to the hand-written text (the
       invariant the window and the controller rely on).
     - `cleanAnswerIsNotAnError()`: `{"edits":[]}`, a fenced `{"edits":[]}` and an answer identical to
       the source all give `error == None` with no issues.
     - `plainTextFallback()`: a corrected sentence → `ruleId == "llm-diff"` issues and
       `result.correctedText` re-wrapped in the source's leading/trailing whitespace.
     - `droppedEditsIsBadResponse()`: an `edits` document whose `original` is absent → `BadResponse`,
       fixed message.
     - `truncatedAnswerIsTooLong()`: `chatResponse("{\"edits\":[{\"orig", "length")` → `TooLong` (not
       `PlainText`, not `BadResponse`), and a `413` answer → `TooLong`.
     - `malformedAnswer()`: `{"foo":1}`, an empty content and `{"edits":"nope"}` → `BadResponse`.
     - `httpErrors_data()`/`httpErrors()`: `400`, `401`, `402`, `403`, `404`, `413`, `429`, `500`, `502`
       → `BadResponse`, `Auth`, `QuotaExceeded`, `Auth`, `BadResponse`, `TooLong`, `RateLimited`,
       `Network`, `Network`; each message non-empty and free of the checked text, the key, `://` and the
       server address (the `verifyFixedMessage` idea of `tests/tst_languagetoolservice.cpp:241-249`).
     - `errorObjectInResponse()`: `200` with `{"error":{"code":429,...}}` → `RateLimited`; `200` with
       `{"error":{"message":"..."}}` → `BadResponse`; `200` without `choices` → `BadResponse`.
     - `unsupportedLanguage()` and `emptyText()`: `UnsupportedLanguage` / `EmptyText` and
       `m_server->requests.isEmpty()`.
5. **`tests/tst_llmproofreadingservice.cpp`, cancellation, timeout and log hygiene.**
   - `timeout()`: `m_network->setTimeoutMs(300)` with `respond = false` → `Timeout`, as
     `tests/tst_openaicompatibleservice.cpp:352-363`.
   - `abort()`: abort before the server answers → `QSignalSpy` on `finished()` fires exactly once and
     `result().error == Cancelled`; a late response is ignored.
   - `logHygiene()`: the `LogCapture` of `tests/tst_languagetoolservice.cpp:253-280` with the rule
     `qtranslate.service.llm-proofreading.debug=true`; run one successful check with an edit and one
     `400`; assert that no captured line contains a fragment of the checked text, the key, the server
     address, the model's `message` or the replacement, and that a debug line carries the issue count.
6. **Settings coverage and the counts.**
   - `tests/tst_settings.cpp:146-147`: `proofreadingProviderIds()` is now `{"languagetool", "llm"}`.
   - New slot next to `proofreadingRoundTrip()` (`:462-511`):
     `proofreadingLlmProviderRoundTrip()` — save and reload with `proofreading/provider=llm`, the value
     survives, the INI still holds it; the unknown-id fallback at `:544-571` stays as it is and must keep
     passing.
   - Counts: Linux 52 → **53**, Windows 46 → **47** (derived, no MSVC here).
7. **Final verification.**
   - Host: `cmake --build build/linux-debug` and `cmake --build build/linux-release` (configure with
     `cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release` if the directory is
     missing), `0` warnings under `-Wall -Wextra`, then
     `xvfb-run -a ctest --test-dir build/linux-debug --output-on-failure` and the same for
     `build/linux-release`: **53/53** each, `ctest -N` lists 53 targets.
   - Container, from the repository root:
     ```sh
     docker run --rm -v "$PWD":/src -w /src qtranslate-trixie:latest bash -c '
       cmake -S . -B build/trixie-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr &&
       cmake --build build/trixie-release &&
       timeout 420 xvfb-run -a ctest --test-dir build/trixie-release --timeout 45 --output-on-failure'
     ```
     Release **53/53**, `0` warnings; Debug in `build/trixie-debug` the same. `xvfb-run` flakes in the
     container (`01-ai-tab`, `02`): bound it with `timeout` and fall back to the host run.
   - Targeted while iterating: `ctest --test-dir build/linux-debug -R
     "tst_llmproofreadingservice|tst_llmproofreadingparser|tst_settings|tst_proofreadingmanager"
     --output-on-failure`.
   - `.ts`: `for f in i18n/*.ts; do printf '%s %s\n' "$f" "$(rg -c '<message' "$f")"; done` → **383** in
     each of the 14 files; `rg -n "tr\(" src/services/llmproofreadingservice.cpp` finds nothing, no
     `lupdate`/`lrelease` run.
   - Windows: not performed (no MSVC). The new target makes a Windows run **47**; the change is reviewed
     by diff, and no `WIN32`-specific code is touched.
   - Hygiene: `git status` shows only the ten files above plus the roadmap; `git diff --check` clean; no
     scratch file, build directory or `.ts` in the diff.

## Done when

- `LlmProofreadingService` implements the contract: id `llm`, `displayName()` the untranslated `LLM`,
  `languages()` non-empty with `auto` supported, `isConfigured()` false without a model,
  `configurationError()` naming the AI tab, `applySettings()` reading `[llm]` only.
- `check()` sends one Chat Completions request to `<baseUrl>/chat/completions` with the configured model,
  `temperature: 0`, no `max_tokens`, a system message from `systemPrompt(request.language,
  request.categories)` and a user message equal to the trimmed text, plus `Authorization: Bearer <key>`
  only when a key is set.
- **The status mapping of `02` is honoured and tested**: `Edits` with no issues and `droppedEdits == 0`
  is a successful check with zero issues; `Edits` with no issues and `droppedEdits > 0` is
  `BadResponse`; `Malformed` is `BadResponse`; `PlainText` with zero issues (an answer that differs from
  the source only in whitespace) is a successful check with zero issues.
- **A truncated answer is `TooLong`**: `finish_reason: "length"` is turned into `TooLong` by
  `OpenAiChat::parse()` before the content is interpreted, and a truncated JSON answer never becomes a
  `PlainText` diff. Proven by a test with a cut-off JSON and `finish_reason: "length"`, and by a `413`
  answer.
- Issues point into the **untrimmed** request text: a text with leading spaces yields shifted offsets, and
  `ProofreadingText::corrected(request.text, result.issues)` equals the expected text.
  `result.correctedText` is the whole corrected text for the `PlainText` answer and empty for `Edits`.
- Every error value is a fixed message: the tests cover `400`, `401`, `402`, `403`, `404`, `413`, `429`,
  `500`, `502`, a `200` error object, a `200` without `choices`, `malformed` JSON, a transfer timeout and
  `abort()` → `Cancelled`, and none of the messages leaks the text, the key, the URL or the body.
- `proofreadingProviderIds()` lists `languagetool` and `llm`; a stored `proofreading/provider=llm` survives
  `SettingsStore`, and an unknown id still falls back to `languagetool`; `tst_settings` covers both.
- `AppController` creates, configures and registers the service after `m_languageTool` and applies `[llm]`
  changes to it; `tst_proofreadingmanager`, `tst_languagetoolservice`, `tst_languagetoolparser`,
  `tst_proofreadingwindow`, `tst_proofreadingpopup` and `tst_settingsdialog` pass **without an edit** —
  the issue list, the preview, Replace/Ignore/Replace all and the popup are provider-independent.
- No new `tr()` and no `.ts` change: **383** messages in each of the 14 files, no `lupdate`/`lrelease`
  run. The only log category added is `qtranslate.service.llm-proofreading`, and its lines carry counts,
  the `droppedEdits` counter, timings and tokens only.
- Linux becomes **53** tests, Windows **47** (derived). Debug and Release build without warnings on the
  host and in `qtranslate-trixie`, **53/53** in both; Windows not performed and reviewed by diff.

## Risks

- **A clean answer must not become an error.** This is the failure the `02` Outcome was written for: an
  `{"edits":[]}` answer reported as `BadResponse` tells the user their check failed when the text is
  clean, and a `droppedEdits > 0` answer reported as "no issues" tells them the text is clean when the
  model did change something. Both cases need their own test row, not one shared "empty issues" branch.
- **A truncated JSON answer is not plain text.** Without the `parse()` gate it lands in `PlainText` and
  the diff proposes replacing the whole text with half an answer. Never call `parseResponse()` on a body
  that `OpenAiChat::parse()` rejected.
- **The offset shift is easy to forget and hard to see.** Every test for a leading-whitespace text must
  assert the shifted offset, not just the replacement text; a check on `"  foo"` with an edit at 0 would
  highlight the first two spaces.
- **Double-checking the same text.** A check cancels only the unfinished request of its channel; a window
  check and a popup check can run at once (`proofreadingmanager.cpp:130-193`). The service must therefore
  keep each HTTP reply on its own `ProofreadingReply` and never share state between replies.
- **The model answers 200 with prose.** The parser reports `Status::PlainText` and the diff keeps the
  result usable; reporting `BadResponse` for that case would throw away a usable correction.
- **A partial answer is a success, and that is deliberate.** When the model returns some usable edits and
  drops others, the check succeeds with the usable issues; the lost ones are visible only as the
  `droppedEdits` count in the log line. Turning every partial answer into `BadResponse` would throw away
  good corrections, and the alternative — a warning in the issue list — is UI work outside this task.
- **The stashed draft is not a base.** `stash@{0}` is unreviewed; `OpenAiChat` plus the working
  `OpenAiCompatibleService::Reply` is the reference, and the stashed code is not cherry-picked.
- **No key pre-check is deliberate.** `OpenAiCompatibleService::translate()` refuses a non-local host
  without a key; the checking provider does not, so a remote server answers `401` → `Auth`. The `Auth`
  title says "rejected the request", which reads slightly off for a missing key, and the fix belongs to
  the shared `ProofreadingManager`/`ProofreadingErrorText` mapping, not here.
- **`AppController` has no test target.** The registration and the `applySettings()` line are covered by
  the settings-dialog/provider-combo flow and by the manual run, not by a unit test — the same limitation
  `01-ai-tab` and `02` recorded.
- **A real model is still unverified.** The mock server proves the protocol, the mapping and
  cancellation; a live run with a real key or a local Ollama/llama.cpp server is the user's check and is
  recorded as not performed until then.

## Outcome

Done on 2026-11-15 (this commit, base `5dcc390`). `LlmProofreadingService` (`src/services/
llmproofreadingservice.{h,cpp}`) is the second checking provider: id `llm`, the untranslated
`displayName()` `LLM`, `languages()` from `Languages::supported("llm", Source)`, auto-detect
supported, `applySettings()` reading only `[llm]` with the `OpenAiCompatibleService::defaultBaseUrl()`/
`defaultEndpoint()` fallback, one Chat Completions request per check through `OpenAiChat`
(`temperature 0`, no `max_tokens`, `systemPrompt(request.language, request.categories)`, user message
equal to the trimmed text, referer/title as the translation service sends them), and a `Reply` shaped
like `OpenAiCompatibleService::Reply` with its own HTTP reply per check. `proofreadingProviderIds()`
lists `languagetool` and `llm`; `AppController` creates, configures and registers the service after
`m_languageTool`; `tst_llmproofreadingservice` (27 cases) and a new `tst_settings` slot cover it. No
new `tr()`, no `.ts` change, no edit under `src/ai`, `src/services/openaicompatibleservice.*`, `i18n`
or `doc/`.

The contract of `02` is honoured as the plan prescribed: `Edits` with no issues and `droppedEdits == 0`
is a successful check, `Edits` with no issues and `droppedEdits > 0` is `BadResponse`, `Malformed` is
`BadResponse`, `PlainText` is a success (zero issues included), and the truncation guard is
`OpenAiChat::parse()` alone — `finish_reason: "length"` becomes `TooLong` before the body is
interpreted, so a cut-off JSON answer never reaches the parser as plain text. Issues are shifted by
the length of the leading whitespace, and `result.correctedText` carries the whole corrected text for
the `PlainText` answer and stays empty for `Edits`.

Decisions taken where the plan was silent or where code disagreed with its letter:

1. **A public `QUrl endpoint() const` was added to the header** (comment: test seam). Step 4 requires
   `appliesSettings()` rows for `/api/v1` → `/api/v1/chat/completions`, the default host on an empty
   base URL and a full endpoint kept, and the interface of `## Task` exposes no way to read it; the
   accessor mirrors `LanguageToolService::endpoint()`. It reveals nothing secret (the endpoint is sent
   with every request) and changes no behaviour.
2. **Fixed messages: only `400` and `404` use the plan's wording**; the rest are short English literals
   in the LanguageTool style with the HTTP status in parentheses (`the provider reported a rate limit
   (HTTP 429)`), and the timeout reads `the provider did not respond within %1 s` with
   `NetworkManager::timeoutMs()`, as LanguageTool's does. None of them carries the text, the key, the
   URL or the body; `logHygiene()` and `verifyFixedMessage()` assert that.
3. **`configurationError()` is `the LLM provider is configured on the AI tab`** — the technical detail
   appended to the translated title, naming the tab the provider is configured on.
4. **The HTTP debug line also logs `QNetworkReply::NetworkError` next to the status**, exactly as
   `openaicompatibleservice.cpp:231-232` does: a numeric code, not a URL and not a body.
5. **The registration sits between `registerService(m_languageTool)` and `setActiveService(...)`.**
   The plan fixed the order relative to `m_languageTool` but not relative to `setActiveService`; the
   service has to be registered before the stored provider id is applied, or a saved `llm` would only
   be picked on the next start. LanguageTool is still registered first, so it stays the default for an
   unknown id.
6. **One comment-only fix outside the plan's ten files** (second review round): `src/core/
   proofreadingservice.h:59` said `// "languagetool" (stage 6 adds "llm")`; it now reads
   `// "languagetool" | "llm"`, like `settings.h:79`.

Tests. `tests/tst_llmproofreadingservice.cpp` (`QTEST_GUILESS_MAIN`, its own `FakeHttpServer`) has 16
slots and 27 cases: properties, `applySettings()`, the request (URL, model, `temperature` `0`, no
`max_tokens`, two messages, `Authorization` present with a key and absent without one,
`HTTP-Referer`/`X-Title`), structured edits with shifted offsets and the
`ProofreadingText::corrected()` invariant, three shapes of a clean answer, the plain-text fallback and
`correctedText`, `droppedEdits > 0` → `BadResponse`, a partial answer with usable issues → success,
`finish_reason: "length"` and `413` → `TooLong`, three malformed answers, the nine HTTP statuses, the
`200` error object and the `200` without `choices`, an unsupported language and an empty text (no
request sent), the transfer timeout, `abort()` → `Cancelled` with `finished()` firing once, and log
hygiene with the checked text and the key marked in the fixture. `tests/tst_settings.cpp` expects the
new provider list and has `proofreadingLlmProviderRoundTrip()`.

Verification: host `build/linux-debug` and `build/linux-release` — **53/53** each, `ctest -N` 53,
0 warnings under `-Wall -Wextra`; `qtranslate-trixie` (Qt 6.8.2) Release, `timeout 900 docker run …
timeout 420 xvfb-run -a ctest --timeout 45` — **53/53** in 27.9 s, exit 0. `.ts`: 383 `<message`
entries in each of the 14 files, no `lupdate`/`lrelease` run, `tr(` absent from the service. Windows
not built (no MSVC): 47 targets derived, no `WIN32` code touched, reviewed by diff. The container run
started on the first attempt this time; the `xvfb-run` flake of `01`/`02` did not reappear.

Review: two remarks accepted and fixed, both small — the stale `id()` comment above and a missing
partial-answer row (`partialAnswerIsASuccess()`: two edits, one unplaceable, `ok()` with one issue;
verified to fail if the service condition is narrowed to `droppedEdits > 0`). Declined as observations
and recorded here only: the issue list keeps the model's order (no sort as LanguageTool does; the
panel shows them in that order, `ProofreadingText::corrected()` and `applyIssueEdits()` sort
themselves); an empty `content` surfaces the transport's own literal `the response has no translation
text`, which reads oddly for a check but is what the plan asks for and belongs to the shared
`OpenAiChat` mapping; `droppedEdits` also counts a requested-category mismatch, so a model that
ignores the filter can turn a real answer into `BadResponse` — that is the parser's counter from `02`,
not this service; and there is no test with two concurrent replies, because the state of a request is
copied into `Reply` and the service holds no mutable state between checks.

Left open: the `AppController` wiring (creation, registration, `applySettings()`) has no test target,
as in `01` and `02`; a live provider was not exercised — the mock server proves the protocol, the
mapping and cancellation; one request per check without chunking means a text beyond the model context
is reported as `TooLong` rather than split. Documentation still to update after `04`: the service and
test lists in `doc/architecture.md` (lines 260, 319, 360, 362), §…:1716 "only languagetool in this
version", and `doc/handoff.md`.

**Superseded by the stage acceptance (`5210d43`).** The parser was changed during acceptance: the
edits dropped by the category filter and the unchanged replacements moved to the new
`Parsed::filteredEdits`, so `droppedEdits` now means "could not be placed in the text" only. The
condition in this service stayed `issues.isEmpty() && droppedEdits > 0` → `BadResponse`, which is why
the contract above still reads correctly; a category-filtered answer is now a clean check.
