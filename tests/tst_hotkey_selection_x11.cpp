// Reproduces the X11 hotkey grab-release defect: when the hotkey modifiers are
// released before the main key, the passive XGrabKey stays active, so a
// synthetic Ctrl+C sent while the user still holds the main key goes to the
// hotkey manager instead of the focused application and the selection is lost.
//
// The test needs a real X server, the XTEST extension and the xcb platform
// plugin; run it under `xvfb-run -a` with QT_QPA_PLATFORM=xcb (the CMake target
// sets that). It changes the X input focus and the clipboard, assumes there is
// no window manager, and must not run against a real desktop session.

#include "platform/hotkeymanager.h"
#include "platform/selectiongrabber.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QSignalSpy>
#include <QTest>
#include <QTextEdit>

#include <memory>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

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

// Releases every key that is still down when the guard is destroyed and flushes
// the releases, so a failed assertion cannot leave a key pressed or an active
// keyboard grab behind for the next test function.
class PressedKeysGuard
{
public:
    explicit PressedKeysGuard(Display *display)
        : m_display(display)
    {
    }

    ~PressedKeysGuard() { releaseAll(); }

    bool press(KeyCode code)
    {
        if (!m_display || !XTestFakeKeyEvent(m_display, code, True, CurrentTime))
            return false;
        XSync(m_display, False);
        return true;
    }

    bool release(KeyCode code)
    {
        if (!m_display || !XTestFakeKeyEvent(m_display, code, False, CurrentTime))
            return false;
        XSync(m_display, False);
        return true;
    }

    void releaseAll()
    {
        if (!m_display)
            return;

        char keys[32] = {};
        XQueryKeymap(m_display, keys);
        for (int code = 8; code <= 255; ++code) {
            if ((static_cast<unsigned char>(keys[code / 8]) & (1 << (code % 8))) != 0)
                XTestFakeKeyEvent(m_display, KeyCode(code), False, CurrentTime);
        }
        XSync(m_display, False);
    }

private:
    Display *m_display;
};

const char kMarkerFormat[] = "application/x-qtranslate-test";

void setMarkerClipboard(QClipboard *clipboard, const QString &text)
{
    auto marker = std::make_unique<QMimeData>();
    marker->setText(text);
    marker->setData(QString::fromLatin1(kMarkerFormat), QByteArray("marker"));
    clipboard->setMimeData(marker.release());
}

} // namespace

class TestHotkeySelectionX11 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void releasesModifiersBeforeMainKeyReplace();
    void releasesModifiersBeforeMainKeyPopup();
    void holdsCombinationPastModifierTimeout();
    void normalReleaseOrderStillWorks();
    void secondActivationAfterCapture();

private:
    enum class RegistrationResult { Ok, Conflict, Failed };

    bool focusWidget(QWidget *widget);
    RegistrationResult registerAction(HotkeyManager &manager, const QSignalSpy &failures,
                                      HotkeyAction action, const QKeySequence &sequence,
                                      QString *reason);

    Display *m_display = nullptr;
    bool m_environmentModified = false;
    bool m_hadWaylandDisplay = false;
    QByteArray m_oldWaylandDisplay;
    bool m_hadSessionType = false;
    QByteArray m_oldSessionType;
};

