#include "services/bingwebservice.h"

#include "core/languages.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/bingwebparser.h"

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

Q_LOGGING_CATEGORY(lcBingWeb, "qtranslate.service.bing-web", QtWarningMsg)

namespace {

const QString kServiceId = QStringLiteral("bing-web");

// The doc gives no Bing-specific request limit. 5000 UTF-16 code units is a
// conservative assumption, not a measured value; change it only after a
// measurement.
constexpr int kMaxChunkUnits = 5000;

// The doc gives no Bing-specific pause either. 1000 ms between every request
// to the scraped host is a conservative assumption; the shared queue still
// keeps one active request per host.
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

// Percent-encodes both the name and the value as UTF-8. QUrl::toPercentEncoding
// keeps only the unreserved characters, so a space becomes %20 and a literal
// &, =, + or % is escaped instead of acting as a separator.
QByteArray formField(const QString &name, const QString &value)
{
    return QUrl::toPercentEncoding(name) + '=' + QUrl::toPercentEncoding(value);
}

// The fields and their order are documented in doc/free-endpoints.md, §3.
QByteArray formBody(const QString &text, const QString &source, const QString &target,
                    const QString &token, const QString &key)
{
    return formField(QStringLiteral("text"), text) + '&'
        + formField(QStringLiteral("fromLang"), source) + '&'
        + formField(QStringLiteral("to"), target) + '&'
        + formField(QStringLiteral("token"), token) + '&'
        + formField(QStringLiteral("key"), key) + '&'
        + formField(QStringLiteral("isAuthv2"), QStringLiteral("true")) + '&'
        + formField(QStringLiteral("tryFetchingGenderDebiasedTranslations"),
                    QStringLiteral("true"));
}

TranslationReply *finishedWithError(TranslationError error, const QString &message)
{
    TranslationResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    return TranslationReply::createFinished(result);
}

// The scraped MUID is pasted into a request header; a control character means
// the page or a proxy is mangled, and the value must not be used at all.
bool hasControlCharacter(const QString &value)
{
    for (const QChar character : value) {
        if (character.unicode() < 0x20)
            return true;
    }
    return false;
}

} // namespace

class BingWebService::Reply : public QueuedWebService::Reply
{
public:
    Reply(NetworkManager *network, QUrl tokenEndpoint, QUrl translateEndpoint, QList<Part> parts,
          QString source, QString target, BingWebService *service)
        : m_network(network)
        , m_tokenEndpoint(std::move(tokenEndpoint))
        , m_translateEndpoint(std::move(translateEndpoint))
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
        qCDebug(lcBingWeb) << "Cancelled";
        stopRequest();
    }

private:
    // The GET produces the token and the MUID, the POST consumes them. The
    // stage decides how the finished request is interpreted; only one of the
    // two is in flight at a time.
    enum class Stage { TokenPage, Post };

    void send();
    QNetworkRequest preparedRequest(const QUrl &url) const;
    void fetchTokenPage();
    void postChunk();
    void onFinished();
    void finishTokenPage(QNetworkReply *request, int status, const QByteArray &body,
                         const QList<QByteArray> &setCookies);
    void finishPost(QNetworkReply *request, int status, const QByteArray &body);
    void handleTokenRejection();
    void scheduleSend();
    void finishTranslation();
    void fail(TranslationError error, const QString &message);
    void stopRequest();
    qsizetype nextUnsentPart() const;

    QPointer<NetworkManager> m_network;
    QUrl m_tokenEndpoint;
    QUrl m_translateEndpoint;
    QList<Part> m_parts;
    QString m_source;
    QString m_target;
    QPointer<BingWebService> m_service;
    QPointer<QNetworkReply> m_request;
    QStringList m_translations;
    QString m_detected;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_requestElapsed;
    Stage m_stage = Stage::TokenPage;
    qsizetype m_current = -1;
    // The token value used by the last POST; a 401 must not clear a token that
    // another refresh already replaced.
    QString m_usedToken;
    // The page fetched for this reply is still usable even when the page
    // reported a 0 ms lifetime, so the token is posted once before a new GET.
    bool m_tokenFetched = false;
    // A rejected token must force one token-page fetch even if the cache holds
    // a token that was not the rejected one.
    bool m_forceTokenFetch = false;
    // One refresh per reply, not per chunk: a second 401 is a block.
    bool m_refreshed = false;
};

