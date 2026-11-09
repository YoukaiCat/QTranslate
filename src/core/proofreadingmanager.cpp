#include "core/proofreadingmanager.h"

#include "core/languages.h"
#include "core/proofreadingservice.h"

#include <QElapsedTimer>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcProofreading, "qtranslate.proofreading", QtWarningMsg)

namespace {

// A fixed name for the log: never the user text, an issue message, a URL or a key.
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

QString channelName(ProofreadingManager::Channel channel)
{
    switch (channel) {
    case ProofreadingManager::Channel::InPlace:
        return QStringLiteral("in-place");
    case ProofreadingManager::Channel::MainWindow:
        break;
    }
    return QStringLiteral("main-window");
}

// Only ids, the channel, the language/categories and the error kind: never the
// text, an issue message, a replacement, a URL or a key.
void logCheck(const QString &serviceId, ProofreadingManager::Channel channel,
              const QString &language, const QStringList &categories,
              const ProofreadingResult &result, qint64 elapsedMs)
{
    const QString details =
        QStringLiteral("channel=%1 provider=%2 language=%3 categories=%4 error=%5 elapsed=%6ms")
            .arg(channelName(channel),
                 serviceId.isEmpty() ? QStringLiteral("-") : serviceId,
                 language.isEmpty() ? QStringLiteral("-") : language,
                 categories.isEmpty() ? QStringLiteral("-")
                                      : categories.join(QLatin1Char(',')),
                 errorName(result.error))
            .arg(elapsedMs);
    if (result.error == TranslationError::None || result.error == TranslationError::Cancelled)
        qCDebug(lcProofreading) << details;
    else
        qCWarning(lcProofreading) << details;
}

ProofreadingReply *finishedWithError(const QString &serviceId, TranslationError error,
                                     const QString &message)
{
    ProofreadingResult result;
    result.serviceId = serviceId;
    result.error = error;
    result.errorMessage = message;
    return ProofreadingReply::createFinished(result);
}

} // namespace

ProofreadingManager::ProofreadingManager(QObject *parent)
    : QObject(parent)
{
}

ProofreadingManager::~ProofreadingManager() = default;

void ProofreadingManager::registerService(ProofreadingService *service)
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

ProofreadingService *ProofreadingManager::service(const QString &id) const
{
    for (ProofreadingService *s : m_services) {
        if (s->id() == id)
            return s;
    }
    return nullptr;
}

void ProofreadingManager::setActiveService(const QString &id)
{
    ProofreadingService *s = service(id);
    if (!s) {
        qCWarning(lcProofreading) << "Unknown service" << id;
        return;
    }
    if (s == m_active)
        return;
    m_active = s;
    emit activeServiceChanged(id);
}

void ProofreadingManager::setOnlineServicesAllowed(bool allowed)
{
    m_onlineServicesAllowed = allowed;
}

ProofreadingReply *ProofreadingManager::check(const ProofreadingRequest &request,
                                              Channel channel)
{
    cancel(channel);

    QElapsedTimer timer;
    timer.start();

    const QString serviceId = m_active ? m_active->id() : QString();
    ProofreadingReply *reply = nullptr;

    if (request.text.trimmed().isEmpty()) {
        reply = finishedWithError(serviceId, TranslationError::EmptyText, QString());
    } else if (!m_active) {
        reply = finishedWithError(serviceId, TranslationError::ServiceUnavailable,
                                  tr("No text checking provider is available."));
    } else if (!m_active->isConfigured()) {
        const QString hint = m_active->configurationError();
        reply = finishedWithError(serviceId, TranslationError::Auth,
                                  hint.isEmpty()
                                      ? tr("The text checking provider needs a key.")
                                      : hint);
    } else if (!m_active->isLocal() && !m_onlineServicesAllowed) {
        reply = finishedWithError(
            serviceId, TranslationError::ServiceUnavailable,
            tr("Enable online text checking in Settings → Text checking."));
    } else if ((request.language == Languages::autoCode()
                && !m_active->supportsAutoLanguage())
               || (request.language != Languages::autoCode()
                   && !m_active->languages().contains(request.language))) {
        reply = finishedWithError(serviceId, TranslationError::UnsupportedLanguage,
                                  QStringLiteral("language \"%1\" is not supported")
                                      .arg(request.language));
    } else {
        reply = m_active->check(request);
        if (!reply) {
            // A service returning no reply is a defect: report it instead of
            // handing the caller a null pointer.
            reply = finishedWithError(serviceId, TranslationError::BadResponse,
                                      QStringLiteral("no reply from the service"));
        }
    }

    // The outcome is logged when the reply finishes, so a pre-network refusal
    // and a provider answer are treated the same way. Only the fields that may
    // be logged are captured, not the checked text.
    const QString language = request.language;
    const QStringList categories = request.categories;
    connect(reply, &ProofreadingReply::finished, this,
            [reply, serviceId, channel, language, categories, timer] {
                logCheck(serviceId, channel, language, categories, reply->result(),
                         timer.elapsed());
            });
    current(channel) = reply;
    return reply;
}

void ProofreadingManager::cancel(Channel channel)
{
    if (ProofreadingReply *previous = current(channel)) {
        if (!previous->isFinished()) {
            qCDebug(lcProofreading) << "Cancelling a stale check in channel"
                                    << channelName(channel);
            previous->abort();
        }
    }
    current(channel) = nullptr;
}

void ProofreadingManager::cancelAll()
{
    cancel(Channel::MainWindow);
    cancel(Channel::InPlace);
}

QPointer<ProofreadingReply> &ProofreadingManager::current(Channel channel)
{
    switch (channel) {
    case Channel::InPlace:
        return m_inPlaceReply;
    case Channel::MainWindow:
        break;
    }
    return m_mainWindowReply;
}
