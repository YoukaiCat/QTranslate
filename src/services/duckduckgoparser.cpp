#include "services/duckduckgoparser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <initializer_list>

namespace {

const QString kServiceId = QStringLiteral("duckduckgo");

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

// The fixed block words of a plain-text body, matched case-insensitively. A
// rate-limit text comes first because a block page can also mention the
// request rate.
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

// The scraped token is pasted into a request URL; a control character means the
// page or a proxy is mangled, and the value must not be used at all.
bool hasControlCharacter(const QString &value)
{
    for (const QChar character : value) {
        if (character.unicode() < 0x20)
            return true;
    }
    return false;
}

} // namespace

namespace DuckDuckGoParser {

bool tokenFromPage(const QByteArray &html, QString *token, QString *errorMessage)
{
    const auto fail = [errorMessage]() {
        if (errorMessage)
            *errorMessage = QStringLiteral("the token page has no usable vqd token");
        return false;
    };

    // Only the first quoted marker: the page carries unquoted `vqd=` mentions
    // as well, and an unquoted value is not a token.
    const QByteArray marker("vqd=\"");
    const qsizetype start = html.indexOf(marker);
    if (start < 0)
        return fail();
    const qsizetype begin = start + marker.size();
    const qsizetype end = html.indexOf('"', begin);
    if (end < 0)
        return fail();

    const QString value = QString::fromLatin1(html.mid(begin, end - begin));
    // The control character is checked before the trim: trimming a newline away
    // would hide the very damage that makes the token unusable.
    if (hasControlCharacter(value))
        return fail();
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return fail();

    if (token)
        *token = trimmed;
    return true;
}

TranslationResult parseResponse(const QByteArray &body)
{
    // The endpoint reports its errors under HTTP 200 as well, so an error body
    // is never a translation.
    const TranslationResult error = parseErrorBody(body);
    if (!error.ok())
        return error;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return badResponse();

    const QJsonObject object = doc.object();
    const QJsonValue translated = object.value(QStringLiteral("translated"));
    if (!translated.isString() || translated.toString().trimmed().isEmpty())
        return badResponse();

    TranslationResult result;
    result.translatedText = translated.toString();
    // detected_language is null or absent when the request carried an explicit
    // source, so only a string is mapped.
    const QJsonValue detected = object.value(QStringLiteral("detected_language"));
    if (detected.isString()) {
        result.detectedSourceLang = Languages::fromServiceCode(
            kServiceId, detected.toString(), Languages::Direction::Source);
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

        // The two codes the research recorded, compared exactly and without
        // case: a new code must not be mistaken for either of them.
        const QString code = error.toString();
        if (code.compare(QLatin1String("invalid_text"), Qt::CaseInsensitive) == 0) {
            return errorResult(TranslationError::TooLong,
                               QStringLiteral("the request is too long for the endpoint"));
        }
        if (code.compare(QLatin1String("invalid_to"), Qt::CaseInsensitive) == 0) {
            return errorResult(TranslationError::UnsupportedLanguage,
                               QStringLiteral("the endpoint rejected the language"));
        }
        // `Forbidden`, a `vqd` complaint and any later code are the same block
        // for the caller.
        return errorResult(TranslationError::ServiceUnavailable,
                           QStringLiteral("the endpoint returned an error"));
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

} // namespace DuckDuckGoParser
