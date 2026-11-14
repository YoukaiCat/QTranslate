#include "services/duckduckgoservice.h"

#include "core/languages.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/duckduckgoparser.h"

#include <QElapsedTimer>
#include <QList>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrlQuery>

#include <utility>

Q_LOGGING_CATEGORY(lcDuckDuckGo, "qtranslate.service.duckduckgo", QtWarningMsg)

namespace {

const QString kServiceId = QStringLiteral("duckduckgo");

// The research accepted 986 characters and rejected 2006 with `invalid_text`,
// so the real limit lies between them. 900 UTF-16 code units is deliberately
// below the lowest proven success, and a UTF-16 unit is never more than one
// character.
constexpr int kMaxChunkUnits = 900;

// The research measured no pause. One request per second to a scraped frontend
// is a conservative named assumption; the shared queue still keeps one active
// request per host.
constexpr int kDefaultRateLimitMs = 1000;

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

class DuckDuckGoService::Reply : public QueuedWebService::Reply
{
public:
    Reply(NetworkManager *network, QUrl endpoint, QList<Part> parts, QString source,
          QString target, DuckDuckGoService *service)
        : m_network(network)
        , m_endpoint(std::move(endpoint))
        , m_parts(std::move(parts))
        , m_source(std::move(source))
        , m_target(std::move(target))
        , m_service(service)
    {
        m_current = nextUnsentPart();
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
        qCDebug(lcDuckDuckGo) << "Cancelled";
        stopRequest();
    }

private:
    // The GET produces the token, the POST consumes it. The stage decides how
    // the finished request is interpreted; only one of the two is in flight at
    // a time.
    enum class Stage { TokenPage, Post };

    void send();
    QNetworkRequest preparedRequest(const QUrl &url) const;
    void fetchTokenPage();
    void postChunk();
    void onFinished();
    void finishTokenPage(QNetworkReply *request, int status, const QByteArray &body);
    void finishPost(QNetworkReply *request, int status, const QByteArray &body);
    void handleTokenRejection();
    void scheduleSend();
    void finishTranslation();
    void fail(TranslationError error, const QString &message);
    void stopRequest();
    qsizetype nextUnsentPart() const;

    QPointer<NetworkManager> m_network;
    QUrl m_endpoint;
    QList<Part> m_parts;
    QString m_source; // empty for auto-detect: the route detects it itself
    QString m_target;
    QPointer<DuckDuckGoService> m_service;
    QPointer<QNetworkReply> m_request;
    QStringList m_translations;
    QString m_detected;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_requestElapsed;
    Stage m_stage = Stage::TokenPage;
    qsizetype m_current = -1;
    // The token value used by the last POST; a 403 must not clear a token that
    // another reply already replaced.
    QString m_usedToken;
    // One refresh per reply, not per chunk: a second 403 is a block.
    bool m_refreshed = false;
};

qsizetype DuckDuckGoService::Reply::nextUnsentPart() const
{
    for (qsizetype i = 0; i < m_parts.size(); ++i) {
        const Part &part = m_parts.at(i);
        if (!part.sent && !part.text.isEmpty())
            return i;
    }
    return -1;
}

void DuckDuckGoService::Reply::send()
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
    if (m_current < 0) {
        finishTranslation();
        return;
    }

    // The token is cached on the service and has no lifetime of its own: it is
    // fetched once and refreshed only when the endpoint rejects it.
    if (m_service->m_token.isEmpty()) {
        fetchTokenPage();
        return;
    }

    postChunk();
}

void DuckDuckGoService::Reply::fetchTokenPage()
{
    m_stage = Stage::TokenPage;

    // The endpoint is the site root; the token page is the same URL with the
    // documented search query.
    QUrl url = m_endpoint;
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("q"), QStringLiteral("translate"));
    url.setQuery(query);

    QNetworkRequest request = preparedRequest(url);
    request.setRawHeader("Accept", "text/html");

    // Records the request for the per-host pause before it leaves.
    m_service->noteRequestSent();
    m_requestElapsed.start();
    m_request = m_network->nam()->get(request);
    connect(m_request, &QNetworkReply::finished, this, &Reply::onFinished);
}

// Every request to the scraped host shares the browser identity, the manual
// cookie attributes and the same-origin policy. The attributes are client-side
// only: the jar neither supplies nor receives a cookie for these requests.
QNetworkRequest DuckDuckGoService::Reply::preparedRequest(const QUrl &url) const
{
    QNetworkRequest request = m_network->makeRequest(url);
    // Overrides AppInfo::userAgent() for this request only; the other services
    // and the shared makeRequest() keep the application User-Agent.
    request.setHeader(QNetworkRequest::UserAgentHeader, browserUserAgent());
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    // The page and the POST must stay on the same host; a redirect must not
    // carry the text or the token to another origin.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    return request;
}

