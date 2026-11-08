#pragma once

#include "core/translationtypes.h"

#include <QByteArray>

// Pure functions that interpret MyMemory responses (see doc/free-endpoints.md,
// §6); tested on saved responses without network access.
namespace MyMemoryParser {

// Parses responseData.translatedText, responseStatus (a number or a numeric
// string; missing means 200), responseDetails, quotaFinished (a bool or a
// "true"/"false" string) and the optional responseData.detectedLanguage.
// An HTML body is ServiceUnavailable, quotaFinished/quota text is
// QuotaExceeded, query-length details are TooLong, invalid language details
// are UnsupportedLanguage and rate-limit details are RateLimited; anything
// unclear is BadResponse. serviceId is left empty, and every message is a
// fixed literal that never includes responseDetails, the body or the text.
TranslationResult parseResponse(const QByteArray &body);

// Error for a non-2xx HTTP status: 403/429 are a rate limit, 413/414 mean the
// request was too long, 5xx is a network error, anything else is a bad
// response.
TranslationError errorForHttpStatus(int status);

} // namespace MyMemoryParser
