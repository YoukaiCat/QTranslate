#pragma once

#include <QObject>

#include <memory>

class MainWindow;
class SingleInstance;
class TrayIcon;

// Wires the application modules together and implements the user scenarios.
// The only class that knows about all modules (see doc/architecture.md, §4.2).
class AppController : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        bool startHidden = false; // --minimized
    };

    AppController(SingleInstance *instance, const Options &options, QObject *parent = nullptr);
    ~AppController() override;

    // Creates the UI and shows the tray icon (and the main window unless hidden).
    void start();

private:
    void showMainWindow();
    void toggleMainWindow();
    void quit();
    void prepareForQuit();

    SingleInstance *m_instance = nullptr;
    Options m_options;
    bool m_trayAvailable = false;

    std::unique_ptr<MainWindow> m_mainWindow;
    TrayIcon *m_tray = nullptr;
};
