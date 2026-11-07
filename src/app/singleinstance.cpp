#include "app/singleinstance.h"

#include "app/logging.h"
#include "core/appinfo.h"
#include "platform/windowactivation.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QThread>

namespace {

constexpr int kConnectTimeoutMs = 200;
constexpr int kWriteTimeoutMs = 500;
// How long a secondary instance keeps trying to reach a primary that holds
// the lock but has not started its server yet.
constexpr int kPrimaryWaitMs = 1000;

const QByteArray kActivateMessage = QByteArrayLiteral("activate\n");
const QByteArray kNoopMessage = QByteArrayLiteral("noop\n");
const QByteArray kTranslatePopupMessage = QByteArrayLiteral("translate-popup\n");
const QByteArray kTranslateMainMessage = QByteArrayLiteral("translate-main\n");
const QByteArray kTranslateInplaceMessage = QByteArrayLiteral("translate-inplace\n");

QByteArray messageFor(SingleInstance::Command command)
{
    switch (command) {
    case SingleInstance::Command::Activate:
        return kActivateMessage;
    case SingleInstance::Command::TranslatePopup:
        return kTranslatePopupMessage;
    case SingleInstance::Command::TranslateMain:
        return kTranslateMainMessage;
    case SingleInstance::Command::TranslateInplace:
        return kTranslateInplaceMessage;
    case SingleInstance::Command::None:
        break;
    }
    return kNoopMessage;
}

SingleInstance::Command commandForMessage(const QByteArray &line)
{
    if (line == kActivateMessage)
        return SingleInstance::Command::Activate;
    if (line == kTranslatePopupMessage)
        return SingleInstance::Command::TranslatePopup;
    if (line == kTranslateMainMessage)
        return SingleInstance::Command::TranslateMain;
    if (line == kTranslateInplaceMessage)
        return SingleInstance::Command::TranslateInplace;
    // "noop" and unknown lines are ignored.
    return SingleInstance::Command::None;
}

QString sanitize(const QString &value)
{
    QString result;
    result.reserve(value.size());
    for (const QChar ch : value)
        result.append(ch.isLetterOrNumber() || ch == QLatin1Char('-') ? ch : QLatin1Char('_'));
    return result;
}

} // namespace

SingleInstance::SingleInstance(const QString &key, QObject *parent)
    : QObject(parent)
    , m_serverName(key)
    , m_lock(QDir::temp().absoluteFilePath(key + QStringLiteral(".lock")))
{
    // Keep the default stale lock time (30 s). On Windows QLockFile keeps the
    // file open without FILE_SHARE_DELETE, so a live primary's lock cannot be
    // removed however old it is, while an empty or corrupt lock left by a
    // power loss is cleared after 30 s.
    qRegisterMetaType<SingleInstance::Command>("SingleInstance::Command");
}

SingleInstance::~SingleInstance() = default;

QString SingleInstance::defaultKey()
{
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty())
        user = qEnvironmentVariable("USER");
    return AppInfo::name() + QLatin1Char('-') + sanitize(user);
}

bool SingleInstance::tryBecomePrimary(Command command)
{
    if (!m_lock.tryLock(0)) {
        if (m_lock.error() == QLockFile::LockFailedError) {
            qCInfo(lcApp) << "Another instance is running, forwarding command";
            if (!sendToPrimary(command))
                qCWarning(lcApp) << "Could not reach the running instance";
            return false;
        }
        // Permission or other unexpected error: better to run than to refuse.
        qCWarning(lcApp) << "Single-instance lock unavailable, error" << m_lock.error();
    }

    m_server = std::make_unique<QLocalServer>();
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(m_serverName); // stale socket file on Unix; no-op on Windows
    if (!m_server->listen(m_serverName)) {
        qCWarning(lcApp) << "Single-instance server failed:" << m_server->errorString();
        return true; // still primary, just without activation from other instances
    }
    connect(m_server.get(), &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
    return true;
}

bool SingleInstance::sendToPrimary(Command command)
{
    // The user just launched us, so we own the foreground right; pass it on so
    // the primary can bring its window to the front.
    if (command == Command::Activate)
        Platform::allowAnyProcessToActivate();

    const QByteArray message = messageFor(command);
    const QDeadlineTimer deadline(kPrimaryWaitMs);

    do {
        QLocalSocket socket;
        socket.connectToServer(m_serverName);
        if (socket.waitForConnected(kConnectTimeoutMs)) {
            socket.write(message);
            const bool written = socket.waitForBytesWritten(kWriteTimeoutMs);
            socket.disconnectFromServer();
            if (socket.state() != QLocalSocket::UnconnectedState)
                socket.waitForDisconnected(kWriteTimeoutMs);
            return written;
        }
        QThread::msleep(50);
    } while (!deadline.hasExpired());

    return false;
}

void SingleInstance::onNewConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            while (socket->canReadLine()) {
                const QByteArray line = socket->readLine();
                const Command command = commandForMessage(line);
                if (command == Command::None)
                    continue;
                qCInfo(lcApp) << "Received command" << line.trimmed();
                emit commandRequested(command);
            }
        });
    }
}
