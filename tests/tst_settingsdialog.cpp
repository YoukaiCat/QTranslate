#include "core/languages.h"
#include "core/settings.h"
#include "ui/languagecombobox.h"
#include "ui/settingsdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QKeySequenceEdit>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>

class TestSettingsDialog : public QObject
{
    Q_OBJECT

private slots:
    void showsCurrentValues()
    {
        AppSettings settings;
        settings.general.autostart = true;
        settings.general.startMinimized = false;
        settings.general.restoreClipboard = false;
        settings.translation.sourceLang = QStringLiteral("en");
        settings.translation.primaryLang = QStringLiteral("de");
        settings.translation.secondaryLang = QStringLiteral("fr");
        settings.translation.maxLength = 5000;
        settings.translation.autoTranslateDelayMs = 0;
        settings.popup.autoCloseOnFocusLoss = false;

        SettingsDialog dialog(settings, {QStringLiteral("en"), QStringLiteral("ru")},
                              {QStringLiteral("de"), QStringLiteral("en"),
                               QStringLiteral("fr"), QStringLiteral("ru")},
                              true);

        auto *autostart = dialog.findChild<QCheckBox *>(QStringLiteral("autostart"));
        auto *startMinimized =
            dialog.findChild<QCheckBox *>(QStringLiteral("startMinimized"));
        auto *restoreClipboard =
            dialog.findChild<QCheckBox *>(QStringLiteral("restoreClipboard"));
        auto *autoClose = dialog.findChild<QCheckBox *>(QStringLiteral("autoClosePopup"));
        auto *popupHotkey =
            dialog.findChild<QKeySequenceEdit *>(QStringLiteral("popupHotkey"));
        auto *mainHotkey =
            dialog.findChild<QKeySequenceEdit *>(QStringLiteral("mainWindowHotkey"));
        auto *source =
            dialog.findChild<LanguageComboBox *>(QStringLiteral("sourceLanguage"));
        auto *primary =
            dialog.findChild<LanguageComboBox *>(QStringLiteral("primaryLanguage"));
        auto *secondary =
            dialog.findChild<LanguageComboBox *>(QStringLiteral("secondaryLanguage"));
        auto *maxLength = dialog.findChild<QSpinBox *>(QStringLiteral("maxLength"));
        auto *delay =
            dialog.findChild<QSpinBox *>(QStringLiteral("autoTranslateDelay"));

        QVERIFY(autostart);
        QVERIFY(startMinimized);
        QVERIFY(restoreClipboard);
        QVERIFY(autoClose);
        QVERIFY(popupHotkey);
        QVERIFY(mainHotkey);
        QVERIFY(source);
        QVERIFY(primary);
        QVERIFY(secondary);
        QVERIFY(maxLength);
        QVERIFY(delay);

        QVERIFY(autostart->isChecked());
        QVERIFY(!startMinimized->isChecked());
        QVERIFY(!restoreClipboard->isChecked());
        QVERIFY(!autoClose->isChecked());
        QCOMPARE(popupHotkey->keySequence(), QKeySequence(Qt::CTRL | Qt::Key_Q));
        QCOMPARE(mainHotkey->keySequence(), QKeySequence(Qt::CTRL | Qt::Key_W));
        QCOMPARE(source->currentCode(), QStringLiteral("en"));
        QCOMPARE(primary->currentCode(), QStringLiteral("de"));
        QCOMPARE(secondary->currentCode(), QStringLiteral("fr"));
        QCOMPARE(maxLength->value(), 5000);
        QCOMPARE(delay->value(), 0);
    }

