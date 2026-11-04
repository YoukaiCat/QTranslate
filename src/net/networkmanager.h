#pragma once

#include <QNetworkRequest>
#include <QObject>

class QNetworkAccessManager;
class QUrl;

// Owns the application's single QNetworkAccessManager and the common request
// parameters (see doc/architecture.md, §4.10). Proxy support arrives in stage 7.
class NetworkManager : public QObject
{
    Q_OBJECT

public:
    static constexpr int kDefaultTimeoutMs = 10000;

    explicit NetworkManager(QObject *parent = nullptr);
    ~NetworkManager() override;

    QNetworkAccessManager *nam() const { return m_nam; }

    // A request with the transfer timeout and User-Agent set.
    QNetworkRequest makeRequest(const QUrl &url) const;

    // Aborts a request when no data has been transferred for this long.
    int timeoutMs() const { return m_timeoutMs; }
    void setTimeoutMs(int ms);

private:
    QNetworkAccessManager *m_nam = nullptr;
    int m_timeoutMs = kDefaultTimeoutMs;
};
