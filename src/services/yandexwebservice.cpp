#include "services/yandexwebservice.h"

#include "core/languages.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/yandexwebparser.h"

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

#include <utility>

Q_LOGGING_CATEGORY(lcYandexWeb, "qtranslate.service.yandex-web", QtWarningMsg)

namespace {

const QString kServiceId = QStringLiteral("yandex-web");

// The doc gives no Yandex-specific request limit. 5000 UTF-16 code units is a
// conservative assumption, not a measured value; change it only after a
// measurement.
constexpr int kMaxChunkUnits = 5000;

// The pause between requests comes from doc/free-endpoints.md, §8. The shared
// queue still keeps one active request per host.
constexpr int kDefaultRateLimitMs = 750;

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

// The fields and their order are documented in doc/free-endpoints.md, §2. The
// protocol has no source field, so the source is validated but never sent.
QByteArray formBody(const QString &text, const QString &target)
{
    return formField(QStringLiteral("text"), text) + '&'
        + formField(QStringLiteral("brandID"), QStringLiteral("int")) + '&'
        + formField(QStringLiteral("statLang"), target) + '&'
        + formField(QStringLiteral("targetLang"), QStringLiteral("auto")) + '&'
        + formField(QStringLiteral("locale"), target) + '&'
        + formField(QStringLiteral("clid"), QStringLiteral("2270494")) + '&'
        + formField(QStringLiteral("disable"), QStringLiteral("serp")) + '&'
        + formField(QStringLiteral("use_llm_srv"), QStringLiteral("0"));
}

TranslationReply *finishedWithError(TranslationError error, const QString &message)
{
    TranslationResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    return TranslationReply::createFinished(result);
}

} // namespace

class YandexWebService::Reply : public QueuedWebService::Reply
{
public:
    Reply(NetworkManager *network, QUrl endpoint, QList<Part> parts, QString source,
          QString target, YandexWebService *service)
        : m_network(network)
        , m_endpoint(std::move(endpoint))
        , m_parts(std::move(parts))
        , m_source(std::move(source))
        , m_target(std::move(target))
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
        qCDebug(lcYandexWeb) << "Cancelled";
        stopRequest();
    }

private:
    void send();
    void onFinished();
    void scheduleNextOrFinish();
    void finishTranslation();
    void fail(TranslationError error, const QString &message);
    void stopRequest();
    qsizetype nextUnsentPart() const;

    QPointer<NetworkManager> m_network;
    QUrl m_endpoint;
    QList<Part> m_parts;
    QString m_source;
    QString m_target;
    QPointer<YandexWebService> m_service;
    QPointer<QNetworkReply> m_request;
    QStringList m_translations;
    QString m_detected;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_requestElapsed;
};

qsizetype YandexWebService::Reply::nextUnsentPart() const
{
    for (qsizetype i = 0; i < m_parts.size(); ++i) {
        const Part &part = m_parts.at(i);
        if (!part.sent && !part.text.isEmpty())
            return i;
    }
    return -1;
}

