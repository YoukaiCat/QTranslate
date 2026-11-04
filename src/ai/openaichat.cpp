#include "ai/openaichat.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>

namespace OpenAiChat {

namespace {

QString contentText(const QJsonValue &content)
{
    if (content.isString())
        return content.toString();

    if (!content.isArray())
        return {};

    // OpenAI-compatible providers may return content as an array of parts.
    QString text;
    for (const QJsonValue &part : content.toArray()) {
        if (part.isString()) {
            text += part.toString();
            continue;
        }
        if (!part.isObject())
            continue;
        const QJsonObject object = part.toObject();
        if (object.value(QStringLiteral("type")).toString() == QStringLiteral("text")
            || object.contains(QStringLiteral("text"))) {
            text += object.value(QStringLiteral("text")).toString();
        }
    }
    return text;
}

QUrl endpointUrl(const QString &baseUrl, const QString &endpoint)
{
    if (baseUrl.trimmed().isEmpty())
        return {};

    QUrl url(baseUrl);
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);

    if (path.endsWith(QLatin1Char('/') + endpoint)) {
        url.setPath(path);
        return url;
    }

    if (!path.endsWith(QLatin1String("/v1"))) {
        if (path.isEmpty())
            path = QStringLiteral("/v1");
        else
            path += QStringLiteral("/v1");
    }
    path += QLatin1Char('/') + endpoint;
    url.setPath(path);
    return url;
}

} // namespace

QByteArray serialize(const Request &request)
{
    QJsonArray messages;
    for (const Message &message : request.messages) {
        QJsonObject item;
        item.insert(QStringLiteral("role"), message.role);
        item.insert(QStringLiteral("content"), message.content);
        messages.append(item);
    }

    QJsonObject object;
    object.insert(QStringLiteral("model"), request.model);
    object.insert(QStringLiteral("messages"), messages);
    object.insert(QStringLiteral("temperature"), request.temperature);
    object.insert(QStringLiteral("stream"), false);
    if (request.maxTokens > 0)
        object.insert(QStringLiteral("max_tokens"), request.maxTokens);

    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

Response parse(const QByteArray &body)
{
    Response response;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        response.error = TranslationError::BadResponse;
        response.errorMessage = QStringLiteral("the response is not a JSON object");
        return response;
    }

    const QJsonObject root = document.object();
    if (root.contains(QStringLiteral("error"))) {
        const QJsonValue error = root.value(QStringLiteral("error"));
        if (error.isObject()) {
            const QJsonObject errorObject = error.toObject();
            response.errorCode = errorObject.value(QStringLiteral("code")).toInt();
        }
        response.error = response.errorCode > 0
            ? errorForHttpStatus(response.errorCode)
            : TranslationError::BadResponse;
        response.errorMessage = QStringLiteral("the service reported an error");
        return response;
    }

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty() || !choices.first().isObject()) {
        response.error = TranslationError::BadResponse;
        response.errorMessage = QStringLiteral("the response has no choices");
        return response;
    }

    const QJsonObject choice = choices.first().toObject();
    response.finishReason = choice.value(QStringLiteral("finish_reason")).toString();
    const QJsonValue message = choice.value(QStringLiteral("message"));
    if (!message.isObject()) {
        response.error = TranslationError::BadResponse;
        response.errorMessage = QStringLiteral("the response has no message");
        return response;
    }

    response.text = contentText(message.toObject().value(QStringLiteral("content")));
    response.model = root.value(QStringLiteral("model")).toString();

    const QJsonObject usage = root.value(QStringLiteral("usage")).toObject();
    response.promptTokens = usage.value(QStringLiteral("prompt_tokens")).toInt();
    response.completionTokens = usage.value(QStringLiteral("completion_tokens")).toInt();

    if (response.finishReason == QLatin1String("length")) {
        response.error = TranslationError::TooLong;
        response.errorMessage = QStringLiteral("the model stopped at the output token limit");
        return response;
    }
    if (response.finishReason == QLatin1String("content_filter")) {
        response.error = TranslationError::BadResponse;
        response.errorMessage = QStringLiteral("the model refused to process the text");
        return response;
    }
    if (response.finishReason == QLatin1String("error")) {
        response.error = TranslationError::BadResponse;
        response.errorMessage = QStringLiteral("the model reported an error");
        return response;
    }
    if (response.text.trimmed().isEmpty()) {
        response.error = TranslationError::BadResponse;
        response.errorMessage = QStringLiteral("the response has no translation text");
        return response;
    }

    return response;
}

TranslationError errorForHttpStatus(int status)
{
    switch (status) {
    case 401:
    case 403:
        return TranslationError::Auth;
    case 402:
        return TranslationError::QuotaExceeded;
    case 408:
        return TranslationError::Timeout;
    case 413:
        return TranslationError::TooLong;
    case 429:
        return TranslationError::RateLimited;
    default:
        break;
    }
    if (status >= 500)
        return TranslationError::Network;
    return TranslationError::BadResponse;
}

QUrl chatCompletionsUrl(const QString &baseUrl)
{
    return endpointUrl(baseUrl, QStringLiteral("chat/completions"));
}

QUrl modelsUrl(const QString &baseUrl)
{
    return endpointUrl(baseUrl, QStringLiteral("models"));
}

QStringList parseModels(const QByteArray &body, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("the response is not a JSON object");
        return {};
    }

    const QJsonArray data = document.object().value(QStringLiteral("data")).toArray();
    if (data.isEmpty()) {
        if (error)
            *error = QStringLiteral("the response has no models");
        return {};
    }

    QStringList models;
    QSet<QString> seen;
    for (const QJsonValue &value : data) {
        if (!value.isObject())
            continue;
        const QString id = value.toObject().value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty() || seen.contains(id))
            continue;
        seen.insert(id);
        models.append(id);
    }

    models.sort(Qt::CaseInsensitive);
    if (models.isEmpty() && error)
        *error = QStringLiteral("the response has no model identifiers");
    return models;
}

} // namespace OpenAiChat
