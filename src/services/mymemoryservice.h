#pragma once

#include "services/queuedwebservice.h"

#include <QUrl>

class NetworkManager;

// The free MyMemory API (see doc/free-endpoints.md, §6). It needs no key, but
// it requires an explicit source language and is enabled together with the
// other free web services.
class MyMemoryService : public QueuedWebService
{
    Q_OBJECT

public:
    explicit MyMemoryService(NetworkManager *network, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    bool isConfigured() const override;
    bool requiresOptIn() const override;
    QStringList sourceLanguages() const override;
    QStringList targetLanguages() const override;
    void applySettings(const AppSettings &) override;
    TranslationReply *translate(const TranslationRequest &request) override;

    // Replaces the endpoint. For tests only: they run a local HTTP server.
    void setEndpoint(const QUrl &url);

    static QUrl defaultEndpoint();

private:
    class Reply;

    NetworkManager *m_network = nullptr;
    QUrl m_endpoint;
};
