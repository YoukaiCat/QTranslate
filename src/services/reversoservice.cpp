#include "services/reversoservice.h"

#include "core/languages.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/reversoparser.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStringList>
#include <QTimer>

#include <utility>

Q_LOGGING_CATEGORY(lcReverso, "qtranslate.service.reverso", QtWarningMsg)

namespace {

const QString kServiceId = QStringLiteral("reverso");

// The doc gives no Reverso-specific request limit. 5000 UTF-16 code units is a
// conservative assumption, not a measured value; change it only after a
// measurement.
constexpr int kMaxChunkUnits = 5000;

// The doc gives no measured Reverso-specific pause either. 600 ms between
// every request to the scraped host is a named assumption; the shared queue
// still keeps one active request per host.
constexpr int kDefaultRateLimitMs = 600;

// A chunk keeps its edge whitespace out of the request and gets it back when
// the parts are joined, so the formatting at the joints survives. A part that
// is only whitespace is never sent.
struct Part
{
    QString leading;
    QString text;
    QString trailing;
    bool sent = false;
};

QList<Part> splitParts(const QString &text)
{
    QList<Part> result;
    for (const QString &part : TextChunker::split(text, kMaxChunkUnits)) {
        qsizetype begin = 0;
        while (begin < part.size() && part.at(begin).isSpace())
            ++begin;
        if (begin == part.size()) {
            result.append({part, QString(), QString(), false});
            continue;
        }
        qsizetype end = part.size();
        while (part.at(end - 1).isSpace())
            --end;
        result.append({part.left(begin), part.mid(begin, end - begin), part.mid(end), false});
    }
    return result;
}

// The fields are documented in doc/free-endpoints.md, §4. `source` is the text,
// `direction` is `<source>-<target>`; all seven values are JSON strings, so
// `deviceId` and `appId` are `"0"`, not `0`. The compact serializer writes
// UTF-8, so spaces and non-ASCII text stay readable inside the string.
QByteArray jsonBody(const QString &text, const QString &direction)
{
    QJsonObject body;
    body.insert(QStringLiteral("source"), text);
    body.insert(QStringLiteral("direction"), direction);
    body.insert(QStringLiteral("deviceId"), QStringLiteral("0"));
    body.insert(QStringLiteral("uiLang"), QStringLiteral("en"));
    body.insert(QStringLiteral("origin"), QStringLiteral("chromeextension"));
    body.insert(QStringLiteral("accessToken"), QString());
    body.insert(QStringLiteral("appId"), QStringLiteral("0"));
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

TranslationReply *finishedWithError(TranslationError error, const QString &message)
{
    TranslationResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    return TranslationReply::createFinished(result);
}

// The log line names only the error kind: the provider message may echo the
// source text, so it must never reach the log.
const char *errorKind(TranslationError error)
{
    switch (error) {
    case TranslationError::None:
        return "none";
    case TranslationError::Network:
        return "network";
    case TranslationError::Timeout:
        return "timeout";
    case TranslationError::Auth:
        return "auth";
    case TranslationError::QuotaExceeded:
        return "quota-exceeded";
    case TranslationError::RateLimited:
        return "rate-limited";
    case TranslationError::ServiceUnavailable:
        return "unavailable";
    case TranslationError::TooLong:
        return "too-long";
    case TranslationError::UnsupportedLanguage:
        return "unsupported-language";
    case TranslationError::BadResponse:
        return "bad-response";
    case TranslationError::Cancelled:
        return "cancelled";
    case TranslationError::EmptyText:
        return "empty-text";
    }
    return "unknown";
}

} // namespace

class ReversoService::Reply : public QueuedWebService::Reply
{
public:
    Reply(NetworkManager *network, QUrl endpoint, QList<Part> parts, QString direction,
          ReversoService *service)
        : m_network(network)
        , m_endpoint(std::move(endpoint))
        , m_parts(std::move(parts))
        , m_direction(std::move(direction))
        , m_service(service)
    {
    }

    ~Reply() override { stopRequest(); }

