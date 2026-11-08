#include "services/queuedwebservice.h"

#include <QTimer>

QueuedWebService::QueuedWebService(QObject *parent)
    : TranslationService(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &QueuedWebService::schedule);
}

void QueuedWebService::setRateLimitMs(int ms)
{
    m_rateLimitMs = qMax(0, ms);
}

void QueuedWebService::enqueue(Reply *reply)
{
    m_queue.append(reply);
    schedule();
}

void QueuedWebService::schedule()
{
    if (m_active || m_queue.isEmpty())
        return;

    while (!m_queue.isEmpty()) {
        const QPointer<Reply> first = m_queue.first();
        if (!first || first->isFinished()) {
            m_queue.removeFirst();
            continue;
        }
        break;
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

void QueuedWebService::startNext()
{
    while (!m_queue.isEmpty()) {
        Reply *reply = m_queue.takeFirst();
        if (!reply || reply->isFinished())
            continue;

        m_active = reply;
        connect(reply, &TranslationReply::finished, this, [this, reply] {
            if (m_active == reply)
                m_active = nullptr;
            schedule();
        });
        connect(reply, &QObject::destroyed, this, [this, reply] {
            if (m_active == reply)
                m_active = nullptr;
            schedule();
        });
        reply->start();
        return;
    }
}

void QueuedWebService::noteRequestSent()
{
    m_lastRequest.restart();
}
