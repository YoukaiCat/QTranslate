#include "services/deeplwebparser.h"

#include <QTest>

Q_DECLARE_METATYPE(TranslationError)

class TestDeepLWebParser : public QObject
{
    Q_OBJECT

private slots:
    void parsesTranslations()
    {
        const QByteArray body = R"({
            "translations": [
                {"detected_source_language": "en", "text": "Hallo, Welt"},
                {"detected_source_language": "en", "text": "Wie geht es dir?"}
            ]
        })";

        QStringList texts;
        QString detected;
        QString error;
        QVERIFY(DeepLWebParser::parseTranslations(body, &texts, &detected, &error));
        QCOMPARE(texts, QStringList({QStringLiteral("Hallo, Welt"),
                                     QStringLiteral("Wie geht es dir?")}));
        QCOMPARE(detected, QStringLiteral("en"));
        QVERIFY(error.isEmpty());
    }

    void mapsDetectedChinese()
    {
        const QByteArray body =
            R"({"translations":[{"detected_source_language":"ZH","text":"你好"}]})";
        QStringList texts;
        QString detected;
        QVERIFY(DeepLWebParser::parseTranslations(body, &texts, &detected, nullptr));
        QCOMPARE(detected, QStringLiteral("zh-Hans"));
    }

    void rejectsUnexpectedBodies()
    {
        QStringList texts;
        QString error;
        QVERIFY(!DeepLWebParser::parseTranslations("<html>blocked</html>", &texts, nullptr,
                                                    &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!DeepLWebParser::parseTranslations("{}", &texts, nullptr, &error));
        QVERIFY(!DeepLWebParser::parseTranslations(
            R"({"translations":[]})", &texts, nullptr, &error));
    }

    void errorStatuses_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("403 block") << 403 << TranslationError::RateLimited;
        QTest::newRow("429") << 429 << TranslationError::RateLimited;
        QTest::newRow("413") << 413 << TranslationError::TooLong;
        QTest::newRow("500") << 500 << TranslationError::Network;
        QTest::newRow("503") << 503 << TranslationError::Network;
        QTest::newRow("400") << 400 << TranslationError::BadResponse;
        QTest::newRow("404") << 404 << TranslationError::BadResponse;
    }

    void errorStatuses()
    {
        QFETCH(int, status);
        QFETCH(TranslationError, error);
        QCOMPARE(DeepLWebParser::errorForHttpStatus(status), error);
    }

    void detectsRateLimitBody()
    {
        QVERIFY(DeepLWebParser::isRateLimitBody(
            R"({"error":{"code":1042911,"message":"Too many requests"}})"));
        QVERIFY(DeepLWebParser::isRateLimitBody(R"({"error":"Too Many Requests"})"));
        QVERIFY(DeepLWebParser::isRateLimitBody(R"({"error":"rate limit exceeded"})"));
        QVERIFY(!DeepLWebParser::isRateLimitBody(
            R"({"translations":[{"text":"Hallo"}]})"));
    }
};

QTEST_GUILESS_MAIN(TestDeepLWebParser)
#include "tst_deeplwebparser.moc"
