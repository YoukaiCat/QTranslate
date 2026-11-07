// libsecret pulls in GLib/GIO, which has a struct member named "signals" that
// collides with Qt's keyword macro. Include libsecret before any Qt header.
#if defined(QTRANSLATE_HAVE_LIBSECRET)
#  include <libsecret/secret.h>
#endif

#include "platform/secretprotector.h"

#include "core/apppaths.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QUuid>

#if defined(QTRANSLATE_HAVE_LIBSECRET)
#  include <QDBusConnection>
#  include <QDBusConnectionInterface>
#  include <QDBusMessage>
#endif

Q_LOGGING_CATEGORY(lcSecret, "qtranslate.secret", QtWarningMsg)

namespace {

constexpr auto kFallbackPrefix = "f:";

QString fallbackDirectory()
{
    return QDir(AppPaths::configDirectory()).filePath(QStringLiteral("secrets"));
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// Returns the UUID after a "<prefix>:" token, or an empty string for a
// malformed token. Only our own token format is accepted; this also keeps a
// crafted INI value from escaping the secrets directory.
QString tokenId(const QByteArray &protectedData, char prefix)
{
    if (protectedData.size() != 2 + 36 || protectedData.at(0) != prefix
        || protectedData.at(1) != ':') {
        return {};
    }
    const QString id = QString::fromLatin1(protectedData.mid(2));
    if (QUuid::fromString(id).toString(QUuid::WithoutBraces) != id)
        return {};
    return id;
}

QByteArray storeFallback(const QString &plainText)
{
    const QString directory = fallbackDirectory();
    if (!QDir().mkpath(directory)) {
        qCWarning(lcSecret) << "Cannot create the secrets directory" << directory;
        return {};
    }
    // mkpath() applies the umask; make the directory owner-only explicitly.
    if (!QFile::setPermissions(directory, QFileDevice::ReadOwner
                                            | QFileDevice::WriteOwner
                                            | QFileDevice::ExeOwner)) {
        qCWarning(lcSecret) << "Cannot restrict the secrets directory" << directory;
        return {};
    }

    const QString id = newId();
    const QString path = QDir(directory).filePath(id);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate,
                   QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        qCWarning(lcSecret) << "Cannot write the secret file" << path;
        return {};
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray data = plainText.toUtf8();
    if (file.write(data) != data.size() || !file.flush()) {
        qCWarning(lcSecret) << "Cannot write the secret file" << path;
        file.remove();
        return {};
    }
    file.close();
    return QByteArray(kFallbackPrefix) + id.toLatin1();
}

QString loadFallback(const QString &id)
{
    const QString path = QDir(fallbackDirectory()).filePath(id);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcSecret) << "Cannot read the secret file" << path;
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

#if defined(QTRANSLATE_HAVE_LIBSECRET)

constexpr auto kLibsecretPrefix = "l:";

// Set when a libsecret call fails: later protect() calls then go straight to
// the fallback instead of retrying a keyring that already failed.
bool g_libsecretFailed = false;

const SecretSchema *secretSchema()
{
    // A plain aggregate initializer leaves the private fields of SecretSchema
    // uninitialized and warns under -Wextra, so fill the public part here.
    static const SecretSchema kSchema = [] {
        SecretSchema schema = {};
        schema.name = "org.qtranslate.Secret";
        schema.flags = SECRET_SCHEMA_NONE;
        schema.attributes[0] = {"application", SECRET_SCHEMA_ATTRIBUTE_STRING};
        schema.attributes[1] = {"id", SECRET_SCHEMA_ATTRIBUTE_STRING};
        schema.attributes[2] = {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING};
        return schema;
    }();
    return &kSchema;
}

void logGError(const char *operation, GError *error)
{
    if (error) {
        qCWarning(lcSecret) << operation << "failed:" << error->message;
        g_error_free(error);
    } else {
        qCWarning(lcSecret) << operation << "failed";
    }
}

bool secretServiceAvailable()
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface())
        return false;
    if (bus.interface()->isServiceRegistered(QStringLiteral("org.freedesktop.secrets")))
        return true;

