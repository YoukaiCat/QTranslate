#include "platform/hotkeymanager.h"
#include "platform/portalshortcut.h"
#include "platform/sessiontype.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPair>
#include <QSet>
#include <QSocketNotifier>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUuid>
#include <QVariantMap>

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

Q_LOGGING_CATEGORY(lcHotkey, "qtranslate.hotkey", QtWarningMsg)

namespace {

// Xlib has one process-global error handler and it cannot carry user data, so
// the state checked by the scoped handler lives here. Every Xlib call stays on
// the manager's thread, and the handler is installed only around a grab batch.
int *g_x11ErrorCode = nullptr;

int x11ErrorHandler(Display *, XErrorEvent *event)
{
    if (g_x11ErrorCode && *g_x11ErrorCode == 0)
        *g_x11ErrorCode = event->error_code;
    return 0;
}

// Caps Lock and Num Lock must not break a hotkey, so each combination is
// grabbed four times: as is and with either lock mask added.
constexpr unsigned int kLockVariants[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};

// GlobalShortcuts portal names. The interface is versioned; version 1 is the
// first release that supports BindShortcuts.
constexpr auto kPortalService = QLatin1String("org.freedesktop.portal.Desktop");
constexpr auto kPortalPath = QLatin1String("/org/freedesktop/portal/desktop");
constexpr auto kGlobalShortcutsInterface =
    QLatin1String("org.freedesktop.portal.GlobalShortcuts");
constexpr auto kRequestInterface = QLatin1String("org.freedesktop.portal.Request");
constexpr auto kSessionInterface = QLatin1String("org.freedesktop.portal.Session");
constexpr auto kPropertiesInterface = QLatin1String("org.freedesktop.DBus.Properties");

// Stable IDs and English descriptions. The user-visible descriptions are not
// translated here: adding them to the HotkeyManager context would make the
// translation test require all 14 .ts files, which is a separate i18n pass.
struct ShortcutInfo
{
    QString id;
    QString description;
};

const ShortcutInfo &shortcutInfo(HotkeyAction action)
{
    static const ShortcutInfo shortcuts[] = {
        {QStringLiteral("translate-popup"),
         QStringLiteral("Translate selected text in a popup window")},
        {QStringLiteral("translate-replace"),
         QStringLiteral("Translate selected text and replace it")},
        {QStringLiteral("translate-main-window"),
         QStringLiteral("Translate selected text in the main window")},
        {QStringLiteral("show-main-window"), QStringLiteral("Show the main window")},
    };
    return shortcuts[int(action)];
}

bool actionForShortcutId(const QString &id, HotkeyAction *action)
{
    for (int i = 0; i <= int(HotkeyAction::ShowMainWindow); ++i) {
        if (shortcutInfo(HotkeyAction(i)).id == id) {
            *action = HotkeyAction(i);
            return true;
        }
    }
    return false;
}

bool mapSequence(const QKeySequence &sequence, KeySym *keysym, quint32 *modifiers,
                 QString *error);

// Convert the stored QKeySequence to the XDG shortcuts-spec preferred_trigger
// string: modifiers in CTRL, ALT, SHIFT, LOGO order, then the xkbcommon keysym
// name. mapSequence() supplies validation, the keysym, and the implicit Shift
// of shifted punctuation, exactly like the X11 grab path.
QString preferredTrigger(const QKeySequence &sequence, QString *error)
{
    KeySym keysym = NoSymbol;
    quint32 mask = 0;
    if (!mapSequence(sequence, &keysym, &mask, error))
        return QString();

    const char *name = XKeysymToString(keysym);
    if (!name) {
        *error = QStringLiteral("The key has no X11 keysym name.");
        return QString();
    }

    QStringList parts;
    if (mask & ControlMask)
        parts << QStringLiteral("CTRL");
    if (mask & Mod1Mask)
        parts << QStringLiteral("ALT");
    if (mask & ShiftMask)
        parts << QStringLiteral("SHIFT");
    if (mask & Mod4Mask)
        parts << QStringLiteral("LOGO");
    parts << QString::fromLatin1(name);
    return parts.join(QLatin1Char('+'));
}

QString senderToken(const QDBusConnection &bus)
{
    QString sender = bus.baseService();
    if (sender.startsWith(QLatin1Char(':')))
        sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return sender;
}

QString requestPathForToken(const QDBusConnection &bus, const QString &handleToken)
{
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
        .arg(senderToken(bus), handleToken);
}

// The portal is allowed to show a confirmation dialog for BindShortcuts, so a
// bind is asynchronous and may wait up to a minute. The mock-portal test needs
// a shorter wait; production always uses the 60 s default.
int bindTimeoutMs()
{
    bool ok = false;
    const int override =
        qEnvironmentVariableIntValue("QTRANSLATE_TEST_BIND_TIMEOUT_MS", &ok);
    return ok && override > 0 ? override : 60 * 1000;
}

void registerPortalTypes()
{
    static const bool registered = [] {
        qDBusRegisterMetaType<PortalShortcut>();
        qDBusRegisterMetaType<QList<PortalShortcut>>();
        return true;
    }();
    Q_UNUSED(registered);
}

QString noPortalReason(const QString &detail)
{
    return QStringLiteral(
               "%1 Use a CLI command or a desktop shortcut assigned in the desktop "
               "settings as the fallback.")
        .arg(detail);
}

// Marks a synchronous portal wait. Its nested event loop can deliver unrelated
// calls; while a guard is alive HotkeyManager::unregisterAll() must not reset
// the state whose member function is on the stack.
class PortalCallGuard
{
public:
    explicit PortalCallGuard(int &depth)
        : m_depth(depth)
    {
        ++m_depth;
    }

    ~PortalCallGuard() { --m_depth; }

    PortalCallGuard(const PortalCallGuard &) = delete;
    PortalCallGuard &operator=(const PortalCallGuard &) = delete;

private:
    int &m_depth;
};

// Check the portal without QDBusInterface introspection: the GUI thread must not
// wait for generated introspection data.
bool checkPortal(QString *reason)
{
    QElapsedTimer elapsed;
    elapsed.start();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        *reason = noPortalReason(QStringLiteral(
            "No D-Bus session bus is available, so the "
            "org.freedesktop.portal.GlobalShortcuts portal is unavailable."));
        return false;
    }

    // isServiceRegistered() has no timeout parameter; explicit calls keep the
    // whole availability check inside one shared 3 s budget.
    QDBusMessage nameHasOwner = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameHasOwner"));
    nameHasOwner << QString(kPortalService);
    const QDBusMessage ownerReply = bus.call(nameHasOwner, QDBus::Block, 3000);
    if (ownerReply.type() != QDBusMessage::ReplyMessage
        || !ownerReply.arguments().value(0).toBool()) {
        *reason = noPortalReason(QStringLiteral(
            "The org.freedesktop.portal.Desktop portal is not available."));
        return false;
    }

    // The second call gets only what the first one left of the 3 s budget.
    const qint64 remaining = 3000 - elapsed.elapsed();
    if (remaining <= 0) {
        *reason = noPortalReason(QStringLiteral(
            "The desktop did not answer the GlobalShortcuts availability check "
            "within 3 seconds."));
        return false;
    }

