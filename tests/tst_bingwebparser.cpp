#include "services/bingwebparser.h"

#include <QFile>
#include <QTest>

Q_DECLARE_METATYPE(TranslationError)

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(QTRANSLATE_FIXTURES_DIR "/bingweb/") + name);
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

const QString missingPageMessage =
    QStringLiteral("the token page is missing required parameters");
const QString noTranslatedTextMessage = QStringLiteral("no translated text in the response");

} // namespace

class TestBingWebParser : public QObject
{
    Q_OBJECT

private slots:
    void translatorPage()
    {
        BingWebParser::TokenPage page;
        QString error;
        QVERIFY(BingWebParser::parseTranslatorPage(
            fixture(QStringLiteral("bing_translator_page.html")), &page, &error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.ig, QStringLiteral("0123456789ABCDEF0123456789ABCDEF"));
        // The live page has several data-iid values; the first one is used.
        QCOMPARE(page.iid, QStringLiteral("translator.5023"));
        // The key is a JSON number; the exact digits must survive.
        QCOMPARE(page.key, QStringLiteral("1750000000123"));
        QCOMPARE(page.token, QStringLiteral("fake0token0value0for0tests0"));
        QCOMPARE(page.muid, QStringLiteral("00112233445566778899AABBCCDDEEFF"));
        QCOMPARE(page.sid, QStringLiteral("FEDCBA9876543210FEDCBA9876543210"));
        QCOMPARE(page.tid, QStringLiteral("0123456789abcdef0123456789abcdef"));
        QCOMPARE(page.lifetimeMs, qint64(3600000));
    }

    void translatorPageWithoutOptionalFields()
    {
        BingWebParser::TokenPage page;
        QString error;
        QVERIFY(BingWebParser::parseTranslatorPage(
            fixture(QStringLiteral("translator_page_no_muid.html")), &page, &error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.ig, QStringLiteral("0123456789ABCDEF0123456789ABCDEF"));
        QCOMPARE(page.iid, QStringLiteral("translator.5023"));
        QCOMPARE(page.key, QStringLiteral("1750000000123"));
        QVERIFY(page.muid.isEmpty());
        QVERIFY(page.sid.isEmpty());
        QVERIFY(page.tid.isEmpty());
        QCOMPARE(page.lifetimeMs, qint64(3600000));
    }

    void translatorPageStringKey()
    {
        // The key is accepted as a string as well, and the third helper element
        // replaces the one-hour default.
        const QByteArray html =
            R"(<html><script>IG:"0123456789ABCDEF0123456789ABCDEF";)"
            R"( data-iid="translator.5023";)"
            R"( params_AbusePreventionHelper = ["1750000000123", "fake0tok", 60000];)"
            R"(</script></html>)";
        BingWebParser::TokenPage page;
        QString error;
        QVERIFY(BingWebParser::parseTranslatorPage(html, &page, &error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.key, QStringLiteral("1750000000123"));
        QCOMPARE(page.token, QStringLiteral("fake0tok"));
        QCOMPARE(page.lifetimeMs, qint64(60000));

        const QByteArray noLifetime =
            R"(<html><script>IG:"A"; data-iid="translator.5023";)"
            R"( params_AbusePreventionHelper = [1, "fake0tok"];)</script></html>)";
        QVERIFY(BingWebParser::parseTranslatorPage(noLifetime, &page, &error));
        QCOMPARE(page.lifetimeMs, qint64(3600000));

        // A negative lifetime is not a valid one; the default stays.
        const QByteArray negativeLifetime =
            R"(<html><script>IG:"A"; data-iid="translator.5023";)"
            R"( params_AbusePreventionHelper = [1, "fake0tok", -5];)</script></html>)";
        QVERIFY(BingWebParser::parseTranslatorPage(negativeLifetime, &page, &error));
        QCOMPARE(page.lifetimeMs, qint64(3600000));
    }

    void translatorPageFailures_data()
    {
        QTest::addColumn<QString>("name");

        QTest::newRow("missing IG") << "translator_page_missing_ig.html";
        QTest::newRow("missing IID") << "translator_page_missing_iid.html";
        QTest::newRow("truncated page") << "translator_page_truncated.html";
        QTest::newRow("malformed helper") << "translator_page_bad_helper.html";
        QTest::newRow("short helper") << "translator_page_short_helper.html";
        QTest::newRow("JSON body") << "response.json";
    }

    void translatorPageFailures()
    {
        QFETCH(QString, name);

        BingWebParser::TokenPage page;
        QString error;
        QVERIFY(!BingWebParser::parseTranslatorPage(fixture(name), &page, &error));
        QCOMPARE(error, missingPageMessage);
        verifyFixedMessage(error);
        // A failed extraction must not leave a half-filled page behind.
        QVERIFY(page.ig.isEmpty());
        QVERIFY(page.muid.isEmpty());
        QCOMPARE(page.lifetimeMs, qint64(3600000));
    }

    void translatorPageEmptyMarker()
    {
        // A present but empty marker is as bad as a missing one.
        const QByteArray emptyMarkers =
            R"(<html><script>IG:""; data-iid="";)"
            R"( params_AbusePreventionHelper = [1, "fake0tok"];)</script></html>)";
        BingWebParser::TokenPage page;
        QString error;
        QVERIFY(!BingWebParser::parseTranslatorPage(emptyMarkers, &page, &error));
        QCOMPARE(error, missingPageMessage);
        verifyFixedMessage(error);

        const QByteArray emptyToken =
            R"(<html><script>IG:"A"; data-iid="translator.5023";)"
            R"( params_AbusePreventionHelper = [1, ""];)</script></html>)";
        QVERIFY(!BingWebParser::parseTranslatorPage(emptyToken, &page, &error));
        QCOMPARE(error, missingPageMessage);
        verifyFixedMessage(error);
    }

    void muidFromCookies()
    {
        QCOMPARE(BingWebParser::muidFromSetCookieValues(
                     {QByteArray("MUID=00112233445566778899AABBCCDDEEFF; expires=Mon, 09 Nov 2026 "
                                 "10:00:00 GMT; path=/; secure; HttpOnly")}),
                 QStringLiteral("00112233445566778899AABBCCDDEEFF"));

        // MUIDB is a different cookie and must not be taken for MUID.
        QCOMPARE(BingWebParser::muidFromSetCookieValues(
                     {QByteArray("MUIDB=WRONGVALUE; path=/"),
                      QByteArray("MUID=RIGHTVALUE; path=/")}),
                 QStringLiteral("RIGHTVALUE"));
        QCOMPARE(BingWebParser::muidFromSetCookieValues(
                     {QByteArray("MUIDB=WRONGVALUE; expires=Mon, 09 Nov 2026 10:00:00 GMT, "
                                 "MUID=RIGHTVALUE; path=/")}),
                 QStringLiteral("RIGHTVALUE"));

        // A Qt-merged list of cookies in one header value.
        QCOMPARE(BingWebParser::muidFromSetCookieValues(
                     {QByteArray("_EDGE_S=mkt=en-us; path=/, MUID=mergedvalue; path=/")}),
                 QStringLiteral("mergedvalue"));

        // The name is matched case-insensitively.
        QCOMPARE(BingWebParser::muidFromSetCookieValues(
                     {QByteArray("muid=lowercasevalue; path=/")}),
                 QStringLiteral("lowercasevalue"));

        QVERIFY(BingWebParser::muidFromSetCookieValues({}).isEmpty());
        QVERIFY(BingWebParser::muidFromSetCookieValues({QByteArray("MUIDB=onlywrong; path=/")})
                    .isEmpty());
        QVERIFY(BingWebParser::muidFromSetCookieValues({QByteArray("SRCHUID=V=2&GUID=abc; path=/")})
                    .isEmpty());
    }

    void response()
    {
        const TranslationResult result =
            BingWebParser::parseResponse(fixture(QStringLiteral("response.json")));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.translatedText, QStringLiteral("Hallo Welt"));
        QCOMPARE(result.detectedSourceLang, QStringLiteral("en"));
        QVERIFY(result.serviceId.isEmpty());
        QVERIFY(result.errorMessage.isEmpty());
    }

    void responseJoinsTranslations()
    {
        const TranslationResult result =
            BingWebParser::parseResponse(fixture(QStringLiteral("response_multi.json")));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.translatedText, QStringLiteral("Hallo Welt!"));
        QCOMPARE(result.detectedSourceLang, QStringLiteral("en"));
    }

    void responseIgnoresLaterElements()
    {
        // The first element with translations wins; usedLLM, `to` and a second
        // element must not replace it.
        const TranslationResult result = BingWebParser::parseResponse(
            R"([{"translations":[{"text":"Eins","to":"de"}],"usedLLM":true,)"
            R"("detectedLanguage":{"language":"en"}},)"
            R"({"translations":[{"text":"Zwei"}],"detectedLanguage":{"language":"de"}}])");
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.translatedText, QStringLiteral("Eins"));
        QCOMPARE(result.detectedSourceLang, QStringLiteral("en"));
    }

