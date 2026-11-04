#include "app/appcontroller.h"

#include "app/logging.h"
#include "app/singleinstance.h"
#include "core/appinfo.h"
#include "core/languages.h"
#include "core/settings.h"
#include "core/translationmanager.h"
#include "core/translationservice.h"
#include "net/networkmanager.h"
#include "platform/autostart.h"
#include "platform/hotkeymanager.h"
#include "platform/selectiongrabber.h"
#include "services/deeplservice.h"
#include "services/googletranslateservice.h"
#include "ui/appicon.h"
#include "ui/mainwindow.h"
#include "ui/popupwindow.h"
#include "ui/settingsdialog.h"
#include "ui/trayicon.h"

#include <QApplication>
#include <QCursor>
#include <QDialog>
#include <QElapsedTimer>
#include <QEvent>
#include <QSessionManager>
#include <QTranslator>
#include <QUrl>

namespace {

ProxySettings toProxySettings(const NetworkSettings &network)
{
    ProxySettings settings;
    settings.mode = network.proxyMode == QStringLiteral("none")
        ? ProxyMode::None
        : network.proxyMode == QStringLiteral("manual") ? ProxyMode::Manual
                                                        : ProxyMode::System;
    settings.type = network.proxyType == QStringLiteral("socks5") ? ProxyType::Socks5
                                                                  : ProxyType::Http;
    settings.host = network.proxyHost;
    settings.port = quint16(network.proxyPort);
    settings.authEnabled = network.proxyAuth;
    settings.user = network.proxyUser;
    settings.password = network.proxyPassword;
    return settings;
}

} // namespace

AppController::AppController(SingleInstance *instance, SettingsStore *settings,
                             const Options &options, QObject *parent)
    : QObject(parent)
    , m_instance(instance)
    , m_settings(settings)
    , m_options(options)
{
}

AppController::~AppController() = default;

void AppController::start()
{
    const AppSettings settings = m_settings ? m_settings->current() : AppSettings{};

    const QIcon icon = AppIcon::create();
    QApplication::setWindowIcon(icon);

    m_trayAvailable = TrayIcon::isAvailable();

    applyUiLanguage(settings.general.uiLanguage);

    createTranslation();
    connect(m_translations, &TranslationManager::activeServiceChanged, this,
            &AppController::onActiveServiceChanged);

    m_mainWindow = std::make_unique<MainWindow>();
    m_mainWindow->setCloseHidesToTray(m_trayAvailable);
    m_mainWindow->setAutoTranslateDelay(settings.translation.autoTranslateDelayMs);
    if (!settings.mainWindow.geometry.isEmpty())
        m_mainWindow->restoreGeometry(settings.mainWindow.geometry);
    if (!settings.mainWindow.state.isEmpty())
        m_mainWindow->restoreState(settings.mainWindow.state);
    initMainWindowLanguages();
    connect(m_mainWindow.get(), &MainWindow::translateRequested, this,
            &AppController::translateInMainWindow);
    connect(m_mainWindow.get(), &MainWindow::serviceChanged, this,
            &AppController::selectService);

    if (m_trayAvailable) {
        m_tray = new TrayIcon(icon, this);
        connect(m_tray, &TrayIcon::showWindowRequested, this, &AppController::showMainWindow);
        connect(m_tray, &TrayIcon::toggleWindowRequested, this, &AppController::toggleMainWindow);
        connect(m_tray, &TrayIcon::serviceSelected, this, &AppController::selectService);
        connect(m_tray, &TrayIcon::settingsRequested, this, &AppController::showSettings);
        connect(m_tray, &TrayIcon::quitRequested, this, &AppController::quit);
        m_tray->show();
    } else {
        qCWarning(lcApp) << "System tray is not available; closing the window quits";
        // Without a tray, the last closed window has to end the application.
        QApplication::setQuitOnLastWindowClosed(true);
    }

    createHotkeys();
    updateServiceSelectors();

    if (m_settings) {
        connect(m_settings, &SettingsStore::changed, this, &AppController::applySettings);
    }

    if (!Autostart::sync(settings.general.autostart)) {
        qCWarning(lcApp) << "Could not synchronize the autostart entry";
        if (m_tray) {
            m_tray->showMessage(tr("Autostart"),
                                tr("Could not update the autostart entry."));
        }
    }

    connect(m_instance, &SingleInstance::activateRequested, this, &AppController::showMainWindow);

    // Let the window close for real when the app quits or the session ends.
    connect(qApp, &QCoreApplication::aboutToQuit, this, &AppController::prepareForQuit);
    connect(qApp, &QGuiApplication::commitDataRequest, this,
            [this](QSessionManager &) { prepareForQuit(); });

    const bool startHidden = m_options.startHidden || settings.general.startMinimized;
    if (!startHidden || !m_trayAvailable)
        showMainWindow();
}

