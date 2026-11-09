#pragma once

#include "core/translationtypes.h"

#include <QByteArray>

// Pure functions that interpret Yandex web responses (see
// doc/free-endpoints.md, §2); tested on saved responses without network access.
namespace YandexWebParser {

// A 200/2xx body: [{"text":"...","from":"en","to":"de"}]; the first element is
// used. Extra fields and later elements are tolerated. Errors are
// ServiceUnavailable, RateLimited or BadResponse; every message is a fixed
// literal that never includes the body, the text or a URL.
TranslationResult parseResponse(const QByteArray &body);

// Recognises an HTML/captcha page, a JSON error and a plain-text
// rate-limit/block body. Returns TranslationError::None when the body is not a
// recognised error, so the caller can fall back to the HTTP status.
TranslationResult parseErrorBody(const QByteArray &body);

// Error for a non-2xx HTTP status: 403/429 are a rate limit, 413 means the
// request was too long, 5xx is a network error, anything else is a bad
// response.
TranslationError errorForHttpStatus(int status);

} // namespace YandexWebParser
