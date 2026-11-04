#include "core/translationservice.h"

#include <QMetaObject>

TranslationReply::TranslationReply(QObject *parent)
    : QObject(parent)
{
}

TranslationReply *TranslationReply::createFinished(const TranslationResult &result, QObject *parent)
{
    auto *reply = new TranslationReply(parent);
    // If the reply is aborted or deleted before this runs, the call is a no-op.
    QMetaObject::invokeMethod(
        reply, [reply, result] { reply->finish(result); }, Qt::QueuedConnection);
    return reply;
}

void TranslationReply::abort()
{
    if (m_finished)
        return;
    TranslationResult cancelled;
    cancelled.error = TranslationError::Cancelled;
    finish(cancelled);
    abortImpl();
}

void TranslationReply::finish(const TranslationResult &result)
{
    if (m_finished)
        return;
    m_finished = true;
    m_result = result;
    emit finished();
}