    QDBusMessage get = QDBusMessage::createMethodCall(
        kPortalService, kPortalPath, kPropertiesInterface, QStringLiteral("Get"));
    get << QString(kGlobalShortcutsInterface) << QStringLiteral("version");
    const QDBusMessage reply = bus.call(get, QDBus::Block, int(remaining));
    if (reply.type() != QDBusMessage::ReplyMessage) {
        *reason = noPortalReason(
            QStringLiteral("The org.freedesktop.portal.GlobalShortcuts interface is "
                           "unavailable: %1 (%2).")
                .arg(reply.errorName(), reply.errorMessage()));
        return false;
    }

    const uint version =
        reply.arguments().value(0).value<QDBusVariant>().variant().toUInt();
    if (version < 1) {
        *reason = noPortalReason(
            QStringLiteral("The org.freedesktop.portal.GlobalShortcuts portal version "
                           "%1 is too old; version 1 is required.")
                .arg(version));
        return false;
    }
    return true;
}

quint32 x11Modifiers(Qt::KeyboardModifiers modifiers)
{
    quint32 mask = 0;
    if (modifiers & Qt::ControlModifier)
        mask |= ControlMask;
    if (modifiers & Qt::ShiftModifier)
        mask |= ShiftMask;
    if (modifiers & Qt::AltModifier)
        mask |= Mod1Mask;
    if (modifiers & Qt::MetaModifier)
        mask |= Mod4Mask;
    // AltGr is reported as Ctrl+Alt (or as GroupSwitch) by Qt on some layouts;
    // X11 has no separate AltGr grab mask, so normalize it like
    // core/hotkeysequence.cpp does for RegisterHotKey on Windows.
    if (modifiers & Qt::GroupSwitchModifier)
        mask |= ControlMask | Mod1Mask;
    return mask;
}

// `implicitShift` reports keys that are typed with Shift on a US layout, e.g.
// Key_Exclam is Shift+1. XGrabKey needs the physical key plus Shift, the same
// way the Windows helper needs the virtual key plus MOD_SHIFT.
KeySym keysymForQtKey(int key, bool keypad, bool *implicitShift)
{
    *implicitShift = false;

    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return XK_a + KeySym(key - Qt::Key_A);
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        if (keypad)
            return XK_KP_0 + KeySym(key - Qt::Key_0);
        return XK_0 + KeySym(key - Qt::Key_0);
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return XK_F1 + KeySym(key - Qt::Key_F1);

    switch (key) {
    case Qt::Key_Escape:
        return XK_Escape;
    case Qt::Key_Tab:
        return XK_Tab;
    case Qt::Key_Backspace:
        return XK_BackSpace;
    case Qt::Key_Return:
        return XK_Return;
    case Qt::Key_Enter:
        return keypad ? XK_KP_Enter : XK_Return;
    case Qt::Key_Insert:
        return XK_Insert;
    case Qt::Key_Delete:
        return XK_Delete;
    case Qt::Key_Home:
        return XK_Home;
    case Qt::Key_End:
        return XK_End;
    case Qt::Key_PageUp:
        return XK_Page_Up;
    case Qt::Key_PageDown:
        return XK_Page_Down;
    case Qt::Key_Left:
        return XK_Left;
    case Qt::Key_Up:
        return XK_Up;
    case Qt::Key_Right:
        return XK_Right;
    case Qt::Key_Down:
        return XK_Down;
    case Qt::Key_Pause:
        return XK_Pause;
    case Qt::Key_Print:
        return XK_Print;
    case Qt::Key_Clear:
        return XK_Clear;
    case Qt::Key_CapsLock:
        return XK_Caps_Lock;
    case Qt::Key_NumLock:
        return XK_Num_Lock;
    case Qt::Key_ScrollLock:
        return XK_Scroll_Lock;
    case Qt::Key_Menu:
        return XK_Menu;
    case Qt::Key_Space:
        return XK_space;

    // Punctuation (US layout, unshifted).
    case Qt::Key_Semicolon:
        return XK_semicolon;
    case Qt::Key_Equal:
        return XK_equal;
    case Qt::Key_Comma:
        return XK_comma;
    case Qt::Key_Minus:
        return keypad ? XK_KP_Subtract : XK_minus;
    case Qt::Key_Period:
        return keypad ? XK_KP_Decimal : XK_period;
    case Qt::Key_Slash:
        return keypad ? XK_KP_Divide : XK_slash;
    case Qt::Key_QuoteLeft:
        return XK_grave;
    case Qt::Key_BracketLeft:
        return XK_bracketleft;
    case Qt::Key_Backslash:
        return XK_backslash;
    case Qt::Key_BracketRight:
        return XK_bracketright;
    case Qt::Key_Apostrophe:
        return XK_apostrophe;

    // Shifted punctuation: the base key plus Shift.
    case Qt::Key_Exclam:
        *implicitShift = true;
        return XK_1;
    case Qt::Key_At:
        *implicitShift = true;
        return XK_2;
    case Qt::Key_NumberSign:
        *implicitShift = true;
        return XK_3;
    case Qt::Key_Dollar:
        *implicitShift = true;
        return XK_4;
    case Qt::Key_Percent:
        *implicitShift = true;
        return XK_5;
    case Qt::Key_AsciiCircum:
        *implicitShift = true;
        return XK_6;
    case Qt::Key_Ampersand:
        *implicitShift = true;
        return XK_7;
    case Qt::Key_Asterisk:
        if (keypad)
            return XK_KP_Multiply;
        *implicitShift = true;
        return XK_8;
    case Qt::Key_ParenLeft:
        *implicitShift = true;
        return XK_9;
    case Qt::Key_ParenRight:
        *implicitShift = true;
        return XK_0;
    case Qt::Key_Underscore:
        *implicitShift = true;
        return XK_minus;
    case Qt::Key_Plus:
        if (keypad)
            return XK_KP_Add;
        *implicitShift = true;
        return XK_equal;
    case Qt::Key_BraceLeft:
        *implicitShift = true;
        return XK_bracketleft;
    case Qt::Key_BraceRight:
        *implicitShift = true;
        return XK_bracketright;
    case Qt::Key_Bar:
        *implicitShift = true;
        return XK_backslash;
    case Qt::Key_Colon:
        *implicitShift = true;
        return XK_semicolon;
    case Qt::Key_QuoteDbl:
        *implicitShift = true;
        return XK_apostrophe;
    case Qt::Key_Less:
        *implicitShift = true;
        return XK_comma;
    case Qt::Key_Greater:
        *implicitShift = true;
        return XK_period;
    case Qt::Key_Question:
        *implicitShift = true;
        return XK_slash;
    case Qt::Key_AsciiTilde:
        *implicitShift = true;
        return XK_grave;
    default:
        break;
    }

    return NoSymbol;
}

// `keysym` may be null when only validation and the modifier mask are needed.
bool mapSequence(const QKeySequence &sequence, KeySym *keysym, quint32 *modifiers,
                 QString *error)
{
    if (sequence.isEmpty()) {
        *error = QStringLiteral("The key sequence is empty.");
        return false;
    }
    if (sequence.count() != 1) {
        *error = QStringLiteral("Only a single key combination is supported.");
        return false;
    }

    const QKeyCombination combination = sequence[0];
    const Qt::Key key = combination.key();
    if (key == Qt::Key(0)) {
        *error = QStringLiteral("Modifier-only combinations are not supported.");
        return false;
    }

    const Qt::KeyboardModifiers qtModifiers = combination.keyboardModifiers();
    const bool keypad = qtModifiers & Qt::KeypadModifier;
    bool implicitShift = false;
    const KeySym symbol = keysymForQtKey(int(key), keypad, &implicitShift);
    if (symbol == NoSymbol) {
        *error = QStringLiteral("The key is not supported as a global hotkey.");
        return false;
    }

    quint32 mask = x11Modifiers(qtModifiers);
    if (implicitShift)
        mask |= ShiftMask;
    if (keysym)
        *keysym = symbol;
    *modifiers = mask;
    return true;
}

