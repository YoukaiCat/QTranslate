#include "core/languages.h"

#include <QSet>
#include <QTest>

using Languages::Direction;

Q_DECLARE_METATYPE(Languages::Direction)

namespace {
const QString google = QStringLiteral("google");
const QString deepl = QStringLiteral("deepl");
const QString deeplWeb = QStringLiteral("deepl-web");
const QString llm = QStringLiteral("llm");
const QString mozhi = QStringLiteral("mozhi");
const QString mymemory = QStringLiteral("mymemory");
const QString yandexWeb = QStringLiteral("yandex-web");
const QString bingWeb = QStringLiteral("bing-web");
} // namespace

class TestLanguages : public QObject
{
    Q_OBJECT

private slots:
    void codesAreUniqueAndNamed()
    {
        const QStringList codes = Languages::allCodes();
        QVERIFY(codes.size() > 100);
        QCOMPARE(QSet<QString>(codes.begin(), codes.end()).size(), codes.size());
        QVERIFY(!codes.contains(Languages::autoCode()));
        for (const QString &code : codes) {
            QVERIFY2(Languages::displayName(code) != code, qPrintable(code));
            QVERIFY2(!Languages::nativeName(code).isEmpty(), qPrintable(code));
        }
    }

    void displayNames()
    {
        QCOMPARE(Languages::displayName(QStringLiteral("de")), QStringLiteral("German"));
        QCOMPARE(Languages::nativeName(QStringLiteral("de")), QStringLiteral("Deutsch"));
        QCOMPARE(Languages::nativeName(QStringLiteral("ru")), QStringLiteral("Русский"));
        QCOMPARE(Languages::englishName(QStringLiteral("de")), QStringLiteral("German"));
        QCOMPARE(Languages::englishName(QStringLiteral("pt-BR")),
                 QStringLiteral("Portuguese (Brazil)"));
        QCOMPARE(Languages::displayName(Languages::autoCode()), QStringLiteral("Auto-detect"));
        QCOMPARE(Languages::displayName(QStringLiteral("xx")), QStringLiteral("xx"));
        QVERIFY(Languages::nativeName(QStringLiteral("xx")).isEmpty());
    }

