#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <memory>

class HotkeyManager;
class MainWindow;
class NetworkManager;
class PopupWindow;
class SelectionGrabber;
class SingleInstance;
class TranslationManager;
class TranslationReply;
class TrayIcon;
struct TranslationRequest;
struct TranslationResult;

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
    void createHotkeys();
    void initMainWindowLanguages();
    PopupWindow *ensurePopup();
    void translateInMainWindow(const TranslationRequest &request);
    void translateSelectionInMainWindow();
    void translateSelectionInPopup();
    void translateInPopup(const TranslationRequest &request);
    void openPopupInMainWindow(const TranslationRequest &request,
                               const TranslationResult &result);
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
    QPointer<TranslationReply> m_popupReply;      // the request the popup waits for

    QStringList m_targetLanguages;
    QString m_sourceLanguage;
    QString m_targetLanguage;

    std::unique_ptr<MainWindow> m_mainWindow;
    std::unique_ptr<PopupWindow> m_popup;
    TrayIcon *m_tray = nullptr;
    HotkeyManager *m_hotkeys = nullptr;
    SelectionGrabber *m_grabber = nullptr;
};