    void detectedLanguage_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("expected");

        QTest::newRow("pt") << "response_pt.json" << "pt-BR";
        QTest::newRow("pt-PT") << "response_pt_pt.json" << "pt-PT";
        QTest::newRow("nb") << "response_nb.json" << "nb";
        QTest::newRow("sr-Cyrl") << "response_sr_cyrl.json" << "sr";
        QTest::newRow("sr-Latn") << "response_sr_latn.json" << "sr";
        QTest::newRow("zh-Hans") << "response_zh_hans.json" << "zh-Hans";
        QTest::newRow("zh-Hant") << "response_zh_hant.json" << "zh-Hant";
        QTest::newRow("he") << "response_he.json" << "he";
        QTest::newRow("unknown language") << "response_unknown_language.json" << "";
        QTest::newRow("detected without language") << "response_missing_detected.json" << "";
        QTest::newRow("no detected language") << "response_no_detected.json" << "";
    }

    void detectedLanguage()
    {
        QFETCH(QString, name);
        QFETCH(QString, expected);

        const TranslationResult result = BingWebParser::parseResponse(fixture(name));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.detectedSourceLang, expected);
    }

    void badResponses_data()
    {
        QTest::addColumn<QString>("name");

        QTest::newRow("empty array") << "response_empty_array.json";
        QTest::newRow("empty translations") << "response_empty_translations.json";
        QTest::newRow("missing text") << "response_missing_text.json";
        QTest::newRow("non-string text") << "response_non_string_text.json";
        QTest::newRow("missing translations") << "response_missing_translations.json";
        QTest::newRow("JSON object") << "response_object.json";
        QTest::newRow("JSON string") << "response_string.json";
        QTest::newRow("malformed JSON") << "response_malformed.json";
    }

    void badResponses()
    {
        QFETCH(QString, name);

        const TranslationResult result = BingWebParser::parseResponse(fixture(name));
        QCOMPARE(result.error, TranslationError::BadResponse);
        QCOMPARE(result.errorMessage, noTranslatedTextMessage);
        QVERIFY(result.translatedText.isEmpty());
        QVERIFY(result.serviceId.isEmpty());
        verifyFixedMessage(result.errorMessage);
    }

    void tokenRejection()
    {
        QVERIFY(BingWebParser::isTokenRejection(fixture(QStringLiteral("error_401.json"))));
        QVERIFY(BingWebParser::isTokenRejection(
            fixture(QStringLiteral("error_401_string.json"))));
        // A 403 is a block, not a rejected token.
        QVERIFY(!BingWebParser::isTokenRejection(fixture(QStringLiteral("error_403.json"))));
        QVERIFY(!BingWebParser::isTokenRejection(fixture(QStringLiteral("error_429.json"))));
        QVERIFY(!BingWebParser::isTokenRejection(fixture(QStringLiteral("response_html.html"))));
        QVERIFY(!BingWebParser::isTokenRejection(fixture(QStringLiteral("response.json"))));
        QVERIFY(!BingWebParser::isTokenRejection(R"({"statusCode":)"));
        QVERIFY(!BingWebParser::isTokenRejection(QByteArray()));
    }

    void errorBodies_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<TranslationError>("error");
        QTest::addColumn<QString>("message");

        QTest::newRow("HTML page") << "response_html.html"
                                   << TranslationError::ServiceUnavailable
                                   << "the endpoint returned an HTML page";
        QTest::newRow("401") << "error_401.json" << TranslationError::RateLimited
                             << "the endpoint rejected the token";
        QTest::newRow("401 string") << "error_401_string.json" << TranslationError::RateLimited
                                    << "the endpoint rejected the token";
        QTest::newRow("403") << "error_403.json" << TranslationError::RateLimited
                             << "the endpoint reported a rate limit";
        QTest::newRow("429") << "error_429.json" << TranslationError::RateLimited
                             << "the endpoint reported a rate limit";
        QTest::newRow("205") << "error_205.json" << TranslationError::ServiceUnavailable
                             << "the endpoint returned an error";
        QTest::newRow("500") << "error_500.json" << TranslationError::Network
                             << "the endpoint is temporarily unavailable";
        QTest::newRow("message only") << "error_message_only.json"
                                      << TranslationError::ServiceUnavailable
                                      << "the endpoint returned an error";
        QTest::newRow("unknown status") << "error_unknown.json"
                                        << TranslationError::ServiceUnavailable
                                        << "the endpoint returned an error";
        QTest::newRow("rate limit text") << "rate_limit.txt" << TranslationError::RateLimited
                                         << "the endpoint reported a rate limit";
        QTest::newRow("blocked text") << "blocked.txt" << TranslationError::ServiceUnavailable
                                      << "the endpoint is blocked or unavailable";
    }

    void errorBodies()
    {
        QFETCH(QString, name);
        QFETCH(TranslationError, error);
        QFETCH(QString, message);

        const QByteArray body = fixture(name);
        const TranslationResult parsedError = BingWebParser::parseErrorBody(body);
        QCOMPARE(parsedError.error, error);
        QCOMPARE(parsedError.errorMessage, message);
        QVERIFY(parsedError.serviceId.isEmpty());
        verifyFixedMessage(parsedError.errorMessage);

        // A provider error can arrive with HTTP 200; parseResponse must not treat
        // it as a success.
        const TranslationResult result = BingWebParser::parseResponse(body);
        QCOMPARE(result.error, error);
        QCOMPARE(result.errorMessage, message);
        QVERIFY(result.translatedText.isEmpty());
        verifyFixedMessage(result.errorMessage);
    }

    void unrecognisedBodiesAreNotErrors()
    {
        QCOMPARE(BingWebParser::parseErrorBody(fixture(QStringLiteral("response.json"))).error,
                 TranslationError::None);
        QCOMPARE(
            BingWebParser::parseErrorBody(fixture(QStringLiteral("response_empty_array.json")))
                .error,
            TranslationError::None);
        QCOMPARE(BingWebParser::parseErrorBody(QByteArray()).error, TranslationError::None);
        QCOMPARE(
            BingWebParser::parseErrorBody(fixture(QStringLiteral("response_malformed.json"))).error,
            TranslationError::None);
        QCOMPARE(BingWebParser::parseErrorBody(fixture(QStringLiteral("plain.txt"))).error,
                 TranslationError::None);

        // An unrecognised plain text is not a success: the JSON parse fails.
        const TranslationResult result = BingWebParser::parseResponse(fixture(
            QStringLiteral("plain.txt")));
        QCOMPARE(result.error, TranslationError::BadResponse);
        verifyFixedMessage(result.errorMessage);
    }

    void httpStatus_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("400") << 400 << TranslationError::BadResponse;
        QTest::newRow("401") << 401 << TranslationError::RateLimited;
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
        QCOMPARE(BingWebParser::errorForHttpStatus(status), error);
    }
};

QTEST_GUILESS_MAIN(TestBingWebParser)
#include "tst_bingwebparser.moc"
