// Covers the external-selection replacement on X11 with the real platform code:
// a focused editor with a selected word, the real SelectionGrabber (XTest
// Ctrl+C), the real ProofreadingPopupWindow in external mode and the real
// Platform::sendPasteShortcut() pasting the corrected text back.
//
// The test needs a real X server, the XTEST extension and the xcb platform
// plugin; run it under `xvfb-run -a` with QT_QPA_PLATFORM=xcb (the CMake target
// sets that). It changes the X input focus and the clipboard, assumes there is
// no window manager, and must not run against a real desktop session.
//
// AppController owns the window-change and the Wayland copy fallbacks, and its
// post-paste clipboard restore reads the settings; the controller cannot be
// linked here without the whole application, so those stay manual checks (see
// the task outcome). This test covers the grabber's own restore only.

#include "core/proofreadingtypes.h"
#include "platform/selectiongrabber.h"
#include "platform/windowactivation.h"
#include "ui/proofreadingpopupwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>
#include <QTextEdit>

#include <memory>

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

// Releases every key that is still down when the guard is destroyed and flushes
// the releases, so a failed assertion cannot leave the grabber's synthetic
// Ctrl+C pressed for the next test function.
class PressedKeysGuard
{
public:
    explicit PressedKeysGuard(Display *display)
        : m_display(display)
    {
    }

    ~PressedKeysGuard()
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

bool hasMarker(const QMimeData *data)
{
    return data && data->hasFormat(QString::fromLatin1(kMarkerFormat))
        && data->data(QString::fromLatin1(kMarkerFormat)) == QByteArray("marker");
}

ProofreadingIssue makeIssue(int offset, int length, const QString &replacement)
{
    ProofreadingIssue issue;
    issue.offset = offset;
    issue.length = length;
    issue.message = QStringLiteral("Possible spelling mistake");
    issue.replacements = QStringList{replacement};
    issue.ruleId = QStringLiteral("MORFOLOGIK_RULE_EN_US");
    return issue;
}

ProofreadingResult makeResult(const QVector<ProofreadingIssue> &issues)
{
    ProofreadingResult result;
    result.issues = issues;
    result.serviceId = QStringLiteral("fake");
    return result;
}

} // namespace

class TestProofreadingSelectionX11 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    void replacesFocusedEditorSelection();
    void popupDoesNotTakeFocus();
    void grabberRestoresClipboard();
    void noSelectionIsEmpty();

private:
    bool focusWidget(QWidget *widget);
    void settleClipboard(QClipboard *clipboard, const QString &expected);

    Display *m_display = nullptr;
    std::unique_ptr<EnvironmentGuard> m_waylandGuard;
    std::unique_ptr<EnvironmentGuard> m_sessionTypeGuard;
};

void TestProofreadingSelectionX11::initTestCase()
{
    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        QSKIP("Cannot open the X11 display; the selection replacement test needs a running "
              "X server.");
    }

    if (QGuiApplication::platformName() != QLatin1String("xcb"))
        QSKIP("The selection replacement test needs the xcb platform plugin; set "
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
    m_waylandGuard = std::make_unique<EnvironmentGuard>("WAYLAND_DISPLAY");
    m_sessionTypeGuard = std::make_unique<EnvironmentGuard>("XDG_SESSION_TYPE");
    qunsetenv("WAYLAND_DISPLAY");
    qputenv("XDG_SESSION_TYPE", "x11");
}

void TestProofreadingSelectionX11::cleanupTestCase()
{
    if (m_display) {
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
}

void TestProofreadingSelectionX11::cleanup()
{
    if (!m_display)
        return;

    // The widgets of a test function are gone at this point; without a window
    // manager the server would keep the input focus on a destroyed window.
    XSetInputFocus(m_display, PointerRoot, RevertToPointerRoot, CurrentTime);
    XSync(m_display, False);
}

bool TestProofreadingSelectionX11::focusWidget(QWidget *widget)
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

// The X11 clipboard is asynchronous even for the process that owns it; wait
// until the new content is readable so a synthetic paste cannot race it.
void TestProofreadingSelectionX11::settleClipboard(QClipboard *clipboard,
                                                   const QString &expected)
{
    QTRY_COMPARE_WITH_TIMEOUT(clipboard->text(), expected, 2000);
}

void TestProofreadingSelectionX11::replacesFocusedEditorSelection()
{
    PressedKeysGuard keys(m_display);

    QTextEdit edit;
    edit.setPlainText(QStringLiteral("teh cat"));
    QTextCursor selection = edit.textCursor();
    selection.setPosition(0);
    selection.setPosition(3, QTextCursor::KeepAnchor); // select "teh" only
    edit.setTextCursor(selection);
    QVERIFY(focusWidget(&edit));
    const quintptr editorWindow = quintptr(edit.winId());

    QClipboard *clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard);
    setMarkerClipboard(clipboard, QStringLiteral("replacement marker"));
    settleClipboard(clipboard, QStringLiteral("replacement marker"));

    // The real capture: the grabber waits for the keys, sends Ctrl+C through
    // XTEST to the focused editor and restores the clipboard afterwards.
    SelectionGrabber grabber;
    int grabCalls = 0;
    QString grabbed;
    grabber.grab([&grabCalls, &grabbed](const QString &text) {
        ++grabCalls;
        grabbed = text;
    });
    QTRY_COMPARE_WITH_TIMEOUT(grabCalls, 1, 5000);
    QCOMPARE(grabbed, QStringLiteral("teh"));

    // The real confirmation window in external mode, fed with a synthetic
    // LanguageTool-style result for the captured text.
    ProofreadingPopupWindow popup;
    QSignalSpy replaceSpy(&popup, &ProofreadingPopupWindow::replaceRequested);
    popup.showChecking(grabbed, QCursor::pos());
    QTRY_VERIFY_WITH_TIMEOUT(popup.isVisible(), 5000);
    popup.showResult(makeResult({makeIssue(0, 3, QStringLiteral("the"))}));

    auto *replaceAll = popup.findChild<QPushButton *>(QStringLiteral("proofreadingReplaceAll"));
    QVERIFY(replaceAll);
    QVERIFY(replaceAll->isEnabled());
    QCOMPARE(popup.correctedText(), QStringLiteral("the"));

    // A direct click() does not move the focus, as in the real confirmation.
    // The window only emits the signal; AppController owns the paste.
    replaceAll->click();
    QCOMPARE(replaceSpy.count(), 1);

    // AppController remembers the corrected text of the finished result and
    // reads it in the replaceRequested handler before it hides the popup and
    // pastes; the test takes the same snapshot before the hide.
    const QString corrected = popup.correctedText();
    popup.hide();
    QTRY_VERIFY(!popup.isVisible());
    // The AppController foreground guard would fall back to the clipboard on a
    // changed window; the popup must not have caused that.
    QCOMPARE(Platform::foregroundWindow(), editorWindow);
    clipboard->setText(corrected);
    settleClipboard(clipboard, corrected);
    QVERIFY(Platform::sendPasteShortcut());

    // The synthetic Ctrl+V goes to the editor, which is still the foreground
    // window, and replaces the selected "teh".
    QTRY_COMPARE_WITH_TIMEOUT(edit.toPlainText(), QStringLiteral("the cat"), 5000);
    QCOMPARE(replaceSpy.count(), 1);
}

