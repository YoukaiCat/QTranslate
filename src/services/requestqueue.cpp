#include "services/requestqueue.h"

#include <QTimer>

#include <utility>

RequestQueue::RequestQueue(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &RequestQueue::schedule);
}

void RequestQueue::setRateLimitMs(int ms)
{
    m_rateLimitMs = qMax(0, ms);
}

void RequestQueue::enqueue(QObject *owner, std::function<bool()> isDone,
                           std::function<void()> start)
{
    m_queue.append(Task{QPointer<QObject>(owner), std::move(isDone), std::move(start)});
    schedule();
}

void RequestQueue::taskFinished(QObject *owner)
{
    if (owner && m_active == owner)
        m_active = nullptr;
    schedule();
}

void RequestQueue::schedule()
{
    if (m_active || m_queue.isEmpty())
        return;

    while (!m_queue.isEmpty()) {
        const Task &first = m_queue.first();
        // The owner check comes first: isDone() may capture a raw pointer to it.
        if (first.owner && (!first.isDone || !first.isDone()))
            break;
        m_queue.removeFirst();
    }
    if (m_queue.isEmpty())
        return;

    if (m_lastRequest.isValid()) {
        const qint64 remaining = qint64(m_rateLimitMs) - m_lastRequest.elapsed();
        if (remaining > 0) {
            m_timer->start(int(remaining));
            return;
        }
    }
    startNext();
}

void RequestQueue::startNext()
{
    while (!m_queue.isEmpty()) {
        const Task task = m_queue.takeFirst();
        if (!task.owner || (task.isDone && task.isDone()))
            continue;

        m_active = task.owner;
        connect(task.owner, &QObject::destroyed, this, [this, owner = task.owner.data()] {
            if (owner && m_active == owner)
                m_active = nullptr;
            schedule();
        });
        task.start();
        return;
    }
}

void RequestQueue::noteRequestSent()
{
    m_lastRequest.restart();
}