    void toServiceCode_data()
    {
        QTest::addColumn<QString>("service");
        QTest::addColumn<QString>("code");
        QTest::addColumn<Direction>("direction");
        QTest::addColumn<QString>("expected");

        QTest::newRow("google en") << google << "en" << Direction::Target << "en";
        QTest::newRow("google pt-BR") << google << "pt-BR" << Direction::Target << "pt";
        QTest::newRow("google pt-PT") << google << "pt-PT" << Direction::Target << "pt-PT";
        QTest::newRow("google zh-Hans") << google << "zh-Hans" << Direction::Source << "zh-CN";
        QTest::newRow("google zh-Hant") << google << "zh-Hant" << Direction::Target << "zh-TW";
        QTest::newRow("google he") << google << "he" << Direction::Target << "iw";
        QTest::newRow("google nb") << google << "nb" << Direction::Target << "no";
        QTest::newRow("google fil") << google << "fil" << Direction::Target << "tl";
        QTest::newRow("deepl en source") << deepl << "en" << Direction::Source << "EN";
        QTest::newRow("deepl en target") << deepl << "en" << Direction::Target << "EN-US";
        QTest::newRow("deepl pt-BR source") << deepl << "pt-BR" << Direction::Source << "PT";
        QTest::newRow("deepl pt-BR target") << deepl << "pt-BR" << Direction::Target << "PT-BR";
        QTest::newRow("deepl zh-Hans target") << deepl << "zh-Hans" << Direction::Target << "ZH-HANS";
        QTest::newRow("deepl ru") << deepl << "ru" << Direction::Target << "RU";
        QTest::newRow("deepl unsupported") << deepl << "hy" << Direction::Target << "";
        QTest::newRow("deepl web en target") << deeplWeb << "en" << Direction::Target << "EN-US";
        QTest::newRow("deepl web pt-BR target") << deeplWeb << "pt-BR" << Direction::Target
                                                  << "PT-BR";
        QTest::newRow("deepl web zh-Hans target") << deeplWeb << "zh-Hans" << Direction::Target
                                                    << "ZH-HANS";
        QTest::newRow("llm pt-BR") << llm << "pt-BR" << Direction::Target << "pt-BR";
        QTest::newRow("llm zh-Hans") << llm << "zh-Hans" << Direction::Source << "zh-Hans";
        QTest::newRow("mozhi en") << mozhi << "en" << Direction::Target << "en";
        QTest::newRow("mozhi ru") << mozhi << "ru" << Direction::Source << "ru";
        QTest::newRow("mozhi pt-BR") << mozhi << "pt-BR" << Direction::Target << "pt";
        QTest::newRow("mozhi pt-PT") << mozhi << "pt-PT" << Direction::Source << "pt-PT";
        QTest::newRow("mozhi zh-Hans") << mozhi << "zh-Hans" << Direction::Target << "zh-CN";
        QTest::newRow("mozhi zh-Hant") << mozhi << "zh-Hant" << Direction::Source << "zh-TW";
        QTest::newRow("mozhi he") << mozhi << "he" << Direction::Target << "iw";
        QTest::newRow("mozhi jv") << mozhi << "jv" << Direction::Source << "jw";
        QTest::newRow("mozhi nb") << mozhi << "nb" << Direction::Target << "no";
        QTest::newRow("mozhi fil") << mozhi << "fil" << Direction::Source << "tl";
        QTest::newRow("mozhi unknown code") << mozhi << "xx" << Direction::Target << "";
        QTest::newRow("mymemory en") << mymemory << "en" << Direction::Target << "en";
        QTest::newRow("mymemory ru") << mymemory << "ru" << Direction::Source << "ru";
        QTest::newRow("mymemory pt-BR") << mymemory << "pt-BR" << Direction::Target << "pt-BR";
        QTest::newRow("mymemory pt-PT") << mymemory << "pt-PT" << Direction::Source << "pt-PT";
        QTest::newRow("mymemory zh-Hans") << mymemory << "zh-Hans" << Direction::Target << "zh-CN";
        QTest::newRow("mymemory zh-Hant") << mymemory << "zh-Hant" << Direction::Source << "zh-TW";
        QTest::newRow("mymemory nb") << mymemory << "nb" << Direction::Target << "no";
        QTest::newRow("mymemory fil") << mymemory << "fil" << Direction::Source << "tl";
        QTest::newRow("mymemory he") << mymemory << "he" << Direction::Target << "he";
        QTest::newRow("mymemory jv") << mymemory << "jv" << Direction::Source << "jv";
        QTest::newRow("mymemory unknown code") << mymemory << "xx" << Direction::Target << "";
        QTest::newRow("yandex web en") << yandexWeb << "en" << Direction::Target << "en";
        QTest::newRow("yandex web ru") << yandexWeb << "ru" << Direction::Target << "ru";
        QTest::newRow("yandex web uk") << yandexWeb << "uk" << Direction::Source << "uk";
        QTest::newRow("yandex web tr") << yandexWeb << "tr" << Direction::Target << "tr";
        QTest::newRow("yandex web de") << yandexWeb << "de" << Direction::Target << "de";
        QTest::newRow("yandex web zh-Hans") << yandexWeb << "zh-Hans" << Direction::Target << "zh";
        QTest::newRow("yandex web zh-Hant") << yandexWeb << "zh-Hant" << Direction::Source << "zh";
        QTest::newRow("yandex web nb") << yandexWeb << "nb" << Direction::Target << "no";
        QTest::newRow("yandex web pt-BR") << yandexWeb << "pt-BR" << Direction::Target << "pt";
        QTest::newRow("yandex web he") << yandexWeb << "he" << Direction::Target << "he";
        QTest::newRow("yandex web pt-PT") << yandexWeb << "pt-PT" << Direction::Source << "";
        QTest::newRow("yandex web fil") << yandexWeb << "fil" << Direction::Target << "";
        QTest::newRow("yandex web jv") << yandexWeb << "jv" << Direction::Source << "";
        QTest::newRow("yandex web ceb") << yandexWeb << "ceb" << Direction::Target << "";
        QTest::newRow("yandex web zu") << yandexWeb << "zu" << Direction::Source << "";
        QTest::newRow("yandex web unknown code") << yandexWeb << "xx" << Direction::Target << "";
        QTest::newRow("yandex web auto") << yandexWeb << "auto" << Direction::Source << "";
        QTest::newRow("bing web en") << bingWeb << "en" << Direction::Target << "en";
        QTest::newRow("bing web ru") << bingWeb << "ru" << Direction::Target << "ru";
        QTest::newRow("bing web de") << bingWeb << "de" << Direction::Source << "de";
        QTest::newRow("bing web zh-Hans") << bingWeb << "zh-Hans" << Direction::Target
                                          << "zh-Hans";
        QTest::newRow("bing web zh-Hant") << bingWeb << "zh-Hant" << Direction::Source
                                          << "zh-Hant";
        QTest::newRow("bing web pt-BR") << bingWeb << "pt-BR" << Direction::Target << "pt";
        QTest::newRow("bing web pt-PT") << bingWeb << "pt-PT" << Direction::Source << "pt-PT";
        QTest::newRow("bing web nb") << bingWeb << "nb" << Direction::Target << "nb";
        QTest::newRow("bing web sr") << bingWeb << "sr" << Direction::Source << "sr-Cyrl";
        QTest::newRow("bing web he") << bingWeb << "he" << Direction::Target << "he";
        QTest::newRow("bing web fil") << bingWeb << "fil" << Direction::Target << "fil";
        QTest::newRow("bing web jv") << bingWeb << "jv" << Direction::Source << "jv";
        QTest::newRow("bing web hmn") << bingWeb << "hmn" << Direction::Target << "hmn";
        QTest::newRow("bing web ceb") << bingWeb << "ceb" << Direction::Target << "";
        QTest::newRow("bing web kk") << bingWeb << "kk" << Direction::Source << "";
        QTest::newRow("bing web pa") << bingWeb << "pa" << Direction::Target << "";
        QTest::newRow("bing web haw") << bingWeb << "haw" << Direction::Source << "";
        QTest::newRow("bing web auto") << bingWeb << "auto" << Direction::Source << "";
        QTest::newRow("unknown code") << google << "xx" << Direction::Target << "";
        QTest::newRow("auto is not a language") << google << "auto" << Direction::Source << "";
        QTest::newRow("unknown service") << "yandex" << "en" << Direction::Target << "";
    }

