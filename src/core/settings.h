#pragma once

#include <QByteArray>
#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

// All user-configurable values (see doc/architecture.md, §4.11).
struct GeneralSettings
{
    bool autostart = false;
    bool startMinimized = true;
    QString uiLanguage = QStringLiteral("system"); // used by the localization stage
    bool restoreClipboard = true;
};

struct HotkeySettings
{
    QKeySequence popup;
    QKeySequence replaceSelection;
    QKeySequence mainWindow;
    QKeySequence showMainWindow;
    bool pauseInFullscreen = true;
};

struct TranslationSettings
{
    QString service = QStringLiteral("google");
    QString sourceLang = QStringLiteral("auto");
    QString primaryLang;
    QString secondaryLang;
    int maxLength = 20000;
    int autoTranslateDelayMs = 700; // 0 = off; used by the auto-translate stage
    // Gates services whose requiresOptIn() is true: the manager refuses them
    // while this is false.
    bool allowFreeServices = false;
    // Empty means no fallback. One of fallbackServiceIds() otherwise.
    QString fallbackService;
};

struct GoogleSettings
{
    QString mode = QStringLiteral("free"); // free | cloud
    QString apiKey;
};

struct DeepLSettings
{
    QString mode = QStringLiteral("api"); // api | free-web
    QString apiKey;
    QString englishVariant = QStringLiteral("EN-US");
};

struct MozhiSettings
{
    QString instance = QStringLiteral("https://mozhi.adminforge.de");
    QString engine = QStringLiteral("google");
};

struct LlmSettings
{
    QString baseUrl = QStringLiteral("https://openrouter.ai/api/v1");
    QString apiKey;
    QString model = QStringLiteral("openrouter/free");
};

struct ProofreadingSettings
{
    bool enabled = false;
    QString provider = QStringLiteral("languagetool"); // the only id of this stage
    QString language = QStringLiteral("auto");
    QString endpoint; // empty: the provider default
    QString apiKey;
    // proofreadingCategoryIds(); initialised in AppSettings' constructor.
    QStringList categories;
    bool checkBeforeTranslation = false;
    bool autoReplace = false;
    bool allowOnlineServices = false;
};

struct NetworkSettings
{
    QString proxyMode = QStringLiteral("system"); // none | system | manual
    QString proxyType = QStringLiteral("http");    // http | socks5
    QString proxyHost;
    int proxyPort = 0;
    bool proxyAuth = false;
    QString proxyUser;
    QString proxyPassword;
    int timeoutMs = 10000;
};

struct PopupSettings
{
    int width = 460;
    int height = 260; // informational: the popup height follows its contents
    bool autoCloseOnFocusLoss = true;
};

struct MainWindowSettings
{
    QByteArray geometry;
    QByteArray state;
};

struct AppSettings
{
    static constexpr int kCurrentVersion = 1;

    int settingsVersion = kCurrentVersion;
    GeneralSettings general;
    HotkeySettings hotkeys;
    TranslationSettings translation;
    GoogleSettings google;
    DeepLSettings deepl;
    MozhiSettings mozhi;
    LlmSettings llm;
    ProofreadingSettings proofreading;
    NetworkSettings network;
    PopupSettings popup;
    MainWindowSettings mainWindow;

    AppSettings();
};

Q_DECLARE_METATYPE(AppSettings)

// Pure helpers shared by SettingsStore and the Mozhi settings UI, so the
// instance and engine rules live in one place. They never log.
QStringList mozhiEngineIds();
// The settings-layer guard for translation/fallbackService. It must track the
// services registered with requiresOptIn(); the manager still ignores an
// unknown id at request time.
QStringList fallbackServiceIds();
// The settings-layer guard for proofreading/provider: only ids that a
// registered service can answer are listed. proofreadingCategoryIds() is the
// canonical category order.
QStringList proofreadingProviderIds();
QStringList proofreadingCategoryIds();
bool isValidMozhiInstance(const QString &instance);
QString normalizedMozhiInstance(const QString &instance);
QString normalizedMozhiEngine(const QString &engine);
// An empty endpoint is valid: the provider default applies. Otherwise an
// absolute http(s) URL with a host, without user info, a query or a fragment.
bool isValidProofreadingEndpoint(const QString &endpoint);
QString normalizedProofreadingEndpoint(const QString &endpoint);

// Injected by the application so that core/settings stays free of WinAPI.
// Without a codec secrets are not stored at all.
struct SecretCodec
{
    std::function<QByteArray(const QString &)> protect;
    std::function<QString(const QByteArray &)> unprotect;
    // Optional: whether an existing protected value can stay as-is when its
    // plaintext did not change. Empty keeps the old behavior (reuse unchanged
    // non-empty values).
    std::function<bool(const QByteArray &)> canReuse = {};
};

// Loads and saves AppSettings as an INI file (see doc/architecture.md, §4.11).
class SettingsStore : public QObject
{
    Q_OBJECT

public:
    explicit SettingsStore(QObject *parent = nullptr);
    SettingsStore(const QString &filePath, const SecretCodec &codec,
                  QObject *parent = nullptr);
    ~SettingsStore() override;

    // Portable mode: qtranslate.ini next to the executable. Otherwise
    // <appDataDir>/qtranslate.ini. Kept pure for tests.
    static QString defaultFilePath(const QString &applicationDir, const QString &appDataDir);

    const AppSettings &current() const { return m_current; }
    QString filePath() const { return m_filePath; }
    bool isPortable() const { return m_portable; }

    void setSecretCodec(const SecretCodec &codec) { m_codec = codec; }

    // Missing or unreadable file (and invalid values) fall back to defaults.
    void load();

    // Writes the file, becomes the current value and emits changed(old, new).
    void save(const AppSettings &settings);

signals:
    void changed(const AppSettings &oldSettings, const AppSettings &newSettings);

private:
    QString m_filePath;
    bool m_portable = false;
    SecretCodec m_codec;
    AppSettings m_current;
};