void AppController::createTranslation()
{
    const AppSettings settings = m_settings ? m_settings->current() : AppSettings{};

    m_network = new NetworkManager(this);
    m_network->setTimeoutMs(settings.network.timeoutMs);
    m_network->applyProxy(toProxySettings(settings.network));
    m_translations = new TranslationManager(this);
    m_translations->setMaxLength(settings.translation.maxLength);
    m_translations->setPrimaryLanguage(settings.translation.primaryLang);
    m_translations->setSecondaryLanguage(settings.translation.secondaryLang);

    m_google = new GoogleTranslateService(m_network);
    m_deepl = new DeepLService(m_network);
    m_google->applySettings(settings);
    m_deepl->applySettings(settings);
    m_translations->registerService(m_google);
    m_translations->registerService(m_deepl);

    if (m_translations->service(settings.translation.service))
        m_translations->setActiveService(settings.translation.service);
    else if (!settings.translation.service.isEmpty())
        qCWarning(lcApp) << "Configured service is not available:"
                         << settings.translation.service;
}

void AppController::createHotkeys()
{
    const AppSettings settings = m_settings ? m_settings->current() : AppSettings{};

    m_hotkeys = new HotkeyManager(this);
    m_grabber = new SelectionGrabber(this);
    m_grabber->setMaxLength(m_translations->maxLength());
    m_grabber->setRestoreClipboard(settings.general.restoreClipboard);

    connect(m_hotkeys, &HotkeyManager::activated, this, [this](HotkeyAction action) {
        switch (action) {
        case HotkeyAction::TranslateToPopup:
            translateSelectionInPopup();
            break;
        case HotkeyAction::TranslateToMainWindow:
            translateSelectionInMainWindow();
            break;
        case HotkeyAction::ShowMainWindow:
            showMainWindow();
            break;
        }
    });

    connect(m_hotkeys, &HotkeyManager::registrationFailed, this,
            [this](HotkeyAction, const QKeySequence &sequence, const QString &reason) {
                const QString keys = sequence.toString(QKeySequence::NativeText);
                qCWarning(lcApp) << "Hotkey" << keys << "is unavailable:" << reason;
                if (m_tray) {
                    m_tray->showMessage(tr("Hotkey unavailable"),
                                        tr("%1 is already used by another program. "
                                           "Close that program or choose another combination "
                                           "in the settings.")
                                            .arg(keys));
                }
            });

    const auto registerHotkey = [this](HotkeyAction action, const QKeySequence &sequence) {
        if (!sequence.isEmpty())
            m_hotkeys->registerHotkey(action, sequence);
    };
    registerHotkey(HotkeyAction::TranslateToPopup, settings.hotkeys.popup);
    registerHotkey(HotkeyAction::TranslateToMainWindow, settings.hotkeys.mainWindow);
    registerHotkey(HotkeyAction::ShowMainWindow, settings.hotkeys.showMainWindow);
    updateTrayToolTip();
}

