#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointer>

#include <functional>

class QTimer;

// A per-host request queue: at most one task is active, and the next one starts
// no sooner than rateLimitMs() after the last network request (see
// doc/architecture.md, §4.5). Shared by QueuedWebService and the proofreading
// services, whose replies do not derive from TranslationReply.
//
// A task holds its slot from start() until its owner emits finished() (reported
// through taskFinished()) or is destroyed, so a multi-chunk protocol sends its
// chunks sequentially inside one slot and paces them with rateLimitMs() instead
// of enqueuing every chunk as a separate task. Tasks call noteRequestSent()
// right before every network request, including retries and later chunks, so
// the pause is measured from the actual request.
// setRateLimitMs(0) disables the pause for tests only.
class RequestQueue : public QObject
{
    Q_OBJECT

public:
    explicit RequestQueue(QObject *parent = nullptr);

    void setRateLimitMs(int ms);
    int rateLimitMs() const { return m_rateLimitMs; }

    // Appends a task and schedules it. `owner` bounds the callbacks' lifetime:
    // a null owner or a task whose isDone() returns true is skipped before
    // start() is called. isDone() must not be called before the owner is
    // checked; it may capture a raw pointer to the owner.
    void enqueue(QObject *owner, std::function<bool()> isDone, std::function<void()> start);

    // Frees the slot when `owner` is the active task and schedules the next
    // one. A stale finished() from an already replaced task changes nothing.
    void taskFinished(QObject *owner);

    // Records that the task has just handed a request to the network.
    void noteRequestSent();

private:
    struct Task
    {
        QPointer<QObject> owner;
        std::function<bool()> isDone;
        std::function<void()> start;
    };

    void schedule();
    void startNext();

    QList<Task> m_queue;
    QPointer<QObject> m_active;
    QElapsedTimer m_lastRequest;
    QTimer *m_timer = nullptr;
    int m_rateLimitMs = 2000;
};
