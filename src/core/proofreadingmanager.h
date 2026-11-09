#pragma once

#include "core/proofreadingtypes.h"

#include <QList>
#include <QObject>
#include <QPointer>

class ProofreadingReply;
class ProofreadingService;

// Facade over the proofreading services used by AppController and the UI (see
// doc/proofreading.md, §4): service registry, request channels with
// cancellation of stale requests, checks before any network request.
class ProofreadingManager : public QObject
{
    Q_OBJECT

public:
    // Independent request streams: a new request cancels the unfinished one of
    // the same channel only. The external-selection scenario has a confirmation
    // window and reuses InPlace.
    enum class Channel { MainWindow, InPlace };

    explicit ProofreadingManager(QObject *parent = nullptr);
    ~ProofreadingManager() override;

    // Takes ownership. The first registered service becomes active.
    void registerService(ProofreadingService *service);
    QList<ProofreadingService *> services() const { return m_services; }
    ProofreadingService *service(const QString &id) const;
    ProofreadingService *activeService() const { return m_active; }
    void setActiveService(const QString &id);

    // Whether services with isLocal() == false are allowed. The manager refuses
    // them with ServiceUnavailable while this is false.
    bool onlineServicesAllowed() const { return m_onlineServicesAllowed; }
    void setOnlineServicesAllowed(bool allowed);

    // Checks with the active service. The returned reply is owned by the
    // caller; it finishes with Cancelled if a newer request in the same channel
    // replaces it. finished() is never emitted from inside this call.
    ProofreadingReply *check(const ProofreadingRequest &request, Channel channel);

    // Cancels the unfinished request of the channel, if any.
    void cancel(Channel channel);
    void cancelAll();

signals:
    void activeServiceChanged(const QString &id);

private:
    QPointer<ProofreadingReply> &current(Channel channel);

    QList<ProofreadingService *> m_services;
    ProofreadingService *m_active = nullptr;
    bool m_onlineServicesAllowed = false;
    QPointer<ProofreadingReply> m_mainWindowReply;
    QPointer<ProofreadingReply> m_inPlaceReply;
};
