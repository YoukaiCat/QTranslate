#include "platform/autostart.h"

#include <QDir>
#include <QTest>

class TestAutostart : public QObject
{
    Q_OBJECT

private slots:
    void commandQuotesThePath()
    {
        const QString path =
            QDir::toNativeSeparators(QStringLiteral("C:/Program Files/qtranslate/qtranslate.exe"));
        QCOMPARE(Autostart::commandForExecutable(path),
                 QStringLiteral("\"%1\" --minimized").arg(path));
    }

    void commandUsesTheGivenPath()
    {
        const QString path = QDir::toNativeSeparators(QStringLiteral("C:/tools/qtranslate.exe"));
        QVERIFY(Autostart::commandForExecutable(path).startsWith(QLatin1Char('"')));
        QVERIFY(Autostart::commandForExecutable(path).endsWith(QStringLiteral(" --minimized")));
    }
};

QTEST_GUILESS_MAIN(TestAutostart)
#include "tst_autostart.moc"
