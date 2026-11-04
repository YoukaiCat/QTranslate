#include "services/deeplparser.h"

#include "core/languages.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {

TranslationResult badResponse(const QString &reason)
{
    TranslationResult result;
    result.error = TranslationError::BadResponse;
    result.errorMessage = reason;
    return result;
}

} // namespace

namespace DeepLParser {

TranslationResult parseResponse(const QByteArray &body)
{
    QStringList texts;
    QString detected;
    QString error;
    if (!parseTranslations(body, &texts, &detected, &error))
        return badResponse(error);
    if (texts.isEmpty())
        return badResponse(QStringLiteral("no translated text in response"));

    TranslationResult result;
    for (const QString &text : texts)
        result.translatedText += text;
    result.detectedSourceLang = detected;
    return result;
}

bool parseTranslations(const QByteArray &body, QStringList *texts,
                       QString *detectedSourceLang, QString *errorMessage)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("response is not a JSON object");
        return false;
    }

    const QJsonValue translations = doc.object().value(QStringLiteral("translations"));
    if (!translations.isArray()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("no translations in response");
        return false;
    }

    QStringList result;
    QString detected;
    for (const QJsonValue &item : translations.toArray()) {
        const QJsonObject translation = item.toObject();
        const QJsonValue text = translation.value(QStringLiteral("text"));
        if (!text.isString())
            continue;
        result << text.toString();
        if (detected.isEmpty()) {
            const QString code =
                translation.value(QStringLiteral("detected_source_language")).toString();
            detected = Languages::fromServiceCode(QStringLiteral("deepl"), code,
                                                  Languages::Direction::Source);
        }
    }
    if (result.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("no translated text in response");
        return false;
    }

    if (texts)
        *texts = result;
    if (detectedSourceLang)
        *detectedSourceLang = detected;
    return true;
}

bool parseUsage(const QByteArray &body, int *characterCount, int *characterLimit)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject object = doc.object();
    const QJsonValue count = object.value(QStringLiteral("character_count"));
    const QJsonValue limit = object.value(QStringLiteral("character_limit"));
    if (!count.isDouble() || !limit.isDouble())
        return false;

    if (characterCount)
        *characterCount = count.toInt();
    if (characterLimit)
        *characterLimit = limit.toInt();
    return true;
}

TranslationError errorForHttpStatus(int status)
{
    switch (status) {
    case 403:
        return TranslationError::Auth;
    case 456:
        return TranslationError::QuotaExceeded;
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

} // namespace DeepLParser