void TestProofreadingSelectionX11::popupDoesNotTakeFocus()
{
    PressedKeysGuard keys(m_display);

    QTextEdit edit;
    edit.setPlainText(QStringLiteral("focus text"));
    QVERIFY(focusWidget(&edit));

    const quintptr editorWindow = quintptr(edit.winId());
    QTRY_COMPARE_WITH_TIMEOUT(QApplication::focusWindow(), edit.windowHandle(), 2000);
    QCOMPARE(Platform::foregroundWindow(), editorWindow);

    ProofreadingPopupWindow popup;
    popup.showChecking(QStringLiteral("focus text"), QCursor::pos());
    QTRY_VERIFY_WITH_TIMEOUT(popup.isVisible(), 5000);

    // Give the map and focus events time to arrive before the premise check.
    QTest::qWait(200);

    // This is the premise of the AppController foreground guard: the source
    // application has to stay active until the paste, so the confirmation
    // window must not become the focus window.
    QVERIFY(popup.windowHandle());
    QCOMPARE(QApplication::focusWindow(), edit.windowHandle());
    QCOMPARE(Platform::foregroundWindow(), editorWindow);
    QVERIFY(QApplication::focusWindow() != popup.windowHandle());

    popup.hide();
}

void TestProofreadingSelectionX11::grabberRestoresClipboard()
{
    PressedKeysGuard keys(m_display);

    QTextEdit edit;
    edit.setPlainText(QStringLiteral("grab marker text"));
    edit.selectAll();
    QVERIFY(focusWidget(&edit));

    QClipboard *clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard);
    // A non-text format proves the grabber restores its whole snapshot, not
    // only the text that Ctrl+C overwrote.
    setMarkerClipboard(clipboard, QStringLiteral("restored marker"));
    settleClipboard(clipboard, QStringLiteral("restored marker"));

    SelectionGrabber grabber;
    int grabCalls = 0;
    QString grabbed;
    grabber.grab([&grabCalls, &grabbed](const QString &text) {
        ++grabCalls;
        grabbed = text;
    });
    QTRY_COMPARE_WITH_TIMEOUT(grabCalls, 1, 5000);
    QCOMPARE(grabbed, QStringLiteral("grab marker text"));

    QCOMPARE(clipboard->text(), QStringLiteral("restored marker"));
    QVERIFY(hasMarker(clipboard->mimeData()));

    // The grabber restores its own snapshot here. The second restore after the
    // paste is AppController::pasteText()'s 500 ms snapshot, which needs the
    // settings and is therefore a manual check.
}

void TestProofreadingSelectionX11::noSelectionIsEmpty()
{
    PressedKeysGuard keys(m_display);

    QTextEdit edit;
    edit.setPlainText(QStringLiteral("Nothing is selected here"));
    QVERIFY(focusWidget(&edit));

    QClipboard *clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard);
    setMarkerClipboard(clipboard, QStringLiteral("empty marker"));
    settleClipboard(clipboard, QStringLiteral("empty marker"));

    SelectionGrabber grabber;
    int grabCalls = 0;
    QString grabbed = QStringLiteral("not called");
    grabber.grab([&grabCalls, &grabbed](const QString &text) {
        ++grabCalls;
        grabbed = text;
    });
    QTRY_COMPARE_WITH_TIMEOUT(grabCalls, 1, 5000);
    QVERIFY2(grabbed.isEmpty(), "An editor without a selection must produce an empty grab");

    // The grabber's own restore keeps the marker. The "Nothing is selected."
    // message belongs to AppController and is not checked here.
    QCOMPARE(clipboard->text(), QStringLiteral("empty marker"));
    QVERIFY(hasMarker(clipboard->mimeData()));
}

QTEST_MAIN(TestProofreadingSelectionX11)
#include "tst_proofreading_selection_x11.moc"
