#pragma once

#include "core/proofreadingtypes.h"

#include <QObject>
#include <QStringList>

struct AppSettings;

// An asynchronous text check in progress. Owned by whoever requested it:
// delete it (deleteLater) after finished(), or delete it earlier to drop the
// request.
class ProofreadingReply : public QObject
{
    Q_OBJECT

public:
    explicit ProofreadingReply(QObject *parent = nullptr);

    // A reply that finishes with `result` on the next event loop iteration, so
    // the caller of check() has time to connect to finished().
    static ProofreadingReply *createFinished(const ProofreadingResult &result,
                                             QObject *parent = nullptr);

    const ProofreadingResult &result() const { return m_result; }
    bool isFinished() const { return m_finished; }

    // Finishes the reply with TranslationError::Cancelled (if it has not
    // finished yet) and stops the underlying work.
    void abort();

signals:
    // Emitted exactly once. Never emitted from inside ProofreadingService::check().
    void finished();

protected:
    // Sets the result and emits finished(). Later calls are ignored.
    void finish(const ProofreadingResult &result);

    // Stops the underlying work (network requests). Called by abort() after
    // the reply has already finished as cancelled, so whatever the stopped work
    // reports afterwards is ignored.
    virtual void abortImpl() {}

private:
    ProofreadingResult m_result;
    bool m_finished = false;
};

// A text checking service (LanguageTool, ...). Adding a service means adding
// one class implementing this interface (see doc/proofreading.md, §4).
class ProofreadingService : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual QString id() const = 0; // "languagetool" (stage 6 adds "llm")
    virtual QString displayName() const = 0;
    virtual bool isConfigured() const = 0; // False when the provider's settings are incomplete.
    // Optional user-facing hint when isConfigured() is false.
    virtual QString configurationError() const { return {}; }
    virtual QStringList languages() const = 0; // internal codes, without "auto"
    virtual bool supportsAutoLanguage() const { return false; }

    // Sends the text to a remote service; the manager refuses it while
    // proofreading/allowOnlineServices is false. A future local engine would
    // return true; no provider of this stage does.
    virtual bool isLocal() const { return false; }

    // Applies the endpoint and category changes without recreating the service.
    virtual void applySettings(const AppSettings &settings) = 0;

    // Returns a reply owned by the caller. Errors detected before any request
    // (e.g. unsupported language) are reported through the reply as well.
    virtual ProofreadingReply *check(const ProofreadingRequest &request) = 0;
};
