#pragma once

#include "core/translationservice.h"

class RequestQueue;

// A service whose replies are serialized per host. One instance handles one
// host: at most one reply is active, and the next one starts no sooner than
// rateLimitMs() after the last network request (see doc/architecture.md, §4.5).
//
// A reply holds its slot from start() until finished(), so a multi-chunk
// protocol sends its chunks sequentially inside one slot and paces them with
// rateLimitMs() instead of enqueuing every chunk as a separate reply. Replies
// call noteRequestSent() right before every network request, including retries
// and later chunks, so the pause is measured from the actual request.
// setRateLimitMs(0) disables the pause for tests only.
class QueuedWebService : public TranslationService
{
    Q_OBJECT

public:
    // A translation that occupies one queue slot. start() is called when the
    // slot is free; the reply reports each request with noteRequestSent().
    class Reply : public TranslationReply
    {
    public:
        using TranslationReply::TranslationReply;

        virtual void start() = 0;
    };

    explicit QueuedWebService(QObject *parent = nullptr);

    void setRateLimitMs(int ms);
    int rateLimitMs() const;

    // Records that the reply has just handed a request to the network.
    void noteRequestSent();

protected:
    void enqueue(Reply *reply);

private:
    RequestQueue *m_queue = nullptr;
};
