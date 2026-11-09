#include "ui/settingsdialog.h"

#include "core/appinfo.h"
#include "core/hotkeysequence.h"
#include "core/languages.h"
#include "core/uilanguages.h"
#include "ui/languagecombobox.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <iterator>

SettingsDialog::SettingsDialog(const AppSettings &settings,
                               const QStringList &sourceLanguages,
                               const QStringList &targetLanguages, bool autostartApproved,
                               bool secretStorageFallback, QWidget *parent)
    : QDialog(parent)
    , m_initial(settings)
    , m_targetLanguages(targetLanguages)
{
    setWindowTitle(tr("Settings"));
    setModal(true);
    resize(600, 600);

    buildUi(sourceLanguages);
    loadValues(settings, autostartApproved, secretStorageFallback);

    connect(qApp, &QApplication::focusChanged, this,
            [this](QWidget *, QWidget *) { updateHotkeyEditing(); });
    connect(this, &QDialog::finished, this, [this] { setHotkeyEditing(false); });
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::buildUi(const QStringList &sourceLanguages)
{
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("settingsTabs"));

    // General.
    auto *generalPage = new QWidget(m_tabs);
    auto *generalLayout = new QVBoxLayout(generalPage);
    auto *generalForm = new QFormLayout;
    m_uiLanguage = new QComboBox(generalPage);
    m_uiLanguage->setObjectName(QStringLiteral("uiLanguage"));
    m_uiLanguage->addItem(tr("System"), QStringLiteral("system"));
    for (const UiLanguages::Info &language : UiLanguages::all())
        m_uiLanguage->addItem(language.nativeName, language.code);
    generalForm->addRow(tr("Interface language:"), m_uiLanguage);
    generalLayout->addLayout(generalForm);
#if defined(Q_OS_WIN)
    m_autostart = new QCheckBox(tr("Start %1 when Windows starts").arg(AppInfo::name()),
                                generalPage);
#else
    m_autostart = new QCheckBox(tr("Start %1 when you log in").arg(AppInfo::name()),
                                generalPage);
#endif
    m_autostart->setObjectName(QStringLiteral("autostart"));
    m_autostartHint = new QLabel(tr("Autostart is disabled in Windows Task Manager."),
                                 generalPage);
    m_autostartHint->setObjectName(QStringLiteral("autostartHint"));
    m_autostartHint->setWordWrap(true);
    m_autostartHint->setStyleSheet(QStringLiteral("color: #d32f2f;"));
    m_secretStorageHint = new QLabel(
        tr("Secrets are stored unencrypted in a file readable only by your user "
           "because the system keyring is unavailable."),
        generalPage);
    m_secretStorageHint->setObjectName(QStringLiteral("secretStorageHint"));
    m_secretStorageHint->setWordWrap(true);
    m_secretStorageHint->setStyleSheet(QStringLiteral("color: #d32f2f;"));
    m_startMinimized = new QCheckBox(tr("Start minimized in the system tray"), generalPage);
    m_startMinimized->setObjectName(QStringLiteral("startMinimized"));
    m_restoreClipboard =
        new QCheckBox(tr("Restore clipboard after copying the selection"), generalPage);
    m_restoreClipboard->setObjectName(QStringLiteral("restoreClipboard"));
    m_autoClosePopup =
        new QCheckBox(tr("Close the popup when it loses focus"), generalPage);
    m_autoClosePopup->setObjectName(QStringLiteral("autoClosePopup"));

    generalLayout->addWidget(m_autostart);
    generalLayout->addWidget(m_autostartHint);
    generalLayout->addWidget(m_secretStorageHint);
    generalLayout->addWidget(m_startMinimized);
    generalLayout->addWidget(m_restoreClipboard);
    generalLayout->addWidget(m_autoClosePopup);
    generalLayout->addStretch();

    // Hotkeys.
    auto *hotkeysPage = new QWidget(m_tabs);
    auto *hotkeysLayout = new QVBoxLayout(hotkeysPage);
    auto *hotkeysForm = new QFormLayout;
    m_popupHotkey = new QKeySequenceEdit(hotkeysPage);
    m_popupHotkey->setObjectName(QStringLiteral("popupHotkey"));
    m_replaceSelectionHotkey = new QKeySequenceEdit(hotkeysPage);
    m_replaceSelectionHotkey->setObjectName(QStringLiteral("replaceSelectionHotkey"));
    m_mainWindowHotkey = new QKeySequenceEdit(hotkeysPage);
    m_mainWindowHotkey->setObjectName(QStringLiteral("mainWindowHotkey"));
    m_showMainWindowHotkey = new QKeySequenceEdit(hotkeysPage);
    m_showMainWindowHotkey->setObjectName(QStringLiteral("showMainWindowHotkey"));
    hotkeysForm->addRow(tr("Translate in a popup:"), m_popupHotkey);
    hotkeysForm->addRow(tr("Replace the selected text with the translation:"),
                        m_replaceSelectionHotkey);
    hotkeysForm->addRow(tr("Translate in the main window:"), m_mainWindowHotkey);
    hotkeysForm->addRow(tr("Show the main window:"), m_showMainWindowHotkey);
    m_pauseInFullscreen =
        new QCheckBox(tr("Pause global hotkeys in fullscreen apps"), hotkeysPage);
    m_pauseInFullscreen->setObjectName(QStringLiteral("pauseInFullscreen"));
    auto *hotkeysHint = new QLabel(
        tr("Global hotkeys are disabled while a key combination field is focused. "
           "Leave a field empty to disable that action."),
        hotkeysPage);
    hotkeysHint->setWordWrap(true);
    hotkeysLayout->addLayout(hotkeysForm);
    hotkeysLayout->addWidget(m_pauseInFullscreen);
    hotkeysLayout->addWidget(hotkeysHint);
    hotkeysLayout->addStretch();

    // Translation.
    auto *translationPage = new QWidget(m_tabs);
    auto *translationLayout = new QVBoxLayout(translationPage);
    auto *translationForm = new QFormLayout;
    m_sourceLanguage = new LanguageComboBox(translationPage);
    m_sourceLanguage->setObjectName(QStringLiteral("sourceLanguage"));
    m_sourceLanguage->setLanguages(sourceLanguages, true);
    m_primaryLanguage = new LanguageComboBox(translationPage);
    m_primaryLanguage->setObjectName(QStringLiteral("primaryLanguage"));
    m_primaryLanguage->setLanguages(m_targetLanguages, false);
    m_secondaryLanguage = new LanguageComboBox(translationPage);
    m_secondaryLanguage->setObjectName(QStringLiteral("secondaryLanguage"));
    m_secondaryLanguage->setLanguages(m_targetLanguages, false);
    m_maxLength = new QSpinBox(translationPage);
    m_maxLength->setObjectName(QStringLiteral("maxLength"));
    m_maxLength->setRange(100, 100000);
    m_maxLength->setSingleStep(1000);
    m_maxLength->setSuffix(tr(" characters"));
    m_autoTranslateDelay = new QSpinBox(translationPage);
    m_autoTranslateDelay->setObjectName(QStringLiteral("autoTranslateDelay"));
    m_autoTranslateDelay->setRange(0, 60000);
    m_autoTranslateDelay->setSingleStep(100);
    m_autoTranslateDelay->setSuffix(tr(" ms"));
    m_autoTranslateDelay->setSpecialValueText(tr("Off"));

    translationForm->addRow(tr("Source language:"), m_sourceLanguage);
    translationForm->addRow(tr("Primary language:"), m_primaryLanguage);
    translationForm->addRow(tr("Additional language:"), m_secondaryLanguage);
    translationForm->addRow(tr("Maximum text length:"), m_maxLength);
    translationForm->addRow(tr("Auto-translate delay:"), m_autoTranslateDelay);
    auto *translationHint = new QLabel(
        tr("The additional language is used automatically when the detected text is already "
           "in the primary language."),
        translationPage);
    translationHint->setWordWrap(true);
    translationLayout->addLayout(translationForm);
    translationLayout->addWidget(translationHint);
    translationLayout->addStretch();

    m_tabs->addTab(generalPage, tr("General"));
    m_tabs->addTab(hotkeysPage, tr("Hotkeys"));
    m_tabs->addTab(translationPage, tr("Translation"));

    // Services. The page is taller than the dialog at high DPI scaling, so it
    // scrolls instead of clipping the hint at the bottom.
    auto *servicesPage = new QWidget(m_tabs);
    auto *servicesPageLayout = new QVBoxLayout(servicesPage);
    servicesPageLayout->setContentsMargins(0, 0, 0, 0);
    auto *servicesScroll = new QScrollArea(servicesPage);
    servicesScroll->setObjectName(QStringLiteral("servicesScroll"));
    servicesScroll->setWidgetResizable(true);
    servicesScroll->setFrameShape(QFrame::NoFrame);
    auto *servicesContent = new QWidget(servicesScroll);
    auto *servicesLayout = new QVBoxLayout(servicesContent);
    auto *googleGroup = new QGroupBox(tr("Google Translate"), servicesContent);
    auto *googleForm = new QFormLayout(googleGroup);
    m_googleMode = new QComboBox(googleGroup);
    m_googleMode->setObjectName(QStringLiteral("googleMode"));
    m_googleMode->addItem(tr("Free endpoint (no key)"), QStringLiteral("free"));
    m_googleMode->addItem(tr("Cloud Translation API (key required)"),
                          QStringLiteral("cloud"));
    m_googleApiKey = new QLineEdit(googleGroup);
    m_googleApiKey->setObjectName(QStringLiteral("googleApiKey"));
    m_googleApiKey->setEchoMode(QLineEdit::Password);
    m_googleApiKey->setPlaceholderText(tr("Google Cloud API key"));
    googleForm->addRow(tr("Mode:"), m_googleMode);
    googleForm->addRow(tr("API key:"), m_googleApiKey);
    auto *googleHint = new QLabel(
        tr("The free mode uses the public endpoint and needs no key. The Cloud API "
           "requires a key with the Translation API enabled."),
        googleGroup);
    googleHint->setWordWrap(true);
    googleForm->addRow(googleHint);
    connect(m_googleMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::updateGoogleControls);

    auto *deepLGroup = new QGroupBox(tr("DeepL"), servicesContent);
    auto *deepLForm = new QFormLayout(deepLGroup);
    m_deeplMode = new QComboBox(deepLGroup);
    m_deeplMode->setObjectName(QStringLiteral("deeplMode"));
    m_deeplMode->addItem(tr("API (key required)"), QStringLiteral("api"));
    m_deeplMode->addItem(tr("Free web (unofficial, no key)"), QStringLiteral("free-web"));
    m_deeplApiKey = new QLineEdit(deepLGroup);
    m_deeplApiKey->setObjectName(QStringLiteral("deepLApiKey"));
    m_deeplApiKey->setEchoMode(QLineEdit::Password);
    m_deeplApiKey->setPlaceholderText(tr("DeepL API key (ends with :fx for Free)"));
    m_deeplEnglishVariant = new QComboBox(deepLGroup);
    m_deeplEnglishVariant->setObjectName(QStringLiteral("deeplEnglishVariant"));
    m_deeplEnglishVariant->addItem(QStringLiteral("EN-US"), QStringLiteral("EN-US"));
    m_deeplEnglishVariant->addItem(QStringLiteral("EN-GB"), QStringLiteral("EN-GB"));
    m_checkDeepLKey = new QPushButton(tr("Check key"), deepLGroup);
    m_checkDeepLKey->setObjectName(QStringLiteral("checkDeepLKey"));
    m_deepLKeyStatus = new QLabel(deepLGroup);
    m_deepLKeyStatus->setObjectName(QStringLiteral("deeplKeyStatus"));
    m_deepLKeyStatus->setWordWrap(true);
    m_deepLKeyStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_deepLWebHint = new QLabel(
        tr("Available without an API key. This unofficial endpoint may stop working, apply "
           "rate limits or block requests, and the text is sent to DeepL."),
        deepLGroup);
    m_deepLWebHint->setObjectName(QStringLiteral("deepLWebHint"));
    m_deepLWebHint->setWordWrap(true);
    deepLForm->addRow(tr("Mode:"), m_deeplMode);
    deepLForm->addRow(tr("API key:"), m_deeplApiKey);
    deepLForm->addRow(tr("English target:"), m_deeplEnglishVariant);
    deepLForm->addRow(m_checkDeepLKey);
    deepLForm->addRow(m_deepLKeyStatus);
    deepLForm->addRow(m_deepLWebHint);
    connect(m_deeplMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::updateDeepLControls);
    connect(m_checkDeepLKey, &QPushButton::clicked, this, [this] {
        m_deepLKeyStatus->setStyleSheet(QString());
        m_deepLKeyStatus->setText(tr("Checking…"));
        m_checkDeepLKey->setEnabled(false);
        emit deepLKeyCheckRequested(m_deeplApiKey->text().trimmed());
    });

    auto *llmGroup = new QGroupBox(tr("OpenAI-compatible provider"), servicesContent);
    auto *llmForm = new QFormLayout(llmGroup);
    m_llmBaseUrl = new QLineEdit(llmGroup);
    m_llmBaseUrl->setObjectName(QStringLiteral("llmBaseUrl"));
    m_llmBaseUrl->setPlaceholderText(QStringLiteral("https://openrouter.ai/api/v1"));
    m_llmApiKey = new QLineEdit(llmGroup);
    m_llmApiKey->setObjectName(QStringLiteral("llmApiKey"));
    m_llmApiKey->setEchoMode(QLineEdit::Password);
    m_llmApiKey->setPlaceholderText(tr("API key"));
    m_llmModel = new QComboBox(llmGroup);
    m_llmModel->setObjectName(QStringLiteral("llmModel"));
    m_llmModel->setEditable(true);
    m_llmModel->setInsertPolicy(QComboBox::NoInsert);
    m_llmModel->setMaxVisibleItems(30);
    if (m_llmModel->lineEdit()) {
        m_llmModel->lineEdit()->setPlaceholderText(
            tr("Model ID, for example openai/gpt-4o-mini"));
    }
    m_loadLlmModels = new QPushButton(tr("Load models"), llmGroup);
    m_loadLlmModels->setObjectName(QStringLiteral("loadLlmModels"));
    m_llmStatus = new QLabel(llmGroup);
    m_llmStatus->setObjectName(QStringLiteral("llmStatus"));
    m_llmStatus->setWordWrap(true);
    m_llmStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    llmForm->addRow(tr("Server URL:"), m_llmBaseUrl);
    llmForm->addRow(tr("API key:"), m_llmApiKey);
    llmForm->addRow(tr("Model:"), m_llmModel);
    llmForm->addRow(m_loadLlmModels);
    llmForm->addRow(m_llmStatus);
    auto *llmHint = new QLabel(
        tr("Uses the OpenAI-compatible Chat Completions API. The default server is OpenRouter; "
           "the URL can point to another compatible endpoint such as OpenAI, Ollama, llama.cpp "
           "or TeamRouter. The model must be available to your key. The text is sent to the "
           "selected provider."),
        llmGroup);
    llmHint->setObjectName(QStringLiteral("llmHint"));
    llmHint->setWordWrap(true);
    llmForm->addRow(llmHint);
    connect(m_loadLlmModels, &QPushButton::clicked, this, [this] {
        m_llmStatus->setStyleSheet(QString());
        m_llmStatus->setText(tr("Checking…"));
        m_loadLlmModels->setEnabled(false);
        emit llmModelsRequested(m_llmBaseUrl->text().trimmed(),
                                m_llmApiKey->text().trimmed());
    });

    servicesLayout->addWidget(googleGroup);
    servicesLayout->addWidget(deepLGroup);
    servicesLayout->addWidget(llmGroup);
    auto *freeWebGroup = new QGroupBox(tr("Free web services"), servicesContent);
    auto *freeWebForm = new QFormLayout(freeWebGroup);
    m_allowFreeServices = new QCheckBox(tr("Allow free web services"), freeWebGroup);
    m_allowFreeServices->setObjectName(QStringLiteral("allowFreeServices"));
    auto *freeWebHint = new QLabel(
        tr("These services need no API key. Most are unofficial: they may stop working, apply "
           "rate limits or block requests, and the text is sent to a third party. The Google "
           "and DeepL APIs remain the recommended choice."),
        freeWebGroup);
    freeWebHint->setObjectName(QStringLiteral("freeWebHint"));
    freeWebHint->setWordWrap(true);
    freeWebForm->addRow(m_allowFreeServices);
    freeWebForm->addRow(freeWebHint);
    servicesLayout->addWidget(freeWebGroup);

    auto *myMemoryGroup = new QGroupBox(tr("MyMemory"), servicesContent);
    myMemoryGroup->setObjectName(QStringLiteral("mymemoryGroup"));
    auto *myMemoryLayout = new QVBoxLayout(myMemoryGroup);
    auto *myMemoryHint = new QLabel(
        tr("MyMemory is a free translation API and needs no key. Enable free web services above "
           "to use it. It cannot detect the source language, and long text is sent in several "
           "requests. The text is sent to a third party."),
        myMemoryGroup);
    myMemoryHint->setObjectName(QStringLiteral("mymemoryHint"));
    myMemoryHint->setWordWrap(true);
    myMemoryLayout->addWidget(myMemoryHint);
    servicesLayout->addWidget(myMemoryGroup);
    m_mymemoryGroup = myMemoryGroup;
    connect(m_allowFreeServices, &QCheckBox::toggled, this,
            &SettingsDialog::updateMyMemoryControls);

    m_mozhiGroup = new QGroupBox(tr("Mozhi"), servicesContent);
    m_mozhiGroup->setObjectName(QStringLiteral("mozhiGroup"));
    auto *mozhiLayout = new QVBoxLayout(m_mozhiGroup);
    auto *mozhiForm = new QFormLayout;
    m_mozhiInstance = new QLineEdit(m_mozhiGroup);
    m_mozhiInstance->setObjectName(QStringLiteral("mozhiInstance"));
    m_mozhiInstance->setPlaceholderText(MozhiSettings{}.instance);
    mozhiForm->addRow(tr("Instance URL:"), m_mozhiInstance);
    m_mozhiEngine = new QComboBox(m_mozhiGroup);
    m_mozhiEngine->setObjectName(QStringLiteral("mozhiEngine"));
    // Display names are product names, so they stay untranslated.
    const QHash<QString, QString> engineNames = {
        {QStringLiteral("google"), QStringLiteral("Google")},
        {QStringLiteral("deepl"), QStringLiteral("DeepL")},
        {QStringLiteral("duckduckgo"), QStringLiteral("DuckDuckGo")},
        {QStringLiteral("libretranslate"), QStringLiteral("LibreTranslate")},
        {QStringLiteral("mymemory"), QStringLiteral("MyMemory")},
        {QStringLiteral("reverso"), QStringLiteral("Reverso")},
        {QStringLiteral("yandex"), QStringLiteral("Yandex")},
    };
    for (const QString &id : mozhiEngineIds())
        m_mozhiEngine->addItem(engineNames.value(id, id), id);
    mozhiForm->addRow(tr("Engine:"), m_mozhiEngine);
    mozhiLayout->addLayout(mozhiForm);
    m_mozhiEffectiveInstance = new QLabel(m_mozhiGroup);
    m_mozhiEffectiveInstance->setObjectName(QStringLiteral("mozhiEffectiveInstance"));
    m_mozhiEffectiveInstance->setWordWrap(true);
    m_mozhiEffectiveInstance->setTextFormat(Qt::PlainText);
    mozhiLayout->addWidget(m_mozhiEffectiveInstance);
    m_mozhiInstanceWarning = new QLabel(
        tr("Enter a base http(s) instance URL without credentials, a query or a fragment. "
           "The default instance will be used."),
        m_mozhiGroup);
    m_mozhiInstanceWarning->setObjectName(QStringLiteral("mozhiInstanceWarning"));
    m_mozhiInstanceWarning->setWordWrap(true);
    m_mozhiInstanceWarning->setVisible(false);
    mozhiLayout->addWidget(m_mozhiInstanceWarning);
    m_mozhiHint = new QLabel(
        tr("Mozhi is a proxy to public translation engines. Instances are unofficial: they may "
           "stop working or rate-limit requests, and some engines may be disabled on an instance. "
           "A self-hosted instance's terms and stability are your responsibility. Enable free web "
           "services above to use Mozhi. Long text is sent in several requests. The text is sent "
           "to the selected instance and engine."),
        m_mozhiGroup);
    m_mozhiHint->setObjectName(QStringLiteral("mozhiHint"));
    m_mozhiHint->setWordWrap(true);
    mozhiLayout->addWidget(m_mozhiHint);
    servicesLayout->addWidget(m_mozhiGroup);
    connect(m_allowFreeServices, &QCheckBox::toggled, this,
            &SettingsDialog::updateMozhiControls);
    connect(m_mozhiInstance, &QLineEdit::textChanged, this,
            &SettingsDialog::updateMozhiControls);

    m_yandexGroup = new QGroupBox(tr("Yandex"), servicesContent);
    m_yandexGroup->setObjectName(QStringLiteral("yandexGroup"));
    auto *yandexLayout = new QVBoxLayout(m_yandexGroup);
    auto *yandexHint = new QLabel(
        tr("Yandex web is an unofficial endpoint and needs no key. Enable free web services above "
           "to use it. It may stop working, apply rate limits or block requests. Long text is sent "
           "in several requests. The text is sent to a third party."),
        m_yandexGroup);
    yandexHint->setObjectName(QStringLiteral("yandexHint"));
    yandexHint->setWordWrap(true);
    yandexLayout->addWidget(yandexHint);
    servicesLayout->addWidget(m_yandexGroup);
    connect(m_allowFreeServices, &QCheckBox::toggled, this,
            &SettingsDialog::updateYandexControls);

    m_bingGroup = new QGroupBox(tr("Bing"), servicesContent);
    m_bingGroup->setObjectName(QStringLiteral("bingGroup"));
    auto *bingLayout = new QVBoxLayout(m_bingGroup);
    auto *bingHint = new QLabel(
        tr("Bing web is an unofficial endpoint and needs no key. Enable free web services above "
           "to use it. It may stop working, apply rate limits or block requests. Long text is sent "
           "in several requests. The text is sent to a third party."),
        m_bingGroup);
    bingHint->setObjectName(QStringLiteral("bingHint"));
    bingHint->setWordWrap(true);
    bingLayout->addWidget(bingHint);
    servicesLayout->addWidget(m_bingGroup);
    connect(m_allowFreeServices, &QCheckBox::toggled, this,
            &SettingsDialog::updateBingControls);

    m_reversoGroup = new QGroupBox(tr("Reverso"), servicesContent);
    m_reversoGroup->setObjectName(QStringLiteral("reversoGroup"));
    auto *reversoLayout = new QVBoxLayout(m_reversoGroup);
    auto *reversoHint = new QLabel(
        tr("Reverso is an unofficial endpoint and needs no key. It supports only English, French, "
           "German, Spanish, Italian and Russian and cannot detect the source language. Enable free "
           "web services above to use it. It may stop working, apply rate limits or block requests. "
           "Long text is sent in several requests. The text is sent to a third party."),
        m_reversoGroup);
    reversoHint->setObjectName(QStringLiteral("reversoHint"));
    reversoHint->setWordWrap(true);
    reversoLayout->addWidget(reversoHint);
    servicesLayout->addWidget(m_reversoGroup);
    connect(m_allowFreeServices, &QCheckBox::toggled, this,
            &SettingsDialog::updateReversoControls);

    servicesLayout->addStretch();
    servicesScroll->setWidget(servicesContent);
    servicesPageLayout->addWidget(servicesScroll);
    m_tabs->addTab(servicesPage, tr("Services"));

    // Network.
    auto *networkPage = new QWidget(m_tabs);
    auto *networkLayout = new QVBoxLayout(networkPage);
    auto *networkForm = new QFormLayout;
    m_proxyMode = new QComboBox(networkPage);
    m_proxyMode->setObjectName(QStringLiteral("proxyMode"));
    m_proxyMode->addItem(tr("No proxy"), QStringLiteral("none"));
    m_proxyMode->addItem(tr("System proxy"), QStringLiteral("system"));
    m_proxyMode->addItem(tr("Manual proxy"), QStringLiteral("manual"));
    m_proxyType = new QComboBox(networkPage);
    m_proxyType->setObjectName(QStringLiteral("proxyType"));
    m_proxyType->addItem(tr("HTTP"), QStringLiteral("http"));
    m_proxyType->addItem(tr("SOCKS5"), QStringLiteral("socks5"));
    m_proxyHost = new QLineEdit(networkPage);
    m_proxyHost->setObjectName(QStringLiteral("proxyHost"));
    m_proxyHost->setPlaceholderText(tr("Proxy host"));
    m_proxyPort = new QSpinBox(networkPage);
    m_proxyPort->setObjectName(QStringLiteral("proxyPort"));
    m_proxyPort->setRange(0, 65535);
    m_proxyPort->setSpecialValueText(tr("Not set"));
    m_proxyAuth = new QCheckBox(tr("The proxy requires authentication"), networkPage);
    m_proxyAuth->setObjectName(QStringLiteral("proxyAuth"));
    m_proxyUser = new QLineEdit(networkPage);
    m_proxyUser->setObjectName(QStringLiteral("proxyUser"));
    m_proxyUser->setPlaceholderText(tr("User name"));
    m_proxyPassword = new QLineEdit(networkPage);
    m_proxyPassword->setObjectName(QStringLiteral("proxyPassword"));
    m_proxyPassword->setEchoMode(QLineEdit::Password);
    m_proxyPassword->setPlaceholderText(tr("Password"));
    m_networkTimeout = new QSpinBox(networkPage);
    m_networkTimeout->setObjectName(QStringLiteral("networkTimeout"));
    m_networkTimeout->setRange(1000, 120000);
    m_networkTimeout->setSingleStep(1000);
    m_networkTimeout->setSuffix(tr(" ms"));
    m_testProxy = new QPushButton(tr("Test connection"), networkPage);
    m_testProxy->setObjectName(QStringLiteral("testProxy"));
    m_proxyTestStatus = new QLabel(networkPage);
    m_proxyTestStatus->setObjectName(QStringLiteral("proxyTestStatus"));
    m_proxyTestStatus->setWordWrap(true);
    m_proxyTestStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);

    networkForm->addRow(tr("Proxy mode:"), m_proxyMode);
    networkForm->addRow(tr("Proxy type:"), m_proxyType);
    networkForm->addRow(tr("Host:"), m_proxyHost);
    networkForm->addRow(tr("Port:"), m_proxyPort);
    networkForm->addRow(m_proxyAuth);
    networkForm->addRow(tr("User name:"), m_proxyUser);
    networkForm->addRow(tr("Password:"), m_proxyPassword);
    networkForm->addRow(tr("Request timeout:"), m_networkTimeout);
    networkForm->addRow(m_testProxy);
    networkForm->addRow(m_proxyTestStatus);
    networkLayout->addLayout(networkForm);
    networkLayout->addStretch();
    m_tabs->addTab(networkPage, tr("Network"));

    connect(m_proxyMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::updateProxyControls);
    connect(m_proxyType, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::updateProxyControls);
    connect(m_proxyAuth, &QCheckBox::toggled, this, &SettingsDialog::updateProxyControls);
    connect(m_testProxy, &QPushButton::clicked, this, [this] {
        m_proxyTestStatus->setStyleSheet(QString());
        m_proxyTestStatus->setText(tr("Checking…"));
        m_testProxy->setEnabled(false);
        emit proxyTestRequested();
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("settingsButtons"));
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(buttons);
}

