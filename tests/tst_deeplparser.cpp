#include "services/deeplparser.h"

#include <QFile>
#include <QTest>

Q_DECLARE_METATYPE(TranslationError)

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(QTRANSLATE_FIXTURES_DIR "/deepl/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open fixture %s", qPrintable(file.fileName()));
    return file.readAll();
}

} // namespace

class TestDeepLParser : public QObject
{
    Q_OBJECT

private slots:
    void parseSingle()
    {
        const TranslationResult result =
            DeepLParser::parseResponse(fixture(QStringLiteral("translate_single.json")));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.translatedText, QStringLiteral("Hallo Welt!"));
        QCOMPARE(result.detectedSourceLang, QStringLiteral("en"));
        QVERIFY(result.serviceId.isEmpty());
    }

    void parseMultiple()
    {
        const TranslationResult result =
            DeepLParser::parseResponse(fixture(QStringLiteral("translate_multiple.json")));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.translatedText,
                 QStringLiteral("Das Wetter ist heute schön. Wir gehen im Park spazieren."));
        QCOMPARE(result.detectedSourceLang, QStringLiteral("en"));
    }

    void splitTranslations()
    {
        QStringList texts;
        QString detected;
        QString error;
        QVERIFY(DeepLParser::parseTranslations(
            fixture(QStringLiteral("translate_multiple.json")), &texts, &detected, &error));
        QCOMPARE(texts.size(), 2);
        QCOMPARE(texts.at(0), QStringLiteral("Das Wetter ist heute schön. "));
        QCOMPARE(texts.at(1), QStringLiteral("Wir gehen im Park spazieren."));
        QCOMPARE(detected, QStringLiteral("en"));
        QVERIFY(error.isEmpty());
    }

    void unknownDetectedLanguage()
    {
        const TranslationResult result =
            DeepLParser::parseResponse(R"({"translations":[{"text":"Hola","detected_source_language":"XX"}]})");
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QVERIFY(result.detectedSourceLang.isEmpty());
    }

    void badResponses_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("array root") << QByteArray("[]");
        QTest::newRow("no translations") << QByteArray(R"({"foo": 1})");
        QTest::newRow("empty translations") << QByteArray(R"({"translations": []})");
        QTest::newRow("no text") << QByteArray(R"({"translations": [{"foo": "bar"}]})");
        QTest::newRow("html") << QByteArray("<html>error</html>");
    }

    void badResponses()
    {
        QFETCH(QByteArray, body);
        const TranslationResult result = DeepLParser::parseResponse(body);
        QCOMPARE(result.error, TranslationError::BadResponse);
        QVERIFY(!result.errorMessage.isEmpty());
        QVERIFY(result.translatedText.isEmpty());
    }

    void usage()
    {
        int used = 0;
        int limit = 0;
        QVERIFY(DeepLParser::parseUsage(fixture(QStringLiteral("usage.json")), &used, &limit));
        QCOMPARE(used, 1234);
        QCOMPARE(limit, 500000);
        QVERIFY(!DeepLParser::parseUsage(QByteArray(R"({"character_count": 1})"), &used,
                                         &limit));
        QVERIFY(!DeepLParser::parseUsage(QByteArray("[]"), &used, &limit));
    }

    void httpStatus_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("400") << 400 << TranslationError::BadResponse;
        QTest::newRow("403") << 403 << TranslationError::Auth;
        QTest::newRow("413") << 413 << TranslationError::TooLong;
        QTest::newRow("429") << 429 << TranslationError::RateLimited;
        QTest::newRow("456") << 456 << TranslationError::QuotaExceeded;
        QTest::newRow("500") << 500 << TranslationError::Network;
        QTest::newRow("503") << 503 << TranslationError::Network;
    }

    void httpStatus()
    {
        QFETCH(int, status);
        QFETCH(TranslationError, error);
        QCOMPARE(DeepLParser::errorForHttpStatus(status), error);
    }
};

QTEST_GUILESS_MAIN(TestDeepLParser)
#include "tst_deeplparser.moc"
