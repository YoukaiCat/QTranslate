#pragma once

#include "core/translationservice.h"

#include <QUrl>

#include <functional>

class NetworkManager;

// Translation through any OpenAI-compatible Chat Completions API. OpenRouter
// is only the default endpoint; the same adapter serves OpenAI, TeamRouter,
// Ollama, llama.cpp and similar servers. The transport will be reused by the
// future AI actions; only translation is exposed for now
// (see doc/llm-actions.md, §5 and §11).
class OpenAiCompatibleService : public TranslationService
{
    Q_OBJECT

public:
    using ModelsCallback =
        std::function<void(bool ok, const QString &message, const QStringList &models)>;

    explicit OpenAiCompatibleService(NetworkManager *network, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    bool isConfigured() const override;
    QString configurationError() const override;
    QStringList sourceLanguages() const override;
    QStringList targetLanguages() const override;
    void applySettings(const AppSettings &settings) override;
    TranslationReply *translate(const TranslationRequest &request) override;

    // Checks the key with GET /key (OpenRouter only) and then loads GET /models
    // for the given base URL. An empty base URL uses the current settings. The
    // callback is not called if the service is destroyed first.
    void loadModels(const QString &baseUrl, const QString &key, ModelsCallback callback);

    static QString defaultBaseUrl();
    static QUrl defaultEndpoint();
    static QUrl defaultModelsEndpoint();
    static QUrl defaultKeyEndpoint();

    // Replaces endpoints. For tests only.
    void setEndpoint(const QUrl &url);
    void setModelsEndpoint(const QUrl &url);
    void setKeyEndpoint(const QUrl &url);

private:
    class Reply;

    static QString httpErrorMessage(TranslationError error, int status);
    static bool isOpenRouterHost(const QString &host);
    static bool isLocalHost(const QString &host);
    bool isOpenRouterEndpoint() const;
    bool isLocalEndpoint() const;
    QUrl keyEndpointForModels(const QUrl &modelsEndpoint) const;
    void requestModels(const QUrl &modelsEndpoint, const QString &key,
                       ModelsCallback callback);

    NetworkManager *m_network = nullptr;
    QString m_apiKey;
    QString m_model;
    bool m_openRouterEndpoint = false;
    QUrl m_endpoint;
    QUrl m_modelsEndpoint;
    QUrl m_keyEndpoint;
};