    // Starts on the next event loop iteration: finished() must not be emitted
    // from inside translate().
    void start() override
    {
        m_elapsed.start();
        QMetaObject::invokeMethod(this, [this] { send(); }, Qt::QueuedConnection);
    }

protected:
    void abortImpl() override
    {
        qCDebug(lcReverso) << "Cancelled";
        stopRequest();
    }

private:
    void send();
    void onFinished();
    void scheduleNext();
    void finishTranslation();
    void fail(TranslationError error, const QString &message);
    void stopRequest();
    qsizetype nextUnsentPart() const;

    QPointer<NetworkManager> m_network;
    QUrl m_endpoint;
    QList<Part> m_parts;
    QString m_direction;
    QPointer<ReversoService> m_service;
    QPointer<QNetworkReply> m_request;
    QStringList m_translations;
    QString m_detected;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_requestElapsed;
    qsizetype m_current = -1;
};

qsizetype ReversoService::Reply::nextUnsentPart() const
{
    for (qsizetype i = 0; i < m_parts.size(); ++i) {
        const Part &part = m_parts.at(i);
        if (!part.sent && !part.text.isEmpty())
            return i;
    }
    return -1;
}

void ReversoService::Reply::send()
{
    if (isFinished())
        return;
    if (!m_network) {
        fail(TranslationError::Network, QStringLiteral("network is not available"));
        return;
    }
    if (!m_service) {
        fail(TranslationError::Network, QStringLiteral("the service is not available"));
        return;
    }

    const qsizetype index = nextUnsentPart();
    if (index < 0) {
        finishTranslation();
        return;
    }
    m_current = index;

    const QByteArray body = jsonBody(m_parts.at(index).text, m_direction);

    QNetworkRequest request = m_network->makeRequest(m_endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    // Overrides AppInfo::userAgent() for this request only; the other services
    // and the shared makeRequest() keep the application User-Agent.
    request.setHeader(QNetworkRequest::UserAgentHeader, browserUserAgent());
    // The attributes are client-side only: the shared cookie jar neither
    // supplies nor receives a cookie for these requests.
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    // The text must never move to another host behind the user's back.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);

    // Records the request for the per-host pause before it leaves.
    m_service->noteRequestSent();

    m_requestElapsed.start();
    m_request = m_network->nam()->post(request, body);
    connect(m_request, &QNetworkReply::finished, this, &Reply::onFinished);
}

void ReversoService::Reply::onFinished()
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

    qCDebug(lcReverso) << "HTTP" << status << "in" << m_requestElapsed.elapsed() << "ms";

    // abort() finishes the reply before stopping the request, so a cancelled
    // request here means the transfer timeout fired.
    if (error == QNetworkReply::OperationCanceledError || error == QNetworkReply::TimeoutError) {
        const double seconds = m_network ? m_network->timeoutMs() / 1000.0 : 0;
        fail(TranslationError::Timeout,
             QStringLiteral("Reverso did not respond within %1 s")
                 .arg(QString::number(seconds, 'g', 3)));
        return;
    }
    if (status != 0 && (status < 200 || status >= 300)) {
        // The body is classified before the generic status rule: the endpoint
        // may report a rate limit or a block page with an arbitrary status.
        const TranslationResult parsedError = ReversoParser::parseErrorBody(body);
        if (!parsedError.ok()) {
            fail(parsedError.error, parsedError.errorMessage);
            return;
        }
        fail(ReversoParser::errorForHttpStatus(status), QStringLiteral("HTTP %1").arg(status));
        return;
    }
    if (error != QNetworkReply::NoError) {
        // Neither the body, the URL nor QNetworkReply::errorString() is used:
        // the request body contains the text.
        fail(TranslationError::Network,
             m_network ? m_network->describeNetworkError(request, false)
                       : QStringLiteral("Network error"));
        return;
    }

    const ReversoParser::Response parsed = ReversoParser::parseResponse(body);
    if (!parsed.result.ok()) {
        fail(parsed.result.error, parsed.result.errorMessage);
        return;
    }

    m_translations.append(parsed.result.translatedText);
    if (m_detected.isEmpty())
        m_detected = parsed.result.detectedSourceLang;
    // The part is marked only now: a failed chunk fails the whole reply and is
    // never counted as translated.
    m_parts[m_current].sent = true;
    m_current = nextUnsentPart();
    if (m_current < 0) {
        finishTranslation();
        return;
    }
    scheduleNext();
}

void ReversoService::Reply::scheduleNext()
{
    const int pause = m_service ? m_service->rateLimitMs() : 0;
    QTimer::singleShot(pause, this, [this] {
        if (!isFinished())
            send();
    });
}

void ReversoService::Reply::finishTranslation()
{
    int sentCount = 0;
    for (const Part &part : std::as_const(m_parts)) {
        if (part.sent)
            ++sentCount;
    }
    if (m_translations.size() != sentCount) {
        fail(TranslationError::BadResponse,
             QStringLiteral("expected %1 translations, got %2")
                 .arg(sentCount)
                 .arg(m_translations.size()));
        return;
    }

    TranslationResult result;
    result.serviceId = kServiceId;
    result.detectedSourceLang = m_detected;
    int index = 0;
    for (const Part &part : std::as_const(m_parts)) {
        if (part.sent)
            result.translatedText += part.leading + m_translations.at(index++) + part.trailing;
        else
            result.translatedText += part.leading;
    }

    qCDebug(lcReverso) << "Translated" << m_parts.size() << "part(s)," << m_direction << "in"
                       << m_elapsed.elapsed() << "ms";
    finish(result);
}

void ReversoService::Reply::fail(TranslationError error, const QString &message)
{
    // Only the kind is logged: the provider message may repeat the source text.
    qCWarning(lcReverso) << "Translation failed:" << errorKind(error);
    TranslationResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    finish(result);
}

void ReversoService::Reply::stopRequest()
{
    if (QNetworkReply *request = m_request) {
        m_request = nullptr;
        request->disconnect(this);
        request->abort();
        request->deleteLater();
    }
}

ReversoService::ReversoService(NetworkManager *network, QObject *parent)
    : QueuedWebService(parent)
    , m_network(network)
    , m_endpoint(defaultEndpoint())
{
    setRateLimitMs(kDefaultRateLimitMs);
}

QString ReversoService::id() const
{
    return kServiceId;
}

QString ReversoService::displayName() const
{
    return QStringLiteral("Reverso");
}

bool ReversoService::isConfigured() const
{
    return true;
}

bool ReversoService::requiresOptIn() const
{
    return true;
}

QStringList ReversoService::sourceLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Source);
}

