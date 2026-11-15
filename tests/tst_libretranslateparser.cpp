#include "services/libretranslateparser.h"

#include <QFile>
#include <QTest>

Q_DECLARE_METATYPE(TranslationError)

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(QTRANSLATE_FIXTURES_DIR "/libretranslate/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open fixture %s", qPrintable(file.fileName()));
    return file.readAll();
}

// Every message is a fixed literal: the body, the error sentence, the text and
// any URL must never leak into it.
void verifySafeMessage(const QString &message)
{
    QVERIFY(!message.isEmpty());
    QVERIFY2(!message.contains(QStringLiteral("SECRET")), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('<')), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('{')), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('}')), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
}

void verifySuccess(const TranslationResult &result)
{
    QVERIFY2(result.ok(), qPrintable(result.errorMessage));
    QVERIFY(result.errorMessage.isEmpty());
    QVERIFY(result.serviceId.isEmpty());
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

const QString rateLimitMessage = QStringLiteral("the endpoint reported a rate limit");
const QString tooLongMessage = QStringLiteral("the request is too long for the endpoint");
const QString unsupportedLanguageMessage = QStringLiteral("the endpoint rejected the language");
const QString authMessage = QStringLiteral("the endpoint rejected the API key");
const QString rejectedRequestMessage = QStringLiteral("the endpoint rejected the request");
const QString providerErrorMessage = QStringLiteral("the endpoint returned an error");
const QString htmlMessage = QStringLiteral("the endpoint returned an HTML page");
const QString noTranslatedTextMessage = QStringLiteral("no translated text in the response");

} // namespace

class TestLibreTranslateParser : public QObject
{
    Q_OBJECT

private slots:
    void response()
    {
        const TranslationResult result = LibreTranslateParser::parseResponse(
            fixture(QStringLiteral("response.json")));
        verifySuccess(result);
        QCOMPARE(result.translatedText, QStringLiteral("Привет"));
        QVERIFY(result.detectedSourceLang.isEmpty());
    }

    void detectedLanguage()
    {
        // The auto shape carries a mandatory-language object and an optional
        // float confidence, which TranslationResult has no field for.
        {
            const TranslationResult result = LibreTranslateParser::parseResponse(
                fixture(QStringLiteral("response_auto.json")));
            verifySuccess(result);
            QCOMPARE(result.translatedText, QStringLiteral("Good morning"));
            QCOMPARE(result.detectedSourceLang, QStringLiteral("ru"));
        }
        {
            const TranslationResult result = LibreTranslateParser::parseResponse(
                fixture(QStringLiteral("response_auto_no_confidence.json")));
            verifySuccess(result);
            QCOMPARE(result.translatedText, QStringLiteral("Good morning"));
            QCOMPARE(result.detectedSourceLang, QStringLiteral("ru"));
        }
        // An unknown code, null and an absent field leave the detected language
        // empty instead of guessing.
        for (const QString &name : {QStringLiteral("response_detected_unknown.json"),
                                    QStringLiteral("response_detected_null.json"),
                                    QStringLiteral("response.json")}) {
            const TranslationResult result =
                LibreTranslateParser::parseResponse(fixture(name));
            verifySuccess(result);
            QVERIFY2(result.detectedSourceLang.isEmpty(), qPrintable(name));
        }
        // The two reverse aliases come from the existing scan of serviceCodeOf():
        // the server reports `tl` for Tagalog and `pt` for its Portuguese model.
        {
            const TranslationResult result = LibreTranslateParser::parseResponse(
                R"({"detectedLanguage":{"language":"tl"},"translatedText":"Good morning"})");
            verifySuccess(result);
            QCOMPARE(result.detectedSourceLang, QStringLiteral("fil"));
        }
        {
            const TranslationResult result = LibreTranslateParser::parseResponse(
                R"({"detectedLanguage":{"language":"pt"},"translatedText":"Good morning"})");
            verifySuccess(result);
            QCOMPARE(result.detectedSourceLang, QStringLiteral("pt-PT"));
        }
    }

