#include "services/mozhiparser.h"

#include "core/languages.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <initializer_list>

namespace {

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

// The messages are the plain-text error bodies of the instance, matched
// case-insensitively. The order matters: a rate-limit text can also mention a
// status code, and the engine text must not win over a language text.
TranslationError errorForText(const QString &text)
{
    if (containsAny(text, {QLatin1String("rate limit"), QLatin1String("too many requests"),
                           QLatin1String("status code: 429"), QLatin1String("status code: 403")})) {
        return TranslationError::RateLimited;
    }
    if (containsAny(text, {QLatin1String("language code invalid"), QLatin1String("language pair"),
                           QLatin1String("invalid language"),
                           QLatin1String("unsupported language")})) {
        return TranslationError::UnsupportedLanguage;
    }
    if (containsAny(text, {QLatin1String("engine does not exist"),
                           QLatin1String("engine has been disabled")})
        || (text.contains(QLatin1String("engine"), Qt::CaseInsensitive)
            && text.contains(QLatin1String("disabled"), Qt::CaseInsensitive))) {
        return TranslationError::ServiceUnavailable;
    }
    if (text.contains(QLatin1String("required query strings"), Qt::CaseInsensitive))
        return TranslationError::BadResponse;
    return TranslationError::None;
}

QString messageFor(const TranslationError error)
{
    switch (error) {
    case TranslationError::RateLimited:
        return QStringLiteral("the endpoint reported a rate limit");
    case TranslationError::UnsupportedLanguage:
        return QStringLiteral("the language pair is not supported");
    case TranslationError::ServiceUnavailable:
        return QStringLiteral("the selected engine is not available on this instance");
    case TranslationError::BadResponse:
        return QStringLiteral("the endpoint rejected the request parameters");
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

namespace MozhiParser {

Response parseResponse(const QByteArray &body)
{
    Response response;
    const TranslationResult error = parseErrorBody(body);
    if (!error.ok()) {
        response.result = error;
        return response;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        response.result = errorResult(TranslationError::BadResponse,
                                      QStringLiteral("no translated text in the response"));
        return response;
    }
    const QJsonObject root = doc.object();

    const QJsonValue translatedValue = root.value(QStringLiteral("translated-text"));
    const QString translated = translatedValue.isString() ? translatedValue.toString() : QString();
    if (translated.trimmed().isEmpty()) {
        response.result = errorResult(TranslationError::BadResponse,
                                      QStringLiteral("no translated text in the response"));
        return response;
    }

    const QString detected = stringField(root, QLatin1String("detected"));
    const QString sourceLanguage = stringField(root, QLatin1String("source_language"));
    response.sourceLanguage = sourceLanguage;
    response.targetTransliteration = stringField(root, QLatin1String("target_transliteration"));

    TranslationResult result;
    result.translatedText = translated;
    result.detectedSourceLang =
        Languages::fromServiceCode(QStringLiteral("mozhi"), detected, Languages::Direction::Source);
    if (result.detectedSourceLang.isEmpty() && !sourceLanguage.trimmed().isEmpty()
        && sourceLanguage.trimmed() != QStringLiteral("auto")) {
        result.detectedSourceLang = Languages::fromServiceCode(QStringLiteral("mozhi"), sourceLanguage,
                                                               Languages::Direction::Source);
    }
    response.result = result;
    return response;
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
        const QJsonValue errorValue = doc.object().value(QStringLiteral("error"));
        QString errorText;
        if (errorValue.isString()) {
            errorText = errorValue.toString();
        } else if (errorValue.isObject()) {
            errorText = errorValue.toObject().value(QStringLiteral("message")).toString();
        }
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
    case 414:
        return TranslationError::TooLong;
    default:
        break;
    }
    if (status >= 500 && status < 600)
        return TranslationError::Network;
    return TranslationError::BadResponse;
}

} // namespace MozhiParser
