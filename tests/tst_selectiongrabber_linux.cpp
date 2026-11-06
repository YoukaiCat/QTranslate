#include "platform/selectiongrabber.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QTest>
#include <QTextEdit>
#include <QWidget>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

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

class TestSelectionGrabberLinux : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void copiesSelectionAndRestoresClipboard();
    void waitsForModifierRelease();
    void releasesHeldModifiersOnTimeout();
    void cancelStopsGrab();
    void cancelDuringClipboardWait();
    void noSelectionTimesOut();
    void restoreDisabledLeavesCopiedText();
    void trimsAndLimits();
    void grabWhileBusyIsIgnored();
    void waylandReadsClipboardWithoutXTest();

private:
    bool focusWidget(QWidget *widget);

    Display *m_display = nullptr;
    bool m_environmentModified = false;
    bool m_hadWaylandDisplay = false;
    QByteArray m_oldWaylandDisplay;
    bool m_hadSessionType = false;
    QByteArray m_oldSessionType;
};

void TestSelectionGrabberLinux::initTestCase()
{
    m_display = XOpenDisplay(nullptr);
    if (!m_display)
        QSKIP("Cannot open the X11 display; the grabber test needs a running X server.");

    if (QGuiApplication::platformName() != QLatin1String("xcb"))
        QSKIP("The grabber test needs the xcb platform plugin; set QT_QPA_PLATFORM=xcb.");

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
}

void TestSelectionGrabberLinux::cleanupTestCase()
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

bool TestSelectionGrabberLinux::focusWidget(QWidget *widget)
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

void TestSelectionGrabberLinux::copiesSelectionAndRestoresClipboard()
{
    QTextEdit edit;
    edit.setPlainText(QStringLiteral("hello selection"));
    edit.selectAll();
    QVERIFY(focusWidget(&edit));

    QClipboard *clipboard = QGuiApplication::clipboard();
    auto original = std::make_unique<QMimeData>();
    original->setText(QStringLiteral("original clipboard"));
    original->setData(QStringLiteral("application/x-qtranslate-test"), QByteArray("marker"));
    clipboard->setMimeData(original.release());

    SelectionGrabber grabber;
    QString result;
    bool called = false;
    grabber.grab([&](const QString &text) {
        called = true;
        result = text;
    });

    QTRY_VERIFY_WITH_TIMEOUT(called, 3000);
    QCOMPARE(result, QStringLiteral("hello selection"));
    QVERIFY(!grabber.isBusy());

    QCOMPARE(clipboard->text(), QStringLiteral("original clipboard"));
    const QMimeData *restored = clipboard->mimeData();
    QVERIFY(restored);
    QVERIFY(restored->hasFormat(QStringLiteral("application/x-qtranslate-test")));
    QCOMPARE(restored->data(QStringLiteral("application/x-qtranslate-test")),
             QByteArray("marker"));
}

void TestSelectionGrabberLinux::waitsForModifierRelease()
{
    QTextEdit edit;
    edit.setPlainText(QStringLiteral("modifier selection"));
    edit.selectAll();
    QVERIFY(focusWidget(&edit));

    const KeyCode control = XKeysymToKeycode(m_display, XK_Control_L);
    QVERIFY(control != 0);
    QVERIFY(XTestFakeKeyEvent(m_display, control, True, CurrentTime));
    XSync(m_display, False);

    SelectionGrabber grabber;
    QString result;
    bool called = false;
    grabber.grab([&](const QString &text) {
        called = true;
        result = text;
    });

    QVERIFY(grabber.isBusy());
    QTest::qWait(100);
    QVERIFY(!called);

    QVERIFY(XTestFakeKeyEvent(m_display, control, False, CurrentTime));
    XSync(m_display, False);

    QTRY_VERIFY_WITH_TIMEOUT(called, 3000);
    QCOMPARE(result, QStringLiteral("modifier selection"));
}

void TestSelectionGrabberLinux::releasesHeldModifiersOnTimeout()
{
    QTextEdit edit;
    edit.setPlainText(QStringLiteral("timeout selection"));
    edit.selectAll();
    QVERIFY(focusWidget(&edit));

    const KeyCode control = XKeysymToKeycode(m_display, XK_Control_L);
    QVERIFY(control != 0);
    QVERIFY(XTestFakeKeyEvent(m_display, control, True, CurrentTime));
    XSync(m_display, False);

    SelectionGrabber grabber;
    QString result;
    bool called = false;
    grabber.grab([&](const QString &text) {
        called = true;
        result = text;
    });

    QTRY_VERIFY_WITH_TIMEOUT(called, 3000);
    QCOMPARE(result, QStringLiteral("timeout selection"));

    QVERIFY(XTestFakeKeyEvent(m_display, control, False, CurrentTime));
    XSync(m_display, False);
}

void TestSelectionGrabberLinux::cancelStopsGrab()
{
    QTextEdit edit;
    edit.setPlainText(QStringLiteral("cancel selection"));
    edit.selectAll();
    QVERIFY(focusWidget(&edit));

    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(QStringLiteral("cancel marker"));

    const KeyCode control = XKeysymToKeycode(m_display, XK_Control_L);
    QVERIFY(control != 0);
    QVERIFY(XTestFakeKeyEvent(m_display, control, True, CurrentTime));
    XSync(m_display, False);

    SelectionGrabber grabber;
    bool called = false;
    grabber.grab([&](const QString &) { called = true; });
    QVERIFY(grabber.isBusy());

    grabber.cancel();
    QVERIFY(!grabber.isBusy());
    QTest::qWait(100);
    QVERIFY(!called);
    QCOMPARE(clipboard->text(), QStringLiteral("cancel marker"));

    QVERIFY(XTestFakeKeyEvent(m_display, control, False, CurrentTime));
    XSync(m_display, False);
}

