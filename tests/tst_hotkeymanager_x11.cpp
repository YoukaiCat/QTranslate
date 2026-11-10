#include "platform/hotkeymanager.h"

#include <QByteArray>
#include <QSignalSpy>
#include <QTest>

#include <iterator>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#ifdef QTRANSLATE_HAVE_XTEST
#include <X11/extensions/XTest.h>
#endif

namespace {

int g_grabProbeError = 0;

int grabProbeErrorHandler(Display *, XErrorEvent *event)
{
    g_grabProbeError = event->error_code;
    return 0;
}

// Restores an environment variable on scope exit, including early QSKIP returns.
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

} // namespace

class TestHotkeyManagerX11 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void registersAllActions();
    void mapsRepresentativeKeys();
    void unregisterReleasesGrab();
    void replacesRegistration();
    void destructorReleasesGrab();
    void conflictIsReported();
    void suspensionReleasesAndRestores();
    void suspendedRegistrationIsDeferred();
    void invalidSequencesAreRejected();
    void layoutUnresolvableSequenceIsRejected();
    void unregisterAllReleasesEverything();
    void waylandSessionIsRejected();
    void emptyWaylandDisplayIsNotWayland();
    void xtestActivation();

private:
    bool m_environmentModified = false;
    bool m_hadWaylandDisplay = false;
    QByteArray m_oldWaylandDisplay;
    bool m_hadSessionType = false;
    QByteArray m_oldSessionType;
};

void TestHotkeyManagerX11::initTestCase()
{
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        QSKIP("Cannot open the X11 display; the hotkey test needs a running X server.");

    // Test the X11 path even when the test is started from a Wayland desktop:
    // xvfb-run provides the server, but the caller's environment may still name
    // a Wayland session.
    m_hadWaylandDisplay = qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
    m_oldWaylandDisplay = qgetenv("WAYLAND_DISPLAY");
    m_hadSessionType = qEnvironmentVariableIsSet("XDG_SESSION_TYPE");
    m_oldSessionType = qgetenv("XDG_SESSION_TYPE");
    qunsetenv("WAYLAND_DISPLAY");
    qputenv("XDG_SESSION_TYPE", "x11");
    m_environmentModified = true;

    // Parsec on Astra Linux denies passive grabs for untrusted binaries with
    // BadAccess even on a fresh Xvfb. Probe a few spare combinations on a
    // separate connection and skip instead of reporting a code failure.
    bool grabAllowed = false;
    const KeySym probes[] = {XK_F6, XK_F7, XK_F8};
    for (KeySym probe : probes) {
        const KeyCode keycode = XKeysymToKeycode(display, probe);
        if (keycode == 0)
            continue;
        g_grabProbeError = 0;
        const XErrorHandler previousHandler = XSetErrorHandler(grabProbeErrorHandler);
        XGrabKey(display, int(keycode), ControlMask | Mod1Mask | ShiftMask,
                 DefaultRootWindow(display), False, GrabModeAsync, GrabModeAsync);
        XSync(display, False);
        XSetErrorHandler(previousHandler);
        if (g_grabProbeError == 0) {
            XUngrabKey(display, int(keycode), ControlMask | Mod1Mask | ShiftMask,
                       DefaultRootWindow(display));
            XSync(display, False);
            grabAllowed = true;
            break;
        }
    }
    XCloseDisplay(display);

    if (!grabAllowed) {
        QSKIP("The X server denies passive key grabs (BadAccess); on Astra Linux this is "
              "the Parsec keylogger policy. Run the X11 gate in the Debian container.");
    }
}

void TestHotkeyManagerX11::cleanupTestCase()
{
    if (!m_environmentModified)
        return;
    if (m_hadWaylandDisplay)
        qputenv("WAYLAND_DISPLAY", m_oldWaylandDisplay);
    else
        qunsetenv("WAYLAND_DISPLAY");
    if (m_hadSessionType)
        qputenv("XDG_SESSION_TYPE", m_oldSessionType);
    else
        qunsetenv("XDG_SESSION_TYPE");
}

