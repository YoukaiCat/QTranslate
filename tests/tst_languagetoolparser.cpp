#include "services/languagetoolparser.h"

#include <QFile>
#include <QTest>

Q_DECLARE_METATYPE(TranslationError)

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(QTRANSLATE_FIXTURES_DIR "/languagetool/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open fixture %s", qPrintable(file.fileName()));
    return file.readAll();
}

// Every error message is a fixed literal: the response body, the context and
// sentence, the checked text and any URL must never leak into it.
void verifyFixedMessage(const QString &message)
{
    QVERIFY(!message.isEmpty());
    QVERIFY2(!message.contains(QStringLiteral("SECRET")), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('<')), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
}

void verifyError(const ProofreadingResult &result, TranslationError error)
{
    QCOMPARE(result.error, error);
    QVERIFY(result.issues.isEmpty());
    QCOMPARE(result.serviceId, QStringLiteral("languagetool"));
    QVERIFY(!result.errorMessage.contains(QStringLiteral("SECRET")));
    verifyFixedMessage(result.errorMessage);
}

} // namespace

class TestLanguageToolParser : public QObject
{
    Q_OBJECT

private slots:
    void categoryFor_data()
    {
        QTest::addColumn<QString>("issueType");
        QTest::addColumn<QString>("categoryId");
        QTest::addColumn<QString>("expected");

        // rule.category.id rows.
        QTest::newRow("category TYPOS") << "" << "TYPOS" << "spelling";
        QTest::newRow("category PUNCTUATION") << "" << "PUNCTUATION" << "punctuation";
        QTest::newRow("category TYPOGRAPHY") << "" << "TYPOGRAPHY" << "punctuation";
        QTest::newRow("category CASING") << "" << "CASING" << "style";
        QTest::newRow("category STYLE") << "" << "STYLE" << "style";
        QTest::newRow("category REDUNDANCY") << "" << "REDUNDANCY" << "style";
        QTest::newRow("category REGISTER") << "" << "REGISTER" << "style";
        QTest::newRow("category GRAMMAR") << "" << "GRAMMAR" << "grammar";
        QTest::newRow("category MISC") << "" << "MISC" << "grammar";
        QTest::newRow("category SEMANTICS") << "" << "SEMANTICS" << "grammar";
        QTest::newRow("category CONFUSED_WORDS") << "" << "CONFUSED_WORDS" << "grammar";
        QTest::newRow("category COLLOCATIONS") << "" << "COLLOCATIONS" << "grammar";

        // rule.issueType rows.
        QTest::newRow("type misspelling") << "misspelling" << "" << "spelling";
        QTest::newRow("type typographical") << "typographical" << "" << "punctuation";
        QTest::newRow("type whitespace") << "whitespace" << "" << "punctuation";
        QTest::newRow("type style") << "style" << "" << "style";
        QTest::newRow("type register") << "register" << "" << "style";
        QTest::newRow("type non-conformance") << "non-conformance" << "" << "style";
        QTest::newRow("type locale-violation") << "locale-violation" << "" << "style";
        QTest::newRow("type locale-specific-content") << "locale-specific-content" << ""
                                                      << "style";
        QTest::newRow("type inconsistency") << "inconsistency" << "" << "style";

        // Both fields, an unknown pair and empty values.
        QTest::newRow("category wins over typographical") << "typographical" << "CASING"
                                                          << "style";
        QTest::newRow("missing issue type") << "" << "TYPOS" << "spelling";
        QTest::newRow("known issue type, unknown category") << "misspelling" << "OTHER"
                                                            << "spelling";
        QTest::newRow("unknown pair") << "other" << "OTHER" << "grammar";
        QTest::newRow("empty pair") << "" << "" << "grammar";
        QTest::newRow("category trimmed") << "" << "  typos  " << "spelling";
        QTest::newRow("issue type trimmed") << "  Misspelling  " << "" << "spelling";
    }

    void categoryFor()
    {
        QFETCH(QString, issueType);
        QFETCH(QString, categoryId);
        QFETCH(QString, expected);
        QCOMPARE(LanguageToolParser::categoryFor(issueType, categoryId), expected);
    }

