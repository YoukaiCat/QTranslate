#include "core/settings.h"
#include "core/translationtypes.h"
#include "services/queuedwebservice.h"

#include <QElapsedTimer>
#include <QTest>
#include <QTimer>

#include <memory>

namespace {

struct ReplyStats
{
    int starts = 0;
    QList<qint64> noteMs;
    qint64 startMs = -1;
    qint64 finishMs = -1;
    TranslationError error = TranslationError::None;
};

class FakeQueuedService;

// A reply that plays a request without the network: every chunk reports a
// request with noteRequestSent(), later chunks wait for rateLimitMs(), and the
// reply finishes holdMs after its last request.
class FakeReply : public QueuedWebService::Reply
{
public:
    FakeReply(QueuedWebService *service, const QElapsedTimer *clock, ReplyStats *stats,
              int chunks, int holdMs)
        : m_service(service)
        , m_clock(clock)
        , m_stats(stats)
        , m_chunks(chunks)
        , m_holdMs(holdMs)
    {
    }

    void start() override;

private:
    void sendChunk();

    QPointer<QueuedWebService> m_service;
    const QElapsedTimer *m_clock = nullptr;
    ReplyStats *m_stats = nullptr;
    int m_chunks = 1;
    int m_holdMs = 10;
    int m_sentChunks = 0;
};

class FakeQueuedService : public QueuedWebService
{
public:
    QString id() const override { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("Fake"); }
    bool isConfigured() const override { return true; }
    QStringList sourceLanguages() const override { return {}; }
    QStringList targetLanguages() const override { return {}; }
    void applySettings(const AppSettings &) override {}

    TranslationReply *translate(const TranslationRequest &) override
    {
        auto *reply = new FakeReply(this, clock, nextStats, nextChunks, nextHoldMs);
        enqueue(reply);
        return reply;
    }

    const QElapsedTimer *clock = nullptr;
    int nextChunks = 1;
    int nextHoldMs = 10;
    ReplyStats *nextStats = nullptr;
};

void FakeReply::start()
{
    ++m_stats->starts;
    m_stats->startMs = m_clock->elapsed();
    sendChunk();
}

void FakeReply::sendChunk()
{
    if (m_service)
        m_service->noteRequestSent();
    m_stats->noteMs.append(m_clock->elapsed());

    if (++m_sentChunks < m_chunks) {
        const int pause = m_service ? m_service->rateLimitMs() : 0;
        QTimer::singleShot(pause, this, [this] {
            if (!isFinished())
                sendChunk();
        });
        return;
    }

    QTimer::singleShot(m_holdMs, this, [this] {
        if (isFinished())
            return;
        m_stats->finishMs = m_clock->elapsed();
        TranslationResult result;
        if (m_service)
            result.serviceId = m_service->id();
        finish(result);
        m_stats->error = result.error;
    });
}

} // namespace