void TestHotkeyManagerX11::registersAllActions()
{
    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);

    const struct {
        HotkeyAction action;
        QKeySequence sequence;
    } cases[] = {
        {HotkeyAction::TranslateToPopup, QKeySequence(Qt::CTRL | Qt::Key_Q)},
        {HotkeyAction::TranslateAndReplace,
         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q)},
        {HotkeyAction::TranslateToMainWindow, QKeySequence(Qt::CTRL | Qt::Key_W)},
        {HotkeyAction::ShowMainWindow,
         QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F10)},
        {HotkeyAction::ProofreadSelection,
         QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F11)},
    };

    for (const auto &testCase : cases) {
        if (!manager.registerHotkey(testCase.action, testCase.sequence)) {
            // Only a desktop shortcut that already owns the combination is a
            // reason to skip. Any other failure, for example a mapping
            // regression reported as a key missing from the layout, must fail
            // the test instead of being hidden behind QSKIP.
            QVERIFY2(!failures.isEmpty(), "registerHotkey failed without a reason");
            const QString reason = failures.last().at(2).toString();
            QVERIFY2(reason.contains(QLatin1String("another program"), Qt::CaseInsensitive),
                     qPrintable(reason));
            QSKIP("A desktop shortcut already owns one of the test combinations; "
                  "skipping on this desktop.");
        }
        QCOMPARE(manager.hotkey(testCase.action), testCase.sequence);
    }
    QCOMPARE(failures.count(), 0);
}

void TestHotkeyManagerX11::mapsRepresentativeKeys()
{
    const struct {
        KeySym keysym;
        QKeySequence sequence;
    } cases[] = {
        // Shift keeps Ctrl+Alt+F1-F12 away from the server's VT switching.
        {XK_a, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_A)},
        {XK_z, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Z)},
        {XK_0, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_0)},
        {XK_9, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_9)},
        {XK_F1, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F1)},
        {XK_F12, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F12)},
        {XK_Left, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Left)},
        {XK_Up, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Up)},
        {XK_Right, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Right)},
        {XK_Down, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Down)},
        {XK_Home, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Home)},
        {XK_End, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_End)},
        {XK_Page_Up, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_PageUp)},
        {XK_Page_Down,
         QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_PageDown)},
        {XK_Insert, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Insert)},
        {XK_Delete, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Delete)},
        {XK_semicolon,
         QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Semicolon)},
        // Shifted punctuation maps to the base key plus ShiftMask.
        {XK_1, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Exclam)},
        {XK_KP_1, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::KeypadModifier
                               | Qt::Key_1)},
        {XK_KP_Add, QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::KeypadModifier
                                 | Qt::Key_Plus)},
    };

    Display *probe = XOpenDisplay(nullptr);
    if (!probe)
        QSKIP("Cannot open the X11 display for a keymap probe.");

    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    int checked = 0;
    for (const auto &testCase : cases) {
        if (XKeysymToKeycode(probe, testCase.keysym) == 0)
            continue;
        if (!manager.registerHotkey(HotkeyAction::ShowMainWindow, testCase.sequence)) {
            XCloseDisplay(probe);
            // See registersAllActions(): QSKIP only for a genuine conflict with
            // another program, so a mapping regression cannot pass as a skip.
            QVERIFY2(!failures.isEmpty(), "registerHotkey failed without a reason");
            const QString reason = failures.last().at(2).toString();
            QVERIFY2(reason.contains(QLatin1String("another program"), Qt::CaseInsensitive),
                     qPrintable(reason));
            QSKIP("A desktop shortcut owns one of the mapping test combinations.");
        }
        QCOMPARE(manager.hotkey(HotkeyAction::ShowMainWindow), testCase.sequence);
        ++checked;
    }
    XCloseDisplay(probe);

    QVERIFY2(checked > 0, "No representative key was available in the keymap");
    QCOMPARE(failures.count(), 0);
}

void TestHotkeyManagerX11::unregisterReleasesGrab()
{
    const QKeySequence sequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F10);

    HotkeyManager first;
    QVERIFY(first.registerHotkey(HotkeyAction::TranslateToPopup, sequence));
    QCOMPARE(first.hotkey(HotkeyAction::TranslateToPopup), sequence);

    first.unregisterHotkey(HotkeyAction::TranslateToPopup);
    QVERIFY(first.hotkey(HotkeyAction::TranslateToPopup).isEmpty());

    HotkeyManager second;
    QSignalSpy failures(&second, &HotkeyManager::registrationFailed);
    QVERIFY(second.registerHotkey(HotkeyAction::ShowMainWindow, sequence));
    QCOMPARE(failures.count(), 0);
}

