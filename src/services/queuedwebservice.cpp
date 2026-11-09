#include "services/queuedwebservice.h"

#include "services/requestqueue.h"

QueuedWebService::QueuedWebService(QObject *parent)
    : TranslationService(parent)
    , m_queue(new RequestQueue(this))
{
}

void QueuedWebService::setRateLimitMs(int ms)
{
    m_queue->setRateLimitMs(ms);
}

int QueuedWebService::rateLimitMs() const
{
    return m_queue->rateLimitMs();
}

void QueuedWebService::enqueue(Reply *reply)
{
    connect(reply, &TranslationReply::finished, m_queue, [queue = m_queue, reply] {
        queue->taskFinished(reply);
    });
    m_queue->enqueue(reply, [reply] { return reply->isFinished(); },
                     [reply] { reply->start(); });
}

void QueuedWebService::noteRequestSent()
{
    m_queue->noteRequestSent();
}
