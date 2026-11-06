#include "platform/selectiongrabber.h"
#include "platform/sessiontype.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMimeData>
#include <QStringList>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

Q_LOGGING_CATEGORY(lcGrab, "qtranslate.grab", QtWarningMsg)

namespace {

constexpr int kPollIntervalMs = 10;
constexpr int kMaxReleaseWaitMs = 300;
constexpr int kMaxClipboardWaitMs = 400;
constexpr int kDefaultMaxLength = 20000;

bool keyIsDown(const char keys[32], KeyCode code)
{
    return code != 0
        && (static_cast<unsigned char>(keys[code / 8]) & (1 << (code % 8))) != 0;
}

std::unique_ptr<QMimeData> snapshotClipboard()
{
    auto copy = std::make_unique<QMimeData>();
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return copy;

    const QMimeData *source = clipboard->mimeData();
    if (!source)
        return copy;

    const QStringList formats = source->formats();
    for (const QString &format : formats)
        copy->setData(format, source->data(format));
    return copy;
}

QString limitLength(QString text, int maxLength)
{
    text = text.trimmed();
    if (text.size() <= maxLength)
        return text;

    int length = maxLength;
    if (length > 0 && text.at(length - 1).isHighSurrogate())
        --length; // do not split a surrogate pair
    qCWarning(lcGrab) << "Selection truncated to" << length << "characters";
    text.truncate(length);
    return text;
}

} // namespace

struct SelectionGrabber::LinuxState
{
    bool wayland = false;
    Display *display = nullptr;
    Window root = 0;
    KeyCode control = 0;
    KeyCode shift = 0;
    KeyCode alt = 0;
    KeyCode superLeft = 0;
    KeyCode superRight = 0;
    KeyCode c = 0;
    bool xtest = false;
    bool unavailable = false;
    bool clipboardChanged = false;

    bool ensureDisplay(SelectionGrabber *grabber);
    bool anyModifierDown() const;
    void releaseModifiers() const;
    bool sendCopy() const;
};

bool SelectionGrabber::LinuxState::ensureDisplay(SelectionGrabber *grabber)
{
    if (unavailable) {
        grabber->finish(QString());
        return false;
    }

    if (!display) {
        display = XOpenDisplay(nullptr);
        if (!display) {
            unavailable = true;
            qCWarning(lcGrab) << "Cannot open the X11 display";
            grabber->finish(QString());
            return false;
        }

        root = DefaultRootWindow(display);
        control = XKeysymToKeycode(display, XK_Control_L);
        shift = XKeysymToKeycode(display, XK_Shift_L);
        alt = XKeysymToKeycode(display, XK_Alt_L);
        superLeft = XKeysymToKeycode(display, XK_Super_L);
        superRight = XKeysymToKeycode(display, XK_Super_R);
        c = XKeysymToKeycode(display, XK_c);
        if (c == 0)
            c = XKeysymToKeycode(display, XK_C);

        int eventBase = 0;
        int errorBase = 0;
        int major = 0;
        int minor = 0;
        xtest = XTestQueryExtension(display, &eventBase, &errorBase, &major, &minor);
    }

    if (!xtest || control == 0 || c == 0) {
        unavailable = true;
        qCWarning(lcGrab) << "Cannot synthesize Ctrl+C:"
                          << (xtest ? "the keycodes are unavailable"
                                    : "the XTEST extension is unavailable");
        grabber->finish(QString());
        return false;
    }
    return true;
}

bool SelectionGrabber::LinuxState::anyModifierDown() const
{
    if (!display)
        return false;

    char keys[32] = {};
    XQueryKeymap(display, keys);
    return keyIsDown(keys, control) || keyIsDown(keys, shift) || keyIsDown(keys, alt)
        || keyIsDown(keys, superLeft) || keyIsDown(keys, superRight);
}

void SelectionGrabber::LinuxState::releaseModifiers() const
{
    if (!display)
        return;

    char keys[32] = {};
    XQueryKeymap(display, keys);
    const KeyCode modifiers[] = {control, shift, alt, superLeft, superRight};
    for (KeyCode code : modifiers) {
        if (keyIsDown(keys, code))
            XTestFakeKeyEvent(display, code, False, CurrentTime);
    }
    XSync(display, False);
}

bool SelectionGrabber::LinuxState::sendCopy() const
{
    if (!display || !xtest || control == 0 || c == 0)
        return false;

    const Bool ok = XTestFakeKeyEvent(display, control, True, CurrentTime)
        && XTestFakeKeyEvent(display, c, True, CurrentTime)
        && XTestFakeKeyEvent(display, c, False, CurrentTime)
        && XTestFakeKeyEvent(display, control, False, CurrentTime);
    XSync(display, False);
    return ok;
}

