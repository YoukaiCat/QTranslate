#include "services/duckduckgoparser.h"

#include <QFile>
#include <QTest>

Q_DECLARE_METATYPE(TranslationError)

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(QTRANSLATE_FIXTURES_DIR "/duckduckgo/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open fixture %s", qPrintable(file.fileName()));
    return file.readAll();
}

// Every message is a fixed literal: the page, the body, the text and any URL
// must never leak into it.
void verifyFixedMessage(const QString &message)
{
    QVERIFY(!message.isEmpty());
    QVERIFY2(!message.contains(QStringLiteral("SECRET")), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('<')), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('{')), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('}')), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
}

const QString tokenMessage = QStringLiteral("the token page has no usable vqd token");
const QString noTranslatedTextMessage = QStringLiteral("no translated text in the response");

} // namespace

class TestDuckDuckGoParser : public QObject
{
    Q_OBJECT

private slots:
    void tokenPage()
    {
        QString token;
        QString error;
        QVERIFY(DuckDuckGoParser::tokenFromPage(fixture(QStringLiteral("token_page.html")), &token,
                                                &error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        // The earlier unquoted `vqd=` mention is not a token.
        QCOMPARE(token, QStringLiteral("4-00000000000000000000000000000000000"));
    }

    void tokenPageUsesTheFirstQuotedMarker()
    {
        const QByteArray html = R"(<div vqd="first"></div><div vqd="second"></div>)";
        QString token;
        QString error;
        QVERIFY(DuckDuckGoParser::tokenFromPage(html, &token, &error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(token, QStringLiteral("first"));
    }

    void tokenPageFailures_data()
    {
        QTest::addColumn<QString>("name");

        QTest::newRow("missing marker") << "token_page_missing.html";
        QTest::newRow("empty value") << "token_page_empty.html";
        QTest::newRow("truncated value") << "token_page_truncated.html";
        QTest::newRow("control character") << "token_page_control.html";
        QTest::newRow("JSON body") << "response.json";
    }

    void tokenPageFailures()
    {
        QFETCH(QString, name);

        QString token;
        QString error;
        QVERIFY(!DuckDuckGoParser::tokenFromPage(fixture(name), &token, &error));
        QCOMPARE(error, tokenMessage);
        verifyFixedMessage(error);
        // A failed extraction must not leave a half-filled value behind.
        QVERIFY(token.isEmpty());
    }

    void tokenPageEmptyBody()
    {
        QString token;
        QString error;
        QVERIFY(!DuckDuckGoParser::tokenFromPage(QByteArray(), &token, &error));
        QCOMPARE(error, tokenMessage);
        verifyFixedMessage(error);
        QVERIFY(token.isEmpty());
    }

    void response()
    {
        const TranslationResult result =
            DuckDuckGoParser::parseResponse(fixture(QStringLiteral("response.json")));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.translatedText, QStringLiteral("Hallo"));
        QCOMPARE(result.detectedSourceLang, QStringLiteral("en"));
        QVERIFY(result.serviceId.isEmpty());
        QVERIFY(result.errorMessage.isEmpty());
    }

    void responseWithoutDetection()
    {
        // An explicit source makes the endpoint report `null`; the field may also
        // be absent. Both leave the detected language empty.
        for (const QString &name :
             {QStringLiteral("response_no_detected.json"),
              QStringLiteral("response_missing_detected.json")}) {
            const TranslationResult result = DuckDuckGoParser::parseResponse(fixture(name));
            QVERIFY2(result.ok(), qPrintable(result.errorMessage));
            QVERIFY2(result.detectedSourceLang.isEmpty(), qPrintable(name));
            QVERIFY2(!result.translatedText.isEmpty(), qPrintable(name));
        }
        QCOMPARE(DuckDuckGoParser::parseResponse(fixture(QStringLiteral("response_no_detected.json")))
                     .translatedText,
                 QStringLiteral("Good morning"));
    }

    void detectedLanguage_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("expected");

        QTest::newRow("en") << "en" << "en";
        QTest::newRow("fr") << "fr" << "fr";
        QTest::newRow("de") << "de" << "de";
        QTest::newRow("ru") << "ru" << "ru";
        QTest::newRow("mixed case") << "FR" << "fr";
        QTest::newRow("unknown language") << "xx" << "";
    }

    void detectedLanguage()
    {
        QFETCH(QString, language);
        QFETCH(QString, expected);

        // Built by hand so the test covers the wire form; a raw string that
        // ends with a quote trips the moc lexer.
        const QByteArray body = "{\"detected_language\":\"" + language.toUtf8()
            + "\",\"translated\":\"Hallo\"}";
        const TranslationResult result = DuckDuckGoParser::parseResponse(body);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.detectedSourceLang, expected);
    }

    void badResponses_data()
    {
        QTest::addColumn<QString>("name");

        QTest::newRow("missing translated") << "response_missing_translated.json";
        QTest::newRow("empty translated") << "response_empty_translated.json";
        QTest::newRow("whitespace translated") << "response_whitespace_translated.json";
        QTest::newRow("non-string translated") << "response_non_string_translated.json";
        QTest::newRow("JSON array") << "response_array.json";
        QTest::newRow("JSON string") << "response_string.json";
        QTest::newRow("malformed JSON") << "response_malformed.json";
        QTest::newRow("empty body") << "response_empty.json";
    }

    void badResponses()
    {
        QFETCH(QString, name);

        const TranslationResult result = DuckDuckGoParser::parseResponse(fixture(name));
        QCOMPARE(result.error, TranslationError::BadResponse);
        QCOMPARE(result.errorMessage, noTranslatedTextMessage);
        QVERIFY(result.translatedText.isEmpty());
        QVERIFY(result.serviceId.isEmpty());
        verifyFixedMessage(result.errorMessage);
    }

    void errorBodies_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<TranslationError>("error");
        QTest::addColumn<QString>("message");

        QTest::newRow("invalid_text") << "error_invalid_text.json" << TranslationError::TooLong
                                      << "the request is too long for the endpoint";
        QTest::newRow("invalid_to") << "error_invalid_to.json"
                                    << TranslationError::UnsupportedLanguage
                                    << "the endpoint rejected the language";
        QTest::newRow("Forbidden") << "error_forbidden.json"
                                   << TranslationError::ServiceUnavailable
                                   << "the endpoint returned an error";
        QTest::newRow("unknown code") << "error_unknown.json"
                                      << TranslationError::ServiceUnavailable
                                      << "the endpoint returned an error";
        QTest::newRow("HTML challenge") << "html_challenge.html"
                                        << TranslationError::ServiceUnavailable
                                        << "the endpoint returned an HTML page";
        QTest::newRow("rate limit text") << "rate_limit.txt" << TranslationError::RateLimited
                                         << "the endpoint reported a rate limit";
        QTest::newRow("blocked text") << "blocked.txt"
                                      << TranslationError::ServiceUnavailable
                                      << "the endpoint is blocked or unavailable";
    }

