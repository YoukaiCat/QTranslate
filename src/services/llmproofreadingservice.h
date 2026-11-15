#pragma once

#include "core/proofreadingservice.h"

#include <QUrl>

class NetworkManager;

// The LLM text-checking provider (see doc/roadmap/qtranslate/12-llm-proofreading):
// the second checking provider, one Chat Completions request per check through
// the shared OpenAiChat transport and the same [llm] configuration as the
// translation service. The parser indexes the trimmed message, so the returned
// offsets are shifted back to the untrimmed request text; the messages are
// fixed and never carry the checked text, a replacement or the key.
class LlmProofreadingService : public ProofreadingService
{
    Q_OBJECT

public:
    explicit LlmProofreadingService(NetworkManager *network, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    bool isConfigured() const override;
    QString configurationError() const override;
    QStringList languages() const override;
    bool supportsAutoLanguage() const override;
    void applySettings(const AppSettings &settings) override;
    ProofreadingReply *check(const ProofreadingRequest &request) override;

    // Test seam: the resolved .../chat/completions endpoint.
    QUrl endpoint() const { return m_endpoint; }

private:
    class Reply;

    NetworkManager *m_network = nullptr;
    QUrl m_endpoint;
    QString m_apiKey;
    QString m_model;
};