void AppController::updateTrayToolTip()
{
    if (!m_tray || !m_hotkeys)
        return;

    QStringList keys;
    for (const HotkeyAction action :
         {HotkeyAction::TranslateToPopup, HotkeyAction::TranslateToMainWindow,
          HotkeyAction::ShowMainWindow}) {
        const QKeySequence sequence = m_hotkeys->hotkey(action);
        if (!sequence.isEmpty())
            keys << sequence.toString(QKeySequence::NativeText);
    }

    m_tray->setToolTip(keys.isEmpty()
                           ? AppInfo::name()
                           : tr("%1 — %2").arg(AppInfo::name(), keys.join(QStringLiteral(" / "))));
}

void AppController::applyUiLanguage(const QString &code)
{
    QString language = code;
    if (language.isEmpty() || language == QStringLiteral("system")) {
        language = Languages::systemLanguage() == QStringLiteral("ru")
            ? QStringLiteral("ru")
            : QStringLiteral("en");
    }
    if (language != QStringLiteral("ru"))
        language = QStringLiteral("en");

    if (m_translator) {
        QApplication::removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }

    if (language == QStringLiteral("ru")) {
        auto *translator = new QTranslator(this);
        if (translator->load(QStringLiteral(":/i18n/qtranslate_ru_RU.qm"))) {
            m_translator = translator;
            QApplication::installTranslator(translator);
        } else {
            qCWarning(lcApp) << "Cannot load the Russian translation";
            delete translator;
        }
    }

    // updateLanguageConfiguration() rebuilds the language lists with the new
    // translations; top-level widgets get LanguageChange for their own labels.
    QEvent languageChange(QEvent::LanguageChange);
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget *widget : widgets)
        QCoreApplication::sendEvent(widget, &languageChange);

    if (m_tray)
        m_tray->retranslate();
    if (m_mainWindow)
        m_mainWindow->retranslateUi();
    if (m_popup) {
        m_popup->hide();
        m_popup.reset();
    }

    updateLanguageConfiguration();
    updateTrayToolTip();
}

void AppController::initMainWindowLanguages()
{
    updateLanguageConfiguration();
}

void AppController::updateLanguageConfiguration()
{
    TranslationService *service = m_translations ? m_translations->activeService() : nullptr;
    if (!service || !m_mainWindow)
        return;

    const QStringList sourceLanguages = service->sourceLanguages();
    m_targetLanguages = service->targetLanguages();

    const AppSettings settings = m_settings ? m_settings->current() : AppSettings{};
    QString source = settings.translation.sourceLang;
    if (source != Languages::autoCode() && !sourceLanguages.contains(source))
        source = Languages::autoCode();

    QString target = Languages::closest(service->id(), settings.translation.primaryLang,
                                        Languages::Direction::Target);

    m_sourceLanguage = source;
    m_targetLanguage = target;

    m_mainWindow->setLanguages(sourceLanguages, m_targetLanguages);
    m_mainWindow->setSourceLanguage(m_sourceLanguage);
    m_mainWindow->setTargetLanguage(m_targetLanguage);

    if (m_popup) {
        m_popup->setLanguages(m_targetLanguages);
        m_popup->setTargetLanguage(m_targetLanguage);
        m_popup->setServiceName(service->displayName());
        m_popup->setPreferredWidth(settings.popup.width);
        m_popup->setAutoCloseOnFocusLoss(settings.popup.autoCloseOnFocusLoss);
    }

    updateServiceSelectors();
}

void AppController::updateServiceSelectors()
{
    if (!m_translations)
        return;

    const TranslationService *active = m_translations->activeService();
    const QString activeId = active ? active->id() : QString();

    QList<MainWindow::ServiceEntry> mainWindowServices;
    QList<TrayIcon::ServiceEntry> trayServices;
    for (const TranslationService *service : m_translations->services()) {
        mainWindowServices.append({service->id(), service->displayName()});
        trayServices.append({service->id(), service->displayName()});
    }

    if (m_mainWindow)
        m_mainWindow->setServices(mainWindowServices, activeId);
    if (m_tray)
        m_tray->setServices(trayServices, activeId);
}

void AppController::onActiveServiceChanged(const QString &)
{
    updateLanguageConfiguration();
}

