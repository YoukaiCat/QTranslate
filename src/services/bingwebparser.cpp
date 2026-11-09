#include "services/bingwebparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <initializer_list>

namespace {

const QString kServiceId = QStringLiteral("bing-web");

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

// statusCode arrives as a JSON number or as a string; both are reduced to the
// trimmed digits so the classification has one path.
QString statusCodeText(const QJsonObject &object)
{
    const QJsonValue value = object.value(QStringLiteral("statusCode"));
    if (value.isDouble())
        return QString::number(static_cast<qint64>(value.toDouble()));
    if (value.isString())
        return value.toString().trimmed();
    return {};
}

bool isAsciiWhitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// The bytes between `marker` and the next double quote. False when the marker or
// the closing quote is absent, so a truncated page fails instead of reading out
// of bounds.
bool quotedValueAfter(const QByteArray &html, const QByteArray &marker, QString *value)
{
    const qsizetype start = html.indexOf(marker);
    if (start < 0)
        return false;
    const qsizetype begin = start + marker.size();
    const qsizetype end = html.indexOf('"', begin);
    if (end < 0)
        return false;
    *value = QString::fromLatin1(html.mid(begin, end - begin)).trimmed();
    return !value->isEmpty();
}

// An optional `"<name>":"<value>"` field somewhere in the page. The service may
// get MUID from Set-Cookie instead, so an absent or unquoted value is an empty
// string rather than a failure.
QString pageJsonField(const QByteArray &html, const QByteArray &name)
{
    const QByteArray marker = '"' + name + "\":";
    qsizetype pos = html.indexOf(marker);
    if (pos < 0)
        return {};
    pos += marker.size();
    while (pos < html.size() && isAsciiWhitespace(html.at(pos)))
        ++pos;
    if (pos >= html.size() || html.at(pos) != '"')
        return {};
    const qsizetype begin = pos + 1;
    const qsizetype end = html.indexOf('"', begin);
    if (end < 0)
        return {};
    return QString::fromLatin1(html.mid(begin, end - begin)).trimmed();
}

// params_AbusePreventionHelper = [key,"token",lifetime]; only the array text is
// passed to QJsonDocument, never the whole page.
bool parseHelperArray(const QByteArray &html, QJsonArray *array)
{
    const QByteArray marker("params_AbusePreventionHelper = ");
    const qsizetype start = html.indexOf(marker);
    if (start < 0)
        return false;
    const qsizetype begin = start + marker.size();
    const qsizetype end = html.indexOf(';', begin);
    if (end < 0)
        return false;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(html.mid(begin, end - begin), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray())
        return false;
    const QJsonArray values = doc.array();
    if (values.size() < 2)
        return false;
    *array = values;
    return true;
}

} // namespace

