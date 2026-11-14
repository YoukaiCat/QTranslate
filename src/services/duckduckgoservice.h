#pragma once

#include "services/queuedwebservice.h"

#include <QString>
#include <QUrl>

class NetworkManager;

// The unofficial DuckDuckGo translation frontend (see doc/free-endpoints.md).
// It needs no key, but it is the search page's own route: a `vqd` token is
// scraped out of https://duckduckgo.com/?q=translate with a browser
// User-Agent, and the text is then posted to translation.js, which is not
// documented, versioned or supported.
//
// The token lives without a lifetime of its own: the research proved one older
// than 26 minutes, so a rejected token is refreshed once on the next 403
// instead of inventing an expiry.
//
// The body limit lies between 986 and 2006 characters, so the 900 UTF-16-unit
// chunk is below the lowest proven success and the 1000 ms pause is a
// conservative named assumption; change them only after a measurement.
class DuckDuckGoService : public QueuedWebService
{
    Q_OBJECT

public:
    explicit DuckDuckGoService(NetworkManager *network, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    bool isConfigured() const override;
    QStringList sourceLanguages() const override;
    QStringList targetLanguages() const override;
    void applySettings(const AppSettings &) override;
    TranslationReply *translate(const TranslationRequest &request) override;

    // Replaces the endpoint. For tests only: they run a local HTTP server. An
    // empty or invalid URL falls back to defaultEndpoint(), and the path always
    // ends with a slash so QUrl::resolved() cannot climb out of it.
    void setEndpoint(const QUrl &url);
    QUrl endpoint() const;

    static QUrl defaultEndpoint();

    // The endpoint expects a browser; this is protocol emulation, not a claim
    // that QTranslate is a browser. The value is set on the DuckDuckGo requests
    // only, after NetworkManager::makeRequest() has set AppInfo::userAgent().
    static QString browserUserAgent();

private:
    class Reply;

    NetworkManager *m_network = nullptr;
    QUrl m_endpoint;
    QString m_token; // cached vqd; no lifetime of its own
};
