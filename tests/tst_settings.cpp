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
