#include "net/networkmanager.h"

#include "core/appinfo.h"

#include <QNetworkAccessManager>
#include <QUrl>

NetworkManager::NetworkManager(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

NetworkManager::~NetworkManager() = default;

QNetworkRequest NetworkManager::makeRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setTransferTimeout(m_timeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, AppInfo::userAgent());
    return request;
}

void NetworkManager::setTimeoutMs(int ms)
{
    m_timeoutMs = ms > 0 ? ms : kDefaultTimeoutMs;
}