SelectionGrabber::SelectionGrabber(QObject *parent)
    : QObject(parent)
    , m_linux(std::make_unique<LinuxState>())
{
    m_timer.setInterval(kPollIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &SelectionGrabber::poll);

    // The session cannot change at runtime, so the Wayland flag is cached once.
    m_linux->wayland = Platform::isWaylandSession();
    if (!m_linux->wayland) {
        if (QClipboard *clipboard = QGuiApplication::clipboard()) {
            connect(clipboard, &QClipboard::changed, this,
                    [this](QClipboard::Mode mode) {
                        if (mode != QClipboard::Clipboard
                            || m_state != State::WaitingForClipboard) {
                            return;
                        }
                        m_linux->clipboardChanged = true;
                        poll();
                    });
        }
    }
}

SelectionGrabber::~SelectionGrabber()
{
    cancel();
    if (m_linux->display) {
        XCloseDisplay(m_linux->display);
        m_linux->display = nullptr;
    }
}

void SelectionGrabber::grab(Callback callback)
{
    if (!callback)
        return;
    if (isBusy()) {
        qCDebug(lcGrab) << "Grab already in progress; ignoring";
        return;
    }

    m_callback = std::move(callback);
    m_snapshot.reset();
    m_linux->clipboardChanged = false;

    if (m_linux->wayland) {
        // The user copied the selection manually. The first timer tick reads
        // the clipboard; there is no snapshot, no X11 and no synthetic input.
        m_state = State::WaitingForClipboard;
        m_elapsed.start();
        m_timer.start();
        return;
    }

    if (!m_linux->ensureDisplay(this))
        return; // finish() already reported the empty selection

    m_state = State::WaitingForRelease;
    m_elapsed.start();
    m_timer.start();
    poll();
}

void SelectionGrabber::cancel()
{
    if (!isBusy())
        return;

    const State previous = m_state;
    m_timer.stop();
    // Leave the state before restoring: setMimeData() emits changed()
    // synchronously, and the handler must not start another poll.
    m_state = State::Idle;
    if (previous == State::WaitingForClipboard)
        restoreClipboardData();
    else
        m_snapshot.reset();
    m_callback = nullptr;
    m_linux->clipboardChanged = false;
}

void SelectionGrabber::setMaxLength(int length)
{
    m_maxLength = length > 0 ? length : kDefaultMaxLength;
}

void SelectionGrabber::poll()
{
    if (m_state == State::WaitingForRelease) {
        if (!m_linux->anyModifierDown()) {
            startCopy();
        } else if (m_elapsed.elapsed() >= kMaxReleaseWaitMs) {
            qCDebug(lcGrab) << "Modifiers are still down after" << kMaxReleaseWaitMs << "ms";
            m_linux->releaseModifiers();
            startCopy();
        }
        return;
    }

    if (m_state != State::WaitingForClipboard)
        return;

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        finish(QString());
        return;
    }

    if (m_linux->wayland) {
        const QString limited = limitLength(clipboard->text(), m_maxLength);
        qCDebug(lcGrab) << "Read" << limited.size() << "characters from the clipboard";
        finish(limited);
        return;
    }

    if (!m_linux->clipboardChanged) {
        if (m_elapsed.elapsed() >= kMaxClipboardWaitMs) {
            qCDebug(lcGrab) << "Clipboard did not change; no selection";
            finish(QString());
        }
        return;
    }

    const QMimeData *data = clipboard->mimeData();
    const QString text = data && data->hasText() ? data->text() : QString();

    // Leave the state before restoring: the restore emits changed() and must
    // not be mistaken for a new selection.
    m_state = State::Idle;
    restoreClipboardData();
    const QString limited = limitLength(text, m_maxLength);
    qCDebug(lcGrab) << "Grabbed" << limited.size() << "characters in" << m_elapsed.elapsed()
                    << "ms";
    finish(limited);
}

void SelectionGrabber::startCopy()
{
    if (!m_linux->ensureDisplay(this))
        return;

    if (m_restoreClipboard)
        m_snapshot = snapshotClipboard();
    m_linux->clipboardChanged = false;

    if (!m_linux->sendCopy()) {
        qCWarning(lcGrab) << "Cannot send Ctrl+C";
        finish(QString());
        return;
    }

    m_state = State::WaitingForClipboard;
    m_elapsed.start();
}

void SelectionGrabber::finish(const QString &text)
{
    m_timer.stop();
    m_state = State::Idle;
    m_linux->clipboardChanged = false;
    m_snapshot.reset();

    auto callback = std::move(m_callback);
    m_callback = nullptr;
    if (callback)
        callback(text);
}

void SelectionGrabber::restoreClipboardData()
{
    if (!m_snapshot)
        return;
    if (!m_restoreClipboard) {
        m_snapshot.reset();
        return;
    }

    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setMimeData(m_snapshot.release());
    else
        m_snapshot.reset();
}