void SettingsDialog::loadValues(const AppSettings &settings, bool autostartApproved,
                                bool secretStorageFallback)
{
    m_autostart->setChecked(settings.general.autostart);
    m_startMinimized->setChecked(settings.general.startMinimized);
    m_restoreClipboard->setChecked(settings.general.restoreClipboard);
    m_autoClosePopup->setChecked(settings.popup.autoCloseOnFocusLoss);
    const int uiLanguage = m_uiLanguage->findData(settings.general.uiLanguage);
    m_uiLanguage->setCurrentIndex(uiLanguage >= 0 ? uiLanguage : 0);

    auto updateAutostartHint = [this, autostartApproved] {
        m_autostartHint->setVisible(!autostartApproved && m_autostart->isChecked());
    };
    connect(m_autostart, &QCheckBox::toggled, this, updateAutostartHint);
    updateAutostartHint();

    m_secretStorageHint->setVisible(secretStorageFallback);

    m_popupHotkey->setKeySequence(settings.hotkeys.popup);
    m_replaceSelectionHotkey->setKeySequence(settings.hotkeys.replaceSelection);
    m_mainWindowHotkey->setKeySequence(settings.hotkeys.mainWindow);
    m_showMainWindowHotkey->setKeySequence(settings.hotkeys.showMainWindow);
    m_pauseInFullscreen->setChecked(settings.hotkeys.pauseInFullscreen);

    if (!m_sourceLanguage->setCurrentCode(settings.translation.sourceLang))
        m_sourceLanguage->setCurrentCode(Languages::autoCode());
    if (!m_primaryLanguage->setCurrentCode(settings.translation.primaryLang)
        && m_primaryLanguage->count() > 0) {
        m_primaryLanguage->setCurrentIndex(0);
    }
    if (!m_secondaryLanguage->setCurrentCode(settings.translation.secondaryLang)
        && m_secondaryLanguage->count() > 0) {
        m_secondaryLanguage->setCurrentIndex(0);
    }

    m_maxLength->setValue(settings.translation.maxLength);
    m_autoTranslateDelay->setValue(settings.translation.autoTranslateDelayMs);
    m_allowFreeServices->setChecked(settings.translation.allowFreeServices);
    {
        const QSignalBlocker blocker(m_mozhiInstance);
        m_mozhiInstance->setText(settings.mozhi.instance);
    }
    const int mozhiEngine =
        m_mozhiEngine->findData(normalizedMozhiEngine(settings.mozhi.engine));
    m_mozhiEngine->setCurrentIndex(mozhiEngine >= 0 ? mozhiEngine : 0);

    const int googleMode = m_googleMode->findData(settings.google.mode);
    m_googleMode->setCurrentIndex(googleMode >= 0 ? googleMode : 0);
    m_googleApiKey->setText(settings.google.apiKey);

    const int deeplMode = m_deeplMode->findData(settings.deepl.mode);
    m_deeplMode->setCurrentIndex(deeplMode >= 0 ? deeplMode : 0);
    m_deeplApiKey->setText(settings.deepl.apiKey);
    const int englishVariant = m_deeplEnglishVariant->findData(settings.deepl.englishVariant);
    m_deeplEnglishVariant->setCurrentIndex(englishVariant >= 0 ? englishVariant : 0);
    m_deepLKeyStatus->clear();
    m_checkDeepLKey->setEnabled(true);
    updateDeepLControls();

    m_llmBaseUrl->setText(settings.llm.baseUrl);
    m_llmApiKey->setText(settings.llm.apiKey);
    m_llmModel->setCurrentText(settings.llm.model);
    m_llmStatus->clear();
    m_loadLlmModels->setEnabled(true);
    updateGoogleControls();

    const int proxyMode = m_proxyMode->findData(settings.network.proxyMode);
    m_proxyMode->setCurrentIndex(proxyMode >= 0 ? proxyMode : 1);
    const int proxyType = m_proxyType->findData(settings.network.proxyType);
    m_proxyType->setCurrentIndex(proxyType >= 0 ? proxyType : 0);
    m_proxyHost->setText(settings.network.proxyHost);
    m_proxyPort->setValue(settings.network.proxyPort);
    m_proxyAuth->setChecked(settings.network.proxyAuth);
    m_proxyUser->setText(settings.network.proxyUser);
    m_proxyPassword->setText(settings.network.proxyPassword);
    m_networkTimeout->setValue(settings.network.timeoutMs);
    m_proxyTestStatus->clear();
    m_testProxy->setEnabled(true);
    updateProxyControls();
    updateMyMemoryControls();
    updateMozhiControls();
    updateYandexControls();
    updateBingControls();
    updateReversoControls();
}

