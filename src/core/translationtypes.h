#pragma once

#include <QMetaType>
#include <QString>

// Value types shared by translation services, TranslationManager and the UI
// (see doc/architecture.md, §4.5).

struct TranslationRequest
{
    QString text;
    QString sourceLang; // internal language code or "auto"
    QString targetLang; // internal language code
};

enum class TranslationError {
    None,
    Network,       // no connection, proxy, DNS or TLS error
    Timeout,
    Auth,          // invalid or missing API key (HTTP 401/403)
    QuotaExceeded, // DeepL 456, Google Cloud quota
    RateLimited,   // HTTP 429
    TooLong,       // request size limit exceeded
    UnsupportedLanguage,
    BadResponse,   // the response could not be parsed
    Cancelled,
    EmptyText,     // nothing to translate; detected before any request
};

struct TranslationResult
{
    QString translatedText;
    QString detectedSourceLang; // internal code, if the service reported it
    QString serviceId;
    TranslationError error = TranslationError::None;
    QString errorMessage; // technical details for the user and the log; never the text itself

    bool ok() const { return error == TranslationError::None; }
};

Q_DECLARE_METATYPE(TranslationRequest)
Q_DECLARE_METATYPE(TranslationResult)
