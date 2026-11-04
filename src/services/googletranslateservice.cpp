#include "services/googletranslateservice.h"

#include "core/languages.h"
#include "core/settings.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/googleparser.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QUrlQuery>

#include <utility>

Q_LOGGING_CATEGORY(lcGoogle, "qtranslate.service.google", QtWarningMsg)

namespace {

const QString kServiceId = QStringLiteral("google");

// Sends the parts of one translation sequentially and joins the results.
class GoogleReply : public TranslationReply
{
public:
    GoogleReply(NetworkManager *network, QUrl url, QStringList parts, QString source,
                QString target, bool cloud)
        : m_network(network)
        , m_url(std::move(url))
        , m_parts(std::move(parts))
        , m_source(std::move(source))
        , m_target(std::move(target))
        , m_cloud(cloud)
    {
    }

    ~GoogleReply() override { stopRequest(); }

    // Starts on the next event loop iteration: finished() must not be emitted
    // from inside translate(), even if nothing needs to be sent.
    void start()
    {
        m_elapsed.start();
        QMetaObject::invokeMethod(this, [this] { sendNext(); }, Qt::QueuedConnection);
    }

protected:
    void abortImpl() override
    {
        qCDebug(lcGoogle) << "Cancelled at part" << m_index + 1 << "of" << m_parts.size();
        stopRequest();
    }

private:
    void sendNext();
    void onRequestFinished();
    void fail(TranslationError error, const QString &message);
    void stopRequest();

    QPointer<NetworkManager> m_network;
    QUrl m_url;
    QStringList m_parts;
    QString m_source;
    QString m_target;
    bool m_cloud = false;
    qsizetype m_index = 0;
    QString m_trailing; // trailing whitespace of the part being translated, kept out of the request
    TranslationResult m_result;
    QPointer<QNetworkReply> m_request;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_requestElapsed;
};

void GoogleReply::sendNext()
{
    if (isFinished())
        return;
    if (!m_network) {
        fail(TranslationError::Network, QStringLiteral("network is not available"));
        return;
    }

    for (; m_index < m_parts.size(); ++m_index) {
        // Whitespace at the edges of a part is not sent but put back when
        // joining, so the formatting at the joints survives.
        const QString &part = m_parts.at(m_index);
        qsizetype begin = 0;
        while (begin < part.size() && part.at(begin).isSpace())
            ++begin;
        if (begin == part.size()) {
            m_result.translatedText += part;
            continue;
        }
        qsizetype end = part.size();
        while (part.at(end - 1).isSpace())
            --end;

        m_result.translatedText += part.left(begin);
        m_trailing = part.mid(end);

        QNetworkRequest request = m_network->makeRequest(m_url);
        QByteArray body;
        if (m_cloud) {
            request.setHeader(QNetworkRequest::ContentTypeHeader,
                              QStringLiteral("application/json;charset=UTF-8"));
            QJsonObject object;
            object.insert(QStringLiteral("q"), part.mid(begin, end - begin));
            object.insert(QStringLiteral("target"), m_target);
            object.insert(QStringLiteral("format"), QStringLiteral("text"));
            if (m_source != Languages::autoCode())
                object.insert(QStringLiteral("source"), m_source);
            body = QJsonDocument(object).toJson(QJsonDocument::Compact);
        } else {
            request.setHeader(QNetworkRequest::ContentTypeHeader,
                              QStringLiteral("application/x-www-form-urlencoded;charset=UTF-8"));
            body = "q=" + QUrl::toPercentEncoding(part.mid(begin, end - begin));
        }

        m_requestElapsed.start();
        m_request = m_network->nam()->post(request, body);
        connect(m_request, &QNetworkReply::finished, this, &GoogleReply::onRequestFinished);
        return;
    }

    qCDebug(lcGoogle) << "Translated" << m_parts.size() << "part(s) in" << m_elapsed.elapsed()
                      << "ms, result" << m_result.translatedText.size() << "chars";
    m_result.serviceId = kServiceId;
    finish(m_result);
}

void GoogleReply::onRequestFinished()
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
    qCDebug(lcGoogle) << "Part" << m_index + 1 << "of" << m_parts.size() << ": HTTP" << status
                      << error << "in" << m_requestElapsed.elapsed() << "ms";

    const QByteArray responseBody = request->readAll();

