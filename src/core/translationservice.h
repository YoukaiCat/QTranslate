#pragma once

#include "core/translationtypes.h"

#include <QObject>
#include <QStringList>

// An asynchronous translation in progress. Owned by whoever requested it:
// delete it (deleteLater) after finished(), or delete it earlier to drop the request.
class TranslationReply : public QObject
{
    Q_OBJECT

public:
    explicit TranslationReply(QObject *parent = nullptr);

    // A reply that finishes with `result` on the next event loop iteration, so
    // the caller of translate() has time to connect to finished().
    static TranslationReply *createFinished(const TranslationResult &result,
                                            QObject *parent = nullptr);

    const TranslationResult &result() const { return m_result; }
    bool isFinished() const { return m_finished; }

    // Finishes the reply with TranslationError::Cancelled (if it has not
    // finished yet) and stops the underlying work.
    void abort();

signals:
    // Emitted exactly once. Never emitted from inside TranslationService::translate().
    void finished();

protected:
    // Sets the result and emits finished(). Later calls are ignored.
    void finish(const TranslationResult &result);

    // Stops the underlying work (network requests). Called by abort() after
    // the reply has already finished as cancelled, so whatever the stopped work
    // reports afterwards is ignored.
    virtual void abortImpl() {}

private:
    TranslationResult m_result;
    bool m_finished = false;
};

// A translation service (Google, DeepL, ...). Adding a service means adding
// one class implementing this interface (see doc/architecture.md, §4.5).
class TranslationService : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual QString id() const = 0; // "google", "deepl"
    virtual QString displayName() const = 0;
    virtual bool isConfigured() const = 0;           // e.g. DeepL without an API key: false
    virtual QStringList sourceLanguages() const = 0; // internal codes, without "auto"
    virtual QStringList targetLanguages() const = 0;

    // Returns a reply owned by the caller. Errors detected before any request
    // (e.g. unsupported language) are reported through the reply as well.
    virtual TranslationReply *translate(const TranslationRequest &request) = 0;
};
