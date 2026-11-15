#include "services/libretranslateparser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace {

const QString kServiceId = QStringLiteral("libretranslate");

TranslationResult errorResult(TranslationError error, const QString &message)
{
    TranslationResult result;
    result.error = error;
    result.errorMessage = message;
    return result;
}

TranslationResult badResponse()
{
    return errorResult(TranslationError::BadResponse,
                       QStringLiteral("no translated text in the response"));
}

// The recorded `error` sentences of the server, matched case-insensitively. The
// order matters: "Invalid request: request (6000) exceeds text limit (5000)" is
// a size error, not the generic rejected request, and a key-protected instance
// answers 400 rather than 401 with a key message.
TranslationError errorForText(const QString &text)
{
    if (text.contains(QLatin1String("slowdown"), Qt::CaseInsensitive))
        return TranslationError::RateLimited;
    if (text.contains(QLatin1String("exceeds text limit"), Qt::CaseInsensitive))
        return TranslationError::TooLong;
    if (text.contains(QLatin1String("is not supported"), Qt::CaseInsensitive))
        return TranslationError::UnsupportedLanguage;
    if (text.contains(QLatin1String("api key"), Qt::CaseInsensitive))
        return TranslationError::Auth;
    if (text.startsWith(QLatin1String("invalid request"), Qt::CaseInsensitive))
        return TranslationError::BadResponse;
    return TranslationError::ServiceUnavailable;
}

QString messageFor(const TranslationError error)
{
    switch (error) {
    case TranslationError::RateLimited:
        return QStringLiteral("the endpoint reported a rate limit");
    case TranslationError::TooLong:
        return QStringLiteral("the request is too long for the endpoint");
    case TranslationError::UnsupportedLanguage:
        return QStringLiteral("the endpoint rejected the language");
    case TranslationError::Auth:
        return QStringLiteral("the endpoint rejected the API key");
    case TranslationError::BadResponse:
        return QStringLiteral("the endpoint rejected the request");
    default:
        break;
    }
    return QStringLiteral("the endpoint returned an error");
}

} // namespace

namespace LibreTranslateParser {

TranslationResult parseResponse(const QByteArray &body)
{
    // A proxy or a misconfigured server may answer 200 with an error object, so
    // an error body is never a translation.
    const TranslationResult error = parseErrorBody(body);
    if (!error.ok())
        return error;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return badResponse();

    const QJsonObject object = doc.object();
    const QJsonValue translated = object.value(QStringLiteral("translatedText"));
    if (!translated.isString() || translated.toString().trimmed().isEmpty())
        return badResponse();

    TranslationResult result;
    result.translatedText = translated.toString();
    // detectedLanguage is present only for `source: "auto"`; it may be null, and
    // the confidence it carries has no field in TranslationResult.
    const QJsonValue detected = object.value(QStringLiteral("detectedLanguage"));
    if (detected.isObject()) {
        const QJsonValue language = detected.toObject().value(QStringLiteral("language"));
        if (language.isString()) {
            result.detectedSourceLang = Languages::fromServiceCode(
                kServiceId, language.toString(), Languages::Direction::Source);
        }
    }
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
        const QJsonValue error = doc.object().value(QStringLiteral("error"));
        if (!error.isString())
            return TranslationResult{};
        const TranslationError kind = errorForText(error.toString());
        return errorResult(kind, messageFor(kind));
    }

    // The endpoint has no plain-text error shape: a JSON array, a JSON object
    // without `error` and any unrecognised text carry no classification.
    return TranslationResult{};
}

TranslationError errorForHttpStatus(int status)
{
    switch (status) {
    case 401:
    case 403:
        return TranslationError::Auth;
    case 429:
        return TranslationError::RateLimited;
    case 413:
    case 414:
        return TranslationError::TooLong;
    default:
        break;
    }
    if (status >= 500 && status < 600)
        return TranslationError::Network;
    return TranslationError::BadResponse;
}

} // namespace LibreTranslateParser
