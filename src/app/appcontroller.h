#pragma once

#include <QObject>
#include <QPointer>

#include <memory>

class MainWindow;
class NetworkManager;
class SingleInstance;
class TranslationManager;
class TranslationReply;
class TrayIcon;
struct TranslationRequest;

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
    void createTranslation();
    void initMainWindowLanguages();
    void translateInMainWindow(const TranslationRequest &request);
    void showMainWindow();
    void toggleMainWindow();
    void quit();
    void prepareForQuit();

    SingleInstance *m_instance = nullptr;
    Options m_options;
    bool m_trayAvailable = false;

    NetworkManager *m_network = nullptr;
    TranslationManager *m_translations = nullptr;
    QPointer<TranslationReply> m_mainWindowReply; // the request the main window waits for

    std::unique_ptr<MainWindow> m_mainWindow;
    TrayIcon *m_tray = nullptr;
};
