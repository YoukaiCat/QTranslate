#include "platform/hotkeymanager.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QSet>
#include <QSocketNotifier>
#include <QString>

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

bool isWaylandSession()
{
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        return true;
    if (qEnvironmentVariable("XDG_SESSION_TYPE").compare(QLatin1String("wayland"),
                                                         Qt::CaseInsensitive)
        == 0) {
        return true;
    }
    // The GUI application reports the selected platform plugin. The guiless
    // hotkey test has no QGuiApplication, so this check is skipped there.
    return qGuiApp && qGuiApp->platformName().startsWith(QLatin1String("wayland"),
                                                         Qt::CaseInsensitive);
}

QString waylandReason()
{
    return QStringLiteral(
        "Wayland session: X11 global hotkeys are unavailable. Use the "
        "org.freedesktop.portal.GlobalShortcuts portal or a CLI/desktop shortcut.");
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

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
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
    if (registration.registered) {
        if (m_x11)
            m_x11->ungrab(registration.keycode, registration.modifiers);
        registration.registered = false;
    }
    registration.sequence = sequence;
    registration.keycode = 0;
    registration.modifiers = modifiers;

    if (isWaylandSession()) {
        qCWarning(lcHotkey) << "Cannot register"
                            << sequence.toString(QKeySequence::NativeText)
                            << "on a Wayland session";
        emit registrationFailed(action, sequence, waylandReason());
        return false;
    }

    if (m_suspended) {
        qCInfo(lcHotkey) << "Suspended; remembering"
                         << sequence.toString(QKeySequence::NativeText);
        return true;
    }
    return registerNative(action, registration);
}

void HotkeyManager::unregisterHotkey(HotkeyAction action)
{
    auto it = m_registrations.find(int(action));
    if (it == m_registrations.end())
        return;
    if (it->registered && m_x11)
        m_x11->ungrab(it->keycode, it->modifiers);
    m_registrations.erase(it);
}

void HotkeyManager::unregisterAll()
{
    for (auto it = m_registrations.begin(); it != m_registrations.end(); ++it) {
        if (it->registered && m_x11)
            m_x11->ungrab(it->keycode, it->modifiers);
        it->registered = false;
    }
    m_registrations.clear();
    m_x11.reset();
}

void HotkeyManager::setSuspended(bool suspended)
{
    if (m_suspended == suspended)
        return;
    m_suspended = suspended;

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
    return it == m_registrations.cend() ? QKeySequence() : it->sequence;
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
    if (isWaylandSession()) {
        registration.registered = false;
        registration.keycode = 0;
        qCWarning(lcHotkey) << "Cannot register"
                            << registration.sequence.toString(QKeySequence::NativeText)
                            << "on a Wayland session";
        emit registrationFailed(action, registration.sequence, waylandReason());
        return false;
    }

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