void TestHotkeyManagerX11::replacesRegistration()
{
    const QKeySequence first(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F10);
    const QKeySequence second(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F11);

    HotkeyManager manager;
    QVERIFY(manager.registerHotkey(HotkeyAction::TranslateToPopup, first));
    QVERIFY(manager.registerHotkey(HotkeyAction::TranslateToPopup, second));
    QCOMPARE(manager.hotkey(HotkeyAction::TranslateToPopup), second);

    HotkeyManager other;
    // The replaced combination is free again, the new one is still grabbed.
    QVERIFY(other.registerHotkey(HotkeyAction::ShowMainWindow, first));
    QVERIFY(!other.registerHotkey(HotkeyAction::ShowMainWindow, second));
}

void TestHotkeyManagerX11::destructorReleasesGrab()
{
    const QKeySequence sequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F10);
    {
        HotkeyManager manager;
        QVERIFY(manager.registerHotkey(HotkeyAction::TranslateToPopup, sequence));
    }

    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    QVERIFY(manager.registerHotkey(HotkeyAction::ShowMainWindow, sequence));
    QCOMPARE(failures.count(), 0);
}

void TestHotkeyManagerX11::conflictIsReported()
{
    HotkeyManager first;
    HotkeyManager second;
    const QKeySequence sequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F11);

    QVERIFY(first.registerHotkey(HotkeyAction::TranslateToMainWindow, sequence));

    QSignalSpy failures(&second, &HotkeyManager::registrationFailed);
    QVERIFY(!second.registerHotkey(HotkeyAction::TranslateToPopup, sequence));
    QCOMPARE(failures.count(), 1);
    const QList<QVariant> failure = failures.first();
    QVERIFY(failure.at(0).value<HotkeyAction>() == HotkeyAction::TranslateToPopup);
    QCOMPARE(failure.at(1).value<QKeySequence>(), sequence);
    const QString reason = failure.at(2).toString();
    QVERIFY(!reason.isEmpty());
    QVERIFY2(reason.contains(QLatin1String("another program"), Qt::CaseInsensitive),
             qPrintable(reason));
}

void TestHotkeyManagerX11::suspensionReleasesAndRestores()
{
    HotkeyManager first;
    QSignalSpy failures(&first, &HotkeyManager::registrationFailed);
    const QKeySequence sequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F10);

    QVERIFY(first.registerHotkey(HotkeyAction::TranslateToPopup, sequence));
    first.setSuspended(true);
    QVERIFY(first.isSuspended());

    {
        HotkeyManager other;
        QVERIFY(other.registerHotkey(HotkeyAction::TranslateToPopup, sequence));
    } // the other manager releases the sequence

    first.setSuspended(false);
    QVERIFY(!first.isSuspended());
    // The sequence was free again, so resuming must succeed.
    QCOMPARE(failures.count(), 0);
    QCOMPARE(first.hotkey(HotkeyAction::TranslateToPopup), sequence);
}

void TestHotkeyManagerX11::suspendedRegistrationIsDeferred()
{
    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    const QKeySequence sequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F10);

    manager.setSuspended(true);
    QVERIFY(manager.registerHotkey(HotkeyAction::ShowMainWindow, sequence));
    QCOMPARE(manager.hotkey(HotkeyAction::ShowMainWindow), sequence);

    {
        HotkeyManager probe;
        QVERIFY(probe.registerHotkey(HotkeyAction::TranslateToPopup, sequence));
    } // nothing was grabbed while suspended

    manager.setSuspended(false);
    QCOMPARE(failures.count(), 0);
    QCOMPARE(manager.hotkey(HotkeyAction::ShowMainWindow), sequence);

    HotkeyManager probe;
    QVERIFY(!probe.registerHotkey(HotkeyAction::TranslateToPopup, sequence));
}

void TestHotkeyManagerX11::invalidSequencesAreRejected()
{
    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);

    const QKeySequence sequences[] = {
        QKeySequence(),
        QKeySequence(Qt::CTRL),
        QKeySequence(QStringLiteral("Ctrl+K, Ctrl+C")),
        QKeySequence(Qt::Key_MediaPlay),
    };

    int expectedFailures = 0;
    for (const QKeySequence &sequence : sequences) {
        QVERIFY2(!manager.registerHotkey(HotkeyAction::ShowMainWindow, sequence),
                 qPrintable(sequence.toString(QKeySequence::NativeText)));
        QCOMPARE(failures.count(), ++expectedFailures);
        const QList<QVariant> failure = failures.last();
        QCOMPARE(failure.at(1).value<QKeySequence>(), sequence);
        QVERIFY(!failure.at(2).toString().isEmpty());
    }
}

