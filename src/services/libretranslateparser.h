#pragma once

#include "core/languages.h"
#include "core/translationtypes.h"

#include <QByteArray>

// Pure functions that interpret the LibreTranslate HTTP API (see the endpoint
// research in doc/roadmap/qtranslate/11-duckduckgo-libretranslate, §1.1). A 2xx
// body is `{"translatedText":"…"}` with an optional `detectedLanguage`; an error
// is a flat `{"error":"<sentence>"}` whose sentence is a human-readable English
// message, not a stable code, so the classifier matches the recorded phrases
// instead of the status. No network and no state; tested on synthetic bodies.
namespace LibreTranslateParser {

// Recognises an HTML page (a proxy or a policy page) and a provider error object
// whose `error` is a string. Returns TranslationError::None for an empty body, a
// plain-text body, a JSON array or an object without a string `error`, so the
// caller falls back to the HTTP status.
TranslationResult parseErrorBody(const QByteArray &body);

// A 2xx translate body. An error body is classified first, because a proxy or a
// misconfigured server may answer 200. `translatedText` must be a non-empty,
// non-whitespace string. `detectedLanguage` is optional and may be JSON null;
// only its string `language` is mapped, and `confidence` is ignored. Other
// fields are ignored, and serviceId is left empty for the caller to set.
TranslationResult parseResponse(const QByteArray &body);

// Error for a non-2xx HTTP status: 401/403 are auth, 429 is a rate limit,
// 413/414 mean the request was too long, 5xx is a network error, anything else
// (400, 404, an unfollowed 3xx) is a bad response.
TranslationError errorForHttpStatus(int status);

} // namespace LibreTranslateParser
