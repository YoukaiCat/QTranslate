#include "services/reversoparser.h"

#include <QFile>
#include <QTest>

Q_DECLARE_METATYPE(TranslationError)

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(QTRANSLATE_FIXTURES_DIR "/reverso/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open fixture %s", qPrintable(file.fileName()));
    return file.readAll();
}

// Every message is a fixed literal or a short validated provider message: the
// body, the text and any URL must never leak into it.
void verifySafeMessage(const QString &message)
{
    QVERIFY(!message.isEmpty());
    QVERIFY2(!message.contains(QStringLiteral("SECRET")), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('<')), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('{')), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('}')), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
}

void verifySuccess(const ReversoParser::Response &response)
{
    QVERIFY2(response.result.ok(), qPrintable(response.result.errorMessage));
    QVERIFY(response.result.errorMessage.isEmpty());
    QVERIFY(response.result.serviceId.isEmpty());
}

void verifyErrorResult(const TranslationResult &result, TranslationError error,
                       const QString &message)
{
    QVERIFY2(!result.ok(), qPrintable(result.errorMessage));
    QCOMPARE(result.error, error);
    QCOMPARE(result.errorMessage, message);
    verifySafeMessage(result.errorMessage);
    QVERIFY(result.translatedText.isEmpty());
    QVERIFY(result.serviceId.isEmpty());
}

void verifyErrorResponse(const ReversoParser::Response &response, TranslationError error,
                         const QString &message)
{
    verifyErrorResult(response.result, error, message);
    // The raw direction fields are exposed only on a success body.
    QVERIFY(response.directionFrom.isEmpty());
    QVERIFY(response.directionTo.isEmpty());
}

const QString rateLimitMessage = QStringLiteral("the endpoint reported a rate limit");
const QString genericErrorMessage = QStringLiteral("the endpoint reported an error");
const QString badJsonMessage = QStringLiteral("the response is not valid JSON");
const QString noSuccessFlagMessage = QStringLiteral("the response has no success flag");
const QString noTranslatedTextMessage = QStringLiteral("no translated text in the response");
const QString htmlPageMessage = QStringLiteral("the endpoint returned an HTML page");
const QString blockedMessage = QStringLiteral("the endpoint is blocked or unavailable");

} // namespace

class TestReversoParser : public QObject
{
    Q_OBJECT

private slots:
    void successSample()
    {
        const QByteArray body = fixture(QStringLiteral("response.json"));
        const ReversoParser::Response response = ReversoParser::parseResponse(body);
        verifySuccess(response);
        QCOMPARE(response.result.translatedText, QStringLiteral("La banque est fermee."));
        QCOMPARE(response.result.detectedSourceLang, QStringLiteral("en"));
        QCOMPARE(response.directionFrom, QStringLiteral("en"));
        QCOMPARE(response.directionTo, QStringLiteral("fr"));
        QCOMPARE(ReversoParser::parseErrorBody(body).error, TranslationError::None);
    }

    void secondPair()
    {
        const ReversoParser::Response response =
            ReversoParser::parseResponse(fixture(QStringLiteral("response_de_es.json")));
        verifySuccess(response);
        // A non-ASCII translation survives the JSON parse unchanged.
        QCOMPARE(response.result.translatedText, QStringLiteral("El banco está cerrado."));
        QCOMPARE(response.result.detectedSourceLang, QStringLiteral("de"));
        QCOMPARE(response.directionFrom, QStringLiteral("de"));
        QCOMPARE(response.directionTo, QStringLiteral("es"));
    }

    void extraFields()
    {
        const ReversoParser::Response response =
            ReversoParser::parseResponse(fixture(QStringLiteral("response_extra_fields.json")));
        verifySuccess(response);
        QCOMPARE(response.result.translatedText, QStringLiteral("Grüße"));
        QCOMPARE(response.result.detectedSourceLang, QStringLiteral("en"));
        QCOMPARE(response.directionFrom, QStringLiteral("en"));
        QCOMPARE(response.directionTo, QStringLiteral("de"));
    }

    void missingDirections()
    {
        const ReversoParser::Response response = ReversoParser::parseResponse(
            fixture(QStringLiteral("response_no_directions.json")));
        verifySuccess(response);
        QCOMPARE(response.result.translatedText, QStringLiteral("Bonjour"));
        QVERIFY(response.directionFrom.isEmpty());
        QVERIFY(response.directionTo.isEmpty());
        QVERIFY(response.result.detectedSourceLang.isEmpty());
    }

    void unknownDirection()
    {
        const ReversoParser::Response response = ReversoParser::parseResponse(
            fixture(QStringLiteral("response_unknown_direction.json")));
        verifySuccess(response);
        QCOMPARE(response.directionFrom, QStringLiteral("xx"));
        QCOMPARE(response.directionTo, QStringLiteral("de"));
        QVERIFY(response.result.detectedSourceLang.isEmpty());
    }

    void providerErrors_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<TranslationError>("error");
        QTest::addColumn<QString>("message");