void TestSelectionGrabberLinux::cancelDuringClipboardWait()
{
    QWidget widget;
    QVERIFY(focusWidget(&widget));

    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(QStringLiteral("wait marker"));

    SelectionGrabber grabber;
    bool called = false;
    grabber.grab([&](const QString &) { called = true; });

    // The grabber sends Ctrl+C to a widget that ignores it; the clipboard does
    // not change, so the grabber is in WaitingForClipboard.
    QTest::qWait(50);
    QVERIFY(grabber.isBusy());

    grabber.cancel();
    QVERIFY(!grabber.isBusy());
    QTest::qWait(100);
    QVERIFY(!called);
    QCOMPARE(clipboard->text(), QStringLiteral("wait marker"));
}

void TestSelectionGrabberLinux::noSelectionTimesOut()
{
    QWidget widget;
    QVERIFY(focusWidget(&widget));

    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(QStringLiteral("timeout marker"));

    SelectionGrabber grabber;
    QString result = QStringLiteral("not called");
    bool called = false;
    grabber.grab([&](const QString &text) {
        called = true;
        result = text;
    });

    QTRY_VERIFY_WITH_TIMEOUT(called, 3000);
    QVERIFY(result.isEmpty());
    QCOMPARE(clipboard->text(), QStringLiteral("timeout marker"));
}

void TestSelectionGrabberLinux::restoreDisabledLeavesCopiedText()
{
    QTextEdit edit;
    edit.setPlainText(QStringLiteral("restore disabled selection"));
    edit.selectAll();
    QVERIFY(focusWidget(&edit));

    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(QStringLiteral("restore marker"));

    SelectionGrabber grabber;
    grabber.setRestoreClipboard(false);
    QString result;
    bool called = false;
    grabber.grab([&](const QString &text) {
        called = true;
        result = text;
    });

    QTRY_VERIFY_WITH_TIMEOUT(called, 3000);
    QCOMPARE(result, QStringLiteral("restore disabled selection"));
    QCOMPARE(clipboard->text(), QStringLiteral("restore disabled selection"));
}

void TestSelectionGrabberLinux::trimsAndLimits()
{
    QTextEdit edit;
    edit.setPlainText(QStringLiteral("  abcdefghij  "));
    edit.selectAll();
    QVERIFY(focusWidget(&edit));

    SelectionGrabber grabber;
    grabber.setMaxLength(5);
    QString result;
    bool called = false;
    grabber.grab([&](const QString &text) {
        called = true;
        result = text;
    });

    QTRY_VERIFY_WITH_TIMEOUT(called, 3000);
    QCOMPARE(result, QStringLiteral("abcde"));

    // A surrogate pair must not be split by the truncation.
    const QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80");
    edit.setPlainText(emoji + emoji + emoji);
    edit.selectAll();
    QVERIFY(focusWidget(&edit));

    SelectionGrabber pairGrabber;
    pairGrabber.setMaxLength(3);
    QString pairResult;
    bool pairCalled = false;
    pairGrabber.grab([&](const QString &text) {
        pairCalled = true;
        pairResult = text;
    });

    QTRY_VERIFY_WITH_TIMEOUT(pairCalled, 3000);
    QCOMPARE(pairResult, emoji);
}

void TestSelectionGrabberLinux::grabWhileBusyIsIgnored()
{
    QTextEdit edit;
    edit.setPlainText(QStringLiteral("busy selection"));
    edit.selectAll();
    QVERIFY(focusWidget(&edit));

    const KeyCode control = XKeysymToKeycode(m_display, XK_Control_L);
    QVERIFY(control != 0);
    QVERIFY(XTestFakeKeyEvent(m_display, control, True, CurrentTime));
    XSync(m_display, False);

    SelectionGrabber grabber;
    int calls = 0;
    QString result;
    grabber.grab([&](const QString &text) {
        ++calls;
        result = text;
    });
    grabber.grab([&](const QString &) { ++calls; });

    QVERIFY(XTestFakeKeyEvent(m_display, control, False, CurrentTime));
    XSync(m_display, False);

    QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 3000);
    QCOMPARE(result, QStringLiteral("busy selection"));
}

void TestSelectionGrabberLinux::waylandReadsClipboardWithoutXTest()
{
    EnvironmentGuard waylandDisplay("WAYLAND_DISPLAY");
    EnvironmentGuard sessionType("XDG_SESSION_TYPE");
    qputenv("WAYLAND_DISPLAY", "wayland-test");
    qputenv("XDG_SESSION_TYPE", "wayland");

    QTextEdit edit;
    edit.setPlainText(QStringLiteral("selection text"));
    edit.selectAll();
    QVERIFY(focusWidget(&edit));

    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(QStringLiteral("clipboard text"));

    SelectionGrabber grabber;
    QString result;
    bool called = false;
    grabber.grab([&](const QString &text) {
        called = true;
        result = text;
    });

    QTRY_VERIFY_WITH_TIMEOUT(called, 3000);
    QCOMPARE(result, QStringLiteral("clipboard text"));
    QCOMPARE(clipboard->text(), QStringLiteral("clipboard text"));
}

QTEST_MAIN(TestSelectionGrabberLinux)
#include "tst_selectiongrabber_linux.moc"
