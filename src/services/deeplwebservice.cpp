#include "services/deeplwebservice.h"

#include "core/languages.h"
#include "core/settings.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/deeplwebparser.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>

#include <utility>

Q_LOGGING_CATEGORY(lcDeepLWeb, "qtranslate.service.deeplweb", QtWarningMsg)

namespace {

constexpr int kMaxChunkLength = 5000;

struct WebPart
{
    QString leading;
    QString text;
    QString trailing;
    bool sent = false;
};

QList<WebPart> splitParts(const QString &text)
{
    QList<WebPart> result;
    for (const QString &part : TextChunker::split(text, kMaxChunkLength)) {
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
        result.append({part.left(begin), part.mid(begin, end - begin), part.mid(end), true});
    }
    return result;
}

} // namespace

class DeepLWebService::Reply : public QueuedWebService::Reply
{
public:
    Reply(NetworkManager *network, QUrl url, QString serviceId, QList<WebPart> parts,
          QString source, QString target, int retryDelayMs, DeepLWebService *service)
        : m_network(network)
        , m_url(std::move(url))
        , m_serviceId(std::move(serviceId))
        , m_parts(std::move(parts))
        , m_source(std::move(source))
        , m_target(std::move(target))
        , m_retryDelayMs(retryDelayMs)
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
        qCDebug(lcDeepLWeb) << "Cancelled";
        stopRequest();
    }

private:
    void send();
    void onFinished();
    void fail(TranslationError error, const QString &message);
    void stopRequest();

    QPointer<NetworkManager> m_network;
    QUrl m_url;
    QString m_serviceId;
    QList<WebPart> m_parts;
    QString m_source;
    QString m_target;
    int m_retryDelayMs = 3000;
    QPointer<DeepLWebService> m_service;
    QPointer<QNetworkReply> m_request;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_requestElapsed;
    bool m_retried = false;
};

void DeepLWebService::Reply::send()
{
    if (isFinished())
        return;
    if (!m_network) {
        fail(TranslationError::Network, QStringLiteral("network is not available"));
        return;
    }

    QJsonArray texts;
    for (const WebPart &part : std::as_const(m_parts)) {
        if (part.sent)
            texts.append(part.text);
    }

    QJsonObject object;
    object.insert(QStringLiteral("text"), texts);
    object.insert(QStringLiteral("target_lang"), m_target);
    if (m_source != Languages::autoCode())
        object.insert(QStringLiteral("source_lang"), m_source);

    QNetworkRequest request = m_network->makeRequest(m_url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json;charset=UTF-8"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", "None");

    if (m_service)
        m_service->noteRequestSent();

    qCDebug(lcDeepLWeb) << "Translating" << texts.size() << "part(s)," << m_source << "->"
                        << m_target;
    m_requestElapsed.start();
    m_request = m_network->nam()->post(request,
                                       QJsonDocument(object).toJson(QJsonDocument::Compact));
    connect(m_request, &QNetworkReply::finished, this, &Reply::onFinished);
}

void DeepLWebService::Reply::onFinished()
{
    QNetworkReply *request = m_request;
    m_request = nullptr;
    if (!request)
        return;

    const QByteArray responseBody = request->readAll();
    request->deleteLater();
    if (isFinished())
        return;

    const int status = request->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError error = request->error();
    qCDebug(lcDeepLWeb) << "HTTP" << status << error << "in" << m_requestElapsed.elapsed()
                        << "ms";

    // abort() finishes the reply before stopping the request, so a cancelled
    // request here means the transfer timeout fired.
    if (error == QNetworkReply::OperationCanceledError || error == QNetworkReply::TimeoutError) {
        const double seconds = m_network ? m_network->timeoutMs() / 1000.0 : 0;
        fail(TranslationError::Timeout,
             QStringLiteral("no response from %1 within %2 s")
                 .arg(m_url.host(), QString::number(seconds, 'g', 3)));
        return;
    }
    if (status == 429 && !m_retried) {
        m_retried = true;
        qCDebug(lcDeepLWeb) << "HTTP 429; retrying in" << m_retryDelayMs << "ms";
        QTimer::singleShot(m_retryDelayMs, this, [this] {
            if (!isFinished())
                send();
        });
        return;
    }
    if (status != 0 && (status < 200 || status >= 300)) {
        fail(DeepLWebParser::errorForHttpStatus(status), QStringLiteral("HTTP %1").arg(status));
        return;
    }
    if (error != QNetworkReply::NoError) {
        fail(TranslationError::Network,
             m_network ? m_network->describeNetworkError(request) : request->errorString());
        return;
    }

    QStringList translations;
    QString detected;
    QString parseError;
    if (!DeepLWebParser::parseTranslations(responseBody, &translations, &detected,
                                           &parseError)) {
        if (DeepLWebParser::isRateLimitBody(responseBody))
            fail(TranslationError::RateLimited,
                 QStringLiteral("the endpoint reported a rate limit"));
        else
            fail(TranslationError::BadResponse, parseError);
        return;
    }

    int sentCount = 0;
    for (const WebPart &part : std::as_const(m_parts)) {
        if (part.sent)
            ++sentCount;
    }
    if (translations.size() != sentCount) {
        fail(TranslationError::BadResponse,
             QStringLiteral("expected %1 translations, got %2")
                 .arg(sentCount)
                 .arg(translations.size()));
        return;
    }

    TranslationResult result;
    result.serviceId = m_serviceId;
    result.detectedSourceLang = detected;
    int index = 0;
    for (const WebPart &part : std::as_const(m_parts)) {
        if (part.sent)
            result.translatedText += part.leading + translations.at(index++) + part.trailing;
        else
            result.translatedText += part.leading;
    }

    qCDebug(lcDeepLWeb) << "Translated" << m_parts.size() << "part(s) in"
                        << m_elapsed.elapsed() << "ms, result" << result.translatedText.size()
                        << "chars";
    finish(result);
}