class TestQueuedWebService : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_clock.start();
        m_service = std::make_unique<FakeQueuedService>();
        m_service->clock = &m_clock;
    }

    void cleanup()
    {
        m_service.reset();
    }

    void startsRepliesOneAtATime()
    {
        m_service->setRateLimitMs(0);
        ReplyStats firstStats;
        ReplyStats secondStats;
        m_service->nextHoldMs = 60;
        m_service->nextStats = &firstStats;
        std::unique_ptr<TranslationReply> first(
            m_service->translate({QStringLiteral("One"), QStringLiteral("en"),
                                  QStringLiteral("de")}));
        m_service->nextHoldMs = 10;
        m_service->nextStats = &secondStats;
        std::unique_ptr<TranslationReply> second(
            m_service->translate({QStringLiteral("Two"), QStringLiteral("en"),
                                  QStringLiteral("de")}));

        QTRY_VERIFY_WITH_TIMEOUT(firstStats.starts == 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(secondStats.starts == 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(first->isFinished() && second->isFinished(), 5000);

        QCOMPARE(firstStats.starts, 1);
        QCOMPARE(secondStats.starts, 1);
        QVERIFY2(secondStats.startMs >= firstStats.finishMs,
                 qPrintable(QStringLiteral("second start %1 ms, first finish %2 ms")
                                .arg(secondStats.startMs)
                                .arg(firstStats.finishMs)));
        QCOMPARE(first->result().error, TranslationError::None);
        QCOMPARE(second->result().error, TranslationError::None);
    }

    void rateLimitDelaysTheNextReply()
    {
        m_service->setRateLimitMs(100);
        ReplyStats firstStats;
        ReplyStats secondStats;
        m_service->nextHoldMs = 10;
        m_service->nextStats = &firstStats;
        std::unique_ptr<TranslationReply> first(
            m_service->translate({QStringLiteral("One"), QStringLiteral("en"),
                                  QStringLiteral("de")}));
        m_service->nextStats = &secondStats;
        std::unique_ptr<TranslationReply> second(
            m_service->translate({QStringLiteral("Two"), QStringLiteral("en"),
                                  QStringLiteral("de")}));

        QTRY_VERIFY_WITH_TIMEOUT(secondStats.starts == 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(second->isFinished(), 5000);

        QCOMPARE(firstStats.noteMs.size(), 1);
        const qint64 delta = secondStats.startMs - firstStats.noteMs.last();
        QVERIFY2(delta >= 90, qPrintable(QStringLiteral("delta %1 ms").arg(delta)));
    }

    void abortedQueuedReplyIsSkipped()
    {
        m_service->setRateLimitMs(0);
        ReplyStats firstStats;
        ReplyStats secondStats;
        m_service->nextHoldMs = 60;
        m_service->nextStats = &firstStats;
        std::unique_ptr<TranslationReply> first(
            m_service->translate({QStringLiteral("One"), QStringLiteral("en"),
                                  QStringLiteral("de")}));
        m_service->nextHoldMs = 10;
        m_service->nextStats = &secondStats;
        std::unique_ptr<TranslationReply> second(
            m_service->translate({QStringLiteral("Two"), QStringLiteral("en"),
                                  QStringLiteral("de")}));

        second->abort();
        QCOMPARE(second->result().error, TranslationError::Cancelled);
        QTRY_VERIFY_WITH_TIMEOUT(first->isFinished(), 5000);
        QCOMPARE(firstStats.starts, 1);
        QCOMPARE(secondStats.starts, 0);
    }

    void deletingTheActiveReplyStartsTheNext()
    {
        m_service->setRateLimitMs(0);
        ReplyStats firstStats;
        ReplyStats secondStats;
        m_service->nextHoldMs = 1000;
        m_service->nextStats = &firstStats;
        std::unique_ptr<TranslationReply> first(
            m_service->translate({QStringLiteral("One"), QStringLiteral("en"),
                                  QStringLiteral("de")}));
        m_service->nextHoldMs = 10;
        m_service->nextStats = &secondStats;
        std::unique_ptr<TranslationReply> second(
            m_service->translate({QStringLiteral("Two"), QStringLiteral("en"),
                                  QStringLiteral("de")}));

        QCOMPARE(firstStats.starts, 1);
        QCOMPARE(secondStats.starts, 0);

        first.reset();

        QTRY_VERIFY_WITH_TIMEOUT(secondStats.starts == 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(second->isFinished(), 5000);
        QCOMPARE(second->result().error, TranslationError::None);
    }

    void noteRequestSentRestartsThePause()
    {
        m_service->setRateLimitMs(100);
        ReplyStats chunkedStats;
        ReplyStats queuedStats;
        m_service->nextChunks = 2;
        m_service->nextHoldMs = 10;
        m_service->nextStats = &chunkedStats;
        std::unique_ptr<TranslationReply> chunked(
            m_service->translate({QStringLiteral("One"), QStringLiteral("en"),
                                  QStringLiteral("de")}));
        m_service->nextChunks = 1;
        m_service->nextStats = &queuedStats;
        std::unique_ptr<TranslationReply> queued(
            m_service->translate({QStringLiteral("Two"), QStringLiteral("en"),
                                  QStringLiteral("de")}));

        QTRY_VERIFY_WITH_TIMEOUT(queuedStats.starts == 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(queued->isFinished(), 5000);

        QCOMPARE(chunkedStats.noteMs.size(), 2);
        const qint64 chunkDelta = chunkedStats.noteMs.at(1) - chunkedStats.noteMs.at(0);
        QVERIFY2(chunkDelta >= 90,
                 qPrintable(QStringLiteral("chunk delta %1 ms").arg(chunkDelta)));
        QVERIFY2(queuedStats.startMs >= chunkedStats.finishMs,
                 qPrintable(QStringLiteral("queued start %1 ms, chunked finish %2 ms")
                                .arg(queuedStats.startMs)
                                .arg(chunkedStats.finishMs)));
        const qint64 pauseDelta = queuedStats.startMs - chunkedStats.noteMs.last();
        QVERIFY2(pauseDelta >= 90,
                 qPrintable(QStringLiteral("delta %1 ms").arg(pauseDelta)));
        QCOMPARE(chunked->result().error, TranslationError::None);
        QCOMPARE(queued->result().error, TranslationError::None);
    }

private:
    QElapsedTimer m_clock;
    std::unique_ptr<FakeQueuedService> m_service;
};

QTEST_GUILESS_MAIN(TestQueuedWebService)
#include "tst_queuedwebservice.moc"