    void normalResponse()
    {
        // The checked text is "This are a test.  Teh eror is very unique."
        // (42 UTF-16 units); every match lies inside it.
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("normal.json")), 42);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.serviceId, QStringLiteral("languagetool"));
        QVERIFY(result.correctedText.isEmpty());
        QVERIFY(result.errorMessage.isEmpty());
        QCOMPARE(result.issues.size(), 4);

        // Server order is preserved: the spelling match comes first even though
        // its offset is not the smallest.
        const ProofreadingIssue &spelling = result.issues.at(0);
        QCOMPARE(spelling.offset, 18);
        QCOMPARE(spelling.length, 3);
        QCOMPARE(spelling.message, QStringLiteral("Possible spelling mistake found."));
        QCOMPARE(spelling.replacements,
                 (QStringList{QStringLiteral("The"), QStringLiteral("Ten"),
                              QStringLiteral("Te")}));
        QCOMPARE(spelling.ruleId, QStringLiteral("MORFOLOGIK_RULE_EN_US"));
        QCOMPARE(spelling.category, QStringLiteral("spelling"));
        QCOMPARE(spelling.severity, 0);

        const ProofreadingIssue &grammar = result.issues.at(1);
        QCOMPARE(grammar.offset, 0);
        QCOMPARE(grammar.length, 8);
        QCOMPARE(grammar.message, QStringLiteral("Did you mean 'is'?"));
        QCOMPARE(grammar.replacements, QStringList{QStringLiteral("This is")});
        QCOMPARE(grammar.ruleId, QStringLiteral("THIS_ARE"));
        QCOMPARE(grammar.category, QStringLiteral("grammar"));
        QCOMPARE(grammar.severity, 0);

        const ProofreadingIssue &whitespace = result.issues.at(2);
        QCOMPARE(whitespace.offset, 16);
        QCOMPARE(whitespace.length, 2);
        QCOMPARE(whitespace.replacements, QStringList{QStringLiteral(" ")});
        QCOMPARE(whitespace.ruleId, QStringLiteral("CONSECUTIVE_SPACES"));
        QCOMPARE(whitespace.category, QStringLiteral("punctuation"));
        QCOMPARE(whitespace.severity, 0);

        const ProofreadingIssue &style = result.issues.at(3);
        QCOMPARE(style.offset, 30);
        QCOMPARE(style.length, 11);
        QCOMPARE(style.replacements, QStringList{});
        QCOMPARE(style.ruleId, QStringLiteral("VERY_UNIQUE"));
        QCOMPARE(style.category, QStringLiteral("style"));
        QCOMPARE(style.severity, 0);
    }

    void emptyMatches()
    {
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("empty_matches.json")), 0);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QVERIFY(result.issues.isEmpty());
        QCOMPARE(result.serviceId, QStringLiteral("languagetool"));
        QVERIFY(result.correctedText.isEmpty());
        QVERIFY(result.errorMessage.isEmpty());
    }

    void contextOffsetDiffers()
    {
        // The top-level offset is 5; context.offset is 0 and must be ignored.
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("context_offset_differs.json")), 16);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.issues.size(), 1);
        QCOMPARE(result.issues.at(0).offset, 5);
        QCOMPARE(result.issues.at(0).length, 3);
    }

    void missingIssueType()
    {
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("missing_issue_type.json")), 10);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.issues.size(), 1);
        QCOMPARE(result.issues.at(0).category, QStringLiteral("spelling"));
    }

    void unknownIssueType()
    {
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("unknown_issue_type.json")), 10);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.issues.size(), 1);
        QCOMPARE(result.issues.at(0).category, QStringLiteral("grammar"));
    }

    void casingIsStyle()
    {
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("casing.json")), 20);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.issues.size(), 1);
        QCOMPARE(result.issues.at(0).category, QStringLiteral("style"));
    }

    void emojiUtf16()
    {
        // Live evidence of 2026-11-09: POST /v2/check with
        // "😀 This are a test." reported offset 3 and length 8 for "This are":
        // the emoji is two UTF-16 units, not one code point.
        const QString text = QStringLiteral("😀 This are a test.");
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("emoji.json")), int(text.size()));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.issues.size(), 1);
        const ProofreadingIssue &issue = result.issues.at(0);
        QCOMPARE(issue.offset, 3);
        QCOMPARE(issue.length, 8);
        QCOMPARE(text.mid(issue.offset, issue.length), QStringLiteral("This are"));
    }

    void shortMessageFallback()
    {
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            QByteArrayLiteral(
                R"({"matches":[{"message":"","shortMessage":"Typo","offset":0,"length":3,"rule":{}}]})"),
            5);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.issues.size(), 1);
        QCOMPARE(result.issues.at(0).message, QStringLiteral("Typo"));
        QVERIFY(result.issues.at(0).ruleId.isEmpty());
        QCOMPARE(result.issues.at(0).category, QStringLiteral("grammar"));
    }

    void replacements()
    {
        const QByteArray body = QByteArrayLiteral(
            R"({"matches":[{"message":"M","offset":0,"length":1,)"
            R"("replacements":[{"value":"a"},7,{"value":3},{"value":"b"},{"other":"c"}]}]})");
        const ProofreadingResult result = LanguageToolParser::parseResponse(body, 5);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.issues.size(), 1);
        QCOMPARE(result.issues.at(0).replacements,
                 (QStringList{QStringLiteral("a"), QStringLiteral("b")}));

        // A missing array is an empty replacement list.
        const ProofreadingResult missing = LanguageToolParser::parseResponse(
            QByteArrayLiteral(R"({"matches":[{"message":"M","offset":0,"length":1,"rule":{}}]})"),
            5);
        QVERIFY2(missing.ok(), qPrintable(missing.errorMessage));
        QVERIFY(missing.issues.at(0).replacements.isEmpty());
    }

    void htmlBody()
    {
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("html.html")), 16);
        verifyError(result, TranslationError::ServiceUnavailable);
        QCOMPARE(result.errorMessage, QStringLiteral("the endpoint returned an HTML page"));
    }

    void malformedJson()
    {
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("malformed.json")), 16);
        verifyError(result, TranslationError::BadResponse);
        QCOMPARE(result.errorMessage,
                 QStringLiteral("response is not a LanguageTool response"));
    }

    void nonObjectRoot()
    {
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            QByteArrayLiteral(R"([{"matches":[]}])"), 0);
        verifyError(result, TranslationError::BadResponse);
        QCOMPARE(result.errorMessage,
                 QStringLiteral("response is not a LanguageTool response"));
    }

    void noMatchesArray()
    {
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("no_matches_array.json")), 16);
        verifyError(result, TranslationError::BadResponse);
        QCOMPARE(result.errorMessage, QStringLiteral("no matches array in the response"));
    }

    void matchesNotArray()
    {
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("matches_not_array.json")), 16);
        verifyError(result, TranslationError::BadResponse);
        QCOMPARE(result.errorMessage, QStringLiteral("no matches array in the response"));
    }

    void matchNotObject()
    {
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("match_not_object.json")), 16);
        verifyError(result, TranslationError::BadResponse);
        QCOMPARE(result.errorMessage, QStringLiteral("a match is not an object"));
    }

    void missingOffset()
    {
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("missing_offset.json")), 16);
        verifyError(result, TranslationError::BadResponse);
        QCOMPARE(result.errorMessage, QStringLiteral("a match has an invalid range"));
    }

    void badRange()
    {
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("bad_range.json")), 16);
        verifyError(result, TranslationError::BadResponse);
        QCOMPARE(result.errorMessage, QStringLiteral("a match has an invalid range"));
    }

    void zeroLength()
    {
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("zero_length.json")), 16);
        verifyError(result, TranslationError::BadResponse);
        QCOMPARE(result.errorMessage, QStringLiteral("a match has an invalid range"));
    }

    void invalidRange_data()
    {
        QTest::addColumn<QByteArray>("body");

        QTest::newRow("negative offset")
            << QByteArrayLiteral(R"({"matches":[{"message":"M","offset":-1,"length":3}]})");
        QTest::newRow("fractional offset")
            << QByteArrayLiteral(R"({"matches":[{"message":"M","offset":0.5,"length":3}]})");
        QTest::newRow("string offset")
            << QByteArrayLiteral(R"({"matches":[{"message":"M","offset":"0","length":3}]})");
        QTest::newRow("fractional length")
            << QByteArrayLiteral(R"({"matches":[{"message":"M","offset":0,"length":1.5}]})");
        QTest::newRow("past the text")
            << QByteArrayLiteral(R"({"matches":[{"message":"M","offset":2,"length":4}]})");
    }

    void invalidRange()
    {
        QFETCH(QByteArray, body);
        const ProofreadingResult result = LanguageToolParser::parseResponse(body, 5);
        verifyError(result, TranslationError::BadResponse);
        QCOMPARE(result.errorMessage, QStringLiteral("a match has an invalid range"));
    }

    void missingMessage()
    {
        const ProofreadingResult result = LanguageToolParser::parseResponse(
            fixture(QStringLiteral("missing_message.json")), 16);
        verifyError(result, TranslationError::BadResponse);
        QCOMPARE(result.errorMessage, QStringLiteral("a match has no message"));
    }

    void errorForHttpStatus_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("400") << 400 << TranslationError::BadResponse;
        QTest::newRow("401") << 401 << TranslationError::Auth;
        QTest::newRow("403") << 403 << TranslationError::Auth;
        QTest::newRow("404") << 404 << TranslationError::BadResponse;
        QTest::newRow("413") << 413 << TranslationError::TooLong;
        QTest::newRow("429") << 429 << TranslationError::RateLimited;
        QTest::newRow("500") << 500 << TranslationError::Network;
        QTest::newRow("503") << 503 << TranslationError::Network;
    }

    void errorForHttpStatus()
    {
        QFETCH(int, status);
        QFETCH(TranslationError, error);
        QCOMPARE(LanguageToolParser::errorForHttpStatus(status), error);
    }
};

QTEST_GUILESS_MAIN(TestLanguageToolParser)
#include "tst_languagetoolparser.moc"