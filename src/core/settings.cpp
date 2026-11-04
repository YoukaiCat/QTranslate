#include "core/settings.h"

#include "core/languages.h"
#include "core/uilanguages.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSettings>
#include <QStandardPaths>

#include <utility>

Q_LOGGING_CATEGORY(lcSettings, "qtranslate.settings", QtWarningMsg)

namespace {

constexpr int kDefaultMaxLength = 20000;
constexpr int kMinMaxLength = 100;
constexpr int kMaxMaxLength = 100000;
constexpr int kDefaultDelayMs = 700;
constexpr int kMaxDelayMs = 60000;
constexpr int kDefaultTimeoutMs = 10000;
constexpr int kMinTimeoutMs = 1000;
constexpr int kMaxTimeoutMs = 120000;
constexpr int kDefaultPopupWidth = 460;
constexpr int kDefaultPopupHeight = 260;
constexpr int kMinPopupWidth = 320;
constexpr int kMaxPopupWidth = 800;
constexpr int kMinPopupHeight = 160;
constexpr int kMaxPopupHeight = 1200;

int boundedInt(const QVariant &value, int fallback, int minimum, int maximum)
{
    bool ok = false;
    const int number = value.toInt(&ok);
    if (!ok || number < minimum || number > maximum)
        return fallback;
    return number;
}

QString choice(const QString &value, const QString &fallback,
               std::initializer_list<const char *> allowed)
{
    for (const char *item : allowed) {
        if (value == QLatin1String(item))
            return value;
    }
    return fallback;
}

QKeySequence readKeySequence(const QSettings &settings, const QString &key,
                             const QKeySequence &fallback)
{
    const QString text = settings.value(key, fallback.toString(QKeySequence::PortableText))
                             .toString();
    if (text.isEmpty())
        return {}; // explicitly unassigned
    const QKeySequence sequence =
        QKeySequence::fromString(text, QKeySequence::PortableText);
    if (sequence.isEmpty() || sequence.toString(QKeySequence::PortableText).isEmpty())
        return fallback;
    return sequence;
}

QString readSecret(const QSettings &settings, const QString &key, const SecretCodec &codec)
{
    const QString stored = settings.value(key).toString();
    if (stored.isEmpty() || !codec.unprotect)
        return {};
    return codec.unprotect(QByteArray::fromBase64(stored.toLatin1()));
}

void writeSecret(QSettings &settings, const QString &key, const QString &value,
                 const SecretCodec &codec)
{
    if (value.isEmpty()) {
        settings.remove(key);
        return;
    }
    if (!codec.protect) {
        qCWarning(lcSettings) << "No secret codec; not storing" << key;
        settings.remove(key);
        return;
    }

    const QByteArray protectedData = codec.protect(value);
    if (protectedData.isEmpty()) {
        qCWarning(lcSettings) << "Could not protect secret" << key;
        settings.remove(key);
        return;
    }
    settings.setValue(key, QString::fromLatin1(protectedData.toBase64()));
}

bool sameDirectory(const QString &filePath, const QString &directory)
{
    const QString fileDir = QDir::cleanPath(QFileInfo(filePath).absolutePath());
    const QString dir = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    return fileDir.compare(dir, Qt::CaseInsensitive) == 0;
}

void normalize(AppSettings &settings)
{
    const AppSettings defaults;

    settings.settingsVersion = AppSettings::kCurrentVersion;

    if (settings.general.uiLanguage.isEmpty()) {
        settings.general.uiLanguage = defaults.general.uiLanguage;
    } else if (settings.general.uiLanguage != QStringLiteral("system")) {
        const QString canonical = UiLanguages::canonical(settings.general.uiLanguage);
        settings.general.uiLanguage =
            canonical.isEmpty() ? defaults.general.uiLanguage : canonical;
    }

    settings.translation.service =
        settings.translation.service.isEmpty() ? defaults.translation.service
                                               : settings.translation.service;
    settings.translation.sourceLang =
        settings.translation.sourceLang.isEmpty() ? defaults.translation.sourceLang
                                                  : settings.translation.sourceLang;
    if (settings.translation.primaryLang.isEmpty())
        settings.translation.primaryLang = defaults.translation.primaryLang;
    if (settings.translation.secondaryLang.isEmpty()
        || settings.translation.secondaryLang == settings.translation.primaryLang) {
        settings.translation.secondaryLang =
            settings.translation.primaryLang == QStringLiteral("en")
            ? QStringLiteral("ru")
            : defaults.translation.secondaryLang;
    }
    settings.translation.maxLength =
        boundedInt(settings.translation.maxLength, kDefaultMaxLength, kMinMaxLength,
                   kMaxMaxLength);
    settings.translation.autoTranslateDelayMs =
        boundedInt(settings.translation.autoTranslateDelayMs, kDefaultDelayMs, 0, kMaxDelayMs);

    settings.google.mode =
        choice(settings.google.mode, defaults.google.mode, {"free", "cloud"});
    settings.deepl.englishVariant =
        choice(settings.deepl.englishVariant, defaults.deepl.englishVariant,
               {"EN-US", "EN-GB"});

    settings.network.proxyMode =
        choice(settings.network.proxyMode, defaults.network.proxyMode,
               {"none", "system", "manual"});
    settings.network.proxyType =
        choice(settings.network.proxyType, defaults.network.proxyType, {"http", "socks5"});
    settings.network.proxyPort =
        boundedInt(settings.network.proxyPort, 0, 0, 65535);
    settings.network.timeoutMs =
        boundedInt(settings.network.timeoutMs, kDefaultTimeoutMs, kMinTimeoutMs,
                   kMaxTimeoutMs);

    settings.popup.width =
        boundedInt(settings.popup.width, kDefaultPopupWidth, kMinPopupWidth, kMaxPopupWidth);
    settings.popup.height =
        boundedInt(settings.popup.height, kDefaultPopupHeight, kMinPopupHeight,
                   kMaxPopupHeight);
}

} // namespace

