#pragma once

#include "core/proofreadingtypes.h"

#include <QByteArray>
#include <QString>

// Pure functions that interpret LanguageTool /v2/check responses (see
// doc/proofreading.md, §3); tested on saved responses without network access.
// The parser returns every match in server order; the service applies the
// category filter and adds the chunk offset. It does not log, never copies the
// body into an error message and keeps every error message a fixed literal.
namespace LanguageToolParser {

// Category for a rule.issueType and rule.category.id pair, compared trimmed and
// case-insensitively. The category id wins when it is known because it is more
// precise (CASING is reported as typographical, but it is style); the result is
// one of "spelling", "grammar", "punctuation" or "style", and a pair outside
// both tables is "grammar".
QString categoryFor(const QString &issueType, const QString &categoryId);

// Parses the matches of a /v2/check response. offset/length are UTF-16 code
// units and must form a range inside textLength, the length of the checked
// chunk; a non-integral or out-of-range pair is an error, never a clamped range.
// message falls back to shortMessage, replacements keep their order, rule.id
// becomes ruleId and severity stays 0. An HTML body is ServiceUnavailable; a
// malformed body, a missing matches array, a non-object match or a match
// without a message is BadResponse. serviceId is "languagetool" on success and
// on every error; correctedText stays empty because the parser returns issues.
ProofreadingResult parseResponse(const QByteArray &body, int textLength);

// Error for a non-2xx HTTP status: 401/403 is Auth, 413 TooLong, 429
// RateLimited, 5xx Network, anything else BadResponse.
TranslationError errorForHttpStatus(int status);

} // namespace LanguageToolParser