#include "core/languages.h"
#include "core/settings.h"
#include "ui/languagecombobox.h"
#include "ui/settingsdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
        dialog.findChild<QComboBox *>(QStringLiteral("uiLanguage"))
            ->setCurrentIndex(
                dialog.findChild<QComboBox *>(QStringLiteral("uiLanguage"))
                    ->findData(QStringLiteral("en")));
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
        QCOMPARE(result.general.uiLanguage, QStringLiteral("en"));
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

    void rejectsIncompleteManualProxy()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("en"), QStringLiteral("ru")}, true);
        auto *mode = dialog.findChild<QComboBox *>(QStringLiteral("proxyMode"));
        auto *host = dialog.findChild<QLineEdit *>(QStringLiteral("proxyHost"));
        auto *port = dialog.findChild<QSpinBox *>(QStringLiteral("proxyPort"));
        QVERIFY(mode && host && port);

        mode->setCurrentIndex(mode->findData(QStringLiteral("manual")));
        QString error;
        QVERIFY(!dialog.validate(&error));
        QVERIFY(!error.isEmpty());

        host->setText(QStringLiteral("proxy.local"));
        port->setValue(3128);
        QVERIFY(dialog.validate(&error));
    }

    void servicesTab()
    {
        AppSettings settings;
        settings.google.mode = QStringLiteral("cloud");
        settings.google.apiKey = QStringLiteral("google-key");
        settings.deepl.apiKey = QStringLiteral("deepl-key");
        settings.deepl.englishVariant = QStringLiteral("EN-GB");

        SettingsDialog dialog(settings, {QStringLiteral("en"), QStringLiteral("ru")},
                              {QStringLiteral("de"), QStringLiteral("en")}, true);

        auto *googleMode = dialog.findChild<QComboBox *>(QStringLiteral("googleMode"));
        auto *googleKey = dialog.findChild<QLineEdit *>(QStringLiteral("googleApiKey"));
        auto *deepLKey = dialog.findChild<QLineEdit *>(QStringLiteral("deepLApiKey"));
        auto *variant =
            dialog.findChild<QComboBox *>(QStringLiteral("deeplEnglishVariant"));
        QVERIFY(googleMode);
        QVERIFY(googleKey);
        QVERIFY(deepLKey);
        QVERIFY(variant);

        QCOMPARE(googleMode->currentData().toString(), QStringLiteral("cloud"));
        QCOMPARE(googleKey->text(), QStringLiteral("google-key"));
        QVERIFY(googleKey->isEnabled());
        QCOMPARE(deepLKey->text(), QStringLiteral("deepl-key"));
        QCOMPARE(variant->currentData().toString(), QStringLiteral("EN-GB"));

        googleMode->setCurrentIndex(googleMode->findData(QStringLiteral("free")));
        QVERIFY(!googleKey->isEnabled());
        googleMode->setCurrentIndex(googleMode->findData(QStringLiteral("cloud")));
        QVERIFY(googleKey->isEnabled());

        googleKey->setText(QStringLiteral("new-google-key"));
        deepLKey->setText(QStringLiteral("new-deepl-key"));
        variant->setCurrentIndex(variant->findData(QStringLiteral("EN-US")));

        const AppSettings result = dialog.settings();
        QCOMPARE(result.google.mode, QStringLiteral("cloud"));
        QCOMPARE(result.google.apiKey, QStringLiteral("new-google-key"));
        QCOMPARE(result.deepl.apiKey, QStringLiteral("new-deepl-key"));
        QCOMPARE(result.deepl.englishVariant, QStringLiteral("EN-US"));
    }

    void deepLKeyCheck()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en"), QStringLiteral("ru")},
                              {QStringLiteral("en"), QStringLiteral("ru")}, true);
        auto *key = dialog.findChild<QLineEdit *>(QStringLiteral("deepLApiKey"));
        auto *button = dialog.findChild<QPushButton *>(QStringLiteral("checkDeepLKey"));
        auto *status = dialog.findChild<QLabel *>(QStringLiteral("deeplKeyStatus"));
        QVERIFY(key);
        QVERIFY(button);
        QVERIFY(status);
        key->setText(QStringLiteral("typed-key:fx"));

        QSignalSpy requested(&dialog, &SettingsDialog::deepLKeyCheckRequested);
        button->click();
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.first().first().toString(), QStringLiteral("typed-key:fx"));
        QVERIFY(!button->isEnabled());

        dialog.showDeepLKeyCheckResult(true, QStringLiteral("Used 10 of 500 characters."));
        QVERIFY(button->isEnabled());
        QVERIFY(status->text().contains(QStringLiteral("Used 10")));
    }

    void networkTab()
    {
        AppSettings settings;
        settings.network.proxyMode = QStringLiteral("manual");
        settings.network.proxyType = QStringLiteral("socks5");
        settings.network.proxyHost = QStringLiteral("127.0.0.1");
        settings.network.proxyPort = 1080;
        settings.network.proxyAuth = true;
        settings.network.proxyUser = QStringLiteral("proxy-user");
        settings.network.proxyPassword = QStringLiteral("proxy-password");
        settings.network.timeoutMs = 12000;

        SettingsDialog dialog(settings, {QStringLiteral("en")}, {QStringLiteral("en")}, true);
        auto *mode = dialog.findChild<QComboBox *>(QStringLiteral("proxyMode"));
        auto *type = dialog.findChild<QComboBox *>(QStringLiteral("proxyType"));
        auto *host = dialog.findChild<QLineEdit *>(QStringLiteral("proxyHost"));
        auto *port = dialog.findChild<QSpinBox *>(QStringLiteral("proxyPort"));
        auto *auth = dialog.findChild<QCheckBox *>(QStringLiteral("proxyAuth"));
        auto *user = dialog.findChild<QLineEdit *>(QStringLiteral("proxyUser"));
        auto *password = dialog.findChild<QLineEdit *>(QStringLiteral("proxyPassword"));
        auto *timeout = dialog.findChild<QSpinBox *>(QStringLiteral("networkTimeout"));
        QVERIFY(mode && type && host && port && auth && user && password && timeout);

        QCOMPARE(mode->currentData().toString(), QStringLiteral("manual"));
        QCOMPARE(type->currentData().toString(), QStringLiteral("socks5"));
        QCOMPARE(host->text(), QStringLiteral("127.0.0.1"));
        QCOMPARE(port->value(), 1080);
        QVERIFY(auth->isChecked());
        QCOMPARE(user->text(), QStringLiteral("proxy-user"));
        QCOMPARE(password->text(), QStringLiteral("proxy-password"));
        QCOMPARE(timeout->value(), 12000);
        QVERIFY(host->isEnabled());
        QVERIFY(user->isEnabled());

        mode->setCurrentIndex(mode->findData(QStringLiteral("system")));
        QVERIFY(!host->isEnabled());
        QVERIFY(!auth->isEnabled());
        QVERIFY(!user->isEnabled());

        mode->setCurrentIndex(mode->findData(QStringLiteral("manual")));
        auth->setChecked(false);
        QVERIFY(!user->isEnabled());
        host->setText(QStringLiteral("proxy.local"));
        port->setValue(3128);
        type->setCurrentIndex(type->findData(QStringLiteral("http")));

        const AppSettings result = dialog.settings();
        QCOMPARE(result.network.proxyMode, QStringLiteral("manual"));
        QCOMPARE(result.network.proxyType, QStringLiteral("http"));
        QCOMPARE(result.network.proxyHost, QStringLiteral("proxy.local"));
        QCOMPARE(result.network.proxyPort, 3128);
        QVERIFY(!result.network.proxyAuth);
        QCOMPARE(result.network.proxyUser, QStringLiteral("proxy-user"));
        QCOMPARE(result.network.proxyPassword, QStringLiteral("proxy-password"));
        QCOMPARE(result.network.timeoutMs, 12000);
    }

    void proxyTest()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")}, {QStringLiteral("en")},
                              true);
        auto *button = dialog.findChild<QPushButton *>(QStringLiteral("testProxy"));
        auto *status = dialog.findChild<QLabel *>(QStringLiteral("proxyTestStatus"));
        QVERIFY(button);
        QVERIFY(status);

        QSignalSpy requested(&dialog, &SettingsDialog::proxyTestRequested);
        button->click();
        QCOMPARE(requested.size(), 1);
        QVERIFY(!button->isEnabled());
        QVERIFY(!status->text().isEmpty());

        dialog.showProxyTestResult(false, QStringLiteral("Proxy authentication failed"));
        QVERIFY(button->isEnabled());
        QVERIFY(status->text().contains(QStringLiteral("authentication")));
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
