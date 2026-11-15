#include "services/libretranslateservice.h"

#include "core/languages.h"
#include "core/settings.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/libretranslateparser.h"

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

Q_LOGGING_CATEGORY(lcLibreTranslate, "qtranslate.service.libretranslate", QtWarningMsg)

namespace {

const QString kServiceId = QStringLiteral("libretranslate");

// The endpoint has no documented request limit and the observed stock server
// accepted 6460 characters. 5000 UTF-16 code units is a conservative named
// assumption, not a measurement; change it only after a measurement.
constexpr int kMaxChunkUnits = 5000;

// The pause between requests to one server. Also a named assumption from the
// research (§1.4): the observed server accepted ~30 requests without throttling,
// and QueuedWebService still guarantees one active request per host.
constexpr int kDefaultRateLimitMs = 500;

// A chunk keeps its edge whitespace out of the request and gets it back when the
// parts are joined, so the formatting at the joints survives. A part that is
// only whitespace is never sent.
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

// The base URL is the server root: a prefix such as https://host/lt/ becomes
// https://host/lt/translate, an origin becomes /translate.
QUrl endpointUrl(const QUrl &base)
{
    QUrl url = base;
    url.setQuery(QString());
    url.setFragment(QString());
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path + QStringLiteral("/translate"));
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

class LibreTranslateService::Reply : public QueuedWebService::Reply
{
public:
    Reply(NetworkManager *network, QUrl endpoint, QString apiKey, QList<Part> parts,
          QString source, QString target, LibreTranslateService *service)
        : m_network(network)
        , m_endpoint(std::move(endpoint))
        , m_api_key(std::move(apiKey))
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
        qCDebug(lcLibreTranslate) << "Cancelled";
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
    QString m_api_key;
    QList<Part> m_parts;
    QString m_source; // "auto" or the mapped source code
    QString m_target;
    QPointer<LibreTranslateService> m_service;
    QPointer<QNetworkReply> m_request;
    QStringList m_translations;
    QString m_detected;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_requestElapsed;
};

qsizetype LibreTranslateService::Reply::nextUnsentPart() const
{
    for (qsizetype i = 0; i < m_parts.size(); ++i) {
        const Part &part = m_parts.at(i);
        if (!part.sent && !part.text.isEmpty())
            return i;
    }
    return -1;
}

void LibreTranslateService::Reply::send()
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

    // JSON escaping is why a quote, a backslash or a newline inside the text is
    // safe here, unlike the percent-encoded query of MozhiService. QJsonObject
    // writes its keys alphabetically, so the body is never compared byte for
    // byte. `source` is mandatory: auto-detection is the literal "auto".
    QJsonObject object;
    object.insert(QStringLiteral("q"), m_parts.at(index).text);
    object.insert(QStringLiteral("source"), m_source);
    object.insert(QStringLiteral("target"), m_target);
    const QString apiKey = m_api_key.trimmed();
    if (!apiKey.isEmpty())
        object.insert(QStringLiteral("api_key"), apiKey);
    const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);

    QNetworkRequest request = m_network->makeRequest(m_endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    // Qt's HTTP layer always adds a locale-derived Accept-Language when the
    // request has none (QHttpNetworkConnectionPrivate::prepareRequest()), so
    // omitting the header is impossible. The server localises its `error`
    // sentences through request.accept_languages.best_match() and the classifier
    // matches the recorded English ones (research §1.10), so the language is
    // pinned to English, which also keeps the system locale from leaking to the
    // server.
    request.setRawHeader("Accept-Language", "en");

    // The base URL is user input, so the text must never move to another host
    // behind the user's back.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);

    m_parts[index].sent = true;
    // Records the request for the per-host pause before it leaves.
    if (m_service)
        m_service->noteRequestSent();
    m_requestElapsed.start();
    m_request = m_network->nam()->post(request, body);
    connect(m_request, &QNetworkReply::finished, this, &Reply::onFinished);
}

void LibreTranslateService::Reply::onFinished()
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

    qCDebug(lcLibreTranslate) << "HTTP" << status << "in" << m_requestElapsed.elapsed() << "ms";

    // abort() finishes the reply before stopping the request, so a cancelled
    // request here means the transfer timeout fired.
    if (error == QNetworkReply::OperationCanceledError || error == QNetworkReply::TimeoutError) {
        const double seconds = m_network ? m_network->timeoutMs() / 1000.0 : 0;
        fail(TranslationError::Timeout,
             QStringLiteral("LibreTranslate did not respond within %1 s")
                 .arg(QString::number(seconds, 'g', 3)));
        return;
    }
    if (status != 0 && (status < 200 || status >= 300)) {
        // The body is classified before the generic status rule: a key-protected
        // instance answers 400 (not 401) with a key message, and the sentence is
        // a human-readable English one rather than a stable code.
        const TranslationResult parsedError = LibreTranslateParser::parseErrorBody(body);
        if (!parsedError.ok()) {
            fail(parsedError.error, parsedError.errorMessage);
            return;
        }
        fail(LibreTranslateParser::errorForHttpStatus(status),
             QStringLiteral("HTTP %1").arg(status));
        return;
    }
    if (error != QNetworkReply::NoError) {
        // The request body holds the user's text, so neither the detailed reply
        // text nor QNetworkReply::errorString() is used.
        fail(TranslationError::Network,
             m_network ? m_network->describeNetworkError(request, false)
                       : QStringLiteral("Network error"));
        return;
    }

    const TranslationResult parsed = LibreTranslateParser::parseResponse(body);
    if (!parsed.ok()) {
        fail(parsed.error, parsed.errorMessage);
        return;
    }

    m_translations.append(parsed.translatedText);
    if (m_detected.isEmpty())
        m_detected = parsed.detectedSourceLang;
    scheduleNextOrFinish();
}

