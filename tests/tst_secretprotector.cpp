#include "platform/secretprotector.h"

#include <QTest>

class TestSecretProtector : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip()
    {
        const QString plainText =
            QStringLiteral("api-key: \u043f\u0440\u0438\u0432\u0435\u0442 12345");
        const QByteArray protectedData = SecretProtector::protect(plainText);
        QVERIFY(!protectedData.isEmpty());
        QVERIFY(!protectedData.contains(plainText.toUtf8()));
        QCOMPARE(SecretProtector::unprotect(protectedData), plainText);
    }

    void emptyText()
    {
        QVERIFY(SecretProtector::protect(QString()).isEmpty());
        QVERIFY(SecretProtector::unprotect(QByteArray()).isEmpty());
    }

    void corruptedData()
    {
        QVERIFY(SecretProtector::unprotect(QByteArrayLiteral("not dpapi data")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSecretProtector)
#include "tst_secretprotector.moc"