void AppController::selectService(const QString &id)
{
    if (!m_translations || !m_translations->service(id))
        return;

    const TranslationService *active = m_translations->activeService();
    if (active && active->id() != id)
        m_translations->setActiveService(id);

    if (m_settings && m_settings->current().translation.service != id) {
        AppSettings settings = m_settings->current();
        settings.translation.service = id;
        m_settings->save(settings);
    }
    updateServiceSelectors();
}

void AppController::applySettings(const AppSettings &oldSettings,
                                  const AppSettings &newSettings)
{
    if (m_quitting)
        return;

    const auto updateHotkey = [this](HotkeyAction action, const QKeySequence &oldSequence,
                                     const QKeySequence &newSequence) {
        if (oldSequence == newSequence)
            return;
        if (newSequence.isEmpty())
            m_hotkeys->unregisterHotkey(action);
        else
            m_hotkeys->registerHotkey(action, newSequence);
    };

    updateHotkey(HotkeyAction::TranslateToPopup, oldSettings.hotkeys.popup,
                 newSettings.hotkeys.popup);
    updateHotkey(HotkeyAction::TranslateToMainWindow, oldSettings.hotkeys.mainWindow,
                 newSettings.hotkeys.mainWindow);
    updateHotkey(HotkeyAction::ShowMainWindow, oldSettings.hotkeys.showMainWindow,
                 newSettings.hotkeys.showMainWindow);
    updateTrayToolTip();

    if (oldSettings.general.restoreClipboard != newSettings.general.restoreClipboard)
        m_grabber->setRestoreClipboard(newSettings.general.restoreClipboard);

    if (oldSettings.translation.maxLength != newSettings.translation.maxLength) {
        m_translations->setMaxLength(newSettings.translation.maxLength);
        m_grabber->setMaxLength(newSettings.translation.maxLength);
    }

    if (oldSettings.translation.autoTranslateDelayMs
        != newSettings.translation.autoTranslateDelayMs) {
        m_mainWindow->setAutoTranslateDelay(newSettings.translation.autoTranslateDelayMs);
    }

    if (oldSettings.network.timeoutMs != newSettings.network.timeoutMs)
        m_network->setTimeoutMs(newSettings.network.timeoutMs);

    if (oldSettings.network.proxyMode != newSettings.network.proxyMode
        || oldSettings.network.proxyType != newSettings.network.proxyType
        || oldSettings.network.proxyHost != newSettings.network.proxyHost
        || oldSettings.network.proxyPort != newSettings.network.proxyPort
        || oldSettings.network.proxyAuth != newSettings.network.proxyAuth
        || oldSettings.network.proxyUser != newSettings.network.proxyUser
        || oldSettings.network.proxyPassword != newSettings.network.proxyPassword) {
        m_network->applyProxy(toProxySettings(newSettings.network));
    }

    if (m_google)
        m_google->applySettings(newSettings);
    if (m_deepl)
        m_deepl->applySettings(newSettings);

    if (oldSettings.translation.service != newSettings.translation.service) {
        if (m_translations->service(newSettings.translation.service))
            m_translations->setActiveService(newSettings.translation.service);
        else
            qCWarning(lcApp) << "Configured service is not available:"
                             << newSettings.translation.service;
    }

    if (oldSettings.translation.service != newSettings.translation.service
        || oldSettings.translation.sourceLang != newSettings.translation.sourceLang
        || oldSettings.translation.primaryLang != newSettings.translation.primaryLang
        || oldSettings.translation.secondaryLang != newSettings.translation.secondaryLang) {
        if (oldSettings.translation.primaryLang != newSettings.translation.primaryLang)
            m_translations->setPrimaryLanguage(newSettings.translation.primaryLang);
        if (oldSettings.translation.secondaryLang != newSettings.translation.secondaryLang)
            m_translations->setSecondaryLanguage(newSettings.translation.secondaryLang);
        updateLanguageConfiguration();
    }

    if (m_popup) {
        if (oldSettings.popup.width != newSettings.popup.width)
            m_popup->setPreferredWidth(newSettings.popup.width);
        if (oldSettings.popup.autoCloseOnFocusLoss
            != newSettings.popup.autoCloseOnFocusLoss) {
            m_popup->setAutoCloseOnFocusLoss(newSettings.popup.autoCloseOnFocusLoss);
        }
    }

    if (oldSettings.general.autostart != newSettings.general.autostart) {
        if (!Autostart::sync(newSettings.general.autostart)) {
            qCWarning(lcApp) << "Could not update the autostart entry";
            if (m_tray) {
                m_tray->showMessage(tr("Autostart"),
                                    tr("Could not update the autostart entry."));
            }
        }
    }

    if (oldSettings.general.uiLanguage != newSettings.general.uiLanguage)
        applyUiLanguage(newSettings.general.uiLanguage);
}

