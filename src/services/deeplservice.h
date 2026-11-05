#pragma once

#include "core/translationservice.h"

#include <QUrl>

#include <functional>

class DeepLWebService;
class NetworkManager;

// DeepL API or the unofficial free web endpoint, selected by
// settings.deepl.mode (see doc/architecture.md, §4.7).
class DeepLService : public TranslationService
{
    Q_OBJECT

public:
    using KeyCheckCallback = std::function<void(bool ok, const QString &message)>;

    explicit DeepLService(NetworkManager *network, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    bool isConfigured() const override;
    QString configurationError() const override;
    QStringList sourceLanguages() const override;
    QStringList targetLanguages() const override;
    void applySettings(const AppSettings &settings) override;
    TranslationReply *translate(const TranslationRequest &request) override;

    // Checks an arbitrary key (usually the value currently being edited in the
    // settings dialog) with GET /v2/usage. Asynchronous; the callback is not
    // called if the service is destroyed first.
    void checkKey(const QString &key, KeyCheckCallback callback);

    // Free keys end with ":fx" and use api-free.deepl.com; others use api.deepl.com.
    static QUrl defaultEndpointForKey(const QString &key);

    // Replaces the translate endpoint. For tests only.
    void setEndpoint(const QUrl &url);
    void setWebEndpoint(const QUrl &url);
    void setWebRateLimitMs(int ms);

private:
    QUrl endpointForKey(const QString &key) const;
    QUrl usageUrlForKey(const QString &key) const;

    NetworkManager *m_network = nullptr;
    DeepLWebService *m_web = nullptr;
    QString m_mode = QStringLiteral("api");
    QString m_apiKey;
    QString m_englishVariant = QStringLiteral("EN-US");
    QUrl m_endpointOverride;
};
