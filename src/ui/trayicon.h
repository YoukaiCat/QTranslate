#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QSystemTrayIcon>

#include <memory>

class QAction;
class QIcon;
class QMenu;

// System tray icon with the application menu. Emits requests; the decisions
// (what to show, when to quit) are made by AppController.
class TrayIcon : public QObject
{
    Q_OBJECT

public:
    struct ServiceEntry
    {
        QString id;
        QString name;
    };

    explicit TrayIcon(const QIcon &icon, QObject *parent = nullptr);
    ~TrayIcon() override;

    static bool isAvailable();

    void show();
    void showMessage(const QString &title, const QString &message);
    void setToolTip(const QString &text);
    void setServices(const QList<ServiceEntry> &services, const QString &activeId);

signals:
    void showWindowRequested();
    void toggleWindowRequested();
    void serviceSelected(const QString &id);
    void settingsRequested();
    void quitRequested();

private:
    void onActivated(QSystemTrayIcon::ActivationReason reason);

    std::unique_ptr<QMenu> m_menu; // QMenu needs a QWidget parent, so own it explicitly
    QMenu *m_serviceMenu = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_settingsAction = nullptr;
};