AppSettings SettingsDialog::settings() const
{
    AppSettings result = m_initial;
    result.general.autostart = m_autostart->isChecked();
    result.general.startMinimized = m_startMinimized->isChecked();
    result.general.restoreClipboard = m_restoreClipboard->isChecked();
    result.general.uiLanguage = m_uiLanguage->currentData().toString();
    result.hotkeys.popup = m_popupHotkey->keySequence();
    result.hotkeys.replaceSelection = m_replaceSelectionHotkey->keySequence();
    result.hotkeys.mainWindow = m_mainWindowHotkey->keySequence();
    result.hotkeys.showMainWindow = m_showMainWindowHotkey->keySequence();
    result.hotkeys.pauseInFullscreen = m_pauseInFullscreen->isChecked();
    result.translation.sourceLang = m_sourceLanguage->currentCode();
    result.translation.primaryLang = m_primaryLanguage->currentCode();
    result.translation.secondaryLang = m_secondaryLanguage->currentCode();
    result.translation.maxLength = m_maxLength->value();
    result.translation.autoTranslateDelayMs = m_autoTranslateDelay->value();
    result.translation.allowFreeServices = m_allowFreeServices->isChecked();
    result.popup.autoCloseOnFocusLoss = m_autoClosePopup->isChecked();
    result.google.mode = m_googleMode->currentData().toString();
    result.google.apiKey = m_googleApiKey->text().trimmed();
    result.deepl.mode = m_deeplMode->currentData().toString();
    result.deepl.apiKey = m_deeplApiKey->text().trimmed();
    result.deepl.englishVariant = m_deeplEnglishVariant->currentData().toString();
    result.mozhi.instance = normalizedMozhiInstance(m_mozhiInstance->text());
    result.mozhi.engine = normalizedMozhiEngine(m_mozhiEngine->currentData().toString());
    result.llm.baseUrl = m_llmBaseUrl->text().trimmed();
    result.llm.apiKey = m_llmApiKey->text().trimmed();
    result.llm.model = m_llmModel->currentText().trimmed();
    result.network.proxyMode = m_proxyMode->currentData().toString();
    result.network.proxyType = m_proxyType->currentData().toString();
    result.network.proxyHost = m_proxyHost->text().trimmed();
    result.network.proxyPort = m_proxyPort->value();
    result.network.proxyAuth = m_proxyAuth->isChecked();
    result.network.proxyUser = m_proxyUser->text();
    result.network.proxyPassword = m_proxyPassword->text();
    result.network.timeoutMs = m_networkTimeout->value();
    return result;
}

