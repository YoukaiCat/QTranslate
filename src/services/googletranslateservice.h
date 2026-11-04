#pragma once

#include "core/translationservice.h"

#include <QUrl>

class NetworkManager;

// Google Translate through the free "gtx" endpoint used by browser extensions
// (see doc/architecture.md, §4.6). The Cloud API mode arrives in stage 6.
class GoogleTranslateService : public TranslationService
{
    Q_OBJECT

public:
    // Longer text is split into parts of at most this many characters, sent one by one.
    static constexpr int kMaxChunkLength = 5000;

    explicit GoogleTranslateService(NetworkManager *network, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    bool isConfigured() const override;
    QStringList sourceLanguages() const override;
    QStringList targetLanguages() const override;
    TranslationReply *translate(const TranslationRequest &request) override;

    // Replaces the endpoint URL. For tests only: they run a local HTTP server.
    void setEndpoint(const QUrl &url);

private:
    NetworkManager *m_network = nullptr;
    QUrl m_endpoint;
};