void TestHotkeyManagerX11::layoutUnresolvableSequenceIsRejected()
{
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        QSKIP("Cannot reopen the X11 display.");
    const bool f24Present = XKeysymToKeycode(display, XK_F24) != 0;
    XCloseDisplay(display);
    if (f24Present)
        QSKIP("The current keymap has XK_F24; no layout-unresolvable key is available.");

    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    const QKeySequence sequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F24);

    QVERIFY(!manager.registerHotkey(HotkeyAction::ShowMainWindow, sequence));
    QCOMPARE(failures.count(), 1);
    const QString reason = failures.first().at(2).toString();
    QVERIFY(!reason.isEmpty());
    QVERIFY2(reason.contains(QLatin1String("layout"), Qt::CaseInsensitive),
             qPrintable(reason));
}

void TestHotkeyManagerX11::unregisterAllReleasesEverything()
{
    const QKeySequence firstSequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F10);
    const QKeySequence secondSequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F11);
    const QKeySequence thirdSequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F12);

    HotkeyManager manager;
    QVERIFY(manager.registerHotkey(HotkeyAction::TranslateToPopup, firstSequence));
    QVERIFY(manager.registerHotkey(HotkeyAction::TranslateToMainWindow, secondSequence));
    QVERIFY(manager.registerHotkey(HotkeyAction::ProofreadSelection, thirdSequence));

    manager.unregisterAll();
    QVERIFY(manager.hotkey(HotkeyAction::TranslateToPopup).isEmpty());
    QVERIFY(manager.hotkey(HotkeyAction::TranslateToMainWindow).isEmpty());
    QVERIFY(manager.hotkey(HotkeyAction::ProofreadSelection).isEmpty());

    HotkeyManager other;
    QSignalSpy failures(&other, &HotkeyManager::registrationFailed);
    QVERIFY(other.registerHotkey(HotkeyAction::TranslateToPopup, firstSequence));
    QVERIFY(other.registerHotkey(HotkeyAction::TranslateToMainWindow, secondSequence));
    QVERIFY(other.registerHotkey(HotkeyAction::ProofreadSelection, thirdSequence));
    QCOMPARE(failures.count(), 0);
}

void TestHotkeyManagerX11::waylandSessionIsRejected()
{
    EnvironmentGuard waylandDisplay("WAYLAND_DISPLAY");
    EnvironmentGuard sessionType("XDG_SESSION_TYPE");
    EnvironmentGuard display("DISPLAY");
    EnvironmentGuard busAddress("DBUS_SESSION_BUS_ADDRESS");
    const QByteArray realDisplay = qgetenv("DISPLAY");
    qputenv("WAYLAND_DISPLAY", "wayland-0");
    qputenv("XDG_SESSION_TYPE", "wayland");
    // An unused display: if the backend tried to open it, the reason would be a
    // display error instead of the Wayland one.
    qputenv("DISPLAY", ":77");
    // Do not contact a real portal on a portal-capable desktop; the Wayland
    // backend must report the missing session bus and the CLI fallback.
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/tmp/qtranslate-no-bus");

    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    const QKeySequence sequence(Qt::CTRL | Qt::Key_Q);

    QVERIFY(!manager.registerHotkey(HotkeyAction::TranslateToPopup, sequence));
    QCOMPARE(failures.count(), 1);
    QCOMPARE(manager.hotkey(HotkeyAction::TranslateToPopup), sequence);
    const QString reason = failures.first().at(2).toString();
    QVERIFY(!reason.isEmpty());
    QVERIFY2(reason.contains(QLatin1String("portal"), Qt::CaseInsensitive),
             qPrintable(reason));
    QVERIFY2(reason.contains(QLatin1String("CLI")), qPrintable(reason));

    // The stage rule also covers XWayland: a working X display must not win
    // over the Wayland session detection.
    qputenv("DISPLAY", realDisplay);
    HotkeyManager onXWayland;
    QSignalSpy xwaylandFailures(&onXWayland, &HotkeyManager::registrationFailed);
    const QKeySequence secondSequence(Qt::CTRL | Qt::Key_W);
    QVERIFY(!onXWayland.registerHotkey(HotkeyAction::TranslateToMainWindow, secondSequence));
    QCOMPARE(xwaylandFailures.count(), 1);
    QVERIFY2(xwaylandFailures.first().at(2).toString().contains(
                 QLatin1String("portal"), Qt::CaseInsensitive),
             qPrintable(xwaylandFailures.first().at(2).toString()));
}

