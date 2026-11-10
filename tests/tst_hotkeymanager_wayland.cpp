#include "platform/hotkeymanager.h"
#include "platform/portalshortcut.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

#include <functional>

namespace {

// Restores an environment variable on scope exit, including early QSKIP
// returns.
class EnvironmentGuard
{
public:
    explicit EnvironmentGuard(const char *name)
        : m_name(name)
        , m_wasSet(qEnvironmentVariableIsSet(name))
        , m_value(qgetenv(name))
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
    bool m_wasSet;
    QByteArray m_value;
};

QString senderToken()
{
    QString sender = QDBusConnection::sessionBus().baseService();
    if (sender.startsWith(QLatin1Char(':')))
        sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return sender;
}

QString requestPathForToken(const QString &handleToken)
{
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
        .arg(senderToken(), handleToken);
}

constexpr auto kPortalService = QLatin1String("org.freedesktop.portal.Desktop");
constexpr auto kPortalPath = QLatin1String("/org/freedesktop/portal/desktop");

} // namespace

// One org.freedesktop.portal.Request object per portal request. The response is
// queued with a zero-timer so the method reply reaches the client first.
class MockRequest : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")

public:
    explicit MockRequest(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

signals:
    void Response(uint response, const QVariantMap &results);
};

class MockSession;

class MockPortal : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.GlobalShortcuts")
    Q_PROPERTY(uint version READ version)

public:
    enum class BindMode { Success, Cancel, Error, Timeout, DropShowMainWindow };

    explicit MockPortal(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    uint version() const { return m_version; }
    void setVersion(uint version) { m_version = version; }
    void setBindMode(BindMode mode) { m_bindMode = mode; }
    void setCreateDelay(int ms) { m_createDelayMs = ms; }
    void setCreateResponse(uint response) { m_createResponse = response; }

    bool start(QString *error);
    void stop();
    void reset();
    void unregisterInterface();
    bool isStarted() const { return m_started; }

    void noteSessionClosed(const QString &sessionHandle)
    {
        ++closeCalls;
        lastClosedSession = sessionHandle;
    }

    int createCalls = 0;
    int bindCalls = 0;
    int closeCalls = 0;
    QVariantMap lastCreateOptions;
    QList<PortalShortcut> lastShortcuts;
    QString lastParentWindow;
    QVariantMap lastBindOptions;
    QString lastSessionHandle;
    QString lastClosedSession;
    QStringList sessionPaths;
    // Runs inside the mock's CreateSession slot, before the response is queued.
    std::function<void()> onCreateSession;

    void emitActivated(const QString &sessionHandle, const QString &shortcutId,
                       qulonglong timestamp = 0);
    void emitDeactivated(const QString &sessionHandle, const QString &shortcutId,
                         qulonglong timestamp = 0);
    void emitShortcutsChanged(const QString &sessionHandle,
                              const QList<PortalShortcut> &shortcuts);

public slots:
    QDBusObjectPath CreateSession(const QVariantMap &options);
    QDBusObjectPath BindShortcuts(const QDBusObjectPath &sessionHandle,
                                  const QList<PortalShortcut> &shortcuts,
                                  const QString &parentWindow,
                                  const QVariantMap &options);

signals:
    void Activated(const QDBusObjectPath &sessionHandle, const QString &shortcutId,
                   qulonglong timestamp, const QVariantMap &options);
    void Deactivated(const QDBusObjectPath &sessionHandle, const QString &shortcutId,
                     qulonglong timestamp, const QVariantMap &options);
    void ShortcutsChanged(const QDBusObjectPath &sessionHandle,
                          const QList<PortalShortcut> &shortcuts);

private:
    void registerRequest(const QString &path, uint response, const QVariantMap &results,
                         int delayMs = 0);
    void registerSession(const QString &path);
    void clearObjects();

    uint m_version = 2;
    BindMode m_bindMode = BindMode::Success;
    int m_createDelayMs = 0;
    uint m_createResponse = 0;
    bool m_started = false;
    QStringList m_requestPaths;
    QStringList m_sessionPaths;
    QList<MockRequest *> m_requests;
    QList<MockSession *> m_sessions;
};

class MockSession : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Session")

public:
    MockSession(MockPortal *portal, const QString &path, QObject *parent = nullptr)
        : QObject(parent)
        , m_portal(portal)
        , m_path(path)
    {
    }

public slots:
    void Close()
    {
        m_portal->noteSessionClosed(m_path);
        emit Closed();
    }

signals:
    void Closed();

private:
    MockPortal *m_portal;
    QString m_path;
};

