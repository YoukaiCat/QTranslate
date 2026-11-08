#include "services/mymemoryparser.h"

#include <QFile>
#include <QTest>

Q_DECLARE_METATYPE(TranslationError)

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(QTRANSLATE_FIXTURES_DIR "/mymemory/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open fixture %s", qPrintable(file.fileName()));
    return file.readAll();
}

// Every message is a fixed literal: responseDetails, the body, the text and
// any URL must never leak into it.
void verifyFixedMessage(const QString &message)
{
    QVERIFY(!message.isEmpty());
    QVERIFY2(!message.contains(QStringLiteral("SECRET")), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("blocked"), Qt::CaseInsensitive),
             qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('<')), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
}

} // namespace

class TestMyMemoryParser : public QObject
{
    Q_OBJECT

private slots:
    void normalResponse()
    {
        const TranslationResult result = MyMemoryParser::parseResponse(fixture(QStringLiteral(
            "normal.json")));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.translatedText, QStringLiteral("Hallo Welt!"));
        QVERIFY(result.serviceId.isEmpty());
        QVERIFY(result.detectedSourceLang.isEmpty());
    }

    void detectedLanguage()
    {
        const TranslationResult result = MyMemoryParser::parseResponse(fixture(QStringLiteral(
            "detected.json")));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.translatedText, QStringLiteral("Hello"));
        QCOMPARE(result.detectedSourceLang, QStringLiteral("en"));
    }

    void statusAsString()
    {
        const TranslationResult result = MyMemoryParser::parseResponse(fixture(QStringLiteral(
            "status_string.json")));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.translatedText, QStringLiteral("Hallo Welt!"));
    }

    void missingStatusMeans200()
    {
        const TranslationResult result = MyMemoryParser::parseResponse(
            R"({"responseData": {"translatedText": "Hallo"}})");
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.translatedText, QStringLiteral("Hallo"));
    }

    void quotaFinishedBool()
    {
        const TranslationResult result = MyMemoryParser::parseResponse(fixture(QStringLiteral(
            "quota_finished.json")));
        QCOMPARE(result.error, TranslationError::QuotaExceeded);
        verifyFixedMessage(result.errorMessage);
    }

    void quotaFinishedString()
    {
        const TranslationResult result = MyMemoryParser::parseResponse(fixture(QStringLiteral(
            "quota_finished_string.json")));
        QCOMPARE(result.error, TranslationError::QuotaExceeded);
        verifyFixedMessage(result.errorMessage);
    }

    void quotaDetails()
    {
        const TranslationResult result = MyMemoryParser::parseResponse(fixture(QStringLiteral(
            "quota_details.json")));
        QCOMPARE(result.error, TranslationError::QuotaExceeded);
        verifyFixedMessage(result.errorMessage);
    }

    void invalidPair()
    {
        const TranslationResult result = MyMemoryParser::parseResponse(fixture(QStringLiteral(
            "status_403_details.json")));
        QCOMPARE(result.error, TranslationError::UnsupportedLanguage);
        QVERIFY(result.translatedText.isEmpty());
        verifyFixedMessage(result.errorMessage);
    }

    void unknownNon200()
    {
        const TranslationResult result = MyMemoryParser::parseResponse(fixture(QStringLiteral(
            "status_500_unknown.json")));
        QCOMPARE(result.error, TranslationError::BadResponse);
        QVERIFY(result.translatedText.isEmpty());
        verifyFixedMessage(result.errorMessage);
    }

    void htmlBody()
    {
        const TranslationResult result = MyMemoryParser::parseResponse(fixture(QStringLiteral(
            "html.html")));
        QCOMPARE(result.error, TranslationError::ServiceUnavailable);
        QCOMPARE(result.errorMessage, QStringLiteral("the endpoint returned an HTML page"));
        verifyFixedMessage(result.errorMessage);
    }

    void malformedJson()
    {
        const TranslationResult result = MyMemoryParser::parseResponse(fixture(QStringLiteral(
            "malformed.json")));
        QCOMPARE(result.error, TranslationError::BadResponse);
        verifyFixedMessage(result.errorMessage);
    }

    void missingTranslatedText()
    {
        const TranslationResult result = MyMemoryParser::parseResponse(fixture(QStringLiteral(
            "missing_translated_text.json")));
        QCOMPARE(result.error, TranslationError::BadResponse);
        verifyFixedMessage(result.errorMessage);
    }

    void detailsClassification_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("query length") << QByteArray(
            R"({"responseData":{},"responseDetails":"QUERY LENGTH LIMIT EXCEEDED. MAX ALLOWED QUERY : 500 CHARS","responseStatus":200})")
                                      << TranslationError::TooLong;
        QTest::newRow("distinct languages") << QByteArray(
            R"({"responseData":{},"responseDetails":"PLEASE SELECT TWO DISTINCT LANGUAGES","responseStatus":200})")
                                            << TranslationError::UnsupportedLanguage;
        QTest::newRow("language pair") << QByteArray(
            R"({"responseData":{},"responseDetails":"LANGUAGE PAIR NOT SUPPORTED","responseStatus":200})")
                                       << TranslationError::UnsupportedLanguage;
        QTest::newRow("daily limit") << QByteArray(
            R"({"responseData":{},"responseDetails":"DAILY LIMIT REACHED","responseStatus":200})")
                                     << TranslationError::QuotaExceeded;
        QTest::newRow("too many requests") << QByteArray(
            R"({"responseData":{},"responseDetails":"TOO MANY REQUESTS","responseStatus":429})")
                                           << TranslationError::RateLimited;
    }

    void detailsClassification()
    {
        QFETCH(QByteArray, body);
        QFETCH(TranslationError, error);

        const TranslationResult result = MyMemoryParser::parseResponse(body);
        QCOMPARE(result.error, error);
        verifyFixedMessage(result.errorMessage);
    }

    void httpStatus_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("400") << 400 << TranslationError::BadResponse;
        QTest::newRow("403") << 403 << TranslationError::RateLimited;
        QTest::newRow("413") << 413 << TranslationError::TooLong;
        QTest::newRow("414") << 414 << TranslationError::TooLong;
        QTest::newRow("429") << 429 << TranslationError::RateLimited;
        QTest::newRow("500") << 500 << TranslationError::Network;
        QTest::newRow("503") << 503 << TranslationError::Network;
    }

    void httpStatus()
    {
        QFETCH(int, status);
        QFETCH(TranslationError, error);
        QCOMPARE(MyMemoryParser::errorForHttpStatus(status), error);
    }
};

QTEST_GUILESS_MAIN(TestMyMemoryParser)
#include "tst_mymemoryparser.moc"
