#pragma once

#include "services/queuedwebservice.h"

#include <QElapsedTimer>
#include <QString>
#include <QUrl>

class NetworkManager;

// The unofficial Bing web endpoint (see doc/free-endpoints.md, §3). It needs
// no key, but it is a scraped browser endpoint with no SLA: the translator page
// supplies a short-lived token and a MUID cookie that the translate request
// must send back. It is enabled together with the other free web services.
//
// doc/free-endpoints.md gives no Bing-specific pause or request limit, so the
// 1000 ms pause and the 5000 UTF-16-unit chunk are conservative named
// assumptions; change them only after a measurement.
class BingWebService : public QueuedWebService
{
    Q_OBJECT

public:
    explicit BingWebService(NetworkManager *network, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    bool isConfigured() const override;
    bool requiresOptIn() const override;
    QStringList sourceLanguages() const override;
    QStringList targetLanguages() const override;
    void applySettings(const AppSettings &) override;
    TranslationReply *translate(const TranslationRequest &request) override;

    // Replaces the endpoints. For tests only: they run a local HTTP server.
    // Each empty or invalid URL falls back to its own default independently.
    void setEndpoints(const QUrl &tokenUrl, const QUrl &translateUrl);
    QUrl tokenEndpoint() const;
    QUrl translateEndpoint() const;

    static QUrl defaultTokenEndpoint();
    static QUrl defaultTranslateEndpoint();

    // The endpoint expects a browser; this is protocol emulation, not a claim
    // that QTranslate is a browser. The value is set on the Bing requests only,
    // after NetworkManager::makeRequest() has set AppInfo::userAgent().
    static QString browserUserAgent();

private:
    class Reply;

    // A token is useless without the MUID that the token page produced, and a
    // cached token skips the GET that carries the cookie; both live in one
    // entry. age restarts when the page is parsed, lifetimeMs is the value the
    // page reported (3600000 by default).
    struct CachedToken
    {
        QString ig, iid, key, token, muid;
        qint64 lifetimeMs = 0;
        QElapsedTimer age;

        bool isValid() const
        {
            return age.isValid() && age.elapsed() < lifetimeMs && !token.isEmpty();
        }
    };

    NetworkManager *m_network = nullptr;
    QUrl m_tokenEndpoint;
    QUrl m_translateEndpoint;
    CachedToken m_token;
};
