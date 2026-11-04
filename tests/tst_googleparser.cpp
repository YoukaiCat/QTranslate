#include "services/googleparser.h"

#include <QFile>
#include <QTest>

// Fixtures in tests/fixtures/google. Saved responses of the gtx endpoint
// (neutral test phrases): hello_auto_ru, paragraphs_en_de, explicit_ru_en,
// sentences_en_ja, detected_iw, transliteration_row (dt=t&dt=rm).
// Synthetic: sparse_legacy (old format without nulls), html_error, empty_array,
// object_root, no_translation, detected_in_ld_result, unknown_language.

Q_DECLARE_METATYPE(TranslationError)

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(QTRANSLATE_FIXTURES_DIR "/google/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open fixture %s", qPrintable(file.fileName()));
    return file.readAll();
}

} // namespace

class TestGoogleParser : public QObject
{
    Q_OBJECT

private slots:
    void validResponses_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("translation");
        QTest::addColumn<QString>("detected");

        QTest::newRow("single phrase") << "hello_auto_ru.json" << "Привет, мир" << "en";
        QTest::newRow("sentences and paragraphs")
            << "paragraphs_en_de.json"
            << "Das Wetter ist heute schön. Wir machen einen Spaziergang im Park.\n\n"
               "Am Abend lesen wir ein Buch und trinken Tee. Morgen wird ein neuer Tag sein!"
            << "en";
        QTest::newRow("explicit source") << "explicit_ru_en.json" << "Good afternoon" << "ru";
        QTest::newRow("japanese") << "sentences_en_ja.json" << "おはよう。元気ですか？" << "en";
        QTest::newRow("hebrew reported as iw")
            << "detected_iw.json" << "Good morning everyone" << "he";
        QTest::newRow("transliteration row skipped")
            << "transliteration_row.json" << "Доброе утро" << "en";
        QTest::newRow("legacy sparse arrays") << "sparse_legacy.json" << "Hallo Welt" << "en";
        QTest::newRow("language only in ld_result")
            << "detected_in_ld_result.json" << "Bonjour" << "en";
        QTest::newRow("unknown language code") << "unknown_language.json" << "Hola" << "";
    }

    void validResponses()
    {
        QFETCH(QString, file);
        QFETCH(QString, translation);
        QFETCH(QString, detected);

        const TranslationResult result = GoogleParser::parseFreeResponse(fixture(file));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.translatedText, translation);
        QCOMPARE(result.detectedSourceLang, detected);
        QVERIFY(result.serviceId.isEmpty());
    }

    void badResponses_data()
    {
        QTest::addColumn<QByteArray>("body");

        QTest::newRow("html error page") << fixture(QStringLiteral("html_error.html"));
        QTest::newRow("empty array") << fixture(QStringLiteral("empty_array.json"));
        QTest::newRow("object root") << fixture(QStringLiteral("object_root.json"));
        QTest::newRow("no translation") << fixture(QStringLiteral("no_translation.json"));
        QTest::newRow("empty body") << QByteArray();
        QTest::newRow("truncated") << fixture(QStringLiteral("hello_auto_ru.json")).left(20);
        QTest::newRow("sentences not an array") << QByteArray(R"(["Hello", null, "en"])");
        QTest::newRow("sentence rows of wrong type") << QByteArray(R"([[1, "x", {"a": 2}], null, "en"])");
        QTest::newRow("number") << QByteArray("42");
    }

    void badResponses()
    {
        QFETCH(QByteArray, body);
        const TranslationResult result = GoogleParser::parseFreeResponse(body);
        QCOMPARE(result.error, TranslationError::BadResponse);
        QVERIFY(!result.errorMessage.isEmpty());
        QVERIFY(result.translatedText.isEmpty());
    }

    void commasInsideStringsSurviveSparseFix()
    {
        // The sparse-array fix must not touch commas and brackets inside strings.
        const QByteArray body = "[[[\"a,,b ],[ \\\",, c\",\"x\",,,1]],,\"en\"]";
        const TranslationResult result = GoogleParser::parseFreeResponse(body);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.translatedText, QStringLiteral("a,,b ],[ \",, c"));
        QCOMPARE(result.detectedSourceLang, QStringLiteral("en"));
    }

    void httpStatus_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("400") << 400 << TranslationError::BadResponse;
        QTest::newRow("403") << 403 << TranslationError::RateLimited;
        QTest::newRow("404") << 404 << TranslationError::BadResponse;
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
        QCOMPARE(GoogleParser::errorForFreeHttpStatus(status), error);
    }
};

QTEST_GUILESS_MAIN(TestGoogleParser)
#include "tst_googleparser.moc"
