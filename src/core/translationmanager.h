#pragma once

#include "core/translationtypes.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>

class TranslationReply;
class TranslationService;

// Facade over the translation services used by AppController and the UI
// (see doc/architecture.md, §4.9): service registry, request channels with
// cancellation of stale requests, checks before any network request.
class TranslationManager : public QObject
{
    Q_OBJECT

public:
    // Independent request streams: a new request cancels the unfinished one of
    // the same channel only.
    enum class Channel { MainWindow, Popup, InPlace };

    static constexpr int kDefaultMaxLength = 20000;

    explicit TranslationManager(QObject *parent = nullptr);
    ~TranslationManager() override;

    // Takes ownership. The first registered service becomes active.
    void registerService(TranslationService *service);
    QList<TranslationService *> services() const { return m_services; }
    TranslationService *service(const QString &id) const;
    TranslationService *activeService() const { return m_active; }
    void setActiveService(const QString &id);

    // Longer text is rejected with TooLong before any request.
    int maxLength() const { return m_maxLength; }
    void setMaxLength(int length);

    // Primary/secondary languages for the automatic language switch; changing
    // either clears the in-memory cache.
    QString primaryLanguage() const { return m_primaryLanguage; }
    void setPrimaryLanguage(const QString &code);
    QString secondaryLanguage() const { return m_secondaryLanguage; }
    void setSecondaryLanguage(const QString &code);

    // Translates with the active service. The returned reply is owned by the
    // caller; it finishes with Cancelled if a newer request in the same channel
    // replaces it. finished() is never emitted from inside this call.
    TranslationReply *translate(const TranslationRequest &request, Channel channel);

    // Cancels the unfinished request of the channel, if any.
    void cancel(Channel channel);
    void cancelAll();

signals:
    void activeServiceChanged(const QString &id);

private:
    class Reply;

    QPointer<TranslationReply> &current(Channel channel);
    QString cacheKey(const QString &serviceId, const TranslationRequest &request) const;
    bool cached(const QString &key, TranslationResult *result);
    void storeCached(const QString &key, const TranslationResult &result);
    void clearCache();

    QList<TranslationService *> m_services;
    TranslationService *m_active = nullptr;
    int m_maxLength = kDefaultMaxLength;
    QString m_primaryLanguage;
    QString m_secondaryLanguage;
    QHash<QString, TranslationResult> m_cache;
    QStringList m_cacheOrder; // oldest first
    QPointer<TranslationReply> m_mainWindowReply;
    QPointer<TranslationReply> m_popupReply;
    QPointer<TranslationReply> m_inPlaceReply;
};