void YandexWebService::Reply::send()
{
    if (isFinished())
        return;
    if (!m_network) {
        fail(TranslationError::Network, QStringLiteral("network is not available"));
        return;
    }

    const qsizetype index = nextUnsentPart();
    if (index < 0) {
        finishTranslation();
        return;
    }

    const QByteArray body = formBody(m_parts.at(index).text, m_target);

    QNetworkRequest request = m_network->makeRequest(m_endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("Accept", "application/json");
    // Overrides AppInfo::userAgent() for this request only; the other services
    // and the shared makeRequest() keep the application User-Agent.
    request.setHeader(QNetworkRequest::UserAgentHeader, browserUserAgent());
    // The text must never move to another host behind the user's back.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);

    // Records the request for the per-host pause before it leaves.
    if (m_service)
        m_service->noteRequestSent();

    m_parts[index].sent = true;
    m_requestElapsed.start();
    m_request = m_network->nam()->post(request, body);
    connect(m_request, &QNetworkReply::finished, this, &Reply::onFinished);
}

void YandexWebService::Reply::onFinished()
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

    qCDebug(lcYandexWeb) << "HTTP" << status << "in" << m_requestElapsed.elapsed() << "ms";

    // abort() finishes the reply before stopping the request, so a cancelled
    // request here means the transfer timeout fired.
    if (error == QNetworkReply::OperationCanceledError || error == QNetworkReply::TimeoutError) {
        const double seconds = m_network ? m_network->timeoutMs() / 1000.0 : 0;
        fail(TranslationError::Timeout,
             QStringLiteral("Yandex did not respond within %1 s")
                 .arg(QString::number(seconds, 'g', 3)));
        return;
    }
    if (status != 0 && (status < 200 || status >= 300)) {
        // The body is classified before the generic status rule: the endpoint
        // may report a rate limit or a block page with an arbitrary status.
        const TranslationResult parsedError = YandexWebParser::parseErrorBody(body);
        if (!parsedError.ok()) {
            fail(parsedError.error, parsedError.errorMessage);
            return;
        }
        fail(YandexWebParser::errorForHttpStatus(status), QStringLiteral("HTTP %1").arg(status));
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

    const TranslationResult parsed = YandexWebParser::parseResponse(body);
    if (!parsed.ok()) {
        fail(parsed.error, parsed.errorMessage);
        return;
    }

    m_translations.append(parsed.translatedText);
    if (m_detected.isEmpty())
        m_detected = parsed.detectedSourceLang;
    scheduleNextOrFinish();
}

void YandexWebService::Reply::scheduleNextOrFinish()
{
    if (nextUnsentPart() < 0) {
        finishTranslation();
        return;
    }

    const int pause = m_service ? m_service->rateLimitMs() : 0;
    QTimer::singleShot(pause, this, [this] {
        if (!isFinished())
            send();
    });
}

void YandexWebService::Reply::finishTranslation()
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

    qCDebug(lcYandexWeb) << "Translated" << m_parts.size() << "part(s)," << m_source << "->"
                         << m_target << "in" << m_elapsed.elapsed() << "ms";
    finish(result);
}

void YandexWebService::Reply::fail(TranslationError error, const QString &message)
{
    qCWarning(lcYandexWeb) << "Translation failed:" << message;
    TranslationResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    finish(result);
}

void YandexWebService::Reply::stopRequest()
{
    if (QNetworkReply *request = m_request) {
        m_request = nullptr;
        request->disconnect(this);
        request->abort();
        request->deleteLater();
    }
}

YandexWebService::YandexWebService(NetworkManager *network, QObject *parent)
    : QueuedWebService(parent)
    , m_network(network)
    , m_endpoint(defaultEndpoint())
{
    setRateLimitMs(kDefaultRateLimitMs);
}

QString YandexWebService::id() const
{
    return kServiceId;
}

QString YandexWebService::displayName() const
{
    return QStringLiteral("Yandex");
}

bool YandexWebService::isConfigured() const
{
    return true;
}

bool YandexWebService::requiresOptIn() const
{
    return true;
}

QStringList YandexWebService::sourceLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Source);
}

QStringList YandexWebService::targetLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Target);
}

void YandexWebService::applySettings(const AppSettings &)
{
}

TranslationReply *YandexWebService::translate(const TranslationRequest &request)
{
    if (request.text.trimmed().isEmpty())
        return finishedWithError(TranslationError::EmptyText, QString());

    // The protocol has no source field: "auto" is accepted and an explicit
    // source is only validated, because the endpoint detects the source itself.
    const QString source = request.sourceLang == Languages::autoCode()
        ? QStringLiteral("auto")
        : Languages::toServiceCode(kServiceId, request.sourceLang, Languages::Direction::Source);
    const QString target =
        Languages::toServiceCode(kServiceId, request.targetLang, Languages::Direction::Target);
    if (source.isEmpty() || target.isEmpty()) {
        const QString code = source.isEmpty() ? request.sourceLang : request.targetLang;
        return finishedWithError(TranslationError::UnsupportedLanguage,
                                 QStringLiteral("language \"%1\" is not supported").arg(code));
    }

    const QList<Part> parts = splitParts(request.text);
    qCDebug(lcYandexWeb) << "Translating" << parts.size() << "part(s)," << source << "->" << target;

    auto *reply = new Reply(m_network, m_endpoint, parts, source, target, this);
    enqueue(reply);
    return reply;
}

void YandexWebService::setEndpoint(const QUrl &url)
{
    m_endpoint = !url.isEmpty() && url.isValid() ? url : defaultEndpoint();
}

QUrl YandexWebService::endpoint() const
{
    return m_endpoint;
}

QUrl YandexWebService::defaultEndpoint()
{
    return QUrl(QStringLiteral("https://api.browser.yandex.com/instaserp/translate"));
}

QString YandexWebService::browserUserAgent()
{
    return QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                          "(KHTML, like Gecko) Chrome/144.0.0.0 YaBrowser/26.3.0.0 "
                          "Safari/537.36");
}
