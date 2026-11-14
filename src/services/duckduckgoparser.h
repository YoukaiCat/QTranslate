#pragma once

#include "core/languages.h"
#include "core/translationtypes.h"

#include <QByteArray>
#include <QString>

// Pure functions that interpret the unofficial DuckDuckGo translation frontend
// (see doc/free-endpoints.md). The search page is searched for the `vqd` token,
// never parsed as HTML or executed as JavaScript; the translate route answers
// `{"detected_language":"en","translated":"…"}` and reports its errors under
// HTTP 200 as well. Tested on saved pages and responses without network access.
namespace DuckDuckGoParser {

// The value of the first `vqd="` marker and the bytes up to the next quote. An
// unquoted `vqd=` is not a token. Returns false for a page without the marker,
// an empty value, a truncated value and a value with a control character;
// errorMessage is a fixed literal without the page.
bool tokenFromPage(const QByteArray &html, QString *token, QString *errorMessage);

// A 2xx translate body: `translated` must be a non-empty, non-whitespace
// string. `detected_language` is optional and may be JSON null (an explicit
// source); a string is mapped through Languages::fromServiceCode("duckduckgo",
// ..., Source), anything else leaves detectedSourceLang empty. Other fields are
// ignored.
TranslationResult parseResponse(const QByteArray &body);

// Recognises an HTML page, a provider error object (`invalid_text` and
// `invalid_to` exactly and case-insensitively, any other value) and a
// plain-text rate-limit or block body. Returns TranslationError::None when the
// body is not a recognised error.
TranslationResult parseErrorBody(const QByteArray &body);

// Error for a non-2xx HTTP status: 403/429 are a rate limit, 413 means the
// request was too long, 5xx is a network error, anything else is a bad
// response.
TranslationError errorForHttpStatus(int status);

} // namespace DuckDuckGoParser
