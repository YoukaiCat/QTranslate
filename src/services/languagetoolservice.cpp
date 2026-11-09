#include "services/languagetoolservice.h"

#include "core/languages.h"
#include "core/settings.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/languagetoolparser.h"
#include "services/requestqueue.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVector>

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

Q_LOGGING_CATEGORY(lcLanguageTool, "qtranslate.service.languagetool", QtWarningMsg)

namespace {

constexpr QLatin1String kServiceId("languagetool");

// The public API allows about 20 requests/min; a self-hosted server gets a
// short pause so it is not stalled for 3 s between chunks.
constexpr int kPublicRateLimitMs = 3000;
constexpr int kCustomRateLimitMs = 500;

// The stricter per-request limit of the public API.
constexpr int kMaxChunkUnits = 20000;
constexpr QLatin1String kPublicHost("api.languagetool.org");

bool validEndpoint(const QUrl &url)
{
    return url.isValid()
        && (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"))
        && !url.host().isEmpty();
}

// A fixed name for the log: never the text, an issue message or a body.
const char *errorName(TranslationError error)
{
    switch (error) {
    case TranslationError::Auth:
        return "auth";
    case TranslationError::QuotaExceeded:
        return "quota";
    case TranslationError::RateLimited:
        return "rate-limited";
    case TranslationError::ServiceUnavailable:
        return "unavailable";
    case TranslationError::Network:
        return "network";
    case TranslationError::Timeout:
        return "timeout";
    case TranslationError::Cancelled:
        return "cancelled";
    case TranslationError::BadResponse:
        return "bad-response";
    case TranslationError::TooLong:
        return "too-long";
    case TranslationError::UnsupportedLanguage:
        return "unsupported-language";
    case TranslationError::EmptyText:
        return "empty-text";
    case TranslationError::None:
        break;
    }
    return "none";
}

QString messageForHttpStatus(TranslationError error, int status)
{
    switch (error) {
    case TranslationError::Auth:
        return QStringLiteral("the endpoint rejected the request");
    case TranslationError::TooLong:
        return QStringLiteral("the text is too long for LanguageTool");
    case TranslationError::RateLimited:
        return QStringLiteral("the endpoint reported a rate limit");
    case TranslationError::Network:
        return QStringLiteral("the LanguageTool server failed");
    default:
        return QStringLiteral("HTTP %1").arg(status);
    }
}

// Both the name and the value are percent-encoded as UTF-8; a space becomes
// %20, so a literal '+' is never sent as a space.
QByteArray formField(const QString &name, const QString &value)
{
    return QUrl::toPercentEncoding(name) + '=' + QUrl::toPercentEncoding(value);
}

ProofreadingReply *finishedWithError(TranslationError error, const QString &message)
{
    ProofreadingResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    return ProofreadingReply::createFinished(result);
}

} // namespace

class LanguageToolService::Reply : public ProofreadingReply
{
public:
    Reply(NetworkManager *network, QVector<TextChunker::Part> parts, QString languageCode,
          QStringList categories, QUrl endpoint, LanguageToolService *service)
        : m_network(network)
        , m_endpoint(std::move(endpoint))
        , m_parts(std::move(parts))
        , m_languageCode(std::move(languageCode))
        , m_categories(std::move(categories))
        , m_service(service)
    {
    }

    ~Reply() override { stopRequest(); }

    // Starts on the next event loop iteration: finished() must not be emitted
    // from inside check().
    void start()
    {
        m_elapsed.start();
        QMetaObject::invokeMethod(this, [this] { send(); }, Qt::QueuedConnection);
    }

protected:
    void abortImpl() override
    {
        qCDebug(lcLanguageTool) << "Cancelled";
        stopRequest();
    }

private:
    void send();
    void onFinished();
    void finishCheck();
    void fail(TranslationError error, const QString &message);
    void stopRequest();

