#include "ui/trayicon.h"

#include "core/appinfo.h"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QSignalBlocker>

TrayIcon::TrayIcon(const QIcon &icon, QObject *parent)
    : QObject(parent)
    , m_menu(std::make_unique<QMenu>())
    , m_tray(new QSystemTrayIcon(icon, this))
{
    QAction *showAction = m_menu->addAction(tr("Show window"));
    m_showAction = showAction;
    connect(showAction, &QAction::triggered, this, &TrayIcon::showWindowRequested);
    m_menu->setDefaultAction(showAction);

    m_serviceMenu = m_menu->addMenu(tr("Service"));
    m_serviceMenu->setObjectName(QStringLiteral("serviceMenu"));

    m_menu->addSeparator();

    m_pauseHotkeysAction = m_menu->addAction(tr("Pause hotkeys"));
    m_pauseHotkeysAction->setObjectName(QStringLiteral("pauseHotkeys"));
    m_pauseHotkeysAction->setCheckable(true);
    connect(m_pauseHotkeysAction, &QAction::toggled, this,
            &TrayIcon::hotkeysPauseToggled);

    m_settingsAction = m_menu->addAction(tr("Settings…"));
    connect(m_settingsAction, &QAction::triggered, this, &TrayIcon::settingsRequested);

    QAction *quitAction = m_menu->addAction(tr("Quit"));
    m_quitAction = quitAction;
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

void TrayIcon::setHotkeysPaused(bool paused)
{
    if (!m_pauseHotkeysAction)
        return;
    const QSignalBlocker blocker(m_pauseHotkeysAction);
    m_pauseHotkeysAction->setChecked(paused);
}

void TrayIcon::setServices(const QList<ServiceEntry> &services, const QString &activeId)
{
    if (!m_serviceMenu)
        return;

    const QList<QAction *> oldActions = m_serviceMenu->actions();
    for (QAction *action : oldActions) {
        m_serviceMenu->removeAction(action);
        action->deleteLater();
    }

    for (const ServiceEntry &service : services) {
        QAction *action = m_serviceMenu->addAction(service.name);
        action->setCheckable(true);
        action->setChecked(service.id == activeId);
        action->setData(service.id);
        connect(action, &QAction::triggered, this,
                [this, id = service.id] { emit serviceSelected(id); });
    }
    m_serviceMenu->setEnabled(!services.isEmpty());
}

void TrayIcon::retranslate()
{
    if (m_showAction)
        m_showAction->setText(tr("Show window"));
    if (m_pauseHotkeysAction)
        m_pauseHotkeysAction->setText(tr("Pause hotkeys"));
    if (m_serviceMenu)
        m_serviceMenu->setTitle(tr("Service"));
    if (m_settingsAction)
        m_settingsAction->setText(tr("Settings…"));
    if (m_quitAction)
        m_quitAction->setText(tr("Quit"));
}

void TrayIcon::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger)
        emit toggleWindowRequested();
}