    // abort() finishes the reply before stopping the request, so a cancelled
    // request here means the transfer timeout fired.
    if (error == QNetworkReply::OperationCanceledError || error == QNetworkReply::TimeoutError) {
        const double seconds = m_network ? m_network->timeoutMs() / 1000.0 : 0;
        fail(TranslationError::Timeout,
             QStringLiteral("no response from %1 within %2 s")
                 .arg(m_url.host(), QString::number(seconds, 'g', 3)));
        return;
    }
    if (status != 0 && (status < 200 || status >= 300)) {
        fail(m_cloud ? GoogleParser::errorForCloudResponse(status, responseBody)
                     : GoogleParser::errorForFreeHttpStatus(status),
             QStringLiteral("HTTP %1").arg(status));
        return;
    }
    if (error != QNetworkReply::NoError) {
        // In Cloud mode the URL contains the API key; do not leak it through
        // QNetworkReply::errorString().
        fail(TranslationError::Network,
             m_cloud ? QStringLiteral("network error") : request->errorString());
        return;
    }

    const TranslationResult part = m_cloud
        ? GoogleParser::parseCloudResponse(responseBody)
        : GoogleParser::parseFreeResponse(responseBody);
    if (!part.ok()) {
        fail(part.error, part.errorMessage);
        return;
    }
    m_result.translatedText += part.translatedText + m_trailing;
    if (m_result.detectedSourceLang.isEmpty())
        m_result.detectedSourceLang = part.detectedSourceLang;
    ++m_index;
    sendNext();
}

void GoogleReply::fail(TranslationError error, const QString &message)
{
    qCWarning(lcGoogle) << "Translation failed at part" << m_index + 1 << "of" << m_parts.size()
                        << ":" << message;
    TranslationResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    finish(result);
}

void GoogleReply::stopRequest()
{
    if (QNetworkReply *request = m_request) {
        m_request = nullptr;
        request->disconnect(this);
        request->abort();
        request->deleteLater();
    }
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

GoogleTranslateService::GoogleTranslateService(NetworkManager *network, QObject *parent)
    : TranslationService(parent)
    , m_network(network)
    , m_endpoint(QStringLiteral("https://translate.googleapis.com/translate_a/single"))
    , m_cloudEndpoint(
          QStringLiteral("https://translation.googleapis.com/language/translate/v2"))
{
}

QString GoogleTranslateService::id() const
{
    return kServiceId;
}

QString GoogleTranslateService::displayName() const
{
    return QStringLiteral("Google Translate");
}

bool GoogleTranslateService::isConfigured() const
{
    return m_mode == Mode::Free || !m_apiKey.isEmpty();
}

void GoogleTranslateService::applySettings(const AppSettings &settings)
{
    m_mode = settings.google.mode == QStringLiteral("cloud") ? Mode::Cloud : Mode::Free;
    m_apiKey = settings.google.apiKey;
}

QStringList GoogleTranslateService::sourceLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Source);
}

QStringList GoogleTranslateService::targetLanguages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Target);
}

TranslationReply *GoogleTranslateService::translate(const TranslationRequest &request)
{
    if (m_mode == Mode::Cloud && m_apiKey.isEmpty()) {
        return finishedWithError(TranslationError::Auth,
                                 QStringLiteral("Google Cloud needs an API key"));
    }

    const QString source = request.sourceLang == Languages::autoCode()
                               ? Languages::autoCode()
                               : Languages::toServiceCode(kServiceId, request.sourceLang,
                                                          Languages::Direction::Source);
    const QString target = Languages::toServiceCode(kServiceId, request.targetLang,
                                                    Languages::Direction::Target);
    if (source.isEmpty() || target.isEmpty()) {
        const QString code = source.isEmpty() ? request.sourceLang : request.targetLang;
        return finishedWithError(TranslationError::UnsupportedLanguage,
                                 QStringLiteral("language \"%1\" is not supported").arg(code));
    }

    QUrl url;
    if (m_mode == Mode::Cloud) {
        url = m_cloudEndpoint;
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("key"), m_apiKey);
        url.setQuery(query);
    } else {
        url = m_endpoint;
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("client"), QStringLiteral("gtx"));
        query.addQueryItem(QStringLiteral("sl"), source);
        query.addQueryItem(QStringLiteral("tl"), target);
        query.addQueryItem(QStringLiteral("dt"), QStringLiteral("t"));
        url.setQuery(query);
    }

    const QStringList parts = TextChunker::split(request.text, kMaxChunkLength);
    qCDebug(lcGoogle) << "Translating" << request.text.size() << "chars in" << parts.size()
                      << "part(s)," << source << "->" << target
                      << (m_mode == Mode::Cloud ? "cloud" : "free");

    auto *reply = new GoogleReply(m_network, url, parts, source, target,
                                  m_mode == Mode::Cloud);
    reply->start();
    return reply;
}

void GoogleTranslateService::setEndpoint(const QUrl &url)
{
    m_endpoint = url;
}

void GoogleTranslateService::setCloudEndpoint(const QUrl &url)
{
    m_cloudEndpoint = url;
}