    void toServiceCode()
    {
        QFETCH(QString, service);
        QFETCH(QString, code);
        QFETCH(Direction, direction);
        QFETCH(QString, expected);
        QCOMPARE(Languages::toServiceCode(service, code, direction), expected);
    }

    void fromServiceCode_data()
    {
        QTest::addColumn<QString>("service");
        QTest::addColumn<QString>("serviceCode");
        QTest::addColumn<Direction>("direction");
        QTest::addColumn<QString>("expected");

        QTest::newRow("google en") << google << "en" << Direction::Source << "en";
        QTest::newRow("google case") << google << "EN" << Direction::Source << "en";
        QTest::newRow("google zh-CN") << google << "zh-CN" << Direction::Source << "zh-Hans";
        QTest::newRow("google zh-TW") << google << "zh-TW" << Direction::Source << "zh-Hant";
        QTest::newRow("google zh") << google << "zh" << Direction::Source << "zh-Hans";
        QTest::newRow("google pt") << google << "pt" << Direction::Source << "pt-BR";
        QTest::newRow("google iw") << google << "iw" << Direction::Source << "he";
        QTest::newRow("google he synonym") << google << "he" << Direction::Source << "he";
        QTest::newRow("google jw") << google << "jw" << Direction::Source << "jv";
        QTest::newRow("google jv synonym") << google << "jv" << Direction::Source << "jv";
        QTest::newRow("google no") << google << "no" << Direction::Source << "nb";
        QTest::newRow("deepl EN source") << deepl << "EN" << Direction::Source << "en";
        QTest::newRow("deepl EN-US target") << deepl << "EN-US" << Direction::Target << "en";
        QTest::newRow("deepl EN-GB target") << deepl << "EN-GB" << Direction::Target << "en";
        QTest::newRow("deepl EN-GB source") << deepl << "EN-GB" << Direction::Source << "en";
        QTest::newRow("deepl PT-BR") << deepl << "PT-BR" << Direction::Target << "pt-BR";
        QTest::newRow("deepl PT-PT") << deepl << "pt-pt" << Direction::Target << "pt-PT";
        QTest::newRow("deepl PT source") << deepl << "PT" << Direction::Source << "pt-BR";
        QTest::newRow("deepl ZH source") << deepl << "ZH" << Direction::Source << "zh-Hans";
        QTest::newRow("deepl ZH-HANT") << deepl << "ZH-HANT" << Direction::Target << "zh-Hant";
        QTest::newRow("deepl web EN-US") << deeplWeb << "EN-US" << Direction::Target << "en";
        QTest::newRow("deepl web ZH-HANS") << deeplWeb << "ZH-HANS" << Direction::Target
                                            << "zh-Hans";
        QTest::newRow("llm en") << llm << "EN" << Direction::Source << "en";
        QTest::newRow("llm pt-BR") << llm << "pt-BR" << Direction::Target << "pt-BR";
        QTest::newRow("mozhi zh-CN") << mozhi << "zh-CN" << Direction::Source << "zh-Hans";
        QTest::newRow("mozhi zh-TW") << mozhi << "zh-TW" << Direction::Target << "zh-Hant";
        QTest::newRow("mozhi iw") << mozhi << "iw" << Direction::Source << "he";
        QTest::newRow("mozhi jw") << mozhi << "jw" << Direction::Source << "jv";
        QTest::newRow("mozhi no") << mozhi << "no" << Direction::Source << "nb";
        QTest::newRow("mozhi tl") << mozhi << "tl" << Direction::Target << "fil";
        QTest::newRow("mozhi pt") << mozhi << "pt" << Direction::Source << "pt-BR";
        QTest::newRow("mozhi pt-PT") << mozhi << "pt-PT" << Direction::Target << "pt-PT";
        QTest::newRow("mymemory zh-CN") << mymemory << "zh-CN" << Direction::Source << "zh-Hans";
        QTest::newRow("mymemory zh-TW") << mymemory << "zh-TW" << Direction::Target << "zh-Hant";
        QTest::newRow("mymemory no") << mymemory << "no" << Direction::Source << "nb";
        QTest::newRow("mymemory tl") << mymemory << "tl" << Direction::Target << "fil";
        QTest::newRow("mymemory pt-BR") << mymemory << "pt-BR" << Direction::Source << "pt-BR";
        QTest::newRow("yandex web zh") << yandexWeb << "zh" << Direction::Source << "zh-Hans";
        QTest::newRow("yandex web no") << yandexWeb << "no" << Direction::Source << "nb";
        QTest::newRow("yandex web pt") << yandexWeb << "pt" << Direction::Source << "pt-BR";
        QTest::newRow("yandex web he") << yandexWeb << "he" << Direction::Source << "he";
        QTest::newRow("yandex web en") << yandexWeb << "en" << Direction::Target << "en";
        QTest::newRow("yandex web ru") << yandexWeb << "ru" << Direction::Source << "ru";
        QTest::newRow("yandex web unknown") << yandexWeb << "xx" << Direction::Source << "";
        QTest::newRow("bing web pt") << bingWeb << "pt" << Direction::Source << "pt-BR";
        QTest::newRow("bing web pt-PT") << bingWeb << "pt-PT" << Direction::Source << "pt-PT";
        QTest::newRow("bing web nb") << bingWeb << "nb" << Direction::Source << "nb";
        QTest::newRow("bing web sr-Cyrl") << bingWeb << "sr-Cyrl" << Direction::Source << "sr";
        QTest::newRow("bing web sr-Latn synonym") << bingWeb << "sr-Latn" << Direction::Source
                                                  << "sr";
        QTest::newRow("bing web zh-Hans") << bingWeb << "zh-Hans" << Direction::Source
                                          << "zh-Hans";
        QTest::newRow("bing web zh-Hant") << bingWeb << "zh-Hant" << Direction::Target
                                          << "zh-Hant";
        QTest::newRow("bing web he") << bingWeb << "he" << Direction::Source << "he";
        QTest::newRow("bing web unknown") << bingWeb << "xx" << Direction::Source << "";
        QTest::newRow("unknown") << google << "xx-unknown" << Direction::Source << "";
        QTest::newRow("empty") << google << "" << Direction::Source << "";
        QTest::newRow("google code for deepl") << deepl << "iw" << Direction::Source << "";
    }

