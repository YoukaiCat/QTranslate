#pragma once

#include "core/settings.h"

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QGroupBox;
class QLabel;
class QKeySequenceEdit;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class LanguageComboBox;

// Edits a copy of AppSettings; AppController applies and saves it on OK
// (see doc/architecture.md, §4.13). Knows nothing about services or WinAPI.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(const AppSettings &settings, const QStringList &sourceLanguages,
                   const QStringList &targetLanguages, bool autostartApproved,
                   bool secretStorageFallback = false, QWidget *parent = nullptr);
    ~SettingsDialog() override;

    // Exposed for tests: the current widget values as AppSettings.
    AppSettings settings() const;

    // Empty error text means the values can be accepted. A multi-chord or
    // unsupported key sequence, duplicate hotkeys or equal languages fail.
    bool validate(QString *error) const;

    void accept() override;

public slots:
    // Result of an asynchronous DeepL key check started by AppController.
    void showDeepLKeyCheckResult(bool ok, const QString &message);
    // Result of an asynchronous provider key check/model load.
    void showLlmModels(bool ok, const QString &message, const QStringList &models);
    // Result of an asynchronous proxy test started by AppController.
    void showProxyTestResult(bool ok, const QString &message);

signals:
    // While a QKeySequenceEdit has focus, global hotkeys must be suspended.
    void hotkeyEditingChanged(bool editing);
    // The user asked to verify the key currently typed in the DeepL field.
    void deepLKeyCheckRequested(const QString &key);
    // The user asked to check the provider key and load its model list.
    void llmModelsRequested(const QString &baseUrl, const QString &key);
    // The user asked to test the proxy fields without saving them.
    void proxyTestRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi(const QStringList &sourceLanguages);
    void loadValues(const AppSettings &settings, bool autostartApproved,
                    bool secretStorageFallback);
    void updateHotkeyEditing();
    void setHotkeyEditing(bool editing);
    bool anyHotkeyEditFocused() const;
    void updateGoogleControls();
    void updateDeepLControls();
    void updateMyMemoryControls();
    void updateMozhiControls();
    void updateYandexControls();
    void updateProxyControls();

    AppSettings m_initial;
    QStringList m_targetLanguages;

    QTabWidget *m_tabs = nullptr;

    QCheckBox *m_autostart = nullptr;
    QLabel *m_autostartHint = nullptr;
    QLabel *m_secretStorageHint = nullptr;
    QCheckBox *m_startMinimized = nullptr;
    QCheckBox *m_restoreClipboard = nullptr;
    QCheckBox *m_autoClosePopup = nullptr;
    QComboBox *m_uiLanguage = nullptr;

    QKeySequenceEdit *m_popupHotkey = nullptr;
    QKeySequenceEdit *m_replaceSelectionHotkey = nullptr;
    QKeySequenceEdit *m_mainWindowHotkey = nullptr;
    QKeySequenceEdit *m_showMainWindowHotkey = nullptr;
    QCheckBox *m_pauseInFullscreen = nullptr;

    LanguageComboBox *m_sourceLanguage = nullptr;
    LanguageComboBox *m_primaryLanguage = nullptr;
    LanguageComboBox *m_secondaryLanguage = nullptr;
    QSpinBox *m_maxLength = nullptr;
    QSpinBox *m_autoTranslateDelay = nullptr;
    QCheckBox *m_allowFreeServices = nullptr;

    QComboBox *m_googleMode = nullptr;
    QLineEdit *m_googleApiKey = nullptr;
    QComboBox *m_deeplMode = nullptr;
    QLineEdit *m_deeplApiKey = nullptr;
    QComboBox *m_deeplEnglishVariant = nullptr;
    QPushButton *m_checkDeepLKey = nullptr;
    QLabel *m_deepLKeyStatus = nullptr;
    QLabel *m_deepLWebHint = nullptr;

    QLineEdit *m_llmBaseUrl = nullptr;
    QLineEdit *m_llmApiKey = nullptr;
    QComboBox *m_llmModel = nullptr;
    QPushButton *m_loadLlmModels = nullptr;
    QLabel *m_llmStatus = nullptr;

    QGroupBox *m_mymemoryGroup = nullptr;

    QGroupBox *m_mozhiGroup = nullptr;
    QLineEdit *m_mozhiInstance = nullptr;
    QComboBox *m_mozhiEngine = nullptr;
    QLabel *m_mozhiEffectiveInstance = nullptr;
    QLabel *m_mozhiInstanceWarning = nullptr;
    QLabel *m_mozhiHint = nullptr;

    QGroupBox *m_yandexGroup = nullptr;

    QComboBox *m_proxyMode = nullptr;
    QComboBox *m_proxyType = nullptr;
    QLineEdit *m_proxyHost = nullptr;
    QSpinBox *m_proxyPort = nullptr;
    QCheckBox *m_proxyAuth = nullptr;
    QLineEdit *m_proxyUser = nullptr;
    QLineEdit *m_proxyPassword = nullptr;
    QSpinBox *m_networkTimeout = nullptr;
    QPushButton *m_testProxy = nullptr;
    QLabel *m_proxyTestStatus = nullptr;

    bool m_hotkeyEditing = false;
};
