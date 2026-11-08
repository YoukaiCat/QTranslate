#include "ui/translationerrortext.h"

#include <QCoreApplication>

namespace {

// Untranslated title; marked for lupdate, translated in forResult().
const char *title(TranslationError error)
{
    switch (error) {
    case TranslationError::None:
        return nullptr;
    case TranslationError::Network:
        return QT_TRANSLATE_NOOP("TranslationErrorText", "Network error");
    case TranslationError::Timeout:
        return QT_TRANSLATE_NOOP("TranslationErrorText",
                                 "The translation service did not respond in time");
    case TranslationError::Auth:
        return QT_TRANSLATE_NOOP("TranslationErrorText",
                                 "The translation service rejected the API key");
    case TranslationError::QuotaExceeded:
        return QT_TRANSLATE_NOOP("TranslationErrorText",
                                 "The translation quota is exceeded; try another service");
    case TranslationError::RateLimited:
        return QT_TRANSLATE_NOOP("TranslationErrorText",
                                 "Too many requests; try again later or use another service");
    case TranslationError::ServiceUnavailable:
        return QT_TRANSLATE_NOOP("TranslationErrorText",
                                 "The translation service is unavailable");
    case TranslationError::TooLong:
        return QT_TRANSLATE_NOOP("TranslationErrorText", "The text is too long");
    case TranslationError::UnsupportedLanguage:
        return QT_TRANSLATE_NOOP("TranslationErrorText",
                                 "The language is not supported by the translation service");
    case TranslationError::BadResponse:
        return QT_TRANSLATE_NOOP("TranslationErrorText",
                                 "The translation service returned an unexpected response");
    case TranslationError::Cancelled:
        return QT_TRANSLATE_NOOP("TranslationErrorText", "Translation cancelled");
    case TranslationError::EmptyText:
        return QT_TRANSLATE_NOOP("TranslationErrorText", "Nothing to translate");
    }
    return nullptr;
}

} // namespace

namespace TranslationErrorText {

QString forResult(const TranslationResult &result)
{
    const char *key = title(result.error);
    if (!key)
        return {};
    const QString text = QCoreApplication::translate("TranslationErrorText", key);
    if (result.errorMessage.isEmpty())
        return text;
    return QStringLiteral("%1 (%2)").arg(text, result.errorMessage);
}

} // namespace TranslationErrorText
