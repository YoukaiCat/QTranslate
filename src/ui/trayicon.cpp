#include "ui/trayicon.h"

#include "core/appinfo.h"

#include <QAction>
#include <QIcon>
#include <QMenu>

TrayIcon::TrayIcon(const QIcon &icon, QObject *parent)
    : QObject(parent)
    , m_menu(std::make_unique<QMenu>())
    , m_tray(new QSystemTrayIcon(icon, this))
{
    QAction *showAction = m_menu->addAction(tr("Show window"));
    connect(showAction, &QAction::triggered, this, &TrayIcon::showWindowRequested);
    m_menu->setDefaultAction(showAction);

    m_settingsAction = m_menu->addAction(tr("Settings…"));
    connect(m_settingsAction, &QAction::triggered, this, &TrayIcon::settingsRequested);

    m_menu->addSeparator();

    QAction *quitAction = m_menu->addAction(tr("Quit"));
    connect(quitAction, &QAction::triggered, this, &TrayIcon::quitRequested);

    m_tray->setContextMenu(m_menu.get());
    m_tray->setToolTip(AppInfo::name());
    connect(m_tray, &QSystemTrayIcon::activated, this, &TrayIcon::onActivated);
}

TrayIcon::~TrayIcon()
{
    // m_tray is a child QObject and is deleted after m_menu; detach the menu first.
    m_tray->setContextMenu(nullptr);
}

bool TrayIcon::isAvailable()
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void TrayIcon::show()
{
    m_tray->show();
}

void TrayIcon::showMessage(const QString &title, const QString &message)
{
    m_tray->showMessage(title, message, QSystemTrayIcon::Information, 5000);
}

void TrayIcon::setToolTip(const QString &text)
{
    m_tray->setToolTip(text);
}

void TrayIcon::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger)
        emit toggleWindowRequested();
}
