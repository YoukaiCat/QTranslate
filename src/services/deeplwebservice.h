#pragma once

#include "services/queuedwebservice.h"

#include <QUrl>

class NetworkManager;

// Unofficial free DeepL web endpoint (see doc/free-endpoints.md, §1).
// It needs no API key and is available as a normal service, like the free
// Google endpoint. The UI marks it as an unofficial endpoint.
class DeepLWebService : public QueuedWebService
{
    Q_OBJECT

public:
    explicit DeepLWebService(NetworkManager *network, QObject *parent = nullptr);
    DeepLWebService(NetworkManager *network, QString serviceId, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    bool isConfigured() const override;
    QStringList sourceLanguages() const override;
    QStringList targetLanguages() const override;
    void applySettings(const AppSettings &settings) override;
    TranslationReply *translate(const TranslationRequest &request) override;

    // Replaces the endpoint. For tests only.
    void setEndpoint(const QUrl &url);
    // Retry delay. For tests only.
    void setRetryDelayMs(int ms);

    static QUrl defaultEndpoint();
    // Converts an official DeepL code to the case the web endpoint expects:
    // "DE" -> "de", "PT-BR" -> "pt-BR", "ZH-HANS" -> "zh-Hans".
    static QString toWebCode(const QString &deeplCode);

private:
    class Reply;

    NetworkManager *m_network = nullptr;
    QString m_serviceId = QStringLiteral("deepl-web");
    QString m_englishVariant = QStringLiteral("EN-US");
    QUrl m_endpoint;
    int m_retryDelayMs = 3000;
};
