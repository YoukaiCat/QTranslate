#pragma once

#include "services/queuedwebservice.h"

#include <QUrl>

class NetworkManager;

// The free Mozhi API (see doc/free-endpoints.md, §5): one integration gives
// access to several engines through a public or self-hosted instance. It needs
// no key, but it sends text to a third party and is enabled together with the
// other free web services.
//
// The 500 ms pause between requests and the 5000 UTF-16-unit request limit are
// conservative assumptions. doc/free-endpoints.md gives no Mozhi-specific
// values (§5, §8), and neither one has been measured against an instance.
class MozhiService : public QueuedWebService
{
    Q_OBJECT

public:
    explicit MozhiService(NetworkManager *network, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    bool isConfigured() const override;
    bool requiresOptIn() const override;
    QStringList sourceLanguages() const override;
    QStringList targetLanguages() const override;
    void applySettings(const AppSettings &) override;
    TranslationReply *translate(const TranslationRequest &request) override;

    // Replaces the instance; an empty or invalid URL falls back to the default.
    void setInstance(const QUrl &url);
    QUrl instance() const;
    // Trims the engine id; an empty string falls back to the default.
    void setEngine(const QString &engine);
    QString engine() const;

    static QUrl defaultInstance();
    static QString defaultEngine();

private:
    class Reply;

    NetworkManager *m_network = nullptr;
    QUrl m_instance;
    QString m_engine;
};