void DuckDuckGoService::Reply::postChunk()
{
    QUrl url = m_endpoint.resolved(QUrl(QStringLiteral("translation.js")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("vqd"), m_service->m_token);
    query.addQueryItem(QStringLiteral("query"), QStringLiteral("translate"));
    query.addQueryItem(QStringLiteral("to"), m_target);
    // There is no "auto" value to send: the route detects the language and
    // reports it, so an automatic source simply omits the parameter.
    if (!m_source.isEmpty())
        query.addQueryItem(QStringLiteral("from"), m_source);
    // QUrlQuery escapes the separators, so a token with `&` or `=` cannot break
    // the query.
    url.setQuery(query);

    // The endpoint takes the raw chunk text; only the content type says JSON.
    // Wrapping the text in an object or quoting it breaks the route.
    const QByteArray body = m_parts.at(m_current).text.toUtf8();

    QNetworkRequest request = preparedRequest(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");

    m_usedToken = m_service->m_token;
    m_stage = Stage::Post;
    // Records the request for the per-host pause before it leaves.
    m_service->noteRequestSent();
    m_requestElapsed.start();
    m_request = m_network->nam()->post(request, body);
    connect(m_request, &QNetworkReply::finished, this, &Reply::onFinished);
}

void DuckDuckGoService::Reply::onFinished()
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
    if (!m_service) {
        fail(TranslationError::Network, QStringLiteral("the service is not available"));
        return;
    }

    qCDebug(lcDuckDuckGo) << "HTTP" << status << "in" << m_requestElapsed.elapsed() << "ms";

    // abort() finishes the reply before stopping the request, so a cancelled
    // request here means the transfer timeout fired.
    if (error == QNetworkReply::OperationCanceledError || error == QNetworkReply::TimeoutError) {
        const double seconds = m_network ? m_network->timeoutMs() / 1000.0 : 0;
        fail(TranslationError::Timeout,
             QStringLiteral("DuckDuckGo did not respond within %1 s")
                 .arg(QString::number(seconds, 'g', 3)));
        return;
    }

    if (m_stage == Stage::TokenPage)
        finishTokenPage(request, status, body);
    else
        finishPost(request, status, body);
}

void DuckDuckGoService::Reply::finishTokenPage(QNetworkReply *request, int status,
                                               const QByteArray &body)
{
    const QNetworkReply::NetworkError error = request->error();
    if (status != 0 && (status < 200 || status >= 300)) {
        // The body is classified before the generic status rule: the endpoint
        // may report a rate limit or a block page with an arbitrary status.
        const TranslationResult parsedError = DuckDuckGoParser::parseErrorBody(body);
        if (!parsedError.ok()) {
            fail(parsedError.error, parsedError.errorMessage);
            return;
        }
        // A 403 on the page is a block, not a rejected token: the token stage
        // never refreshes itself, so there is no loop.
        fail(DuckDuckGoParser::errorForHttpStatus(status), QStringLiteral("HTTP %1").arg(status));
        return;
    }
    if (error != QNetworkReply::NoError) {
        // Neither the body, the URL nor QNetworkReply::errorString() is used:
        // the page is the token source and must not leak.
        fail(TranslationError::Network,
             m_network ? m_network->describeNetworkError(request, false)
                       : QStringLiteral("Network error"));
        return;
    }

    QString token;
    QString errorMessage;
    if (!DuckDuckGoParser::tokenFromPage(body, &token, &errorMessage)) {
        fail(TranslationError::ServiceUnavailable, errorMessage);
        return;
    }

    m_service->m_token = token;
    // The 1000 ms pause covers the GET -> POST pair too, not only the chunks.
    scheduleSend();
}

void DuckDuckGoService::Reply::finishPost(QNetworkReply *request, int status,
                                          const QByteArray &body)
{
    const QNetworkReply::NetworkError error = request->error();
    if (status == 403) {
        // A rejected token and a block look the same; one refresh tells them
        // apart, a second 403 is the block.
        handleTokenRejection();
        return;
    }
    if (status != 0 && (status < 200 || status >= 300)) {
        // The body is classified before the generic status rule: the endpoint
        // reports `invalid_text` and `invalid_to` with HTTP 400.
        const TranslationResult parsedError = DuckDuckGoParser::parseErrorBody(body);
        if (!parsedError.ok()) {
            fail(parsedError.error, parsedError.errorMessage);
            return;
        }
        fail(DuckDuckGoParser::errorForHttpStatus(status), QStringLiteral("HTTP %1").arg(status));
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

    const TranslationResult parsed = DuckDuckGoParser::parseResponse(body);
    if (!parsed.ok()) {
        fail(parsed.error, parsed.errorMessage);
        return;
    }

    m_translations.append(parsed.translatedText);
    if (m_detected.isEmpty())
        m_detected = parsed.detectedSourceLang;
    // The part is marked only now: a 403 retry resends the same chunk with the
    // same m_current.
    m_parts[m_current].sent = true;
    m_current = nextUnsentPart();
    if (m_current < 0) {
        finishTranslation();
        return;
    }
    scheduleSend();
}

void DuckDuckGoService::Reply::handleTokenRejection()
{
    if (m_refreshed) {
        fail(TranslationError::RateLimited, QStringLiteral("the endpoint rejected the token"));
        return;
    }
    m_refreshed = true;
    // Clear the cache only if it still holds the rejected token; a token that
    // another request already replaced must not be dropped. The empty cache
    // makes send() fetch a fresh page, and m_current is kept, so the same chunk
    // is resent with the refreshed token.
    if (m_service && m_service->m_token == m_usedToken)
        m_service->m_token.clear();
    scheduleSend();
}

void DuckDuckGoService::Reply::scheduleSend()
{
    const int pause = m_service ? m_service->rateLimitMs() : 0;
    QTimer::singleShot(pause, this, [this] {
        if (!isFinished())
            send();
    });
}

void DuckDuckGoService::Reply::finishTranslation()
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

    const QString source = m_source.isEmpty() ? QStringLiteral("auto") : m_source;
    qCDebug(lcDuckDuckGo) << "Translated" << m_parts.size() << "part(s)," << source << "->"
                          << m_target << "in" << m_elapsed.elapsed() << "ms";
    finish(result);
}

void DuckDuckGoService::Reply::fail(TranslationError error, const QString &message)
{
    // Only the kind is logged: the provider message may repeat the source text.
    qCWarning(lcDuckDuckGo) << "Translation failed:" << errorKind(error);
    TranslationResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    finish(result);
}

void DuckDuckGoService::Reply::stopRequest()
{
    if (QNetworkReply *request = m_request) {
        m_request = nullptr;
        request->disconnect(this);
        request->abort();
        request->deleteLater();
    }
}

DuckDuckGoService::DuckDuckGoService(NetworkManager *network, QObject *parent)
    : QueuedWebService(parent)
    , m_network(network)
    , m_endpoint(defaultEndpoint())
{
    setRateLimitMs(kDefaultRateLimitMs);
}

QString DuckDuckGoService::id() const
{
    return kServiceId;
}

QString DuckDuckGoService::displayName() const
{
    return QStringLiteral("DuckDuckGo");
}

bool DuckDuckGoService::isConfigured() const
{
    return true;
}

QStringList DuckDuckGoService::sourceLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Source);
}

