#include "core/translationmanager.h"

#include "core/languages.h"
#include "core/translationservice.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcTranslation, "qtranslate.translation", QtWarningMsg)

namespace {

constexpr int kMaxCacheEntries = 200;

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

// Wraps a service reply: smart retry on the secondary language, final-result
// caching, and cancellation of the underlying request.
class TranslationManager::Reply : public TranslationReply
{
public:
    Reply(TranslationManager *manager, TranslationService *service,
          const TranslationRequest &request, const QString &cacheKey)
        : m_manager(manager)
        , m_service(service)
        , m_request(request)
        , m_cacheKey(cacheKey)
    {
    }

    void start() { send(m_request); }

protected:
    void abortImpl() override
    {
        if (TranslationReply *inner = m_inner) {
            m_inner = nullptr;
            inner->disconnect(this);
            inner->abort();
            inner->deleteLater();
        }
    }

private:
    void send(const TranslationRequest &request)
    {
        if (isFinished())
            return;
        m_inner = m_service->translate(request);
        if (!m_inner) {
            TranslationResult result;
            result.serviceId = m_service->id();
            result.error = TranslationError::BadResponse;
            result.errorMessage = QStringLiteral("no reply from the service");
            finishResult(result);
            return;
        }
        m_inner->setParent(this);
        connect(m_inner, &TranslationReply::finished, this, &Reply::onInnerFinished);
    }

    void onInnerFinished()
    {
        TranslationReply *inner = m_inner;
        m_inner = nullptr;
        if (!inner || isFinished())
            return;
        const TranslationResult result = inner->result();
        inner->deleteLater();

        if (m_smartRetry) {
            finishResult(result.ok() ? result : m_firstResult);
            return;
        }

        if (!result.ok()) {
            finishResult(result);
            return;
        }

        if (m_request.sourceLang == Languages::autoCode()
            && !m_manager->m_primaryLanguage.isEmpty()
            && m_manager->m_primaryLanguage != m_manager->m_secondaryLanguage
            && m_request.targetLang == m_manager->m_primaryLanguage
            && result.detectedSourceLang == m_manager->m_primaryLanguage) {
            m_firstResult = result;
            m_smartRetry = true;
            TranslationRequest retry = m_request;
            retry.targetLang = m_manager->m_secondaryLanguage;
            send(retry);
            return;
        }

        finishResult(result);
    }

    void finishResult(const TranslationResult &result)
    {
        if (result.ok())
            m_manager->storeCached(m_cacheKey, result);
        finish(result);
    }

    TranslationManager *m_manager = nullptr;
    TranslationService *m_service = nullptr;
    TranslationRequest m_request;
    QString m_cacheKey;
    QPointer<TranslationReply> m_inner;
    TranslationResult m_firstResult;
    bool m_smartRetry = false;
};

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

void TranslationManager::setPrimaryLanguage(const QString &code)
{
    if (m_primaryLanguage == code)
        return;
    m_primaryLanguage = code;
    clearCache();
}

void TranslationManager::setSecondaryLanguage(const QString &code)
{
    if (m_secondaryLanguage == code)
        return;
    m_secondaryLanguage = code;
    clearCache();
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
    } else if (m_active->requiresOptIn() && !m_freeServicesAllowed) {
        reply = finishedWithError(
            serviceId, TranslationError::ServiceUnavailable,
            TranslationManager::tr("Enable free web services in Settings → Services."));
    } else if (!m_active->isConfigured()) {
        const QString hint = m_active->configurationError();
        reply = finishedWithError(
            serviceId, TranslationError::Auth,
            hint.isEmpty() ? QStringLiteral("%1 needs an API key").arg(m_active->displayName())
                           : hint);
    } else {
        const QString key = cacheKey(serviceId, request);
        TranslationResult cachedResult;
        if (cached(key, &cachedResult)) {
            qCDebug(lcTranslation) << "Cache hit";
            reply = TranslationReply::createFinished(cachedResult);
        } else {
            auto *managed = new Reply(this, m_active, request, key);
            reply = managed;
            current(channel) = reply;
            managed->start();
            return reply;
        }
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
    cancel(Channel::InPlace);
}

QString TranslationManager::cacheKey(const QString &serviceId,
                                     const TranslationRequest &request) const
{
    return serviceId + QLatin1Char('|') + request.sourceLang + QLatin1Char('|')
        + request.targetLang + QLatin1Char('|') + request.text;
}

bool TranslationManager::cached(const QString &key, TranslationResult *result)
{
    const auto it = m_cache.constFind(key);
    if (it == m_cache.cend())
        return false;
    *result = it.value();
    m_cacheOrder.removeAll(key);
    m_cacheOrder.append(key);
    return true;
}

void TranslationManager::storeCached(const QString &key, const TranslationResult &result)
{
    if (m_cache.contains(key))
        m_cacheOrder.removeAll(key);
    m_cache.insert(key, result);
    m_cacheOrder.append(key);
    while (m_cacheOrder.size() > kMaxCacheEntries) {
        const QString oldest = m_cacheOrder.takeFirst();
        m_cache.remove(oldest);
    }
}

void TranslationManager::clearCache()
{
    m_cache.clear();
    m_cacheOrder.clear();
}

QPointer<TranslationReply> &TranslationManager::current(Channel channel)
{
    switch (channel) {
    case Channel::Popup:
        return m_popupReply;
    case Channel::InPlace:
        return m_inPlaceReply;
    case Channel::MainWindow:
        break;
    }
    return m_mainWindowReply;
}