bool MockPortal::start(QString *error)
{
    if (m_started)
        return true;

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        *error = QStringLiteral("The private D-Bus session bus is not connected.");
        return false;
    }
    if (!bus.registerService(QString(kPortalService))) {
        *error = QStringLiteral("Cannot register %1: %2")
                     .arg(QString(kPortalService), bus.lastError().message());
        return false;
    }
    if (!bus.registerObject(QString(kPortalPath), this, QDBusConnection::ExportAllContents)) {
        *error = QStringLiteral("Cannot register the mock portal object: %1")
                     .arg(bus.lastError().message());
        bus.unregisterService(QString(kPortalService));
        return false;
    }
    m_started = true;
    return true;
}

void MockPortal::stop()
{
    if (!m_started)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    for (const QString &path : std::as_const(m_requestPaths))
        bus.unregisterObject(path);
    for (const QString &path : std::as_const(m_sessionPaths))
        bus.unregisterObject(path);
    bus.unregisterObject(QString(kPortalPath));
    bus.unregisterService(QString(kPortalService));
    clearObjects();
    m_started = false;
}

void MockPortal::reset()
{
    stop();
    createCalls = 0;
    bindCalls = 0;
    closeCalls = 0;
    lastCreateOptions.clear();
    lastShortcuts.clear();
    lastParentWindow.clear();
    lastBindOptions.clear();
    lastSessionHandle.clear();
    lastClosedSession.clear();
    sessionPaths.clear();
    onCreateSession = {};
    m_version = 2;
    m_bindMode = BindMode::Success;
    m_createDelayMs = 0;
    m_createResponse = 0;
}

void MockPortal::unregisterInterface()
{
    if (!m_started)
        return;
    QDBusConnection::sessionBus().unregisterObject(QString(kPortalPath));
}

void MockPortal::clearObjects()
{
    qDeleteAll(m_requests);
    m_requests.clear();
    m_requestPaths.clear();
    qDeleteAll(m_sessions);
    m_sessions.clear();
    m_sessionPaths.clear();
}

void MockPortal::registerRequest(const QString &path, uint response,
                                 const QVariantMap &results, int delayMs)
{
    auto *request = new MockRequest(this);
    if (!QDBusConnection::sessionBus().registerObject(
            path, request, QDBusConnection::ExportAllContents)) {
        qWarning() << "mock portal: cannot register the request object" << path;
        delete request;
        return;
    }
    m_requestPaths.append(path);
    m_requests.append(request);
    QTimer::singleShot(delayMs > 0 ? delayMs : 0, request,
                       [request, response, results] {
                           emit request->Response(response, results);
                       });
}

void MockPortal::registerSession(const QString &path)
{
    auto *session = new MockSession(this, path, this);
    if (!QDBusConnection::sessionBus().registerObject(
            path, session, QDBusConnection::ExportAllContents)) {
        qWarning() << "mock portal: cannot register the session object" << path;
        delete session;
        return;
    }
    m_sessionPaths.append(path);
    m_sessions.append(session);
}

QDBusObjectPath MockPortal::CreateSession(const QVariantMap &options)
{
    ++createCalls;
    lastCreateOptions = options;
    if (onCreateSession)
        onCreateSession();

    const QString handleToken = options.value(QStringLiteral("handle_token")).toString();
    const QString sessionToken =
        options.value(QStringLiteral("session_handle_token")).toString();
    const QString path = requestPathForToken(handleToken);
    QVariantMap results;
    if (m_createResponse == 0) {
        const QString sessionPath =
            QStringLiteral("/org/freedesktop/portal/desktop/session/%1/%2")
                .arg(senderToken(), sessionToken);
        lastSessionHandle = sessionPath;
        sessionPaths.append(sessionPath);
        registerSession(sessionPath);
        results.insert(QStringLiteral("session_handle"), sessionPath);
    }
    registerRequest(path, m_createResponse, results, m_createDelayMs);
    return QDBusObjectPath(path);
}

QDBusObjectPath MockPortal::BindShortcuts(const QDBusObjectPath &sessionHandle,
                                          const QList<PortalShortcut> &shortcuts,
                                          const QString &parentWindow,
                                          const QVariantMap &options)
{
    ++bindCalls;
    lastSessionHandle = sessionHandle.path();
    lastShortcuts = shortcuts;
    lastParentWindow = parentWindow;
    lastBindOptions = options;
    const QString path = requestPathForToken(options.value(QStringLiteral("handle_token")).toString());

    uint response = 0;
    QList<PortalShortcut> returned = shortcuts;
    QVariantMap results;
    switch (m_bindMode) {
    case BindMode::Success:
        break;
    case BindMode::Cancel:
        response = 1;
        break;
    case BindMode::Error:
        response = 2;
        break;
    case BindMode::Timeout:
        // No Request object response: the client has to time out.
        return QDBusObjectPath(path);
    case BindMode::DropShowMainWindow:
        for (int i = returned.size() - 1; i >= 0; --i) {
            if (returned.at(i).id == QLatin1String("show-main-window"))
                returned.removeAt(i);
        }
        break;
    }

    if (response == 0) {
        for (PortalShortcut &shortcut : returned) {
            if (!shortcut.options.contains(QStringLiteral("trigger_description"))) {
                shortcut.options.insert(
                    QStringLiteral("trigger_description"),
                    shortcut.options.value(QStringLiteral("preferred_trigger")).toString());
            }
        }
        results.insert(QStringLiteral("shortcuts"), QVariant::fromValue(returned));
    }
    registerRequest(path, response, results);
    return QDBusObjectPath(path);
}

