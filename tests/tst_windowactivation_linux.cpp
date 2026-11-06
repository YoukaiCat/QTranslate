// This test changes the clipboard and the X input focus, so it must run under
// xvfb-run with QT_QPA_PLATFORM=xcb; do not run it against the real session.
#include "platform/windowactivation.h"

#include <QApplication>
#include <QClipboard>
#include <QList>
#include <QTest>
#include <QTextEdit>
#include <QWidget>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include <unistd.h>

namespace {

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

class TestWindowActivationLinux : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void netActiveWindowWinsOverFocus();
    void focusFallbackWithoutProperty();
    void staleActiveWindowFallsBack();
    void noFocusReturnsZero();
    void isForegroundWindowCompares();
    void fullscreenStateIsDetected();
    void ownFullscreenWindowIsIgnored();
    void unmappedFullscreenWindowIsIgnored();
    void missingPropertiesAreSafe();
    void sendPasteShortcutPastes();
    void waylandReturnsZeroAndFalse();

private:
    Window createWindow(bool map = true);
    void destroyWindow(Window window);
    bool mapWindow(Window window);
    void setActiveWindow(Window window);
    void setWmState(Window window, const QList<Atom> &states);
    void setPid(Window window, pid_t pid);
    void focusWindow(Window window);
    bool focusWidget(QWidget *widget);

    Display *m_display = nullptr;
    bool m_environmentModified = false;
    bool m_hadWaylandDisplay = false;
    QByteArray m_oldWaylandDisplay;
    bool m_hadSessionType = false;
    QByteArray m_oldSessionType;
};

void TestWindowActivationLinux::initTestCase()
{
    m_display = XOpenDisplay(nullptr);
    if (!m_display)
        QSKIP("Cannot open the X11 display; the window test needs a running X server.");

    if (QGuiApplication::platformName() != QLatin1String("xcb"))
        QSKIP("The window test needs the xcb platform plugin; set QT_QPA_PLATFORM=xcb.");

    int eventBase = 0;
    int errorBase = 0;
    int major = 0;
    int minor = 0;
    if (!XTestQueryExtension(m_display, &eventBase, &errorBase, &major, &minor))
        QSKIP("The XTEST extension is not available.");

    m_hadWaylandDisplay = qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
    m_oldWaylandDisplay = qgetenv("WAYLAND_DISPLAY");
    m_hadSessionType = qEnvironmentVariableIsSet("XDG_SESSION_TYPE");
    m_oldSessionType = qgetenv("XDG_SESSION_TYPE");
    qunsetenv("WAYLAND_DISPLAY");
    qputenv("XDG_SESSION_TYPE", "x11");
    m_environmentModified = true;
}

