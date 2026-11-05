#include "platform/selectiongrabber.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMimeData>
#include <QStringList>

#include <windows.h>

Q_LOGGING_CATEGORY(lcGrab, "qtranslate.grab", QtWarningMsg)

namespace {

constexpr int kPollIntervalMs = 10;
constexpr int kMaxReleaseWaitMs = 300;
constexpr int kMaxClipboardWaitMs = 400;
constexpr int kDefaultMaxLength = 20000;

bool isKeyDown(int virtualKey)
{
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

bool anyModifierDown()
{
    return isKeyDown(VK_CONTROL) || isKeyDown(VK_SHIFT) || isKeyDown(VK_MENU)
        || isKeyDown(VK_LWIN) || isKeyDown(VK_RWIN);
}

void sendKey(WORD virtualKey, bool up)
{
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &input, sizeof(INPUT));
}

void releaseModifiers()
{
    const WORD keys[] = {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN};
    for (WORD key : keys) {
        if (isKeyDown(key))
            sendKey(key, true);
    }
}

bool sendCopy()
{
    INPUT inputs[4] = {};
    for (INPUT &input : inputs) {
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_CONTROL;
    }
    // Ctrl down, C down, C up, Ctrl up. The last three entries override wVk.
    inputs[1].ki.wVk = 'C';
    inputs[2].ki.wVk = 'C';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    return SendInput(4, inputs, sizeof(INPUT)) == 4;
}

std::unique_ptr<QMimeData> snapshotClipboard()
{
    auto copy = std::make_unique<QMimeData>();
    const QMimeData *source = QGuiApplication::clipboard()->mimeData();
    if (!source)
        return copy;

    const QStringList formats = source->formats();
    for (const QString &format : formats)
        copy->setData(format, source->data(format));
    return copy;
}

// Reads CF_UNICODETEXT directly. QClipboard may still hold the previous
// contents for a moment after the clipboard sequence number changes.
bool readClipboardText(QString *text)
{
    if (!OpenClipboard(nullptr))
        return false;

    bool ok = true;
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        const auto *data = static_cast<const wchar_t *>(GlobalLock(handle));
        if (data) {
            *text = QString::fromWCharArray(data);
            GlobalUnlock(handle);
        } else {
            ok = false;
        }
    } else {
        // The new contents have no text (for example, an image or files).
        text->clear();
    }
    CloseClipboard();
    return ok;
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

SelectionGrabber::SelectionGrabber(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(kPollIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &SelectionGrabber::poll);
}

SelectionGrabber::~SelectionGrabber()
{
    cancel();
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
    m_state = State::WaitingForRelease;
    m_elapsed.start();
    m_timer.start();
    poll();
}

void SelectionGrabber::cancel()
{
    if (!isBusy())
        return;

    m_timer.stop();
    if (m_state == State::WaitingForClipboard)
        restoreClipboardData();
    else
        m_snapshot.reset();
    m_state = State::Idle;
    m_callback = nullptr;
    m_sequenceNumber = 0;
}

void SelectionGrabber::setMaxLength(int length)
{
    m_maxLength = length > 0 ? length : kDefaultMaxLength;
}

void SelectionGrabber::poll()
{
    if (m_state == State::WaitingForRelease) {
        if (!anyModifierDown()) {
            startCopy();
        } else if (m_elapsed.elapsed() >= kMaxReleaseWaitMs) {
            qCDebug(lcGrab) << "Modifiers are still down after" << kMaxReleaseWaitMs << "ms";
            releaseModifiers();
            startCopy();
        }
        return;
    }

    if (m_state != State::WaitingForClipboard)
        return;

    if (GetClipboardSequenceNumber() == m_sequenceNumber) {
        if (m_elapsed.elapsed() >= kMaxClipboardWaitMs) {
            qCDebug(lcGrab) << "Clipboard did not change; no selection";
            finish(QString());
        }
        return;
    }

    QString text;
    if (!readClipboardText(&text)) {
        if (m_elapsed.elapsed() >= kMaxClipboardWaitMs) {
            qCWarning(lcGrab) << "Cannot read the clipboard";
            restoreClipboardData();
            finish(QString());
        }
        return;
    }

    restoreClipboardData();
    const QString limited = limitLength(text, m_maxLength);
    qCDebug(lcGrab) << "Grabbed" << limited.size() << "characters in" << m_elapsed.elapsed()
                    << "ms";
    finish(limited);
}

void SelectionGrabber::startCopy()
{
    if (m_restoreClipboard)
        m_snapshot = snapshotClipboard();
    m_sequenceNumber = GetClipboardSequenceNumber();

    if (!sendCopy()) {
        qCWarning(lcGrab) << "SendInput failed:" << int(GetLastError());
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
    m_sequenceNumber = 0;
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

    // Keep the synthetic Ctrl+C out of clipboard history and clipboard
    // managers as far as Windows allows it (see the stage 3 notes in
    // doc/roadmap/qtranslate/legacy-plan.md).
    m_snapshot->setData(QStringLiteral("ExcludeClipboardContentFromMonitorProcessing"),
                        QByteArray(1, '\0'));
    QByteArray noHistory(4, '\0');
    m_snapshot->setData(QStringLiteral("CanIncludeInClipboardHistory"), noHistory);
    QGuiApplication::clipboard()->setMimeData(m_snapshot.release());
}
