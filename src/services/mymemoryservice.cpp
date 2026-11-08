#include "services/mymemoryservice.h"

#include "core/languages.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/mymemoryparser.h"

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

Q_LOGGING_CATEGORY(lcMyMemory, "qtranslate.service.mymemory", QtWarningMsg)

namespace {

const QString kServiceId = QStringLiteral("mymemory");

// The endpoint accepts about 500 bytes per request.
constexpr int kMaxChunkBytes = 500;

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

QList<Part> splitPartsUtf8(const QString &text)
{
    QList<Part> result;
    for (const QString &part : TextChunker::splitUtf8(text, kMaxChunkBytes)) {
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

} // namespace

class MyMemoryService::Reply : public QueuedWebService::Reply
{
public:
    Reply(NetworkManager *network, QUrl url, QList<Part> parts, QString source, QString target,
          MyMemoryService *service)
        : m_network(network)
        , m_url(std::move(url))
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
        qCDebug(lcMyMemory) << "Cancelled";
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
    QUrl m_url;
    QList<Part> m_parts;
    QString m_source;
    QString m_target;
    QPointer<MyMemoryService> m_service;
    QPointer<QNetworkReply> m_request;
    QStringList m_translations;
    QString m_detected;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_requestElapsed;
};

qsizetype MyMemoryService::Reply::nextUnsentPart() const
{
    for (qsizetype i = 0; i < m_parts.size(); ++i) {
        const Part &part = m_parts.at(i);
        if (!part.sent && !part.text.isEmpty())
            return i;
    }
    return -1;
}

void MyMemoryService::Reply::send()
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

    QUrl url = m_url;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), m_parts.at(index).text);
    query.addQueryItem(QStringLiteral("langpair"), m_source + QLatin1Char('|') + m_target);
    query.addQueryItem(QStringLiteral("mt"), QStringLiteral("1"));
    url.setQuery(query);
    // Form-decoding servers read a literal '+' in the query as a space, so the
    // text must carry it as %2B.
    QString encodedQuery = url.query(QUrl::FullyEncoded);
    encodedQuery.replace(QLatin1Char('+'), QStringLiteral("%2B"));
    url.setQuery(encodedQuery, QUrl::StrictMode);

    QNetworkRequest request = m_network->makeRequest(url);
    request.setRawHeader("Accept", "application/json");

    // Records the request for the per-host pause before it leaves.
    if (m_service)
        m_service->noteRequestSent();

    m_parts[index].sent = true;
    m_requestElapsed.start();
    m_request = m_network->nam()->get(request);
    connect(m_request, &QNetworkReply::finished, this, &Reply::onFinished);
}

void MyMemoryService::Reply::onFinished()
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

    qCDebug(lcMyMemory) << "HTTP" << status << "in" << m_requestElapsed.elapsed() << "ms";

    // abort() finishes the reply before stopping the request, so a cancelled
    // request here means the transfer timeout fired.
    if (error == QNetworkReply::OperationCanceledError || error == QNetworkReply::TimeoutError) {
        const double seconds = m_network ? m_network->timeoutMs() / 1000.0 : 0;
        fail(TranslationError::Timeout,
             QStringLiteral("MyMemory did not respond within %1 s")
                 .arg(QString::number(seconds, 'g', 3)));
        return;
    }
    if (status != 0 && (status < 200 || status >= 300)) {
        fail(MyMemoryParser::errorForHttpStatus(status), QStringLiteral("HTTP %1").arg(status));
        return;
    }
    if (error != QNetworkReply::NoError) {
        // The URL contains the text, so the detailed reply text is never used.
        fail(TranslationError::Network,
             m_network ? m_network->describeNetworkError(request, false)
                       : QStringLiteral("Network error"));
        return;
    }

    const TranslationResult parsed = MyMemoryParser::parseResponse(body);
    if (!parsed.ok()) {
        fail(parsed.error, parsed.errorMessage);
        return;
    }

    m_translations.append(parsed.translatedText);
    if (m_detected.isEmpty())
        m_detected = parsed.detectedSourceLang;
    scheduleNextOrFinish();
}

void MyMemoryService::Reply::scheduleNextOrFinish()
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

void MyMemoryService::Reply::finishTranslation()
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

    qCDebug(lcMyMemory) << "Translated" << m_parts.size() << "part(s) in" << m_elapsed.elapsed()
                        << "ms";
    finish(result);
}

void MyMemoryService::Reply::fail(TranslationError error, const QString &message)
{
    qCWarning(lcMyMemory) << "Translation failed:" << message;
    TranslationResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    finish(result);
}

void MyMemoryService::Reply::stopRequest()
{
    if (QNetworkReply *request = m_request) {
        m_request = nullptr;
        request->disconnect(this);
        request->abort();
        request->deleteLater();
    }
}

MyMemoryService::MyMemoryService(NetworkManager *network, QObject *parent)
    : QueuedWebService(parent)
    , m_network(network)
    , m_endpoint(defaultEndpoint())
{
    setRateLimitMs(350);
}

QString MyMemoryService::id() const
{
    return kServiceId;
}

QString MyMemoryService::displayName() const
{
    return QStringLiteral("MyMemory");
}

bool MyMemoryService::isConfigured() const
{
    return true;
}

bool MyMemoryService::requiresOptIn() const
{
    return true;
}

QStringList MyMemoryService::sourceLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Source);
}

QStringList MyMemoryService::targetLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Target);
}

void MyMemoryService::applySettings(const AppSettings &)
{
}

TranslationReply *MyMemoryService::translate(const TranslationRequest &request)
{
    if (request.sourceLang == Languages::autoCode()) {
        return finishedWithError(
            TranslationError::UnsupportedLanguage,
            QStringLiteral(
                "MyMemory needs an explicit source language; choose one instead of Auto-detect"));
    }
    if (request.text.trimmed().isEmpty())
        return finishedWithError(TranslationError::EmptyText, QString());

    const QString source =
        Languages::toServiceCode(kServiceId, request.sourceLang, Languages::Direction::Source);
    const QString target =
        Languages::toServiceCode(kServiceId, request.targetLang, Languages::Direction::Target);
    if (source.isEmpty() || target.isEmpty()) {
        const QString code = source.isEmpty() ? request.sourceLang : request.targetLang;
        return finishedWithError(TranslationError::UnsupportedLanguage,
                                 QStringLiteral("language \"%1\" is not supported").arg(code));
    }

    const QList<Part> parts = splitPartsUtf8(request.text);
    qCDebug(lcMyMemory) << "Translating" << parts.size() << "part(s)," << source << "->"
                        << target;

    auto *reply = new Reply(m_network, m_endpoint, parts, source, target, this);
    enqueue(reply);
    return reply;
}

void MyMemoryService::setEndpoint(const QUrl &url)
{
    m_endpoint = url;
}

QUrl MyMemoryService::defaultEndpoint()
{
    return QUrl(QStringLiteral("https://api.mymemory.translated.net/get"));
}