void TestHotkeySelectionX11::initTestCase()
{
    m_display = XOpenDisplay(nullptr);
    if (!m_display)
        QSKIP("Cannot open the X11 display; the hotkey selection test needs a running X server.");

    if (QGuiApplication::platformName() != QLatin1String("xcb"))
        QSKIP("The hotkey selection test needs the xcb platform plugin; set "
              "QT_QPA_PLATFORM=xcb.");

    int eventBase = 0;
    int errorBase = 0;
    int major = 0;
    int minor = 0;
    if (!XTestQueryExtension(m_display, &eventBase, &errorBase, &major, &minor))
        QSKIP("The XTEST extension is not available.");

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
    // BadAccess even on a fresh Xvfb. Probe spare combinations on a separate
    // connection and skip instead of reporting a code failure.
    Display *probe = XOpenDisplay(nullptr);
    if (!probe)
        QSKIP("Cannot open a second X11 display for the passive-grab probe.");

    bool grabAllowed = false;
    const KeySym probeSymbols[] = {XK_F6, XK_F7, XK_F8};
    for (KeySym symbol : probeSymbols) {
        const KeyCode keycode = XKeysymToKeycode(probe, symbol);
        if (keycode == 0)
            continue;
        g_grabProbeError = 0;
        const XErrorHandler previousHandler = XSetErrorHandler(grabProbeErrorHandler);
        XGrabKey(probe, int(keycode), ControlMask | Mod1Mask | ShiftMask,
                 DefaultRootWindow(probe), False, GrabModeAsync, GrabModeAsync);
        XSync(probe, False);
        XSetErrorHandler(previousHandler);
        if (g_grabProbeError == 0) {
            XUngrabKey(probe, int(keycode), ControlMask | Mod1Mask | ShiftMask,
                       DefaultRootWindow(probe));
            XSync(probe, False);
            grabAllowed = true;
            break;
        }
    }
    XCloseDisplay(probe);

    if (!grabAllowed) {
        QSKIP("The X server denies passive key grabs (BadAccess); on Astra Linux this is "
              "the Parsec keylogger policy. Run the X11 gate in the Debian container.");
    }
}

void TestHotkeySelectionX11::cleanupTestCase()
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

bool TestHotkeySelectionX11::focusWidget(QWidget *widget)
{
    widget->show();
    if (!QTest::qWaitForWindowExposed(widget))
        return false;

    // Without a window manager the X focus does not follow Qt by itself.
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

TestHotkeySelectionX11::RegistrationResult TestHotkeySelectionX11::registerAction(
    HotkeyManager &manager, const QSignalSpy &failures, HotkeyAction action,
    const QKeySequence &sequence, QString *reason)
{
    if (manager.registerHotkey(action, sequence))
        return RegistrationResult::Ok;

    *reason = failures.isEmpty()
        ? QStringLiteral("registerHotkey failed without a reason")
        : failures.last().at(2).toString();
    // Only a desktop shortcut that already owns the combination is a reason to
    // skip. Any other failure, for example a mapping regression, must fail the
    // test instead of being hidden behind QSKIP.
    if (reason->contains(QLatin1String("another program"), Qt::CaseInsensitive))
        return RegistrationResult::Conflict;
    return RegistrationResult::Failed;
}

void TestHotkeySelectionX11::releasesModifiersBeforeMainKeyReplace()
{
    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    QSignalSpy activations(&manager, &HotkeyManager::activated);

    QString reason;
    const RegistrationResult registration = registerAction(
        manager, failures, HotkeyAction::TranslateAndReplace,
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q), &reason);
    if (registration == RegistrationResult::Conflict)
        QSKIP("A desktop shortcut already owns Ctrl+Shift+Q; skipping on this desktop.");
    QVERIFY2(registration == RegistrationResult::Ok, qPrintable(reason));

    const QString selectionText = QStringLiteral("replace selection text");
    QTextEdit edit;
    edit.setPlainText(selectionText);
    edit.selectAll();
    QVERIFY(focusWidget(&edit));

    QClipboard *clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard);
    const QString markerText = QStringLiteral("replace marker text");
    setMarkerClipboard(clipboard, markerText);

    int calls = 0;
    QString captured;
    SelectionGrabber grabber;
    connect(&manager, &HotkeyManager::activated, &grabber,
            [&grabber, &calls, &captured](HotkeyAction action) {
                if (action != HotkeyAction::TranslateAndReplace)
                    return;
                grabber.grab([&calls, &captured](const QString &text) {
                    ++calls;
                    captured = text;
                });
            });

    const KeyCode control = XKeysymToKeycode(m_display, XK_Control_L);
    const KeyCode shift = XKeysymToKeycode(m_display, XK_Shift_L);
    const KeyCode q = XKeysymToKeycode(m_display, XK_q);
    QVERIFY(control != 0 && shift != 0 && q != 0);

    PressedKeysGuard keys(m_display);
    QVERIFY(keys.press(control));
    QVERIFY(keys.press(shift));
    QVERIFY(keys.press(q));

    QTRY_COMPARE_WITH_TIMEOUT(activations.count(), 1, 2000);
    QVERIFY(grabber.isBusy());

    // Release the modifiers first and keep the main key down. A passive
    // XGrabKey stays active until the key that activated it is released.
    QVERIFY(keys.release(control));
    QVERIFY(keys.release(shift));
    QTest::qWait(600);
    QVERIFY2(grabber.isBusy(), "The grabber must keep waiting while the main key is held");
    QCOMPARE(calls, 0);

    QVERIFY(keys.release(q));
    QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 3000);
    QCOMPARE(captured, selectionText);
    QCOMPARE(edit.toPlainText(), selectionText);
    QCOMPARE(clipboard->text(), markerText);
    const QMimeData *restored = clipboard->mimeData();
    QVERIFY(restored);
    QVERIFY(restored->hasFormat(QString::fromLatin1(kMarkerFormat)));
    QCOMPARE(restored->data(QString::fromLatin1(kMarkerFormat)), QByteArray("marker"));
}