KeyCode keycodeForKeysym(Display *display, KeySym keysym)
{
    KeyCode keycode = XKeysymToKeycode(display, keysym);
    // A non-Latin layout can make the lowercase Latin keysym unresolvable; try
    // the uppercase one before giving up. Do not guess a Cyrillic or a
    // second-level keysym: the combination is reported as unavailable instead.
    if (keycode == 0 && keysym >= XK_a && keysym <= XK_z)
        keycode = XKeysymToKeycode(display, keysym - XK_a + XK_A);
    return keycode;
}

} // namespace

struct HotkeyManager::X11State
{
    Display *display = nullptr;
    Window root = 0;
    QSocketNotifier *notifier = nullptr;
    QSet<int> pressedKeys;
    int lastError = 0;

    ~X11State()
    {
        close();
    }

    bool open(HotkeyManager *manager, QString *error)
    {
        if (display)
            return true;

        display = XOpenDisplay(nullptr);
        if (!display) {
            *error = QStringLiteral("Cannot open the X11 display \"%1\".")
                         .arg(QString::fromLocal8Bit(qgetenv("DISPLAY")));
            return false;
        }

        root = DefaultRootWindow(display);
        // Detectable auto-repeat turns a held key into repeated KeyPress events
        // without the synthetic KeyRelease/KeyPress pairs of the classic
        // protocol, so the pressed-key set below can suppress repeats. Servers
        // without XKB fall back to the pairs and may activate a held key more
        // than once; that is the documented difference from MOD_NOREPEAT.
        XkbSetDetectableAutoRepeat(display, True, nullptr);

        notifier = new QSocketNotifier(ConnectionNumber(display), QSocketNotifier::Read,
                                       manager);
        QObject::connect(notifier, &QSocketNotifier::activated, manager,
                         [manager] { manager->m_x11->readEvents(manager); });
        return true;
    }

    void close()
    {
        if (notifier) {
            notifier->disconnect();
            delete notifier;
            notifier = nullptr;
        }
        if (display) {
            XCloseDisplay(display);
            display = nullptr;
        }
        root = 0;
        pressedKeys.clear();
    }

    bool grab(quint32 keycode, quint32 modifiers, QString *error)
    {
        Q_ASSERT(display);
        lastError = 0;
        g_x11ErrorCode = &lastError;
        const XErrorHandler previousHandler = XSetErrorHandler(x11ErrorHandler);
        for (unsigned int extra : kLockVariants) {
            XGrabKey(display, int(keycode), modifiers | extra, root, False,
                     GrabModeAsync, GrabModeAsync);
        }
        XSync(display, False);
        XSetErrorHandler(previousHandler);
        g_x11ErrorCode = nullptr;

        if (lastError != 0) {
            // A partial batch must not stay grabbed.
            ungrab(keycode, modifiers);
            if (lastError == BadAccess) {
                *error = QStringLiteral("The hotkey is already used by another program.");
            } else {
                *error = QStringLiteral("X11 error %1 while grabbing the hotkey.")
                             .arg(lastError);
            }
            return false;
        }
        return true;
    }

    void ungrab(quint32 keycode, quint32 modifiers)
    {
        if (!display || keycode == 0)
            return;
        for (unsigned int extra : kLockVariants)
            XUngrabKey(display, int(keycode), modifiers | extra, root);
        // Flush the releases before another client tries to grab the same
        // combination; a second manager is a separate X connection.
        XSync(display, False);
        pressedKeys.clear();
    }

    void readEvents(HotkeyManager *manager)
    {
        while (display && XPending(display) > 0) {
            XEvent event;
            XNextEvent(display, &event);

            if (event.type == KeyPress) {
                const int keycode = int(event.xkey.keycode);
                if (pressedKeys.contains(keycode))
                    continue; // auto-repeat of a key that is still held

                const quint32 modifiers = event.xkey.state
                    & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);
                for (auto it = manager->m_registrations.cbegin();
                     it != manager->m_registrations.cend(); ++it) {
                    const Registration &registration = it.value();
                    if (!registration.registered
                        || registration.keycode != quint32(keycode)
                        || registration.modifiers != modifiers) {
                        continue;
                    }
                    pressedKeys.insert(keycode);
                    qCDebug(lcHotkey) << "Hotkey activated:" << it.key();
                    emit manager->activated(HotkeyAction(it.key()));
                    break;
                }
            } else if (event.type == KeyRelease) {
                pressedKeys.remove(int(event.xkey.keycode));
            }
        }
    }
};

// The Wayland backend. The class is defined out of line at namespace scope
// because moc rejects Q_OBJECT in nested classes; the forward declaration in the
// header still makes it a member type of HotkeyManager.
struct HotkeyManager::WaylandState : public QObject
{
    Q_OBJECT

public:
    explicit WaylandState(HotkeyManager *manager);
    ~WaylandState() override;

    bool ensurePortal(QString *reason);
    bool registerShortcut(HotkeyAction action, QString *reason);
    void removeShortcut();
    void setSuspended(bool suspended);
    void shutdown();

    bool inPortalCall() const { return m_portalCallDepth > 0; }
    void deferShutdown() { m_shutdownDeferred = true; }
    bool takeDeferredShutdown()
    {
        const bool deferred = m_shutdownDeferred;
        m_shutdownDeferred = false;
        return deferred;
    }

private slots:
    void bindPending();
    void rebuildSession();
    void handleCreateResponse(uint response, const QVariantMap &results);
    void handleBindResponse(uint response, const QVariantMap &results);
    void onBindTimeout();
    void onActivated(const QDBusObjectPath &sessionHandle, const QString &shortcutId,
                     qulonglong timestamp, const QVariantMap &options);
    void onDeactivated(const QDBusObjectPath &sessionHandle, const QString &shortcutId,
                       qulonglong timestamp, const QVariantMap &options);
    void onShortcutsChanged(const QDBusObjectPath &sessionHandle,
                            const QList<PortalShortcut> &shortcuts);

private:
    bool createSession(QString *reason);
    void closeSession();
    void markAllUnbound();
    bool connectSessionSignals(QString *reason);
    void disconnectSessionSignals();
    void disconnectBindResponse();
    void armBind();
    bool hasPendingShortcuts() const;
    void emitUnboundFailures(const QString &reason, int skipAction);
    void failActions(const QList<HotkeyAction> &actions, const QString &reason);

    HotkeyManager *m_manager = nullptr;
    bool m_portalChecked = false;
    bool m_portalAvailable = false;
    QString m_portalReason;

    bool m_sessionOpen = false;
    QDBusObjectPath m_sessionPath;

    // Depth of the synchronous portal waits on the stack. Their nested event
    // loop can deliver unregisterAll(); the teardown is deferred until the
    // member function that is waiting has returned.
    int m_portalCallDepth = 0;
    bool m_shutdownDeferred = false;

    bool m_suspended = false;
    bool m_bindInFlight = false;
    // BindShortcuts may be attempted only once per session.
    bool m_boundOnce = false;
    QList<HotkeyAction> m_bindActions;

    QTimer m_bindTimer;
    QTimer m_bindTimeout;
    QTimer m_rebuildTimer;

