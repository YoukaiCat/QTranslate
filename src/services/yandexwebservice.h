#pragma once

#include "services/queuedwebservice.h"

#include <QUrl>

class NetworkManager;

// The unofficial Yandex web endpoint (see doc/free-endpoints.md, §2). It needs
// no key, but it is a scraped browser endpoint with no SLA, so it is enabled
// together with the other free web services. Yandex has one Chinese code and
// one Portuguese code, which is a known limitation of the language branch.
//
// The 750 ms pause between requests comes from doc/free-endpoints.md, §8. The
// 5000 UTF-16-unit request limit is a conservative assumption: the doc gives no
// Yandex-specific value and it has not been measured.
class YandexWebService : public QueuedWebService
{
    Q_OBJECT

public:
    explicit YandexWebService(NetworkManager *network, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    bool isConfigured() const override;
    bool requiresOptIn() const override;
    QStringList sourceLanguages() const override;
    QStringList targetLanguages() const override;
    void applySettings(const AppSettings &) override;
    TranslationReply *translate(const TranslationRequest &request) override;

    // Replaces the endpoint. For tests only: they run a local HTTP server.
    // An empty or invalid URL falls back to the default.
    void setEndpoint(const QUrl &url);
    QUrl endpoint() const;

    static QUrl defaultEndpoint();

    // The endpoint expects a browser; this is protocol emulation, not a claim
    // that QTranslate is a browser. The value is set on this service's requests
    // only, after NetworkManager::makeRequest() has set AppInfo::userAgent().
    static QString browserUserAgent();

private:
    class Reply;

    NetworkManager *m_network = nullptr;
    QUrl m_endpoint;
};
