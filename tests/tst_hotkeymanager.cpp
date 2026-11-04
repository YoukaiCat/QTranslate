#include "platform/hotkeymanager.h"

#include <QSignalSpy>
#include <QTest>

class TestHotkeyManager : public QObject
{
    Q_OBJECT

private slots:
    void registerAndUnregister()
    {
        HotkeyManager manager;
        QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
        const QKeySequence sequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F24);

        QVERIFY(manager.registerHotkey(HotkeyAction::TranslateToMainWindow, sequence));
        QCOMPARE(manager.hotkey(HotkeyAction::TranslateToMainWindow), sequence);
        QCOMPARE(failures.count(), 0);

        manager.unregisterHotkey(HotkeyAction::TranslateToMainWindow);
        QVERIFY(manager.hotkey(HotkeyAction::TranslateToMainWindow).isEmpty());
    }

    void conflictIsReported()
    {
        HotkeyManager first;
        HotkeyManager second;
        const QKeySequence sequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F23);

        QVERIFY(first.registerHotkey(HotkeyAction::TranslateToMainWindow, sequence));

        QSignalSpy failures(&second, &HotkeyManager::registrationFailed);
        QVERIFY(!second.registerHotkey(HotkeyAction::TranslateToPopup, sequence));
        QCOMPARE(failures.count(), 1);
        const QList<QVariant> failure = failures.first();
        QVERIFY(failure.at(0).value<HotkeyAction>() == HotkeyAction::TranslateToPopup);
        QCOMPARE(failure.at(1).value<QKeySequence>(), sequence);
        QVERIFY(!failure.at(2).toString().isEmpty());
    }

    void suspensionReleasesAndRestores()
    {
        HotkeyManager first;
        QSignalSpy failures(&first, &HotkeyManager::registrationFailed);
        const QKeySequence sequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F22);

        QVERIFY(first.registerHotkey(HotkeyAction::TranslateToPopup, sequence));
        first.setSuspended(true);
        QVERIFY(first.isSuspended());

        {
            HotkeyManager other;
            QVERIFY(other.registerHotkey(HotkeyAction::TranslateToPopup, sequence));
        } // the other manager releases the sequence

        first.setSuspended(false);
        QVERIFY(!first.isSuspended());
        // The sequence was free again, so resuming must succeed.
        QCOMPARE(failures.count(), 0);
        QCOMPARE(first.hotkey(HotkeyAction::TranslateToPopup), sequence);
    }

    void unsupportedSequenceIsReported()
    {
        HotkeyManager manager;
        QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
        QVERIFY(!manager.registerHotkey(HotkeyAction::ShowMainWindow, QKeySequence()));
        QCOMPARE(failures.count(), 1);
    }

    void suspendedRegistrationIsDeferred()
    {
        HotkeyManager manager;
        const QKeySequence sequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F21);
        manager.setSuspended(true);
        QVERIFY(manager.registerHotkey(HotkeyAction::ShowMainWindow, sequence));
        QCOMPARE(manager.hotkey(HotkeyAction::ShowMainWindow), sequence);
        manager.setSuspended(false);
        QCOMPARE(manager.hotkey(HotkeyAction::ShowMainWindow), sequence);
    }
};

QTEST_GUILESS_MAIN(TestHotkeyManager)
#include "tst_hotkeymanager.moc"