void MockPortal::emitActivated(const QString &sessionHandle, const QString &shortcutId,
                               qulonglong timestamp)
{
    emit Activated(QDBusObjectPath(sessionHandle), shortcutId, timestamp, QVariantMap());
}

void MockPortal::emitDeactivated(const QString &sessionHandle, const QString &shortcutId,
                                 qulonglong timestamp)
{
    emit Deactivated(QDBusObjectPath(sessionHandle), shortcutId, timestamp, QVariantMap());
}

void MockPortal::emitShortcutsChanged(const QString &sessionHandle,
                                      const QList<PortalShortcut> &shortcuts)
{
    emit ShortcutsChanged(QDBusObjectPath(sessionHandle), shortcuts);
}

class TestHotkeyManagerWayland : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void missingPortalIsReported();
    void missingInterfaceIsReported();
    void oldVersionIsReported();
    void createSessionErrorIsReportedOncePerAction();
    void happyPathBindsAllActions();
    void bindCancelIsReported();
    void bindErrorIsReported();
    void bindTimeoutIsReported();
    void bindSubsetIsReported();
    void shortcutsChangedReportsRemoval();
    void unregisterAllClosesSession();
    void unregisterAllDuringCreateSessionIsDeferred();
    void destructorClosesSession();
    void sameSequenceRepeatIsNoOp();
    void changedSequenceRebinds();
    void unregisterAndRegisterCoalesce();
    void suspensionKeepsSession();
    void staleSessionIsIgnored();

private:
    QProcess *m_dbus = nullptr;
    MockPortal *m_portal = nullptr;
    bool m_hadWaylandDisplay = false;
    QByteArray m_oldWaylandDisplay;
    bool m_hadSessionType = false;
    QByteArray m_oldSessionType;
    bool m_hadDisplay = false;
    QByteArray m_oldDisplay;
    bool m_hadBusAddress = false;
    QByteArray m_oldBusAddress;
};

void TestHotkeyManagerWayland::initTestCase()
{
    m_hadWaylandDisplay = qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
    m_oldWaylandDisplay = qgetenv("WAYLAND_DISPLAY");
    m_hadSessionType = qEnvironmentVariableIsSet("XDG_SESSION_TYPE");
    m_oldSessionType = qgetenv("XDG_SESSION_TYPE");
    m_hadDisplay = qEnvironmentVariableIsSet("DISPLAY");
    m_oldDisplay = qgetenv("DISPLAY");
    m_hadBusAddress = qEnvironmentVariableIsSet("DBUS_SESSION_BUS_ADDRESS");
    m_oldBusAddress = qgetenv("DBUS_SESSION_BUS_ADDRESS");

    qputenv("WAYLAND_DISPLAY", "wayland-test");
    qputenv("XDG_SESSION_TYPE", "wayland");
    // An unused display: if the backend tried to open it, the reason would be a
    // display error instead of the portal one.
    qputenv("DISPLAY", ":77");

    const QString daemon = QStandardPaths::findExecutable(QStringLiteral("dbus-daemon"));
    if (daemon.isEmpty()) {
        QSKIP("dbus-daemon is not installed; the mock GlobalShortcuts portal needs "
              "a private session bus.");
    }

    m_dbus = new QProcess(this);
    m_dbus->start(daemon, {QStringLiteral("--session"), QStringLiteral("--nofork"),
                           QStringLiteral("--print-address=1")});
    // Only a missing executable may skip: a present but broken daemon means a
    // broken test environment and must fail the suite.
    QVERIFY2(m_dbus->waitForStarted(5000),
             qPrintable(QStringLiteral("Cannot start dbus-daemon: %1")
                            .arg(m_dbus->errorString())));

    QByteArray address;
    while (!address.contains('\n')) {
        if (!m_dbus->waitForReadyRead(5000)) {
            QFAIL(qPrintable(QStringLiteral("dbus-daemon did not print a session address: %1")
                                 .arg(m_dbus->errorString())));
        }
        address += m_dbus->readAllStandardOutput();
    }
    address = address.left(address.indexOf('\n')).trimmed();
    QVERIFY2(!address.isEmpty(), "dbus-daemon printed an empty session address.");

    // QDBusConnection::sessionBus() caches the connection and its address, so
    // the private bus name must be set before the first use.
    qputenv("DBUS_SESSION_BUS_ADDRESS", address);
    QVERIFY2(QDBusConnection::sessionBus().isConnected(),
             "Cannot connect to the private D-Bus session bus.");

    m_portal = new MockPortal(this);
}

