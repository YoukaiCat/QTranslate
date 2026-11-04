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
#include <QSessionManager>

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

    createTranslation();

    m_mainWindow = std::make_unique<MainWindow>();
    m_mainWindow->setCloseHidesToTray(m_trayAvailable);
    if (!settings.mainWindow.geometry.isEmpty())
        m_mainWindow->restoreGeometry(settings.mainWindow.geometry);
    if (!settings.mainWindow.state.isEmpty())
        m_mainWindow->restoreState(settings.mainWindow.state);
    initMainWindowLanguages();
    connect(m_mainWindow.get(), &MainWindow::translateRequested, this,
            &AppController::translateInMainWindow);

    if (m_trayAvailable) {
        m_tray = new TrayIcon(icon, this);
        connect(m_tray, &TrayIcon::showWindowRequested, this, &AppController::showMainWindow);
        connect(m_tray, &TrayIcon::toggleWindowRequested, this, &AppController::toggleMainWindow);
        connect(m_tray, &TrayIcon::settingsRequested, this, &AppController::showSettings);
        connect(m_tray, &TrayIcon::quitRequested, this, &AppController::quit);
        m_tray->show();
    } else {
        qCWarning(lcApp) << "System tray is not available; closing the window quits";
        // Without a tray, the last closed window has to end the application.
        QApplication::setQuitOnLastWindowClosed(true);
    }

    createHotkeys();

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
    m_translations = new TranslationManager(this);
    m_translations->setMaxLength(settings.translation.maxLength);
    m_translations->registerService(new GoogleTranslateService(m_network));

    if (!settings.translation.service.isEmpty()) {
        if (m_translations->service(settings.translation.service))
            m_translations->setActiveService(settings.translation.service);
        else
            qCWarning(lcApp) << "Configured service is not available:"
                             << settings.translation.service;
    }
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

    QString target = settings.translation.primaryLang;
    if (!m_targetLanguages.contains(target)) {
        const QString systemLanguage = Languages::systemLanguage();
        if (m_targetLanguages.contains(systemLanguage))
            target = systemLanguage;
        else if (m_targetLanguages.contains(QStringLiteral("en")))
            target = QStringLiteral("en");
        else
            target = m_targetLanguages.value(0);
    }

    m_sourceLanguage = source;
    m_targetLanguage = target;

    QList<MainWindow::ServiceEntry> services;
    for (const TranslationService *s : m_translations->services())
        services.append({s->id(), s->displayName()});
    m_mainWindow->setServices(services, service->id());
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

    if (oldSettings.network.timeoutMs != newSettings.network.timeoutMs)
        m_network->setTimeoutMs(newSettings.network.timeoutMs);

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
