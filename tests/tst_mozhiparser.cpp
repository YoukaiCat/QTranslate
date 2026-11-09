#include "services/mozhiparser.h"

#include <QFile>
#include <QTest>

Q_DECLARE_METATYPE(TranslationError)

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(QTRANSLATE_FIXTURES_DIR "/mozhi/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open fixture %s", qPrintable(file.fileName()));
    return file.readAll();
}

// Every message is a fixed literal: the body, the text and any URL must never
// leak into it.
void verifyFixedMessage(const QString &message)
{
    QVERIFY(!message.isEmpty());
    QVERIFY2(!message.contains(QStringLiteral("SECRET")), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('<')), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
}

} // namespace

class TestMozhiParser : public QObject
{
    Q_OBJECT

private slots:
    void normalResponse()
    {
        const MozhiParser::Response response = MozhiParser::parseResponse(
            fixture(QStringLiteral("normal.json")));
        QVERIFY2(response.result.ok(), qPrintable(response.result.errorMessage));
        QCOMPARE(response.result.translatedText, QStringLiteral("Hallo Welt!"));
        QCOMPARE(response.result.detectedSourceLang, QStringLiteral("en"));
        QCOMPARE(response.sourceLanguage, QStringLiteral("en"));
        QVERIFY(response.targetTransliteration.isEmpty());
        QVERIFY(response.result.serviceId.isEmpty());
    }

    void sourceLanguageFallback()
    {
        const MozhiParser::Response response = MozhiParser::parseResponse(
            fixture(QStringLiteral("source_language.json")));
        QVERIFY2(response.result.ok(), qPrintable(response.result.errorMessage));
        QCOMPARE(response.result.detectedSourceLang, QStringLiteral("en"));
        QCOMPARE(response.sourceLanguage, QStringLiteral("en"));
    }

    void autoSource()
    {
        const MozhiParser::Response response = MozhiParser::parseResponse(
            fixture(QStringLiteral("auto_source.json")));
        QVERIFY2(response.result.ok(), qPrintable(response.result.errorMessage));
        QCOMPARE(response.result.detectedSourceLang, QStringLiteral("en"));
        QCOMPARE(response.sourceLanguage, QStringLiteral("auto"));
    }

    void autoSourceWithoutDetectedLanguage()
    {
        const MozhiParser::Response response = MozhiParser::parseResponse(
            fixture(QStringLiteral("auto_empty.json")));
        QVERIFY2(response.result.ok(), qPrintable(response.result.errorMessage));
        QVERIFY(response.result.detectedSourceLang.isEmpty());
        QCOMPARE(response.sourceLanguage, QStringLiteral("auto"));
    }

    void targetTransliteration()
    {
        const MozhiParser::Response response = MozhiParser::parseResponse(
            fixture(QStringLiteral("transliteration.json")));
        QVERIFY2(response.result.ok(), qPrintable(response.result.errorMessage));
        QCOMPARE(response.result.translatedText, QStringLiteral("привет"));
        QCOMPARE(response.result.detectedSourceLang, QStringLiteral("ru"));
        QCOMPARE(response.targetTransliteration, QStringLiteral("privet"));
    }

    void emptyTranslation()
    {
        const MozhiParser::Response response = MozhiParser::parseResponse(
            fixture(QStringLiteral("empty_translation.json")));
        QCOMPARE(response.result.error, TranslationError::BadResponse);
        QCOMPARE(response.result.errorMessage,
                 QStringLiteral("no translated text in the response"));
        verifyFixedMessage(response.result.errorMessage);
    }

    void missingTranslation()
    {
        const MozhiParser::Response response = MozhiParser::parseResponse(
            fixture(QStringLiteral("missing_translation.json")));
        QCOMPARE(response.result.error, TranslationError::BadResponse);
        verifyFixedMessage(response.result.errorMessage);
    }

    void malformedJson()
    {
        const MozhiParser::Response response = MozhiParser::parseResponse(
            fixture(QStringLiteral("malformed.json")));
        QCOMPARE(response.result.error, TranslationError::BadResponse);
        verifyFixedMessage(response.result.errorMessage);
    }

