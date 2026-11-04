#include "services/deeplservice.h"

#include "core/languages.h"
#include "core/settings.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/deeplparser.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>

#include <utility>

Q_LOGGING_CATEGORY(lcDeepL, "qtranslate.service.deepl", QtWarningMsg)

namespace {

const QString kServiceId = QStringLiteral("deepl");
constexpr int kMaxChunkLength = 5000;

struct DeepLPart
{
    QString leading;
    QString text;
    QString trailing;
    bool sent = false;
};

QList<DeepLPart> splitParts(const QString &text)
{
    QList<DeepLPart> result;
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

QString httpErrorMessage(TranslationError error, int status)
{
    switch (error) {
    case TranslationError::Auth:
        return QObject::tr("The DeepL API key was rejected (HTTP %1).").arg(status);
    case TranslationError::QuotaExceeded:
        return QObject::tr("The DeepL character limit is exceeded (HTTP %1).").arg(status);
    case TranslationError::RateLimited:
        return QObject::tr("Too many DeepL requests; try again later (HTTP %1).").arg(status);
    case TranslationError::TooLong:
        return QObject::tr("The DeepL request is too large (HTTP %1).").arg(status);
    case TranslationError::Network:
        return QObject::tr("DeepL is temporarily unavailable (HTTP %1).").arg(status);
    default:
        return QObject::tr("DeepL returned an unexpected response (HTTP %1).").arg(status);
    }
}

class DeepLReply : public TranslationReply
{
public:
    DeepLReply(NetworkManager *network, QUrl url, QString apiKey, QList<DeepLPart> parts,
               QString source, QString target)
        : m_network(network)
        , m_url(std::move(url))
        , m_apiKey(std::move(apiKey))
        , m_parts(std::move(parts))
        , m_source(std::move(source))
        , m_target(std::move(target))
    {
    }

    ~DeepLReply() override { stopRequest(); }

    void start()
    {
        m_elapsed.start();
        QMetaObject::invokeMethod(this, [this] { send(); }, Qt::QueuedConnection);
    }

protected:
    void abortImpl() override
    {
        qCDebug(lcDeepL) << "Cancelled";
        stopRequest();
    }

private:
    void send();
    void onFinished();
    void fail(TranslationError error, const QString &message);
    void stopRequest();

    QPointer<NetworkManager> m_network;
    QUrl m_url;
    QString m_apiKey;
    QList<DeepLPart> m_parts;
    QString m_source;
    QString m_target;
    QPointer<QNetworkReply> m_request;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_requestElapsed;
};

void DeepLReply::send()
{
    if (isFinished())
        return;
    if (!m_network) {
        fail(TranslationError::Network, QStringLiteral("network is not available"));
        return;
    }

    QJsonArray texts;
    for (const DeepLPart &part : std::as_const(m_parts)) {
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
    request.setRawHeader("Authorization", "DeepL-Auth-Key " + m_apiKey.toUtf8());

    qCDebug(lcDeepL) << "Translating" << texts.size() << "part(s)," << m_source << "->"
                     << m_target;
    m_requestElapsed.start();
    m_request = m_network->nam()->post(request, QJsonDocument(object).toJson(QJsonDocument::Compact));
    connect(m_request, &QNetworkReply::finished, this, &DeepLReply::onFinished);
}

void DeepLReply::onFinished()
{
    QNetworkReply *request = m_request;
    m_request = nullptr;
    if (!request)
        return;
    request->deleteLater();
    if (isFinished())
        return;

    const int status = request->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError error = request->error();
    qCDebug(lcDeepL) << "HTTP" << status << error << "in" << m_requestElapsed.elapsed() << "ms";

    if (error == QNetworkReply::OperationCanceledError || error == QNetworkReply::TimeoutError) {
        const double seconds = m_network ? m_network->timeoutMs() / 1000.0 : 0;
        fail(TranslationError::Timeout,
             QStringLiteral("no response from %1 within %2 s")
                 .arg(m_url.host(), QString::number(seconds, 'g', 3)));
        return;
    }
    if (status != 0 && (status < 200 || status >= 300)) {
        const TranslationError httpError = DeepLParser::errorForHttpStatus(status);
        fail(httpError, QStringLiteral("HTTP %1").arg(status));
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
    if (!DeepLParser::parseTranslations(request->readAll(), &translations, &detected,
                                        &parseError)) {
        fail(TranslationError::BadResponse, parseError);
        return;
    }

    int sentCount = 0;
    for (const DeepLPart &part : std::as_const(m_parts)) {
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
    result.serviceId = kServiceId;
    result.detectedSourceLang = detected;
    int index = 0;
    for (const DeepLPart &part : std::as_const(m_parts)) {
        if (part.sent)
            result.translatedText += part.leading + translations.at(index++) + part.trailing;
        else
            result.translatedText += part.leading;
    }

    qCDebug(lcDeepL) << "Translated" << m_parts.size() << "part(s) in" << m_elapsed.elapsed()
                     << "ms, result" << result.translatedText.size() << "chars";
    finish(result);
}

void DeepLReply::fail(TranslationError error, const QString &message)
{
    qCWarning(lcDeepL) << "Translation failed:" << message;
    TranslationResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    finish(result);
}

void DeepLReply::stopRequest()
{
    if (QNetworkReply *request = m_request) {
        m_request = nullptr;
        request->disconnect(this);
        request->abort();
        request->deleteLater();
    }
}

} // namespace

DeepLService::DeepLService(NetworkManager *network, QObject *parent)
    : TranslationService(parent)
    , m_network(network)
{
}

QString DeepLService::id() const
{
    return kServiceId;
}

QString DeepLService::displayName() const
{
    return QStringLiteral("DeepL");
}

bool DeepLService::isConfigured() const
{
    return !m_apiKey.trimmed().isEmpty();
}

QStringList DeepLService::sourceLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Source);
}

QStringList DeepLService::targetLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Target);
}

