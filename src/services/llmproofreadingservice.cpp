#include "services/llmproofreadingservice.h"

#include "ai/openaichat.h"
#include "core/languages.h"
#include "core/settings.h"
#include "net/networkmanager.h"
#include "services/llmproofreadingparser.h"
#include "services/openaicompatibleservice.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <utility>

Q_LOGGING_CATEGORY(lcLlmProofreading, "qtranslate.service.llm-proofreading", QtWarningMsg)

namespace {

const QString kServiceId = QStringLiteral("llm");

// A fixed name for the log: never the text, an issue message or a body.
const char *errorName(TranslationError error)
{
    switch (error) {
    case TranslationError::Auth:
        return "auth";
    case TranslationError::QuotaExceeded:
        return "quota";
    case TranslationError::RateLimited:
        return "rate-limited";
    case TranslationError::ServiceUnavailable:
        return "unavailable";
    case TranslationError::Network:
        return "network";
    case TranslationError::Timeout:
        return "timeout";
    case TranslationError::Cancelled:
        return "cancelled";
    case TranslationError::BadResponse:
        return "bad-response";
    case TranslationError::TooLong:
        return "too-long";
    case TranslationError::UnsupportedLanguage:
        return "unsupported-language";
    case TranslationError::EmptyText:
        return "empty-text";
    case TranslationError::None:
        break;
    }
    return "none";
}

// The fixed detail appended to the translated ProofreadingErrorText title. The
// status number is allowed; the key, the URL and the body are not.
QString messageForHttpStatus(TranslationError error, int status)
{
    if (status == 404)
        return QStringLiteral("the server or model was not found (HTTP 404)");
    if (status == 400) {
        return QStringLiteral(
            "the provider rejected the request; the model name may be wrong (HTTP 400)");
    }

    switch (error) {
    case TranslationError::Auth:
        return QStringLiteral("the provider rejected the request (HTTP %1)").arg(status);
    case TranslationError::QuotaExceeded:
        return QStringLiteral("the provider has no available credits (HTTP %1)").arg(status);
    case TranslationError::TooLong:
        return QStringLiteral("the text is too long for the provider (HTTP %1)").arg(status);
    case TranslationError::RateLimited:
        return QStringLiteral("the provider reported a rate limit (HTTP %1)").arg(status);
    case TranslationError::Network:
        return QStringLiteral("the provider server failed (HTTP %1)").arg(status);
    default:
        return QStringLiteral("HTTP %1").arg(status);
    }
}

ProofreadingReply *finishedWithError(TranslationError error, const QString &message)
{
    ProofreadingResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    return ProofreadingReply::createFinished(result);
}

} // namespace

class LlmProofreadingService::Reply : public ProofreadingReply
{
public:
    Reply(NetworkManager *network, OpenAiChat::Request request, QString source, int begin,
          int end, QStringList categories)
        : m_network(network)
        , m_request(std::move(request))
        , m_source(std::move(source))
        , m_begin(begin)
        , m_end(end)
        , m_categories(std::move(categories))
    {
    }

    ~Reply() override { stopRequest(); }

    // Starts on the next event loop iteration: finished() must not be emitted
    // from inside check().
    void start()
    {
        m_elapsed.start();
        QMetaObject::invokeMethod(this, [this] { send(); }, Qt::QueuedConnection);
    }

protected:
    void abortImpl() override
    {
        qCDebug(lcLlmProofreading) << "Cancelled";
        stopRequest();
    }

private:
    void send();
    void onFinished();
    void fail(TranslationError error, const QString &message);
    void stopRequest();

    QPointer<NetworkManager> m_network;
    OpenAiChat::Request m_request;
    // The untrimmed request text; `m_begin`/`m_end` delimit what was sent.
    QString m_source;
    int m_begin = 0;
    int m_end = 0;
    QStringList m_categories;
    QPointer<QNetworkReply> m_httpReply;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_requestElapsed;
};

