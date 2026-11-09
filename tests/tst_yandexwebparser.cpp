#include "services/yandexwebparser.h"

#include <QFile>
#include <QTest>

Q_DECLARE_METATYPE(TranslationError)

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(QTRANSLATE_FIXTURES_DIR "/yandexweb/") + name);
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
    QVERIFY2(!message.contains(QLatin1Char('{')), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('}')), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
}

} // namespace

class TestYandexWebParser : public QObject
{
    Q_OBJECT

private slots:
    void normalResponse()
    {
        const TranslationResult result =
            YandexWebParser::parseResponse(fixture(QStringLiteral("normal.json")));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.translatedText, QStringLiteral("Hallo Welt!"));
        QCOMPARE(result.detectedSourceLang, QStringLiteral("en"));
        QVERIFY(result.serviceId.isEmpty());
        QVERIFY(result.errorMessage.isEmpty());
    }

    void extraFieldsAreIgnored()
    {
        const TranslationResult result =
            YandexWebParser::parseResponse(fixture(QStringLiteral("extra_fields.json")));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        // The first element wins; the second element must not replace it.
        QCOMPARE(result.translatedText, QStringLiteral("Hallo Welt!"));
        QCOMPARE(result.detectedSourceLang, QStringLiteral("en"));
    }

    void fromMapping_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("expected");

        QTest::newRow("en") << "normal.json" << "en";
        QTest::newRow("zh") << "from_zh.json" << "zh-Hans";
        QTest::newRow("no") << "from_no.json" << "nb";
        QTest::newRow("pt") << "from_pt.json" << "pt-BR";
        QTest::newRow("he") << "from_he.json" << "he";
        QTest::newRow("unknown") << "from_unknown.json" << "";
        QTest::newRow("missing") << "missing_from.json" << "";
    }

    void fromMapping()
    {
        QFETCH(QString, name);
        QFETCH(QString, expected);

        const TranslationResult result = YandexWebParser::parseResponse(fixture(name));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.detectedSourceLang, expected);
    }

    void brokenResponse_data()
    {
        QTest::addColumn<QString>("name");

        QTest::newRow("empty array") << "empty_array.json";
        QTest::newRow("empty text") << "empty_text.json";
        QTest::newRow("missing text") << "missing_text.json";
        QTest::newRow("non-object first element") << "first_element.json";
        QTest::newRow("JSON object") << "object.json";
        QTest::newRow("JSON string") << "string.json";
        QTest::newRow("malformed JSON") << "malformed.json";
    }

    void brokenResponse()
    {
        QFETCH(QString, name);

        const TranslationResult result = YandexWebParser::parseResponse(fixture(name));
        QCOMPARE(result.error, TranslationError::BadResponse);
        QCOMPARE(result.errorMessage, QStringLiteral("no translated text in the response"));
        QVERIFY(result.translatedText.isEmpty());
        QVERIFY(result.serviceId.isEmpty());
        verifyFixedMessage(result.errorMessage);
    }

    void htmlBody()
    {
        const TranslationResult result =
            YandexWebParser::parseResponse(fixture(QStringLiteral("html.html")));
        QCOMPARE(result.error, TranslationError::ServiceUnavailable);
        QCOMPARE(result.errorMessage, QStringLiteral("the endpoint returned an HTML page"));
        QVERIFY(result.translatedText.isEmpty());
        verifyFixedMessage(result.errorMessage);
    }

    void errorBodies_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<TranslationError>("error");
        QTest::addColumn<QString>("message");

        QTest::newRow("rate limit JSON") << "rate_limit.json" << TranslationError::RateLimited
                                         << "the endpoint reported a rate limit";
        QTest::newRow("rate limit text") << "rate_limit.txt" << TranslationError::RateLimited
                                         << "the endpoint reported a rate limit";
        QTest::newRow("blocked JSON") << "blocked.json"
                                      << TranslationError::ServiceUnavailable
                                      << "the endpoint is blocked or unavailable";
        QTest::newRow("blocked text") << "blocked.txt"
                                      << TranslationError::ServiceUnavailable
                                      << "the endpoint is blocked or unavailable";
        QTest::newRow("unknown JSON error") << "unknown_error.json"
                                            << TranslationError::ServiceUnavailable
                                            << "the endpoint returned an error";
    }

    void errorBodies()
    {
        QFETCH(QString, name);
        QFETCH(TranslationError, error);
        QFETCH(QString, message);

        const QByteArray body = fixture(name);
        const TranslationResult result = YandexWebParser::parseResponse(body);
        QCOMPARE(result.error, error);
        QCOMPARE(result.errorMessage, message);
        QVERIFY(result.translatedText.isEmpty());
        verifyFixedMessage(result.errorMessage);

        const TranslationResult parsedError = YandexWebParser::parseErrorBody(body);
        QCOMPARE(parsedError.error, error);
        QCOMPARE(parsedError.errorMessage, message);
    }

    void unknownPlainTextIsNotAnError()
    {
        QCOMPARE(YandexWebParser::parseErrorBody("Something happened").error,
                 TranslationError::None);
        const TranslationResult result = YandexWebParser::parseResponse("Something happened");
        QCOMPARE(result.error, TranslationError::BadResponse);
        verifyFixedMessage(result.errorMessage);
    }

    void normalBodyIsNotAnError()
    {
        QCOMPARE(YandexWebParser::parseErrorBody(fixture(QStringLiteral("normal.json"))).error,
                 TranslationError::None);
    }

    void httpStatus_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("400") << 400 << TranslationError::BadResponse;
        QTest::newRow("403") << 403 << TranslationError::RateLimited;
        QTest::newRow("413") << 413 << TranslationError::TooLong;
        QTest::newRow("429") << 429 << TranslationError::RateLimited;
        QTest::newRow("500") << 500 << TranslationError::Network;
        QTest::newRow("503") << 503 << TranslationError::Network;
    }

    void httpStatus()
    {
        QFETCH(int, status);
        QFETCH(TranslationError, error);
        QCOMPARE(YandexWebParser::errorForHttpStatus(status), error);
    }
};

QTEST_GUILESS_MAIN(TestYandexWebParser)
#include "tst_yandexwebparser.moc"
