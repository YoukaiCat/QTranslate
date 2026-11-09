#include "core/translationmanager.h"

#include "core/languages.h"
#include "core/translationservice.h"

#include <QLoggingCategory>
#include <QMetaObject>

Q_LOGGING_CATEGORY(lcTranslation, "qtranslate.translation", QtWarningMsg)

namespace {

constexpr int kMaxCacheEntries = 200;

// A failure the fallback policy retries once. EmptyText, TooLong, Cancelled,
// BadResponse and UnsupportedLanguage are excluded deliberately: a retry would
// not help or must not happen.
bool isRetryable(TranslationError error)
{
    switch (error) {
    case TranslationError::Auth:
    case TranslationError::QuotaExceeded:
    case TranslationError::RateLimited:
    case TranslationError::ServiceUnavailable:
    case TranslationError::Network:
    case TranslationError::Timeout:
        return true;
    case TranslationError::None:
    case TranslationError::TooLong:
    case TranslationError::UnsupportedLanguage:
    case TranslationError::BadResponse:
    case TranslationError::Cancelled:
    case TranslationError::EmptyText:
        break;
    }
    return false;
}

// A fixed name for the log: never the user text, a URL or an error message.
QString errorName(TranslationError error)
{
    switch (error) {
    case TranslationError::Auth:
        return QStringLiteral("auth");
    case TranslationError::QuotaExceeded:
        return QStringLiteral("quota");
    case TranslationError::RateLimited:
        return QStringLiteral("rate-limited");
    case TranslationError::ServiceUnavailable:
        return QStringLiteral("unavailable");
    case TranslationError::Network:
        return QStringLiteral("network");
    case TranslationError::Timeout:
        return QStringLiteral("timeout");
    case TranslationError::Cancelled:
        return QStringLiteral("cancelled");
    case TranslationError::BadResponse:
        return QStringLiteral("bad-response");
    case TranslationError::TooLong:
        return QStringLiteral("too-long");
    case TranslationError::UnsupportedLanguage:
        return QStringLiteral("unsupported-language");
    case TranslationError::EmptyText:
        return QStringLiteral("empty-text");
    case TranslationError::None:
        break;
    }
    return QStringLiteral("none");
}

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

// Wraps a service reply: smart retry on the secondary language, a one-hop
// fallback on a retryable error, final-result caching, and cancellation of the
// underlying request.
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

    // For errors detected before any request (an unconfigured service): the
    // fallback policy still applies, but finished() must not be emitted from
    // inside translate(), so the first step goes through the event loop.
    void startWithError(const TranslationResult &result)
    {
        QMetaObject::invokeMethod(
            this,
            [this, result] {
                if (!isFinished())
                    onPrimaryError(result);
            },
            Qt::QueuedConnection);
    }

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
            onPrimaryError(result);
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

    // The primary failed, an early or a real result. The fallback is resolved
    // here, at failure time, so a gate or fallback change made while the
    // primary was in flight is honoured. One hop only: a fallback failure
    // finishes with the primary error and never starts another request.
    void onPrimaryError(const TranslationResult &result)
    {
        if (!m_fallbackStarted && isRetryable(result.error)) {
            if (TranslationService *fallback = m_manager->fallbackFor(m_service)) {
                m_primaryError = result;
                m_fallbackStarted = true;
                m_fallback = fallback;
                qCWarning(lcTranslation)
                    << "Primary service" << m_service->id()
                    << QStringLiteral("failed (%1); trying fallback").arg(errorName(result.error))
                    << fallback->id();
                m_inner = fallback->translate(m_request);
                if (!m_inner) {
                    // A service returning no reply is a defect, not a retryable
                    // error: report the primary error.
                    qCWarning(lcTranslation) << "Fallback" << fallback->id()
                                             << "returned no reply";
                    finishResult(m_primaryError);
                    return;
                }
                m_inner->setParent(this);
                connect(m_inner, &TranslationReply::finished, this, &Reply::onFallbackFinished);
                return;
            }
        }
        finishResult(result);
    }

    void onFallbackFinished()
    {
        TranslationReply *inner = m_inner;
        m_inner = nullptr;
        if (!inner || isFinished())
            return;
        const TranslationResult result = inner->result();
        inner->deleteLater();

        // One failure is one fallback request, not two: the fallback never gets
        // the smart secondary-language retry, so a success is returned as-is
        // even when detectedSourceLang equals the primary language.
        if (result.ok()) {
            // The result keeps the fallback's serviceId; finishResult() stores
            // it under the primary cache key through m_cacheKey.
            qCWarning(lcTranslation) << "Fallback" << m_fallback->id() << "succeeded";
            finishResult(result);
            return;
        }
        qCWarning(lcTranslation) << "Fallback" << m_fallback->id()
                                 << QStringLiteral("failed (%1)").arg(errorName(result.error));
        finishResult(m_primaryError);
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
    TranslationService *m_fallback = nullptr;
    TranslationResult m_firstResult;
    TranslationResult m_primaryError;
    bool m_smartRetry = false;
    bool m_fallbackStarted = false;
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

void TranslationManager::setFreeServicesAllowed(bool allowed)
{
    if (m_freeServicesAllowed == allowed)
        return;
    m_freeServicesAllowed = allowed;
    if (!allowed)
        clearCache();
}

void TranslationManager::setFallbackService(const QString &id)
{
    const QString normalized = id.trimmed().toLower();
    if (normalized == m_fallbackService)
        return;
    m_fallbackService = normalized;
    clearCache();
}

TranslationService *TranslationManager::fallbackFor(TranslationService *primary) const
{
    if (m_fallbackService.isEmpty() || !m_freeServicesAllowed)
        return nullptr;
    TranslationService *fallback = service(m_fallbackService);
    if (!fallback || fallback == primary)
        return nullptr;
    if (primary && fallback->id() == primary->id())
        return nullptr;
    if (!fallback->requiresOptIn())
        return nullptr;
    return fallback;
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
        TranslationResult result;
        result.serviceId = serviceId;
        result.error = TranslationError::Auth;
        result.errorMessage = hint.isEmpty()
            ? QStringLiteral("%1 needs an API key").arg(m_active->displayName())
            : hint;
        auto *managed = new Reply(this, m_active, request, cacheKey(serviceId, request));
        reply = managed;
        current(channel) = reply;
        managed->startWithError(result);
        return reply;
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
