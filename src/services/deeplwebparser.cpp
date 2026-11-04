#include "services/deeplwebparser.h"

#include "services/deeplparser.h"

namespace DeepLWebParser {

bool parseTranslations(const QByteArray &body, QStringList *texts,
                       QString *detectedSourceLang, QString *errorMessage)
{
    return DeepLParser::parseTranslations(body, texts, detectedSourceLang, errorMessage);
}

TranslationError errorForHttpStatus(int status)
{
    switch (status) {
    case 403:
    case 429:
        return TranslationError::RateLimited;
    case 413:
        return TranslationError::TooLong;
    default:
        break;
    }
    if (status >= 500 && status < 600)
        return TranslationError::Network;
    return TranslationError::BadResponse;
}

bool isRateLimitBody(const QByteArray &body)
{
    const QString text = QString::fromUtf8(body);
    return text.contains(QStringLiteral("1042911"))
        || text.contains(QStringLiteral("too many requests"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("rate limit"), Qt::CaseInsensitive);
}

} // namespace DeepLWebParser
