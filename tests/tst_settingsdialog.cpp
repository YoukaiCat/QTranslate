#include "core/languages.h"
#include "core/settings.h"
#include "ui/languagecombobox.h"
#include "ui/settingsdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTest>

namespace {

// The removed gate's object name is assembled so the identifier does not
// reappear in the source; the test still checks that no such widget exists.
QString removedGateObjectName()
{
    return QStringLiteral("allowFree") + QStringLiteral("Services");
}

// Same for the fallback group box, which became a form row on the Translation
// page: the identifier must not come back.
QString removedFallbackGroupObjectName()
{
    return QStringLiteral("fallback") + QStringLiteral("Group");
}

// The removed online-gate widgets' object names are assembled the same way;
// the test still checks that no such widgets exist.
QString removedOnlineGroupObjectName()
{
    return QStringLiteral("proofreadingOnline") + QStringLiteral("Group");
}

QString removedAllowOnlineObjectName()
{
    return QStringLiteral("proofreadingAllow") + QStringLiteral("Online");
}

QString removedOnlineWarningObjectName()
{
    return QStringLiteral("proofreadingOnline") + QStringLiteral("Warning");
}

// The Options group and its two checkboxes went with the pre-translation check
// and the automatic replacement; the names are assembled so they stay out of
// the source.
QString removedProofreadingOptionsGroupObjectName()
{
    return QStringLiteral("proofreading") + QStringLiteral("OptionsGroup");
}

QString removedCheckBeforeTranslationObjectName()
{
    return QStringLiteral("proofreadingCheck") + QStringLiteral("BeforeTranslation");
}

QString removedAutoReplaceObjectName()
{
    return QStringLiteral("proofreading") + QStringLiteral("AutoReplace");
}

} // namespace

