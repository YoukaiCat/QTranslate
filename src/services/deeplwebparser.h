#pragma once

#include "core/translationtypes.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

// Pure functions for the unofficial DeepL web endpoint
// (see doc/free-endpoints.md, §1).
namespace DeepLWebParser {

// Parses translations[]: text and detected_source_language. The response
// shape matches the official API, so this delegates to DeepLParser.
bool parseTranslations(const QByteArray &body, QStringList *texts,
                       QString *detectedSourceLang, QString *errorMessage);

// 403 is usually a bot/rate block on the web endpoint, not an auth error.
TranslationError errorForHttpStatus(int status);

// Detects a rate-limit message even when it arrives with HTTP 200.
bool isRateLimitBody(const QByteArray &body);

} // namespace DeepLWebParser