void TestHotkeyManagerX11::emptyWaylandDisplayIsNotWayland()
{
    // The session rule uses qEnvironmentVariableIsEmpty, not isSet: an empty
    // but set WAYLAND_DISPLAY must still take the X11 path. DISPLAY stays the
    // one initTestCase verified with XOpenDisplay().
    EnvironmentGuard waylandDisplay("WAYLAND_DISPLAY");
    EnvironmentGuard sessionType("XDG_SESSION_TYPE");
    qputenv("WAYLAND_DISPLAY", QByteArray(""));
    qputenv("XDG_SESSION_TYPE", "x11");
    QVERIFY2(qEnvironmentVariableIsSet("WAYLAND_DISPLAY"),
             "qputenv did not keep WAYLAND_DISPLAY set to an empty value");

    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    const QKeySequence sequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F12);

    const bool registered = manager.registerHotkey(HotkeyAction::ShowMainWindow, sequence);
    const QString reason = failures.isEmpty()
        ? QStringLiteral("registerHotkey failed without a reason")
        : failures.last().at(2).toString();
    QVERIFY2(registered, qPrintable(reason));
    QCOMPARE(failures.count(), 0);
    QCOMPARE(manager.hotkey(HotkeyAction::ShowMainWindow), sequence);

    manager.unregisterHotkey(HotkeyAction::ShowMainWindow);
    QVERIFY(manager.hotkey(HotkeyAction::ShowMainWindow).isEmpty());
}

void TestHotkeyManagerX11::xtestActivation()
{
#ifdef QTRANSLATE_HAVE_XTEST
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        QSKIP("Cannot open the X11 display for XTest.");

    int eventBase = 0;
    int errorBase = 0;
    int major = 0;
    int minor = 0;
    if (!XTestQueryExtension(display, &eventBase, &errorBase, &major, &minor)) {
        XCloseDisplay(display);
        QSKIP("The XTEST extension is not available.");
    }

    HotkeyManager manager;
    QSignalSpy activations(&manager, &HotkeyManager::activated);
    const QKeySequence sequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F9);
    if (!manager.registerHotkey(HotkeyAction::TranslateToPopup, sequence)) {
        XCloseDisplay(display);
        QSKIP("The test combination is not available on this X server.");
    }

    const KeyCode control = XKeysymToKeycode(display, XK_Control_L);
    const KeyCode alt = XKeysymToKeycode(display, XK_Alt_L);
    const KeyCode shift = XKeysymToKeycode(display, XK_Shift_L);
    const KeyCode f9 = XKeysymToKeycode(display, XK_F9);
    if (control == 0 || alt == 0 || shift == 0 || f9 == 0) {
        XCloseDisplay(display);
        QSKIP("The XTest keycodes are not available in the current keymap.");
    }

    const KeyCode modifiers[] = {control, alt, shift};
    const auto press = [&]() {
        for (KeyCode modifier : modifiers) {
            if (!XTestFakeKeyEvent(display, modifier, True, CurrentTime))
                return false;
        }
        if (!XTestFakeKeyEvent(display, f9, True, CurrentTime))
            return false;
        XSync(display, False);
        return true;
    };
    const auto release = [&]() {
        // Release the main key first: a passive grab ends with its modifiers.
        if (!XTestFakeKeyEvent(display, f9, False, CurrentTime))
            return false;
        for (auto it = std::rbegin(modifiers); it != std::rend(modifiers); ++it) {
            if (!XTestFakeKeyEvent(display, *it, False, CurrentTime))
                return false;
        }
        XSync(display, False);
        return true;
    };

    QVERIFY(press());
    QVERIFY(release());
    QTRY_COMPARE_WITH_TIMEOUT(activations.count(), 1, 2000);
    QCOMPARE(activations.first().at(0).value<HotkeyAction>(),
             HotkeyAction::TranslateToPopup);

    QVERIFY(press());
    QVERIFY(release());
    QTRY_COMPARE_WITH_TIMEOUT(activations.count(), 2, 2000);

    XCloseDisplay(display);
#else
    QSKIP("Built without XTest; the activation path is unchecked.");
#endif
}

QTEST_GUILESS_MAIN(TestHotkeyManagerX11)
#include "tst_hotkeymanager_x11.moc"