    QString m_createRequestPath;
    bool m_createResponseReceived = false;
    uint m_createResponseCode = 2;
    QVariantMap m_createResults;
    QEventLoop *m_createLoop = nullptr;

    QString m_bindRequestPath;
    bool m_activatedConnected = false;
    bool m_deactivatedConnected = false;
    bool m_shortcutsChangedConnected = false;
};

HotkeyManager::WaylandState::WaylandState(HotkeyManager *manager)
    : QObject(nullptr)
    , m_manager(manager)
{
    registerPortalTypes();
    m_bindTimer.setSingleShot(true);
    QObject::connect(&m_bindTimer, &QTimer::timeout, this, &WaylandState::bindPending);
    m_bindTimeout.setSingleShot(true);
    QObject::connect(&m_bindTimeout, &QTimer::timeout, this, &WaylandState::onBindTimeout);
    m_rebuildTimer.setSingleShot(true);
    QObject::connect(&m_rebuildTimer, &QTimer::timeout, this, &WaylandState::rebuildSession);
}

HotkeyManager::WaylandState::~WaylandState()
{
    closeSession();
}

bool HotkeyManager::WaylandState::ensurePortal(QString *reason)
{
    if (!m_portalChecked) {
        QString failure;
        bool available = false;
        {
            PortalCallGuard guard(m_portalCallDepth);
            available = checkPortal(&failure);
        }
        // Publish the cache only after the wait finished: a reentrant call must
        // not observe a half-filled result.
        m_portalChecked = true;
        if (available) {
            m_portalAvailable = true;
        } else {
            m_portalReason = failure;
            // Log the first failure once; later registrations reuse the cached
            // reason without repeating the D-Bus round trips.
            qCWarning(lcHotkey) << "GlobalShortcuts portal unavailable:" << failure;
        }
    }
    if (reason)
        *reason = m_portalReason;
    return m_portalAvailable;
}

bool HotkeyManager::WaylandState::registerShortcut(HotkeyAction action, QString *reason)
{
    // Never keep a Registration reference across the blocking calls below: a
    // nested event loop can deliver unregisterHotkey() or unregisterAll().
    auto current = m_manager->m_registrations.find(int(action));
    if (current == m_manager->m_registrations.end()) {
        *reason = noPortalReason(QStringLiteral(
            "The GlobalShortcuts action is no longer registered."));
        return false;
    }

    // Only shortcuts that were really bound in the session being replaced have
    // to be reported when it cannot be recreated; a first registration has no
    // binding to lose.
    QList<HotkeyAction> boundBefore;
    if (current->registered || m_boundOnce) {
        for (auto it = m_manager->m_registrations.cbegin();
             it != m_manager->m_registrations.cend(); ++it) {
            if (it->registered)
                boundBefore.append(HotkeyAction(it.key()));
        }
        // The portal allows one BindShortcuts per session, and a changed trigger
        // must replace the old one, so a fresh session is required. m_boundOnce
        // is set as soon as the request leaves the process.
        closeSession();
    }
    m_rebuildTimer.stop();

    if (!m_sessionOpen) {
        QString createError;
        if (!createSession(&createError)) {
            // The caller reports the current action; report only the other
            // actions that lost a real binding with the closed session.
            for (HotkeyAction lost : boundBefore) {
                if (lost == action)
                    continue;
                const auto it = m_manager->m_registrations.constFind(int(lost));
                if (it == m_manager->m_registrations.cend() || it->sequence.isEmpty())
                    continue;
                qCWarning(lcHotkey) << "GlobalShortcuts registration failed for"
                                    << shortcutInfo(lost).id << ":" << createError;
                emit m_manager->registrationFailed(lost, it->sequence, createError);
            }
            *reason = createError;
            return false;
        }
    }
    armBind();
    return true;
}

void HotkeyManager::WaylandState::removeShortcut()
{
    if (!m_boundOnce && !m_bindInFlight) {
        // The pending bind reads the registrations when it fires. If nothing
        // remains, the session has no purpose and is closed.
        if (!hasPendingShortcuts() && m_sessionOpen) {
            m_bindTimer.stop();
            closeSession();
        }
        return;
    }

    closeSession();
    if (hasPendingShortcuts() && !m_suspended)
        m_rebuildTimer.start(0);
}

void HotkeyManager::WaylandState::setSuspended(bool suspended)
{
    m_suspended = suspended;
}

void HotkeyManager::WaylandState::shutdown()
{
    closeSession();
}

bool HotkeyManager::WaylandState::createSession(QString *reason)
{
    // The event loop below processes unrelated events; mark the wait so a
    // reentrant unregisterAll() defers instead of deleting this state.
    PortalCallGuard guard(m_portalCallDepth);

    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString id = QUuid::createUuid().toString(QUuid::Id128);
    const QString handleToken = QStringLiteral("qtranslate_%1_create").arg(id);
    const QString sessionToken = QStringLiteral("qtranslate_%1_session").arg(id);
    m_createRequestPath = requestPathForToken(bus, handleToken);
    m_createResponseReceived = false;
    m_createResponseCode = 2;
    m_createResults.clear();

    // Subscribe before the call: a fast portal response must not be lost.
    if (!bus.connect(QString(), m_createRequestPath, QString(kRequestInterface),
                     QStringLiteral("Response"), this,
                     SLOT(handleCreateResponse(uint,QVariantMap)))) {
        *reason = noPortalReason(QStringLiteral(
            "Cannot subscribe to the org.freedesktop.portal.Request response."));
        return false;
    }

    QEventLoop loop;
    m_createLoop = &loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(3000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start();

    QDBusMessage call = QDBusMessage::createMethodCall(
        QString(kPortalService), QString(kPortalPath),
        QString(kGlobalShortcutsInterface), QStringLiteral("CreateSession"));
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), handleToken);
    options.insert(QStringLiteral("session_handle_token"), sessionToken);
    call << options;

    bool methodFinished = false;
    QString requestHandle;
    QDBusError callError;
    QDBusPendingCallWatcher watcher(bus.asyncCall(call, 3000));
    QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop,
                     [&](QDBusPendingCallWatcher *self) {
                         methodFinished = true;
                         const QDBusMessage reply = self->reply();
                         if (reply.type() == QDBusMessage::ErrorMessage) {
                             callError = QDBusError(reply);
                             loop.quit();
                             return;
                         }
                         requestHandle =
                             reply.arguments().value(0).value<QDBusObjectPath>().path();
                         if (m_createResponseReceived)
                             loop.quit();
                         // Otherwise keep waiting for the Request::Response
                         // signal under the same 3 s cap.
                     });
    loop.exec();
    m_createLoop = nullptr;

    bus.disconnect(QString(), m_createRequestPath, QString(kRequestInterface),
                   QStringLiteral("Response"), this,
                   SLOT(handleCreateResponse(uint,QVariantMap)));

    if (!methodFinished) {
        *reason = noPortalReason(QStringLiteral(
            "The desktop did not answer the GlobalShortcuts CreateSession request "
            "within 3 seconds."));
        return false;
    }
    if (callError.isValid()) {
        *reason = noPortalReason(
            QStringLiteral("The GlobalShortcuts CreateSession call failed: %1 (%2).")
                .arg(callError.name(), callError.message()));
        return false;
    }
    if (requestHandle != m_createRequestPath) {
        // The spec requires the token-derived path; a different handle still
        // identifies the request, but the subscription is on the expected one.
        qCWarning(lcHotkey) << "Portal returned request handle" << requestHandle
                            << "instead of" << m_createRequestPath;
    }
    if (!m_createResponseReceived) {
        *reason = noPortalReason(QStringLiteral(
            "The desktop did not send a GlobalShortcuts CreateSession response "
            "within 3 seconds."));
        return false;
    }
    if (m_createResponseCode != 0) {
        *reason = noPortalReason(
            m_createResponseCode == 1
                ? QStringLiteral("The GlobalShortcuts session creation was cancelled.")
                : QStringLiteral(
                      "The GlobalShortcuts session creation failed with response %1.")
                      .arg(m_createResponseCode));
        return false;
    }

    // The portal spec declares session_handle as a string, not an object path;
    // parsing it as QDBusObjectPath fails.
    const QString sessionHandle = m_createResults.value("session_handle").toString();
    if (sessionHandle.isEmpty() || !sessionHandle.startsWith(QLatin1Char('/'))) {
        *reason = noPortalReason(QStringLiteral(
            "The desktop returned an invalid GlobalShortcuts session handle."));
        return false;
    }
    m_sessionPath = QDBusObjectPath(sessionHandle);
    m_sessionOpen = true;
    if (!connectSessionSignals(reason)) {
        // Do not leave an orphaned portal session behind; closeSession() also
        // clears the path and the registration flags.
        closeSession();
        return false;
    }
    qCInfo(lcHotkey) << "GlobalShortcuts session created:" << sessionHandle;
    return true;
}

