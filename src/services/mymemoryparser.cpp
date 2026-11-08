#include "services/mymemoryparser.h"

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

// The details are classified before the generic non-200 check: MyMemory sends
// a useful text with both HTTP 200 and an error status, and the details can
// repeat the query, so they must never be echoed to the user.
TranslationError errorForDetails(const QString &details)
{
    if (details.isEmpty())
        return TranslationError::None;
    if (containsAny(details, {QLatin1String("QUERY LENGTH"), QLatin1String("MAX ALLOWED QUERY"),
                              QLatin1String("QUERY TOO LONG")})) {
        return TranslationError::TooLong;
    }
    if (containsAny(details, {QLatin1String("INVALID LANGUAGE"), QLatin1String("LANGUAGE PAIR"),
                              QLatin1String("DISTINCT LANGUAGES")})) {
        return TranslationError::UnsupportedLanguage;
    }
    if (containsAny(details, {QLatin1String("QUOTA"), QLatin1String("FREE TRANSLATIONS"),
                              QLatin1String("DAILY LIMIT")})) {
        return TranslationError::QuotaExceeded;
    }
    if (containsAny(details, {QLatin1String("TOO MANY REQUESTS"),
                              QLatin1String("RATE LIMIT")})) {
        return TranslationError::RateLimited;
    }
    return TranslationError::None;
}

} // namespace

namespace MyMemoryParser {

TranslationResult parseResponse(const QByteArray &body)
{
    if (body.trimmed().startsWith('<'))
        return errorResult(TranslationError::ServiceUnavailable,
                           QStringLiteral("the endpoint returned an HTML page"));

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return errorResult(TranslationError::BadResponse,
                           QStringLiteral("response is not a JSON object"));
    const QJsonObject root = doc.object();

    int status = 200;
    const QJsonValue statusValue = root.value(QStringLiteral("responseStatus"));
    if (!statusValue.isUndefined()) {
        if (statusValue.isDouble()) {
            status = statusValue.toInt();
        } else if (statusValue.isString()) {
            bool ok = false;
            status = statusValue.toString().trimmed().toInt(&ok);
            if (!ok || status <= 0)
                return errorResult(TranslationError::BadResponse,
                                   QStringLiteral("responseStatus is not a number"));
        } else {
            return errorResult(TranslationError::BadResponse,
                               QStringLiteral("responseStatus is not a number"));
        }
    }

    const QJsonValue quotaValue = root.value(QStringLiteral("quotaFinished"));
    bool quotaFinished = false;
    if (quotaValue.isBool())
        quotaFinished = quotaValue.toBool();
    else if (quotaValue.isString())
        quotaFinished = quotaValue.toString().trimmed().compare(QLatin1String("true"),
                                                                Qt::CaseInsensitive)
            == 0;
    if (quotaFinished)
        return errorResult(TranslationError::QuotaExceeded,
                           QStringLiteral("the MyMemory quota is exhausted"));

    const QString details = root.value(QStringLiteral("responseDetails")).toString();
    switch (errorForDetails(details)) {
    case TranslationError::TooLong:
        return errorResult(TranslationError::TooLong,
                           QStringLiteral("the text is too long for MyMemory"));
    case TranslationError::UnsupportedLanguage:
        return errorResult(TranslationError::UnsupportedLanguage,
                           QStringLiteral("the language pair is not supported"));
    case TranslationError::QuotaExceeded:
        return errorResult(TranslationError::QuotaExceeded,
                           QStringLiteral("the MyMemory quota is exhausted"));
    case TranslationError::RateLimited:
        return errorResult(TranslationError::RateLimited,
                           QStringLiteral("the endpoint reported a rate limit"));
    default:
        break;
    }

    if (status != 200)
        return errorResult(TranslationError::BadResponse,
                           QStringLiteral("the endpoint returned an unexpected status"));

    const QJsonObject data = root.value(QStringLiteral("responseData")).toObject();
    const QJsonValue translatedValue = data.value(QStringLiteral("translatedText"));
    const QString translated = translatedValue.isString() ? translatedValue.toString() : QString();
    if (translated.trimmed().isEmpty())
        return errorResult(TranslationError::BadResponse,
                           QStringLiteral("no translated text in the response"));

    TranslationResult result;
    result.translatedText = translated;
    result.detectedSourceLang =
        Languages::fromServiceCode(QStringLiteral("mymemory"),
                                   data.value(QStringLiteral("detectedLanguage")).toString(),
                                   Languages::Direction::Source);
    return result;
}

TranslationError errorForHttpStatus(int status)
{
    switch (status) {
    case 403: // the endpoint blocks clients it considers abusive with 403 or 429
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

} // namespace MyMemoryParser
