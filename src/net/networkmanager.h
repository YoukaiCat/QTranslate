#pragma once

#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QObject>
#include <QString>

#include <functional>

class QAuthenticator;
class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

enum class ProxyMode { None, System, Manual };
enum class ProxyType { Http, Socks5 };

struct ProxySettings
{
    ProxyMode mode = ProxyMode::System;
    ProxyType type = ProxyType::Http;
    QString host;
    quint16 port = 0;
    bool authEnabled = false;
    QString user;
    QString password;
};

// Owns the application's single QNetworkAccessManager and the common request
// parameters (see doc/architecture.md, §4.10).
class NetworkManager : public QObject
{
    Q_OBJECT

public:
    using TestCallback = std::function<void(bool ok, const QString &message)>;

    static constexpr int kDefaultTimeoutMs = 10000;

    explicit NetworkManager(QObject *parent = nullptr);
    ~NetworkManager() override;

    QNetworkAccessManager *nam() const { return m_nam; }

    // A request with the transfer timeout and User-Agent set.
    QNetworkRequest makeRequest(const QUrl &url) const;

    // Applies the proxy to the application manager and drops keep-alive
    // connections that bypass it.
    void applyProxy(const ProxySettings &settings);
    const ProxySettings &proxySettings() const { return m_proxySettings; }

    // Checks a proxy without saving it: a temporary QNAM sends a request to
    // `url`; any HTTP response counts as success.
    void testProxy(const ProxySettings &settings, const QUrl &url, TestCallback callback);

    // Aborts a request when no data has been transferred for this long.
    int timeoutMs() const { return m_timeoutMs; }
    void setTimeoutMs(int ms);

    // Proxy errors become short user-facing texts; other errors keep Qt's text
    // unless `includeDetails` is false (Cloud URLs contain an API key).
    QString describeNetworkError(QNetworkReply *reply, bool includeDetails = true) const;

private:
    void onProxyAuthenticationRequired(const QNetworkProxy &proxy, QAuthenticator *authenticator);

    QNetworkAccessManager *m_nam = nullptr;
    ProxySettings m_proxySettings;
    int m_timeoutMs = kDefaultTimeoutMs;
    bool m_proxyAuthAttempted = false;
};