void TestHotkeyManagerWayland::cleanupTestCase()
{
    if (m_portal)
        m_portal->stop();
    QDBusConnection::disconnectFromBus(QStringLiteral("qt_default_session_bus"));
    if (m_dbus) {
        m_dbus->terminate();
        if (!m_dbus->waitForFinished(3000))
            m_dbus->kill();
    }

    if (m_hadWaylandDisplay)
        qputenv("WAYLAND_DISPLAY", m_oldWaylandDisplay);
    else
        qunsetenv("WAYLAND_DISPLAY");
    if (m_hadSessionType)
        qputenv("XDG_SESSION_TYPE", m_oldSessionType);
    else
        qunsetenv("XDG_SESSION_TYPE");
    if (m_hadDisplay)
        qputenv("DISPLAY", m_oldDisplay);
    else
        qunsetenv("DISPLAY");
    if (m_hadBusAddress)
        qputenv("DBUS_SESSION_BUS_ADDRESS", m_oldBusAddress);
    else
        qunsetenv("DBUS_SESSION_BUS_ADDRESS");
}

void TestHotkeyManagerWayland::init()
{
    if (m_portal)
        m_portal->reset();
}

namespace {

struct ActionCase
{
    HotkeyAction action;
    QKeySequence sequence;
    QString id;
    QString trigger;
};

QList<ActionCase> actionCases()
{
    return {
        {HotkeyAction::TranslateToPopup, QKeySequence(Qt::CTRL | Qt::Key_Q),
         QStringLiteral("translate-popup"), QStringLiteral("CTRL+q")},
        {HotkeyAction::TranslateAndReplace,
         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q), QStringLiteral("translate-replace"),
         QStringLiteral("CTRL+SHIFT+q")},
        {HotkeyAction::TranslateToMainWindow, QKeySequence(Qt::CTRL | Qt::Key_W),
         QStringLiteral("translate-main-window"), QStringLiteral("CTRL+w")},
        {HotkeyAction::ShowMainWindow,
         QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F10),
         QStringLiteral("show-main-window"), QStringLiteral("CTRL+ALT+SHIFT+F10")},
        {HotkeyAction::ProofreadSelection,
         QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F11),
         QStringLiteral("proofread-selection"), QStringLiteral("CTRL+ALT+SHIFT+F11")},
    };
}

bool startPortal(MockPortal *portal)
{
    QString error;
    return portal->start(&error);
}

bool registerAll(HotkeyManager *manager)
{
    for (const ActionCase &testCase : actionCases()) {
        if (!manager->registerHotkey(testCase.action, testCase.sequence))
            return false;
    }
    return true;
}

} // namespace

void TestHotkeyManagerWayland::missingPortalIsReported()
{
    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    const QKeySequence sequence(Qt::CTRL | Qt::Key_Q);

    QVERIFY(!manager.registerHotkey(HotkeyAction::TranslateToPopup, sequence));
    QCOMPARE(failures.count(), 1);
    QCOMPARE(manager.hotkey(HotkeyAction::TranslateToPopup), sequence);

    const QString reason = failures.first().at(2).toString();
    QVERIFY2(reason.contains(QLatin1String("portal"), Qt::CaseInsensitive),
             qPrintable(reason));
    QVERIFY2(reason.contains(QLatin1String("CLI")), qPrintable(reason));
    QCOMPARE(m_portal->createCalls, 0);
}

void TestHotkeyManagerWayland::missingInterfaceIsReported()
{
    QVERIFY(startPortal(m_portal));
    m_portal->unregisterInterface();

    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    QVERIFY(!manager.registerHotkey(HotkeyAction::TranslateToPopup,
                                    QKeySequence(Qt::CTRL | Qt::Key_Q)));
    QCOMPARE(failures.count(), 1);
    const QString reason = failures.first().at(2).toString();
    QVERIFY2(reason.contains(QLatin1String("GlobalShortcuts"), Qt::CaseInsensitive),
             qPrintable(reason));
    QVERIFY2(reason.contains(QLatin1String("portal"), Qt::CaseInsensitive),
             qPrintable(reason));
    QCOMPARE(m_portal->createCalls, 0);
}

