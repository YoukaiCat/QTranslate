#pragma once

#include "core/translationtypes.h"

#include <QByteArray>

// Pure functions that interpret Mozhi responses (see doc/free-endpoints.md,
// §5); tested on saved responses without network access.
namespace MozhiParser {

struct Response
{
    TranslationResult result;      // translatedText/detectedSourceLang/error; serviceId empty
    QString sourceLanguage;        // raw source_language, may be "auto"
    QString targetTransliteration; // raw target_transliteration
};

// Parses translated-text, detected, source_language and target_transliteration.
// The body is classified as an error first, because Mozhi reports rate limits
// and disabled engines as plain text even with HTTP 200 or 500. Errors are
// RateLimited, UnsupportedLanguage, ServiceUnavailable or BadResponse; every
// message is a fixed literal that never includes the body, the text or a URL.
Response parseResponse(const QByteArray &body);

// Recognises only error bodies and returns TranslationError::None for a normal
// response, so the caller can fall back to the HTTP status.
TranslationResult parseErrorBody(const QByteArray &body);

// Error for a non-2xx HTTP status: 403/429 are a rate limit, 413/414 mean the
// request was too long, 5xx is a network error, anything else is a bad
// response.
TranslationError errorForHttpStatus(int status);

} // namespace MozhiParser