    void fromServiceCode()
    {
        QFETCH(QString, service);
        QFETCH(QString, serviceCode);
        QFETCH(Direction, direction);
        QFETCH(QString, expected);
        QCOMPARE(Languages::fromServiceCode(service, serviceCode, direction), expected);
    }

    void roundTrip()
    {
        // Every supported language maps to a service code and back to itself,
        // except where several internal codes share a service code (DeepL "PT", "ZH").
        for (const QString &service : {google, deepl, deeplWeb, llm, mozhi, mymemory, yandexWeb,
                                       bingWeb}) {
            for (Direction direction : {Direction::Source, Direction::Target}) {
                for (const QString &code : Languages::supported(service, direction)) {
                    const QString serviceCode = Languages::toServiceCode(service, code, direction);
                    QVERIFY2(!serviceCode.isEmpty(), qPrintable(code));
                    const QString back = Languages::fromServiceCode(service, serviceCode, direction);
                    // Yandex shares "zh" between zh-Hans and zh-Hant, so only
                    // zh-Hans can be the reverse mapping.
                    if (service == yandexWeb && code == QLatin1String("zh-Hant"))
                        continue;
                    if (service != google && direction == Direction::Source
                        && (code == QLatin1String("pt-PT") || code == QLatin1String("zh-Hant"))) {
                        continue;
                    }
                    QCOMPARE(back, code);
                }
            }
        }
    }