void TestHotkeyManagerWayland::oldVersionIsReported()
{
    QVERIFY(startPortal(m_portal));
    m_portal->setVersion(0);

    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    QVERIFY(!manager.registerHotkey(HotkeyAction::TranslateToPopup,
                                    QKeySequence(Qt::CTRL | Qt::Key_Q)));
    QCOMPARE(failures.count(), 1);
    const QString reason = failures.first().at(2).toString();
    QVERIFY2(reason.contains(QLatin1String("version"), Qt::CaseInsensitive),
             qPrintable(reason));
    QCOMPARE(m_portal->createCalls, 0);
}

void TestHotkeyManagerWayland::createSessionErrorIsReportedOncePerAction()
{
    QVERIFY(startPortal(m_portal));
    m_portal->setCreateResponse(2);

    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);

    QSet<int> failedActions;
    int expected = 0;
    for (const ActionCase &testCase : actionCases()) {
        QVERIFY(!manager.registerHotkey(testCase.action, testCase.sequence));
        ++expected;
        // One signal per call: a first registration has no previous binding to
        // report in addition to the caller's own failure.
        QCOMPARE(failures.count(), expected);
        failedActions.insert(int(failures.last().at(0).value<HotkeyAction>()));
        QVERIFY(!failures.last().at(2).toString().isEmpty());
    }
    QCOMPARE(failures.count(), 5);
    QCOMPARE(failedActions.size(), 5);
    QVERIFY2(failures.first().at(2).toString().contains(QLatin1String("response 2")),
             qPrintable(failures.first().at(2).toString()));
    QCOMPARE(m_portal->createCalls, 5);
    QCOMPARE(m_portal->bindCalls, 0);
    QCOMPARE(m_portal->closeCalls, 0);
    for (const ActionCase &testCase : actionCases())
        QCOMPARE(manager.hotkey(testCase.action), testCase.sequence);
}

void TestHotkeyManagerWayland::happyPathBindsAllActions()
{
    QVERIFY(startPortal(m_portal));

    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    QVERIFY(registerAll(&manager));

    // The bind is asynchronous: immediately after the five calls the sequences
    // are still reported.
    for (const ActionCase &testCase : actionCases())
        QCOMPARE(manager.hotkey(testCase.action), testCase.sequence);

    QTRY_COMPARE_WITH_TIMEOUT(m_portal->createCalls, 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(m_portal->bindCalls, 1, 10000);
    QCOMPARE(m_portal->sessionPaths.size(), 1);
    QCOMPARE(m_portal->lastShortcuts.size(), 5);
    QCOMPARE(m_portal->lastParentWindow, QString());
    QVERIFY(m_portal->lastCreateOptions.contains(QStringLiteral("handle_token")));
    QVERIFY(m_portal->lastCreateOptions.contains(QStringLiteral("session_handle_token")));
    QVERIFY(m_portal->lastBindOptions.contains(QStringLiteral("handle_token")));

    QMap<QString, QString> triggers;
    for (const PortalShortcut &shortcut : std::as_const(m_portal->lastShortcuts)) {
        QVERIFY2(!shortcut.options.value(QStringLiteral("description")).toString().isEmpty(),
                 qPrintable(shortcut.id));
        triggers.insert(shortcut.id,
                        shortcut.options.value(QStringLiteral("preferred_trigger")).toString());
    }
    for (const ActionCase &testCase : actionCases()) {
        QVERIFY2(triggers.contains(testCase.id), qPrintable(testCase.id));
        QCOMPARE(triggers.value(testCase.id), testCase.trigger);
    }

    // The desktop assigned the triggers, so the human-readable sequence is no
    // longer reported.
    for (const ActionCase &testCase : actionCases())
        QTRY_VERIFY_WITH_TIMEOUT(manager.hotkey(testCase.action).isEmpty(), 10000);
    QCOMPARE(failures.count(), 0);

    QSignalSpy activations(&manager, &HotkeyManager::activated);
    m_portal->emitActivated(m_portal->sessionPaths.first(), QStringLiteral("translate-popup"),
                            42);
    QTRY_COMPARE_WITH_TIMEOUT(activations.count(), 1, 5000);
    QCOMPARE(activations.first().at(0).value<HotkeyAction>(),
             HotkeyAction::TranslateToPopup);

    m_portal->emitDeactivated(m_portal->sessionPaths.first(),
                              QStringLiteral("translate-popup"), 43);
    QTest::qWait(100);
    QCOMPARE(activations.count(), 1);
}

void TestHotkeyManagerWayland::bindCancelIsReported()
{
    QVERIFY(startPortal(m_portal));
    m_portal->setBindMode(MockPortal::BindMode::Cancel);

    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    QVERIFY(registerAll(&manager));

    QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 5, 10000);
    QCOMPARE(m_portal->bindCalls, 1);
    QSet<int> failedActions;
    for (const QList<QVariant> &failure : std::as_const(failures))
        failedActions.insert(int(failure.at(0).value<HotkeyAction>()));
    for (const ActionCase &testCase : actionCases())
        QVERIFY(failedActions.contains(int(testCase.action)));
    for (const ActionCase &testCase : actionCases())
        QCOMPARE(manager.hotkey(testCase.action), testCase.sequence);
    QVERIFY(m_portal->closeCalls >= 1);
}

