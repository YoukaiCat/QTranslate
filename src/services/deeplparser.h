#pragma once

#include "core/translationtypes.h"

#include <QByteArray>

// Pure functions for DeepL API responses (see doc/architecture.md, §4.7).
namespace DeepLParser {

// Parses translations[]: text and detected_source_language.
TranslationResult parseResponse(const QByteArray &body);

// Same response split into per-item texts; used by the service to restore the
// whitespace between chunks. Returns false and sets errorMessage on failure.
bool parseTranslations(const QByteArray &body, QStringList *texts,
                       QString *detectedSourceLang, QString *errorMessage);

// Parses the /v2/usage response. Returns false if the fields are missing.
bool parseUsage(const QByteArray &body, int *characterCount, int *characterLimit);

// Maps a non-2xx HTTP status of the DeepL API to a translation error.
TranslationError errorForHttpStatus(int status);

} // namespace DeepLParser