QStringList DuckDuckGoService::targetLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Target);
}

void DuckDuckGoService::applySettings(const AppSettings &)
{
    // No DuckDuckGo setting reaches this protocol: the route has no key, no
    // per-service URL and no mode. The token is not tied to a setting either,
    // so the cache is kept; a token invalidated by a proxy change is refreshed
    // on the first 403.
}

TranslationReply *DuckDuckGoService::translate(const TranslationRequest &request)
{
    if (request.text.trimmed().isEmpty())
        return finishedWithError(TranslationError::EmptyText, QString());

    // The route detects the language itself and reports it, so an automatic
    // source omits the parameter instead of sending a value.
    const bool autoDetect = request.sourceLang == Languages::autoCode();
    const QString source = autoDetect
        ? QString()
        : Languages::toServiceCode(kServiceId, request.sourceLang, Languages::Direction::Source);
    const QString target =
        Languages::toServiceCode(kServiceId, request.targetLang, Languages::Direction::Target);
    const bool unmappedSource = !autoDetect && source.isEmpty();
    if (unmappedSource || target.isEmpty()) {
        const QString code = unmappedSource ? request.sourceLang : request.targetLang;
        return finishedWithError(TranslationError::UnsupportedLanguage,
                                 QStringLiteral("language \"%1\" is not supported").arg(code));
    }

    const QList<Part> parts = splitParts(request.text);
    qCDebug(lcDuckDuckGo) << "Translating" << parts.size() << "part(s),"
                          << (autoDetect ? QStringLiteral("auto") : source) << "->" << target;

    auto *reply = new Reply(m_network, m_endpoint, parts, source, target, this);
    enqueue(reply);
    return reply;
}

void DuckDuckGoService::setEndpoint(const QUrl &url)
{
    QUrl endpoint = !url.isEmpty() && url.isValid() ? url : defaultEndpoint();
    // The POST is m_endpoint.resolved("translation.js"), so the path must end
    // with a slash: without one, the last segment would be replaced instead of
    // extended.
    if (!endpoint.path().endsWith(QLatin1Char('/')))
        endpoint.setPath(endpoint.path() + QLatin1Char('/'));
    m_endpoint = endpoint;
    // A token belongs to the host that issued it.
    m_token.clear();
}

QUrl DuckDuckGoService::endpoint() const
{
    return m_endpoint;
}

QUrl DuckDuckGoService::defaultEndpoint()
{
    return QUrl(QStringLiteral("https://duckduckgo.com/"));
}

QString DuckDuckGoService::browserUserAgent()
{
    return QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                          "(KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36");
}