qsizetype BingWebService::Reply::nextUnsentPart() const
{
    for (qsizetype i = 0; i < m_parts.size(); ++i) {
        const Part &part = m_parts.at(i);
        if (!part.sent && !part.text.isEmpty())
            return i;
    }
    return -1;
}

void BingWebService::Reply::send()
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

    // A page with a 0 ms lifetime yields a token that expires at once. The
    // token fetched for this reply is still posted once, while a later request
    // fetches a fresh one.
    if (m_forceTokenFetch || (!m_tokenFetched && !m_service->m_token.isValid())) {
        m_forceTokenFetch = false;
        fetchTokenPage();
        return;
    }

    m_tokenFetched = false;
    postChunk();
}

void BingWebService::Reply::fetchTokenPage()
{
    m_stage = Stage::TokenPage;

    QNetworkRequest request = preparedRequest(m_tokenEndpoint);
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
QNetworkRequest BingWebService::Reply::preparedRequest(const QUrl &url) const
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

void BingWebService::Reply::postChunk()
{
    const BingWebService::CachedToken &token = m_service->m_token;

    QUrl url = m_translateEndpoint;
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("isVertical"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("IG"), token.ig);
    query.addQueryItem(QStringLiteral("IID"), token.iid);
    url.setQuery(query);

    const QByteArray body =
        formBody(m_parts.at(m_current).text, m_source, m_target, token.token, token.key);

    QNetworkRequest request = preparedRequest(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("Accept", "application/json");
    // Exactly one explicit cookie; the manual attributes keep the shared jar
    // from adding another cookie or receiving this one.
    request.setRawHeader("Cookie", "MUID=" + token.muid.toLatin1());

    m_usedToken = token.token;
    m_stage = Stage::Post;
    // Records the request for the per-host pause before it leaves.
    m_service->noteRequestSent();
    m_requestElapsed.start();
    m_request = m_network->nam()->post(request, body);
    connect(m_request, &QNetworkReply::finished, this, &Reply::onFinished);
}

void BingWebService::Reply::onFinished()
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

    // The MUID comes from the reply headers, not from the cookie jar: the
    // manual cookie attributes leave the jar untouched.
    QList<QByteArray> setCookies;
    if (m_stage == Stage::TokenPage) {
        const QList<QPair<QByteArray, QByteArray>> headers = request->rawHeaderPairs();
        for (const auto &header : headers) {
            if (header.first.compare("Set-Cookie", Qt::CaseInsensitive) == 0)
                setCookies.append(header.second);
        }
    }
    request->deleteLater();
    if (isFinished())
        return;
    if (!m_service) {
        fail(TranslationError::Network, QStringLiteral("the service is not available"));
        return;
    }

    qCDebug(lcBingWeb) << "HTTP" << status << "in" << m_requestElapsed.elapsed() << "ms";

    // abort() finishes the reply before stopping the request, so a cancelled
    // request here means the transfer timeout fired.
    if (error == QNetworkReply::OperationCanceledError || error == QNetworkReply::TimeoutError) {
        const double seconds = m_network ? m_network->timeoutMs() / 1000.0 : 0;
        fail(TranslationError::Timeout,
             QStringLiteral("Bing did not respond within %1 s")
                 .arg(QString::number(seconds, 'g', 3)));
        return;
    }

    if (m_stage == Stage::TokenPage)
        finishTokenPage(request, status, body, setCookies);
    else
        finishPost(request, status, body);
}

void BingWebService::Reply::finishTokenPage(QNetworkReply *request, int status,
                                            const QByteArray &body,
                                            const QList<QByteArray> &setCookies)
{
    const QNetworkReply::NetworkError error = request->error();
    if (status != 0 && (status < 200 || status >= 300)) {
        // The body is classified before the generic status rule: the endpoint
        // may report a rate limit or a block page with an arbitrary status.
        const TranslationResult parsedError = BingWebParser::parseErrorBody(body);
        if (!parsedError.ok()) {
            fail(parsedError.error, parsedError.errorMessage);
            return;
        }
        fail(BingWebParser::errorForHttpStatus(status), QStringLiteral("HTTP %1").arg(status));
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

    BingWebParser::TokenPage page;
    QString errorMessage;
    if (!BingWebParser::parseTranslatorPage(body, &page, &errorMessage)) {
        fail(TranslationError::ServiceUnavailable, errorMessage);
        return;
    }

    // The MUID is taken from Set-Cookie first: it is the cookie the endpoint
    // actually set, while the page marker is a fallback for pages without one.
    QString muid = BingWebParser::muidFromSetCookieValues(setCookies).trimmed();
    if (muid.isEmpty())
        muid = page.muid.trimmed();
    if (hasControlCharacter(muid))
        muid.clear();
    if (muid.isEmpty()) {
        fail(TranslationError::ServiceUnavailable,
             QStringLiteral("the token page has no session cookie"));
        return;
    }

    CachedToken &token = m_service->m_token;
    token.ig = page.ig;
    token.iid = page.iid;
    token.key = page.key;
    token.token = page.token;
    token.muid = muid;
    token.lifetimeMs = page.lifetimeMs;
    token.age.restart();
    m_tokenFetched = true;

    // The 1000 ms pause covers the GET -> POST pair too, not only the chunks.
    scheduleSend();
}

void BingWebService::Reply::finishPost(QNetworkReply *request, int status, const QByteArray &body)
{
    const QNetworkReply::NetworkError error = request->error();
    // A rejected token can arrive as HTTP 401 or as statusCode 401 under HTTP
    // 200; the endpoint reports many errors under 200.
    if (status == 401 || BingWebParser::isTokenRejection(body)) {
        handleTokenRejection();
        return;
    }
    if (status != 0 && (status < 200 || status >= 300)) {
        if (status == 403) {
            // A block is a rate limit whatever the body says, and it is never
            // refreshed.
            fail(TranslationError::RateLimited, QStringLiteral("HTTP %1").arg(status));
            return;
        }
        // The body is classified before the generic status rule: the endpoint
        // may report a rate limit or a block page with an arbitrary status.
        const TranslationResult parsedError = BingWebParser::parseErrorBody(body);
        if (!parsedError.ok()) {
            fail(parsedError.error, parsedError.errorMessage);
            return;
        }
        fail(BingWebParser::errorForHttpStatus(status), QStringLiteral("HTTP %1").arg(status));
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

    const TranslationResult parsed = BingWebParser::parseResponse(body);
    if (!parsed.ok()) {
        fail(parsed.error, parsed.errorMessage);
        return;
    }

    m_translations.append(parsed.translatedText);
    if (m_detected.isEmpty())
        m_detected = parsed.detectedSourceLang;
    // The part is marked only now: a 401 retry resends the same chunk with the
    // same m_current.
    m_parts[m_current].sent = true;
    m_current = nextUnsentPart();
    if (m_current < 0) {
        finishTranslation();
        return;
    }
    scheduleSend();
}

void BingWebService::Reply::handleTokenRejection()
{
    if (m_refreshed) {
        fail(TranslationError::RateLimited, QStringLiteral("the endpoint rejected the token"));
        return;
    }
    m_refreshed = true;
    m_tokenFetched = false;
    m_forceTokenFetch = true;
    // Clear the cache only if it still holds the rejected token; a token that
    // another request already replaced must not be dropped.
    if (m_service && m_service->m_token.token == m_usedToken)
        m_service->m_token = BingWebService::CachedToken{};

    // Reuse the token-page path and keep m_current, so the success path resends
    // the same chunk with the refreshed token.
    scheduleSend();
}

void BingWebService::Reply::scheduleSend()
{
    const int pause = m_service ? m_service->rateLimitMs() : 0;
    QTimer::singleShot(pause, this, [this] {
        if (!isFinished())
            send();
    });
}

void BingWebService::Reply::finishTranslation()
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

    qCDebug(lcBingWeb) << "Translated" << m_parts.size() << "part(s)," << m_source << "->"
                       << m_target << "in" << m_elapsed.elapsed() << "ms";
    finish(result);
}

void BingWebService::Reply::fail(TranslationError error, const QString &message)
{
    qCWarning(lcBingWeb) << "Translation failed:" << message;
    TranslationResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    finish(result);
}

void BingWebService::Reply::stopRequest()
{
    if (QNetworkReply *request = m_request) {
        m_request = nullptr;
        request->disconnect(this);
        request->abort();
        request->deleteLater();
    }
}

BingWebService::BingWebService(NetworkManager *network, QObject *parent)
    : QueuedWebService(parent)
    , m_network(network)
    , m_tokenEndpoint(defaultTokenEndpoint())
    , m_translateEndpoint(defaultTranslateEndpoint())
{
    setRateLimitMs(kDefaultRateLimitMs);
}

QString BingWebService::id() const
{
    return kServiceId;
}

QString BingWebService::displayName() const
{
    return QStringLiteral("Bing");
}

bool BingWebService::isConfigured() const
{
    return true;
}

bool BingWebService::requiresOptIn() const
{
    return true;
}

QStringList BingWebService::sourceLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Source);
}