void AppController::showSettings()
{
    if (!m_settings)
        return;

    const TranslationService *service = m_translations->activeService();
    SettingsDialog dialog(m_settings->current(),
                          service ? service->sourceLanguages() : QStringList(),
                          service ? service->targetLanguages() : QStringList(),
                          Autostart::isApproved(), m_mainWindow.get());
    if (m_hotkeys) {
        connect(&dialog, &SettingsDialog::hotkeyEditingChanged, m_hotkeys,
                &HotkeyManager::setSuspended);
    }

    QPointer<SettingsDialog> dialogGuard(&dialog);
    connect(&dialog, &SettingsDialog::deepLKeyCheckRequested, this,
            [this, dialogGuard](const QString &key) {
                if (!m_deepl) {
                    if (dialogGuard) {
                        dialogGuard->showDeepLKeyCheckResult(
                            false, tr("DeepL is not available."));
                    }
                    return;
                }
                m_deepl->checkKey(key, [dialogGuard](bool ok, const QString &message) {
                    if (dialogGuard)
                        dialogGuard->showDeepLKeyCheckResult(ok, message);
                });
            });

    connect(&dialog, &SettingsDialog::proxyTestRequested, this, [this, dialogGuard] {
        if (!dialogGuard)
            return;
        const NetworkSettings network = dialogGuard->settings().network;
        m_network->testProxy(
            toProxySettings(network), QUrl(QStringLiteral("https://translate.googleapis.com/")),
            [dialogGuard](bool ok, const QString &message) {
                if (dialogGuard)
                    dialogGuard->showProxyTestResult(ok, message);
            });
    });

    if (dialog.exec() == QDialog::Accepted)
        m_settings->save(dialog.settings());

    if (m_hotkeys)
        m_hotkeys->setSuspended(false);
}

void AppController::translateInMainWindow(const TranslationRequest &request)
{
    // Cancels the previous main-window request; its finished() arrives right
    // here with Cancelled and is ignored below.
    TranslationReply *reply =
        m_translations->translate(request, TranslationManager::Channel::MainWindow);
    reply->setParent(this); // freed with the controller if still pending at exit
    m_mainWindowReply = reply;
    m_mainWindow->showTranslationStarted();

    connect(reply, &TranslationReply::finished, this, [this, reply] {
        if (reply == m_mainWindowReply && reply->result().error != TranslationError::Cancelled)
            m_mainWindow->showResult(reply->result());
        reply->deleteLater();
    });
}

void AppController::translateSelectionInMainWindow()
{
    if (m_grabber->isBusy()) {
        // Showing or activating the window now would steal the focus from the
        // application whose selection is still being copied.
        qCDebug(lcApp) << "Ctrl+W ignored: a selection grab is already in progress";
        return;
    }

    // The window is shown only after the selection has been copied: showing it
    // earlier would move the focus and Ctrl+C would go to our own window.
    m_grabber->grab([this](const QString &text) {
        showMainWindow();
        if (!text.isEmpty())
            m_mainWindow->translateText(text);
    });
}

