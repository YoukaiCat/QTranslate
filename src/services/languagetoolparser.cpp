#include "services/languagetoolparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <limits>

namespace {

constexpr QLatin1String kServiceId("languagetool");

ProofreadingResult errorResult(TranslationError error, const QString &message)
{
    ProofreadingResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    return result;
}

bool matches(const QString &value, QLatin1String expected)
{
    return value.compare(expected, Qt::CaseInsensitive) == 0;
}

// JSON numbers are doubles; an offset or length is valid only when it has no
// fractional part and fits an int.
bool wholeNumber(const QJsonValue &value, int *number)
{
    if (!value.isDouble())
        return false;
    const double raw = value.toDouble();
    if (raw < double(std::numeric_limits<int>::min())
        || raw > double(std::numeric_limits<int>::max())) {
        return false;
    }
    *number = static_cast<int>(raw);
    return static_cast<double>(*number) == raw;
}

QStringList replacementsOf(const QJsonObject &match)
{
    QStringList replacements;
    const QJsonArray values = match.value(QStringLiteral("replacements")).toArray();
    replacements.reserve(values.size());
    for (const QJsonValue &value : values) {
        if (!value.isObject())
            continue;
        const QJsonValue text = value.toObject().value(QStringLiteral("value"));
        if (text.isString())
            replacements << text.toString();
    }
    return replacements;
}

} // namespace

namespace LanguageToolParser {

QString categoryFor(const QString &issueType, const QString &categoryId)
{
    // The category id is checked first because it is more precise: CASING is
    // reported as typographical, but it is a style issue, not punctuation.
    const QString category = categoryId.trimmed();
    if (matches(category, QLatin1String("TYPOS")))
        return QStringLiteral("spelling");
    if (matches(category, QLatin1String("PUNCTUATION"))
        || matches(category, QLatin1String("TYPOGRAPHY"))) {
        return QStringLiteral("punctuation");
    }
    if (matches(category, QLatin1String("CASING")) || matches(category, QLatin1String("STYLE"))
        || matches(category, QLatin1String("REDUNDANCY"))
        || matches(category, QLatin1String("REGISTER"))) {
        return QStringLiteral("style");
    }
    if (matches(category, QLatin1String("GRAMMAR")) || matches(category, QLatin1String("MISC"))
        || matches(category, QLatin1String("SEMANTICS"))
        || matches(category, QLatin1String("CONFUSED_WORDS"))
        || matches(category, QLatin1String("COLLOCATIONS"))) {
        return QStringLiteral("grammar");
    }

    const QString type = issueType.trimmed();
    if (matches(type, QLatin1String("misspelling")))
        return QStringLiteral("spelling");
    if (matches(type, QLatin1String("typographical")) || matches(type, QLatin1String("whitespace")))
        return QStringLiteral("punctuation");
    if (matches(type, QLatin1String("style")) || matches(type, QLatin1String("register"))
        || matches(type, QLatin1String("non-conformance"))
        || matches(type, QLatin1String("locale-violation"))
        || matches(type, QLatin1String("locale-specific-content"))
        || matches(type, QLatin1String("inconsistency"))) {
        return QStringLiteral("style");
    }
    return QStringLiteral("grammar");
}

ProofreadingResult parseResponse(const QByteArray &body, int textLength)
{
    if (body.trimmed().startsWith('<'))
        return errorResult(TranslationError::ServiceUnavailable,
                           QStringLiteral("the endpoint returned an HTML page"));

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return errorResult(TranslationError::BadResponse,
                           QStringLiteral("response is not a LanguageTool response"));
    const QJsonObject root = doc.object();

    const QJsonValue matchesValue = root.value(QStringLiteral("matches"));
    if (!matchesValue.isArray())
        return errorResult(TranslationError::BadResponse,
                           QStringLiteral("no matches array in the response"));

    ProofreadingResult result;
    result.serviceId = kServiceId;
    const QJsonArray matchesArray = matchesValue.toArray();
    result.issues.reserve(matchesArray.size());
    for (const QJsonValue &matchValue : matchesArray) {
        if (!matchValue.isObject())
            return errorResult(TranslationError::BadResponse,
                               QStringLiteral("a match is not an object"));
        const QJsonObject match = matchValue.toObject();

        int offset = 0;
        int length = 0;
        if (!wholeNumber(match.value(QStringLiteral("offset")), &offset)
            || !wholeNumber(match.value(QStringLiteral("length")), &length)
            || offset < 0 || length <= 0 || qint64(offset) + length > textLength) {
            return errorResult(TranslationError::BadResponse,
                               QStringLiteral("a match has an invalid range"));
        }

        QString message = match.value(QStringLiteral("message")).toString();
        if (message.isEmpty())
            message = match.value(QStringLiteral("shortMessage")).toString();
        if (message.isEmpty())
            return errorResult(TranslationError::BadResponse,
                               QStringLiteral("a match has no message"));

        const QJsonObject rule = match.value(QStringLiteral("rule")).toObject();
        const QJsonObject ruleCategory = rule.value(QStringLiteral("category")).toObject();

        ProofreadingIssue issue;
        issue.offset = offset;
        issue.length = length;
        issue.message = message;
        issue.replacements = replacementsOf(match);
        issue.ruleId = rule.value(QStringLiteral("id")).toString();
        issue.category =
            categoryFor(rule.value(QStringLiteral("issueType")).toString(),
                        ruleCategory.value(QStringLiteral("id")).toString());
        issue.severity = 0;
        result.issues.append(issue);
    }
    return result;
}

TranslationError errorForHttpStatus(int status)
{
    switch (status) {
    case 401:
    case 403:
        return TranslationError::Auth;
    case 413:
        return TranslationError::TooLong;
    case 429:
        return TranslationError::RateLimited;
    default:
        break;
    }
    if (status >= 500 && status < 600)
        return TranslationError::Network;
    return TranslationError::BadResponse;
}

} // namespace LanguageToolParser