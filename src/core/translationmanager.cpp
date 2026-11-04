#include "core/translationmanager.h"

#include "core/translationservice.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcTranslation, "qtranslate.translation", QtWarningMsg)

namespace {

TranslationReply *finishedWithError(const QString &serviceId, TranslationError error,
                                    const QString &message)
{
    TranslationResult result;
    result.serviceId = serviceId;
    result.error = error;
    result.errorMessage = message;
    return TranslationReply::createFinished(result);
}

} // namespace

TranslationManager::TranslationManager(QObject *parent)
    : QObject(parent)
{
}

TranslationManager::~TranslationManager() = default;

void TranslationManager::registerService(TranslationService *service)
{
    Q_ASSERT(service);
    Q_ASSERT(!this->service(service->id()));
    service->setParent(this);
    m_services.append(service);
    if (!m_active) {
        m_active = service;
        emit activeServiceChanged(service->id());
    }
}

TranslationService *TranslationManager::service(const QString &id) const
{
    for (TranslationService *s : m_services) {
        if (s->id() == id)
            return s;
    }
    return nullptr;
}

void TranslationManager::setActiveService(const QString &id)
{
    TranslationService *s = service(id);
    if (!s) {
        qCWarning(lcTranslation) << "Unknown service" << id;
        return;
    }
    if (s == m_active)
        return;
    m_active = s;
    emit activeServiceChanged(id);
}

void TranslationManager::setMaxLength(int length)
{
    m_maxLength = length > 0 ? length : kDefaultMaxLength;
}

TranslationReply *TranslationManager::translate(const TranslationRequest &request, Channel channel)
{
    cancel(channel);

    const QString serviceId = m_active ? m_active->id() : QString();
    TranslationReply *reply = nullptr;
    if (!m_active) {
        reply = finishedWithError(serviceId, TranslationError::BadResponse,
                                  QStringLiteral("no translation service"));
    } else if (request.text.trimmed().isEmpty()) {
        reply = finishedWithError(serviceId, TranslationError::EmptyText, QString());
    } else if (request.text.size() > m_maxLength) {
        reply = finishedWithError(serviceId, TranslationError::TooLong,
                                  QStringLiteral("%1 characters, the limit is %2")
                                      .arg(request.text.size())
                                      .arg(m_maxLength));
    } else if (!m_active->isConfigured()) {
        reply = finishedWithError(
            serviceId, TranslationError::Auth,
            QStringLiteral("%1 needs an API key").arg(m_active->displayName()));
    } else {
        reply = m_active->translate(request);
    }

    current(channel) = reply;
    return reply;
}

void TranslationManager::cancel(Channel channel)
{
    if (TranslationReply *previous = current(channel)) {
        if (!previous->isFinished()) {
            qCDebug(lcTranslation) << "Cancelling a stale request in channel" << int(channel);
            previous->abort();
        }
    }
    current(channel) = nullptr;
}

void TranslationManager::cancelAll()
{
    cancel(Channel::MainWindow);
    cancel(Channel::Popup);
}

QPointer<TranslationReply> &TranslationManager::current(Channel channel)
{
    return channel == Channel::Popup ? m_popupReply : m_mainWindowReply;
}