    void supportedLists()
    {
        const QStringList googleTargets = Languages::supported(google, Direction::Target);
        const QStringList deeplTargets = Languages::supported(deepl, Direction::Target);
        QCOMPARE(googleTargets, Languages::allCodes());
        QVERIFY(deeplTargets.size() < googleTargets.size());
        QVERIFY(deeplTargets.contains(QStringLiteral("en")));
        QVERIFY(deeplTargets.contains(QStringLiteral("pt-PT")));
        QVERIFY(!deeplTargets.contains(QStringLiteral("hy")));
        QCOMPARE(Languages::supported(deeplWeb, Direction::Target), deeplTargets);
        QCOMPARE(Languages::supported(deeplWeb, Direction::Source),
                 Languages::supported(deepl, Direction::Source));
        QCOMPARE(Languages::supported(llm, Direction::Target), googleTargets);
        QCOMPARE(Languages::supported(llm, Direction::Source), googleTargets);
        const QStringList mymemoryCatalogue = Languages::supported(mymemory, Direction::Target);
        QCOMPARE(mymemoryCatalogue, googleTargets);
        QCOMPARE(Languages::supported(mymemory, Direction::Source), googleTargets);
        QVERIFY(mymemoryCatalogue.contains(QStringLiteral("ru")));
        QVERIFY(mymemoryCatalogue.contains(QStringLiteral("pt-BR")));
        QVERIFY(mymemoryCatalogue.contains(QStringLiteral("zh-Hans")));
        QVERIFY(!mymemoryCatalogue.contains(Languages::autoCode()));
        const QStringList mozhiCatalogue = Languages::supported(mozhi, Direction::Target);
        QCOMPARE(mozhiCatalogue, googleTargets);
        QCOMPARE(Languages::supported(mozhi, Direction::Source), googleTargets);
        QVERIFY(mozhiCatalogue.contains(QStringLiteral("ru")));
        QVERIFY(mozhiCatalogue.contains(QStringLiteral("pt-BR")));
        QVERIFY(mozhiCatalogue.contains(QStringLiteral("zh-Hans")));
        QVERIFY(!mozhiCatalogue.contains(Languages::autoCode()));
        for (const QString &code : deeplTargets)
            QVERIFY2(googleTargets.contains(code), qPrintable(code));
        const QStringList yandexWebSources = Languages::supported(yandexWeb, Direction::Source);
        QCOMPARE(Languages::supported(yandexWeb, Direction::Target), yandexWebSources);
        QVERIFY(!yandexWebSources.isEmpty());
        for (const QString &code : {QStringLiteral("ru"), QStringLiteral("uk"),
                                    QStringLiteral("tr"), QStringLiteral("zh-Hans"),
                                    QStringLiteral("he"), QStringLiteral("nb"),
                                    QStringLiteral("pt-BR")}) {
            QVERIFY2(yandexWebSources.contains(code), qPrintable(code));
        }
        for (const QString &code : {Languages::autoCode(), QStringLiteral("fil"),
                                    QStringLiteral("jv"), QStringLiteral("pt-PT")}) {
            QVERIFY2(!yandexWebSources.contains(code), qPrintable(code));
        }
        const QStringList bingWebSources = Languages::supported(bingWeb, Direction::Source);
        QCOMPARE(Languages::supported(bingWeb, Direction::Target), bingWebSources);
        QVERIFY(!bingWebSources.isEmpty());
        for (const QString &code :
             {QStringLiteral("ru"), QStringLiteral("uk"), QStringLiteral("tr"),
              QStringLiteral("zh-Hans"), QStringLiteral("zh-Hant"), QStringLiteral("pt-BR"),
              QStringLiteral("pt-PT"), QStringLiteral("nb"), QStringLiteral("sr"),
              QStringLiteral("he"), QStringLiteral("fil"), QStringLiteral("jv")}) {
            QVERIFY2(bingWebSources.contains(code), qPrintable(code));
        }
        for (const QString &code :
             {Languages::autoCode(), QStringLiteral("ceb"), QStringLiteral("kk"),
              QStringLiteral("pa"), QStringLiteral("haw")}) {
            QVERIFY2(!bingWebSources.contains(code), qPrintable(code));
        }
        QVERIFY(Languages::supported(QStringLiteral("yandex"), Direction::Target).isEmpty());
    }

