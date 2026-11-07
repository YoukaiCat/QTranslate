#include "core/apppaths.h"
#include "platform/secretprotector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#if defined(Q_OS_LINUX)
#  include <QUuid>
#  include <memory>
#  include <sys/stat.h>
#endif

namespace {

#if defined(Q_OS_LINUX)
// Saves and restores an environment variable even when a QSKIP or a failed
// assertion ends the test function early.
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

    void unset() { qunsetenv(m_name); }

private:
    const char *m_name;
    QByteArray m_value;
    bool m_wasSet;
};
#endif

} // namespace

class TestSecretProtector : public QObject
{
    Q_OBJECT

private slots:
#if defined(Q_OS_LINUX)
    void initTestCase()
    {
        m_oldConfigHome = qgetenv("XDG_CONFIG_HOME");
        m_configHomeWasSet = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
        m_configHome = std::make_unique<QTemporaryDir>();
        QVERIFY(m_configHome->isValid());
        qputenv("XDG_CONFIG_HOME", m_configHome->path().toUtf8());
    }

    void cleanupTestCase()
    {
        if (m_configHomeWasSet)
            qputenv("XDG_CONFIG_HOME", m_oldConfigHome);
        else
            qunsetenv("XDG_CONFIG_HOME");
        m_configHome.reset();
    }
#endif

    void roundTrip()
    {
        const QString plainText =
            QStringLiteral("api-key: \u043f\u0440\u0438\u0432\u0435\u0442 12345");
        const QByteArray protectedData = SecretProtector::protect(plainText);
        QVERIFY(!protectedData.isEmpty());
        QVERIFY(!protectedData.contains(plainText.toUtf8()));
        QCOMPARE(SecretProtector::unprotect(protectedData), plainText);
    }

    void emptyText()
    {
        QVERIFY(SecretProtector::protect(QString()).isEmpty());
        QVERIFY(SecretProtector::unprotect(QByteArray()).isEmpty());
    }

    void corruptedData()
    {
        QVERIFY(SecretProtector::unprotect(QByteArrayLiteral("not dpapi data")).isEmpty());
    }

#if defined(Q_OS_LINUX)
    void fallbackRoundTrip()
    {
        EnvironmentGuard hook("QTRANSLATE_SECRET_FALLBACK");
        qputenv("QTRANSLATE_SECRET_FALLBACK", "1");

        const QString plainText =
            QStringLiteral("fallback-secret \u043f\u0440\u0438\u0432\u0435\u0442 42");
        const QByteArray token = SecretProtector::protect(plainText);
        QVERIFY(token.startsWith(QByteArrayLiteral("f:")));
        QVERIFY(!token.contains(plainText.toUtf8()));
        QVERIFY(!SecretProtector::isAvailable());
        QVERIFY(SecretProtector::canReuse(token));
        QCOMPARE(SecretProtector::unprotect(token), plainText);

        const QString uuid = QString::fromLatin1(token.mid(2));
        const QString directory =
            QDir(AppPaths::configDirectory()).filePath(QStringLiteral("secrets"));
        const QString path = QDir(directory).filePath(uuid);
        QVERIFY(QFileInfo::exists(path));
        struct stat fileStat = {};
        QVERIFY(::stat(QFile::encodeName(path).constData(), &fileStat) == 0);
        QCOMPARE(fileStat.st_mode & 0777u, 0600u);
        struct stat directoryStat = {};
        QVERIFY(::stat(QFile::encodeName(directory).constData(), &directoryStat) == 0);
        QCOMPARE(directoryStat.st_mode & 0777u, 0700u);

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), plainText.toUtf8());
        file.close();

        // A missing or malformed fallback token never reads another secret.
        QVERIFY(SecretProtector::unprotect(QByteArrayLiteral("f:not-a-uuid")).isEmpty());
        QVERIFY(SecretProtector::unprotect(
                    QByteArrayLiteral("f:00000000-0000-0000-0000-000000000000"))
                    .isEmpty());
    }

    void libsecretRoundTrip()
    {
        EnvironmentGuard hook("QTRANSLATE_SECRET_FALLBACK");
        hook.unset();

#if defined(QTRANSLATE_HAVE_LIBSECRET)
        if (!SecretProtector::isAvailable())
            QSKIP("The org.freedesktop.secrets service is not registered or activatable");
        const QString plainText =
            QStringLiteral("libsecret-secret \u043f\u0440\u0438\u0432\u0435\u0442");
        const QByteArray token = SecretProtector::protect(plainText);
        if (!token.startsWith(QByteArrayLiteral("l:")))
            QSKIP("libsecret did not store the secret in the keyring");
        QVERIFY(SecretProtector::canReuse(token));
        QCOMPARE(SecretProtector::unprotect(token), plainText);
#else
        QSKIP("libsecret support is not compiled in");
#endif
    }

    void canReuseFollowsTheBackend()
    {
        QVERIFY(!SecretProtector::canReuse(QByteArray()));
        QVERIFY(!SecretProtector::canReuse(QByteArrayLiteral("not a token")));

        const QByteArray fallbackToken =
            QByteArrayLiteral("f:")
            + QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1();
        QCOMPARE(SecretProtector::canReuse(fallbackToken),
                 !SecretProtector::isAvailable());

        const QByteArray libsecretToken =
            QByteArrayLiteral("l:")
            + QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1();
#if defined(QTRANSLATE_HAVE_LIBSECRET)
        QVERIFY(SecretProtector::canReuse(libsecretToken));
#else
        QVERIFY(!SecretProtector::canReuse(libsecretToken));
#endif
    }

private:
    QByteArray m_oldConfigHome;
    bool m_configHomeWasSet = false;
    std::unique_ptr<QTemporaryDir> m_configHome;
#endif
};

QTEST_GUILESS_MAIN(TestSecretProtector)
#include "tst_secretprotector.moc"