AppSettings::AppSettings()
{
    hotkeys.popup = QKeySequence(Qt::CTRL | Qt::Key_Q);
    hotkeys.replaceSelection = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q);
    hotkeys.mainWindow = QKeySequence(Qt::CTRL | Qt::Key_W);

    translation.primaryLang = Languages::systemLanguage();
    translation.secondaryLang = translation.primaryLang == QStringLiteral("en")
        ? QStringLiteral("ru")
        : QStringLiteral("en");
}

SettingsStore::SettingsStore(QObject *parent)
    : SettingsStore(QString(), SecretCodec{}, parent)
{
}

SettingsStore::SettingsStore(const QString &filePath, const SecretCodec &codec, QObject *parent)
    : QObject(parent)
    , m_filePath(filePath)
    , m_codec(codec)
{
    const QString applicationDir =
        QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QString();
    QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataDir.isEmpty())
        appDataDir = QDir::home().filePath(QStringLiteral(".qtranslate"));

    if (m_filePath.isEmpty())
        m_filePath = defaultFilePath(applicationDir, appDataDir);

    m_portable = !applicationDir.isEmpty()
        && QFileInfo(m_filePath).fileName() == QStringLiteral("qtranslate.ini")
        && sameDirectory(m_filePath, applicationDir);
}

SettingsStore::~SettingsStore() = default;

QString SettingsStore::defaultFilePath(const QString &applicationDir, const QString &appDataDir)
{
    if (!applicationDir.isEmpty()) {
        const QString portable =
            QDir(applicationDir).filePath(QStringLiteral("qtranslate.ini"));
        if (QFileInfo::exists(portable))
            return QDir::cleanPath(portable);
    }
    return QDir::cleanPath(QDir(appDataDir).filePath(QStringLiteral("qtranslate.ini")));
}

