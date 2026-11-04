#include "core/translationmanager.h"
#include "core/translationservice.h"

#include <QPointer>
#include <QSignalSpy>
#include <QTest>

#include <memory>
#include <utility>

namespace {

// A reply the test finishes by hand.
class FakeReply : public TranslationReply
{
public:
    using TranslationReply::TranslationReply;

    void complete(const QString &text)
    {
        TranslationResult result;
        result.translatedText = text;
        result.serviceId = QStringLiteral("fake");
        finish(result);
    }

    int abortCount = 0;

protected:
    void abortImpl() override { ++abortCount; }
};

class FakeService : public TranslationService
{
public:
    explicit FakeService(QString id = QStringLiteral("fake"), bool configured = true)
        : m_id(std::move(id))
        , m_configured(configured)
    {
    }

    QString id() const override { return m_id; }
    QString displayName() const override { return m_id; }
    bool isConfigured() const override { return m_configured; }
    QStringList sourceLanguages() const override { return {QStringLiteral("en")}; }
    QStringList targetLanguages() const override { return {QStringLiteral("ru")}; }
    void applySettings(const AppSettings &) override {}

    TranslationReply *translate(const TranslationRequest &request) override
    {
        requests << request;
        auto *reply = new FakeReply;
        replies << reply;
        return reply;
    }

    QList<TranslationRequest> requests;
    QList<QPointer<FakeReply>> replies;

private:
    QString m_id;
    bool m_configured;
};

TranslationRequest request(const QString &text)
{
    return {text, QStringLiteral("auto"), QStringLiteral("ru")};
}

} // namespace

class TestTranslationManager : public QObject
{
    Q_OBJECT

private slots:
    void registry()
    {
        TranslationManager manager;
        QSignalSpy changed(&manager, &TranslationManager::activeServiceChanged);
        QVERIFY(!manager.activeService());

        auto *first = new FakeService(QStringLiteral("first"));
        auto *second = new FakeService(QStringLiteral("second"));
        manager.registerService(first);
        manager.registerService(second);

        QCOMPARE(first->parent(), &manager);
        QCOMPARE(manager.services(), (QList<TranslationService *>{first, second}));
        QCOMPARE(manager.activeService(), first);
        QCOMPARE(manager.service(QStringLiteral("second")), second);
        QVERIFY(!manager.service(QStringLiteral("none")));

        manager.setActiveService(QStringLiteral("second"));
        QCOMPARE(manager.activeService(), second);
        manager.setActiveService(QStringLiteral("second"));
        manager.setActiveService(QStringLiteral("none"));
        QCOMPARE(manager.activeService(), second);
        QCOMPARE(changed.size(), 2);
        QCOMPARE(changed.at(1).at(0).toString(), QStringLiteral("second"));
    }

