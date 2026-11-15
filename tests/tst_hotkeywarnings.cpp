#include "core/hotkeywarnings.h"

#include <QTest>

class TestHotkeyWarnings : public QObject
{
    Q_OBJECT

private slots:
    void startsEmpty()
    {
        HotkeyWarnings warnings;
        QVERIFY(warnings.isEmpty());
        QVERIFY(warnings.actions().isEmpty());
        QVERIFY(warnings.sequences().isEmpty());
    }

    void listsInActionOrder()
    {
        HotkeyWarnings warnings;
        warnings.record(3, QStringLiteral("Ctrl+W"));
        warnings.record(0, QStringLiteral("Ctrl+Q"));
        warnings.record(5, QStringLiteral("Ctrl+Shift+Q"));

        QCOMPARE(warnings.actions(), (QList<int>{0, 3, 5}));
        QCOMPARE(warnings.sequences(),
                 (QStringList{QStringLiteral("Ctrl+Q"), QStringLiteral("Ctrl+W"),
                              QStringLiteral("Ctrl+Shift+Q")}));
        QVERIFY(!warnings.isEmpty());
    }

    void forgetDropsOneEntry()
    {
        HotkeyWarnings warnings;
        warnings.record(0, QStringLiteral("Ctrl+Q"));
        warnings.record(3, QStringLiteral("Ctrl+W"));

        warnings.forget(0);

        QCOMPARE(warnings.actions(), (QList<int>{3}));
        QCOMPARE(warnings.sequences(), (QStringList{QStringLiteral("Ctrl+W")}));

        warnings.forget(3);
        QVERIFY(warnings.isEmpty());
    }

    void clearEmptiesEverything()
    {
        HotkeyWarnings warnings;
        warnings.record(0, QStringLiteral("Ctrl+Q"));
        warnings.record(3, QStringLiteral("Ctrl+W"));

        warnings.clear();

        QVERIFY(warnings.isEmpty());
        QVERIFY(warnings.actions().isEmpty());
        QVERIFY(warnings.sequences().isEmpty());
    }

    void recordingTwiceKeepsTheNewerText()
    {
        HotkeyWarnings warnings;
        warnings.record(0, QStringLiteral("Ctrl+Q"));
        warnings.record(0, QStringLiteral("Ctrl+Alt+Q"));

        QCOMPARE(warnings.actions(), (QList<int>{0}));
        QCOMPARE(warnings.sequences(), (QStringList{QStringLiteral("Ctrl+Alt+Q")}));
    }

    void emptyTextIsIgnored()
    {
        HotkeyWarnings warnings;
        warnings.record(0, QString());

        QVERIFY(warnings.isEmpty());
    }

    void duplicateTextsAreListedOnce()
    {
        HotkeyWarnings warnings;
        warnings.record(0, QStringLiteral("Ctrl+Q"));
        warnings.record(3, QStringLiteral("Ctrl+Q"));

        QCOMPARE(warnings.actions(), (QList<int>{0, 3}));
        QCOMPARE(warnings.sequences(), (QStringList{QStringLiteral("Ctrl+Q")}));
    }

    // A successful registration has no signal: the entry stays gone because
    // nothing records it again.
    void forgottenEntryStaysGoneAfterSuccess()
    {
        HotkeyWarnings warnings;
        warnings.record(0, QStringLiteral("Ctrl+Q"));

        warnings.forget(0);

        QVERIFY(warnings.isEmpty());
    }
};

QTEST_MAIN(TestHotkeyWarnings)
#include "tst_hotkeywarnings.moc"
