#pragma once

#include "core/translationtypes.h"

#include <QByteArray>

// Pure functions that interpret Google Translate responses; tested on saved
// responses without network access (see doc/architecture.md, §4.6).
namespace GoogleParser {

// Parses a response of the free endpoint (translate_a/single?client=gtx&dt=t):
// nested arrays, data[0] — sentences [translation, original, ...],
// data[2] — detected source language. Tolerates extra and missing fields and
// legacy sparse arrays ("[,,"en"]"); anything unexpected gives BadResponse.
// serviceId is left empty.
TranslationResult parseFreeResponse(const QByteArray &body);

// Parses the Cloud Translation API v2 response:
// data.translations[].translatedText / detectedSourceLanguage.
TranslationResult parseCloudResponse(const QByteArray &body);

// Error for a non-2xx HTTP status of the free endpoint.
TranslationError errorForFreeHttpStatus(int status);

// Error for a non-2xx HTTP status of the Cloud API. The response body is used
// because Google reports an invalid key with HTTP 400 and a message rather
// than 401/403.
TranslationError errorForCloudResponse(int status, const QByteArray &body);

} // namespace GoogleParser
