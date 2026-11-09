#include "services/mozhiservice.h"

#include "core/languages.h"
#include "core/settings.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/mozhiparser.h"

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

Q_LOGGING_CATEGORY(lcMozhi, "qtranslate.service.mozhi", QtWarningMsg)

namespace {

const QString kServiceId = QStringLiteral("mozhi");

// The doc gives no Mozhi-specific request limit. 5000 UTF-16 code units is a
// conservative assumption, not a measured value; an HTTP 413/414 means the
// percent-encoded URL was too long, so change this only after a measurement.
constexpr int kMaxChunkUnits = 5000;

// The pause between requests to one instance. Also an assumption from the
// integration notes in doc/free-endpoints.md, §8; QueuedWebService still
// guarantees one active request per host.
constexpr int kDefaultRateLimitMs = 500;

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

// The instance URL is the base: a prefix such as https://host/mozhi/ becomes
// https://host/mozhi/api/translate, an origin becomes /api/translate.
QUrl endpointUrl(const QUrl &instance)
{
    QUrl url = instance;
    url.setQuery(QString());
    url.setFragment(QString());
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path + QStringLiteral("/api/translate"));
    return url;
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

class MozhiService::Reply : public QueuedWebService::Reply
{
public:
    Reply(NetworkManager *network, QUrl endpoint, QString engine, QList<Part> parts,
          QString source, QString target, MozhiService *service)
        : m_network(network)
        , m_endpoint(std::move(endpoint))
        , m_engine(std::move(engine))
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
        qCDebug(lcMozhi) << "Cancelled";
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
    QString m_engine;
    QList<Part> m_parts;
    QString m_source;
    QString m_target;
    QPointer<MozhiService> m_service;
    QPointer<QNetworkReply> m_request;
    QStringList m_translations;
    QString m_detected;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_requestElapsed;
};

qsizetype MozhiService::Reply::nextUnsentPart() const
{
    for (qsizetype i = 0; i < m_parts.size(); ++i) {
        const Part &part = m_parts.at(i);
        if (!part.sent && !part.text.isEmpty())
            return i;
    }
    return -1;
}

void MozhiService::Reply::send()
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

    QUrl url = m_endpoint;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("engine"), m_engine);
    query.addQueryItem(QStringLiteral("from"), m_source);
    query.addQueryItem(QStringLiteral("to"), m_target);
    query.addQueryItem(QStringLiteral("text"), m_parts.at(index).text);
    url.setQuery(query);
    // Form-decoding servers read a literal '+' in the query as a space, so the
    // text must carry it as %2B.
    QString encodedQuery = url.query(QUrl::FullyEncoded);
    encodedQuery.replace(QLatin1Char('+'), QStringLiteral("%2B"));
    url.setQuery(encodedQuery, QUrl::StrictMode);

    QNetworkRequest request = m_network->makeRequest(url);
    request.setRawHeader("Accept", "application/json");
    // The text must never move to another host behind the user's back.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);

    // Records the request for the per-host pause before it leaves.
    if (m_service)
        m_service->noteRequestSent();

    m_parts[index].sent = true;
    m_requestElapsed.start();
    m_request = m_network->nam()->get(request);
    connect(m_request, &QNetworkReply::finished, this, &Reply::onFinished);
}