    // The bus daemon answers ListActivatableNames itself; unlike a Secret
    // Service call it does not start the keyring or prompt to unlock it.
    const QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ListActivatableNames"));
    const QDBusMessage reply = bus.call(message, QDBus::Block, 1000);
    if (reply.type() != QDBusMessage::ReplyMessage)
        return false;
    return reply.arguments().value(0).toStringList().contains(
        QStringLiteral("org.freedesktop.secrets"));
}

QByteArray storeLibsecret(const QString &plainText)
{
    const QString id = newId();
    const QByteArray idUtf8 = id.toUtf8();
    const QByteArray password = plainText.toUtf8();
    GError *error = nullptr;
    const gboolean stored = secret_password_store_sync(
        secretSchema(), SECRET_COLLECTION_DEFAULT, "QTranslate secret",
        password.constData(), nullptr, &error, "application", "qtranslate", "id",
        idUtf8.constData(), nullptr);
    if (!stored) {
        logGError("secret_password_store_sync", error);
        g_libsecretFailed = true;
        return {};
    }
    return QByteArray(kLibsecretPrefix) + idUtf8;
}

QString loadLibsecret(const QString &id)
{
    const QByteArray idUtf8 = id.toUtf8();
    GError *error = nullptr;
    gchar *password = secret_password_lookup_sync(secretSchema(), nullptr, &error,
                                                  "application", "qtranslate", "id",
                                                  idUtf8.constData(), nullptr);
    if (!password) {
        logGError("secret_password_lookup_sync", error);
        g_libsecretFailed = true;
        return {};
    }
    const QString plainText = QString::fromUtf8(password);
    secret_password_free(password);
    return plainText;
}

#endif // QTRANSLATE_HAVE_LIBSECRET

} // namespace

namespace SecretProtector {

QByteArray protect(const QString &plainText)
{
    if (plainText.isEmpty())
        return {};

#if defined(QTRANSLATE_HAVE_LIBSECRET)
    if (isAvailable()) {
        const QByteArray token = storeLibsecret(plainText);
        if (!token.isEmpty())
            return token;
        // The keyring failed mid-session; fall through to the fallback that
        // the settings dialog already warns about.
    }
#endif
    return storeFallback(plainText);
}

QString unprotect(const QByteArray &protectedData)
{
    if (protectedData.isEmpty())
        return {};

#if defined(QTRANSLATE_HAVE_LIBSECRET)
    const QString libsecretId = tokenId(protectedData, 'l');
    if (!libsecretId.isEmpty())
        return loadLibsecret(libsecretId);
#endif

    const QString fallbackId = tokenId(protectedData, 'f');
    if (!fallbackId.isEmpty())
        return loadFallback(fallbackId);

    qCWarning(lcSecret) << "Ignoring a protected secret with an unknown format";
    return {};
}

bool canReuse(const QByteArray &protectedData)
{
    if (!tokenId(protectedData, 'l').isEmpty()) {
#if defined(QTRANSLATE_HAVE_LIBSECRET)
        // A transient keyring outage must not downgrade an encrypted token to
        // the plaintext fallback on an unrelated save.
        return true;
#else
        // The current build has no libsecret, so an l: token cannot be read.
        return false;
#endif
    }
    if (!tokenId(protectedData, 'f').isEmpty()) {
        // Keep the fallback while it is the active backend, and migrate it to
        // libsecret once the keyring is available.
        return !isAvailable();
    }
    return false;
}

bool isAvailable()
{
#if defined(QTRANSLATE_HAVE_LIBSECRET)
    if (qEnvironmentVariableIsSet("QTRANSLATE_SECRET_FALLBACK"))
        return false;
    if (g_libsecretFailed)
        return false;
    return secretServiceAvailable();
#else
    return false;
#endif
}

} // namespace SecretProtector