    void translateUsesActiveService()
    {
        TranslationManager manager;
        auto *service = new FakeService;
        manager.registerService(service);

        std::unique_ptr<TranslationReply> reply(
            manager.translate(request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        QCOMPARE(service->requests.size(), 1);
        QCOMPARE(service->requests.first().text, QStringLiteral("Hello"));
        QCOMPARE(service->requests.first().targetLang, QStringLiteral("ru"));

        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        service->replies.first()->complete(QStringLiteral("Привет"));
        QCOMPARE(finished.size(), 1);
        QVERIFY(reply->isFinished());
        QCOMPARE(reply->result().translatedText, QStringLiteral("Привет"));
    }

    void newRequestCancelsStaleOneInSameChannel()
    {
        TranslationManager manager;
        auto *service = new FakeService;
        manager.registerService(service);

        std::unique_ptr<TranslationReply> first(
            manager.translate(request(QStringLiteral("one")), TranslationManager::Channel::Popup));
        QSignalSpy firstFinished(first.get(), &TranslationReply::finished);

        std::unique_ptr<TranslationReply> second(
            manager.translate(request(QStringLiteral("two")), TranslationManager::Channel::Popup));
        QSignalSpy secondFinished(second.get(), &TranslationReply::finished);

        QCOMPARE(firstFinished.size(), 1);
        QCOMPARE(first->result().error, TranslationError::Cancelled);
        QCOMPARE(service->replies.at(0)->abortCount, 1);

        // A late answer to the cancelled request changes nothing.
        service->replies.at(0)->complete(QStringLiteral("stale"));
        QCOMPARE(firstFinished.size(), 1);
        QCOMPARE(first->result().error, TranslationError::Cancelled);
        QVERIFY(first->result().translatedText.isEmpty());

        service->replies.at(1)->complete(QStringLiteral("два"));
        QCOMPARE(secondFinished.size(), 1);
        QVERIFY(second->result().ok());
        QCOMPARE(second->result().translatedText, QStringLiteral("два"));
    }

    void channelsAreIndependent()
    {
        TranslationManager manager;
        auto *service = new FakeService;
        manager.registerService(service);

        std::unique_ptr<TranslationReply> main(
            manager.translate(request(QStringLiteral("one")), TranslationManager::Channel::MainWindow));
        std::unique_ptr<TranslationReply> popup(
            manager.translate(request(QStringLiteral("two")), TranslationManager::Channel::Popup));
        QVERIFY(!main->isFinished());
        QCOMPARE(service->replies.at(0)->abortCount, 0);

        manager.cancel(TranslationManager::Channel::Popup);
        QCOMPARE(popup->result().error, TranslationError::Cancelled);
        QVERIFY(!main->isFinished());

        manager.cancelAll();
        QCOMPARE(main->result().error, TranslationError::Cancelled);
    }

    void finishedRequestIsNotAborted()
    {
        TranslationManager manager;
        auto *service = new FakeService;
        manager.registerService(service);

        std::unique_ptr<TranslationReply> first(
            manager.translate(request(QStringLiteral("one")), TranslationManager::Channel::MainWindow));
        service->replies.at(0)->complete(QStringLiteral("один"));
        std::unique_ptr<TranslationReply> second(
            manager.translate(request(QStringLiteral("two")), TranslationManager::Channel::MainWindow));
        QCOMPARE(service->replies.at(0)->abortCount, 0);
        QVERIFY(first->result().ok());
    }

    void replyDeletedByCallerBeforeNextRequest()
    {
        TranslationManager manager;
        auto *service = new FakeService;
        manager.registerService(service);

        delete manager.translate(request(QStringLiteral("one")), TranslationManager::Channel::MainWindow);
        QVERIFY(service->replies.at(0).isNull());
        std::unique_ptr<TranslationReply> second(
            manager.translate(request(QStringLiteral("two")), TranslationManager::Channel::MainWindow));
        QVERIFY(!second->isFinished());
        manager.cancelAll();
        QCOMPARE(second->result().error, TranslationError::Cancelled);
    }

    void checksBeforeRequest_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("configured");
        QTest::addColumn<int>("error");

        QTest::newRow("empty") << QString() << true << int(TranslationError::EmptyText);
        QTest::newRow("whitespace") << QStringLiteral(" \n\t ") << true
                                    << int(TranslationError::EmptyText);
        QTest::newRow("too long") << QString(101, u'a') << true << int(TranslationError::TooLong);
        QTest::newRow("not configured") << QStringLiteral("Hello") << false
                                        << int(TranslationError::Auth);
    }

    void checksBeforeRequest()
    {
        QFETCH(QString, text);
        QFETCH(bool, configured);
        QFETCH(int, error);

        TranslationManager manager;
        manager.setMaxLength(100);
        auto *service = new FakeService(QStringLiteral("fake"), configured);
        manager.registerService(service);

        std::unique_ptr<TranslationReply> reply(
            manager.translate(request(text), TranslationManager::Channel::MainWindow));
        QVERIFY(service->requests.isEmpty());

        // The error arrives through the event loop, after the caller has connected.
        QVERIFY(!reply->isFinished());
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(int(reply->result().error), error);
        QCOMPARE(reply->result().serviceId, QStringLiteral("fake"));
    }

    void maxLengthBoundary()
    {
        TranslationManager manager;
        manager.setMaxLength(5);
        auto *service = new FakeService;
        manager.registerService(service);
        std::unique_ptr<TranslationReply> reply(
            manager.translate(request(QStringLiteral("12345")), TranslationManager::Channel::MainWindow));
        QCOMPARE(service->requests.size(), 1);
        QCOMPARE(manager.maxLength(), 5);
        manager.setMaxLength(0);
        QCOMPARE(manager.maxLength(), TranslationManager::kDefaultMaxLength);
    }

    void earlyErrorCanBeCancelled()
    {
        TranslationManager manager;
        manager.registerService(new FakeService);

        std::unique_ptr<TranslationReply> empty(
            manager.translate(request(QString()), TranslationManager::Channel::MainWindow));
        QSignalSpy finished(empty.get(), &TranslationReply::finished);
        std::unique_ptr<TranslationReply> next(
            manager.translate(request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));

        QCOMPARE(finished.size(), 1);
        QCOMPARE(empty->result().error, TranslationError::Cancelled);
        // The queued EmptyText result is dropped: still exactly one finished().
        QTest::qWait(20);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(empty->result().error, TranslationError::Cancelled);
    }

    void abortIsIdempotent()
    {
        FakeReply reply;
        QSignalSpy finished(&reply, &TranslationReply::finished);
        reply.abort();
        reply.abort();
        reply.complete(QStringLiteral("late"));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(reply.abortCount, 1);
        QCOMPARE(reply.result().error, TranslationError::Cancelled);
    }
};

QTEST_GUILESS_MAIN(TestTranslationManager)
#include "tst_translationmanager.moc"
