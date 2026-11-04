#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>

class QMimeData;

// Grabs the text selected in the active Windows application through the
// clipboard: waits for the hotkey to be released, sends Ctrl+C, reads the
// result and restores the previous clipboard contents (see
// doc/architecture.md, §4.4). Asynchronous; the callback is called exactly
// once per successful grab().
class SelectionGrabber : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(const QString &text)>; // empty = no selection

    explicit SelectionGrabber(QObject *parent = nullptr);
    ~SelectionGrabber() override;

    // Ignored while another grab is in progress.
    void grab(Callback callback);
    bool isBusy() const { return m_state != State::Idle; }

    // Stops the current grab and restores the clipboard if it was captured.
    void cancel();

    int maxLength() const { return m_maxLength; }
    void setMaxLength(int length);

    bool restoreClipboard() const { return m_restoreClipboard; }
    void setRestoreClipboard(bool restore) { m_restoreClipboard = restore; }

private:
    enum class State { Idle, WaitingForRelease, WaitingForClipboard };

    void poll();
    void startCopy();
    void finish(const QString &text);
    void restoreClipboardData();

    QTimer m_timer;
    QElapsedTimer m_elapsed;
    State m_state = State::Idle;
    Callback m_callback;
    std::unique_ptr<QMimeData> m_snapshot;
    quint32 m_sequenceNumber = 0;
    int m_maxLength = 20000;
    bool m_restoreClipboard = true;
};