void TestHotkeyManagerWayland::bindErrorIsReported()
{
    QVERIFY(startPortal(m_portal));
    m_portal->setBindMode(MockPortal::BindMode::Error);

    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    QVERIFY(registerAll(&manager));

    QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 5, 10000);
    QCOMPARE(m_portal->bindCalls, 1);
    for (const ActionCase &testCase : actionCases())
        QCOMPARE(manager.hotkey(testCase.action), testCase.sequence);
    QVERIFY(m_portal->closeCalls >= 1);
}

void TestHotkeyManagerWayland::bindTimeoutIsReported()
{
    EnvironmentGuard timeoutOverride("QTRANSLATE_TEST_BIND_TIMEOUT_MS");
    qputenv("QTRANSLATE_TEST_BIND_TIMEOUT_MS", "300");
    QVERIFY(startPortal(m_portal));
    m_portal->setBindMode(MockPortal::BindMode::Timeout);

    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    QVERIFY(registerAll(&manager));

    QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 5, 5000);
    QCOMPARE(m_portal->bindCalls, 1);
    for (const ActionCase &testCase : actionCases())
        QCOMPARE(manager.hotkey(testCase.action), testCase.sequence);
    QVERIFY(m_portal->closeCalls >= 1);
}

void TestHotkeyManagerWayland::bindSubsetIsReported()
{
    QVERIFY(startPortal(m_portal));
    m_portal->setBindMode(MockPortal::BindMode::DropShowMainWindow);

    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    QVERIFY(registerAll(&manager));

    QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 10000);
    QCOMPARE(failures.first().at(0).value<HotkeyAction>(), HotkeyAction::ShowMainWindow);
    QVERIFY(failures.first().at(2).toString().contains(QLatin1String("bind"),
                                                       Qt::CaseInsensitive));
    QCOMPARE(manager.hotkey(HotkeyAction::ShowMainWindow),
             QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F10));
    QVERIFY(manager.hotkey(HotkeyAction::TranslateToPopup).isEmpty());
    QCOMPARE(m_portal->closeCalls, 0);
}

void TestHotkeyManagerWayland::shortcutsChangedReportsRemoval()
{
    QVERIFY(startPortal(m_portal));

    HotkeyManager manager;
    QVERIFY(registerAll(&manager));
    QTRY_COMPARE_WITH_TIMEOUT(m_portal->bindCalls, 1, 10000);
    for (const ActionCase &testCase : actionCases())
        QTRY_VERIFY_WITH_TIMEOUT(manager.hotkey(testCase.action).isEmpty(), 10000);

    QList<PortalShortcut> changed = m_portal->lastShortcuts;
    for (int i = changed.size() - 1; i >= 0; --i) {
        if (changed.at(i).id == QLatin1String("show-main-window"))
            changed.removeAt(i);
    }
    QCOMPARE(changed.size(), 4);

    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    m_portal->emitShortcutsChanged(m_portal->sessionPaths.first(), changed);
    QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 5000);
    QCOMPARE(failures.first().at(0).value<HotkeyAction>(), HotkeyAction::ShowMainWindow);
    QVERIFY2(failures.first().at(2).toString().contains(QLatin1String("removed"),
                                                         Qt::CaseInsensitive),
             qPrintable(failures.first().at(2).toString()));
    QCOMPARE(manager.hotkey(HotkeyAction::ShowMainWindow),
             QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F10));
    QVERIFY(manager.hotkey(HotkeyAction::TranslateToPopup).isEmpty());
}