void SettingsDialog::showDeepLKeyCheckResult(bool ok, const QString &message)
{
    m_deepLKeyStatus->setText(message);
    m_deepLKeyStatus->setStyleSheet(ok ? QStringLiteral("color: #2e7d32;")
                                       : QStringLiteral("color: #d32f2f;"));
    m_checkDeepLKey->setEnabled(true);
    updateDeepLControls();
}

void SettingsDialog::showLlmModels(bool ok, const QString &message,
                                          const QStringList &models)
{
    m_llmStatus->setText(message);
    m_llmStatus->setStyleSheet(ok ? QStringLiteral("color: #2e7d32;")
                                        : QStringLiteral("color: #d32f2f;"));
    m_loadLlmModels->setEnabled(true);

    if (!ok || models.isEmpty())
        return;

    const QString current = m_llmModel->currentText().trimmed();
    const QSignalBlocker blocker(m_llmModel);
    m_llmModel->clear();
    for (const QString &model : models)
        m_llmModel->addItem(model, model);
    const int currentIndex = m_llmModel->findText(current);
    if (currentIndex >= 0)
        m_llmModel->setCurrentIndex(currentIndex);
    else if (m_llmModel->count() > 0)
        m_llmModel->setCurrentIndex(0);
}

void SettingsDialog::showProxyTestResult(bool ok, const QString &message)
{
    m_proxyTestStatus->setText(message);
    m_proxyTestStatus->setStyleSheet(ok ? QStringLiteral("color: #2e7d32;")
                                        : QStringLiteral("color: #d32f2f;"));
    m_testProxy->setEnabled(true);
}

