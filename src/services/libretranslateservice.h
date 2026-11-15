#pragma once

#include "services/queuedwebservice.h"

#include <QString>
#include <QUrl>

class NetworkManager;

// The LibreTranslate HTTP API of a server the user runs or selects (see the
// endpoint research in doc/roadmap/qtranslate/11-duckduckgo-libretranslate,
// §1.1). The base URL has no default: no key-less public instance answered the
// research, and a default would send the text to a volunteer server the user did
// not enter, so the provider is unconfigured until setBaseUrl() names a server.
// The API key is optional, sent as `api_key` only when set, and never changes
// isConfigured().
//
// The 5000 UTF-16-unit chunk and the 500 ms pause are conservative named
// assumptions (§1.4): the observed stock server accepted 6460 characters and did
// not throttle, so neither value is a measurement.
class LibreTranslateService : public QueuedWebService
{
    Q_OBJECT

public:
    explicit LibreTranslateService(NetworkManager *network, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    bool isConfigured() const override;
    QString configurationError() const override;
    QStringList sourceLanguages() const override;
    QStringList targetLanguages() const override;
    void applySettings(const AppSettings &) override;
    TranslationReply *translate(const TranslationRequest &request) override;

    // Stores the base URL as given. There is no default to fall back to, so an
    // empty or invalid URL is kept and makes the service unconfigured, unlike
    // MozhiService::setInstance().
    void setBaseUrl(const QUrl &url);
    QUrl baseUrl() const;
    // Stores the key as given; a whitespace-only key is trimmed when the request
    // body is built and simply omits `api_key`.
    void setApiKey(const QString &key);
    QString apiKey() const;

    static QUrl defaultBaseUrl();

private:
    class Reply;

    NetworkManager *m_network = nullptr;
    QUrl m_baseUrl;
    QString m_apiKey; // optional; never logged
};
