#include "app/appcontroller.h"

#include "app/logging.h"
#include "app/singleinstance.h"
#include "core/languages.h"
#include "core/translationmanager.h"
#include "core/translationservice.h"
#include "net/networkmanager.h"
#include "platform/hotkeymanager.h"
#include "platform/selectiongrabber.h"
#include "services/googletranslateservice.h"
#include "ui/appicon.h"
#include "ui/mainwindow.h"
#include "ui/popupwindow.h"
#include "ui/trayicon.h"

#include <QApplication>
#include <QCursor>
#include <QElapsedTimer>
#include <QSessionManager>

AppController::AppController(SingleInstance *instance, const Options &options, QObject *parent)
    : QObject(parent)
    , m_instance(instance)
    , m_options(options)
{
}

AppController::~AppController() = default;

void AppController::start()
{
    const QIcon icon = AppIcon::create();
    QApplication::setWindowIcon(icon);

    m_trayAvailable = TrayIcon::isAvailable();

    createTranslation();

    m_mainWindow = std::make_unique<MainWindow>();
    m_mainWindow->setCloseHidesToTray(m_trayAvailable);
    initMainWindowLanguages();
    connect(m_mainWindow.get(), &MainWindow::translateRequested, this,
            &AppController::translateInMainWindow);

    if (m_trayAvailable) {
        m_tray = new TrayIcon(icon, this);
        connect(m_tray, &TrayIcon::showWindowRequested, this, &AppController::showMainWindow);
        connect(m_tray, &TrayIcon::toggleWindowRequested, this, &AppController::toggleMainWindow);
        connect(m_tray, &TrayIcon::quitRequested, this, &AppController::quit);
        m_tray->show();
    } else {
        qCWarning(lcApp) << "System tray is not available; closing the window quits";
        // Without a tray, the last closed window has to end the application.
        QApplication::setQuitOnLastWindowClosed(true);
    }

    createHotkeys();

    connect(m_instance, &SingleInstance::activateRequested, this, &AppController::showMainWindow);

    // Let the window close for real when the app quits or the session ends.
    connect(qApp, &QCoreApplication::aboutToQuit, this, &AppController::prepareForQuit);
    connect(qApp, &QGuiApplication::commitDataRequest, this,
            [this](QSessionManager &) { prepareForQuit(); });

    if (!m_options.startHidden || !m_trayAvailable)
        showMainWindow();
}

void AppController::createTranslation()
{
    m_network = new NetworkManager(this);
    m_translations = new TranslationManager(this);
    m_translations->registerService(new GoogleTranslateService(m_network));
}

void AppController::createHotkeys()
{
    m_hotkeys = new HotkeyManager(this);
    m_grabber = new SelectionGrabber(this);
    m_grabber->setMaxLength(m_translations->maxLength());

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

    // Ctrl+Q and Ctrl+W are the defaults until settings arrive (stage 5).
    m_hotkeys->registerHotkey(HotkeyAction::TranslateToPopup,
                              QKeySequence(Qt::CTRL | Qt::Key_Q));
    m_hotkeys->registerHotkey(HotkeyAction::TranslateToMainWindow,
                              QKeySequence(Qt::CTRL | Qt::Key_W));
}

void AppController::initMainWindowLanguages()
{
    const TranslationService *service = m_translations->activeService();

    QList<MainWindow::ServiceEntry> services;
    for (const TranslationService *s : m_translations->services())
        services.append({s->id(), s->displayName()});
    m_mainWindow->setServices(services, service->id());

    // Defaults until settings arrive (stage 5): auto-detect -> system language.
    const QStringList targets = service->targetLanguages();
    const QString systemLanguage = Languages::systemLanguage();
    const QString target = targets.contains(systemLanguage) ? systemLanguage
                                                            : QStringLiteral("en");

    m_targetLanguages = targets;
    m_sourceLanguage = Languages::autoCode();
    m_targetLanguage = target;

    m_mainWindow->setLanguages(service->sourceLanguages(), targets);
    m_mainWindow->setSourceLanguage(m_sourceLanguage);
    m_mainWindow->setTargetLanguage(m_targetLanguage);
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