void SettingsDialog::updateGoogleControls()
{
    m_googleApiKey->setEnabled(m_googleMode->currentData().toString()
                               == QStringLiteral("cloud"));
}

void SettingsDialog::updateDeepLControls()
{
    const bool freeWeb =
        m_deeplMode->currentData().toString() == QStringLiteral("free-web");
    m_deeplApiKey->setEnabled(!freeWeb);
    m_checkDeepLKey->setEnabled(!freeWeb);
    m_deepLKeyStatus->setEnabled(!freeWeb);
    m_deepLWebHint->setVisible(freeWeb);
}

void SettingsDialog::updateMyMemoryControls()
{
    m_mymemoryGroup->setEnabled(m_allowFreeServices->isChecked());
}

void SettingsDialog::updateMozhiControls()
{
    m_mozhiGroup->setEnabled(m_allowFreeServices->isChecked());
    const QString text = m_mozhiInstance->text();
    m_mozhiEffectiveInstance->setText(
        tr("Effective instance: %1").arg(normalizedMozhiInstance(text)));
    m_mozhiInstanceWarning->setVisible(!isValidMozhiInstance(text));
}

void SettingsDialog::updateYandexControls()
{
    m_yandexGroup->setEnabled(m_allowFreeServices->isChecked());
}

void SettingsDialog::updateBingControls()
{
    m_bingGroup->setEnabled(m_allowFreeServices->isChecked());
}