void SettingsStore::load()
{
    AppSettings loaded;

    if (m_filePath.isEmpty()) {
        m_current = loaded;
        return;
    }

    QSettings settings(m_filePath, QSettings::IniFormat);
    if (settings.status() != QSettings::NoError) {
        qCWarning(lcSettings) << "Cannot read settings" << m_filePath
                              << "status" << int(settings.status());
        m_current = loaded;
        return;
    }

    const int version = settings.value(QStringLiteral("settingsVersion"),
                                       AppSettings::kCurrentVersion)
                            .toInt();
    if (version > AppSettings::kCurrentVersion) {
        qCWarning(lcSettings) << "Settings version" << version
                              << "is newer than supported; using defaults";
        m_current = loaded;
        return;
    }

    settings.beginGroup(QStringLiteral("app"));
    loaded.general.autostart =
        settings.value(QStringLiteral("autostart"), loaded.general.autostart).toBool();
    loaded.general.startMinimized =
        settings.value(QStringLiteral("startMinimized"), loaded.general.startMinimized).toBool();
    loaded.general.uiLanguage =
        settings.value(QStringLiteral("uiLanguage"), loaded.general.uiLanguage).toString();
    loaded.general.restoreClipboard =
        settings.value(QStringLiteral("restoreClipboard"), loaded.general.restoreClipboard)
            .toBool();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("hotkeys"));
    loaded.hotkeys.popup = readKeySequence(settings, QStringLiteral("popup"),
                                           loaded.hotkeys.popup);
    loaded.hotkeys.replaceSelection =
        readKeySequence(settings, QStringLiteral("replaceSelection"),
                        loaded.hotkeys.replaceSelection);
    loaded.hotkeys.mainWindow = readKeySequence(settings, QStringLiteral("mainWindow"),
                                                loaded.hotkeys.mainWindow);
    loaded.hotkeys.showMainWindow = readKeySequence(settings, QStringLiteral("showMainWindow"),
                                                    loaded.hotkeys.showMainWindow);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("translation"));
    loaded.translation.service =
        settings.value(QStringLiteral("service"), loaded.translation.service).toString();
    loaded.translation.sourceLang =
        settings.value(QStringLiteral("sourceLang"), loaded.translation.sourceLang).toString();
    loaded.translation.primaryLang =
        settings.value(QStringLiteral("primaryLang"), loaded.translation.primaryLang).toString();
    loaded.translation.secondaryLang =
        settings.value(QStringLiteral("secondaryLang"), loaded.translation.secondaryLang)
            .toString();
    loaded.translation.maxLength =
        settings.value(QStringLiteral("maxLength"), loaded.translation.maxLength).toInt();
    loaded.translation.autoTranslateDelayMs =
        settings.value(QStringLiteral("autoTranslateDelayMs"),
                       loaded.translation.autoTranslateDelayMs)
            .toInt();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("google"));
    loaded.google.mode =
        settings.value(QStringLiteral("mode"), loaded.google.mode).toString();
    loaded.google.apiKey = readSecret(settings, QStringLiteral("apiKey"), m_codec);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("deepl"));
    loaded.deepl.apiKey = readSecret(settings, QStringLiteral("apiKey"), m_codec);
    loaded.deepl.englishVariant =
        settings.value(QStringLiteral("englishVariant"), loaded.deepl.englishVariant)
            .toString();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("network"));
    loaded.network.proxyMode =
        settings.value(QStringLiteral("proxyMode"), loaded.network.proxyMode).toString();
    loaded.network.proxyType =
        settings.value(QStringLiteral("proxyType"), loaded.network.proxyType).toString();
    loaded.network.proxyHost =
        settings.value(QStringLiteral("proxyHost"), loaded.network.proxyHost).toString();
    loaded.network.proxyPort =
        settings.value(QStringLiteral("proxyPort"), loaded.network.proxyPort).toInt();
    loaded.network.proxyAuth =
        settings.value(QStringLiteral("proxyAuth"), loaded.network.proxyAuth).toBool();
    loaded.network.proxyUser =
        settings.value(QStringLiteral("proxyUser"), loaded.network.proxyUser).toString();
    loaded.network.proxyPassword =
        readSecret(settings, QStringLiteral("proxyPassword"), m_codec);
    loaded.network.timeoutMs =
        settings.value(QStringLiteral("timeoutMs"), loaded.network.timeoutMs).toInt();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("popup"));
    loaded.popup.width =
        settings.value(QStringLiteral("width"), loaded.popup.width).toInt();
    loaded.popup.height =
        settings.value(QStringLiteral("height"), loaded.popup.height).toInt();
    loaded.popup.autoCloseOnFocusLoss =
        settings.value(QStringLiteral("autoCloseOnFocusLoss"),
                       loaded.popup.autoCloseOnFocusLoss)
            .toBool();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("mainWindow"));
    loaded.mainWindow.geometry =
        settings.value(QStringLiteral("geometry"), loaded.mainWindow.geometry).toByteArray();
    loaded.mainWindow.state =
        settings.value(QStringLiteral("state"), loaded.mainWindow.state).toByteArray();
    settings.endGroup();

    if (settings.status() != QSettings::NoError) {
        qCWarning(lcSettings) << "Error while reading settings" << m_filePath;
        m_current = AppSettings{};
        return;
    }

    normalize(loaded);
    m_current = loaded;
}