    QPointer<NetworkManager> m_network;
    QUrl m_endpoint;
    QVector<TextChunker::Part> m_parts;
    QString m_languageCode;
    QStringList m_categories;
    QPointer<LanguageToolService> m_service;
    QPointer<QNetworkReply> m_request;
    QVector<ProofreadingIssue> m_issues;
    qsizetype m_nextPart = 0;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_requestElapsed;
};

void LanguageToolService::Reply::send()
{
    if (isFinished())
        return;
    if (!m_service) {
        fail(TranslationError::Network, QStringLiteral("the service is not available"));
        return;
    }
    if (!m_network) {
        fail(TranslationError::Network, QStringLiteral("network is not available"));
        return;
    }
    if (m_nextPart >= m_parts.size()) {
        finishCheck();
        return;
    }

    const TextChunker::Part &part = m_parts.at(m_nextPart);

    QNetworkRequest request = m_network->makeRequest(m_endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("Accept", "application/json");
    // The attributes are client-side only: the shared cookie jar neither
    // supplies nor receives a cookie for this request.
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    // The text must never move to another host behind the user's back.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);

    QByteArray body;
    body += formField(QStringLiteral("text"), part.text);
    body += '&';
    body += formField(QStringLiteral("language"), m_languageCode);
    body += '&';
    body += formField(QStringLiteral("enabledOnly"), QStringLiteral("false"));

    // Records the request for the per-host pause before it leaves.
    m_service->noteRequestSent();
    m_requestElapsed.start();
    m_request = m_network->nam()->post(request, body);
    connect(m_request, &QNetworkReply::finished, this, &Reply::onFinished);
}

void LanguageToolService::Reply::onFinished()
{
    QNetworkReply *request = m_request;
    m_request = nullptr;
    if (!request)
        return;

    const int status = request->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError error = request->error();
    // A timed-out or refused transfer has no readable device.
    const QByteArray body =
        status != 0 || error == QNetworkReply::NoError ? request->readAll() : QByteArray();
    request->deleteLater();
    if (isFinished())
        return;

    qCDebug(lcLanguageTool) << "HTTP" << status << "in" << m_requestElapsed.elapsed() << "ms";

    if (!m_service) {
        fail(TranslationError::Network, QStringLiteral("the service is not available"));
        return;
    }

    // abort() finishes the reply before stopping the request, so a cancelled
    // request here means the transfer timeout fired.
    if (error == QNetworkReply::OperationCanceledError || error == QNetworkReply::TimeoutError) {
        const double seconds = m_network ? m_network->timeoutMs() / 1000.0 : 0;
        fail(TranslationError::Timeout,
             QStringLiteral("LanguageTool did not respond within %1 s")
                 .arg(QString::number(seconds, 'g', 3)));
        return;
    }
    if (status != 0 && (status < 200 || status >= 300)) {
        const TranslationError statusError = LanguageToolParser::errorForHttpStatus(status);
        fail(statusError, messageForHttpStatus(statusError, status));
        return;
    }
    if (error != QNetworkReply::NoError) {
        fail(TranslationError::Network,
             m_network ? m_network->describeNetworkError(request, false)
                       : QStringLiteral("Network error"));
        return;
    }

    const TextChunker::Part &part = m_parts.at(m_nextPart);
    const ProofreadingResult parsed =
        LanguageToolParser::parseResponse(body, int(part.text.size()));
    if (!parsed.ok()) {
        fail(parsed.error, parsed.errorMessage);
        return;
    }

    // The category filter is client-side and never changes an offset; an empty
    // request list means all four categories.
    for (const ProofreadingIssue &issue : parsed.issues) {
        if (!m_categories.isEmpty() && !m_categories.contains(issue.category))
            continue;
        ProofreadingIssue shifted = issue;
        shifted.offset += part.offset;
        m_issues.append(shifted);
    }
    ++m_nextPart;
    if (m_nextPart >= m_parts.size()) {
        // The check is complete: finish without a trailing pause, so the queue
        // slot is not held for rateLimitMs() after the last response.
        finishCheck();
        return;
    }

    const int pause = m_service->rateLimitMs();
    QTimer::singleShot(pause, this, [this] {
        if (!isFinished())
            send();
    });
}

void LanguageToolService::Reply::finishCheck()
{
    std::sort(m_issues.begin(), m_issues.end(),
              [](const ProofreadingIssue &a, const ProofreadingIssue &b) {
                  if (a.offset != b.offset)
                      return a.offset < b.offset;
                  if (a.length != b.length)
                      return a.length < b.length;
                  return a.ruleId < b.ruleId;
              });

    // A rule or overlapping chunks can repeat an issue exactly; keep the first
    // one. The set removes every duplicate, not only the adjacent ones.
    std::set<std::tuple<int, int, QString, QString>> seen;
    QVector<ProofreadingIssue> unique;
    unique.reserve(m_issues.size());
    for (const ProofreadingIssue &issue : std::as_const(m_issues)) {
        if (seen.emplace(issue.offset, issue.length, issue.ruleId, issue.message).second)
            unique.append(issue);
    }

    ProofreadingResult result;
    result.serviceId = kServiceId;
    result.issues = std::move(unique);
    qCDebug(lcLanguageTool) << "Checked" << m_parts.size() << "chunk(s),"
                            << result.issues.size() << "issue(s) in" << m_elapsed.elapsed()
                            << "ms";
    finish(result);
}

void LanguageToolService::Reply::fail(TranslationError error, const QString &message)
{
    qCWarning(lcLanguageTool) << "Check failed (" << errorName(error) << "):" << message;
    ProofreadingResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    finish(result);
}

void LanguageToolService::Reply::stopRequest()
{
    if (QNetworkReply *request = m_request) {
        m_request = nullptr;
        request->disconnect(this);
        request->abort();
        request->deleteLater();
    }
}

LanguageToolService::LanguageToolService(NetworkManager *network, QObject *parent)
    : ProofreadingService(parent)
    , m_network(network)
    , m_queue(new RequestQueue(this))
    , m_endpoint(defaultEndpoint())
{
    m_queue->setRateLimitMs(kPublicRateLimitMs);
}

QString LanguageToolService::id() const
{
    return kServiceId;
}

QString LanguageToolService::displayName() const
{
    return QStringLiteral("LanguageTool");
}

bool LanguageToolService::isConfigured() const
{
    return true;
}

QStringList LanguageToolService::languages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Source);
}

