#include "app/appcontroller.h"

#include "app/logging.h"
#include "app/singleinstance.h"
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

    m_mainWindow = std::make_unique<MainWindow>();
    m_mainWindow->setCloseHidesToTray(m_trayAvailable);

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
    if (m_mainWindow)
        m_mainWindow->setCloseHidesToTray(false);
}
