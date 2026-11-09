#pragma once

#include "core/languages.h"
#include "core/translationtypes.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QString>

// Pure functions that interpret the unofficial Bing web endpoint (see
// doc/free-endpoints.md, §3); tested on saved pages and responses without
// network access. The translator page is searched for the documented markers,
// never parsed as HTML or executed as JavaScript.
namespace BingWebParser {

struct TokenPage
{
    QString ig;       // IG:"..."
    QString iid;      // data-iid="..." (the first one)
    QString key;      // params_AbusePreventionHelper[0]
    QString token;    // params_AbusePreventionHelper[1]
    QString muid;     // "muid":"..." (optional)
    QString sid;      // "sid":"..." (optional, diagnostic only)
    QString tid;      // "tid":"..." (optional, diagnostic only)
    qint64 lifetimeMs = 3600000; // params_AbusePreventionHelper[2]
};

// Extracts the markers from the translator page. Returns false for a page without
// the required markers, a truncated page, a malformed helper array or a JSON body
// passed by mistake; errorMessage is a fixed literal without the body.
bool parseTranslatorPage(const QByteArray &html, TokenPage *page, QString *errorMessage);

// The MUID value from the GET's Set-Cookie header values; empty when absent.
// The name is matched case-insensitively and exactly, so MUIDB is not MUID.
QString muidFromSetCookieValues(const QList<QByteArray> &setCookieValues);

// True for a JSON object with statusCode 401 (number or string): a rejected
// token that the service may refresh exactly once. 403 is not a token rejection.
bool isTokenRejection(const QByteArray &body);

// A 2xx translate body: the first array element with a non-empty translations
// array is used; translations[].text is joined and detectedLanguage.language is
// mapped through Languages::fromServiceCode("bing-web", ..., Source).
TranslationResult parseResponse(const QByteArray &body);

// Recognises an HTML/interstitial page, a provider error object and a plain-text
// rate-limit/block body. Returns TranslationError::None when the body is not a
// recognised error.
TranslationResult parseErrorBody(const QByteArray &body);

// Error for a non-2xx HTTP status: 401/403/429 are a rate limit, 413 means the
// request was too long, 5xx is a network error, anything else is a bad
// response.
TranslationError errorForHttpStatus(int status);

} // namespace BingWebParser
