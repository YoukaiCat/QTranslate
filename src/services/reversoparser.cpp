#include "services/reversoparser.h"

#include "core/languages.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <initializer_list>

namespace {

const QString kServiceId = QStringLiteral("reverso");

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

bool containsAny(const QString &text, std::initializer_list<QLatin1String> needles)
{
    for (const QLatin1String &needle : needles) {
        if (text.contains(needle, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

// The provider texts that mean a rate limit; "Too many requests", "rate limit",
// "rate-limit" and "ratelimit" all reduce to one fixed message.
bool isRateLimitText(const QString &text)
{
    return containsAny(text, {QLatin1String("too many"), QLatin1String("rate limit"),
                              QLatin1String("rate-limit"), QLatin1String("ratelimit"),
                              QLatin1String("quota")});
}

bool isControlCharacter(QChar character)
{
    return character.category() == QChar::Other_Control;
}

// The provider message is echoed only when it is a short plain sentence: no
// markup, no URL and no control character. Anything else is replaced by the
// fixed literal, so an HTML page or a URL can never reach the user.
bool isValidProviderMessage(const QString &message)
{
    if (message.isEmpty() || message.size() > 200)
        return false;
    if (message.contains(QLatin1Char('<')) || message.contains(QLatin1Char('>'))
        || message.contains(QLatin1Char('{')) || message.contains(QLatin1Char('}'))
        || message.contains(QStringLiteral("://"))) {
        return false;
    }
    for (const QChar character : message) {
        if (isControlCharacter(character))
            return false;
    }
    return true;
}

// Builds the error of a provider object. The caller decides whether the object
// carries an error; this function only applies the message rules.
TranslationResult providerErrorResult(const QJsonObject &root)
{
    const QString message = root.value(QStringLiteral("message")).toString().trimmed();
    if (!isValidProviderMessage(message)) {
        return errorResult(TranslationError::ServiceUnavailable,
                           QStringLiteral("the endpoint reported an error"));
    }
    if (isRateLimitText(message)) {
        return errorResult(TranslationError::RateLimited,
                           QStringLiteral("the endpoint reported a rate limit"));
    }
    return errorResult(TranslationError::ServiceUnavailable, message);
}

// A provider object is an error when `error` is true or `success` is a boolean
// false. An object with neither flag is a normal response.
TranslationResult parseProviderBody(const QJsonObject &root)
{
    const QJsonValue errorValue = root.value(QStringLiteral("error"));
    const QJsonValue successValue = root.value(QStringLiteral("success"));
    const bool errorFlag = errorValue.isBool() && errorValue.toBool();
    const bool falseSuccess = successValue.isBool() && !successValue.toBool();
    if (!errorFlag && !falseSuccess)
        return TranslationResult{};
    return providerErrorResult(root);
}

QString stringField(const QJsonObject &object, QLatin1String key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

} // namespace

namespace ReversoParser {

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
                                      QStringLiteral("the response is not valid JSON"));
        return response;
    }
    const QJsonObject root = doc.object();

    const QJsonValue successValue = root.value(QStringLiteral("success"));
    if (!successValue.isBool()) {
        response.result = errorResult(TranslationError::BadResponse,
                                      QStringLiteral("the response has no success flag"));
        return response;
    }
    if (!successValue.toBool()) {
        // parseErrorBody() already classifies a `success: false` body; this
        // keeps an inconsistent object out of the success path.
        response.result = providerErrorResult(root);
        return response;
    }

    const QJsonValue translationValue = root.value(QStringLiteral("translation"));
    if (!translationValue.isString() || translationValue.toString().trimmed().isEmpty()) {
        response.result = badResponse();
        return response;
    }

    // The raw direction fields are exposed for the service and the tests; only
    // `directionFrom` fills the detected source language. A missing or
    // non-string direction is not a failure.
    response.directionFrom = stringField(root, QLatin1String("directionFrom"));
    response.directionTo = stringField(root, QLatin1String("directionTo"));

    TranslationResult result;
    result.translatedText = translationValue.toString();
    result.detectedSourceLang = Languages::fromServiceCode(
        kServiceId, response.directionFrom, Languages::Direction::Source);
    response.result = result;
    return response;
}

TranslationResult parseErrorBody(const QByteArray &body)
{
    const QByteArray trimmed = body.trimmed();
    if (trimmed.isEmpty())
        return TranslationResult{};
    if (trimmed.startsWith('<')) {
        return errorResult(TranslationError::ServiceUnavailable,
                           QStringLiteral("the endpoint returned an HTML page"));
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error == QJsonParseError::NoError) {
        // An object is a provider body; an array, a string or any other root
        // type carries no error to classify.
        return doc.isObject() ? parseProviderBody(doc.object()) : TranslationResult{};
    }

    // A malformed JSON body and a body that starts with a JSON-significant
    // character are never treated as plain text.
    if (trimmed.startsWith('{') || trimmed.startsWith('[') || trimmed.startsWith('"'))
        return TranslationResult{};

    const QString text = QString::fromUtf8(trimmed);
    if (isRateLimitText(text)) {
        return errorResult(TranslationError::RateLimited,
                           QStringLiteral("the endpoint reported a rate limit"));
    }
    if (containsAny(text, {QLatin1String("captcha"), QLatin1String("blocked"),
                           QLatin1String("forbidden"), QLatin1String("access denied"),
                           QLatin1String("banned"), QLatin1String("robot"),
                           QLatin1String("unavailable")})) {
        return errorResult(TranslationError::ServiceUnavailable,
                           QStringLiteral("the endpoint is blocked or unavailable"));
    }
    return TranslationResult{};
}

TranslationError errorForHttpStatus(int status)
{
    switch (status) {
    case 401:
    case 403:
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

} // namespace ReversoParser
