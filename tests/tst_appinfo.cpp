#include "core/appinfo.h"

#include <QRegularExpression>
#include <QTest>

class TestAppInfo : public QObject
{
    Q_OBJECT

private slots:
    void nameIsStable()
    {
        QCOMPARE(AppInfo::name(), QStringLiteral("qtranslate"));
    }

    void versionIsSemantic()
    {
        static const QRegularExpression semver(QStringLiteral(R"(^\d+\.\d+\.\d+$)"));
        QVERIFY2(semver.match(AppInfo::version()).hasMatch(), qPrintable(AppInfo::version()));
    }

    void userAgentContainsNameAndVersion()
    {
        QCOMPARE(AppInfo::userAgent(), AppInfo::name() + QLatin1Char('/') + AppInfo::version());
    }
};

QTEST_GUILESS_MAIN(TestAppInfo)
#include "tst_appinfo.moc"