    void badResponses_data()
    {
        QTest::addColumn<QString>("name");

        QTest::newRow("missing") << "response_missing_translated.json";
        QTest::newRow("empty") << "response_empty_translated.json";
        QTest::newRow("whitespace") << "response_whitespace_translated.json";
        QTest::newRow("non-string") << "response_non_string_translated.json";
        QTest::newRow("batch array") << "response_array_translated.json";
        QTest::newRow("array") << "response_array.json";
        QTest::newRow("string") << "response_string.json";
        QTest::newRow("malformed") << "response_malformed.json";
        QTest::newRow("empty body") << "response_empty.json";
    }

    void badResponses()
    {
        QFETCH(QString, name);
        verifyErrorResult(LibreTranslateParser::parseResponse(fixture(name)),
                          TranslationError::BadResponse, noTranslatedTextMessage);
    }

    void errorBodies_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<TranslationError>("error");
        QTest::addColumn<QString>("message");

        QTest::newRow("slowdown") << "error_slowdown.json" << TranslationError::RateLimited
                                  << rateLimitMessage;
        QTest::newRow("text limit") << "error_text_limit.json" << TranslationError::TooLong
                                    << tooLongMessage;
        QTest::newRow("not supported") << "error_not_supported.json"
                                       << TranslationError::UnsupportedLanguage
                                       << unsupportedLanguageMessage;
        QTest::newRow("invalid key") << "error_invalid_key.json" << TranslationError::Auth
                                     << authMessage;
        QTest::newRow("portal key") << "error_portal.json" << TranslationError::Auth
                                    << authMessage;
        QTest::newRow("missing q") << "error_missing_q.json" << TranslationError::BadResponse
                                   << rejectedRequestMessage;
        QTest::newRow("unknown") << "error_unknown.json"
                                 << TranslationError::ServiceUnavailable << providerErrorMessage;
        QTest::newRow("html page") << "html_page.html" << TranslationError::ServiceUnavailable
                                   << htmlMessage;
    }

    void errorBodies()
    {
        QFETCH(QString, name);
        QFETCH(TranslationError, error);
        QFETCH(QString, message);
        verifyErrorResult(LibreTranslateParser::parseErrorBody(fixture(name)), error, message);
    }

    void unrecognisedBodiesAreNotErrors()
    {
        // The recorded endpoint has no plain-text error shape, so an unknown body
        // carries no classification and the caller falls back to the status.
        for (const QByteArray &body :
             {fixture(QStringLiteral("plain.txt")), QByteArray(), QByteArray("{}"),
              QByteArray("[1,2]")}) {
            const TranslationResult result = LibreTranslateParser::parseErrorBody(body);
            QCOMPARE(result.error, TranslationError::None);
            QVERIFY(result.errorMessage.isEmpty());
            QVERIFY(result.translatedText.isEmpty());
            QVERIFY(result.serviceId.isEmpty());
        }
    }

    void httpStatus_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("400") << 400 << TranslationError::BadResponse;
        QTest::newRow("404") << 404 << TranslationError::BadResponse;
        QTest::newRow("401") << 401 << TranslationError::Auth;
        QTest::newRow("403") << 403 << TranslationError::Auth;
        QTest::newRow("429") << 429 << TranslationError::RateLimited;
        QTest::newRow("413") << 413 << TranslationError::TooLong;
        QTest::newRow("414") << 414 << TranslationError::TooLong;
        QTest::newRow("500") << 500 << TranslationError::Network;
        QTest::newRow("503") << 503 << TranslationError::Network;
    }

    void httpStatus()
    {
        QFETCH(int, status);
        QFETCH(TranslationError, error);
        QCOMPARE(LibreTranslateParser::errorForHttpStatus(status), error);
    }
};

QTEST_GUILESS_MAIN(TestLibreTranslateParser)
#include "tst_libretranslateparser.moc"
