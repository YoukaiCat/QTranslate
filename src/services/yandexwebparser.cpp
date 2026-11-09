#include "services/yandexwebparser.h"

#include "core/languages.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <initializer_list>

namespace {

const QString kServiceId = QStringLiteral("yandex-web");

TranslationResult errorResult(TranslationError error, const QString &message)
{
    TranslationResult result;
    result.error = error;
    result.errorMessage = message;
    return result;
}

bool containsAny(const QString &text, std::initializer_list<QLatin1String> needles)
{
    for (const QLatin1String &needle : needles) {
        if (text.contains(needle, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

// The error texts of the endpoint, matched case-insensitively. A rate-limit
// text comes first because a block page can also mention the request rate.
TranslationError errorForText(const QString &text)
{
    if (containsAny(text, {QLatin1String("rate limit"), QLatin1String("too many requests"),
                           QLatin1String("quota"), QLatin1String("status code: 429"),
                           QLatin1String("status code: 403")})) {
        return TranslationError::RateLimited;
    }
    if (containsAny(text, {QLatin1String("captcha"), QLatin1String("blocked"),
                           QLatin1String("forbidden"), QLatin1String("access denied"),
                           QLatin1String("banned"), QLatin1String("robot"),
                           QLatin1String("unavailable")})) {
        return TranslationError::ServiceUnavailable;
    }
    return TranslationError::None;
}

QString messageFor(const TranslationError error)
{
    switch (error) {
    case TranslationError::RateLimited:
        return QStringLiteral("the endpoint reported a rate limit");
    case TranslationError::ServiceUnavailable:
        return QStringLiteral("the endpoint is blocked or unavailable");
    default:
        break;
    }
    return QString();
}

QString stringField(const QJsonObject &object, QLatin1String key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

} // namespace

namespace YandexWebParser {

TranslationResult parseResponse(const QByteArray &body)
{
    const TranslationResult error = parseErrorBody(body);
    if (!error.ok())
        return error;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        return errorResult(TranslationError::BadResponse,
                           QStringLiteral("no translated text in the response"));
    }
    const QJsonArray array = doc.array();
    if (array.isEmpty() || !array.first().isObject()) {
        return errorResult(TranslationError::BadResponse,
                           QStringLiteral("no translated text in the response"));
    }

    const QJsonObject first = array.first().toObject();
    const QString translated = stringField(first, QLatin1String("text"));
    if (translated.trimmed().isEmpty()) {
        return errorResult(TranslationError::BadResponse,
                           QStringLiteral("no translated text in the response"));
    }

    // Only `text` and the detected `from` language are used; `to` and any other
    // fields are read only to tolerate them.
    TranslationResult result;
    result.translatedText = translated;
    result.detectedSourceLang = Languages::fromServiceCode(
        kServiceId, stringField(first, QLatin1String("from")), Languages::Direction::Source);
    return result;
}

TranslationResult parseErrorBody(const QByteArray &body)
{
    const QByteArray trimmed = body.trimmed();
    if (trimmed.startsWith('<')) {
        return errorResult(TranslationError::ServiceUnavailable,
                           QStringLiteral("the endpoint returned an HTML page"));
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject root = doc.object();
        const QJsonValue errorValue = root.value(QStringLiteral("error"));
        QString errorText;
        if (errorValue.isString()) {
            errorText = errorValue.toString();
        } else if (errorValue.isObject()) {
            errorText = errorValue.toObject().value(QStringLiteral("message")).toString();
        }
        if (errorText.trimmed().isEmpty())
            errorText = stringField(root, QLatin1String("message"));
        if (errorText.trimmed().isEmpty())
            return TranslationResult{};

        const TranslationError error = errorForText(errorText);
        if (error == TranslationError::None) {
            return errorResult(TranslationError::ServiceUnavailable,
                               QStringLiteral("the endpoint returned an error"));
        }
        return errorResult(error, messageFor(error));
    }

    // An empty, malformed or non-object JSON body has no error to classify; a
    // body that starts with '{' or '[' is never treated as plain text.
    if (trimmed.isEmpty() || trimmed.startsWith('{') || trimmed.startsWith('['))
        return TranslationResult{};

    const TranslationError error = errorForText(QString::fromUtf8(trimmed));
    if (error == TranslationError::None)
        return TranslationResult{};
    return errorResult(error, messageFor(error));
}

TranslationError errorForHttpStatus(int status)
{
    switch (status) {
    case 403: // a blocked client or a rate limit
    case 429:
        return TranslationError::RateLimited;
    case 413:
        return TranslationError::TooLong;
    default:
        break;
    }
    if (status >= 500 && status < 600)
        return TranslationError::Network;
    return TranslationError::BadResponse;
}

} // namespace YandexWebParser