namespace BingWebParser {

bool parseTranslatorPage(const QByteArray &html, TokenPage *page, QString *errorMessage)
{
    const auto fail = [errorMessage]() {
        if (errorMessage)
            *errorMessage = QStringLiteral("the token page is missing required parameters");
        return false;
    };

    TokenPage parsed;
    if (!quotedValueAfter(html, "IG:\"", &parsed.ig))
        return fail();
    if (!quotedValueAfter(html, "data-iid=\"", &parsed.iid))
        return fail();

    QJsonArray helper;
    if (!parseHelperArray(html, &helper))
        return fail();

    // A numeric key is a 13-digit integer: converting through qint64 keeps the
    // exact digits, while QVariant::toString() can switch to scientific notation.
    const QJsonValue keyValue = helper.at(0);
    if (keyValue.isDouble()) {
        parsed.key = QString::number(static_cast<qint64>(keyValue.toDouble()));
    } else if (keyValue.isString()) {
        parsed.key = keyValue.toString().trimmed();
    }
    if (parsed.key.isEmpty())
        return fail();

    const QJsonValue tokenValue = helper.at(1);
    if (tokenValue.isString())
        parsed.token = tokenValue.toString().trimmed();
    if (parsed.token.isEmpty())
        return fail();

    if (helper.size() >= 3) {
        const QJsonValue lifetimeValue = helper.at(2);
        if (lifetimeValue.isDouble() && lifetimeValue.toDouble() >= 0)
            parsed.lifetimeMs = static_cast<qint64>(lifetimeValue.toDouble());
    }

    // MUID, SID and TID are optional: the service may get MUID from Set-Cookie,
    // and SID/TID are diagnostic only.
    parsed.muid = pageJsonField(html, "muid");
    parsed.sid = pageJsonField(html, "sid");
    parsed.tid = pageJsonField(html, "tid");

    if (page)
        *page = parsed;
    return true;
}

QString muidFromSetCookieValues(const QList<QByteArray> &setCookieValues)
{
    for (const QByteArray &value : setCookieValues) {
        // One header value holds several cookies separated by ';', and Qt merges
        // repeated Set-Cookie headers with ','.
        for (const QByteArray &part : value.split(';')) {
            for (const QByteArray &segment : part.split(',')) {
                const QByteArray cookie = segment.trimmed();
                const qsizetype equals = cookie.indexOf('=');
                if (equals < 0)
                    continue;
                // The exact name only: MUIDB is a different cookie.
                if (cookie.left(equals).trimmed().compare("MUID", Qt::CaseInsensitive) != 0)
                    continue;
                return QString::fromLatin1(cookie.mid(equals + 1).trimmed());
            }
        }
    }
    return {};
}

bool isTokenRejection(const QByteArray &body)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    return statusCodeText(doc.object()) == QLatin1String("401");
}

TranslationResult parseResponse(const QByteArray &body)
{
    const TranslationResult error = parseErrorBody(body);
    if (!error.ok())
        return error;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray())
        return badResponse();

    // The first array element that actually contains translations wins; a
    // later element must not replace it.
    QJsonObject first;
    bool found = false;
    for (const QJsonValue &element : doc.array()) {
        if (!element.isObject())
            continue;
        const QJsonObject object = element.toObject();
        const QJsonValue translations = object.value(QStringLiteral("translations"));
        if (!translations.isArray() || translations.toArray().isEmpty())
            continue;
        first = object;
        found = true;
        break;
    }
    if (!found)
        return badResponse();

    QString translated;
    for (const QJsonValue &value : first.value(QStringLiteral("translations")).toArray()) {
        if (!value.isObject())
            return badResponse();
        const QString text = stringField(value.toObject(), QLatin1String("text"));
        if (text.trimmed().isEmpty())
            return badResponse();
        // The endpoint may split the result into several entries; join them in
        // order without inventing a separator.
        translated += text;
    }

    // Only the joined text and the detected source language are used; `to`,
    // `usedLLM` and any other fields are tolerated.
    TranslationResult result;
    result.translatedText = translated;
    const QString detected = stringField(
        first.value(QStringLiteral("detectedLanguage")).toObject(), QLatin1String("language"));
    result.detectedSourceLang =
        Languages::fromServiceCode(kServiceId, detected, Languages::Direction::Source);
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
        const QString status = statusCodeText(root);
        const QString message = stringField(root, QLatin1String("errorMessage"));
        if (status.isEmpty() && message.trimmed().isEmpty())
            return TranslationResult{};

        bool numericStatus = false;
        const int code = status.toInt(&numericStatus);
        if (numericStatus) {
            switch (code) {
            case 401:
                return errorResult(TranslationError::RateLimited,
                                   QStringLiteral("the endpoint rejected the token"));
            case 403:
            case 429:
                return errorResult(TranslationError::RateLimited,
                                   QStringLiteral("the endpoint reported a rate limit"));
            case 413:
                return errorResult(TranslationError::TooLong,
                                   QStringLiteral("the request is too long for the endpoint"));
            default:
                break;
            }
            if (code >= 500 && code < 600) {
                return errorResult(TranslationError::Network,
                                   QStringLiteral("the endpoint is temporarily unavailable"));
            }
        }
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

} // namespace BingWebParser