    void closest_data()
    {
        QTest::addColumn<QString>("service");
        QTest::addColumn<QString>("code");
        QTest::addColumn<Direction>("direction");
        QTest::addColumn<QString>("expected");

        QTest::newRow("exact") << deepl << "de" << Direction::Target << "de";
        QTest::newRow("same base pt") << deepl << "pt" << Direction::Target << "pt-BR";
        QTest::newRow("same base zh") << deepl << "zh" << Direction::Target << "zh-Hans";
        QTest::newRow("deepl web closest") << deeplWeb << "pt" << Direction::Target << "pt-BR";
        QTest::newRow("google keeps fil") << google << "fil" << Direction::Target << "fil";
        QTest::newRow("auto") << deepl << Languages::autoCode() << Direction::Source
                              << Languages::autoCode();
        QTest::newRow("unknown service") << "yandex" << "en" << Direction::Target << "";
    }

    void closest()
    {
        QFETCH(QString, service);
        QFETCH(QString, code);
        QFETCH(Direction, direction);
        QFETCH(QString, expected);
        QCOMPARE(Languages::closest(service, code, direction), expected);
    }

    void closestFallsBackToSupported()
    {
        const QString result = Languages::closest(deepl, QStringLiteral("fil"),
                                                  Direction::Target);
        QVERIFY(!result.isEmpty());
        QVERIFY(Languages::supported(deepl, Direction::Target).contains(result));
    }

    void forLocale_data()
    {
        QTest::addColumn<QString>("locale");
        QTest::addColumn<QString>("expected");

        QTest::newRow("ru_RU") << "ru_RU" << "ru";
        QTest::newRow("en_US") << "en_US" << "en";
        QTest::newRow("en_GB") << "en_GB" << "en";
        QTest::newRow("de_AT") << "de_AT" << "de";
        QTest::newRow("pt_BR") << "pt_BR" << "pt-BR";
        QTest::newRow("pt_PT") << "pt_PT" << "pt-PT";
        QTest::newRow("pt") << "pt" << "pt-BR";
        QTest::newRow("zh_CN") << "zh_CN" << "zh-Hans";
        QTest::newRow("zh_TW") << "zh_TW" << "zh-Hant";
        QTest::newRow("zh_HK") << "zh_HK" << "zh-Hant";
        QTest::newRow("nb_NO") << "nb_NO" << "nb";
        QTest::newRow("nn_NO") << "nn_NO" << "nb";
        QTest::newRow("he_IL") << "he_IL" << "he";
        QTest::newRow("fil_PH") << "fil_PH" << "fil";
        QTest::newRow("C") << "C" << "en";
        QTest::newRow("unsupported language") << "kl_GL" << "en";
    }

    void forLocale()
    {
        QFETCH(QString, locale);
        QFETCH(QString, expected);
        QCOMPARE(Languages::forLocale(QLocale(locale)), expected);
    }

    void systemLanguageIsKnown()
    {
        QVERIFY(Languages::isKnown(Languages::systemLanguage()));
    }
};

QTEST_GUILESS_MAIN(TestLanguages)
#include "tst_languages.moc"