    void htmlBody()
    {
        const MozhiParser::Response response = MozhiParser::parseResponse(
            fixture(QStringLiteral("html.html")));
        QCOMPARE(response.result.error, TranslationError::ServiceUnavailable);
        QCOMPARE(response.result.errorMessage,
                 QStringLiteral("the endpoint returned an HTML page"));
        verifyFixedMessage(response.result.errorMessage);
    }

    void jsonError()
    {
        const MozhiParser::Response response = MozhiParser::parseResponse(
            fixture(QStringLiteral("json_error.json")));
        QCOMPARE(response.result.error, TranslationError::ServiceUnavailable);
        QCOMPARE(response.result.errorMessage,
                 QStringLiteral("the selected engine is not available on this instance"));
        verifyFixedMessage(response.result.errorMessage);
    }

    void invalidLanguageJson()
    {
        const MozhiParser::Response response = MozhiParser::parseResponse(
            fixture(QStringLiteral("invalid_language.json")));
        QCOMPARE(response.result.error, TranslationError::UnsupportedLanguage);
        QCOMPARE(response.result.errorMessage,
                 QStringLiteral("the language pair is not supported"));
        verifyFixedMessage(response.result.errorMessage);
    }

    void jsonErrorObjectMessage()
    {
        const MozhiParser::Response response = MozhiParser::parseResponse(
            R"({"error":{"message":"Target language code invalid"}})");
        QCOMPARE(response.result.error, TranslationError::UnsupportedLanguage);
        verifyFixedMessage(response.result.errorMessage);
    }

    void unknownJsonError()
    {
        const MozhiParser::Response response = MozhiParser::parseResponse(
            R"({"error":"Something SECRET happened"})");
        QCOMPARE(response.result.error, TranslationError::ServiceUnavailable);
        QCOMPARE(response.result.errorMessage, QStringLiteral("the endpoint returned an error"));
        verifyFixedMessage(response.result.errorMessage);
    }

    void plainTextErrors_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<TranslationError>("error");
        QTest::addColumn<QString>("message");

        QTest::newRow("rate limit") << fixture(QStringLiteral("rate_limit.txt"))
                                    << TranslationError::RateLimited
                                    << QStringLiteral("the endpoint reported a rate limit");
        QTest::newRow("status code 429") << fixture(QStringLiteral("status_code_429.txt"))
                                         << TranslationError::RateLimited
                                         << QStringLiteral("the endpoint reported a rate limit");
        QTest::newRow("invalid language") << fixture(QStringLiteral("invalid_language.txt"))
                                          << TranslationError::UnsupportedLanguage
                                          << QStringLiteral("the language pair is not supported");
        QTest::newRow("unknown engine") << fixture(QStringLiteral("unknown_engine.txt"))
                                        << TranslationError::ServiceUnavailable
                                        << QStringLiteral(
                                               "the selected engine is not available on this "
                                               "instance");
        QTest::newRow("required fields") << fixture(QStringLiteral("required_fields.txt"))
                                         << TranslationError::BadResponse
                                         << QStringLiteral(
                                                "the endpoint rejected the request parameters");
    }

    void plainTextErrors()
    {
        QFETCH(QByteArray, body);
        QFETCH(TranslationError, error);
        QFETCH(QString, message);

        const TranslationResult result = MozhiParser::parseErrorBody(body);
        QCOMPARE(result.error, error);
        QCOMPARE(result.errorMessage, message);
        verifyFixedMessage(result.errorMessage);
    }

    void unknownPlainTextIsNotAnError()
    {
        QCOMPARE(MozhiParser::parseErrorBody("blocked").error, TranslationError::None);
    }

    void normalBodyIsNotAnError()
    {
        QCOMPARE(MozhiParser::parseErrorBody(fixture(QStringLiteral("normal.json"))).error,
                 TranslationError::None);
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
        QCOMPARE(MozhiParser::errorForHttpStatus(status), error);
    }
};

QTEST_GUILESS_MAIN(TestMozhiParser)
#include "tst_mozhiparser.moc"