void HotkeyManager::WaylandState::closeSession()
{
    m_bindTimer.stop();
    m_bindTimeout.stop();
    m_rebuildTimer.stop();
    // Drop the response subscription even when the bind already completed or
    // timed out: a late reply must not reach the next session.
    disconnectBindResponse();
    m_bindInFlight = false;
    m_bindActions.clear();
    m_boundOnce = false;
    disconnectSessionSignals();

    if (m_sessionOpen) {
        // Clear the state before the blocking call: a nested event loop can
        // re-enter closeSession(), and the session must not be closed twice.
        const QDBusObjectPath sessionPath = m_sessionPath;
        m_sessionOpen = false;
        m_sessionPath = QDBusObjectPath();
        // The only other synchronous D-Bus wait besides CreateSession; the
        // shared session bus itself is never closed.
        QDBusMessage close = QDBusMessage::createMethodCall(
            QString(kPortalService), sessionPath.path(), QString(kSessionInterface),
            QStringLiteral("Close"));
        QDBusMessage reply;
        {
            PortalCallGuard guard(m_portalCallDepth);
            reply = QDBusConnection::sessionBus().call(close, QDBus::Block, 1000);
        }
        if (reply.type() != QDBusMessage::ReplyMessage) {
            qCWarning(lcHotkey) << "Cannot close the GlobalShortcuts session:"
                                << reply.errorName() << reply.errorMessage();
        }
    }
    m_sessionPath = QDBusObjectPath();
    markAllUnbound();
}

void HotkeyManager::WaylandState::markAllUnbound()
{
    for (auto it = m_manager->m_registrations.begin();
         it != m_manager->m_registrations.end(); ++it) {
        it->registered = false;
        it->desktopAssigned = false;
    }
}