void TestWindowActivationLinux::cleanupTestCase()
{
    if (m_environmentModified) {
        if (m_hadWaylandDisplay)
            qputenv("WAYLAND_DISPLAY", m_oldWaylandDisplay);
        else
            qunsetenv("WAYLAND_DISPLAY");
        if (m_hadSessionType)
            qputenv("XDG_SESSION_TYPE", m_oldSessionType);
        else
            qunsetenv("XDG_SESSION_TYPE");
    }
    if (m_display) {
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
}

void TestWindowActivationLinux::init()
{
    setActiveWindow(0);
    XSetInputFocus(m_display, PointerRoot, RevertToPointerRoot, CurrentTime);
    XSync(m_display, False);
}

void TestWindowActivationLinux::cleanup()
{
    setActiveWindow(0);
}

Window TestWindowActivationLinux::createWindow(bool map)
{
    const Window window = XCreateSimpleWindow(m_display, DefaultRootWindow(m_display),
                                              0, 0, 100, 100, 0, 0, 0);
    if (map)
        mapWindow(window);
    else
        XSync(m_display, False);
    return window;
}

void TestWindowActivationLinux::destroyWindow(Window window)
{
    if (window)
        XDestroyWindow(m_display, window);
    XSync(m_display, False);
}

bool TestWindowActivationLinux::mapWindow(Window window)
{
    XMapWindow(m_display, window);
    XSync(m_display, False);
    for (int i = 0; i < 100; ++i) {
        XWindowAttributes attributes;
        if (XGetWindowAttributes(m_display, window, &attributes)
            && attributes.map_state == IsViewable) {
            return true;
        }
        QTest::qWait(10);
    }
    return false;
}

void TestWindowActivationLinux::setActiveWindow(Window window)
{
    const Atom property = XInternAtom(m_display, "_NET_ACTIVE_WINDOW", False);
    if (window == 0) {
        XDeleteProperty(m_display, DefaultRootWindow(m_display), property);
    } else {
        XChangeProperty(m_display, DefaultRootWindow(m_display), property, XA_WINDOW, 32,
                        PropModeReplace, reinterpret_cast<const unsigned char *>(&window), 1);
    }
    XSync(m_display, False);
}

void TestWindowActivationLinux::setWmState(Window window, const QList<Atom> &states)
{
    const Atom property = XInternAtom(m_display, "_NET_WM_STATE", False);
    if (states.isEmpty()) {
        XDeleteProperty(m_display, window, property);
    } else {
        QList<Atom> values = states;
        XChangeProperty(m_display, window, property, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char *>(values.constData()),
                        values.size());
    }
    XSync(m_display, False);
}

void TestWindowActivationLinux::setPid(Window window, pid_t pid)
{
    const Atom property = XInternAtom(m_display, "_NET_WM_PID", False);
    const unsigned long value = static_cast<unsigned long>(pid);
    XChangeProperty(m_display, window, property, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(&value), 1);
    XSync(m_display, False);
}

void TestWindowActivationLinux::focusWindow(Window window)
{
    XSetInputFocus(m_display, window, RevertToParent, CurrentTime);
    XSync(m_display, False);
}

bool TestWindowActivationLinux::focusWidget(QWidget *widget)
{
    widget->show();
    if (!QTest::qWaitForWindowExposed(widget))
        return false;

    XSetInputFocus(m_display, WId(widget->winId()), RevertToParent, CurrentTime);
    XSync(m_display, False);

    Window focus = None;
    int revertTo = 0;
    XGetInputFocus(m_display, &focus, &revertTo);
    if (focus != WId(widget->winId()))
        return false;

    widget->setFocus();
    return true;
}

void TestWindowActivationLinux::netActiveWindowWinsOverFocus()
{
    const Window a = createWindow();
    const Window b = createWindow();
    QVERIFY(a != 0);
    QVERIFY(b != 0);

    setActiveWindow(a);
    focusWindow(b);

    QCOMPARE(Platform::foregroundWindow(), quintptr(a));

    destroyWindow(a);
    destroyWindow(b);
}

void TestWindowActivationLinux::focusFallbackWithoutProperty()
{
    const Window a = createWindow();
    QVERIFY(a != 0);

    setActiveWindow(0);
    focusWindow(a);

    QCOMPARE(Platform::foregroundWindow(), quintptr(a));

    destroyWindow(a);
}

void TestWindowActivationLinux::staleActiveWindowFallsBack()
{
    const Window a = createWindow();
    const Window stale = createWindow(false);
    QVERIFY(a != 0);
    QVERIFY(stale != 0);

    destroyWindow(stale);
    setActiveWindow(stale);
    focusWindow(a);

    QCOMPARE(Platform::foregroundWindow(), quintptr(a));

    destroyWindow(a);
}

void TestWindowActivationLinux::noFocusReturnsZero()
{
    setActiveWindow(0);
    XSetInputFocus(m_display, PointerRoot, RevertToPointerRoot, CurrentTime);
    XSync(m_display, False);

    QCOMPARE(Platform::foregroundWindow(), quintptr(0));
    QVERIFY(!Platform::isForegroundWindow(0));
}

void TestWindowActivationLinux::isForegroundWindowCompares()
{
    const Window a = createWindow();
    const Window b = createWindow();
    QVERIFY(a != 0);
    QVERIFY(b != 0);

    setActiveWindow(a);

    QVERIFY(Platform::isForegroundWindow(quintptr(a)));
    QVERIFY(!Platform::isForegroundWindow(quintptr(b)));
    QVERIFY(!Platform::isForegroundWindow(0));

    destroyWindow(a);
    destroyWindow(b);
}

void TestWindowActivationLinux::fullscreenStateIsDetected()
{
    const Window window = createWindow();
    QVERIFY(window != 0);

    setActiveWindow(window);
    setPid(window, 1); // another process
    const Atom fullscreen = XInternAtom(m_display, "_NET_WM_STATE_FULLSCREEN", False);
    setWmState(window, {fullscreen});

    QVERIFY(Platform::isFullscreenForegroundWindow());

    setWmState(window, {});
    QVERIFY(!Platform::isFullscreenForegroundWindow());

    destroyWindow(window);
}

void TestWindowActivationLinux::ownFullscreenWindowIsIgnored()
{
    const Window window = createWindow();
    QVERIFY(window != 0);

    setActiveWindow(window);
    setPid(window, getpid());
    const Atom fullscreen = XInternAtom(m_display, "_NET_WM_STATE_FULLSCREEN", False);
    setWmState(window, {fullscreen});

    QVERIFY(!Platform::isFullscreenForegroundWindow());

    destroyWindow(window);
}

void TestWindowActivationLinux::unmappedFullscreenWindowIsIgnored()
{
    const Window window = createWindow(false);
    QVERIFY(window != 0);

    setActiveWindow(window);
    setPid(window, 1);
    const Atom fullscreen = XInternAtom(m_display, "_NET_WM_STATE_FULLSCREEN", False);
    setWmState(window, {fullscreen});

    QVERIFY(!Platform::isFullscreenForegroundWindow());

    destroyWindow(window);
}

void TestWindowActivationLinux::missingPropertiesAreSafe()
{
    setActiveWindow(0);
    XSetInputFocus(m_display, PointerRoot, RevertToPointerRoot, CurrentTime);
    XSync(m_display, False);

    QCOMPARE(Platform::foregroundWindow(), quintptr(0));
    QVERIFY(!Platform::isFullscreenForegroundWindow());
}

void TestWindowActivationLinux::sendPasteShortcutPastes()
{
    const KeyCode control = XKeysymToKeycode(m_display, XK_Control_L);
    const KeyCode v = XKeysymToKeycode(m_display, XK_v);
    if (control == 0 || v == 0)
        QSKIP("The XTest keycodes are not available in the current keymap.");

    QTextEdit edit;
    QVERIFY(focusWidget(&edit));

    QGuiApplication::clipboard()->setText(QStringLiteral("pasted"));

    QVERIFY(Platform::sendPasteShortcut());
    QTRY_COMPARE_WITH_TIMEOUT(edit.toPlainText(), QStringLiteral("pasted"), 3000);
}

void TestWindowActivationLinux::waylandReturnsZeroAndFalse()
{
    EnvironmentGuard waylandDisplay("WAYLAND_DISPLAY");
    EnvironmentGuard sessionType("XDG_SESSION_TYPE");
    qputenv("WAYLAND_DISPLAY", "wayland-test");
    qputenv("XDG_SESSION_TYPE", "wayland");

    const Window window = createWindow();
    QVERIFY(window != 0);
    setActiveWindow(window);

    QCOMPARE(Platform::foregroundWindow(), quintptr(0));
    QVERIFY(!Platform::isForegroundWindow(quintptr(window)));
    QVERIFY(!Platform::isFullscreenForegroundWindow());
    QVERIFY(!Platform::sendPasteShortcut());
    Platform::allowAnyProcessToActivate(); // harmless no-op

    QTextEdit edit;
    QVERIFY(focusWidget(&edit));
    QGuiApplication::clipboard()->setText(QStringLiteral("wayland paste"));
    QVERIFY(!Platform::sendPasteShortcut());
    QTest::qWait(100);
    QVERIFY(edit.toPlainText().isEmpty());

    destroyWindow(window);
}

QTEST_MAIN(TestWindowActivationLinux)
#include "tst_windowactivation_linux.moc"
