#include "core/apppaths.h"
#include "core/languages.h"
#include "core/settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

SecretCodec fakeCodec()
{
    return {
        [](const QString &plainText) {
            return QByteArrayLiteral("P:") + plainText.toUtf8().toBase64();
        },
        [](const QByteArray &protectedData) {
            if (!protectedData.startsWith(QByteArrayLiteral("P:")))
                return QString();
            return QString::fromUtf8(
                QByteArray::fromBase64(protectedData.mid(2)));
        },
    };
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

#if defined(Q_OS_LINUX)
// Saves and restores an environment variable even when an assertion ends the
// test function early.
class EnvironmentGuard
{
public:
    explicit EnvironmentGuard(const char *name)
        : m_name(name)
        , m_value(qgetenv(name))
        , m_wasSet(qEnvironmentVariableIsSet(name))
    {
    }

    ~EnvironmentGuard()
    {
        if (m_wasSet)
            qputenv(m_name, m_value);
        else
            qunsetenv(m_name);
    }

private:
    const char *m_name;
    QByteArray m_value;
    bool m_wasSet;
};
#endif

} // namespace

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<AppSettings>();
    }

    void defaults()
    {
        const AppSettings settings;
        QCOMPARE(settings.settingsVersion, AppSettings::kCurrentVersion);
        QVERIFY(!settings.general.autostart);
        QVERIFY(settings.general.startMinimized);
        QVERIFY(settings.general.restoreClipboard);
        QCOMPARE(settings.general.uiLanguage, QStringLiteral("system"));

        QCOMPARE(settings.hotkeys.popup, QKeySequence(Qt::CTRL | Qt::Key_Q));
        QCOMPARE(settings.hotkeys.replaceSelection,
                 QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q));
        QCOMPARE(settings.hotkeys.mainWindow, QKeySequence(Qt::CTRL | Qt::Key_W));
        QVERIFY(settings.hotkeys.showMainWindow.isEmpty());
        QVERIFY(settings.hotkeys.pauseInFullscreen);

        QCOMPARE(settings.translation.service, QStringLiteral("google"));
        QCOMPARE(settings.translation.sourceLang, Languages::autoCode());
        QCOMPARE(settings.translation.primaryLang, Languages::systemLanguage());
        QVERIFY(settings.translation.secondaryLang != settings.translation.primaryLang);
        QCOMPARE(settings.translation.maxLength, 20000);
        QCOMPARE(settings.translation.autoTranslateDelayMs, 700);

        QCOMPARE(settings.google.mode, QStringLiteral("free"));
        QCOMPARE(settings.deepl.mode, QStringLiteral("api"));
        QCOMPARE(settings.deepl.englishVariant, QStringLiteral("EN-US"));
        QCOMPARE(settings.llm.baseUrl, QStringLiteral("https://openrouter.ai/api/v1"));
        QVERIFY(settings.llm.apiKey.isEmpty());
        QCOMPARE(settings.llm.model, QStringLiteral("openrouter/free"));
        QCOMPARE(settings.network.proxyMode, QStringLiteral("system"));
        QCOMPARE(settings.network.timeoutMs, 10000);
        QCOMPARE(settings.popup.width, 460);
        QCOMPARE(settings.popup.height, 260);
        QVERIFY(settings.popup.autoCloseOnFocusLoss);
    }

    void roundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));

        SettingsStore store(path, fakeCodec());
        AppSettings settings = store.current();
        settings.general.autostart = true;
        settings.general.startMinimized = false;
        settings.general.uiLanguage = QStringLiteral("ru");
        settings.general.restoreClipboard = false;

        settings.hotkeys.popup = QKeySequence(QStringLiteral("Ctrl+Alt+T"));
        settings.hotkeys.replaceSelection = QKeySequence(QStringLiteral("Ctrl+Alt+R"));
        settings.hotkeys.mainWindow = QKeySequence();
        settings.hotkeys.showMainWindow = QKeySequence(QStringLiteral("Ctrl+Shift+F12"));
        settings.hotkeys.pauseInFullscreen = false;

        settings.translation.service = QStringLiteral("deepl");
        settings.translation.sourceLang = QStringLiteral("en");
        settings.translation.primaryLang = QStringLiteral("de");
        settings.translation.secondaryLang = QStringLiteral("fr");
        settings.translation.maxLength = 5000;
        settings.translation.autoTranslateDelayMs = 0;

        settings.google.mode = QStringLiteral("cloud");
        settings.google.apiKey = QStringLiteral("google-secret");
        settings.deepl.mode = QStringLiteral("free-web");
        settings.deepl.apiKey = QStringLiteral("deepl-secret");
        settings.deepl.englishVariant = QStringLiteral("EN-GB");
        settings.llm.baseUrl = QStringLiteral("http://127.0.0.1:11434/v1");
        settings.llm.apiKey = QStringLiteral("llm-secret");
        settings.llm.model = QStringLiteral("local-model");

        settings.network.proxyMode = QStringLiteral("manual");
        settings.network.proxyType = QStringLiteral("socks5");
        settings.network.proxyHost = QStringLiteral("127.0.0.1");
        settings.network.proxyPort = 1080;
        settings.network.proxyAuth = true;
        settings.network.proxyUser = QStringLiteral("user");
        settings.network.proxyPassword = QStringLiteral("proxy-secret");
        settings.network.timeoutMs = 12000;

        settings.popup.width = 520;
        settings.popup.height = 300;
        settings.popup.autoCloseOnFocusLoss = false;

        settings.mainWindow.geometry = QByteArray::fromHex("0011223344ff");
        settings.mainWindow.state = QByteArray("state-data");

        store.save(settings);
        QCOMPARE(store.current().general.autostart, true);

        SettingsStore loaded(path, fakeCodec());
        loaded.load();
        const AppSettings &result = loaded.current();

        QCOMPARE(result.general.autostart, true);
        QCOMPARE(result.general.startMinimized, false);
        QCOMPARE(result.general.uiLanguage, QStringLiteral("ru"));
        QCOMPARE(result.general.restoreClipboard, false);

        QCOMPARE(result.hotkeys.popup, QKeySequence(QStringLiteral("Ctrl+Alt+T")));
        QCOMPARE(result.hotkeys.replaceSelection,
                 QKeySequence(QStringLiteral("Ctrl+Alt+R")));
        QVERIFY(result.hotkeys.mainWindow.isEmpty());
        QCOMPARE(result.hotkeys.showMainWindow,
                 QKeySequence(QStringLiteral("Ctrl+Shift+F12")));
        QVERIFY(!result.hotkeys.pauseInFullscreen);

        QCOMPARE(result.translation.service, QStringLiteral("deepl"));
        QCOMPARE(result.translation.sourceLang, QStringLiteral("en"));
        QCOMPARE(result.translation.primaryLang, QStringLiteral("de"));
        QCOMPARE(result.translation.secondaryLang, QStringLiteral("fr"));
        QCOMPARE(result.translation.maxLength, 5000);
        QCOMPARE(result.translation.autoTranslateDelayMs, 0);

        QCOMPARE(result.google.mode, QStringLiteral("cloud"));
        QCOMPARE(result.google.apiKey, QStringLiteral("google-secret"));
        QCOMPARE(result.deepl.mode, QStringLiteral("free-web"));
        QCOMPARE(result.deepl.apiKey, QStringLiteral("deepl-secret"));
        QCOMPARE(result.deepl.englishVariant, QStringLiteral("EN-GB"));
        QCOMPARE(result.llm.baseUrl, QStringLiteral("http://127.0.0.1:11434/v1"));
        QCOMPARE(result.llm.apiKey, QStringLiteral("llm-secret"));
        QCOMPARE(result.llm.model, QStringLiteral("local-model"));

        QCOMPARE(result.network.proxyMode, QStringLiteral("manual"));
        QCOMPARE(result.network.proxyType, QStringLiteral("socks5"));
        QCOMPARE(result.network.proxyHost, QStringLiteral("127.0.0.1"));
        QCOMPARE(result.network.proxyPort, 1080);
        QVERIFY(result.network.proxyAuth);
        QCOMPARE(result.network.proxyUser, QStringLiteral("user"));
        QCOMPARE(result.network.proxyPassword, QStringLiteral("proxy-secret"));
        QCOMPARE(result.network.timeoutMs, 12000);

        QCOMPARE(result.popup.width, 520);
        QCOMPARE(result.popup.height, 300);
        QCOMPARE(result.popup.autoCloseOnFocusLoss, false);

        QCOMPARE(result.mainWindow.geometry, QByteArray::fromHex("0011223344ff"));
        QCOMPARE(result.mainWindow.state, QByteArray("state-data"));

        const QByteArray ini = readFile(path);
        QVERIFY(!ini.isEmpty());
        QVERIFY(!ini.contains("google-secret"));
        QVERIFY(!ini.contains("deepl-secret"));
        QVERIFY(!ini.contains("llm-secret"));
        QVERIFY(!ini.contains("proxy-secret"));
    }

    void migratesLegacyDeepLWebService()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));
        {
            QSettings raw(path, QSettings::IniFormat);
            raw.setValue(QStringLiteral("settingsVersion"), 1);
            raw.setValue(QStringLiteral("translation/service"), QStringLiteral("deepl-web"));
            raw.sync();
        }

        SettingsStore store(path, fakeCodec());
        store.load();
        QCOMPARE(store.current().translation.service, QStringLiteral("deepl"));
        QCOMPARE(store.current().deepl.mode, QStringLiteral("free-web"));
    }

    void changedSignal()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SettingsStore store(dir.filePath(QStringLiteral("qtranslate.ini")), fakeCodec());
        QSignalSpy changed(&store, &SettingsStore::changed);

        AppSettings settings = store.current();
        settings.general.autostart = true;
        store.save(settings);

        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.at(0).at(0).value<AppSettings>().general.autostart, false);
        QCOMPARE(changed.at(0).at(1).value<AppSettings>().general.autostart, true);
        QCOMPARE(store.current().general.autostart, true);
    }

    void missingFileUsesDefaults()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("missing.ini"));
        QVERIFY(!QFileInfo::exists(path));

        SettingsStore store(path, fakeCodec());
        store.load();
        QCOMPARE(store.current().translation.maxLength, 20000);
        QCOMPARE(store.current().hotkeys.popup, QKeySequence(Qt::CTRL | Qt::Key_Q));
    }

    void corruptValuesAreNormalized()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));

        {
            QSettings raw(path, QSettings::IniFormat);
            raw.setValue(QStringLiteral("settingsVersion"), 1);
            raw.setValue(QStringLiteral("app/uiLanguage"), QStringLiteral("xx"));
            raw.setValue(QStringLiteral("translation/maxLength"), -5);
            raw.setValue(QStringLiteral("translation/autoTranslateDelayMs"), 999999);
            raw.setValue(QStringLiteral("translation/primaryLang"), QString());
            raw.setValue(QStringLiteral("translation/secondaryLang"), QString());
            raw.setValue(QStringLiteral("hotkeys/popup"), QStringLiteral("not-a-key"));
            raw.setValue(QStringLiteral("popup/width"), 10);
            raw.setValue(QStringLiteral("popup/height"), 10000);
            raw.setValue(QStringLiteral("network/timeoutMs"), 1);
            raw.sync();
        }

        SettingsStore store(path, fakeCodec());
        store.load();
        const AppSettings &result = store.current();
        QCOMPARE(result.general.uiLanguage, QStringLiteral("system"));
        QCOMPARE(result.translation.maxLength, 20000);
        QCOMPARE(result.translation.autoTranslateDelayMs, 700);
        QCOMPARE(result.translation.primaryLang, Languages::systemLanguage());
        QVERIFY(result.translation.secondaryLang != result.translation.primaryLang);
        QCOMPARE(result.hotkeys.popup, QKeySequence(Qt::CTRL | Qt::Key_Q));
        QCOMPARE(result.popup.width, 460);
        QCOMPARE(result.popup.height, 260);
        QCOMPARE(result.network.timeoutMs, 10000);
    }

    void portablePath()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString appDir = dir.filePath(QStringLiteral("app"));
        const QString dataDir = dir.filePath(QStringLiteral("data"));
        QVERIFY(QDir().mkpath(appDir));
        QVERIFY(QDir().mkpath(dataDir));

        QCOMPARE(SettingsStore::defaultFilePath(appDir, dataDir),
                 QDir::cleanPath(QDir(dataDir).filePath(QStringLiteral("qtranslate.ini"))));

        const QString portable = QDir(appDir).filePath(QStringLiteral("qtranslate.ini"));
        QFile file(portable);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        QCOMPARE(SettingsStore::defaultFilePath(appDir, dataDir),
                 QDir::cleanPath(portable));
    }