void SettingsDialog::updateReversoControls()
{
    m_reversoGroup->setEnabled(m_allowFreeServices->isChecked());
}

void SettingsDialog::updateProxyControls()
{
    const bool manual = m_proxyMode->currentData().toString() == QStringLiteral("manual");
    m_proxyType->setEnabled(manual);
    m_proxyHost->setEnabled(manual);
    m_proxyPort->setEnabled(manual);
    m_proxyAuth->setEnabled(manual);
    const bool auth = manual && m_proxyAuth->isChecked();
    m_proxyUser->setEnabled(auth);
    m_proxyPassword->setEnabled(auth);
}

bool SettingsDialog::validate(QString *error) const
{
    const struct
    {
        QKeySequence sequence;
        QString name;
    } hotkeys[] = {
        {m_popupHotkey->keySequence(), tr("Translate in a popup")},
        {m_replaceSelectionHotkey->keySequence(),
         tr("Replace the selected text with the translation")},
        {m_mainWindowHotkey->keySequence(), tr("Translate in the main window")},
        {m_showMainWindowHotkey->keySequence(), tr("Show the main window")},
    };

    for (const auto &hotkey : hotkeys) {
        if (hotkey.sequence.isEmpty())
            continue;
        if (hotkey.sequence.count() != 1) {
            if (error) {
                *error = tr("\"%1\" uses more than one key combination. "
                            "Global hotkeys support a single combination.")
                             .arg(hotkey.sequence.toString(QKeySequence::NativeText));
            }
            return false;
        }
        if (!HotkeySequence::fromKeySequence(hotkey.sequence).isValid()) {
            if (error) {
                *error = tr("\"%1\" cannot be used as a global hotkey.")
                             .arg(hotkey.sequence.toString(QKeySequence::NativeText));
            }
            return false;
        }
    }

    for (int i = 0; i < int(std::size(hotkeys)); ++i) {
        if (hotkeys[i].sequence.isEmpty())
            continue;
        for (int j = i + 1; j < int(std::size(hotkeys)); ++j) {
            if (hotkeys[j].sequence == hotkeys[i].sequence) {
                if (error) {
                    *error = tr("The same key combination is assigned more than once: %1.")
                                 .arg(hotkeys[i].sequence.toString(QKeySequence::NativeText));
                }
                return false;
            }
        }
    }

    if (!m_primaryLanguage->currentCode().isEmpty()
        && m_primaryLanguage->currentCode() == m_secondaryLanguage->currentCode()) {
        if (error)
            *error = tr("The primary and additional languages must differ.");
        return false;
    }

    if (m_proxyMode->currentData().toString() == QStringLiteral("manual")
        && (m_proxyHost->text().trimmed().isEmpty() || m_proxyPort->value() == 0)) {
        if (error)
            *error = tr("Enter the proxy host and port, or choose another proxy mode.");
        return false;
    }
    return true;
}

void SettingsDialog::accept()
{
    QString error;
    if (!validate(&error)) {
        QMessageBox::warning(this, tr("Settings"), error);
        return;
    }
    QDialog::accept();
}

void SettingsDialog::closeEvent(QCloseEvent *event)
{
    setHotkeyEditing(false);
    QDialog::closeEvent(event);
}

bool SettingsDialog::anyHotkeyEditFocused() const
{
    const QWidget *focus = QApplication::focusWidget();
    if (!focus)
        return false;
    for (const QKeySequenceEdit *edit :
         {m_popupHotkey, m_replaceSelectionHotkey, m_mainWindowHotkey,
          m_showMainWindowHotkey}) {
        if (edit == focus || edit->isAncestorOf(focus))
            return true;
    }
    return false;
}

void SettingsDialog::updateHotkeyEditing()
{
    setHotkeyEditing(anyHotkeyEditFocused());
}

void SettingsDialog::setHotkeyEditing(bool editing)
{
    if (m_hotkeyEditing == editing)
        return;
    m_hotkeyEditing = editing;
    emit hotkeyEditingChanged(editing);
}