void SettingsStore::save(const AppSettings &settings)
{
    AppSettings normalized = settings;
    normalize(normalized);

    const AppSettings old = m_current;

    if (m_filePath.isEmpty()) {
        qCWarning(lcSettings) << "Cannot save settings: no file path";
        m_current = normalized;
        emit changed(old, m_current);
        return;
    }

    const QFileInfo fileInfo(m_filePath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        qCWarning(lcSettings) << "Cannot create settings directory" << fileInfo.absolutePath();
        m_current = normalized;
        emit changed(old, m_current);
        return;
    }

    QSettings out(m_filePath, QSettings::IniFormat);
    out.setValue(QStringLiteral("settingsVersion"), AppSettings::kCurrentVersion);

    out.beginGroup(QStringLiteral("app"));
    out.setValue(QStringLiteral("autostart"), normalized.general.autostart);
    out.setValue(QStringLiteral("startMinimized"), normalized.general.startMinimized);
    out.setValue(QStringLiteral("uiLanguage"), normalized.general.uiLanguage);
    out.setValue(QStringLiteral("restoreClipboard"), normalized.general.restoreClipboard);
    out.endGroup();

    out.beginGroup(QStringLiteral("hotkeys"));
    out.setValue(QStringLiteral("popup"),
                 normalized.hotkeys.popup.toString(QKeySequence::PortableText));
    out.setValue(QStringLiteral("replaceSelection"),
                 normalized.hotkeys.replaceSelection.toString(QKeySequence::PortableText));
    out.setValue(QStringLiteral("mainWindow"),
                 normalized.hotkeys.mainWindow.toString(QKeySequence::PortableText));
    out.setValue(QStringLiteral("showMainWindow"),
                 normalized.hotkeys.showMainWindow.toString(QKeySequence::PortableText));
    out.endGroup();

    out.beginGroup(QStringLiteral("translation"));
    out.setValue(QStringLiteral("service"), normalized.translation.service);
    out.setValue(QStringLiteral("sourceLang"), normalized.translation.sourceLang);
    out.setValue(QStringLiteral("primaryLang"), normalized.translation.primaryLang);
    out.setValue(QStringLiteral("secondaryLang"), normalized.translation.secondaryLang);
    out.setValue(QStringLiteral("maxLength"), normalized.translation.maxLength);
    out.setValue(QStringLiteral("autoTranslateDelayMs"),
                 normalized.translation.autoTranslateDelayMs);
    out.endGroup();

    out.beginGroup(QStringLiteral("google"));
    out.setValue(QStringLiteral("mode"), normalized.google.mode);
    writeSecret(out, QStringLiteral("apiKey"), normalized.google.apiKey, m_codec);
    out.endGroup();

    out.beginGroup(QStringLiteral("deepl"));
    writeSecret(out, QStringLiteral("apiKey"), normalized.deepl.apiKey, m_codec);
    out.setValue(QStringLiteral("englishVariant"), normalized.deepl.englishVariant);
    out.endGroup();

    out.beginGroup(QStringLiteral("network"));
    out.setValue(QStringLiteral("proxyMode"), normalized.network.proxyMode);
    out.setValue(QStringLiteral("proxyType"), normalized.network.proxyType);
    out.setValue(QStringLiteral("proxyHost"), normalized.network.proxyHost);
    out.setValue(QStringLiteral("proxyPort"), normalized.network.proxyPort);
    out.setValue(QStringLiteral("proxyAuth"), normalized.network.proxyAuth);
    out.setValue(QStringLiteral("proxyUser"), normalized.network.proxyUser);
    writeSecret(out, QStringLiteral("proxyPassword"), normalized.network.proxyPassword,
                m_codec);
    out.setValue(QStringLiteral("timeoutMs"), normalized.network.timeoutMs);
    out.endGroup();

    out.beginGroup(QStringLiteral("popup"));
    out.setValue(QStringLiteral("width"), normalized.popup.width);
    out.setValue(QStringLiteral("height"), normalized.popup.height);
    out.setValue(QStringLiteral("autoCloseOnFocusLoss"),
                 normalized.popup.autoCloseOnFocusLoss);
    out.endGroup();

    out.beginGroup(QStringLiteral("mainWindow"));
    out.setValue(QStringLiteral("geometry"), normalized.mainWindow.geometry);
    out.setValue(QStringLiteral("state"), normalized.mainWindow.state);
    out.endGroup();

    out.sync();
    if (out.status() != QSettings::NoError)
        qCWarning(lcSettings) << "Cannot write settings" << m_filePath
                              << "status" << int(out.status());

    m_current = normalized;
    emit changed(old, m_current);
}
