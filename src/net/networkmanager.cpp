#include "net/networkmanager.h"

#include "core/appinfo.h"

#include <QAuthenticator>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QSharedPointer>
#include <QTimer>
#include <QUrl>

namespace {

void configureProxy(QNetworkAccessManager *nam, const ProxySettings &settings)
{
    switch (settings.mode) {
    case ProxyMode::None:
        QNetworkProxyFactory::setUseSystemConfiguration(false);
        nam->setProxy(QNetworkProxy::NoProxy);
        break;
    case ProxyMode::System:
        QNetworkProxyFactory::setUseSystemConfiguration(true);
        nam->setProxy(QNetworkProxy(QNetworkProxy::DefaultProxy));
        break;
    case ProxyMode::Manual: {
        QNetworkProxyFactory::setUseSystemConfiguration(false);
        QNetworkProxy proxy(settings.type == ProxyType::Socks5
                                ? QNetworkProxy::Socks5Proxy
                                : QNetworkProxy::HttpProxy,
                            settings.host, settings.port);
        if (settings.authEnabled) {
            proxy.setUser(settings.user);
            proxy.setPassword(settings.password);
        }
        nam->setProxy(proxy);
        break;
    }
    }
}

} // namespace

NetworkManager::NetworkManager(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    connect(m_nam, &QNetworkAccessManager::proxyAuthenticationRequired, this,
            &NetworkManager::onProxyAuthenticationRequired);
}

NetworkManager::~NetworkManager() = default;

QNetworkRequest NetworkManager::makeRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setTransferTimeout(m_timeoutMs);
    // The free Google endpoint occasionally answers HTTP/2 requests with 429;
    // HTTP/1.1 behaves like the browser extensions that use it.
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setHeader(QNetworkRequest::UserAgentHeader, AppInfo::userAgent());
    return request;
}

void NetworkManager::applyProxy(const ProxySettings &settings)
{
    m_proxySettings = settings;
    m_proxyAuthAttempted = false;
    configureProxy(m_nam, settings);
    m_nam->clearConnectionCache();
}

void NetworkManager::testProxy(const ProxySettings &settings, const QUrl &url,
                               TestCallback callback)
{
    auto *nam = new QNetworkAccessManager(this);
    configureProxy(nam, settings);

    auto authAttempted = QSharedPointer<bool>::create(false);
    connect(nam, &QNetworkAccessManager::proxyAuthenticationRequired, nam,
            [settings, authAttempted](const QNetworkProxy &, QAuthenticator *authenticator) {
                if (settings.authEnabled && !*authAttempted) {
                    authenticator->setUser(settings.user);
                    authenticator->setPassword(settings.password);
                    *authAttempted = true;
                } else {
                    authenticator->setUser(QString());
                    authenticator->setPassword(QString());
                }
            });

    QNetworkReply *reply = nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, callback] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // A 404/403 from the test URL still proves that the proxy forwarded
        // the request. Only a proxy-level failure (407) or no HTTP response at
        // all means the proxy does not work.
        if (reply->error() == QNetworkReply::ProxyAuthenticationRequiredError) {
            callback(false, describeNetworkError(reply));
        } else if (status > 0) {
            callback(true, tr("Proxy connection works (HTTP %1).").arg(status));
        } else if (reply->error() != QNetworkReply::NoError) {
            callback(false, describeNetworkError(reply));
        } else {
            callback(false, tr("The proxy test returned no HTTP response."));
        }
        reply->deleteLater();
        nam->deleteLater();
    });
}

void NetworkManager::setTimeoutMs(int ms)
{
    m_timeoutMs = ms > 0 ? ms : kDefaultTimeoutMs;
}

QString NetworkManager::describeNetworkError(QNetworkReply *reply, bool includeDetails) const
{
    if (!reply)
        return tr("Network error");
    switch (reply->error()) {
    case QNetworkReply::ProxyAuthenticationRequiredError:
        return tr("Proxy authentication failed");
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::ProxyTimeoutError:
        return tr("Proxy connection failed");
    default:
        return includeDetails ? reply->errorString() : tr("Network error");
    }
}

void NetworkManager::onProxyAuthenticationRequired(const QNetworkProxy &,
                                                   QAuthenticator *authenticator)
{
    if (m_proxySettings.mode == ProxyMode::Manual && m_proxySettings.authEnabled
        && !m_proxyAuthAttempted) {
        authenticator->setUser(m_proxySettings.user);
        authenticator->setPassword(m_proxySettings.password);
        m_proxyAuthAttempted = true;
        // Allow later requests to retry the credentials after a failure.
        QTimer::singleShot(2000, this, [this] { m_proxyAuthAttempted = false; });
        return;
    }

    // Do not loop forever on rejected credentials: Qt will fail the request
    // with ProxyAuthenticationRequiredError, which becomes a clear message.
    authenticator->setUser(QString());
    authenticator->setPassword(QString());
}