void DeepLService::applySettings(const AppSettings &settings)
{
    m_apiKey = settings.deepl.apiKey;
    m_englishVariant = settings.deepl.englishVariant == QStringLiteral("EN-GB")
        ? QStringLiteral("EN-GB")
        : QStringLiteral("EN-US");
}

TranslationReply *DeepLService::translate(const TranslationRequest &request)
{
    if (!isConfigured()) {
        TranslationResult result;
        result.serviceId = kServiceId;
        result.error = TranslationError::Auth;
        result.errorMessage = DeepLService::tr("DeepL needs an API key");
        return TranslationReply::createFinished(result);
    }

    const QString source = request.sourceLang == Languages::autoCode()
                               ? Languages::autoCode()
                               : Languages::toServiceCode(kServiceId, request.sourceLang,
                                                          Languages::Direction::Source);
    QString target = Languages::toServiceCode(kServiceId, request.targetLang,
                                              Languages::Direction::Target);
    if (request.targetLang == QStringLiteral("en") && !target.isEmpty())
        target = m_englishVariant;

    if (source.isEmpty() || target.isEmpty()) {
        const QString code = source.isEmpty() ? request.sourceLang : request.targetLang;
        return TranslationReply::createFinished(
            TranslationResult{ {}, {}, kServiceId, TranslationError::UnsupportedLanguage,
                               DeepLService::tr("language \"%1\" is not supported").arg(code) });
    }

    const QList<DeepLPart> parts = splitParts(request.text);
    qCDebug(lcDeepL) << "Translating" << request.text.size() << "chars in" << parts.size()
                     << "part(s)," << source << "->" << target;

    auto *reply = new DeepLReply(m_network, endpointForKey(m_apiKey), m_apiKey, parts,
                                 source, target);
    reply->start();
    return reply;
}

void DeepLService::checkKey(const QString &key, KeyCheckCallback callback)
{
    if (key.trimmed().isEmpty()) {
        const QString message = tr("Enter a DeepL API key.");
        QMetaObject::invokeMethod(
            this, [callback = std::move(callback), message] { callback(false, message); },
            Qt::QueuedConnection);
        return;
    }
    if (!m_network) {
        const QString message = tr("The network is not available.");
        QMetaObject::invokeMethod(
            this, [callback = std::move(callback), message] { callback(false, message); },
            Qt::QueuedConnection);
        return;
    }

    QNetworkRequest request = m_network->makeRequest(usageUrlForKey(key));
    request.setRawHeader("Authorization", "DeepL-Auth-Key " + key.toUtf8());
    QNetworkReply *reply = m_network->nam()->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, callback = std::move(callback)] {
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QNetworkReply::NetworkError error = reply->error();
                if (error == QNetworkReply::OperationCanceledError
                    || error == QNetworkReply::TimeoutError) {
                    const double seconds = m_network ? m_network->timeoutMs() / 1000.0 : 0;
                    callback(false, tr("DeepL did not respond in time (%1 s).")
                                        .arg(QString::number(seconds, 'g', 3)));
                } else if (status != 0 && (status < 200 || status >= 300)) {
                    callback(false, httpErrorMessage(DeepLParser::errorForHttpStatus(status),
                                                     status));
                } else if (error != QNetworkReply::NoError) {
                    callback(false, m_network ? m_network->describeNetworkError(reply)
                                              : reply->errorString());
                } else {
                    int used = 0;
                    int limit = 0;
                    if (DeepLParser::parseUsage(reply->readAll(), &used, &limit))
                        callback(true, tr("Used %1 of %2 characters.").arg(used).arg(limit));
                    else
                        callback(false, tr("DeepL returned an unexpected response."));
                }
                reply->deleteLater();
            });
}

void DeepLService::setEndpoint(const QUrl &url)
{
    m_endpointOverride = url;
}

QUrl DeepLService::defaultEndpointForKey(const QString &key)
{
    return QUrl(key.endsWith(QStringLiteral(":fx"))
                    ? QStringLiteral("https://api-free.deepl.com/v2/translate")
                    : QStringLiteral("https://api.deepl.com/v2/translate"));
}

QUrl DeepLService::endpointForKey(const QString &key) const
{
    return m_endpointOverride.isEmpty() ? defaultEndpointForKey(key) : m_endpointOverride;
}

QUrl DeepLService::usageUrlForKey(const QString &key) const
{
    QUrl url = endpointForKey(key);
    QString path = url.path();
    path.replace(QStringLiteral("/translate"), QStringLiteral("/usage"));
    url.setPath(path);
    return url;
}