void MozhiService::Reply::onFinished()
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

    qCDebug(lcMozhi) << "HTTP" << status << "in" << m_requestElapsed.elapsed() << "ms";

    // abort() finishes the reply before stopping the request, so a cancelled
    // request here means the transfer timeout fired.
    if (error == QNetworkReply::OperationCanceledError || error == QNetworkReply::TimeoutError) {
        const double seconds = m_network ? m_network->timeoutMs() / 1000.0 : 0;
        fail(TranslationError::Timeout,
             QStringLiteral("Mozhi did not respond within %1 s")
                 .arg(QString::number(seconds, 'g', 3)));
        return;
    }
    if (status != 0 && (status < 200 || status >= 300)) {
        // A plain-text body is classified before the generic status rule: the
        // default instance returns HTTP 500 with an upstream rate-limit text.
        const TranslationResult parsedError = MozhiParser::parseErrorBody(body);
        if (!parsedError.ok()) {
            fail(parsedError.error, parsedError.errorMessage);
            return;
        }
        fail(MozhiParser::errorForHttpStatus(status), QStringLiteral("HTTP %1").arg(status));
        return;
    }
    if (error != QNetworkReply::NoError) {
        // The URL contains the text, so the detailed reply text is never used.
        fail(TranslationError::Network,
             m_network ? m_network->describeNetworkError(request, false)
                       : QStringLiteral("Network error"));
        return;
    }

    const MozhiParser::Response parsed = MozhiParser::parseResponse(body);
    if (!parsed.result.ok()) {
        fail(parsed.result.error, parsed.result.errorMessage);
        return;
    }

    m_translations.append(parsed.result.translatedText);
    if (m_detected.isEmpty())
        m_detected = parsed.result.detectedSourceLang;
    scheduleNextOrFinish();
}

void MozhiService::Reply::scheduleNextOrFinish()
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

void MozhiService::Reply::finishTranslation()
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

    qCDebug(lcMozhi) << "Translated" << m_parts.size() << "part(s)," << m_source << "->"
                     << m_target << "via" << m_engine << "in" << m_elapsed.elapsed() << "ms";
    finish(result);
}

void MozhiService::Reply::fail(TranslationError error, const QString &message)
{
    qCWarning(lcMozhi) << "Translation failed:" << message;
    TranslationResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    finish(result);
}

void MozhiService::Reply::stopRequest()
{
    if (QNetworkReply *request = m_request) {
        m_request = nullptr;
        request->disconnect(this);
        request->abort();
        request->deleteLater();
    }
}

MozhiService::MozhiService(NetworkManager *network, QObject *parent)
    : QueuedWebService(parent)
    , m_network(network)
    , m_instance(defaultInstance())
    , m_engine(defaultEngine())
{
    setRateLimitMs(kDefaultRateLimitMs);
}

QString MozhiService::id() const
{
    return kServiceId;
}

QString MozhiService::displayName() const
{
    return QStringLiteral("Mozhi");
}

bool MozhiService::isConfigured() const
{
    return true;
}

bool MozhiService::requiresOptIn() const
{
    return true;
}

QStringList MozhiService::sourceLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Source);
}

QStringList MozhiService::targetLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Target);
}

void MozhiService::applySettings(const AppSettings &settings)
{
    setInstance(QUrl(normalizedMozhiInstance(settings.mozhi.instance)));
    setEngine(normalizedMozhiEngine(settings.mozhi.engine));
}

TranslationReply *MozhiService::translate(const TranslationRequest &request)
{
    if (request.text.trimmed().isEmpty())
        return finishedWithError(TranslationError::EmptyText, QString());

    // The default Google engine accepts "auto"; an instance that rejects the
    // pair answers with a language error, which becomes UnsupportedLanguage.
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
    qCDebug(lcMozhi) << "Translating" << parts.size() << "part(s)," << source << "->" << target
                     << "via" << m_engine;

    auto *reply = new Reply(m_network, endpointUrl(m_instance), m_engine, parts, source, target,
                            this);
    enqueue(reply);
    return reply;
}

void MozhiService::setInstance(const QUrl &url)
{
    m_instance = !url.isEmpty() && url.isValid() ? url : defaultInstance();
}

QUrl MozhiService::instance() const
{
    return m_instance;
}

void MozhiService::setEngine(const QString &engine)
{
    const QString trimmed = engine.trimmed();
    m_engine = trimmed.isEmpty() ? defaultEngine() : trimmed;
}

QString MozhiService::engine() const
{
    return m_engine;
}

QUrl MozhiService::defaultInstance()
{
    return QUrl(MozhiSettings{}.instance);
}

QString MozhiService::defaultEngine()
{
    return MozhiSettings{}.engine;
}