PopupWindow *AppController::ensurePopup()
{
    if (m_popup)
        return m_popup.get();

    m_popup = std::make_unique<PopupWindow>();
    const AppSettings settings = m_settings ? m_settings->current() : AppSettings{};
    m_popup->setPreferredWidth(settings.popup.width);
    m_popup->setAutoCloseOnFocusLoss(settings.popup.autoCloseOnFocusLoss);
    m_popup->setLanguages(m_targetLanguages);
    m_popup->setTargetLanguage(m_targetLanguage);
    if (const TranslationService *service = m_translations->activeService())
        m_popup->setServiceName(service->displayName());

    connect(m_popup.get(), &PopupWindow::translateRequested, this,
            &AppController::translateInPopup);
    connect(m_popup.get(), &PopupWindow::openInMainWindowRequested, this,
            &AppController::openPopupInMainWindow);
    connect(m_popup.get(), &PopupWindow::closed, this, [this] {
        // The window is gone; an answer that is still on its way must not
        // pop it up again or replace the next request.
        m_popupReply = nullptr;
        m_translations->cancel(TranslationManager::Channel::Popup);
    });
    return m_popup.get();
}

void AppController::translateSelectionInPopup()
{
    if (m_grabber->isBusy()) {
        // A selection grab is already talking to the source application;
        // activating the popup now would break Ctrl+C.
        qCDebug(lcApp) << "Ctrl+Q ignored: a selection grab is already in progress";
        return;
    }

    // The anchor must be the cursor position at the moment of the hotkey:
    // the user may move the mouse while the selection is being copied.
    const QPoint anchor = QCursor::pos();
    QElapsedTimer timer;
    timer.start();

    m_grabber->grab([this, anchor, timer](const QString &text) {
        PopupWindow *popup = ensurePopup();
        if (text.isEmpty()) {
            m_popupReply = nullptr;
            m_translations->cancel(TranslationManager::Channel::Popup);
            popup->showNoSelection(anchor);
            qCDebug(lcApp) << "Popup shown (nothing selected) in" << timer.elapsed() << "ms";
            return;
        }

        const TranslationRequest request{text, m_sourceLanguage,
                                         popup->targetLanguage()};
        popup->showLoading(request, anchor);
        qCDebug(lcApp) << "Popup shown in" << timer.elapsed() << "ms after Ctrl+Q";
    });
}

void AppController::translateInPopup(const TranslationRequest &request)
{
    // Cancels the previous popup request; its finished() arrives right here
    // with Cancelled and is ignored below.
    TranslationReply *reply =
        m_translations->translate(request, TranslationManager::Channel::Popup);
    reply->setParent(this); // freed with the controller if still pending at exit
    m_popupReply = reply;

    connect(reply, &TranslationReply::finished, this, [this, reply] {
        if (reply == m_popupReply) {
            m_popupReply = nullptr;
            if (m_popup && reply->result().error != TranslationError::Cancelled)
                m_popup->showResult(reply->result());
        }
        reply->deleteLater();
    });
}

void AppController::openPopupInMainWindow(const TranslationRequest &request,
                                          const TranslationResult &result)
{
    m_mainWindow->showTranslation(request, result);
    showMainWindow();
}

void AppController::showMainWindow()
{
    m_mainWindow->showAndActivate();
}

void AppController::toggleMainWindow()
{
    if (m_mainWindow->isVisible() && !m_mainWindow->isMinimized())
        m_mainWindow->hide();
    else
        showMainWindow();
}

void AppController::quit()
{
    prepareForQuit();
    QApplication::quit();
}

void AppController::prepareForQuit()
{
    if (m_quitting)
        return;
    m_quitting = true;

    if (m_settings && m_mainWindow) {
        AppSettings settings = m_settings->current();
        settings.mainWindow.geometry = m_mainWindow->saveGeometry();
        settings.mainWindow.state = m_mainWindow->saveState();
        m_settings->save(settings);
    }

    if (m_hotkeys)
        m_hotkeys->unregisterAll();
    if (m_grabber)
        m_grabber->cancel();
    if (m_translations)
        m_translations->cancelAll();
    if (m_popup)
        m_popup->hide();
    if (m_mainWindow)
        m_mainWindow->setCloseHidesToTray(false);
}