void TestHotkeySelectionX11::releasesModifiersBeforeMainKeyPopup()
{
    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    QSignalSpy activations(&manager, &HotkeyManager::activated);

    QString reason;
    const RegistrationResult registration =
        registerAction(manager, failures, HotkeyAction::TranslateToPopup,
                       QKeySequence(Qt::CTRL | Qt::Key_Q), &reason);
    if (registration == RegistrationResult::Conflict)
        QSKIP("A desktop shortcut already owns Ctrl+Q; skipping on this desktop.");
    QVERIFY2(registration == RegistrationResult::Ok, qPrintable(reason));

    const QString selectionText = QStringLiteral("popup selection text");
    QTextEdit edit;
    edit.setPlainText(selectionText);
    edit.selectAll();
    QVERIFY(focusWidget(&edit));

    QClipboard *clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard);
    const QString markerText = QStringLiteral("popup marker text");
    setMarkerClipboard(clipboard, markerText);

    int calls = 0;
    QString captured;
    SelectionGrabber grabber;
    connect(&manager, &HotkeyManager::activated, &grabber,
            [&grabber, &calls, &captured](HotkeyAction action) {
                if (action != HotkeyAction::TranslateToPopup)
                    return;
                grabber.grab([&calls, &captured](const QString &text) {
                    ++calls;
                    captured = text;
                });
            });

    const KeyCode control = XKeysymToKeycode(m_display, XK_Control_L);
    const KeyCode q = XKeysymToKeycode(m_display, XK_q);
    QVERIFY(control != 0 && q != 0);

    PressedKeysGuard keys(m_display);
    QVERIFY(keys.press(control));
    QVERIFY(keys.press(q));

    QTRY_COMPARE_WITH_TIMEOUT(activations.count(), 1, 2000);
    QVERIFY(grabber.isBusy());

    QVERIFY(keys.release(control));
    QTest::qWait(600);
    QVERIFY2(grabber.isBusy(), "The grabber must keep waiting while the main key is held");
    QCOMPARE(calls, 0);

    QVERIFY(keys.release(q));
    QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 3000);
    QCOMPARE(captured, selectionText);
    QCOMPARE(edit.toPlainText(), selectionText);
    QCOMPARE(clipboard->text(), markerText);
    const QMimeData *restored = clipboard->mimeData();
    QVERIFY(restored);
    QVERIFY(restored->hasFormat(QString::fromLatin1(kMarkerFormat)));
    QCOMPARE(restored->data(QString::fromLatin1(kMarkerFormat)), QByteArray("marker"));
}

