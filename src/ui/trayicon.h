#pragma once

#include <QObject>
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
    explicit TrayIcon(const QIcon &icon, QObject *parent = nullptr);
    ~TrayIcon() override;

    static bool isAvailable();

    void show();

signals:
    void showWindowRequested();
    void toggleWindowRequested();
    void settingsRequested();
    void quitRequested();

private:
    void onActivated(QSystemTrayIcon::ActivationReason reason);

    std::unique_ptr<QMenu> m_menu; // QMenu needs a QWidget parent, so own it explicitly
    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_settingsAction = nullptr;
};
