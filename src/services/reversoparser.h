#pragma once

#include "core/languages.h"
#include "core/translationtypes.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

// Pure functions that interpret the unofficial Reverso endpoint (see
// doc/free-endpoints.md, §4); tested on saved responses without network access.
// The parser does not parse HTML and never puts the body, the text or a URL into
// a message.
namespace ReversoParser {

struct Response
{
    TranslationResult result; // translatedText/detectedSourceLang/error; serviceId empty
    QString directionFrom;    // raw directionFrom, empty when absent or not a string
    QString directionTo;      // raw directionTo, empty when absent or not a string
};

// Parses a documented JSON body. A provider error (`error == true` or
// `success == false`) is classified first; otherwise `success` must be true
// and `translation` a non-empty string. Every message is a fixed literal
// except the validated short provider message of a `success: false` body;
// the raw body, the text and a URL are never included.
Response parseResponse(const QByteArray &body);

// Recognises an HTML page, a provider error object and a plain-text
// rate-limit/block body. Returns TranslationError::None when the body is not
// a recognised error, so the caller can fall back to the HTTP status.
TranslationResult parseErrorBody(const QByteArray &body);

// Error for a non-2xx HTTP status: 401/403/429 are a rate limit, 413 means
// the request was too long, 5xx is a network error, anything else is a bad
// response.
TranslationError errorForHttpStatus(int status);

} // namespace ReversoParser