void TestHotkeySelectionX11::holdsCombinationPastModifierTimeout()
{
    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    QSignalSpy activations(&manager, &HotkeyManager::activated);

    QString reason;
    const RegistrationResult registration = registerAction(
        manager, failures, HotkeyAction::TranslateAndReplace,
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q), &reason);
    if (registration == RegistrationResult::Conflict)
        QSKIP("A desktop shortcut already owns Ctrl+Shift+Q; skipping on this desktop.");
    QVERIFY2(registration == RegistrationResult::Ok, qPrintable(reason));

    const QString selectionText = QStringLiteral("held selection text");
    QTextEdit edit;
    edit.setPlainText(selectionText);
    edit.selectAll();
    QVERIFY(focusWidget(&edit));

    QClipboard *clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard);
    const QString markerText = QStringLiteral("held marker text");
    setMarkerClipboard(clipboard, markerText);

    int calls = 0;
    QString captured;
    SelectionGrabber grabber;
    connect(&manager, &HotkeyManager::activated, &grabber,
            [&grabber, &calls, &captured](HotkeyAction action) {
                if (action != HotkeyAction::TranslateAndReplace)
                    return;
                grabber.grab([&calls, &captured](const QString &text) {
                    ++calls;
                    captured = text;
                });
            });

    const KeyCode control = XKeysymToKeycode(m_display, XK_Control_L);
    const KeyCode shift = XKeysymToKeycode(m_display, XK_Shift_L);
    const KeyCode q = XKeysymToKeycode(m_display, XK_q);
    QVERIFY(control != 0 && shift != 0 && q != 0);

    PressedKeysGuard keys(m_display);
    QVERIFY(keys.press(control));
    QVERIFY(keys.press(shift));
    QVERIFY(keys.press(q));

    QTRY_COMPARE_WITH_TIMEOUT(activations.count(), 1, 2000);
    QVERIFY(grabber.isBusy());

    // The 300 ms modifier timeout releases Ctrl and Shift, but the copy still
    // has to wait for the activating key: the passive grab belongs to the
    // hotkey manager's client until Q is released.
    QTest::qWait(800);
    QVERIFY2(grabber.isBusy(), "The 300 ms modifier timeout must not send Ctrl+C");
    QCOMPARE(calls, 0);

    QVERIFY(keys.release(q));
    QVERIFY(keys.release(shift));
    QVERIFY(keys.release(control));
    QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 3000);
    QCOMPARE(captured, selectionText);
    QCOMPARE(edit.toPlainText(), selectionText);
    QCOMPARE(clipboard->text(), markerText);
    const QMimeData *restored = clipboard->mimeData();
    QVERIFY(restored);
    QVERIFY(restored->hasFormat(QString::fromLatin1(kMarkerFormat)));
    QCOMPARE(restored->data(QString::fromLatin1(kMarkerFormat)), QByteArray("marker"));
}

void TestHotkeySelectionX11::normalReleaseOrderStillWorks()
{
    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    QSignalSpy activations(&manager, &HotkeyManager::activated);

    QString reason;
    const RegistrationResult registration = registerAction(
        manager, failures, HotkeyAction::TranslateAndReplace,
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q), &reason);
    if (registration == RegistrationResult::Conflict)
        QSKIP("A desktop shortcut already owns Ctrl+Shift+Q; skipping on this desktop.");
    QVERIFY2(registration == RegistrationResult::Ok, qPrintable(reason));

    const QString selectionText = QStringLiteral("normal selection text");
    QTextEdit edit;
    edit.setPlainText(selectionText);
    edit.selectAll();
    QVERIFY(focusWidget(&edit));

    QClipboard *clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard);
    const QString markerText = QStringLiteral("normal marker text");
    setMarkerClipboard(clipboard, markerText);

    int calls = 0;
    QString captured;
    SelectionGrabber grabber;
    connect(&manager, &HotkeyManager::activated, &grabber,
            [&grabber, &calls, &captured](HotkeyAction action) {
                if (action != HotkeyAction::TranslateAndReplace)
                    return;
                grabber.grab([&calls, &captured](const QString &text) {
                    ++calls;
                    captured = text;
                });
            });

    const KeyCode control = XKeysymToKeycode(m_display, XK_Control_L);
    const KeyCode shift = XKeysymToKeycode(m_display, XK_Shift_L);
    const KeyCode q = XKeysymToKeycode(m_display, XK_q);
    QVERIFY(control != 0 && shift != 0 && q != 0);

    PressedKeysGuard keys(m_display);
    QVERIFY(keys.press(control));
    QVERIFY(keys.press(shift));
    QVERIFY(keys.press(q));

    QTRY_COMPARE_WITH_TIMEOUT(activations.count(), 1, 2000);
    QVERIFY(grabber.isBusy());

    // The release order a normal user produces: the main key first.
    QVERIFY(keys.release(q));
    QVERIFY(keys.release(shift));
    QVERIFY(keys.release(control));

    QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 3000);
    QCOMPARE(captured, selectionText);
    QCOMPARE(edit.toPlainText(), selectionText);
    QCOMPARE(clipboard->text(), markerText);
}

