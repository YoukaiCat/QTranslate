#pragma once

#include "core/translationtypes.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

// Minimal OpenAI-compatible Chat Completions transport, shared by the
// OpenAI-compatible translation service and the future AI actions
// (see doc/llm-actions.md, §5). It knows nothing about QNetwork*: callers
// send the serialized request and parse the response.
namespace OpenAiChat {

struct Message
{
    QString role; // "system", "user", "assistant"
    QString content;
};

struct Request
{
    QUrl endpoint; // full .../chat/completions URL
    QString apiKey; // optional: local servers may not need one
    QString model;
    QList<Message> messages;
    double temperature = 0.2;
    int maxTokens = 0; // 0: do not send max_tokens
    QString referer;   // optional OpenRouter attribution
    QString title;     // optional OpenRouter attribution
};

struct Response
{
    QString text;
    QString model;
    QString finishReason;
    int promptTokens = 0;
    int completionTokens = 0;
    int errorCode = 0; // status-like code reported inside a 200 response
    TranslationError error = TranslationError::None;
    QString errorMessage;

    bool ok() const { return error == TranslationError::None; }
};

QByteArray serialize(const Request &request);
Response parse(const QByteArray &body);

// Maps HTTP/API status codes to the error kinds understood by the UI.
TranslationError errorForHttpStatus(int status);

// Builds .../v1/chat/completions and .../v1/models from a base URL such as
// https://openrouter.ai/api/v1 or http://127.0.0.1:11434. A full endpoint
// URL is returned unchanged.
QUrl chatCompletionsUrl(const QString &baseUrl);
QUrl modelsUrl(const QString &baseUrl);

// Parses the {"data":[{"id":...}]} model list. Returns sorted unique IDs.
QStringList parseModels(const QByteArray &body, QString *error = nullptr);

} // namespace OpenAiChat