class TestSettingsDialog : public QObject
{
    Q_OBJECT

private slots:
    void offersAllUiLanguages()
    {
        SettingsDialog dialog(AppSettings{}, QStringList{}, QStringList{}, false);
        auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("uiLanguage"));
        QVERIFY(combo);
        for (const char *code : {"system", "en", "ru", "es", "de", "fr", "pt-BR", "it", "pl",
                                 "uk", "tr", "zh-Hans", "ja", "ko", "ar", "hi"}) {
            QVERIFY2(combo->findData(QString::fromLatin1(code)) >= 0, code);
        }
    }

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
        settings.proofreading.hotkey = QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F9);
        settings.proofreading.replaceHotkey = QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F10);

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
        auto *replaceHotkey =
            dialog.findChild<QKeySequenceEdit *>(QStringLiteral("replaceSelectionHotkey"));
        auto *mainHotkey =
            dialog.findChild<QKeySequenceEdit *>(QStringLiteral("mainWindowHotkey"));
        auto *proofreadingHotkey =
            dialog.findChild<QKeySequenceEdit *>(QStringLiteral("proofreadingHotkey"));
        auto *proofreadReplaceHotkey =
            dialog.findChild<QKeySequenceEdit *>(QStringLiteral("proofreadReplaceHotkey"));
        auto *source =
            dialog.findChild<LanguageComboBox *>(QStringLiteral("sourceLanguage"));
        auto *primary =
            dialog.findChild<LanguageComboBox *>(QStringLiteral("primaryLanguage"));
        auto *secondary =
            dialog.findChild<LanguageComboBox *>(QStringLiteral("secondaryLanguage"));
        auto *maxLength = dialog.findChild<QSpinBox *>(QStringLiteral("maxLength"));
        auto *delay =
            dialog.findChild<QSpinBox *>(QStringLiteral("autoTranslateDelay"));
        auto *pauseInFullscreen =
            dialog.findChild<QCheckBox *>(QStringLiteral("pauseInFullscreen"));

        QVERIFY(autostart);
        QVERIFY(startMinimized);
        QVERIFY(restoreClipboard);
        QVERIFY(autoClose);
        QVERIFY(popupHotkey);
        QVERIFY(replaceHotkey);
        QVERIFY(mainHotkey);
        QVERIFY(proofreadingHotkey);
        QVERIFY(proofreadReplaceHotkey);
        QVERIFY(source);
        QVERIFY(primary);
        QVERIFY(secondary);
        QVERIFY(maxLength);
        QVERIFY(delay);
        QVERIFY(pauseInFullscreen);

        QVERIFY(autostart->isChecked());
        QVERIFY(!startMinimized->isChecked());
        QVERIFY(!restoreClipboard->isChecked());
        QVERIFY(!autoClose->isChecked());
        QCOMPARE(popupHotkey->keySequence(), QKeySequence(Qt::CTRL | Qt::Key_Q));
        QCOMPARE(replaceHotkey->keySequence(),
                 QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q));
        QCOMPARE(mainHotkey->keySequence(), QKeySequence(Qt::CTRL | Qt::Key_W));
        QCOMPARE(proofreadingHotkey->keySequence(),
                 QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F9));
        QCOMPARE(proofreadReplaceHotkey->keySequence(),
                 QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F10));
        QCOMPARE(source->currentCode(), QStringLiteral("en"));
        QCOMPARE(primary->currentCode(), QStringLiteral("de"));
        QCOMPARE(secondary->currentCode(), QStringLiteral("fr"));
        QCOMPARE(maxLength->value(), 5000);
        QCOMPARE(delay->value(), 0);
        QVERIFY(pauseInFullscreen->isChecked());
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
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("replaceSelectionHotkey"))
            ->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+R")));
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("mainWindowHotkey"))
            ->setKeySequence(QKeySequence());
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("proofreadingHotkey"))
            ->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+P")));
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("proofreadReplaceHotkey"))
            ->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+E")));
        dialog.findChild<LanguageComboBox *>(QStringLiteral("sourceLanguage"))
            ->setCurrentCode(QStringLiteral("en"));
        dialog.findChild<LanguageComboBox *>(QStringLiteral("primaryLanguage"))
            ->setCurrentCode(QStringLiteral("fr"));
        dialog.findChild<LanguageComboBox *>(QStringLiteral("secondaryLanguage"))
            ->setCurrentCode(QStringLiteral("de"));
        dialog.findChild<QSpinBox *>(QStringLiteral("maxLength"))->setValue(7000);
        dialog.findChild<QSpinBox *>(QStringLiteral("autoTranslateDelay"))->setValue(1200);
        dialog.findChild<QCheckBox *>(QStringLiteral("pauseInFullscreen"))->setChecked(false);

        const AppSettings result = dialog.settings();
        QVERIFY(result.general.autostart);
        QVERIFY(!result.general.startMinimized);
        QVERIFY(!result.general.restoreClipboard);
        QCOMPARE(result.general.uiLanguage, QStringLiteral("en"));
        QVERIFY(!result.popup.autoCloseOnFocusLoss);
        QCOMPARE(result.hotkeys.popup, QKeySequence(QStringLiteral("Ctrl+Alt+T")));
        QCOMPARE(result.hotkeys.replaceSelection,
                 QKeySequence(QStringLiteral("Ctrl+Alt+R")));
        QVERIFY(result.hotkeys.mainWindow.isEmpty());
        QCOMPARE(result.proofreading.hotkey, QKeySequence(QStringLiteral("Ctrl+Alt+P")));
        QCOMPARE(result.proofreading.replaceHotkey,
                 QKeySequence(QStringLiteral("Ctrl+Alt+E")));
        QCOMPARE(result.translation.sourceLang, QStringLiteral("en"));
        QCOMPARE(result.translation.primaryLang, QStringLiteral("fr"));
        QCOMPARE(result.translation.secondaryLang, QStringLiteral("de"));
        QCOMPARE(result.translation.maxLength, 7000);
        QCOMPARE(result.translation.autoTranslateDelayMs, 1200);
        QVERIFY(!result.hotkeys.pauseInFullscreen);
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
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("replaceSelectionHotkey"))
            ->setKeySequence(duplicate);

        QString error;
        QVERIFY(!dialog.validate(&error));
        QVERIFY(error.contains(QStringLiteral("assigned more than once")));

        // The proofreading rows are checked against the four existing actions too.
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("replaceSelectionHotkey"))
            ->setKeySequence(QKeySequence());
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("proofreadingHotkey"))
            ->setKeySequence(duplicate);
        QVERIFY(!dialog.validate(&error));
        QVERIFY(error.contains(QStringLiteral("assigned more than once")));

        // A duplicate between the two proofreading rows is rejected as well.
        // Clear the earlier duplicate on popupHotkey so this failure can only
        // come from the proofreading/replace pair.
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("popupHotkey"))
            ->setKeySequence(QKeySequence());
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("proofreadReplaceHotkey"))
            ->setKeySequence(duplicate);
        QVERIFY(!dialog.validate(&error));
        QVERIFY(error.contains(QStringLiteral("assigned more than once")));
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

        // The replace row rejects a multi-combination sequence too.
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("popupHotkey"))
            ->setKeySequence(QKeySequence());
        dialog.findChild<QKeySequenceEdit *>(QStringLiteral("proofreadReplaceHotkey"))
            ->setKeySequence(QKeySequence(QStringLiteral("Ctrl+K, Ctrl+C")));
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
        for (const char *name : {"popupHotkey", "replaceSelectionHotkey",
                                 "mainWindowHotkey", "showMainWindowHotkey",
                                 "proofreadingHotkey", "proofreadReplaceHotkey"}) {
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

    void servicesTabIsScrollable()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en")}, true);
        auto *scroll = dialog.findChild<QScrollArea *>(QStringLiteral("servicesScroll"));
        QVERIFY(scroll);
        QVERIFY(scroll->widget());
        QVERIFY(scroll->widgetResizable());
    }

    void servicesTab()
    {
        AppSettings settings;
        settings.google.mode = QStringLiteral("cloud");
        settings.google.apiKey = QStringLiteral("google-key");
        settings.deepl.apiKey = QStringLiteral("deepl-key");
        settings.deepl.englishVariant = QStringLiteral("EN-GB");
        settings.llm.baseUrl = QStringLiteral("https://openrouter.ai/api/v1");
        settings.llm.apiKey = QStringLiteral("llm-key");
        settings.llm.model = QStringLiteral("openai/gpt-4o-mini");

        SettingsDialog dialog(settings, {QStringLiteral("en"), QStringLiteral("ru")},
                              {QStringLiteral("de"), QStringLiteral("en")}, true);

        auto *googleMode = dialog.findChild<QComboBox *>(QStringLiteral("googleMode"));
        auto *googleKey = dialog.findChild<QLineEdit *>(QStringLiteral("googleApiKey"));
        auto *deepLMode = dialog.findChild<QComboBox *>(QStringLiteral("deeplMode"));
        auto *deepLKey = dialog.findChild<QLineEdit *>(QStringLiteral("deepLApiKey"));
        auto *variant =
            dialog.findChild<QComboBox *>(QStringLiteral("deeplEnglishVariant"));
        auto *llmUrl = dialog.findChild<QLineEdit *>(QStringLiteral("llmBaseUrl"));
        auto *llmKey = dialog.findChild<QLineEdit *>(QStringLiteral("llmApiKey"));
        auto *llmModel = dialog.findChild<QComboBox *>(QStringLiteral("llmModel"));
        QVERIFY(googleMode);
        QVERIFY(googleKey);
        QVERIFY(deepLMode);
        QVERIFY(deepLKey);
        QVERIFY(variant);
        QVERIFY(llmUrl);
        QVERIFY(llmKey);
        QVERIFY(llmModel);

        QCOMPARE(googleMode->currentData().toString(), QStringLiteral("cloud"));
        QCOMPARE(googleKey->text(), QStringLiteral("google-key"));
        QVERIFY(googleKey->isEnabled());
        QCOMPARE(deepLMode->currentData().toString(), QStringLiteral("api"));
        QCOMPARE(deepLKey->text(), QStringLiteral("deepl-key"));
        QVERIFY(deepLKey->isEnabled());
        QCOMPARE(variant->currentData().toString(), QStringLiteral("EN-GB"));

        deepLMode->setCurrentIndex(deepLMode->findData(QStringLiteral("free-web")));
        QVERIFY(!deepLKey->isEnabled());
        deepLMode->setCurrentIndex(deepLMode->findData(QStringLiteral("api")));
        QVERIFY(deepLKey->isEnabled());
        QCOMPARE(llmUrl->text(), QStringLiteral("https://openrouter.ai/api/v1"));
        QCOMPARE(llmKey->text(), QStringLiteral("llm-key"));
        QCOMPARE(llmModel->currentText(), QStringLiteral("openai/gpt-4o-mini"));

        googleMode->setCurrentIndex(googleMode->findData(QStringLiteral("free")));
        QVERIFY(!googleKey->isEnabled());
        googleMode->setCurrentIndex(googleMode->findData(QStringLiteral("cloud")));
        QVERIFY(googleKey->isEnabled());

        googleKey->setText(QStringLiteral("new-google-key"));
        deepLMode->setCurrentIndex(deepLMode->findData(QStringLiteral("free-web")));
        deepLKey->setText(QStringLiteral("new-deepl-key"));
        variant->setCurrentIndex(variant->findData(QStringLiteral("EN-US")));
        llmUrl->setText(QStringLiteral("http://127.0.0.1:11434/v1"));
        llmKey->setText(QStringLiteral("new-llm-key"));
        llmModel->setCurrentText(QStringLiteral("local-model"));

        const AppSettings result = dialog.settings();
        QCOMPARE(result.google.mode, QStringLiteral("cloud"));
        QCOMPARE(result.google.apiKey, QStringLiteral("new-google-key"));
        QCOMPARE(result.deepl.mode, QStringLiteral("free-web"));
        QCOMPARE(result.deepl.apiKey, QStringLiteral("new-deepl-key"));
        QCOMPARE(result.deepl.englishVariant, QStringLiteral("EN-US"));
        QCOMPARE(result.llm.baseUrl, QStringLiteral("http://127.0.0.1:11434/v1"));
        QCOMPARE(result.llm.apiKey, QStringLiteral("new-llm-key"));
        QCOMPARE(result.llm.model, QStringLiteral("local-model"));
    }

    void freeWebGateIsGone()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en")}, true);
        QVERIFY(!dialog.findChild<QCheckBox *>(removedGateObjectName()));
        QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("freeWebHint")));

        for (const char *name : {"mymemoryGroup", "mozhiGroup", "yandexGroup", "bingGroup",
                                 "reversoGroup", "duckduckgoGroup", "libretranslateGroup"}) {
            auto *group = dialog.findChild<QGroupBox *>(QString::fromLatin1(name));
            QVERIFY2(group, name);
            QVERIFY2(group->isEnabled(), name);
        }
        auto *fallback = dialog.findChild<QComboBox *>(QStringLiteral("fallbackService"));
        QVERIFY(fallback);
        QVERIFY(fallback->isEnabled());
    }

    void myMemoryGroup()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("de"), QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en")}, true);
        auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("mymemoryGroup"));
        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("mymemoryHint"));
        QVERIFY(group);
        QVERIFY(hint);

        QCOMPARE(group->title(), QStringLiteral("MyMemory"));
        QVERIFY(group->isEnabled());
        QVERIFY(hint->wordWrap());
        QVERIFY(hint->text().contains(QStringLiteral("no key")));
        QVERIFY(hint->text().contains(QStringLiteral("source language")));
        QVERIFY(hint->text().contains(QStringLiteral("several requests")));
        QVERIFY(!hint->text().contains(QStringLiteral("third party")));
        QVERIFY(group->findChildren<QLineEdit *>().isEmpty());
    }

    void mozhiGroup()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en"),
                               QStringLiteral("ru")},
                              true);
        auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("mozhiGroup"));
        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("mozhiHint"));
        auto *instance = dialog.findChild<QLineEdit *>(QStringLiteral("mozhiInstance"));
        auto *engine = dialog.findChild<QComboBox *>(QStringLiteral("mozhiEngine"));
        auto *effective =
            dialog.findChild<QLabel *>(QStringLiteral("mozhiEffectiveInstance"));
        auto *warning =
            dialog.findChild<QLabel *>(QStringLiteral("mozhiInstanceWarning"));
        QVERIFY(group);
        QVERIFY(hint);
        QVERIFY(instance);
        QVERIFY(engine);
        QVERIFY(effective);
        QVERIFY(warning);

        const QString defaultInstance = QStringLiteral("https://mozhi.adminforge.de");
        QCOMPARE(group->title(), QStringLiteral("Mozhi"));
        QVERIFY(group->isEnabled());
        QCOMPARE(instance->text(), defaultInstance);
        QCOMPARE(instance->placeholderText(), defaultInstance);
        QCOMPARE(engine->currentData().toString(), QStringLiteral("google"));
        QCOMPARE(effective->text(),
                 QStringLiteral("Effective instance: ") + defaultInstance);
        QVERIFY(warning->isHidden());
        QVERIFY(hint->wordWrap());
        QVERIFY(warning->wordWrap());
        QVERIFY(effective->wordWrap());
        QVERIFY(hint->text().contains(QStringLiteral("unofficial")));
        QVERIFY(hint->text().contains(QStringLiteral("rate-limit")));
        QVERIFY(hint->text().contains(QStringLiteral("self-hosted")));
        QVERIFY(hint->text().contains(QStringLiteral("several requests")));
        QVERIFY(!hint->text().contains(QStringLiteral("selected instance")));
        QVERIFY(warning->text().contains(QStringLiteral("http(s)")));
        QVERIFY(warning->text().contains(QStringLiteral("default instance")));

        const QList<QLineEdit *> fields = group->findChildren<QLineEdit *>();
        QCOMPARE(fields.size(), 1);
        QCOMPARE(fields.first(), instance);

        QCOMPARE(engine->count(), mozhiEngineIds().size());
        const QStringList engineNames = {QStringLiteral("Google"), QStringLiteral("DeepL"),
                                         QStringLiteral("DuckDuckGo"),
                                         QStringLiteral("LibreTranslate"),
                                         QStringLiteral("MyMemory"), QStringLiteral("Reverso"),
                                         QStringLiteral("Yandex")};
        QCOMPARE(engineNames.size(), mozhiEngineIds().size());
        for (int i = 0; i < engine->count(); ++i) {
            QCOMPARE(engine->itemData(i).toString(), mozhiEngineIds().at(i));
            QCOMPARE(engine->itemText(i), engineNames.at(i));
        }

        for (const QString &invalid : {QStringLiteral("ftp://host"), QString(),
                                       QStringLiteral("https://host/?engine=google")}) {
            instance->setText(invalid);
            QVERIFY(!warning->isHidden());
            QCOMPARE(effective->text(),
                     QStringLiteral("Effective instance: ") + defaultInstance);
            QCOMPARE(dialog.settings().mozhi.instance, defaultInstance);
            QString error;
            QVERIFY2(dialog.validate(&error), qPrintable(error));
        }

        instance->setText(QStringLiteral("http://127.0.0.1:8080/mozhi/"));
        QVERIFY(warning->isHidden());
        QCOMPARE(effective->text(),
                 QStringLiteral("Effective instance: http://127.0.0.1:8080/mozhi"));
        QCOMPARE(dialog.settings().mozhi.instance,
                 QStringLiteral("http://127.0.0.1:8080/mozhi"));

        QCOMPARE(engine->currentData().toString(), QStringLiteral("google"));
        QCOMPARE(dialog.settings().mozhi.engine, QStringLiteral("google"));
        engine->setCurrentIndex(engine->findData(QStringLiteral("deepl")));
        QCOMPARE(dialog.settings().mozhi.engine, QStringLiteral("deepl"));
    }

    void mozhiGroupShowsCustomSettings()
    {
        AppSettings settings;
        settings.mozhi.instance = QStringLiteral("https://mozhi.example.org/proxy");
        settings.mozhi.engine = QStringLiteral("libretranslate");

        SettingsDialog dialog(settings, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en")}, true);
        auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("mozhiGroup"));
        auto *instance = dialog.findChild<QLineEdit *>(QStringLiteral("mozhiInstance"));
        auto *engine = dialog.findChild<QComboBox *>(QStringLiteral("mozhiEngine"));
        auto *effective =
            dialog.findChild<QLabel *>(QStringLiteral("mozhiEffectiveInstance"));
        auto *warning =
            dialog.findChild<QLabel *>(QStringLiteral("mozhiInstanceWarning"));
        QVERIFY(group);
        QVERIFY(instance);
        QVERIFY(engine);
        QVERIFY(effective);
        QVERIFY(warning);

        QVERIFY(group->isEnabled());
        QCOMPARE(instance->text(), QStringLiteral("https://mozhi.example.org/proxy"));
        QCOMPARE(engine->currentData().toString(), QStringLiteral("libretranslate"));
        QCOMPARE(engine->currentText(), QStringLiteral("LibreTranslate"));
        QVERIFY(warning->isHidden());
        QCOMPARE(effective->text(),
                 QStringLiteral("Effective instance: https://mozhi.example.org/proxy"));

        instance->setText(QStringLiteral("https://mozhi.example.org/proxy/"));
        QVERIFY(warning->isHidden());
        QCOMPARE(effective->text(),
                 QStringLiteral("Effective instance: https://mozhi.example.org/proxy"));
        QCOMPARE(dialog.settings().mozhi.instance,
                 QStringLiteral("https://mozhi.example.org/proxy"));
    }

    void yandexGroup()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en"),
                               QStringLiteral("ru")},
                              true);
        auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("yandexGroup"));
        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("yandexHint"));
        QVERIFY(group);
        QVERIFY(hint);

        QCOMPARE(group->title(), QStringLiteral("Yandex"));
        QVERIFY(!group->isHidden());
        QVERIFY(group->isEnabled());
        QVERIFY(hint->wordWrap());
        QVERIFY(hint->text().contains(QStringLiteral("unofficial")));
        QVERIFY(hint->text().contains(QStringLiteral("no key")));
        QVERIFY(hint->text().contains(QStringLiteral("rate")));
        QVERIFY(hint->text().contains(QStringLiteral("block")));
        QVERIFY(hint->text().contains(QStringLiteral("several requests")));
        QVERIFY(!hint->text().contains(QStringLiteral("third party")));
        QVERIFY(group->findChildren<QLineEdit *>().isEmpty());
    }

    void bingGroup()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en"),
                               QStringLiteral("ru")},
                              true);
        auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("bingGroup"));
        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("bingHint"));
        QVERIFY(group);
        QVERIFY(hint);

        QCOMPARE(group->title(), QStringLiteral("Bing"));
        QVERIFY(!group->isHidden());
        QVERIFY(group->isEnabled());
        QVERIFY(hint->wordWrap());
        QVERIFY(hint->text().contains(QStringLiteral("unofficial")));
        QVERIFY(hint->text().contains(QStringLiteral("no key")));
        QVERIFY(hint->text().contains(QStringLiteral("rate")));
        QVERIFY(hint->text().contains(QStringLiteral("block")));
        QVERIFY(hint->text().contains(QStringLiteral("several requests")));
        QVERIFY(!hint->text().contains(QStringLiteral("third party")));
        QVERIFY(group->findChildren<QLineEdit *>().isEmpty());
    }

    void reversoGroup()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en"),
                               QStringLiteral("ru")},
                              true);
        auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("reversoGroup"));
        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("reversoHint"));
        QVERIFY(group);
        QVERIFY(hint);

        QCOMPARE(group->title(), QStringLiteral("Reverso"));
        QVERIFY(!group->isHidden());
        QVERIFY(group->isEnabled());
        QVERIFY(hint->wordWrap());
        QVERIFY(hint->text().contains(QStringLiteral("unofficial")));
        QVERIFY(hint->text().contains(QStringLiteral("no key")));
        QVERIFY(hint->text().contains(QStringLiteral("rate")));
        QVERIFY(hint->text().contains(QStringLiteral("block")));
        QVERIFY(hint->text().contains(QStringLiteral("several requests")));
        QVERIFY(!hint->text().contains(QStringLiteral("third party")));
        QVERIFY(hint->text().contains(QStringLiteral("English")));
        QVERIFY(hint->text().contains(QStringLiteral("source language")));
        QVERIFY(group->findChildren<QLineEdit *>().isEmpty());
    }

    void duckduckgoGroup()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en"),
                               QStringLiteral("ru")},
                              true);
        auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("duckduckgoGroup"));
        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("duckduckgoHint"));
        auto *reverso = dialog.findChild<QGroupBox *>(QStringLiteral("reversoGroup"));
        QVERIFY(group);
        QVERIFY(hint);
        QVERIFY(reverso);

        QCOMPARE(group->title(), QStringLiteral("DuckDuckGo"));
        QVERIFY(!group->isHidden());
        QVERIFY(group->isEnabled());
        QVERIFY(hint->wordWrap());
        QVERIFY(hint->text().contains(QStringLiteral("unofficial")));
        QVERIFY(hint->text().contains(QStringLiteral("no key")));
        QVERIFY(hint->text().contains(QStringLiteral("source language")));
        QVERIFY(hint->text().contains(QStringLiteral("Bing")));
        QVERIFY(hint->text().contains(QStringLiteral("several requests")));
        QVERIFY(!hint->text().contains(QStringLiteral("third party")));
        QVERIFY(group->findChildren<QLineEdit *>().isEmpty());

        const QLayout *servicesLayout = group->parentWidget()->layout();
        QVERIFY(servicesLayout);
        QVERIFY(servicesLayout->indexOf(reverso) >= 0);
        QVERIFY(servicesLayout->indexOf(reverso) < servicesLayout->indexOf(group));
        // The stretch must stay last, so the group is the last block on the
        // Services page rather than a widget pushed below the stretch.
        QVERIFY(servicesLayout->itemAt(servicesLayout->count() - 1)->spacerItem());
    }

    void libreTranslateGroup()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en"),
                               QStringLiteral("ru")},
                              true);
        auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("libretranslateGroup"));
        auto *baseUrl =
            dialog.findChild<QLineEdit *>(QStringLiteral("libretranslateBaseUrl"));
        auto *apiKey = dialog.findChild<QLineEdit *>(QStringLiteral("libretranslateApiKey"));
        auto *effective =
            dialog.findChild<QLabel *>(QStringLiteral("libretranslateEffectiveUrl"));
        auto *warning =
            dialog.findChild<QLabel *>(QStringLiteral("libretranslateUrlWarning"));
        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("libretranslateHint"));
        auto *duckDuckGo = dialog.findChild<QGroupBox *>(QStringLiteral("duckduckgoGroup"));
        QVERIFY(group);
        QVERIFY(baseUrl);
        QVERIFY(apiKey);
        QVERIFY(effective);
        QVERIFY(warning);
        QVERIFY(hint);
        QVERIFY(duckDuckGo);

        QCOMPARE(group->title(), QStringLiteral("LibreTranslate"));
        QVERIFY(!group->isHidden());
        QVERIFY(group->isEnabled());
        QVERIFY(baseUrl->text().isEmpty());
        QCOMPARE(apiKey->echoMode(), QLineEdit::Password);
        QVERIFY(apiKey->text().isEmpty());
        // Empty is valid and means "not configured": no warning, no effective URL.
        QVERIFY(effective->isHidden());
        QVERIFY(warning->isHidden());
        QVERIFY(hint->wordWrap());
        QVERIFY(warning->wordWrap());
        QVERIFY(effective->wordWrap());
        QVERIFY(hint->text().contains(QStringLiteral("self-hosted")));
        QVERIFY(hint->text().contains(QStringLiteral("you trust")));
        QVERIFY(hint->text().contains(QStringLiteral("optional")));
        QVERIFY(hint->text().contains(QStringLiteral("Google")));
        QVERIFY(hint->text().contains(QStringLiteral("several requests")));
        QVERIFY(!hint->text().contains(QStringLiteral("third party")));
        QCOMPARE(group->findChildren<QLineEdit *>().size(), 2);

        // A non-empty valid value shows the normalized effective URL.
        baseUrl->setText(QStringLiteral("http://127.0.0.1:8080/lt/"));
        QVERIFY(!effective->isHidden());
        QVERIFY(warning->isHidden());
        QVERIFY(effective->text().contains(QStringLiteral("http://127.0.0.1:8080/lt")));
        QVERIFY(!effective->text().contains(QStringLiteral("/lt/")));
        QCOMPARE(dialog.settings().libretranslate.baseUrl,
                 QStringLiteral("http://127.0.0.1:8080/lt"));

        // A non-empty invalid value warns and is stored as "not configured".
        baseUrl->setText(QStringLiteral("ftp://host"));
        QVERIFY(effective->isHidden());
        QVERIFY(!warning->isHidden());
        QVERIFY(warning->text().contains(QStringLiteral("http(s)")));
        QVERIFY(dialog.settings().libretranslate.baseUrl.isEmpty());

        // The key is trimmed and never configures the provider.
        baseUrl->clear();
        apiKey->setText(QStringLiteral("  secret-key  "));
        QCOMPARE(dialog.settings().libretranslate.apiKey, QStringLiteral("secret-key"));
        QVERIFY(dialog.settings().libretranslate.baseUrl.isEmpty());

        // The group follows DuckDuckGo and stays above the stretch.
        const QLayout *servicesLayout = group->parentWidget()->layout();
        QVERIFY(servicesLayout);
        QVERIFY(servicesLayout->indexOf(duckDuckGo) >= 0);
        QVERIFY(servicesLayout->indexOf(duckDuckGo) < servicesLayout->indexOf(group));
        QVERIFY(servicesLayout->itemAt(servicesLayout->count() - 1)->spacerItem());
    }

    void fallbackRowOnTranslationPage()
    {
        const QList<QPair<QString, QString>> services = {
            {QStringLiteral("yandex-web"), QStringLiteral("Yandex")},
            {QStringLiteral("bing-web"), QStringLiteral("Bing")},
            {QStringLiteral("mymemory"), QStringLiteral("MyMemory")},
            {QStringLiteral("mozhi"), QStringLiteral("Mozhi")},
            {QStringLiteral("reverso"), QStringLiteral("Reverso")},
        };

        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en"),
                               QStringLiteral("ru")},
                              true, services);
        QVERIFY(!dialog.findChild<QGroupBox *>(removedFallbackGroupObjectName()));
        auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("fallbackService"));
        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("fallbackHint"));
        QVERIFY(combo);
        QVERIFY(hint);

        // A fallback is a translation behaviour, so the row lives on the
        // Translation page, not on Services.
        auto *translationPage =
            dialog.findChild<LanguageComboBox *>(QStringLiteral("sourceLanguage"))
                ->parentWidget();
        auto *servicesPage =
            dialog.findChild<QScrollArea *>(QStringLiteral("servicesScroll"))->parentWidget();
        QVERIFY(translationPage);
        QVERIFY(servicesPage);
        QVERIFY(translationPage->isAncestorOf(combo));
        QVERIFY(translationPage->isAncestorOf(hint));
        QVERIFY(!servicesPage->isAncestorOf(combo));
        QVERIFY(!servicesPage->isAncestorOf(hint));

        bool labelFound = false;
        for (const QLabel *label : translationPage->findChildren<QLabel *>()) {
            if (label->text() == QStringLiteral("Fallback service:"))
                labelFound = true;
        }
        QVERIFY(labelFound);

        QCOMPARE(combo->count(), 6);
        QCOMPARE(combo->itemText(0), QStringLiteral("None"));
        QCOMPARE(combo->itemData(0).toString(), QString());
        QCOMPARE(combo->itemText(1), QStringLiteral("Yandex"));
        QCOMPARE(combo->itemData(1).toString(), QStringLiteral("yandex-web"));
        QCOMPARE(combo->itemText(2), QStringLiteral("Bing"));
        QCOMPARE(combo->itemData(2).toString(), QStringLiteral("bing-web"));
        QCOMPARE(combo->itemText(3), QStringLiteral("MyMemory"));
        QCOMPARE(combo->itemData(3).toString(), QStringLiteral("mymemory"));
        QCOMPARE(combo->itemText(4), QStringLiteral("Mozhi"));
        QCOMPARE(combo->itemData(4).toString(), QStringLiteral("mozhi"));
        QCOMPARE(combo->itemText(5), QStringLiteral("Reverso"));
        QCOMPARE(combo->itemData(5).toString(), QStringLiteral("reverso"));
        QCOMPARE(combo->currentIndex(), 0);
        QCOMPARE(dialog.settings().translation.fallbackService, QString());

        QVERIFY(hint->wordWrap());
        QVERIFY(hint->text().contains(QStringLiteral("no connection")));
        QVERIFY(hint->text().contains(QStringLiteral("timeout")));
        QVERIFY(hint->text().contains(QStringLiteral("rate limit")));
        QVERIFY(hint->text().contains(QStringLiteral("quota")));
        QVERIFY(hint->text().contains(QStringLiteral("invalid key")));
        QVERIFY(hint->text().contains(QStringLiteral("None")));
        QVERIFY(!hint->text().contains(QStringLiteral("third party")));

        QVERIFY(combo->isEnabled());

        combo->setCurrentIndex(4);
        QCOMPARE(combo->currentIndex(), 4);
        QCOMPARE(dialog.settings().translation.fallbackService, QStringLiteral("mozhi"));

        AppSettings remembered;
        remembered.translation.fallbackService = QStringLiteral("mozhi");
        SettingsDialog rememberedDialog(remembered, {QStringLiteral("en")},
                                        {QStringLiteral("de"), QStringLiteral("en")}, true,
                                        services);
        auto *rememberedCombo =
            rememberedDialog.findChild<QComboBox *>(QStringLiteral("fallbackService"));
        QVERIFY(rememberedCombo);
        QCOMPARE(rememberedCombo->currentIndex(), 4);
        QVERIFY(rememberedCombo->isEnabled());
        QCOMPARE(rememberedDialog.settings().translation.fallbackService,
                 QStringLiteral("mozhi"));

        AppSettings unknown;
        unknown.translation.fallbackService = QStringLiteral("nope");
        SettingsDialog unknownDialog(unknown, {QStringLiteral("en")},
                                     {QStringLiteral("de"), QStringLiteral("en")}, true,
                                     services);
        auto *unknownCombo =
            unknownDialog.findChild<QComboBox *>(QStringLiteral("fallbackService"));
        QVERIFY(unknownCombo);
        QCOMPARE(unknownCombo->currentIndex(), 0);
        QCOMPARE(unknownDialog.settings().translation.fallbackService, QString());
    }

    void providerOrder()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en")}, true);
        auto *scroll = dialog.findChild<QScrollArea *>(QStringLiteral("servicesScroll"));
        QVERIFY(scroll);
        QVERIFY(scroll->widget());
        const QLayout *layout = scroll->widget()->layout();
        QVERIFY(layout);

        const QStringList names = {
            QStringLiteral("googleGroup"),   QStringLiteral("yandexGroup"),
            QStringLiteral("bingGroup"),     QStringLiteral("deepLGroup"),
            QStringLiteral("mymemoryGroup"), QStringLiteral("mozhiGroup"),
            QStringLiteral("reversoGroup"),  QStringLiteral("duckduckgoGroup"),
            QStringLiteral("libretranslateGroup"),
        };
        int previous = -1;
        for (const QString &name : names) {
            auto *group = dialog.findChild<QGroupBox *>(name);
            QVERIFY2(group, qPrintable(name));
            const int index = layout->indexOf(group);
            QVERIFY2(index >= 0, qPrintable(name));
            QVERIFY2(index > previous, qPrintable(name));
            previous = index;
        }

        // The provider group lives on the AI tab; the Services page keeps only
        // a pointer after the DeepL group, which is the regression this guards.
        auto *llmGroup = dialog.findChild<QGroupBox *>(QStringLiteral("llmGroup"));
        auto *servicesScroll =
            dialog.findChild<QScrollArea *>(QStringLiteral("servicesScroll"));
        auto *movedHint = dialog.findChild<QLabel *>(QStringLiteral("llmMovedHint"));
        QVERIFY(llmGroup && servicesScroll && movedHint);
        QVERIFY(!servicesScroll->widget()->isAncestorOf(llmGroup));
        QCOMPARE(movedHint->parentWidget(), servicesScroll->widget());
        auto *deepLGroup = dialog.findChild<QGroupBox *>(QStringLiteral("deepLGroup"));
        QVERIFY(deepLGroup);
        QVERIFY(layout->indexOf(movedHint) > layout->indexOf(deepLGroup));

        QVERIFY(!dialog.findChild<QGroupBox *>(removedFallbackGroupObjectName()));
    }

    void llmModels()
    {
        AppSettings settings;
        settings.llm.model.clear();
        SettingsDialog dialog(settings, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en")}, true);
        auto *key = dialog.findChild<QLineEdit *>(QStringLiteral("llmApiKey"));
        auto *model = dialog.findChild<QComboBox *>(QStringLiteral("llmModel"));
        auto *button = dialog.findChild<QPushButton *>(QStringLiteral("loadLlmModels"));
        auto *status = dialog.findChild<QLabel *>(QStringLiteral("llmStatus"));
        QVERIFY(key);
        QVERIFY(model);
        QVERIFY(button);
        QVERIFY(status);

        key->setText(QStringLiteral("typed-key"));
        QString requestedBase;
        QString requestedKey;
        connect(&dialog, &SettingsDialog::llmModelsRequested, this,
                [&requestedBase, &requestedKey](const QString &baseUrl, const QString &key) {
                    requestedBase = baseUrl;
                    requestedKey = key;
                });
        button->click();
        QCOMPARE(requestedBase, QStringLiteral("https://openrouter.ai/api/v1"));
        QCOMPARE(requestedKey, QStringLiteral("typed-key"));
        QVERIFY(!button->isEnabled());

        dialog.showLlmModels(true, QStringLiteral("API key is valid. Loaded 2 models."),
                             {QStringLiteral("openai/gpt-4o-mini"),
                              QStringLiteral("deepseek/deepseek-chat")});
        QVERIFY(button->isEnabled());
        QVERIFY(status->text().contains(QStringLiteral("Loaded 2")));
        QCOMPARE(model->count(), 2);
        QCOMPARE(model->currentText(), QStringLiteral("openai/gpt-4o-mini"));
    }

    void aiTab()
    {
        AppSettings settings;
        settings.llm.baseUrl = QStringLiteral("http://127.0.0.1:11434/v1");
        settings.llm.apiKey = QStringLiteral("llm-key");
        settings.llm.model = QStringLiteral("local-model");
        SettingsDialog dialog(settings, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en")}, true);

        auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("settingsTabs"));
        auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("llmGroup"));
        QVERIFY(tabs && group);
        QCOMPARE(group->title(), QStringLiteral("OpenAI-compatible provider"));
        QVERIFY(tabs->indexOf(group->parentWidget()) >= 0);
        QCOMPARE(tabs->tabText(tabs->indexOf(group->parentWidget())), QStringLiteral("AI"));

        auto *url = dialog.findChild<QLineEdit *>(QStringLiteral("llmBaseUrl"));
        auto *key = dialog.findChild<QLineEdit *>(QStringLiteral("llmApiKey"));
        auto *model = dialog.findChild<QComboBox *>(QStringLiteral("llmModel"));
        QVERIFY(url && key && model);
        QCOMPARE(url->text(), QStringLiteral("http://127.0.0.1:11434/v1"));
        QCOMPARE(key->text(), QStringLiteral("llm-key"));
        QCOMPARE(model->currentText(), QStringLiteral("local-model"));

        // The group still edits the one [llm] configuration.
        url->setText(QStringLiteral("http://127.0.0.1:8080/v1"));
        key->setText(QStringLiteral("new-key"));
        model->setCurrentText(QStringLiteral("other-model"));
        const AppSettings result = dialog.settings();
        QCOMPARE(result.llm.baseUrl, QStringLiteral("http://127.0.0.1:8080/v1"));
        QCOMPARE(result.llm.apiKey, QStringLiteral("new-key"));
        QCOMPARE(result.llm.model, QStringLiteral("other-model"));
    }

    void deepLWebHint()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en")}, true);
        auto *mode = dialog.findChild<QComboBox *>(QStringLiteral("deeplMode"));
        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("deepLWebHint"));
        QVERIFY(mode);
        QVERIFY(hint);
        QVERIFY(hint->isHidden());

        mode->setCurrentIndex(mode->findData(QStringLiteral("free-web")));
        QVERIFY(!hint->isHidden());
        QVERIFY(hint->text().contains(QStringLiteral("without an API key"),
                                      Qt::CaseInsensitive));
        QVERIFY(hint->text().contains(QStringLiteral("unofficial"),
                                      Qt::CaseInsensitive));
        QVERIFY(!hint->text().contains(QStringLiteral("sent to DeepL")));
    }

    void llmHint()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en")}, true);
        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("llmHint"));
        QVERIFY(hint);
        QVERIFY(hint->wordWrap());
        QVERIFY(hint->text().contains(QStringLiteral("OpenAI-compatible")));
        QVERIFY(hint->text().contains(QStringLiteral("OpenRouter")));
        QVERIFY(hint->text().contains(QStringLiteral("your key")));
        QVERIFY(!hint->text().contains(QStringLiteral("selected provider")));
    }

    void secretStorageHint()
    {
        {
            SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                                  {QStringLiteral("de"), QStringLiteral("en")}, true);
            auto *hint = dialog.findChild<QLabel *>(QStringLiteral("secretStorageHint"));
            QVERIFY(hint);
            QVERIFY(hint->isHidden());
        }
        {
            SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                                  {QStringLiteral("de"), QStringLiteral("en")}, true, {},
                                  true);
            auto *hint = dialog.findChild<QLabel *>(QStringLiteral("secretStorageHint"));
            QVERIFY(hint);
            QVERIFY(!hint->isHidden());
            QVERIFY(hint->text().contains(QStringLiteral("unencrypted")));
        }
    }

    void hotkeyWarningHint()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en")}, true);
        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("hotkeyWarning"));
        QVERIFY(hint);
        QVERIFY(hint->isHidden());

        dialog.setUnavailableHotkeys({QStringLiteral("Ctrl+Q"), QStringLiteral("Ctrl+W")});
        QVERIFY(!hint->isHidden());
        QVERIFY(hint->text().contains(QStringLiteral("Ctrl+Q")));
        QVERIFY(hint->text().contains(QStringLiteral("Ctrl+W")));
        QVERIFY(hint->text().contains(QStringLiteral("--verbose")));
        // The reason is not always a foreign program: on Wayland it is a
        // missing portal, so the hint must not claim one.
        QVERIFY(!hint->text().contains(QStringLiteral("already used")));

        dialog.setUnavailableHotkeys({});
        QVERIFY(hint->isHidden());
        QVERIFY(hint->text().isEmpty());
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

    void proofreadingTab()
    {
        AppSettings settings;
        settings.proofreading.enabled = true;
        settings.proofreading.language = QStringLiteral("ru");
        settings.proofreading.endpoint = QStringLiteral("http://127.0.0.1:8081/v2/check");
        settings.proofreading.categories = {QStringLiteral("grammar"),
                                            QStringLiteral("style")};

        const QList<SettingsDialog::ProofreadingProvider> providers = {
            {QStringLiteral("languagetool"), QStringLiteral("LanguageTool"),
             {QStringLiteral("en"), QStringLiteral("ru"), QStringLiteral("zh-Hans")}, true},
            {QStringLiteral("other"), QStringLiteral("Other provider"),
             {QStringLiteral("fr")}, false},
        };
        SettingsDialog dialog(settings, {QStringLiteral("en"), QStringLiteral("ru")},
                              {QStringLiteral("de"), QStringLiteral("en")}, true, {}, false,
                              providers);

        auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("settingsTabs"));
        auto *enabled = dialog.findChild<QCheckBox *>(QStringLiteral("proofreadingEnabled"));
        auto *provider =
            dialog.findChild<QComboBox *>(QStringLiteral("proofreadingProvider"));
        auto *language =
            dialog.findChild<LanguageComboBox *>(QStringLiteral("proofreadingLanguage"));
        auto *endpoint =
            dialog.findChild<QLineEdit *>(QStringLiteral("proofreadingEndpoint"));
        auto *hint =
            dialog.findChild<QLabel *>(QStringLiteral("proofreadingEndpointHint"));
        auto *spelling =
            dialog.findChild<QCheckBox *>(QStringLiteral("proofreadingSpelling"));
        auto *grammar =
            dialog.findChild<QCheckBox *>(QStringLiteral("proofreadingGrammar"));
        auto *punctuation =
            dialog.findChild<QCheckBox *>(QStringLiteral("proofreadingPunctuation"));
        auto *style = dialog.findChild<QCheckBox *>(QStringLiteral("proofreadingStyle"));
        auto *hotkey =
            dialog.findChild<QKeySequenceEdit *>(QStringLiteral("proofreadingHotkey"));
        QVERIFY(tabs && enabled && provider && language && endpoint && hint && spelling
                && grammar && punctuation && style && hotkey);

        QCOMPARE(tabs->count(), 7);
        QCOMPARE(tabs->tabText(3), QStringLiteral("Services"));
        QCOMPARE(tabs->tabText(4), QStringLiteral("AI"));
        QCOMPARE(tabs->tabText(5), QStringLiteral("Text checking"));
        QCOMPARE(tabs->tabText(6), QStringLiteral("Network"));

        QVERIFY(enabled->isChecked());
        QCOMPARE(provider->count(), 2);
        QCOMPARE(provider->itemText(0), QStringLiteral("LanguageTool"));
        QCOMPARE(provider->itemData(0).toString(), QStringLiteral("languagetool"));
        QCOMPARE(provider->itemText(1), QStringLiteral("Other provider"));
        QCOMPARE(provider->currentData().toString(), QStringLiteral("languagetool"));

        QCOMPARE(language->itemData(0).toString(), Languages::autoCode());
        QVERIFY(language->findData(QStringLiteral("en")) >= 0);
        QVERIFY(language->findData(QStringLiteral("ru")) >= 0);
        QVERIFY(language->findData(QStringLiteral("zh-Hans")) >= 0);
        QCOMPARE(language->currentCode(), QStringLiteral("ru"));
        QCOMPARE(endpoint->text(), QStringLiteral("http://127.0.0.1:8081/v2/check"));
        QVERIFY(hint->wordWrap());
        QVERIFY(hint->text().contains(QStringLiteral("public")));
        QVERIFY(!spelling->isChecked());
        QVERIFY(grammar->isChecked());
        QVERIFY(!punctuation->isChecked());
        QVERIFY(style->isChecked());
        // With the proofreading mode enabled the row stays editable; the
        // disabled mode is covered by proofreadingHotkeyIsIndependentOfMode().
        QVERIFY(hotkey->isEnabled());

        const AppSettings result = dialog.settings();
        QVERIFY(result.proofreading.enabled);
        QCOMPARE(result.proofreading.provider, QStringLiteral("languagetool"));
        QCOMPARE(result.proofreading.language, QStringLiteral("ru"));
        QCOMPARE(result.proofreading.endpoint,
                 QStringLiteral("http://127.0.0.1:8081/v2/check"));
        QCOMPARE(result.proofreading.categories,
                 QStringList({QStringLiteral("grammar"), QStringLiteral("style")}));

        // The language list follows the selected provider and is rebuilt on a
        // provider change; a provider without Auto-detect offers no "auto".
        provider->setCurrentIndex(1);
        QCOMPARE(language->count(), 1);
        QCOMPARE(language->itemData(0).toString(), QStringLiteral("fr"));
        QCOMPARE(language->currentCode(), QStringLiteral("fr"));

        provider->setCurrentIndex(0);
        QCOMPARE(language->count(), 4);
        QCOMPARE(language->itemData(0).toString(), Languages::autoCode());
        QCOMPARE(language->currentCode(), Languages::autoCode());
    }

    void proofreadingLlmProviderHidesLanguageToolRow()
    {
        AppSettings settings;
        settings.proofreading.enabled = true;
        settings.proofreading.language = QStringLiteral("en");
        settings.proofreading.endpoint = QStringLiteral("http://127.0.0.1:8081/v2/check");

        const QList<SettingsDialog::ProofreadingProvider> providers = {
            {QStringLiteral("languagetool"), QStringLiteral("LanguageTool"),
             Languages::supported(QStringLiteral("languagetool"),
                                  Languages::Direction::Source),
             true},
            {QStringLiteral("llm"), QStringLiteral("LLM"),
             Languages::supported(QStringLiteral("llm"), Languages::Direction::Source), true},
        };
        SettingsDialog dialog(settings, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en"),
                               QStringLiteral("ru")},
                              true, {}, false, providers);

        auto *enabled = dialog.findChild<QCheckBox *>(QStringLiteral("proofreadingEnabled"));
        auto *providerGroup =
            dialog.findChild<QGroupBox *>(QStringLiteral("proofreadingProviderGroup"));
        auto *provider = dialog.findChild<QComboBox *>(QStringLiteral("proofreadingProvider"));
        auto *language =
            dialog.findChild<LanguageComboBox *>(QStringLiteral("proofreadingLanguage"));
        auto *endpoint = dialog.findChild<QLineEdit *>(QStringLiteral("proofreadingEndpoint"));
        auto *endpointHint =
            dialog.findChild<QLabel *>(QStringLiteral("proofreadingEndpointHint"));
        auto *endpointWarning =
            dialog.findChild<QLabel *>(QStringLiteral("proofreadingEndpointWarning"));
        auto *llmHint = dialog.findChild<QLabel *>(QStringLiteral("proofreadingLlmHint"));
        QVERIFY(enabled && providerGroup && provider && language && endpoint && endpointHint
                && endpointWarning && llmHint);
        QVERIFY(llmHint->wordWrap());
        QCOMPARE(llmHint->parentWidget(), providerGroup);

        auto *form = qobject_cast<QFormLayout *>(providerGroup->layout());
        QVERIFY(form);
        QWidget *endpointLabel = form->labelForField(endpoint);
        QVERIFY(endpointLabel);

        // On open the LanguageTool row and hint are visible and the LLM hint is not.
        QVERIFY(enabled->isChecked());
        QCOMPARE(provider->currentData().toString(), QStringLiteral("languagetool"));
        QVERIFY(!endpoint->isHidden());
        QVERIFY(!endpointHint->isHidden());
        QVERIFY(llmHint->isHidden());
        QVERIFY(!endpointLabel->isHidden());
        QCOMPARE(dialog.settings().proofreading.provider, QStringLiteral("languagetool"));

        // The row label goes with the field; the hint text names the AI tab.
        provider->setCurrentIndex(provider->findData(QStringLiteral("llm")));
        QCOMPARE(provider->currentData().toString(), QStringLiteral("llm"));
        QVERIFY(endpoint->isHidden());
        QVERIFY(endpointHint->isHidden());
        QVERIFY(endpointWarning->isHidden());
        QVERIFY(endpointLabel->isHidden());
        QVERIFY(!llmHint->isHidden());
        QCOMPARE(llmHint->text(),
                 QStringLiteral("The OpenAI-compatible provider is configured on the AI tab."));
        QCOMPARE(endpoint->text(), QStringLiteral("http://127.0.0.1:8081/v2/check"));
        QCOMPARE(dialog.settings().proofreading.provider, QStringLiteral("llm"));
        QCOMPARE(dialog.settings().proofreading.endpoint,
                 QStringLiteral("http://127.0.0.1:8081/v2/check"));
        QString error;
        QVERIFY(dialog.validate(&error));
        QVERIFY(error.isEmpty());
        QVERIFY(language->findData(Languages::autoCode()) >= 0);
        QVERIFY(language->findData(QStringLiteral("en")) >= 0);

        // Switching back restores the row without reopening the dialog.
        provider->setCurrentIndex(0);
        QVERIFY(!endpoint->isHidden());
        QVERIFY(!endpointHint->isHidden());
        QVERIFY(llmHint->isHidden());
        QVERIFY(!endpointLabel->isHidden());
        QCOMPARE(endpoint->text(), QStringLiteral("http://127.0.0.1:8081/v2/check"));
        QCOMPARE(dialog.settings().proofreading.provider, QStringLiteral("languagetool"));
    }

    void proofreadingLlmProviderFromSettings()
    {
        AppSettings settings;
        settings.proofreading.enabled = true;
        settings.proofreading.provider = QStringLiteral("llm");
        settings.proofreading.language = QStringLiteral("de");
        settings.proofreading.endpoint = QStringLiteral("http://127.0.0.1:11435/v2/check");

        const QList<SettingsDialog::ProofreadingProvider> providers = {
            {QStringLiteral("languagetool"), QStringLiteral("LanguageTool"),
             Languages::supported(QStringLiteral("languagetool"),
                                  Languages::Direction::Source),
             true},
            {QStringLiteral("llm"), QStringLiteral("LLM"),
             Languages::supported(QStringLiteral("llm"), Languages::Direction::Source), true},
        };
        SettingsDialog dialog(settings, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en"),
                               QStringLiteral("ru")},
                              true, {}, false, providers);

        auto *provider = dialog.findChild<QComboBox *>(QStringLiteral("proofreadingProvider"));
        auto *endpoint = dialog.findChild<QLineEdit *>(QStringLiteral("proofreadingEndpoint"));
        auto *endpointHint =
            dialog.findChild<QLabel *>(QStringLiteral("proofreadingEndpointHint"));
        auto *endpointWarning =
            dialog.findChild<QLabel *>(QStringLiteral("proofreadingEndpointWarning"));
        auto *llmHint = dialog.findChild<QLabel *>(QStringLiteral("proofreadingLlmHint"));
        QVERIFY(provider && endpoint && endpointHint && endpointWarning && llmHint);

        // A stored llm opens the page in the hidden-row state; the URL is kept.
        QCOMPARE(provider->currentData().toString(), QStringLiteral("llm"));
        QVERIFY(endpoint->isHidden());
        QVERIFY(endpointHint->isHidden());
        QVERIFY(endpointWarning->isHidden());
        QVERIFY(!llmHint->isHidden());
        QCOMPARE(endpoint->text(), QStringLiteral("http://127.0.0.1:11435/v2/check"));
        QCOMPARE(dialog.settings().proofreading.provider, QStringLiteral("llm"));
        QCOMPARE(dialog.settings().proofreading.endpoint,
                 QStringLiteral("http://127.0.0.1:11435/v2/check"));
    }

    void proofreadingLlmProviderKeepsTheLanguageToolWarning()
    {
        AppSettings settings;
        settings.proofreading.enabled = true;
        settings.proofreading.language = QStringLiteral("en");
        settings.proofreading.endpoint = QStringLiteral("http://127.0.0.1:8081/v2/check");

        const QList<SettingsDialog::ProofreadingProvider> providers = {
            {QStringLiteral("languagetool"), QStringLiteral("LanguageTool"),
             Languages::supported(QStringLiteral("languagetool"),
                                  Languages::Direction::Source),
             true},
            {QStringLiteral("llm"), QStringLiteral("LLM"),
             Languages::supported(QStringLiteral("llm"), Languages::Direction::Source), true},
        };
        SettingsDialog dialog(settings, {QStringLiteral("en")},
                              {QStringLiteral("de"), QStringLiteral("en"),
                               QStringLiteral("ru")},
                              true, {}, false, providers);

        auto *provider = dialog.findChild<QComboBox *>(QStringLiteral("proofreadingProvider"));
        auto *endpoint = dialog.findChild<QLineEdit *>(QStringLiteral("proofreadingEndpoint"));
        auto *endpointWarning =
            dialog.findChild<QLabel *>(QStringLiteral("proofreadingEndpointWarning"));
        QVERIFY(provider && endpoint && endpointWarning);

        const QString invalidUrlError = QStringLiteral(
            "Enter a valid http(s) LanguageTool URL, or leave the field empty for the public "
            "endpoint.");

        endpoint->setText(QStringLiteral("not a URL"));
        QVERIFY(!endpointWarning->isHidden());
        QString error;
        QVERIFY(!dialog.validate(&error));
        QCOMPARE(error, invalidUrlError);

        // The warning stays hidden under llm, but validate() still refuses the
        // invalid value instead of letting settings() drop it silently.
        provider->setCurrentIndex(provider->findData(QStringLiteral("llm")));
        QVERIFY(endpointWarning->isHidden());
        QVERIFY(endpoint->isHidden());
        error.clear();
        QVERIFY(!dialog.validate(&error));
        QCOMPARE(error, invalidUrlError);

        provider->setCurrentIndex(0);
        QVERIFY(!endpointWarning->isHidden());

        endpoint->setText(QStringLiteral("http://127.0.0.1:8081/v2/check"));
        QVERIFY(endpointWarning->isHidden());
        provider->setCurrentIndex(provider->findData(QStringLiteral("llm")));
        error.clear();
        QVERIFY(dialog.validate(&error));
        QVERIFY(error.isEmpty());
        QCOMPARE(dialog.settings().proofreading.endpoint,
                 QStringLiteral("http://127.0.0.1:8081/v2/check"));
    }

    void proofreadingHotkeyIsIndependentOfMode()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("en"), QStringLiteral("ru")}, true);
        auto *enabled =
            dialog.findChild<QCheckBox *>(QStringLiteral("proofreadingEnabled"));
        auto *hotkey =
            dialog.findChild<QKeySequenceEdit *>(QStringLiteral("proofreadingHotkey"));
        auto *replace =
            dialog.findChild<QKeySequenceEdit *>(QStringLiteral("proofreadReplaceHotkey"));
        QVERIFY(enabled && hotkey && replace);
        QVERIFY(!enabled->isChecked());

        // A combination can be assigned before the feature is enabled, so both
        // rows stay editable in either mode; the action itself is gated at
        // activation: while the switch is off, AppController shows a message
        // and performs no grab and no check.
        QVERIFY(hotkey->isEnabled());
        QVERIFY(replace->isEnabled());
        enabled->setChecked(true);
        QVERIFY(hotkey->isEnabled());
        QVERIFY(replace->isEnabled());
    }

    void proofreadingHotkeyRowsAreDistinctAndOrdered()
    {
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en")},
                              {QStringLiteral("en"), QStringLiteral("ru")}, true);
        auto *popup =
            dialog.findChild<QKeySequenceEdit *>(QStringLiteral("proofreadingHotkey"));
        auto *replace =
            dialog.findChild<QKeySequenceEdit *>(QStringLiteral("proofreadReplaceHotkey"));
        auto *main =
            dialog.findChild<QKeySequenceEdit *>(QStringLiteral("mainWindowHotkey"));
        QVERIFY(popup && replace && main);

        QFormLayout *form = nullptr;
        for (QFormLayout *candidate : dialog.findChildren<QFormLayout *>()) {
            int row = -1;
            QFormLayout::ItemRole role = QFormLayout::FieldRole;
            candidate->getWidgetPosition(popup, &row, &role);
            if (row >= 0) {
                form = candidate;
                break;
            }
        }
        QVERIFY(form);

        int popupRow = -1;
        int replaceRow = -1;
        int mainRow = -1;
        QFormLayout::ItemRole role = QFormLayout::FieldRole;
        form->getWidgetPosition(popup, &popupRow, &role);
        form->getWidgetPosition(replace, &replaceRow, &role);
        form->getWidgetPosition(main, &mainRow, &role);
        QVERIFY(popupRow >= 0);
        QVERIFY(replaceRow >= 0);
        QVERIFY(mainRow >= 0);
        // The replace row follows the popup check and stays before the
        // main-window rows.
        QCOMPARE(replaceRow, popupRow + 1);
        QVERIFY(mainRow > replaceRow);

        const auto labelText = [form](int row) {
            const QLayoutItem *item = form->itemAt(row, QFormLayout::LabelRole);
            const auto *label = item ? qobject_cast<QLabel *>(item->widget()) : nullptr;
            return label ? label->text() : QString();
        };
        const QString popupLabel = labelText(popupRow);
        const QString replaceLabel = labelText(replaceRow);
        QVERIFY2(popupLabel.contains(QStringLiteral("popup"), Qt::CaseInsensitive),
                 qPrintable(popupLabel));
        QVERIFY2(replaceLabel.contains(QStringLiteral("corrected"), Qt::CaseInsensitive),
                 qPrintable(replaceLabel));
        QVERIFY(popupLabel != replaceLabel);
    }

    void proofreadingTabHasNoGate()
    {
        const QList<SettingsDialog::ProofreadingProvider> providers = {
            {QStringLiteral("languagetool"), QStringLiteral("LanguageTool"),
             {QStringLiteral("en"), QStringLiteral("ru")}, true},
        };
        SettingsDialog dialog(AppSettings{}, {QStringLiteral("en"), QStringLiteral("ru")},
                              {QStringLiteral("de"), QStringLiteral("en"),
                               QStringLiteral("ru")},
                              true, {}, false, providers);

        auto *enabled = dialog.findChild<QCheckBox *>(QStringLiteral("proofreadingEnabled"));
        auto *providerGroup =
            dialog.findChild<QGroupBox *>(QStringLiteral("proofreadingProviderGroup"));
        auto *issuesGroup =
            dialog.findChild<QGroupBox *>(QStringLiteral("proofreadingIssuesGroup"));
        auto *provider =
            dialog.findChild<QComboBox *>(QStringLiteral("proofreadingProvider"));
        auto *endpoint =
            dialog.findChild<QLineEdit *>(QStringLiteral("proofreadingEndpoint"));
        auto *endpointWarning =
            dialog.findChild<QLabel *>(QStringLiteral("proofreadingEndpointWarning"));
        QVERIFY(enabled && providerGroup && issuesGroup && provider && endpoint
                && endpointWarning);

        // The enable switch is the only consent: the former online group and
        // its widgets are gone.
        QVERIFY(!dialog.findChild<QGroupBox *>(removedOnlineGroupObjectName()));
        QVERIFY(!dialog.findChild<QCheckBox *>(removedAllowOnlineObjectName()));
        QVERIFY(!dialog.findChild<QLabel *>(removedOnlineWarningObjectName()));

        // The former Options group is gone as a whole: no pre-translation
        // check and no automatic replacement.
        QVERIFY(!dialog.findChild<QGroupBox *>(removedProofreadingOptionsGroupObjectName()));
        QVERIFY(!dialog.findChild<QCheckBox *>(removedCheckBeforeTranslationObjectName()));
        QVERIFY(!dialog.findChild<QCheckBox *>(removedAutoReplaceObjectName()));

        const AppSettings before = dialog.settings();
        QVERIFY(!enabled->isChecked());
        QVERIFY(!providerGroup->isEnabled());
        QVERIFY(!issuesGroup->isEnabled());

        // Only the mode switch disables rows.
        enabled->setChecked(true);
        QVERIFY(providerGroup->isEnabled());
        QVERIFY(issuesGroup->isEnabled());
        QVERIFY(provider->isEnabled());
        QVERIFY(endpoint->isEnabled());

        endpoint->setText(QStringLiteral("not a URL"));
        QVERIFY(!endpointWarning->isHidden());
        QVERIFY(dialog.settings().proofreading.endpoint.isEmpty());
        QString error;
        QVERIFY(!dialog.validate(&error));
        QCOMPARE(error, QStringLiteral("Enter a valid http(s) LanguageTool URL, or leave the "
                                       "field empty for the public endpoint."));

        endpoint->clear();
        QVERIFY(endpointWarning->isHidden());
        error.clear();
        QVERIFY(dialog.validate(&error));
        QVERIFY(error.isEmpty());
        QVERIFY(dialog.settings().proofreading.endpoint.isEmpty());

        // The page never reads or writes a translation.* value.
        const AppSettings after = dialog.settings();
        QCOMPARE(after.translation.sourceLang, before.translation.sourceLang);
        QCOMPARE(after.translation.primaryLang, before.translation.primaryLang);
        QCOMPARE(after.translation.secondaryLang, before.translation.secondaryLang);
        QCOMPARE(after.translation.maxLength, before.translation.maxLength);
        QCOMPARE(after.translation.autoTranslateDelayMs,
                 before.translation.autoTranslateDelayMs);
        QCOMPARE(after.translation.fallbackService, before.translation.fallbackService);
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