bool LanguageToolService::supportsAutoLanguage() const
{
    return true;
}

bool LanguageToolService::isLocal() const
{
    return false;
}

void LanguageToolService::applySettings(const AppSettings &settings)
{
    const QUrl configured(settings.proofreading.endpoint);
    m_endpoint = validEndpoint(configured) ? configured : defaultEndpoint();
    m_queue->setRateLimitMs(m_endpoint.host().compare(kPublicHost, Qt::CaseInsensitive) == 0
                                ? kPublicRateLimitMs
                                : kCustomRateLimitMs);
}

ProofreadingReply *LanguageToolService::check(const ProofreadingRequest &request)
{
    if (request.text.trimmed().isEmpty())
        return finishedWithError(TranslationError::EmptyText, QString());

    QString languageCode;
    if (request.language == Languages::autoCode()) {
        // The detector picks the language; LanguageTool accepts the literal
        // "auto" and there is no internal code to map through.
        languageCode = QStringLiteral("auto");
    } else {
        if (!Languages::supported(kServiceId, Languages::Direction::Source)
                 .contains(request.language)) {
            return finishedWithError(
                TranslationError::UnsupportedLanguage,
                QStringLiteral("language \"%1\" is not supported").arg(request.language));
        }
        languageCode = Languages::toServiceCode(kServiceId, request.language,
                                                Languages::Direction::Source);
        if (languageCode.isEmpty()) {
            return finishedWithError(
                TranslationError::UnsupportedLanguage,
                QStringLiteral("language \"%1\" is not supported").arg(request.language));
        }
    }

    const QVector<TextChunker::Part> parts =
        TextChunker::splitWithOffsets(request.text, kMaxChunkUnits);
    if (parts.isEmpty())
        return finishedWithError(TranslationError::EmptyText, QString());

    // Only the part count, the language and the number of categories: never
    // the text itself.
    qCDebug(lcLanguageTool) << "Checking" << parts.size() << "part(s), language"
                            << request.language << "with" << request.categories.size()
                            << "requested category filter(s)";

    auto *reply = new Reply(m_network, parts, languageCode, request.categories, m_endpoint, this);
    enqueue(reply);
    return reply;
}

void LanguageToolService::setEndpoint(const QUrl &url)
{
    m_endpoint = validEndpoint(url) ? url : defaultEndpoint();
}

QUrl LanguageToolService::defaultEndpoint()
{
    return QUrl(QStringLiteral("https://api.languagetool.org/v2/check"));
}

void LanguageToolService::setRateLimitMs(int ms)
{
    m_queue->setRateLimitMs(ms);
}

int LanguageToolService::rateLimitMs() const
{
    return m_queue->rateLimitMs();
}

void LanguageToolService::enqueue(Reply *reply)
{
    connect(reply, &ProofreadingReply::finished, m_queue, [queue = m_queue, reply] {
        queue->taskFinished(reply);
    });
    m_queue->enqueue(reply, [reply] { return reply->isFinished(); },
                     [reply] { reply->start(); });
}

void LanguageToolService::noteRequestSent()
{
    m_queue->noteRequestSent();
}