#if defined(Q_OS_LINUX)
    void settingsPathUsesXdgConfigHome()
    {
        EnvironmentGuard configHome("XDG_CONFIG_HOME");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());

        const QString expectedConfig = QDir(dir.path()).filePath(QStringLiteral("qtranslate"));
        QCOMPARE(AppPaths::configDirectory(), expectedConfig);
        QCOMPARE(SettingsStore::defaultFilePath(QString(), expectedConfig),
                 QDir(expectedConfig).filePath(QStringLiteral("qtranslate.ini")));

        // A relative XDG value is invalid and must fall back to ~/.config.
        qputenv("XDG_CONFIG_HOME", QByteArrayLiteral("relative/config"));
        const QString fallback =
            QDir(QDir::homePath()).filePath(QStringLiteral(".config/qtranslate"));
        QCOMPARE(AppPaths::configDirectory(), fallback);
        QVERIFY(QDir::isAbsolutePath(AppPaths::configDirectory()));
    }
#endif

    void secretsRequireCodec()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));

        SettingsStore store(path, SecretCodec{});
        AppSettings settings = store.current();
        settings.google.apiKey = QStringLiteral("do-not-store-me");
        store.save(settings);

        const QByteArray ini = readFile(path);
        QVERIFY(!ini.contains("do-not-store-me"));

        SettingsStore loaded(path, SecretCodec{});
        loaded.load();
        QVERIFY(loaded.current().google.apiKey.isEmpty());
    }

    void unchangedSecretsAreNotReprotected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));

        {
            SettingsStore store(path, fakeCodec());
            AppSettings settings = store.current();
            settings.google.apiKey = QStringLiteral("google-secret");
            settings.llm.apiKey = QStringLiteral("llm-secret");
            store.save(settings);
        }

        QString googleToken;
        QString llmToken;
        {
            QSettings raw(path, QSettings::IniFormat);
            googleToken = raw.value(QStringLiteral("google/apiKey")).toString();
            llmToken = raw.value(QStringLiteral("llm/apiKey")).toString();
        }
        QVERIFY(!googleToken.isEmpty());
        QVERIFY(!llmToken.isEmpty());

        int protectCalls = 0;
        const SecretCodec countingCodec{
            [&protectCalls](const QString &plainText) {
                ++protectCalls;
                return QByteArrayLiteral("P:") + plainText.toUtf8().toBase64();
            },
            [](const QByteArray &protectedData) {
                if (!protectedData.startsWith(QByteArrayLiteral("P:")))
                    return QString();
                return QString::fromUtf8(
                    QByteArray::fromBase64(protectedData.mid(2)));
            },
        };

        {
            SettingsStore store(path, countingCodec);
            store.load();
            QCOMPARE(store.current().google.apiKey, QStringLiteral("google-secret"));
            QCOMPARE(store.current().llm.apiKey, QStringLiteral("llm-secret"));

            AppSettings settings = store.current();
            settings.mainWindow.geometry = QByteArray::fromHex("00112233");
            store.save(settings);
            QCOMPARE(protectCalls, 0);
        }

        {
            QSettings raw(path, QSettings::IniFormat);
            QCOMPARE(raw.value(QStringLiteral("google/apiKey")).toString(), googleToken);
            QCOMPARE(raw.value(QStringLiteral("llm/apiKey")).toString(), llmToken);
        }

        {
            SettingsStore store(path, countingCodec);
            store.load();
            AppSettings settings = store.current();
            settings.google.apiKey = QStringLiteral("google-secret-2");
            store.save(settings);
            QCOMPARE(protectCalls, 1);
        }

        {
            SettingsStore store(path, countingCodec);
            store.load();
            QCOMPARE(store.current().google.apiKey, QStringLiteral("google-secret-2"));
            QCOMPARE(store.current().llm.apiKey, QStringLiteral("llm-secret"));
        }

        {
            QSettings raw(path, QSettings::IniFormat);
            QVERIFY(raw.value(QStringLiteral("google/apiKey")).toString() != googleToken);
            QCOMPARE(raw.value(QStringLiteral("llm/apiKey")).toString(), llmToken);
        }
    }

    void fallbackSecretIsMigratedWhenBackendChanges()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));

        const SecretCodec oldCodec{
            [](const QString &plainText) {
                return QByteArrayLiteral("O:") + plainText.toUtf8().toBase64();
            },
            [](const QByteArray &protectedData) {
                if (!protectedData.startsWith(QByteArrayLiteral("O:")))
                    return QString();
                return QString::fromUtf8(
                    QByteArray::fromBase64(protectedData.mid(2)));
            },
        };

        {
            SettingsStore store(path, oldCodec);
            AppSettings settings = store.current();
            settings.google.apiKey = QStringLiteral("google-secret");
            store.save(settings);
        }

        int protectCalls = 0;
        const SecretCodec newCodec{
            [&protectCalls](const QString &plainText) {
                ++protectCalls;
                return QByteArrayLiteral("N:") + plainText.toUtf8().toBase64();
            },
            [](const QByteArray &protectedData) {
                if (!protectedData.startsWith(QByteArrayLiteral("O:"))
                    && !protectedData.startsWith(QByteArrayLiteral("N:"))) {
                    return QString();
                }
                return QString::fromUtf8(
                    QByteArray::fromBase64(protectedData.mid(2)));
            },
            [](const QByteArray &protectedData) {
                return protectedData.startsWith(QByteArrayLiteral("N:"));
            },
        };

        // The old token is readable but the new backend cannot reuse it, so the
        // unchanged secret is protected again and migrated to the new format.
        {
            SettingsStore store(path, newCodec);
            store.load();
            QCOMPARE(store.current().google.apiKey, QStringLiteral("google-secret"));

            store.save(store.current());
            QCOMPARE(protectCalls, 1);
        }

        {
            QSettings raw(path, QSettings::IniFormat);
            const QByteArray stored = QByteArray::fromBase64(
                raw.value(QStringLiteral("google/apiKey")).toString().toLatin1());
            QVERIFY(stored.startsWith(QByteArrayLiteral("N:")));
        }

        // Once migrated, the new token is reused and the secret is not
        // protected again.
        {
            SettingsStore store(path, newCodec);
            store.load();
            QCOMPARE(store.current().google.apiKey, QStringLiteral("google-secret"));

            store.save(store.current());
            QCOMPARE(protectCalls, 1);
        }
    }

    void failedProtectRetriesOnNextSave()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));

        SettingsStore store(path, SecretCodec{
                                       [](const QString &) { return QByteArray(); },
                                       [](const QByteArray &) { return QString(); },
                                   });
        AppSettings settings = store.current();
        settings.google.apiKey = QStringLiteral("retry-me");
        store.save(settings);

        {
            QSettings raw(path, QSettings::IniFormat);
            QVERIFY(!raw.contains(QStringLiteral("google/apiKey")));
        }
        // The plaintext stays in memory even though the protect failure
        // removed the key from the file.
        QCOMPARE(store.current().google.apiKey, QStringLiteral("retry-me"));

        store.setSecretCodec(fakeCodec());
        store.save(store.current());

        {
            QSettings raw(path, QSettings::IniFormat);
            QVERIFY(raw.contains(QStringLiteral("google/apiKey")));
        }

        SettingsStore loaded(path, fakeCodec());
        loaded.load();
        QCOMPARE(loaded.current().google.apiKey, QStringLiteral("retry-me"));
    }

    void failedSecretLoadKeepsStoredToken()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));

        {
            SettingsStore store(path, fakeCodec());
            AppSettings settings = store.current();
            settings.google.apiKey = QStringLiteral("google-secret");
            store.save(settings);
        }

        QString token;
        {
            QSettings raw(path, QSettings::IniFormat);
            token = raw.value(QStringLiteral("google/apiKey")).toString();
        }
        QVERIFY(!token.isEmpty());

        // The keyring is unavailable: the token cannot be decrypted, so the
        // secret loads empty while the file still holds a usable token.
        const SecretCodec lockedCodec{
            [](const QString &plainText) {
                return QByteArrayLiteral("P:") + plainText.toUtf8().toBase64();
            },
            [](const QByteArray &) { return QString(); },
            [](const QByteArray &protectedData) {
                return protectedData.startsWith(QByteArrayLiteral("P:"));
            },
        };
        {
            SettingsStore store(path, lockedCodec);
            store.load();
            QVERIFY(store.current().google.apiKey.isEmpty());

            // Saving on exit must not delete the token that failed to load.
            store.save(store.current());
        }
        {
            QSettings raw(path, QSettings::IniFormat);
            QCOMPARE(raw.value(QStringLiteral("google/apiKey")).toString(), token);
        }

        // Once the keyring works again the secret is back, and an explicit
        // removal still deletes it.
        {
            SettingsStore store(path, fakeCodec());
            store.load();
            QCOMPARE(store.current().google.apiKey, QStringLiteral("google-secret"));

            AppSettings settings = store.current();
            settings.google.apiKey.clear();
            store.save(settings);
        }
        {
            QSettings raw(path, QSettings::IniFormat);
            QVERIFY(!raw.contains(QStringLiteral("google/apiKey")));
        }
    }

    void futureVersionUsesDefaults()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));
        {
            QSettings raw(path, QSettings::IniFormat);
            raw.setValue(QStringLiteral("settingsVersion"), 99);
            raw.setValue(QStringLiteral("translation/maxLength"), 1234);
            raw.sync();
        }

        SettingsStore store(path, fakeCodec());
        store.load();
        QCOMPARE(store.current().settingsVersion, AppSettings::kCurrentVersion);
        QCOMPARE(store.current().translation.maxLength, 20000);
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