void LibreTranslateService::Reply::scheduleNextOrFinish()
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

void LibreTranslateService::Reply::finishTranslation()
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

    qCDebug(lcLibreTranslate) << "Translated" << m_parts.size() << "part(s)," << m_source << "->"
                              << m_target << "in" << m_elapsed.elapsed() << "ms";
    finish(result);
}

void LibreTranslateService::Reply::fail(TranslationError error, const QString &message)
{
    // Only the kind is logged: the provider error sentence may repeat the
    // request (`Invalid request: …`), so it must never reach the log.
    qCWarning(lcLibreTranslate) << "Translation failed:" << errorKind(error);
    TranslationResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    finish(result);
}

void LibreTranslateService::Reply::stopRequest()
{
    if (QNetworkReply *request = m_request) {
        m_request = nullptr;
        request->disconnect(this);
        request->abort();
        request->deleteLater();
    }
}

LibreTranslateService::LibreTranslateService(NetworkManager *network, QObject *parent)
    : QueuedWebService(parent)
    , m_network(network)
{
    setRateLimitMs(kDefaultRateLimitMs);
}

QString LibreTranslateService::id() const
{
    return kServiceId;
}

QString LibreTranslateService::displayName() const
{
    return QStringLiteral("LibreTranslate");
}

// The same rules as isValidServiceBaseUrl() (src/core/settings.cpp). The check
// stays inline and identical to that predicate on purpose: a value that passes
// the helper must configure the service, and applySettings() normalizes it
// first.
bool LibreTranslateService::isConfigured() const
{
    return !m_baseUrl.isEmpty() && m_baseUrl.isValid()
        && (m_baseUrl.scheme() == QLatin1String("http")
            || m_baseUrl.scheme() == QLatin1String("https"))
        && !m_baseUrl.host().isEmpty();
}

QString LibreTranslateService::configurationError() const
{
    if (isConfigured())
        return {};
    // Translated: the message is in the .ts catalogues, 382 entries per file.
    return tr("LibreTranslate needs the base URL of a LibreTranslate server.");
}

QStringList LibreTranslateService::sourceLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Source);
}

QStringList LibreTranslateService::targetLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Target);
}

void LibreTranslateService::applySettings(const AppSettings &settings)
{
    // Re-normalized at the service boundary, as MozhiService does, so a caller
    // that skips SettingsStore::save() cannot send a request to an uncanonical
    // URL. An empty or invalid value clears the base URL and leaves the service
    // unconfigured; the key is optional and never changes isConfigured().
    setBaseUrl(QUrl(normalizedServiceBaseUrl(settings.libretranslate.baseUrl)));
    setApiKey(settings.libretranslate.apiKey);
}

TranslationReply *LibreTranslateService::translate(const TranslationRequest &request)
{
    // A direct caller gets the same hint TranslationManager produces for an
    // unconfigured service.
    if (!isConfigured())
        return finishedWithError(TranslationError::Auth, configurationError());
    if (request.text.trimmed().isEmpty())
        return finishedWithError(TranslationError::EmptyText, QString());

    // The server requires `source`; auto-detection is sent as the literal
    // "auto" and the response then reports the detected language.
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
    qCDebug(lcLibreTranslate) << "Translating" << parts.size() << "part(s)," << source << "->"
                              << target;

    // The endpoint and the key are captured now, so a later settings change
    // cannot alter a request that is already in flight.
    auto *reply = new Reply(m_network, endpointUrl(m_baseUrl), m_apiKey, parts, source, target,
                            this);
    enqueue(reply);
    return reply;
}

void LibreTranslateService::setBaseUrl(const QUrl &url)
{
    m_baseUrl = url;
}

QUrl LibreTranslateService::baseUrl() const
{
    return m_baseUrl;
}

void LibreTranslateService::setApiKey(const QString &key)
{
    m_apiKey = key;
}

QString LibreTranslateService::apiKey() const
{
    return m_apiKey;
}

// Research §1.5 found no public key-less instance that answered, and a default
// would send the user's text to a volunteer server the user did not enter. The
// emptiness is the intended default, not an unfinished value.
QUrl LibreTranslateService::defaultBaseUrl()
{
    return QUrl();
}
