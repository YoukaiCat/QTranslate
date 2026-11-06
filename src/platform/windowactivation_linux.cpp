#include "platform/windowactivation.h"
#include "platform/sessiontype.h"

#include <QList>
#include <QLoggingCategory>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include <unistd.h>

Q_LOGGING_CATEGORY(lcWindow, "qtranslate.window", QtWarningMsg)

namespace {

int g_x11ErrorCode = 0;

int x11ErrorHandler(Display *, XErrorEvent *event)
{
    g_x11ErrorCode = event->error_code;
    return 0;
}

// Xlib has one process-global error handler. Install it only around the
// property reads and restore the previous one immediately.
class ScopedErrorHandler
{
public:
    ScopedErrorHandler()
        : m_previous(XSetErrorHandler(x11ErrorHandler))
    {
        g_x11ErrorCode = 0;
    }

    ~ScopedErrorHandler() { XSetErrorHandler(m_previous); }

    ScopedErrorHandler(const ScopedErrorHandler &) = delete;
    ScopedErrorHandler &operator=(const ScopedErrorHandler &) = delete;

    int errorCode() const { return g_x11ErrorCode; }

private:
    XErrorHandler m_previous;
};

struct X11State
{
    Display *display = nullptr;
    bool opened = false;
    Window root = 0;
    Atom netActiveWindow = None;
    Atom netWmState = None;
    Atom netWmStateFullscreen = None;
    Atom netWmPid = None;
    bool xtest = false;
    KeyCode control = 0;
    KeyCode v = 0;

    ~X11State()
    {
        if (display)
            XCloseDisplay(display);
    }
};

X11State &x11State()
{
    static X11State instance;
    return instance;
}

bool ensureDisplay()
{
    // Checked on every call: the Wayland guard must win even when an X display
    // was opened earlier in the same process (the tests rely on this).
    if (Platform::isWaylandSession())
        return false;

    X11State &state = x11State();
    if (state.opened)
        return state.display != nullptr;

    state.opened = true;
    state.display = XOpenDisplay(nullptr);
    if (!state.display) {
        qCWarning(lcWindow) << "Cannot open the X11 display";
        return false;
    }

    state.root = DefaultRootWindow(state.display);
    state.netActiveWindow = XInternAtom(state.display, "_NET_ACTIVE_WINDOW", False);
    state.netWmState = XInternAtom(state.display, "_NET_WM_STATE", False);
    state.netWmStateFullscreen =
        XInternAtom(state.display, "_NET_WM_STATE_FULLSCREEN", False);
    state.netWmPid = XInternAtom(state.display, "_NET_WM_PID", False);

    int eventBase = 0;
    int errorBase = 0;
    int major = 0;
    int minor = 0;
    state.xtest = XTestQueryExtension(state.display, &eventBase, &errorBase, &major, &minor);
    state.control = XKeysymToKeycode(state.display, XK_Control_L);
    state.v = XKeysymToKeycode(state.display, XK_v);
    if (state.v == 0)
        state.v = XKeysymToKeycode(state.display, XK_V);
    return true;
}

bool readWindowProperty(Display *display, Window window, Atom property, Atom type,
                        unsigned long *value)
{
    if (property == None)
        return false;

    ScopedErrorHandler handler;
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char *data = nullptr;
    const int status = XGetWindowProperty(display, window, property, 0, 1, False, type,
                                          &actualType, &actualFormat, &itemCount,
                                          &bytesAfter, &data);
    if (status != Success || !data)
        return false;

    bool ok = false;
    if (actualType == type && actualFormat == 32 && itemCount == 1) {
        *value = *reinterpret_cast<unsigned long *>(data);
        ok = true;
    }
    XFree(data);
    return ok && handler.errorCode() == 0;
}

bool readAtomList(Display *display, Window window, Atom property, QList<Atom> *atoms)
{
    if (property == None)
        return false;

    ScopedErrorHandler handler;
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char *data = nullptr;
    const int status = XGetWindowProperty(display, window, property, 0, 1024, False,
                                          XA_ATOM, &actualType, &actualFormat, &itemCount,
                                          &bytesAfter, &data);
    if (status != Success || !data)
        return false;

    bool ok = actualType == XA_ATOM && actualFormat == 32;
    if (ok) {
        const auto *values = reinterpret_cast<unsigned long *>(data);
        for (unsigned long i = 0; i < itemCount; ++i)
            atoms->append(Atom(values[i]));
    }
    XFree(data);
    return ok && handler.errorCode() == 0;
}

bool windowIsValid(Display *display, Window window)
{
    ScopedErrorHandler handler;
    XWindowAttributes attributes;
    const bool ok = XGetWindowAttributes(display, window, &attributes) != 0;
    return ok && handler.errorCode() == 0;
}

} // namespace

namespace Platform {

void allowAnyProcessToActivate()
{
}

quintptr foregroundWindow()
{
    if (!ensureDisplay())
        return 0;

    X11State &state = x11State();
    unsigned long active = 0;
    if (readWindowProperty(state.display, state.root, state.netActiveWindow, XA_WINDOW,
                           &active)
        && active != 0 && windowIsValid(state.display, Window(active))) {
        return quintptr(active);
    }

    Window focus = None;
    int revertTo = 0;
    XGetInputFocus(state.display, &focus, &revertTo);
    if (focus == None || focus == PointerRoot || focus == state.root)
        return 0;
    return quintptr(focus);
}

bool isFullscreenForegroundWindow()
{
    if (!ensureDisplay())
        return false;

    X11State &state = x11State();
    const quintptr active = foregroundWindow();
    if (active == 0)
        return false;

    const Window window = Window(active);

    // Mirror the Windows own-process exclusion. A missing _NET_WM_PID does not
    // exclude the window.
    unsigned long pid = 0;
    if (readWindowProperty(state.display, window, state.netWmPid, XA_CARDINAL, &pid)
        && pid == static_cast<unsigned long>(getpid())) {
        return false;
    }

    QList<Atom> states;
    if (!readAtomList(state.display, window, state.netWmState, &states))
        return false;
    if (!states.contains(state.netWmStateFullscreen))
        return false;

    ScopedErrorHandler handler;
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(state.display, window, &attributes))
        return false;
    return attributes.map_state == IsViewable && handler.errorCode() == 0;
}

bool isForegroundWindow(quintptr window)
{
    return window != 0 && foregroundWindow() == window;
}

bool sendPasteShortcut()
{
    if (!ensureDisplay())
        return false;

    X11State &state = x11State();
    if (!state.xtest || state.control == 0 || state.v == 0) {
        qCWarning(lcWindow) << "Cannot synthesize Ctrl+V:"
                            << (state.xtest ? "the keycodes are unavailable"
                                            : "the XTEST extension is unavailable");
        return false;
    }

    const Bool ok = XTestFakeKeyEvent(state.display, state.control, True, CurrentTime)
        && XTestFakeKeyEvent(state.display, state.v, True, CurrentTime)
        && XTestFakeKeyEvent(state.display, state.v, False, CurrentTime)
        && XTestFakeKeyEvent(state.display, state.control, False, CurrentTime);
    XSync(state.display, False);
    return ok;
}

} // namespace Platform