void DeepLWebService::Reply::fail(TranslationError error, const QString &message)
{
    qCWarning(lcDeepLWeb) << "Translation failed:" << message;
    TranslationResult result;
    result.serviceId = m_serviceId;
    result.error = error;
    result.errorMessage = message;
    finish(result);
}

void DeepLWebService::Reply::stopRequest()
{
    if (QNetworkReply *request = m_request) {
        m_request = nullptr;
        request->disconnect(this);
        request->abort();
        request->deleteLater();
    }
}

DeepLWebService::DeepLWebService(NetworkManager *network, QObject *parent)
    : DeepLWebService(network, QStringLiteral("deepl-web"), parent)
{
}

DeepLWebService::DeepLWebService(NetworkManager *network, QString serviceId, QObject *parent)
    : QueuedWebService(parent)
    , m_network(network)
    , m_serviceId(std::move(serviceId))
    , m_endpoint(defaultEndpoint())
{
}

QString DeepLWebService::id() const
{
    return m_serviceId;
}

QString DeepLWebService::displayName() const
{
    return m_serviceId == QStringLiteral("deepl") ? QStringLiteral("DeepL")
                                                  : QStringLiteral("DeepL (free web)");
}

bool DeepLWebService::isConfigured() const
{
    return true;
}

QStringList DeepLWebService::sourceLanguages() const
{
    return Languages::supported(m_serviceId, Languages::Direction::Source);
}

QStringList DeepLWebService::targetLanguages() const
{
    return Languages::supported(m_serviceId, Languages::Direction::Target);
}

void DeepLWebService::applySettings(const AppSettings &settings)
{
    m_englishVariant = settings.deepl.englishVariant == QStringLiteral("EN-GB")
        ? QStringLiteral("EN-GB")
        : QStringLiteral("EN-US");
}

TranslationReply *DeepLWebService::translate(const TranslationRequest &request)
{
    const QString source = request.sourceLang == Languages::autoCode()
                               ? Languages::autoCode()
                               : Languages::toServiceCode(m_serviceId, request.sourceLang,
                                                          Languages::Direction::Source);
    QString target = Languages::toServiceCode(m_serviceId, request.targetLang,
                                              Languages::Direction::Target);
    if (request.targetLang == QStringLiteral("en") && !target.isEmpty())
        target = m_englishVariant;

    if (source.isEmpty() || target.isEmpty()) {
        const QString code = source.isEmpty() ? request.sourceLang : request.targetLang;
        TranslationResult result;
        result.serviceId = m_serviceId;
        result.error = TranslationError::UnsupportedLanguage;
        result.errorMessage = QStringLiteral("language \"%1\" is not supported").arg(code);
        return TranslationReply::createFinished(result);
    }

    const QString webSource = source == Languages::autoCode() ? source : toWebCode(source);
    const QString webTarget = toWebCode(target);
    const QList<WebPart> parts = splitParts(request.text);
    qCDebug(lcDeepLWeb) << "Translating" << request.text.size() << "chars in" << parts.size()
                        << "part(s)," << webSource << "->" << webTarget;

    auto *reply = new Reply(m_network, m_endpoint, m_serviceId, parts, webSource, webTarget,
                            m_retryDelayMs, this);
    enqueue(reply);
    return reply;
}

void DeepLWebService::setEndpoint(const QUrl &url)
{
    m_endpoint = url;
}

void DeepLWebService::setRetryDelayMs(int ms)
{
    m_retryDelayMs = qMax(0, ms);
}

QUrl DeepLWebService::defaultEndpoint()
{
    return QUrl(QStringLiteral("https://oneshot-free.www.deepl.com/v1/translate"));
}

QString DeepLWebService::toWebCode(const QString &deeplCode)
{
    if (deeplCode.isEmpty())
        return {};

    const QStringList parts = deeplCode.split(QLatin1Char('-'));
    QString result = parts.first().toLower();
    for (qsizetype i = 1; i < parts.size(); ++i) {
        QString region = parts.at(i).toLower();
        if (region.size() == 2)
            region = region.toUpper();
        else if (!region.isEmpty())
            region.replace(0, 1, region.left(1).toUpper());
        result += QLatin1Char('-') + region;
    }
    return result;
}