    void errorBodies()
    {
        QFETCH(QString, name);
        QFETCH(TranslationError, error);
        QFETCH(QString, message);

        const QByteArray body = fixture(name);
        const TranslationResult parsedError = DuckDuckGoParser::parseErrorBody(body);
        QCOMPARE(parsedError.error, error);
        QCOMPARE(parsedError.errorMessage, message);
        QVERIFY(parsedError.serviceId.isEmpty());
        verifyFixedMessage(parsedError.errorMessage);

        // A provider error can arrive with HTTP 200; parseResponse must not
        // treat it as a success.
        const TranslationResult result = DuckDuckGoParser::parseResponse(body);
        QCOMPARE(result.error, error);
        QCOMPARE(result.errorMessage, message);
        QVERIFY(result.translatedText.isEmpty());
        verifyFixedMessage(result.errorMessage);
    }

    void errorCodeIsComparedExactly()
    {
        // Only the two recorded codes are classified; a value that merely
        // contains one of them is not a length or a language error.
        QCOMPARE(DuckDuckGoParser::parseErrorBody(R"({"error":"invalid_textual"})").error,
                 TranslationError::ServiceUnavailable);
        QCOMPARE(DuckDuckGoParser::parseErrorBody(R"({"error":"invalid_to_XX"})").error,
                 TranslationError::ServiceUnavailable);
        QCOMPARE(DuckDuckGoParser::parseErrorBody(R"({"error":"INVALID_TEXT"})").error,
                 TranslationError::TooLong);
        QCOMPARE(DuckDuckGoParser::parseErrorBody(R"({"error":"Invalid_To"})").error,
                 TranslationError::UnsupportedLanguage);
        verifyFixedMessage(
            DuckDuckGoParser::parseErrorBody(R"({"error":"invalid_textual"})").errorMessage);
    }

    void unrecognisedBodiesAreNotErrors()
    {
        QCOMPARE(DuckDuckGoParser::parseErrorBody(fixture(QStringLiteral("response.json"))).error,
                 TranslationError::None);
        QCOMPARE(DuckDuckGoParser::parseErrorBody(fixture(QStringLiteral("plain.txt"))).error,
                 TranslationError::None);
        QCOMPARE(DuckDuckGoParser::parseErrorBody(QByteArray()).error, TranslationError::None);
        QCOMPARE(DuckDuckGoParser::parseErrorBody(QByteArray("{}")).error, TranslationError::None);
        QCOMPARE(DuckDuckGoParser::parseErrorBody(fixture(QStringLiteral("response_malformed.json")))
                     .error,
                 TranslationError::None);

        // A body without a recognised word is not a success either: it is not a
        // translation object.
        const TranslationResult result =
            DuckDuckGoParser::parseResponse(fixture(QStringLiteral("plain.txt")));
        QCOMPARE(result.error, TranslationError::BadResponse);
        verifyFixedMessage(result.errorMessage);
    }

    void httpStatus_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("400") << 400 << TranslationError::BadResponse;
        QTest::newRow("403") << 403 << TranslationError::RateLimited;
        QTest::newRow("404") << 404 << TranslationError::BadResponse;
        QTest::newRow("413") << 413 << TranslationError::TooLong;
        QTest::newRow("429") << 429 << TranslationError::RateLimited;
        QTest::newRow("302") << 302 << TranslationError::BadResponse;
        QTest::newRow("500") << 500 << TranslationError::Network;
        QTest::newRow("503") << 503 << TranslationError::Network;
    }

    void httpStatus()
    {
        QFETCH(int, status);
        QFETCH(TranslationError, error);
        QCOMPARE(DuckDuckGoParser::errorForHttpStatus(status), error);
    }
};

QTEST_GUILESS_MAIN(TestDuckDuckGoParser)
#include "tst_duckduckgoparser.moc"
