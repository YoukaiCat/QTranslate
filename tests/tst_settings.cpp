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
        QVERIFY(!settings.translation.allowFreeServices);
        QVERIFY(settings.translation.fallbackService.isEmpty());
        QCOMPARE(fallbackServiceIds(),
                 QStringList({QStringLiteral("mymemory"), QStringLiteral("mozhi"),
                              QStringLiteral("yandex-web"), QStringLiteral("bing-web"),
                              QStringLiteral("reverso")}));

        QCOMPARE(settings.google.mode, QStringLiteral("free"));
        QCOMPARE(settings.deepl.mode, QStringLiteral("api"));
        QCOMPARE(settings.deepl.englishVariant, QStringLiteral("EN-US"));
        QCOMPARE(settings.mozhi.instance, QStringLiteral("https://mozhi.adminforge.de"));
        QCOMPARE(settings.mozhi.engine, QStringLiteral("google"));
        QCOMPARE(mozhiEngineIds(),
                 QStringList({QStringLiteral("google"), QStringLiteral("deepl"),
                              QStringLiteral("duckduckgo"), QStringLiteral("libretranslate"),
                              QStringLiteral("mymemory"), QStringLiteral("reverso"),
                              QStringLiteral("yandex")}));
        QCOMPARE(settings.llm.baseUrl, QStringLiteral("https://openrouter.ai/api/v1"));
        QVERIFY(settings.llm.apiKey.isEmpty());
        QCOMPARE(settings.llm.model, QStringLiteral("openrouter/free"));

        QVERIFY(!settings.proofreading.enabled);
        QCOMPARE(settings.proofreading.provider, QStringLiteral("languagetool"));
        QCOMPARE(proofreadingProviderIds(),
                 QStringList({QStringLiteral("languagetool")}));
        QCOMPARE(settings.proofreading.language, Languages::autoCode());
        QVERIFY(settings.proofreading.endpoint.isEmpty());
        QVERIFY(settings.proofreading.apiKey.isEmpty());
        QCOMPARE(settings.proofreading.categories, proofreadingCategoryIds());
        QCOMPARE(settings.proofreading.categories,
                 QStringList({QStringLiteral("spelling"), QStringLiteral("grammar"),
                              QStringLiteral("punctuation"), QStringLiteral("style")}));
        QVERIFY(!settings.proofreading.checkBeforeTranslation);
        QVERIFY(!settings.proofreading.autoReplace);
        QVERIFY(!settings.proofreading.allowOnlineServices);

        QCOMPARE(settings.network.proxyMode, QStringLiteral("system"));
        QCOMPARE(settings.network.timeoutMs, 10000);
        QCOMPARE(settings.popup.width, 460);
        QCOMPARE(settings.popup.height, 260);
        QVERIFY(settings.popup.autoCloseOnFocusLoss);
    }

    void mozhiInstanceIsNormalized_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<bool>("valid");

        const QString defaultInstance = QStringLiteral("https://mozhi.adminforge.de");
        QTest::newRow("default") << defaultInstance << defaultInstance << true;
        QTest::newRow("empty") << QString() << defaultInstance << false;
        QTest::newRow("whitespace") << QStringLiteral("   ") << defaultInstance << false;
        QTest::newRow("bare host") << QStringLiteral("mozhi.adminforge.de") << defaultInstance
                                   << false;
        QTest::newRow("ftp") << QStringLiteral("ftp://host") << defaultInstance << false;
        QTest::newRow("no host") << QStringLiteral("https://") << defaultInstance << false;
        QTest::newRow("malformed ipv6") << QStringLiteral("http://[::1") << defaultInstance
                                        << false;
        QTest::newRow("bad port") << QStringLiteral("https://host:bad") << defaultInstance
                                  << false;
        QTest::newRow("user and password") << QStringLiteral("https://user:pass@host")
                                           << defaultInstance << false;
        QTest::newRow("user") << QStringLiteral("https://user@host") << defaultInstance
                              << false;
        QTest::newRow("query") << QStringLiteral("https://host/?engine=google")
                               << defaultInstance << false;
        QTest::newRow("fragment") << QStringLiteral("https://host/#fragment") << defaultInstance
                                  << false;
        QTest::newRow("port and path prefix")
            << QStringLiteral("http://127.0.0.1:8080/mozhi/")
            << QStringLiteral("http://127.0.0.1:8080/mozhi") << true;
        QTest::newRow("path case is kept") << QStringLiteral("https://host/Mozhi/")
                                           << QStringLiteral("https://host/Mozhi") << true;
        QTest::newRow("surrounding whitespace") << QStringLiteral("  https://host/mozhi  ")
                                                << QStringLiteral("https://host/mozhi") << true;
    }

    void mozhiInstanceIsNormalized()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QFETCH(bool, valid);

        QCOMPARE(isValidMozhiInstance(input), valid);
        QCOMPARE(normalizedMozhiInstance(input), expected);
    }

    void mozhiEngineIsNormalized_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");

        QTest::newRow("google") << QStringLiteral("google") << QStringLiteral("google");
        QTest::newRow("deepl") << QStringLiteral("deepl") << QStringLiteral("deepl");
        QTest::newRow("duckduckgo") << QStringLiteral("duckduckgo")
                                    << QStringLiteral("duckduckgo");
        QTest::newRow("libretranslate") << QStringLiteral("libretranslate")
                                        << QStringLiteral("libretranslate");
        QTest::newRow("mymemory") << QStringLiteral("mymemory") << QStringLiteral("mymemory");
        QTest::newRow("reverso") << QStringLiteral("reverso") << QStringLiteral("reverso");
        QTest::newRow("yandex") << QStringLiteral("yandex") << QStringLiteral("yandex");
        QTest::newRow("uppercase") << QStringLiteral("DEEPL") << QStringLiteral("deepl");
        QTest::newRow("surrounding whitespace") << QStringLiteral("  Google ")
                                                << QStringLiteral("google");
        QTest::newRow("empty") << QString() << QStringLiteral("google");
        QTest::newRow("whitespace") << QStringLiteral("   ") << QStringLiteral("google");
        QTest::newRow("unknown") << QStringLiteral("unknown") << QStringLiteral("google");
        QTest::newRow("auto") << QStringLiteral("auto") << QStringLiteral("google");
    }

    void mozhiEngineIsNormalized()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);

        QCOMPARE(normalizedMozhiEngine(input), expected);
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
        settings.translation.allowFreeServices = true;
        settings.translation.fallbackService = QStringLiteral("mymemory");

        settings.google.mode = QStringLiteral("cloud");
        settings.google.apiKey = QStringLiteral("google-secret");
        settings.deepl.mode = QStringLiteral("free-web");
        settings.deepl.apiKey = QStringLiteral("deepl-secret");
        settings.deepl.englishVariant = QStringLiteral("EN-GB");
        settings.mozhi.instance = QStringLiteral("http://127.0.0.1:8080/mozhi/");
        settings.mozhi.engine = QStringLiteral("deepl");
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
        QVERIFY(result.translation.allowFreeServices);
        QCOMPARE(result.translation.fallbackService, QStringLiteral("mymemory"));

        QCOMPARE(result.google.mode, QStringLiteral("cloud"));
        QCOMPARE(result.google.apiKey, QStringLiteral("google-secret"));
        QCOMPARE(result.deepl.mode, QStringLiteral("free-web"));
        QCOMPARE(result.deepl.apiKey, QStringLiteral("deepl-secret"));
        QCOMPARE(result.deepl.englishVariant, QStringLiteral("EN-GB"));
        QCOMPARE(result.mozhi.instance, QStringLiteral("http://127.0.0.1:8080/mozhi"));
        QCOMPARE(result.mozhi.engine, QStringLiteral("deepl"));
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

        // The INI itself holds the canonical values, not only the loaded
        // settings.
        {
            QSettings raw(path, QSettings::IniFormat);
            QCOMPARE(raw.value(QStringLiteral("mozhi/instance")).toString(),
                     QStringLiteral("http://127.0.0.1:8080/mozhi"));
            QCOMPARE(raw.value(QStringLiteral("mozhi/engine")).toString(),
                     QStringLiteral("deepl"));
            QCOMPARE(raw.value(QStringLiteral("translation/fallbackService")).toString(),
                     QStringLiteral("mymemory"));
        }

        const QByteArray ini = readFile(path);
        QVERIFY(!ini.isEmpty());
        QVERIFY(ini.contains("allowFreeServices=true"));
        QVERIFY(ini.contains("fallbackService=mymemory"));
        QVERIFY(!ini.contains("google-secret"));
        QVERIFY(!ini.contains("deepl-secret"));
        QVERIFY(!ini.contains("llm-secret"));
        QVERIFY(!ini.contains("proxy-secret"));
    }

    void proofreadingEndpointIsNormalized_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<bool>("valid");

        const QString empty;
        const QString defaultCheckUrl = QStringLiteral("https://api.languagetool.org/v2/check");
        QTest::newRow("empty") << QString() << empty << true;
        QTest::newRow("whitespace") << QStringLiteral("   ") << empty << true;
        QTest::newRow("default check url") << defaultCheckUrl << defaultCheckUrl << true;
        QTest::newRow("self-hosted")
            << QStringLiteral("http://127.0.0.1:8081/v2/check")
            << QStringLiteral("http://127.0.0.1:8081/v2/check") << true;
        QTest::newRow("surrounding whitespace")
            << QStringLiteral("  https://host/v2/check  ")
            << QStringLiteral("https://host/v2/check") << true;
        QTest::newRow("bare host") << QStringLiteral("api.languagetool.org/v2/check")
                                   << empty << false;
        QTest::newRow("ftp") << QStringLiteral("ftp://host/v2/check") << empty << false;
        QTest::newRow("no host") << QStringLiteral("https://") << empty << false;
        QTest::newRow("bad port") << QStringLiteral("https://host:bad/v2/check") << empty
                                  << false;
        QTest::newRow("user and password") << QStringLiteral("https://user:pass@host/v2/check")
                                           << empty << false;
        QTest::newRow("user") << QStringLiteral("https://user@host/v2/check") << empty
                              << false;
        QTest::newRow("query") << QStringLiteral("https://host/v2/check?language=en")
                               << empty << false;
        QTest::newRow("fragment") << QStringLiteral("https://host/v2/check#fragment")
                                  << empty << false;
    }

    void proofreadingEndpointIsNormalized()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QFETCH(bool, valid);

        QCOMPARE(isValidProofreadingEndpoint(input), valid);
        QCOMPARE(normalizedProofreadingEndpoint(input), expected);
    }

    void proofreadingRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));

        SettingsStore store(path, fakeCodec());
        AppSettings settings = store.current();
        settings.proofreading.enabled = true;
        settings.proofreading.provider = QStringLiteral("languagetool");
        settings.proofreading.language = QStringLiteral("ru");
        settings.proofreading.endpoint = QStringLiteral("https://lt.example.invalid/v2/check");
        settings.proofreading.apiKey = QStringLiteral("proofreading-secret");
        settings.proofreading.categories =
            QStringList({QStringLiteral("style"), QStringLiteral("grammar")});
        settings.proofreading.checkBeforeTranslation = true;
        settings.proofreading.autoReplace = true;
        settings.proofreading.allowOnlineServices = true;
        store.save(settings);

        SettingsStore loaded(path, fakeCodec());
        loaded.load();
        const AppSettings &result = loaded.current();
        QCOMPARE(result.proofreading.enabled, true);
        QCOMPARE(result.proofreading.provider, QStringLiteral("languagetool"));
        QCOMPARE(result.proofreading.language, QStringLiteral("ru"));
        QCOMPARE(result.proofreading.endpoint,
                 QStringLiteral("https://lt.example.invalid/v2/check"));
        QCOMPARE(result.proofreading.apiKey, QStringLiteral("proofreading-secret"));
        QCOMPARE(result.proofreading.categories,
                 QStringList({QStringLiteral("grammar"), QStringLiteral("style")}));
        QVERIFY(result.proofreading.checkBeforeTranslation);
        QVERIFY(result.proofreading.autoReplace);
        QVERIFY(result.proofreading.allowOnlineServices);
        QCOMPARE(result.settingsVersion, 1);

        // The INI itself holds the group and a protected key, never the secret.
        {
            QSettings raw(path, QSettings::IniFormat);
            QVERIFY(raw.childGroups().contains(QStringLiteral("proofreading")));
            QCOMPARE(raw.value(QStringLiteral("proofreading/provider")).toString(),
                     QStringLiteral("languagetool"));
            QVERIFY(!raw.value(QStringLiteral("proofreading/apiKey")).toString().isEmpty());
        }
        const QByteArray ini = readFile(path);
        QVERIFY(ini.contains("[proofreading]"));
        QVERIFY(!ini.contains("proofreading-secret"));
    }

    void proofreadingValuesAreNormalized()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));

        {
            QSettings raw(path, QSettings::IniFormat);
            raw.setValue(QStringLiteral("settingsVersion"), 1);
            raw.setValue(QStringLiteral("proofreading/provider"),
                         QStringLiteral("  LanguageTool "));
            raw.setValue(QStringLiteral("proofreading/language"), QStringLiteral("xx"));
            raw.setValue(
                QStringLiteral("proofreading/endpoint"),
                QStringLiteral("https://user:secret@example.invalid/v2/check?key=secret"));
            raw.setValue(QStringLiteral("proofreading/categories"),
                         QStringLiteral(" style, SPELLING ,unknown,grammar"));
            raw.sync();
        }

        SettingsStore store(path, fakeCodec());
        QTest::ignoreMessage(QtWarningMsg,
                             "Proofreading language is not known; using auto");
        QTest::ignoreMessage(
            QtWarningMsg,
            "Proofreading endpoint is not an absolute http(s) URL; using the provider default");
        store.load();
        const AppSettings &result = store.current();
        QCOMPARE(result.settingsVersion, 1);
        QCOMPARE(result.proofreading.provider, QStringLiteral("languagetool"));
        QCOMPARE(result.proofreading.language, Languages::autoCode());
        QVERIFY(result.proofreading.endpoint.isEmpty());
        QCOMPARE(result.proofreading.categories,
                 QStringList({QStringLiteral("spelling"), QStringLiteral("grammar"),
                              QStringLiteral("style")}));
    }

    void proofreadingProviderIsNormalized()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));
        {
            QSettings raw(path, QSettings::IniFormat);
            raw.setValue(QStringLiteral("settingsVersion"), 1);
            raw.setValue(QStringLiteral("proofreading/provider"),
                         QStringLiteral(" unknown-provider "));
            raw.sync();
        }

        SettingsStore store(path, fakeCodec());
        const QString warning =
            QStringLiteral("Proofreading provider is not supported; using the default");
        QTest::ignoreMessage(QtWarningMsg, qPrintable(warning));
        store.load();
        QCOMPARE(store.current().proofreading.provider, QStringLiteral("languagetool"));
        QCOMPARE(store.current().settingsVersion, 1);
    }

    void proofreadingCategoriesFallBackToAll()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));

        for (const QString &value :
             {QStringLiteral("unknown,unknown-2"), QString()}) {
            {
                QSettings raw(path, QSettings::IniFormat);
                raw.setValue(QStringLiteral("settingsVersion"), 1);
                raw.setValue(QStringLiteral("proofreading/categories"), value);
                raw.sync();
            }

            SettingsStore store(path, fakeCodec());
            const QString warning =
                QStringLiteral("Proofreading categories are empty or unknown; using all");
            QTest::ignoreMessage(QtWarningMsg, qPrintable(warning));
            store.load();
            QCOMPARE(store.current().settingsVersion, 1);
            QCOMPARE(store.current().proofreading.categories, proofreadingCategoryIds());
            QCOMPARE(proofreadingCategoryIds(),
                     QStringList({QStringLiteral("spelling"), QStringLiteral("grammar"),
                                  QStringLiteral("punctuation"), QStringLiteral("style")}));
        }
    }

    void proofreadingGroupIsOptional()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));
        {
            QSettings raw(path, QSettings::IniFormat);
            raw.setValue(QStringLiteral("settingsVersion"), 1);
            raw.setValue(QStringLiteral("app/startMinimized"), false);
            raw.sync();
        }
        const QByteArray before = readFile(path);
        QVERIFY(!before.isEmpty());
        QVERIFY(!before.contains("[proofreading]"));

        SettingsStore store(path, fakeCodec());
        store.load();
        QCOMPARE(store.current().settingsVersion, 1);
        QCOMPARE(store.current().general.startMinimized, false);
        QCOMPARE(store.current().proofreading.provider, QStringLiteral("languagetool"));
        QCOMPARE(store.current().proofreading.categories, proofreadingCategoryIds());
        // Loading an old file must not rewrite it.
        QCOMPARE(readFile(path), before);
    }

    void proofreadingChangesDoNotTouchTranslation()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));

        SettingsStore store(path, fakeCodec());
        AppSettings settings = store.current();
        settings.translation.service = QStringLiteral("deepl");
        settings.translation.allowFreeServices = true;
        settings.translation.fallbackService = QStringLiteral("mozhi");
        settings.translation.maxLength = 5000;
        store.save(settings);

        settings = store.current();
        settings.proofreading.enabled = true;
        settings.proofreading.provider = QStringLiteral("languagetool");
        settings.proofreading.allowOnlineServices = true;
        store.save(settings);

        SettingsStore loaded(path, fakeCodec());
        loaded.load();
        QCOMPARE(loaded.current().translation.service, QStringLiteral("deepl"));
        QVERIFY(loaded.current().translation.allowFreeServices);
        QCOMPARE(loaded.current().translation.fallbackService, QStringLiteral("mozhi"));
        QCOMPARE(loaded.current().translation.maxLength, 5000);
        QVERIFY(loaded.current().proofreading.enabled);
        QVERIFY(loaded.current().proofreading.allowOnlineServices);
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
        QVERIFY(store.current().translation.fallbackService.isEmpty());
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
            raw.setValue(
                QStringLiteral("mozhi/instance"),
                QStringLiteral("https://user:secret@example.invalid/?engine=google#fragment"));
            raw.setValue(QStringLiteral("mozhi/engine"), QStringLiteral("unknown-engine"));
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
        QCOMPARE(result.mozhi.instance, QStringLiteral("https://mozhi.adminforge.de"));
        QCOMPARE(result.mozhi.engine, QStringLiteral("google"));
    }

    void fallbackServiceIsNormalized()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));

        {
            QSettings raw(path, QSettings::IniFormat);
            raw.setValue(QStringLiteral("settingsVersion"), 1);
            raw.setValue(QStringLiteral("translation/fallbackService"),
                         QStringLiteral("  Mozhi "));
            raw.sync();
        }

        SettingsStore store(path, fakeCodec());
        store.load();
        QCOMPARE(store.current().settingsVersion, 1);
        QCOMPARE(store.current().translation.fallbackService, QStringLiteral("mozhi"));

        // An unknown id and a non-opt-in service are reset to none. The warning
        // is fixed and must not echo the typed value.
        const QString warning =
            QStringLiteral("Fallback service is not a supported opt-in service; using none");
        for (const QString &value :
             {QStringLiteral("nope"), QStringLiteral("google"), QStringLiteral("deepl")}) {
            QVERIFY(!warning.contains(value));
            {
                QSettings raw(path, QSettings::IniFormat);
                raw.setValue(QStringLiteral("translation/fallbackService"), value);
                raw.sync();
            }

            SettingsStore loaded(path, fakeCodec());
            QTest::ignoreMessage(QtWarningMsg, qPrintable(warning));
            loaded.load();
            QCOMPARE(loaded.current().translation.fallbackService, QString());
        }
    }

    void mozhiGroupIsOptional()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("qtranslate.ini"));
        {
            QSettings raw(path, QSettings::IniFormat);
            raw.setValue(QStringLiteral("settingsVersion"), 1);
            raw.setValue(QStringLiteral("app/startMinimized"), false);
            raw.sync();
        }
        const QByteArray before = readFile(path);
        QVERIFY(!before.isEmpty());
        QVERIFY(!before.contains("[mozhi]"));

        SettingsStore store(path, fakeCodec());
        store.load();
        QCOMPARE(store.current().settingsVersion, 1);
        QCOMPARE(store.current().general.startMinimized, false);
        QCOMPARE(store.current().mozhi.instance,
                 QStringLiteral("https://mozhi.adminforge.de"));
        QCOMPARE(store.current().mozhi.engine, QStringLiteral("google"));
        // Loading an old file must not rewrite it.
        QCOMPARE(readFile(path), before);
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
