#include "services/googleparser.h"

#include "core/languages.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace {

// Older versions of the endpoint omitted nulls in arrays ("[,,"en"]"), which is
// not valid JSON. Inserts the missing nulls outside of strings.
QByteArray fillSparseArrays(const QByteArray &json)
{
    QByteArray out;
    out.reserve(json.size() + 64);
    bool inString = false;
    bool escaped = false;
    char previous = 0; // last significant character outside strings
    for (const char c : json) {
        if (inString) {
            out += c;
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            out += c;
            continue;
        }
        if ((c == ',' && (previous == '[' || previous == ',')) || (c == ']' && previous == ','))
            out += "null";
        if (c == '"')
            inString = true;
        out += c;
        previous = c;
    }
    return out;
}

TranslationResult badResponse(const QString &reason)
{
    TranslationResult result;
    result.error = TranslationError::BadResponse;
    result.errorMessage = reason;
    return result;
}

} // namespace

namespace GoogleParser {

TranslationResult parseFreeResponse(const QByteArray &body)
{
    QJsonParseError parseError{};
    QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        doc = QJsonDocument::fromJson(fillSparseArrays(body), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray())
        return badResponse(QStringLiteral("response is not a JSON array"));

    const QJsonArray root = doc.array();
    const QJsonValue sentences = root.at(0);
    if (!sentences.isArray())
        return badResponse(QStringLiteral("no sentences in response"));

    TranslationResult result;
    bool found = false;
    for (const QJsonValue &sentence : sentences.toArray()) {
        // Rows without a string translation (e.g. transliteration with dt=rm) are skipped.
        const QJsonValue translated = sentence.toArray().at(0);
        if (translated.isString()) {
            result.translatedText += translated.toString();
            found = true;
        }
    }
    if (!found)
        return badResponse(QStringLiteral("no translated sentences in response"));

    // data[2] is the detected language; data[8][0][0] repeats it in newer responses.
    QString code = root.at(2).toString();
    if (code.isEmpty())
        code = root.at(8).toArray().at(0).toArray().at(0).toString();
    result.detectedSourceLang = Languages::fromServiceCode(QStringLiteral("google"), code,
                                                           Languages::Direction::Source);
    return result;
}

TranslationError errorForFreeHttpStatus(int status)
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

} // namespace GoogleParser
