#include "core/uilanguages.h"

#include <QSet>
#include <QTest>

class TestUiLanguages : public QObject
{
    Q_OBJECT

private slots:
    void normalizesLanguageTags();
    void resolvesSystemLocales();
    void exposesResourcesAndNames();
    void tableIsCompleteAndUnique();
};

void TestUiLanguages::normalizesLanguageTags()
{
    QCOMPARE(UiLanguages::normalize(QStringLiteral("de")), QStringLiteral("de"));
    QCOMPARE(UiLanguages::normalize(QStringLiteral("de-DE")), QStringLiteral("de"));
    QCOMPARE(UiLanguages::normalize(QStringLiteral("ru_RU")), QStringLiteral("ru"));
    QCOMPARE(UiLanguages::normalize(QStringLiteral("pt-PT")), QStringLiteral("pt-BR"));
    QCOMPARE(UiLanguages::normalize(QStringLiteral("zh-CN")), QStringLiteral("zh-Hans"));
    QCOMPARE(UiLanguages::normalize(QStringLiteral("zh-Hans")), QStringLiteral("zh-Hans"));
    QCOMPARE(UiLanguages::normalize(QStringLiteral("zh-Hant")), QStringLiteral("en"));
    QCOMPARE(UiLanguages::normalize(QStringLiteral("xx")), QStringLiteral("en"));
    QCOMPARE(UiLanguages::canonical(QStringLiteral("de-DE")), QStringLiteral("de"));
    QCOMPARE(UiLanguages::canonical(QStringLiteral("pt-PT")), QStringLiteral("pt-BR"));
    QVERIFY(UiLanguages::canonical(QStringLiteral("zh-Hant")).isEmpty());
    QVERIFY(UiLanguages::canonical(QStringLiteral("xx")).isEmpty());
}

void TestUiLanguages::resolvesSystemLocales()
{
    const QString systemDefault = UiLanguages::systemDefault();
    QCOMPARE(UiLanguages::normalize(QStringLiteral("system")), systemDefault);
    QCOMPARE(UiLanguages::normalize(QString()), systemDefault);
    QVERIFY(UiLanguages::isSupported(systemDefault));
}

void TestUiLanguages::exposesResourcesAndNames()
{
    QVERIFY(UiLanguages::resourceFor(QStringLiteral("en")).isEmpty());
    QCOMPARE(UiLanguages::resourceFor(QStringLiteral("ru")),
             QStringLiteral(":/i18n/qtranslate_ru_RU.qm"));
    QCOMPARE(UiLanguages::resourceFor(QStringLiteral("pt-BR")),
             QStringLiteral(":/i18n/qtranslate_pt_BR.qm"));
    QCOMPARE(UiLanguages::resourceFor(QStringLiteral("zh-Hans")),
             QStringLiteral(":/i18n/qtranslate_zh_CN.qm"));
    QCOMPARE(UiLanguages::nativeName(QStringLiteral("de")), QStringLiteral("Deutsch"));
    QCOMPARE(UiLanguages::nativeName(QStringLiteral("unknown")), QStringLiteral("unknown"));
    QCOMPARE(UiLanguages::qtTranslationFile(QStringLiteral("de")),
             QStringLiteral("qt_de.qm"));
    QCOMPARE(UiLanguages::qtTranslationFile(QStringLiteral("pt-BR")),
             QStringLiteral("qt_pt_BR.qm"));
    QCOMPARE(UiLanguages::qtTranslationFile(QStringLiteral("zh-Hans")),
             QStringLiteral("qt_zh_CN.qm"));
    QVERIFY(UiLanguages::qtTranslationFile(QStringLiteral("en")).isEmpty());
    QVERIFY(UiLanguages::qtTranslationFile(QStringLiteral("hi")).isEmpty());
}

void TestUiLanguages::tableIsCompleteAndUnique()
{
    QSet<QString> codes;
    for (const UiLanguages::Info &language : UiLanguages::all()) {
        QVERIFY(!language.code.isEmpty());
        QVERIFY(!language.nativeName.isEmpty());
        QVERIFY(!codes.contains(language.code));
        codes.insert(language.code);
        QVERIFY(UiLanguages::isSupported(language.code));
    }
    QCOMPARE(codes.size(), 15);
    QVERIFY(UiLanguages::isSupported(QStringLiteral("system")));
    QVERIFY(!UiLanguages::isSupported(QStringLiteral("xx")));
}

QTEST_GUILESS_MAIN(TestUiLanguages)
#include "tst_uilanguages.moc"