void TestHotkeySelectionX11::secondActivationAfterCapture()
{
    HotkeyManager manager;
    QSignalSpy failures(&manager, &HotkeyManager::registrationFailed);
    QSignalSpy activations(&manager, &HotkeyManager::activated);

    QString reason;
    const RegistrationResult registration = registerAction(
        manager, failures, HotkeyAction::TranslateAndReplace,
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q), &reason);
    if (registration == RegistrationResult::Conflict)
        QSKIP("A desktop shortcut already owns Ctrl+Shift+Q; skipping on this desktop.");
    QVERIFY2(registration == RegistrationResult::Ok, qPrintable(reason));

    const QString selectionText = QStringLiteral("repeat selection text");
    QTextEdit edit;
    edit.setPlainText(selectionText);
    edit.selectAll();
    QVERIFY(focusWidget(&edit));

    QClipboard *clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard);
    const QString markerText = QStringLiteral("repeat marker text");
    setMarkerClipboard(clipboard, markerText);

    int calls = 0;
    QString captured;
    SelectionGrabber grabber;
    connect(&manager, &HotkeyManager::activated, &grabber,
            [&grabber, &calls, &captured](HotkeyAction action) {
                if (action != HotkeyAction::TranslateAndReplace)
                    return;
                grabber.grab([&calls, &captured](const QString &text) {
                    ++calls;
                    captured = text;
                });
            });

    const KeyCode control = XKeysymToKeycode(m_display, XK_Control_L);
    const KeyCode shift = XKeysymToKeycode(m_display, XK_Shift_L);
    const KeyCode q = XKeysymToKeycode(m_display, XK_q);
    QVERIFY(control != 0 && shift != 0 && q != 0);

    PressedKeysGuard keys(m_display);

    // First capture in the normal order.
    QVERIFY(keys.press(control));
    QVERIFY(keys.press(shift));
    QVERIFY(keys.press(q));
    QTRY_COMPARE_WITH_TIMEOUT(activations.count(), 1, 2000);
    QVERIFY(keys.release(q));
    QVERIFY(keys.release(shift));
    QVERIFY(keys.release(control));
    QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 3000);
    QCOMPARE(captured, selectionText);

    // The second activation proves that the manager saw the Q release and
    // cleared pressedKeys; an implementation that ungrabs the keyboard early
    // would miss that release and could not activate again.
    QVERIFY(keys.press(control));
    QVERIFY(keys.press(shift));
    QVERIFY(keys.press(q));
    QTRY_COMPARE_WITH_TIMEOUT(activations.count(), 2, 2000);
    QVERIFY(keys.release(q));
    QVERIFY(keys.release(shift));
    QVERIFY(keys.release(control));
    QTRY_COMPARE_WITH_TIMEOUT(calls, 2, 3000);
    QCOMPARE(captured, selectionText);
    QCOMPARE(edit.toPlainText(), selectionText);
    QCOMPARE(clipboard->text(), markerText);
}

QTEST_MAIN(TestHotkeySelectionX11)
#include "tst_hotkey_selection_x11.moc"
