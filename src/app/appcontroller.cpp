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
#include "ui/trayicon.h"

#include <QApplication>
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
        if (action == HotkeyAction::TranslateToMainWindow)
            translateSelectionInMainWindow();
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

    // Ctrl+Q is registered in stage 4 together with the popup scenario.
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
    m_mainWindow->setLanguages(service->sourceLanguages(), targets);
    m_mainWindow->setSourceLanguage(Languages::autoCode());
    m_mainWindow->setTargetLanguage(targets.contains(systemLanguage) ? systemLanguage
                                                                     : QStringLiteral("en"));
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
    if (m_mainWindow)
        m_mainWindow->setCloseHidesToTray(false);
}