void TestHotkeyManagerWayland::unregisterAllClosesSession()
{
    QVERIFY(startPortal(m_portal));

    HotkeyManager manager;
    QVERIFY(registerAll(&manager));
    QTRY_COMPARE_WITH_TIMEOUT(m_portal->bindCalls, 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(manager.hotkey(HotkeyAction::TranslateToPopup).isEmpty(), 10000);

    const QString session = m_portal->sessionPaths.first();
    const int closesBefore = m_portal->closeCalls;
    manager.unregisterAll();
    QCOMPARE(m_portal->closeCalls, closesBefore + 1);
    QCOMPARE(m_portal->lastClosedSession, session);
    QVERIFY(manager.hotkey(HotkeyAction::TranslateToPopup).isEmpty());
}

void TestHotkeyManagerWayland::unregisterAllDuringCreateSessionIsDeferred()
{
    QVERIFY(startPortal(m_portal));
    // Keep CreateSession's response in flight so the timer below runs inside
    // createSession()'s nested event loop.
    m_portal->setCreateDelay(400);

    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    QSignalSpy activations(&manager, &HotkeyManager::activated);
    m_portal->onCreateSession = [&manager] {
        QTimer::singleShot(0, &manager, [&manager] { manager.unregisterAll(); });
    };

    QVERIFY(manager.registerHotkey(HotkeyAction::TranslateToPopup,
                                   QKeySequence(Qt::CTRL | Qt::Key_Q)));

    // The reentrant unregisterAll() could not reset the state under the
    // waiting caller; the created session is closed exactly once afterwards.
    QCOMPARE(m_portal->createCalls, 1);
    QCOMPARE(m_portal->sessionPaths.size(), 1);
    QCOMPARE(m_portal->closeCalls, 1);
    QCOMPARE(m_portal->lastClosedSession, m_portal->sessionPaths.first());
    QCOMPARE(m_portal->bindCalls, 0);
    QVERIFY(manager.hotkey(HotkeyAction::TranslateToPopup).isEmpty());
    QCOMPARE(failures.count(), 0);

    QTest::qWait(100);
    QCOMPARE(failures.count(), 0);
    QCOMPARE(activations.count(), 0);
    QCOMPARE(m_portal->closeCalls, 1);
    m_portal->onCreateSession = {};
}

void TestHotkeyManagerWayland::destructorClosesSession()
{
    QVERIFY(startPortal(m_portal));
    QString session;
    int closesBefore = 0;
    {
        HotkeyManager manager;
        QVERIFY(registerAll(&manager));
        QTRY_COMPARE_WITH_TIMEOUT(m_portal->bindCalls, 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(manager.hotkey(HotkeyAction::TranslateToPopup).isEmpty(),
                                 10000);
        session = m_portal->sessionPaths.first();
        closesBefore = m_portal->closeCalls;
    }
    QCOMPARE(m_portal->closeCalls, closesBefore + 1);
    QCOMPARE(m_portal->lastClosedSession, session);
}

void TestHotkeyManagerWayland::sameSequenceRepeatIsNoOp()
{
    QVERIFY(startPortal(m_portal));

    HotkeyManager manager;
    QVERIFY(registerAll(&manager));
    QTRY_COMPARE_WITH_TIMEOUT(m_portal->bindCalls, 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(manager.hotkey(HotkeyAction::TranslateToPopup).isEmpty(), 10000);

    const int createCalls = m_portal->createCalls;
    const int bindCalls = m_portal->bindCalls;
    const int closeCalls = m_portal->closeCalls;
    QVERIFY(manager.registerHotkey(HotkeyAction::TranslateToPopup,
                                   QKeySequence(Qt::CTRL | Qt::Key_Q)));
    QTest::qWait(100);
    QCOMPARE(m_portal->createCalls, createCalls);
    QCOMPARE(m_portal->bindCalls, bindCalls);
    QCOMPARE(m_portal->closeCalls, closeCalls);
    QVERIFY(manager.hotkey(HotkeyAction::TranslateToPopup).isEmpty());
}

void TestHotkeyManagerWayland::changedSequenceRebinds()
{
    QVERIFY(startPortal(m_portal));

    HotkeyManager manager;
    QVERIFY(registerAll(&manager));
    QTRY_COMPARE_WITH_TIMEOUT(m_portal->bindCalls, 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(manager.hotkey(HotkeyAction::TranslateToPopup).isEmpty(), 10000);
    QCOMPARE(m_portal->sessionPaths.size(), 1);

    const QString oldSession = m_portal->sessionPaths.first();
    const QKeySequence newSequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F9);
    QVERIFY(manager.registerHotkey(HotkeyAction::TranslateToMainWindow, newSequence));

    QTRY_COMPARE_WITH_TIMEOUT(m_portal->createCalls, 2, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(m_portal->bindCalls, 2, 10000);
    QCOMPARE(m_portal->sessionPaths.size(), 2);
    QVERIFY(m_portal->sessionPaths.at(1) != oldSession);
    QCOMPARE(m_portal->lastSessionHandle, m_portal->sessionPaths.at(1));
    QVERIFY2(m_portal->lastClosedSession == oldSession,
             qPrintable(m_portal->lastClosedSession));
    QCOMPARE(m_portal->lastShortcuts.size(), 5);

    QMap<QString, QString> triggers;
    for (const PortalShortcut &shortcut : std::as_const(m_portal->lastShortcuts))
        triggers.insert(shortcut.id,
                        shortcut.options.value(QStringLiteral("preferred_trigger")).toString());
    QCOMPARE(triggers.value(QStringLiteral("translate-main-window")),
             QStringLiteral("CTRL+ALT+SHIFT+F9"));
    QTRY_VERIFY_WITH_TIMEOUT(manager.hotkey(HotkeyAction::TranslateToMainWindow).isEmpty(),
                             10000);
}

void TestHotkeyManagerWayland::unregisterAndRegisterCoalesce()
{
    QVERIFY(startPortal(m_portal));

    HotkeyManager manager;
    QVERIFY(registerAll(&manager));
    QTRY_COMPARE_WITH_TIMEOUT(m_portal->bindCalls, 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(manager.hotkey(HotkeyAction::TranslateToPopup).isEmpty(), 10000);

    const QKeySequence newSequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F11);
    manager.unregisterHotkey(HotkeyAction::TranslateToMainWindow);
    QVERIFY(manager.registerHotkey(HotkeyAction::TranslateToMainWindow, newSequence));

    QTRY_COMPARE_WITH_TIMEOUT(m_portal->createCalls, 2, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(m_portal->bindCalls, 2, 10000);
    QCOMPARE(m_portal->sessionPaths.size(), 2);
    QCOMPARE(m_portal->lastShortcuts.size(), 5);

    QMap<QString, QString> triggers;
    for (const PortalShortcut &shortcut : std::as_const(m_portal->lastShortcuts))
        triggers.insert(shortcut.id,
                        shortcut.options.value(QStringLiteral("preferred_trigger")).toString());
    QCOMPARE(triggers.value(QStringLiteral("translate-main-window")),
             QStringLiteral("CTRL+ALT+SHIFT+F11"));
}

void TestHotkeyManagerWayland::suspensionKeepsSession()
{
    QVERIFY(startPortal(m_portal));

    HotkeyManager manager;
    QVERIFY(registerAll(&manager));
    QTRY_COMPARE_WITH_TIMEOUT(m_portal->bindCalls, 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(manager.hotkey(HotkeyAction::TranslateToPopup).isEmpty(), 10000);

    const int createCalls = m_portal->createCalls;
    const int bindCalls = m_portal->bindCalls;
    const int closeCalls = m_portal->closeCalls;

    QSignalSpy activations(&manager, &HotkeyManager::activated);
    manager.setSuspended(true);
    QVERIFY(manager.isSuspended());
    QTest::qWait(100);
    m_portal->emitActivated(m_portal->sessionPaths.first(), QStringLiteral("translate-popup"));
    QTest::qWait(100);
    QCOMPARE(activations.count(), 0);
    QCOMPARE(m_portal->closeCalls, closeCalls);
    QCOMPARE(m_portal->createCalls, createCalls);
    QCOMPARE(m_portal->bindCalls, bindCalls);

    manager.setSuspended(false);
    QVERIFY(!manager.isSuspended());
    QTest::qWait(100);
    // The portal session and its bindings were kept; resume does not rebind.
    QCOMPARE(m_portal->createCalls, createCalls);
    QCOMPARE(m_portal->bindCalls, bindCalls);

    m_portal->emitActivated(m_portal->sessionPaths.first(), QStringLiteral("translate-popup"));
    QTRY_COMPARE_WITH_TIMEOUT(activations.count(), 1, 5000);
}

void TestHotkeyManagerWayland::staleSessionIsIgnored()
{
    QVERIFY(startPortal(m_portal));

    HotkeyManager manager;
    QVERIFY(registerAll(&manager));
    QTRY_COMPARE_WITH_TIMEOUT(m_portal->bindCalls, 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(manager.hotkey(HotkeyAction::TranslateToPopup).isEmpty(), 10000);
    const QString oldSession = m_portal->sessionPaths.first();

    QVERIFY(manager.registerHotkey(HotkeyAction::TranslateToMainWindow,
                                   QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F9)));
    QTRY_COMPARE_WITH_TIMEOUT(m_portal->bindCalls, 2, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(m_portal->sessionPaths.size(), 2, 10000);
    const QString currentSession = m_portal->sessionPaths.at(1);

    QSignalSpy activations(&manager, &HotkeyManager::activated);
    m_portal->emitActivated(oldSession, QStringLiteral("translate-popup"));
    QTest::qWait(100);
    QCOMPARE(activations.count(), 0);

    m_portal->emitActivated(currentSession, QStringLiteral("translate-popup"));
    QTRY_COMPARE_WITH_TIMEOUT(activations.count(), 1, 5000);
}

QTEST_GUILESS_MAIN(TestHotkeyManagerWayland)
#include "tst_hotkeymanager_wayland.moc"
