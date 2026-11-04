#include "app/appcontroller.h"

#include "app/logging.h"
#include "app/singleinstance.h"
#include "core/languages.h"
#include "core/translationmanager.h"
#include "core/translationservice.h"
#include "net/networkmanager.h"
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
    if (m_translations)
        m_translations->cancelAll();
    if (m_mainWindow)
        m_mainWindow->setCloseHidesToTray(false);
}
