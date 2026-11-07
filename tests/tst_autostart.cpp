#include "platform/autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTest>

#if defined(Q_OS_LINUX)
#  include <QFileInfo>
#  include <QTemporaryDir>

#  include <memory>
#endif

class TestAutostart : public QObject
{
    Q_OBJECT

private slots:
#if defined(Q_OS_LINUX)
    void init()
    {
        m_oldConfigHome = qgetenv("XDG_CONFIG_HOME");
        m_configHomeWasSet = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
        m_configHome = std::make_unique<QTemporaryDir>();
        QVERIFY(m_configHome->isValid());
        qputenv("XDG_CONFIG_HOME", m_configHome->path().toUtf8());
    }

    void cleanup()
    {
        if (m_configHomeWasSet)
            qputenv("XDG_CONFIG_HOME", m_oldConfigHome);
        else
            qunsetenv("XDG_CONFIG_HOME");
        m_configHome.reset();
    }
#endif

    void commandQuotesThePath()
    {
        const QString path =
            QDir::toNativeSeparators(QStringLiteral("C:/Program Files/qtranslate/qtranslate.exe"));
        QCOMPARE(Autostart::commandForExecutable(path),
                 QStringLiteral("\"%1\" --minimized").arg(path));
    }

    void commandUsesTheGivenPath()
    {
        const QString path = QDir::toNativeSeparators(QStringLiteral("C:/tools/qtranslate.exe"));
        QVERIFY(Autostart::commandForExecutable(path).startsWith(QLatin1Char('"')));
        QVERIFY(Autostart::commandForExecutable(path).endsWith(QStringLiteral(" --minimized")));
    }

#if defined(Q_OS_LINUX)
    void commandUsesDesktopEntryQuoting()
    {
        const QString path =
            QStringLiteral("/opt/qtranslate dir/q\"uote\\back`tick $HOME 100%/qtranslate");
        QCOMPARE(Autostart::commandForExecutable(path),
                 QStringLiteral("\"/opt/qtranslate dir/q\\\"uote\\\\back\\`tick "
                                "\\$HOME 100%%/qtranslate\" --minimized"));
    }

    void writesReadsAndRemovesDesktopFile()
    {
        const QString configHome = qEnvironmentVariable("XDG_CONFIG_HOME");
        QVERIFY(!configHome.isEmpty());
        const QString path = QDir(configHome)
                                 .filePath(QStringLiteral("autostart/qtranslate.desktop"));

        QVERIFY(!Autostart::isEnabled());
        QVERIFY(Autostart::setEnabled(true));
        QVERIFY(Autostart::isEnabled());
        QVERIFY(Autostart::isApproved());

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(file.readAll());
        file.close();

        const QString command =
            Autostart::commandForExecutable(QCoreApplication::applicationFilePath());
        QVERIFY(content.startsWith(QStringLiteral("[Desktop Entry]\n")));
        QVERIFY(content.contains(QStringLiteral("Type=Application\n")));
        QVERIFY(content.contains(QStringLiteral("Name=QTranslate\n")));
        QVERIFY(content.contains(QStringLiteral("Exec=%1\n").arg(command)));
        QVERIFY(content.contains(QStringLiteral("Icon=qtranslate\n")));
        QVERIFY(content.contains(QStringLiteral("Terminal=false\n")));
        QVERIFY(content.contains(QStringLiteral("X-GNOME-Autostart-enabled=true\n")));
        QVERIFY(content.endsWith(QLatin1Char('\n')));

        // sync(true) repairs a moved executable path.
        QString broken = content;
        broken.replace(command, QStringLiteral("\"/moved/qtranslate\" --minimized"));
        QVERIFY(broken != content);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(broken.toUtf8()), qint64(broken.toUtf8().size()));
        file.close();

        QVERIFY(Autostart::sync(true));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString repaired = QString::fromUtf8(file.readAll());
        file.close();
        QVERIFY(repaired.contains(QStringLiteral("Exec=%1\n").arg(command)));
        QVERIFY(!repaired.contains(QStringLiteral("/moved/qtranslate")));

        // The requested state already holds, so sync(true) changes nothing.
        QVERIFY(Autostart::sync(true));

        QVERIFY(Autostart::setEnabled(false));
        QVERIFY(!Autostart::isEnabled());
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(Autostart::sync(false));
    }

private:
    QByteArray m_oldConfigHome;
    bool m_configHomeWasSet = false;
    std::unique_ptr<QTemporaryDir> m_configHome;
#endif
};

QTEST_GUILESS_MAIN(TestAutostart)
#include "tst_autostart.moc"