QStringList BingWebService::targetLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Target);
}

void BingWebService::applySettings(const AppSettings &)
{
    // No Bing setting affects the protocol; dropping the token and its MUID
    // keeps a token from being reused after a configuration change.
    m_token = CachedToken{};
}

TranslationReply *BingWebService::translate(const TranslationRequest &request)
{
    if (request.text.trimmed().isEmpty())
        return finishedWithError(TranslationError::EmptyText, QString());

    // The protocol has a source field; "auto" is sent as the documented
    // "auto-detect" and an explicit source is mapped to a Bing code.
    const QString source = request.sourceLang == Languages::autoCode()
        ? QStringLiteral("auto-detect")
        : Languages::toServiceCode(kServiceId, request.sourceLang, Languages::Direction::Source);
    const QString target =
        Languages::toServiceCode(kServiceId, request.targetLang, Languages::Direction::Target);
    if (source.isEmpty() || target.isEmpty()) {
        const QString code = source.isEmpty() ? request.sourceLang : request.targetLang;
        return finishedWithError(TranslationError::UnsupportedLanguage,
                                 QStringLiteral("language \"%1\" is not supported").arg(code));
    }

    const QList<Part> parts = splitParts(request.text);
    qCDebug(lcBingWeb) << "Translating" << parts.size() << "part(s)," << source << "->" << target;

    auto *reply = new Reply(m_network, m_tokenEndpoint, m_translateEndpoint, parts, source,
                            target, this);
    enqueue(reply);
    return reply;
}

void BingWebService::setEndpoints(const QUrl &tokenUrl, const QUrl &translateUrl)
{
    m_tokenEndpoint =
        !tokenUrl.isEmpty() && tokenUrl.isValid() ? tokenUrl : defaultTokenEndpoint();
    m_translateEndpoint = !translateUrl.isEmpty() && translateUrl.isValid()
        ? translateUrl
        : defaultTranslateEndpoint();
}

QUrl BingWebService::tokenEndpoint() const
{
    return m_tokenEndpoint;
}

QUrl BingWebService::translateEndpoint() const
{
    return m_translateEndpoint;
}

QUrl BingWebService::defaultTokenEndpoint()
{
    return QUrl(QStringLiteral("https://www.bing.com/translator"));
}

QUrl BingWebService::defaultTranslateEndpoint()
{
    return QUrl(QStringLiteral("https://www.bing.com/ttranslatev3"));
}

QString BingWebService::browserUserAgent()
{
    return QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                          "(KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36");
}