void LlmProofreadingService::Reply::send()
{
    if (isFinished())
        return;
    if (!m_network) {
        fail(TranslationError::Network, QStringLiteral("network is not available"));
        return;
    }

    QNetworkRequest request = m_network->makeRequest(m_request.endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json;charset=UTF-8"));
    request.setRawHeader("Accept", "application/json");
    if (!m_request.apiKey.trimmed().isEmpty()) {
        request.setRawHeader("Authorization",
                             "Bearer " + m_request.apiKey.trimmed().toUtf8());
    }
    if (!m_request.referer.isEmpty())
        request.setRawHeader("HTTP-Referer", m_request.referer.toUtf8());
    if (!m_request.title.isEmpty())
        request.setRawHeader("X-Title", m_request.title.toUtf8());

    m_requestElapsed.start();
    m_httpReply = m_network->nam()->post(request, OpenAiChat::serialize(m_request));
    connect(m_httpReply, &QNetworkReply::finished, this, &Reply::onFinished);
}

void LlmProofreadingService::Reply::onFinished()
{
    QNetworkReply *reply = m_httpReply;
    m_httpReply = nullptr;
    if (!reply)
        return;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError error = reply->error();
    // A timed-out or refused transfer has no readable device.
    const QByteArray body =
        status != 0 || error == QNetworkReply::NoError ? reply->readAll() : QByteArray();
    reply->deleteLater();
    if (isFinished())
        return;

    qCDebug(lcLlmProofreading) << "HTTP" << status << error << "in" << m_requestElapsed.elapsed()
                               << "ms";

    // abort() finishes the reply before stopping the request, so a cancelled
    // request here means the transfer timeout fired.
    if (error == QNetworkReply::OperationCanceledError || error == QNetworkReply::TimeoutError) {
        const double seconds = m_network ? m_network->timeoutMs() / 1000.0 : 0;
        fail(TranslationError::Timeout,
             QStringLiteral("the provider did not respond within %1 s")
                 .arg(QString::number(seconds, 'g', 3)));
        return;
    }
    if (status != 0 && (status < 200 || status >= 300)) {
        const TranslationError statusError = OpenAiChat::errorForHttpStatus(status);
        fail(statusError, messageForHttpStatus(statusError, status));
        return;
    }
    if (error != QNetworkReply::NoError) {
        fail(TranslationError::Network,
             m_network ? m_network->describeNetworkError(reply, false)
                       : QStringLiteral("Network error"));
        return;
    }

    // A truncated answer is TooLong here, before its content is interpreted: a
    // cut-off JSON body would otherwise reach the parser as plain text and the
    // diff would rewrite the whole text with half an answer.
    const OpenAiChat::Response response = OpenAiChat::parse(body);
    if (!response.ok()) {
        if (response.errorCode > 0) {
            const TranslationError mapped = OpenAiChat::errorForHttpStatus(response.errorCode);
            fail(mapped, messageForHttpStatus(mapped, response.errorCode));
        } else {
            fail(response.error, response.errorMessage);
        }
        return;
    }

    const QString sent = m_source.mid(m_begin, m_end - m_begin);
    const LlmProofreading::Parsed parsed =
        LlmProofreading::parseResponse(sent, response.text, m_categories);
    if (parsed.status == LlmProofreading::Status::Malformed) {
        fail(TranslationError::BadResponse,
             QStringLiteral("the model did not return the expected JSON"));
        return;
    }
    if (parsed.status == LlmProofreading::Status::Edits && parsed.issues.isEmpty()
        && parsed.droppedEdits > 0) {
        // The model did return changes, but none of them could be placed in the
        // text; reporting "the text is clean" would hide a real correction.
        fail(TranslationError::BadResponse,
             QStringLiteral("the model returned changes that do not match the text"));
        return;
    }

    ProofreadingResult result;
    result.serviceId = kServiceId;
    result.issues = parsed.issues;
    // The parser indexed the trimmed message; the window and the controller
    // apply the issues to the untrimmed text.
    for (ProofreadingIssue &issue : result.issues)
        issue.offset += m_begin;
    if (parsed.status == LlmProofreading::Status::PlainText) {
        // The model's answer re-wrapped in the source's surrounding whitespace.
        result.correctedText =
            m_source.left(m_begin) + parsed.correctedText + m_source.mid(m_end);
    }

    qCDebug(lcLlmProofreading) << "Checked" << result.issues.size() << "issue(s),"
                               << parsed.droppedEdits << "dropped edit(s),"
                               << parsed.filteredEdits << "filtered edit(s) in"
                               << m_elapsed.elapsed() << "ms, tokens" << response.promptTokens
                               << "+" << response.completionTokens;
    finish(result);
}

void LlmProofreadingService::Reply::fail(TranslationError error, const QString &message)
{
    qCWarning(lcLlmProofreading) << "Check failed (" << errorName(error) << "):" << message;
    ProofreadingResult result;
    result.serviceId = kServiceId;
    result.error = error;
    result.errorMessage = message;
    finish(result);
}

void LlmProofreadingService::Reply::stopRequest()
{
    if (QNetworkReply *reply = m_httpReply) {
        m_httpReply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

LlmProofreadingService::LlmProofreadingService(NetworkManager *network, QObject *parent)
    : ProofreadingService(parent)
    , m_network(network)
    , m_endpoint(OpenAiCompatibleService::defaultEndpoint())
{
}

QString LlmProofreadingService::id() const
{
    return kServiceId;
}

QString LlmProofreadingService::displayName() const
{
    return QStringLiteral("LLM");
}

bool LlmProofreadingService::isConfigured() const
{
    // The endpoint always ends up valid because of the fallback, so in practice
    // this is false only without a model. A missing key is not a configuration
    // failure: a local server needs none, and a remote one answers 401.
    return !m_model.isEmpty() && m_endpoint.isValid() && !m_endpoint.host().isEmpty();
}

QString LlmProofreadingService::configurationError() const
{
    return QStringLiteral("the LLM provider is configured on the AI tab");
}

QStringList LlmProofreadingService::languages() const
{
    return Languages::supported(kServiceId, Languages::Direction::Source);
}

bool LlmProofreadingService::supportsAutoLanguage() const
{
    return true;
}

void LlmProofreadingService::applySettings(const AppSettings &settings)
{
    m_apiKey = settings.llm.apiKey.trimmed();
    m_model = settings.llm.model.trimmed();

    QString baseUrl = settings.llm.baseUrl.trimmed();
    if (baseUrl.isEmpty())
        baseUrl = OpenAiCompatibleService::defaultBaseUrl();

    const QUrl endpoint = OpenAiChat::chatCompletionsUrl(baseUrl);
    m_endpoint = endpoint.isValid() && !endpoint.host().isEmpty()
        ? endpoint
        : OpenAiCompatibleService::defaultEndpoint();
}

ProofreadingReply *LlmProofreadingService::check(const ProofreadingRequest &request)
{
    const QString source = request.text;
    qsizetype begin = 0;
    qsizetype end = source.size();
    while (begin < end && source.at(begin).isSpace())
        ++begin;
    while (end > begin && source.at(end - 1).isSpace())
        --end;
    if (begin == end)
        return finishedWithError(TranslationError::EmptyText, QString());

    // The internal code is passed to systemPrompt() unchanged: "auto", an empty
    // code and every catalogue code are resolved inside the parser.
    if (request.language != Languages::autoCode() && !languages().contains(request.language)) {
        return finishedWithError(TranslationError::UnsupportedLanguage,
                                 QStringLiteral("language \"%1\" is not supported")
                                     .arg(request.language));
    }

    OpenAiChat::Request chat;
    chat.endpoint = m_endpoint;
    chat.apiKey = m_apiKey;
    chat.model = m_model;
    chat.temperature = 0.0;
    // maxTokens stays 0: the request carries no max_tokens, so a provider that
    // stops at its own limit is reported as TooLong by OpenAiChat::parse().
    chat.messages = {
        {QStringLiteral("system"),
         LlmProofreading::systemPrompt(request.language, request.categories)},
        {QStringLiteral("user"), source.mid(begin, end - begin)},
    };
    chat.referer = QStringLiteral("https://github.com/youkaicat/QTranslate");
    chat.title = QStringLiteral("QTranslate");

    // Only the length, the language, the category count and the model: never
    // the text itself.
    qCDebug(lcLlmProofreading) << "Checking" << source.size() << "chars, language"
                               << request.language << "with" << request.categories.size()
                               << "requested category filter(s), model" << m_model;

    auto *reply = new Reply(m_network, std::move(chat), source, int(begin), int(end),
                            request.categories);
    reply->start();
    return reply;
}
