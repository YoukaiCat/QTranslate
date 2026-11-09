#include "core/proofreadingservice.h"

#include <QMetaObject>

ProofreadingReply::ProofreadingReply(QObject *parent)
    : QObject(parent)
{
}

ProofreadingReply *ProofreadingReply::createFinished(const ProofreadingResult &result,
                                                     QObject *parent)
{
    auto *reply = new ProofreadingReply(parent);
    // If the reply is aborted or deleted before this runs, the call is a no-op.
    QMetaObject::invokeMethod(
        reply, [reply, result] { reply->finish(result); }, Qt::QueuedConnection);
    return reply;
}

void ProofreadingReply::abort()
{
    if (m_finished)
        return;
    ProofreadingResult cancelled;
    cancelled.error = TranslationError::Cancelled;
    finish(cancelled);
    abortImpl();
}

void ProofreadingReply::finish(const ProofreadingResult &result)
{
    if (m_finished)
        return;
    m_finished = true;
    m_result = result;
    emit finished();
}