    void returnsEditedValues()
    {
        AppSettings settings;
        settings.google.apiKey = QStringLiteral("keep-me");
        settings.network.proxyHost = QStringLiteral("proxy.local");
        SettingsDialog dialog(settings, {QStringLiteral("en"), QStringLiteral("ru")},
                              {QStringLiteral("de"), QStringLiteral("en"),
                               QStringLiteral("fr"), QStringLiteral("ru")},
                              true);

        dialog.findChild<QCheckBox *>(QStringLiteral("autostart"))->setChecked(true);
        dialog.findChild<QCheckBox *>(QStringLiteral("startMinimized"))->setChecked(false);
        dialog.findChild<QCheckBox *>(QStringLiteral("restoreClipboard"))->setChecked(false);
        dialog.findChild<QCheckBox *>(QStringLiteral("autoClosePopup"))->setChecked(false);
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("popupHotkey"))
            ->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+T")));
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("mainWindowHotkey"))
            ->setKeySequence(QKeySequence());
        dialog.findChild<LanguageComboBox *>(QStringLiteral("sourceLanguage"))
            ->setCurrentCode(QStringLiteral("en"));
        dialog.findChild<LanguageComboBox *>(QStringLiteral("primaryLanguage"))
            ->setCurrentCode(QStringLiteral("fr"));
        dialog.findChild<LanguageComboBox *>(QStringLiteral("secondaryLanguage"))
            ->setCurrentCode(QStringLiteral("de"));
        dialog.findChild<QSpinBox *>(QStringLiteral("maxLength"))->setValue(7000);
        dialog.findChild<QSpinBox *>(QStringLiteral("autoTranslateDelay"))->setValue(1200);

        const AppSettings result = dialog.settings();
        QVERIFY(result.general.autostart);
        QVERIFY(!result.general.startMinimized);
        QVERIFY(!result.general.restoreClipboard);
        QVERIFY(!result.popup.autoCloseOnFocusLoss);
        QCOMPARE(result.hotkeys.popup, QKeySequence(QStringLiteral("Ctrl+Alt+T")));
        QVERIFY(result.hotkeys.mainWindow.isEmpty());
        QCOMPARE(result.translation.sourceLang, QStringLiteral("en"));
        QCOMPARE(result.translation.primaryLang, QStringLiteral("fr"));
        QCOMPARE(result.translation.secondaryLang, QStringLiteral("de"));
        QCOMPARE(result.translation.maxLength, 7000);
        QCOMPARE(result.translation.autoTranslateDelayMs, 1200);
        QCOMPARE(result.google.apiKey, QStringLiteral("keep-me"));
        QCOMPARE(result.network.proxyHost, QStringLiteral("proxy.local"));
    }

    void rejectsDuplicateHotkeys()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en"), QStringLiteral("ru")},
                              {QStringLiteral("en"), QStringLiteral("ru")}, true);
        const QKeySequence duplicate(QStringLiteral("Ctrl+Alt+T"));
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("popupHotkey"))
            ->setKeySequence(duplicate);
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("mainWindowHotkey"))
            ->setKeySequence(duplicate);

        QString error;
        QVERIFY(!dialog.validate(&error));
        QVERIFY(!error.isEmpty());
    }

    void rejectsMultiChordHotkey()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en"), QStringLiteral("ru")},
                              {QStringLiteral("en"), QStringLiteral("ru")}, true);
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("popupHotkey"))
            ->setKeySequence(QKeySequence(QStringLiteral("Ctrl+K, Ctrl+C")));

        QString error;
        QVERIFY(!dialog.validate(&error));
        QVERIFY(!error.isEmpty());
    }

    void rejectsModifierOnlyHotkey()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en"), QStringLiteral("ru")},
                              {QStringLiteral("en"), QStringLiteral("ru")}, true);
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("popupHotkey"))
            ->setKeySequence(QKeySequence(int(Qt::CTRL)));

        QString error;
        QVERIFY(!dialog.validate(&error));
        QVERIFY(!error.isEmpty());
    }

    void allowsEmptyHotkeys()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en"), QStringLiteral("ru")},
                              {QStringLiteral("en"), QStringLiteral("ru")}, true);
        for (const char *name : {"popupHotkey", "mainWindowHotkey", "showMainWindowHotkey"}) {
            dialog.findChild<QKeySequenceEdit *>(QString::fromLatin1(name))
                ->setKeySequence(QKeySequence());
        }
        QString error;
        QVERIFY(dialog.validate(&error));
        QVERIFY(error.isEmpty());
    }

    void rejectsEqualLanguages()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en"), QStringLiteral("ru")},
                              {QStringLiteral("de"), QStringLiteral("en"),
                               QStringLiteral("ru")},
                              true);
        dialog.findChild<LanguageComboBox *>(QStringLiteral("primaryLanguage"))
            ->setCurrentCode(QStringLiteral("ru"));
        dialog.findChild<LanguageComboBox *>(QStringLiteral("secondaryLanguage"))
            ->setCurrentCode(QStringLiteral("ru"));

        QString error;
        QVERIFY(!dialog.validate(&error));
        QVERIFY(!error.isEmpty());
    }

    void hotkeyEditingSignal()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en"), QStringLiteral("ru")},
                              {QStringLiteral("en"), QStringLiteral("ru")}, true);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.activateWindow();
        QSignalSpy editing(&dialog, &SettingsDialog::hotkeyEditingChanged);

        auto *hotkey = dialog.findChild<QKeySequenceEdit *>(QStringLiteral("popupHotkey"));
        hotkey->setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(editing.count() > 0);
        QCOMPARE(editing.last().at(0).toBool(), true);

        dialog.findChild<LanguageComboBox *>(QStringLiteral("primaryLanguage"))->setFocus();
        QTRY_VERIFY(editing.count() > 1);
        QCOMPARE(editing.last().at(0).toBool(), false);

        dialog.hide();
    }
};

QTEST_MAIN(TestSettingsDialog)
#include "tst_settingsdialog.moc"