        QTest::newRow("rate limit message") << "error_success_false.json"
                                            << TranslationError::RateLimited << rateLimitMessage;
        QTest::newRow("generic message") << "error_success_false_generic.json"
                                         << TranslationError::ServiceUnavailable
                                         << QStringLiteral("The text is not supported.");
        QTest::newRow("empty message") << "error_empty_message.json"
                                       << TranslationError::ServiceUnavailable
                                       << genericErrorMessage;
        QTest::newRow("error flag only") << "error_flag_only.json"
                                         << TranslationError::ServiceUnavailable
                                         << genericErrorMessage;
        QTest::newRow("error flag with success") << "error_flag_with_success.json"
                                                << TranslationError::ServiceUnavailable
                                                << genericErrorMessage;
    }

    void providerErrors()
    {
        QFETCH(QString, name);
        QFETCH(TranslationError, error);
        QFETCH(QString, message);

        const QByteArray body = fixture(name);
        verifyErrorResponse(ReversoParser::parseResponse(body), error, message);
        verifyErrorResult(ReversoParser::parseErrorBody(body), error, message);
    }

    void invalidProviderMessages_data()
    {
        QTest::addColumn<QString>("name");

        QTest::newRow("markup") << "error_message_html.json";
        QTest::newRow("control character") << "error_message_control.json";
        QTest::newRow("url") << "error_message_url.json";
        QTest::newRow("long message") << "error_long_message.json";
    }

    void invalidProviderMessages()
    {
        QFETCH(QString, name);

        const QByteArray body = fixture(name);
        verifyErrorResponse(ReversoParser::parseResponse(body),
                            TranslationError::ServiceUnavailable, genericErrorMessage);
        verifyErrorResult(ReversoParser::parseErrorBody(body),
                          TranslationError::ServiceUnavailable, genericErrorMessage);
    }

    void badBodies_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("message");

        QTest::newRow("missing translation") << "response_missing_translation.json"
                                             << noTranslatedTextMessage;
        QTest::newRow("empty translation") << "response_empty_translation.json"
                                           << noTranslatedTextMessage;
        QTest::newRow("whitespace translation") << "response_whitespace_translation.json"
                                                << noTranslatedTextMessage;
        QTest::newRow("non-string translation") << "response_non_string_translation.json"
                                                << noTranslatedTextMessage;
        QTest::newRow("missing success") << "response_missing_success.json"
                                         << noSuccessFlagMessage;
        QTest::newRow("non-bool success") << "response_non_bool_success.json"
                                          << noSuccessFlagMessage;
        QTest::newRow("empty object") << "response_empty_object.json" << noSuccessFlagMessage;
        QTest::newRow("array root") << "response_array.json" << badJsonMessage;
        QTest::newRow("string root") << "response_string.json" << badJsonMessage;
        QTest::newRow("malformed") << "response_malformed.json" << badJsonMessage;
        QTest::newRow("empty body") << "response_empty.json" << badJsonMessage;
    }

    void badBodies()
    {
        QFETCH(QString, name);
        QFETCH(QString, message);

        const QByteArray body = fixture(name);
        verifyErrorResponse(ReversoParser::parseResponse(body), TranslationError::BadResponse,
                            message);
        // None of these bodies is a provider error; the caller falls back to
        // the HTTP status.
        QCOMPARE(ReversoParser::parseErrorBody(body).error, TranslationError::None);
    }

    void htmlBody()
    {
        const QByteArray body = fixture(QStringLiteral("response_html.html"));

        verifyErrorResponse(ReversoParser::parseResponse(body),
                            TranslationError::ServiceUnavailable, htmlPageMessage);
        verifyErrorResult(ReversoParser::parseErrorBody(body),
                          TranslationError::ServiceUnavailable, htmlPageMessage);
    }

    void plainTextBodies_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<TranslationError>("error");
        QTest::addColumn<QString>("message");

        QTest::newRow("rate limit") << "rate_limit.txt" << TranslationError::RateLimited
                                    << rateLimitMessage;
        QTest::newRow("blocked") << "blocked.txt" << TranslationError::ServiceUnavailable
                                 << blockedMessage;
        QTest::newRow("neutral") << "plain.txt" << TranslationError::None << QString();
    }

    void plainTextBodies()
    {
        QFETCH(QString, name);
        QFETCH(TranslationError, error);
        QFETCH(QString, message);

        const QByteArray body = fixture(name);
        const TranslationResult result = ReversoParser::parseErrorBody(body);
        QCOMPARE(result.error, error);
        QCOMPARE(result.errorMessage, message);
        if (error == TranslationError::None)
            return;
        verifySafeMessage(result.errorMessage);

        // A recognised plain-text error wins over the HTTP status.
        verifyErrorResponse(ReversoParser::parseResponse(body), error, message);
    }

    void httpStatus_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("401") << 401 << TranslationError::RateLimited;
        QTest::newRow("403") << 403 << TranslationError::RateLimited;
        QTest::newRow("429") << 429 << TranslationError::RateLimited;
        QTest::newRow("413") << 413 << TranslationError::TooLong;
        QTest::newRow("500") << 500 << TranslationError::Network;
        QTest::newRow("503") << 503 << TranslationError::Network;
        QTest::newRow("400") << 400 << TranslationError::BadResponse;
        QTest::newRow("200") << 200 << TranslationError::BadResponse;
        QTest::newRow("204") << 204 << TranslationError::BadResponse;
    }

    void httpStatus()
    {
        QFETCH(int, status);
        QFETCH(TranslationError, error);
        QCOMPARE(ReversoParser::errorForHttpStatus(status), error);
    }
};

QTEST_GUILESS_MAIN(TestReversoParser)
#include "tst_reversoparser.moc"