bool HotkeyManager::WaylandState::connectSessionSignals(QString *reason)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    m_activatedConnected = bus.connect(
        QString(kPortalService), QString(kPortalPath), QString(kGlobalShortcutsInterface),
        QStringLiteral("Activated"), this,
        SLOT(onActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
    m_deactivatedConnected = bus.connect(
        QString(kPortalService), QString(kPortalPath), QString(kGlobalShortcutsInterface),
        QStringLiteral("Deactivated"), this,
        SLOT(onDeactivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
    m_shortcutsChangedConnected = bus.connect(
        QString(kPortalService), QString(kPortalPath), QString(kGlobalShortcutsInterface),
        QStringLiteral("ShortcutsChanged"), this,
        SLOT(onShortcutsChanged(QDBusObjectPath,QList<PortalShortcut>)));
    if (m_activatedConnected && m_deactivatedConnected && m_shortcutsChangedConnected)
        return true;

    disconnectSessionSignals();
    *reason = noPortalReason(QStringLiteral(
        "Cannot subscribe to the org.freedesktop.portal.GlobalShortcuts signals."));
    return false;
}

void HotkeyManager::WaylandState::disconnectSessionSignals()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (m_activatedConnected) {
        bus.disconnect(QString(kPortalService), QString(kPortalPath),
                       QString(kGlobalShortcutsInterface), QStringLiteral("Activated"),
                       this,
                       SLOT(onActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
        m_activatedConnected = false;
    }
    if (m_deactivatedConnected) {
        bus.disconnect(QString(kPortalService), QString(kPortalPath),
                       QString(kGlobalShortcutsInterface), QStringLiteral("Deactivated"),
                       this,
                       SLOT(onDeactivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
        m_deactivatedConnected = false;
    }
    if (m_shortcutsChangedConnected) {
        bus.disconnect(QString(kPortalService), QString(kPortalPath),
                       QString(kGlobalShortcutsInterface),
                       QStringLiteral("ShortcutsChanged"), this,
                       SLOT(onShortcutsChanged(QDBusObjectPath,QList<PortalShortcut>)));
        m_shortcutsChangedConnected = false;
    }
}

void HotkeyManager::WaylandState::disconnectBindResponse()
{
    if (m_bindRequestPath.isEmpty())
        return;
    QDBusConnection::sessionBus().disconnect(
        QString(), m_bindRequestPath, QString(kRequestInterface),
        QStringLiteral("Response"), this,
        SLOT(handleBindResponse(uint,QVariantMap)));
    m_bindRequestPath.clear();
}

void HotkeyManager::WaylandState::armBind()
{
    m_bindTimer.start(0);
}

bool HotkeyManager::WaylandState::hasPendingShortcuts() const
{
    for (auto it = m_manager->m_registrations.cbegin();
         it != m_manager->m_registrations.cend(); ++it) {
        if (!it->sequence.isEmpty())
            return true;
    }
    return false;
}

void HotkeyManager::WaylandState::emitUnboundFailures(const QString &reason, int skipAction)
{
    const QList<int> actions = m_manager->m_registrations.keys();
    for (int key : actions) {
        if (key == skipAction)
            continue;
        auto it = m_manager->m_registrations.find(key);
        if (it == m_manager->m_registrations.end() || it->registered
            || it->sequence.isEmpty()) {
            continue;
        }
        it->desktopAssigned = false;
        qCWarning(lcHotkey) << "GlobalShortcuts registration failed for"
                            << shortcutInfo(HotkeyAction(key)).id << ":" << reason;
        emit m_manager->registrationFailed(HotkeyAction(key), it->sequence, reason);
    }
}

void HotkeyManager::WaylandState::failActions(const QList<HotkeyAction> &actions,
                                              const QString &reason)
{
    for (HotkeyAction action : actions) {
        auto it = m_manager->m_registrations.find(int(action));
        if (it == m_manager->m_registrations.end())
            continue;
        it->registered = false;
        it->desktopAssigned = false;
        qCWarning(lcHotkey) << "GlobalShortcuts registration failed for"
                            << shortcutInfo(action).id << ":" << reason;
        emit m_manager->registrationFailed(action, it->sequence, reason);
    }
}

void HotkeyManager::WaylandState::handleCreateResponse(uint response,
                                                       const QVariantMap &results)
{
    m_createResponseReceived = true;
    m_createResponseCode = response;
    m_createResults = results;
    if (m_createLoop)
        m_createLoop->quit();
}

void HotkeyManager::WaylandState::bindPending()
{
    if (!m_sessionOpen || m_suspended || m_bindInFlight)
        return;

    QDBusConnection bus = QDBusConnection::sessionBus();
    QList<PortalShortcut> shortcuts;
    QList<HotkeyAction> actions;
    QStringList invalidReasons;
    QList<QPair<HotkeyAction, QKeySequence>> invalid;

    for (int i = 0; i <= int(HotkeyAction::ShowMainWindow); ++i) {
        const HotkeyAction action = HotkeyAction(i);
        auto it = m_manager->m_registrations.find(int(action));
        if (it == m_manager->m_registrations.end() || it->sequence.isEmpty()
            || it->registered) {
            continue;
        }

        QString triggerError;
        const QString trigger = preferredTrigger(it->sequence, &triggerError);
        if (trigger.isEmpty()) {
            invalid.append({action, it->sequence});
            invalidReasons.append(triggerError);
            continue;
        }

        PortalShortcut shortcut;
        shortcut.id = shortcutInfo(action).id;
        shortcut.options.insert(QStringLiteral("description"),
                                shortcutInfo(action).description);
        shortcut.options.insert(QStringLiteral("preferred_trigger"), trigger);
        shortcuts.append(shortcut);
        actions.append(action);
    }

    for (int i = 0; i < invalid.size(); ++i)
        failActions({invalid.at(i).first}, invalidReasons.at(i));

    if (shortcuts.isEmpty())
        return;

    const QString token =
        QStringLiteral("qtranslate_%1_bind").arg(QUuid::createUuid().toString(QUuid::Id128));
    m_bindRequestPath = requestPathForToken(bus, token);
    if (!bus.connect(QString(), m_bindRequestPath, QString(kRequestInterface),
                     QStringLiteral("Response"), this,
                     SLOT(handleBindResponse(uint,QVariantMap)))) {
        const QString reason = noPortalReason(
            QStringLiteral("Cannot subscribe to the GlobalShortcuts bind response."));
        closeSession();
        failActions(actions, reason);
        return;
    }

    QDBusMessage call = QDBusMessage::createMethodCall(
        QString(kPortalService), QString(kPortalPath),
        QString(kGlobalShortcutsInterface), QStringLiteral("BindShortcuts"));
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);
    // The tray application has no stable XDG window identifier; the portal
    // permits an unparented dialog, so parent_window stays empty.
    qCDebug(lcHotkey) << "Binding GlobalShortcuts without a parent window";
    call << QVariant::fromValue(m_sessionPath) << QVariant::fromValue(shortcuts)
         << QString() << options;

    m_bindInFlight = true;
    m_boundOnce = true;
    m_bindActions = actions;
    m_bindTimeout.start(bindTimeoutMs());

    const QString bindRequestPath = m_bindRequestPath;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this,
                     [this, bindRequestPath](QDBusPendingCallWatcher *self) {
                         self->deleteLater();
                         // A reply from a cancelled bind must not fail the
                         // shortcuts of the session that replaced it.
                         if (!m_bindInFlight || bindRequestPath != m_bindRequestPath)
                             return;
                         const QDBusMessage reply = self->reply();
                         if (reply.type() == QDBusMessage::ErrorMessage) {
                             const QString reason =
                                 QStringLiteral("The desktop rejected "
                                                "GlobalShortcuts BindShortcuts: %1 (%2).")
                                     .arg(reply.errorName(), reply.errorMessage());
                             const QList<HotkeyAction> failed = m_bindActions;
                             m_bindInFlight = false;
                             m_bindTimeout.stop();
                             disconnectBindResponse();
                             m_bindActions.clear();
                             closeSession();
                             failActions(failed, reason);
                             return;
                         }
                         const QString requestHandle =
                             reply.arguments().value(0).value<QDBusObjectPath>().path();
                         if (requestHandle != m_bindRequestPath) {
                             qCWarning(lcHotkey)
                                 << "Portal returned bind handle" << requestHandle
                                 << "instead of" << m_bindRequestPath;
                         }
                     });
}

void HotkeyManager::WaylandState::rebuildSession()
{
    if (m_suspended || m_sessionOpen || !hasPendingShortcuts())
        return;
    QString reason;
    if (!createSession(&reason)) {
        emitUnboundFailures(reason, -1);
        return;
    }
    armBind();
}

void HotkeyManager::WaylandState::handleBindResponse(uint response,
                                                     const QVariantMap &results)
{
    if (!m_bindInFlight) {
        qCDebug(lcHotkey) << "Ignoring a stale GlobalShortcuts bind response";
        return;
    }
    m_bindInFlight = false;
    m_bindTimeout.stop();
    disconnectBindResponse();

    if (response != 0) {
        const QString reason =
            response == 1
                ? QStringLiteral("The user or the desktop cancelled the "
                                 "GlobalShortcuts BindShortcuts request.")
                : QStringLiteral("The GlobalShortcuts BindShortcuts request failed "
                                 "with response %1.")
                      .arg(response);
        const QList<HotkeyAction> actions = m_bindActions;
        m_bindActions.clear();
        closeSession();
        failActions(actions, reason);
        return;
    }

    const QVariant value = results.value(QStringLiteral("shortcuts"));
    if (!value.isValid()) {
        const QString reason = QStringLiteral(
            "The desktop did not return a GlobalShortcuts shortcut list.");
        const QList<HotkeyAction> actions = m_bindActions;
        m_bindActions.clear();
        closeSession();
        failActions(actions, reason);
        return;
    }

    QSet<QString> requestedIds;
    for (HotkeyAction action : m_bindActions)
        requestedIds.insert(shortcutInfo(action).id);

    QSet<QString> returnedIds;
    // A custom array nested in a{sv} arrives as a QDBusArgument; qdbus_cast
    // also accepts a direct QList<PortalShortcut> value.
    const QList<PortalShortcut> returned = qdbus_cast<QList<PortalShortcut>>(value);
    for (const PortalShortcut &shortcut : returned) {
        HotkeyAction action;
        if (!actionForShortcutId(shortcut.id, &action) || !requestedIds.contains(shortcut.id)) {
            qCWarning(lcHotkey) << "The desktop returned an unexpected GlobalShortcuts shortcut:"
                                << shortcut.id;
            continue;
        }
        auto it = m_manager->m_registrations.find(int(action));
        if (it == m_manager->m_registrations.end() || it->sequence.isEmpty()) {
            qCWarning(lcHotkey) << "The desktop bound a shortcut that is not registered:"
                                << shortcut.id;
            continue;
        }
        returnedIds.insert(shortcut.id);
        it->registered = true;
        it->desktopAssigned = true;
        qCInfo(lcHotkey) << "GlobalShortcuts bound" << shortcut.id
                         << shortcut.options.value(QStringLiteral("description")).toString()
                         << shortcut.options.value(QStringLiteral("trigger_description"))
                                .toString();
    }

    QList<HotkeyAction> missing;
    for (HotkeyAction action : m_bindActions) {
        if (!returnedIds.contains(shortcutInfo(action).id))
            missing.append(action);
    }
    m_bindActions.clear();
    for (HotkeyAction action : missing) {
        failActions({action},
                    QStringLiteral("The desktop did not bind the GlobalShortcuts "
                                   "shortcut \"%1\".")
                        .arg(shortcutInfo(action).id));
    }
}

void HotkeyManager::WaylandState::onBindTimeout()
{
    if (!m_bindInFlight)
        return;
    m_bindInFlight = false;
    const QString reason = QStringLiteral(
        "The desktop did not answer the GlobalShortcuts BindShortcuts request "
        "within %1 seconds.")
        .arg(bindTimeoutMs() / 1000.0, 0, 'g', 3);
    const QList<HotkeyAction> actions = m_bindActions;
    m_bindActions.clear();
    closeSession();
    failActions(actions, reason);
}

void HotkeyManager::WaylandState::onActivated(const QDBusObjectPath &sessionHandle,
                                              const QString &shortcutId,
                                              qulonglong timestamp,
                                              const QVariantMap &options)
{
    Q_UNUSED(options);
    if (!m_sessionOpen || sessionHandle.path() != m_sessionPath.path()) {
        qCDebug(lcHotkey) << "Ignoring Activated from a stale GlobalShortcuts session";
        return;
    }
    if (m_suspended) {
        qCDebug(lcHotkey) << "Ignoring GlobalShortcuts activation while suspended";
        return;
    }
    HotkeyAction action;
    if (!actionForShortcutId(shortcutId, &action)) {
        qCWarning(lcHotkey) << "Unknown GlobalShortcuts shortcut activated:" << shortcutId;
        return;
    }
    const auto it = m_manager->m_registrations.constFind(int(action));
    if (it == m_manager->m_registrations.cend() || !it->registered) {
        qCDebug(lcHotkey) << "Ignoring activation of an unregistered shortcut:"
                          << shortcutId;
        return;
    }
    qCInfo(lcHotkey) << "GlobalShortcuts activated" << shortcutId << timestamp;
    emit m_manager->activated(action);
}

void HotkeyManager::WaylandState::onDeactivated(const QDBusObjectPath &sessionHandle,
                                                const QString &shortcutId,
                                                qulonglong timestamp,
                                                const QVariantMap &options)
{
    Q_UNUSED(options);
    if (!m_sessionOpen || sessionHandle.path() != m_sessionPath.path())
        return;
    // Informational only: the portal owns repeat handling, and deactivation
    // does not change the registration.
    qCDebug(lcHotkey) << "GlobalShortcuts deactivated" << shortcutId << timestamp;
}

void HotkeyManager::WaylandState::onShortcutsChanged(
    const QDBusObjectPath &sessionHandle, const QList<PortalShortcut> &shortcuts)
{
    if (!m_sessionOpen || sessionHandle.path() != m_sessionPath.path()) {
        qCDebug(lcHotkey) << "Ignoring ShortcutsChanged from a stale session";
        return;
    }

    QSet<QString> returnedIds;
    for (const PortalShortcut &shortcut : shortcuts) {
        HotkeyAction action;
        if (!actionForShortcutId(shortcut.id, &action)) {
            qCWarning(lcHotkey) << "Unknown GlobalShortcuts shortcut changed:"
                                << shortcut.id;
            continue;
        }
        returnedIds.insert(shortcut.id);
        auto it = m_manager->m_registrations.find(int(action));
        if (it == m_manager->m_registrations.end() || it->sequence.isEmpty())
            continue;
        it->registered = true;
        it->desktopAssigned = true;
        qCInfo(lcHotkey) << "GlobalShortcuts changed" << shortcut.id
                         << shortcut.options.value(QStringLiteral("description")).toString()
                         << shortcut.options.value(QStringLiteral("trigger_description"))
                                .toString();
    }

    // A previously registered ID that the desktop dropped stays in the
    // registration with its stored sequence but is no longer active.
    QList<QPair<HotkeyAction, QKeySequence>> removed;
    for (int i = 0; i <= int(HotkeyAction::ShowMainWindow); ++i) {
        const HotkeyAction action = HotkeyAction(i);
        const auto it = m_manager->m_registrations.constFind(int(action));
        if (it == m_manager->m_registrations.cend() || !it->registered)
            continue;
        if (!returnedIds.contains(shortcutInfo(action).id))
            removed.append({action, it->sequence});
    }
    for (const auto &entry : removed) {
        auto it = m_manager->m_registrations.find(int(entry.first));
        if (it != m_manager->m_registrations.end()) {
            it->registered = false;
            it->desktopAssigned = false;
        }
        qCWarning(lcHotkey) << "The desktop removed the GlobalShortcuts shortcut"
                            << shortcutInfo(entry.first).id;
        emit m_manager->registrationFailed(
            entry.first, entry.second,
            QStringLiteral("The desktop removed the GlobalShortcuts shortcut \"%1\".")
                .arg(shortcutInfo(entry.first).id));
    }
}

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
    , m_waylandSession(Platform::isWaylandSession())
{
    // No display is opened and no native event filter is installed here: the
    // implementation owns a dedicated Xlib connection and a QSocketNotifier.
    qRegisterMetaType<HotkeyAction>("HotkeyAction");
}

HotkeyManager::~HotkeyManager()
{
    unregisterAll();
}

bool HotkeyManager::registerHotkey(HotkeyAction action, const QKeySequence &sequence)
{
    quint32 modifiers = 0;
    QString error;
    if (!mapSequence(sequence, nullptr, &modifiers, &error)) {
        qCWarning(lcHotkey) << "Unsupported hotkey"
                            << sequence.toString(QKeySequence::NativeText) << ":" << error;
        emit registrationFailed(action, sequence, error);
        return false;
    }

    Registration &registration = m_registrations[int(action)];
    if (m_waylandSession)
        return registerWayland(action, sequence);

    if (registration.registered) {
        if (m_x11)
            m_x11->ungrab(registration.keycode, registration.modifiers);
        registration.registered = false;
    }
    registration.sequence = sequence;
    registration.keycode = 0;
    registration.modifiers = modifiers;

    if (m_suspended) {
        qCInfo(lcHotkey) << "Suspended; remembering"
                         << sequence.toString(QKeySequence::NativeText);
        return true;
    }
    return registerNative(action, registration);
}

bool HotkeyManager::registerWayland(HotkeyAction action, const QKeySequence sequence)
{
    // The sequence is a copy: the map entry below can disappear while a
    // synchronous portal wait runs a nested event loop.
    auto current = m_registrations.find(int(action));
    if (current == m_registrations.end())
        return false; // registerHotkey inserts the entry before this call
    const bool wasBound = current->registered;
    const bool sameSequence = current->sequence == sequence;
    current->sequence = sequence;
    current->keycode = 0;
    current->modifiers = 0;

    if (!m_wayland)
        m_wayland = std::make_unique<WaylandState>(this);

    // The portal check comes before the suspended check, like the X11 task's
    // order: on Wayland a missing portal is reported even while suspended.
    QString reason;
    if (!m_wayland->ensurePortal(&reason)) {
        // A reentrant unregisterAll() was deferred while the portal check was
        // waiting: the application is quitting, so report nothing.
        if (finishDeferredShutdown())
            return true;
        auto registration = m_registrations.find(int(action));
        if (registration != m_registrations.end()) {
            registration->registered = false;
            registration->desktopAssigned = false;
            qCWarning(lcHotkey) << "Cannot register"
                                << sequence.toString(QKeySequence::NativeText)
                                << "on a Wayland session:" << reason;
            emit registrationFailed(action, sequence, reason);
        }
        return false;
    }
    if (finishDeferredShutdown())
        return true;

    if (wasBound && sameSequence) {
        // Repeating the same sequence for a bound action must not recreate the
        // session (and must not prompt the desktop again).
        qCDebug(lcHotkey) << "GlobalShortcuts shortcut already bound:"
                          << shortcutInfo(action).id;
        return true;
    }

    if (m_suspended) {
        if (wasBound)
            m_wayland->shutdown(); // the old trigger must not stay bound
        if (finishDeferredShutdown())
            return true;
        auto registration = m_registrations.find(int(action));
        if (registration != m_registrations.end()) {
            registration->registered = false;
            registration->desktopAssigned = false;
        }
        qCInfo(lcHotkey) << "Suspended; remembering"
                         << sequence.toString(QKeySequence::NativeText);
        return true;
    }

    if (!m_wayland->registerShortcut(action, &reason)) {
        if (finishDeferredShutdown())
            return true;
        auto registration = m_registrations.find(int(action));
        if (registration != m_registrations.end()) {
            registration->registered = false;
            registration->desktopAssigned = false;
            qCWarning(lcHotkey) << "Cannot register"
                                << sequence.toString(QKeySequence::NativeText)
                                << "on a Wayland session:" << reason;
            emit registrationFailed(action, sequence, reason);
        }
        return false;
    }
    if (finishDeferredShutdown())
        return true;
    return true;
}

void HotkeyManager::unregisterHotkey(HotkeyAction action)
{
    auto it = m_registrations.find(int(action));
    if (it == m_registrations.end())
        return;
    if (m_waylandSession) {
        m_registrations.erase(it);
        if (m_wayland) {
            m_wayland->removeShortcut();
            // removeShortcut() waits for Session.Close; a nested unregisterAll()
            // deferred the teardown until that wait returned.
            finishDeferredShutdown();
        }
        return;
    }
    if (it->registered && m_x11)
        m_x11->ungrab(it->keycode, it->modifiers);
    m_registrations.erase(it);
}

void HotkeyManager::unregisterAll()
{
    if (m_waylandSession) {
        if (m_wayland && m_wayland->inPortalCall()) {
            // unregisterAll() can be reached from a nested event loop (for
            // example the tray Quit over D-Bus) while a synchronous portal
            // wait is on the stack. Resetting the state there would invalidate
            // the caller, so finish the teardown after the wait returns.
            m_wayland->deferShutdown();
            return;
        }
        if (m_wayland) {
            m_wayland->shutdown();
            m_wayland.reset();
        }
        m_registrations.clear();
        return;
    }

    for (auto it = m_registrations.begin(); it != m_registrations.end(); ++it) {
        if (it->registered && m_x11)
            m_x11->ungrab(it->keycode, it->modifiers);
        it->registered = false;
    }
    m_registrations.clear();
    m_x11.reset();
}

bool HotkeyManager::finishDeferredShutdown()
{
    if (!m_wayland || !m_wayland->takeDeferredShutdown())
        return false;
    qCInfo(lcHotkey) << "Unregistering global hotkeys after a deferred shutdown";
    m_wayland->shutdown();
    m_wayland.reset();
    m_registrations.clear();
    return true;
}

void HotkeyManager::setSuspended(bool suspended)
{
    if (m_suspended == suspended)
        return;
    m_suspended = suspended;

    if (m_waylandSession) {
        // The portal has no per-session suspend: keep it and ignore Activated.
        // Closing it would force CreateSession+BindShortcuts (and possibly a
        // desktop confirmation) on every settings hotkey edit.
        if (m_wayland)
            m_wayland->setSuspended(suspended);
        if (suspended) {
            qCInfo(lcHotkey) << "Global hotkeys suspended (portal session kept)";
            return;
        }
        qCInfo(lcHotkey) << "Global hotkeys resumed";
        const QList<int> actions = m_registrations.keys();
        for (int action : actions) {
            auto it = m_registrations.find(action);
            if (it != m_registrations.end() && !it->registered)
                registerNative(HotkeyAction(action), *it);
        }
        return;
    }

    if (suspended) {
        for (auto it = m_registrations.begin(); it != m_registrations.end(); ++it) {
            if (it->registered && m_x11) {
                m_x11->ungrab(it->keycode, it->modifiers);
                it->registered = false;
            }
        }
        if (m_x11)
            m_x11->pressedKeys.clear();
        qCInfo(lcHotkey) << "Hotkeys suspended";
        return;
    }

    const QList<int> actions = m_registrations.keys();
    for (int action : actions) {
        auto it = m_registrations.find(action);
        if (it != m_registrations.end() && !it->registered)
            registerNative(HotkeyAction(action), *it);
    }
    qCInfo(lcHotkey) << "Hotkeys resumed";
}

QKeySequence HotkeyManager::hotkey(HotkeyAction action) const
{
    const auto it = m_registrations.constFind(int(action));
    if (it == m_registrations.cend())
        return QKeySequence();
    // After a successful GlobalShortcuts bind the desktop owns the trigger. The
    // portal only returns a locale-dependent trigger_description, so reporting
    // the stored sequence could lie about the actual shortcut.
    if (it->desktopAssigned)
        return QKeySequence();
    return it->sequence;
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message,
                                      qintptr *result)
{
    // XGrabKey events arrive through the dedicated Xlib connection above, not
    // through Qt's xcb event filter. The override only keeps API parity.
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
    return false;
}

bool HotkeyManager::registerNative(HotkeyAction action, Registration &registration)
{
    if (m_waylandSession)
        return registerWayland(action, registration.sequence);

    KeySym keysym = NoSymbol;
    quint32 modifiers = 0;
    QString mapError;
    if (!mapSequence(registration.sequence, &keysym, &modifiers, &mapError)) {
        registration.registered = false;
        registration.keycode = 0;
        qCWarning(lcHotkey) << "Unsupported hotkey"
                            << registration.sequence.toString(QKeySequence::NativeText)
                            << ":" << mapError;
        emit registrationFailed(action, registration.sequence, mapError);
        return false;
    }
    registration.modifiers = modifiers;

    if (!m_x11)
        m_x11 = std::make_unique<X11State>();

    QString displayError;
    if (!m_x11->open(this, &displayError)) {
        registration.registered = false;
        registration.keycode = 0;
        qCWarning(lcHotkey) << "Cannot register"
                            << registration.sequence.toString(QKeySequence::NativeText)
                            << ":" << displayError;
        emit registrationFailed(action, registration.sequence, displayError);
        return false;
    }

    const KeyCode keycode = keycodeForKeysym(m_x11->display, keysym);
    if (keycode == 0) {
        registration.registered = false;
        registration.keycode = 0;
        const QString reason =
            QStringLiteral("The key is not available in the current keyboard layout.");
        qCWarning(lcHotkey) << "Cannot register"
                            << registration.sequence.toString(QKeySequence::NativeText)
                            << ":" << reason;
        emit registrationFailed(action, registration.sequence, reason);
        return false;
    }

    registration.keycode = quint32(keycode);
    QString grabError;
    if (!m_x11->grab(registration.keycode, registration.modifiers, &grabError)) {
        registration.registered = false;
        registration.keycode = 0;
        qCWarning(lcHotkey) << "Cannot register"
                            << registration.sequence.toString(QKeySequence::NativeText)
                            << "for action" << int(action) << ":" << grabError;
        emit registrationFailed(action, registration.sequence, grabError);
        return false;
    }

    registration.registered = true;
    qCInfo(lcHotkey) << "Registered"
                     << registration.sequence.toString(QKeySequence::NativeText)
                     << "for action" << int(action);
    return true;
}

#include "hotkeymanager_linux.moc"
