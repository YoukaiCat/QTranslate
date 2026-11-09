#include "ui/proofreadingerrortext.h"

#include <QCoreApplication>

namespace {

// Untranslated title; marked for lupdate, translated in forResult().
const char *title(TranslationError error)
{
    switch (error) {
    case TranslationError::None:
        return nullptr;
    case TranslationError::Network:
        return QT_TRANSLATE_NOOP("ProofreadingErrorText", "Network error");
    case TranslationError::Timeout:
        return QT_TRANSLATE_NOOP("ProofreadingErrorText",
                                 "The text checking service did not respond in time");
    case TranslationError::Auth:
        return QT_TRANSLATE_NOOP("ProofreadingErrorText",
                                 "The text checking service rejected the request");
    case TranslationError::QuotaExceeded:
        return QT_TRANSLATE_NOOP("ProofreadingErrorText",
                                 "The text checking quota is exceeded; try again later");
    case TranslationError::RateLimited:
        return QT_TRANSLATE_NOOP("ProofreadingErrorText",
                                 "Too many requests; try again later or use a self-hosted server");
    case TranslationError::ServiceUnavailable:
        return QT_TRANSLATE_NOOP("ProofreadingErrorText",
                                 "The text checking service is unavailable");
    case TranslationError::TooLong:
        return QT_TRANSLATE_NOOP("ProofreadingErrorText", "The text is too long to check");
    case TranslationError::UnsupportedLanguage:
        return QT_TRANSLATE_NOOP("ProofreadingErrorText",
                                 "The language is not supported by the text checking service");
    case TranslationError::BadResponse:
        return QT_TRANSLATE_NOOP("ProofreadingErrorText",
                                 "The text checking service returned an unexpected response");
    case TranslationError::Cancelled:
        return QT_TRANSLATE_NOOP("ProofreadingErrorText", "Text checking cancelled");
    case TranslationError::EmptyText:
        return QT_TRANSLATE_NOOP("ProofreadingErrorText", "There is no text to check");
    }
    return nullptr;
}

} // namespace

namespace ProofreadingErrorText {

QString forResult(const ProofreadingResult &result)
{
    const char *key = title(result.error);
    if (!key)
        return {};
    const QString text = QCoreApplication::translate("ProofreadingErrorText", key);
    if (result.errorMessage.isEmpty())
        return text;
    return QStringLiteral("%1 (%2)").arg(text, result.errorMessage);
}

} // namespace ProofreadingErrorText