QStringList ReversoService::targetLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Target);
}

void ReversoService::applySettings(const AppSettings &)
{
    // No Reverso setting affects the protocol: there is no key, no per-service
    // URL and no mode.
}

TranslationReply *ReversoService::translate(const TranslationRequest &request)
{
    if (request.text.trimmed().isEmpty())
        return finishedWithError(TranslationError::EmptyText, QString());

    // The protocol has no automatic detection: the direction is built from an
    // explicit source, so "auto" is rejected before any request.
    if (request.sourceLang == Languages::autoCode()) {
        return finishedWithError(
            TranslationError::UnsupportedLanguage,
            QStringLiteral("Reverso needs an explicit source language; choose one instead of "
                           "Auto-detect"));
    }

    const QString source =
        Languages::toServiceCode(kServiceId, request.sourceLang, Languages::Direction::Source);
    const QString target =
        Languages::toServiceCode(kServiceId, request.targetLang, Languages::Direction::Target);
    if (source.isEmpty() || target.isEmpty()) {
        const QString code = source.isEmpty() ? request.sourceLang : request.targetLang;
        return finishedWithError(TranslationError::UnsupportedLanguage,
                                 QStringLiteral("language \"%1\" is not supported").arg(code));
    }

    const QString direction = source + QLatin1Char('-') + target;
    const QList<Part> parts = splitParts(request.text);
    qCDebug(lcReverso) << "Translating" << parts.size() << "part(s)," << source << "->" << target;

    auto *reply = new Reply(m_network, m_endpoint, parts, direction, this);
    enqueue(reply);
    return reply;
}

void ReversoService::setEndpoint(const QUrl &url)
{
    m_endpoint = !url.isEmpty() && url.isValid() ? url : defaultEndpoint();
}

QUrl ReversoService::endpoint() const
{
    return m_endpoint;
}

QUrl ReversoService::defaultEndpoint()
{
    return QUrl(QStringLiteral("https://cps.reverso.net/api2/TranslateText"));
}

QString ReversoService::browserUserAgent()
{
    return QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                          "(KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36");
}
