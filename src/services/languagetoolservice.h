#pragma once

#include "core/proofreadingservice.h"

#include <QUrl>

class NetworkManager;
class RequestQueue;

// The LanguageTool /v2/check provider (see doc/proofreading.md, §3): the public
// API or a self-hosted server, without any credentials. The endpoint accepts
// "auto" for language detection; the detected language is not exposed because
// ProofreadingResult has no field for it. The service applies the category
// filter and the chunk-offset shift to the parser's issues, and it never copies
// a response body into a message or a log line.
class LanguageToolService : public ProofreadingService
{
    Q_OBJECT

public:
    explicit LanguageToolService(NetworkManager *network, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    bool isConfigured() const override;
    QStringList languages() const override;
    bool supportsAutoLanguage() const override;
    bool isLocal() const override;
    void applySettings(const AppSettings &settings) override;
    ProofreadingReply *check(const ProofreadingRequest &request) override;

    // Replaces the endpoint; an empty or invalid URL falls back to the default.
    // Unlike applySettings(), it does not change the queue pause.
    void setEndpoint(const QUrl &url);
    QUrl endpoint() const { return m_endpoint; }
    static QUrl defaultEndpoint();

    // Test seam: the pause between requests to one host.
    void setRateLimitMs(int ms);
    int rateLimitMs() const;

private:
    class Reply;

    void enqueue(Reply *reply);
    void noteRequestSent();

    NetworkManager *m_network = nullptr;
    RequestQueue *m_queue = nullptr;
    QUrl m_endpoint;
};
