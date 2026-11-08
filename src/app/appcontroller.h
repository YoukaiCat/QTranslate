#pragma once

#include "app/singleinstance.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <memory>

class HotkeyManager;
class MainWindow;
class NetworkManager;
class PopupWindow;
class QMimeData;
class QTimer;
class SelectionGrabber;
class SettingsStore;
class TranslationManager;
class TranslationReply;
class TrayIcon;
class DeepLService;
class GoogleTranslateService;
class MyMemoryService;
class OpenAiCompatibleService;
class QTranslator;
struct AppSettings;
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
        SingleInstance::Command command = SingleInstance::Command::None; // startup scenario
    };

    AppController(SingleInstance *instance, SettingsStore *settings, const Options &options,
                  QObject *parent = nullptr);
    ~AppController() override;

    // Creates the UI and shows the tray icon (and the main window unless hidden).
    void start();

private:
    void createTranslation();
    void createHotkeys();
    void executeCommand(SingleInstance::Command command);
    void initMainWindowLanguages();
    void updateLanguageConfiguration();
    void updateServiceSelectors();
    void updateHotkeySuspension();
    void updateFullscreenState();
    void onActiveServiceChanged(const QString &id);
    void selectService(const QString &id);
    void updateTrayToolTip();
    void applyUiLanguage(const QString &code);
    void applySettings(const AppSettings &oldSettings, const AppSettings &newSettings);
    void showSettings();
    PopupWindow *ensurePopup();
    void translateInMainWindow(const TranslationRequest &request);
    void translateSelectionInMainWindow();
    void translateSelectionInPopup();
    void translateSelectionInPlace();
    void translateInPopup(const TranslationRequest &request);
    void pasteTranslation(const QString &text);
    void restoreClipboardAfterPaste();
    void showInPlaceMessage(const QString &message);
    void finishInPlaceCommand();
    void showTrayUnavailableWarning();
    std::unique_ptr<QMimeData> snapshotClipboard() const;
    void openPopupInMainWindow(const TranslationRequest &request,
                               const TranslationResult &result);
    void showMainWindow();
    void toggleMainWindow();
    void quit();
    void prepareForQuit();

    SingleInstance *m_instance = nullptr;
    SettingsStore *m_settings = nullptr;
    Options m_options;
    bool m_trayAvailable = false;
    bool m_waylandSession = false;
    bool m_quitting = false;
    // A startup --translate-inplace command without a tray shows the main
    // window after the operation, so the result stays visible.
    bool m_showMainWindowAfterInPlace = false;

    NetworkManager *m_network = nullptr;
    TranslationManager *m_translations = nullptr;
    GoogleTranslateService *m_google = nullptr;
    DeepLService *m_deepl = nullptr;
    OpenAiCompatibleService *m_llm = nullptr;
    MyMemoryService *m_mymemory = nullptr;
    QPointer<TranslationReply> m_mainWindowReply; // the request the main window waits for
    QPointer<TranslationReply> m_popupReply;      // the request the popup waits for
    QPointer<TranslationReply> m_inPlaceReply;    // the request for replacing a selection
    quintptr m_inPlaceTarget = 0;                 // window that had the selection
    std::unique_ptr<QMimeData> m_pasteSnapshot;   // clipboard to restore after pasting

    QStringList m_targetLanguages;
    QString m_sourceLanguage;
    QString m_targetLanguage;

    std::unique_ptr<MainWindow> m_mainWindow;
    std::unique_ptr<PopupWindow> m_popup;
    TrayIcon *m_tray = nullptr;
    HotkeyManager *m_hotkeys = nullptr;
    QTimer *m_fullscreenTimer = nullptr;
    bool m_hotkeyEditing = false;
    bool m_hotkeysPaused = false;
    bool m_fullscreenAppActive = false;
    bool m_pauseInFullscreen = true;
    SelectionGrabber *m_grabber = nullptr;
    QTranslator *m_translator = nullptr;
    QTranslator *m_qtTranslator = nullptr;
};
