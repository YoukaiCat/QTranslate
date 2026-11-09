#include "core/translationmanager.h"
#include "core/translationservice.h"
#include "net/networkmanager.h"
#include "services/mymemoryservice.h"

#include <QLoggingCategory>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

#include <memory>
#include <utility>

namespace {

// A reply the test finishes by hand. It carries the emitting service's id so a
// fallback result can be told apart from the primary one.
class FakeReply : public TranslationReply
{
public:
    explicit FakeReply(QString serviceId = QStringLiteral("fake"))
        : m_serviceId(std::move(serviceId))
    {
    }

    void complete(const QString &text, const QString &detected = QString())
    {
        TranslationResult result;
        result.translatedText = text;
        result.detectedSourceLang = detected;
        result.serviceId = m_serviceId;
        finish(result);
    }

    void completeError(TranslationError error, const QString &message)
    {
        TranslationResult result;
        result.error = error;
        result.errorMessage = message;
        result.serviceId = m_serviceId;
        finish(result);
    }

    int abortCount = 0;

protected:
    void abortImpl() override { ++abortCount; }

private:
    QString m_serviceId;
};

class FakeService : public TranslationService
{
public:
    explicit FakeService(QString id = QStringLiteral("fake"), bool configured = true,
                         bool optIn = false)
        : m_id(std::move(id))
        , m_configured(configured)
        , m_optIn(optIn)
    {
    }

    QString id() const override { return m_id; }
    QString displayName() const override { return m_id; }
    bool isConfigured() const override { return m_configured; }
    bool requiresOptIn() const override { return m_optIn; }
    QStringList sourceLanguages() const override { return {QStringLiteral("en")}; }
    QStringList targetLanguages() const override { return {QStringLiteral("ru")}; }
    void applySettings(const AppSettings &) override {}

    TranslationReply *translate(const TranslationRequest &request) override
    {
        requests << request;
        auto *reply = new FakeReply(m_id);
        replies << reply;
        return reply;
    }

    QList<TranslationRequest> requests;
    QList<QPointer<FakeReply>> replies;

private:
    QString m_id;
    bool m_configured;
    bool m_optIn;
};

TranslationRequest request(const QString &text)
{
    return {text, QStringLiteral("auto"), QStringLiteral("ru")};
}

// A configured primary and an opt-in fallback wired for the fallback tests.
struct FallbackPair
{
    FakeService *primary = nullptr;
    FakeService *fallback = nullptr;
};

FallbackPair withFallback(TranslationManager &manager)
{
    auto *primary = new FakeService(QStringLiteral("primary"));
    auto *fallback = new FakeService(QStringLiteral("free"), true, true);
    manager.registerService(primary);
    manager.registerService(fallback);
    manager.setFallbackService(fallback->id());
    manager.setFreeServicesAllowed(true);
    return {primary, fallback};
}

// Captures the application log lines for the hygiene test.
QStringList g_logMessages;

void collectMessage(QtMsgType, const QMessageLogContext &, const QString &message)
{
    g_logMessages << message;
}

struct LogCapture
{
    QtMessageHandler previous;

    LogCapture()
        : previous(qInstallMessageHandler(collectMessage))
    {
        g_logMessages.clear();
    }

    ~LogCapture() { qInstallMessageHandler(previous); }
};

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
        std::unique_ptr<TranslationReply> inPlace(
            manager.translate(request(QStringLiteral("three")), TranslationManager::Channel::InPlace));
        QVERIFY(!main->isFinished());
        QCOMPARE(service->replies.at(0)->abortCount, 0);

        manager.cancel(TranslationManager::Channel::Popup);
        QCOMPARE(popup->result().error, TranslationError::Cancelled);
        QVERIFY(!main->isFinished());
        QVERIFY(!inPlace->isFinished());

        manager.cancelAll();
        QCOMPARE(main->result().error, TranslationError::Cancelled);
        QCOMPARE(inPlace->result().error, TranslationError::Cancelled);
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

    void optInServiceIsRefusedWhenDisabled()
    {
        TranslationManager manager;
        QVERIFY(!manager.freeServicesAllowed());
        auto *service = new FakeService(QStringLiteral("free"), true, true);
        manager.registerService(service);

        std::unique_ptr<TranslationReply> reply(
            manager.translate(request(QStringLiteral("Hello")),
                              TranslationManager::Channel::MainWindow));
        QVERIFY(service->requests.isEmpty());

        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::ServiceUnavailable);
        QCOMPARE(reply->result().serviceId, QStringLiteral("free"));
        QVERIFY(reply->result().errorMessage.contains(QStringLiteral("Settings")));

        // The refusal is not cached: with the gate on the request goes through.
        manager.setFreeServicesAllowed(true);
        std::unique_ptr<TranslationReply> second(
            manager.translate(request(QStringLiteral("Hello")),
                              TranslationManager::Channel::MainWindow));
        QCOMPARE(service->requests.size(), 1);
        service->replies.first()->complete(QStringLiteral("Привет"));
        QCOMPARE(second->result().translatedText, QStringLiteral("Привет"));
    }

    void optInServiceWorksWhenEnabled()
    {
        TranslationManager manager;
        manager.setFreeServicesAllowed(true);
        auto *service = new FakeService(QStringLiteral("free"), true, true);
        manager.registerService(service);

        auto first = std::unique_ptr<TranslationReply>(
            manager.translate(request(QStringLiteral("Hello")),
                              TranslationManager::Channel::MainWindow));
        QCOMPARE(service->requests.size(), 1);
        service->replies.first()->complete(QStringLiteral("Привет"));
        QVERIFY(first->result().ok());

        auto second = std::unique_ptr<TranslationReply>(
            manager.translate(request(QStringLiteral("Hello")),
                              TranslationManager::Channel::MainWindow));
        QSignalSpy finished(second.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(second->result().translatedText, QStringLiteral("Привет"));
        QCOMPARE(service->requests.size(), 1);
    }

    void gatedRealServiceIsRefused()
    {
        NetworkManager network;
        TranslationManager manager;
        QVERIFY(!manager.freeServicesAllowed());
        auto *service = new MyMemoryService(&network);
        manager.registerService(service);
        manager.setActiveService(QStringLiteral("mymemory"));

        std::unique_ptr<TranslationReply> reply(
            manager.translate(request(QStringLiteral("Hello")),
                              TranslationManager::Channel::MainWindow));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::ServiceUnavailable);
        QCOMPARE(reply->result().serviceId, QStringLiteral("mymemory"));
        QVERIFY(reply->result().errorMessage.contains(QStringLiteral("Settings")));
        QVERIFY(reply->result().translatedText.isEmpty());
    }

    void regularServiceIsNotAffectedByTheGate()
    {
        TranslationManager manager;
        QVERIFY(!manager.freeServicesAllowed());
        auto *service = new FakeService;
        manager.registerService(service);

        auto reply = std::unique_ptr<TranslationReply>(
            manager.translate(request(QStringLiteral("Hello")),
                              TranslationManager::Channel::MainWindow));
        QCOMPARE(service->requests.size(), 1);
        service->replies.first()->complete(QStringLiteral("Привет"));
        QVERIFY(reply->result().ok());
        QCOMPARE(reply->result().translatedText, QStringLiteral("Привет"));
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

    void cacheReturnsSuccessfulTranslation()
    {
        TranslationManager manager;
        auto *service = new FakeService;
        manager.registerService(service);

        auto first = std::unique_ptr<TranslationReply>(
            manager.translate(request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        service->replies.first()->complete(QStringLiteral("Привет"));
        QVERIFY(first->result().ok());

        auto second = std::unique_ptr<TranslationReply>(
            manager.translate(request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        QSignalSpy finished(second.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(second->result().translatedText, QStringLiteral("Привет"));
        QCOMPARE(service->requests.size(), 1);
    }

    void errorsAreNotCached()
    {
        TranslationManager manager;
        auto *service = new FakeService;
        manager.registerService(service);

        auto first = std::unique_ptr<TranslationReply>(
            manager.translate(request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        service->replies.first()->completeError(TranslationError::Network, QStringLiteral("offline"));
        QCOMPARE(first->result().error, TranslationError::Network);

        auto second = std::unique_ptr<TranslationReply>(
            manager.translate(request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        QCOMPARE(service->requests.size(), 2);
        service->replies.at(1)->complete(QStringLiteral("Привет"));
        QCOMPARE(second->result().translatedText, QStringLiteral("Привет"));
    }

    void smartRetryUsesSecondaryLanguage()
    {
        TranslationManager manager;
        manager.setPrimaryLanguage(QStringLiteral("ru"));
        manager.setSecondaryLanguage(QStringLiteral("en"));
        auto *service = new FakeService;
        manager.registerService(service);

        auto reply = std::unique_ptr<TranslationReply>(manager.translate(
            {QStringLiteral("Привет"), QStringLiteral("auto"), QStringLiteral("ru")},
            TranslationManager::Channel::MainWindow));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        service->replies.first()->complete(QStringLiteral("Привет"), QStringLiteral("ru"));
        QCOMPARE(finished.size(), 0);
        QCOMPARE(service->requests.size(), 2);
        QCOMPARE(service->requests.at(1).targetLang, QStringLiteral("en"));

        service->replies.at(1)->complete(QStringLiteral("Hello"), QStringLiteral("ru"));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hello"));
    }

    void smartRetryFallsBackToFirstResult()
    {
        TranslationManager manager;
        manager.setPrimaryLanguage(QStringLiteral("ru"));
        manager.setSecondaryLanguage(QStringLiteral("en"));
        auto *service = new FakeService;
        manager.registerService(service);

        auto reply = std::unique_ptr<TranslationReply>(manager.translate(
            {QStringLiteral("Привет"), QStringLiteral("auto"), QStringLiteral("ru")},
            TranslationManager::Channel::MainWindow));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        service->replies.first()->complete(QStringLiteral("Привет"), QStringLiteral("ru"));
        service->replies.at(1)->completeError(TranslationError::Network, QStringLiteral("offline"));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(reply->result().translatedText, QStringLiteral("Привет"));
    }

    void languageChangeClearsCache()
    {
        TranslationManager manager;
        auto *service = new FakeService;
        manager.registerService(service);
        auto first = std::unique_ptr<TranslationReply>(
            manager.translate(request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        service->replies.first()->complete(QStringLiteral("Привет"));

        manager.setPrimaryLanguage(QStringLiteral("de"));
        auto second = std::unique_ptr<TranslationReply>(
            manager.translate(request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        QCOMPARE(service->requests.size(), 2);
        service->replies.at(1)->complete(QStringLiteral("Hallo"));
        QCOMPARE(second->result().translatedText, QStringLiteral("Hallo"));
    }

    void fallbackOnRetryableErrors_data()
    {
        QTest::addColumn<int>("error");

        QTest::newRow("auth") << int(TranslationError::Auth);
        QTest::newRow("quota") << int(TranslationError::QuotaExceeded);
        QTest::newRow("rate limited") << int(TranslationError::RateLimited);
        QTest::newRow("service unavailable") << int(TranslationError::ServiceUnavailable);
        QTest::newRow("network") << int(TranslationError::Network);
        QTest::newRow("timeout") << int(TranslationError::Timeout);
    }

    void fallbackOnRetryableErrors()
    {
        QFETCH(int, error);

        TranslationManager manager;
        const FallbackPair services = withFallback(manager);

        auto reply = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QCOMPARE(services.primary->requests.size(), 1);
        QVERIFY(services.fallback->requests.isEmpty());

        services.primary->replies.first()->completeError(TranslationError(error),
                                                         QStringLiteral("failure"));
        QCOMPARE(services.primary->requests.size(), 1);
        QCOMPARE(services.fallback->requests.size(), 1);
        QCOMPARE(finished.size(), 0);

        services.fallback->replies.first()->complete(QStringLiteral("Привет"));
        QCOMPARE(finished.size(), 1);
        QVERIFY(reply->result().ok());
        QCOMPARE(reply->result().translatedText, QStringLiteral("Привет"));
        QCOMPARE(reply->result().serviceId, QStringLiteral("free"));
    }

    void noFallbackOnNonRetryableErrors_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("error"); // None: the primary succeeds

        QTest::newRow("success") << QStringLiteral("Hello") << int(TranslationError::None);
        QTest::newRow("empty text") << QString() << int(TranslationError::EmptyText);
        QTest::newRow("too long") << QString(TranslationManager::kDefaultMaxLength + 1, u'a')
                                  << int(TranslationError::TooLong);
        QTest::newRow("unsupported language")
            << QStringLiteral("Hello") << int(TranslationError::UnsupportedLanguage);
        QTest::newRow("cancelled") << QStringLiteral("Hello") << int(TranslationError::Cancelled);
        QTest::newRow("bad response")
            << QStringLiteral("Hello") << int(TranslationError::BadResponse);
    }

    void noFallbackOnNonRetryableErrors()
    {
        QFETCH(QString, text);
        QFETCH(int, error);

        TranslationManager manager;
        const FallbackPair services = withFallback(manager);

        auto reply = std::unique_ptr<TranslationReply>(
            manager.translate(request(text), TranslationManager::Channel::MainWindow));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);

        if (error == int(TranslationError::EmptyText) || error == int(TranslationError::TooLong)) {
            // Rejected before any request; the error arrives through the loop.
            QVERIFY(services.primary->requests.isEmpty());
            QVERIFY(finished.wait(1000));
        } else {
            QCOMPARE(services.primary->requests.size(), 1);
            if (error == int(TranslationError::None))
                services.primary->replies.first()->complete(QStringLiteral("Привет"));
            else
                services.primary->replies.first()->completeError(TranslationError(error),
                                                                 QStringLiteral("failure"));
            QCOMPARE(finished.size(), 1);
        }

        QVERIFY(services.fallback->requests.isEmpty());
        QCOMPARE(int(reply->result().error), error);
        QCOMPARE(reply->result().serviceId, QStringLiteral("primary"));
    }

    void fallbackOnUnconfiguredPrimary()
    {
        TranslationManager manager;
        auto *primary = new FakeService(QStringLiteral("primary"), false);
        auto *fallback = new FakeService(QStringLiteral("free"), true, true);
        manager.registerService(primary);
        manager.registerService(fallback);
        manager.setFallbackService(QStringLiteral("free"));
        manager.setFreeServicesAllowed(true);

        auto reply = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        // The pre-request Auth goes through the event loop: nothing is
        // synchronous, not even the fallback request.
        QVERIFY(!reply->isFinished());
        QCOMPARE(primary->requests.size(), 0);
        QCOMPARE(fallback->requests.size(), 0);

        QTRY_COMPARE(fallback->requests.size(), 1);
        QCOMPARE(finished.size(), 0);
        fallback->replies.first()->complete(QStringLiteral("Привет"));
        QCOMPARE(finished.size(), 1);
        QVERIFY(reply->result().ok());
        QCOMPARE(reply->result().translatedText, QStringLiteral("Привет"));
        QCOMPARE(reply->result().serviceId, QStringLiteral("free"));
        QCOMPARE(primary->requests.size(), 0);
    }

    void fallbackIsIgnoredWhenInvalid_data()
    {
        QTest::addColumn<QString>("fallbackId");

        QTest::newRow("empty") << QString();
        QTest::newRow("unregistered") << QStringLiteral("missing");
        QTest::newRow("primary itself") << QStringLiteral("primary");
        QTest::newRow("non-opt-in service") << QStringLiteral("regular");
    }

    void fallbackIsIgnoredWhenInvalid()
    {
        QFETCH(QString, fallbackId);

        TranslationManager manager;
        auto *primary = new FakeService(QStringLiteral("primary"));
        auto *fallback = new FakeService(QStringLiteral("free"), true, true);
        auto *regular = new FakeService(QStringLiteral("regular"));
        manager.registerService(primary);
        manager.registerService(fallback);
        manager.registerService(regular);
        manager.setFallbackService(fallbackId);
        manager.setFreeServicesAllowed(true);

        auto reply = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        primary->replies.first()->completeError(TranslationError::Network,
                                                QStringLiteral("offline"));

        QCOMPARE(finished.size(), 1);
        QVERIFY(fallback->requests.isEmpty());
        QVERIFY(regular->requests.isEmpty());
        QCOMPARE(reply->result().error, TranslationError::Network);
        QCOMPARE(reply->result().serviceId, QStringLiteral("primary"));
    }

    void fallbackRequiresTheGate()
    {
        TranslationManager manager;
        auto *primary = new FakeService(QStringLiteral("primary"));
        auto *fallback = new FakeService(QStringLiteral("free"), true, true);
        manager.registerService(primary);
        manager.registerService(fallback);
        manager.setFallbackService(QStringLiteral("free"));
        QVERIFY(!manager.freeServicesAllowed());

        auto first = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        primary->replies.first()->completeError(TranslationError::Network,
                                                QStringLiteral("offline"));
        QVERIFY(fallback->requests.isEmpty());
        QCOMPARE(first->result().error, TranslationError::Network);

        // The same settings with the gate on do fall back.
        manager.setFreeServicesAllowed(true);
        auto second = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        primary->replies.at(1)->completeError(TranslationError::Network,
                                              QStringLiteral("offline"));
        QCOMPARE(fallback->requests.size(), 1);
        fallback->replies.first()->complete(QStringLiteral("Привет"));
        QVERIFY(second->result().ok());
        QCOMPARE(second->result().serviceId, QStringLiteral("free"));

        // Turning the gate off while the primary is in flight is honoured: the
        // fallback is resolved at failure time.
        auto third = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Bye")), TranslationManager::Channel::MainWindow));
        manager.setFreeServicesAllowed(false);
        primary->replies.at(2)->completeError(TranslationError::Network,
                                              QStringLiteral("offline"));
        QCOMPARE(fallback->requests.size(), 1);
        QCOMPARE(third->result().error, TranslationError::Network);
    }

    void fallbackFailureReturnsPrimaryError()
    {
        TranslationManager manager;
        const FallbackPair services = withFallback(manager);

        auto reply = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        services.primary->replies.first()->completeError(TranslationError::Network,
                                                         QStringLiteral("offline"));
        services.fallback->replies.first()->completeError(TranslationError::Auth,
                                                          QStringLiteral("fallback says no"));

        QCOMPARE(services.fallback->requests.size(), 1);
        QCOMPARE(reply->result().error, TranslationError::Network);
        QCOMPARE(reply->result().errorMessage, QStringLiteral("offline"));
        QCOMPARE(reply->result().serviceId, QStringLiteral("primary"));
    }

    void abortDuringFallback()
    {
        TranslationManager manager;
        const FallbackPair services = withFallback(manager);

        auto reply = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        services.primary->replies.first()->completeError(TranslationError::Network,
                                                         QStringLiteral("offline"));
        QCOMPARE(services.fallback->requests.size(), 1);
        QVERIFY(!reply->isFinished());

        manager.cancel(TranslationManager::Channel::MainWindow);
        QVERIFY(reply->isFinished());
        QCOMPARE(reply->result().error, TranslationError::Cancelled);
        QCOMPARE(services.fallback->replies.first()->abortCount, 1);

        // The aborted fallback result is not cached: the next request starts
        // with the primary again.
        auto second = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        QCOMPARE(services.primary->requests.size(), 2);
        QCOMPARE(services.fallback->requests.size(), 1);
        QVERIFY(!second->isFinished());
    }

    void fallbackResultIsCached()
    {
        TranslationManager manager;
        const FallbackPair services = withFallback(manager);

        auto first = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        services.primary->replies.first()->completeError(TranslationError::Network,
                                                         QStringLiteral("offline"));
        services.fallback->replies.first()->complete(QStringLiteral("Привет"));
        QVERIFY(first->result().ok());

        auto second = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        QSignalSpy finished(second.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(second->result().translatedText, QStringLiteral("Привет"));
        QCOMPARE(second->result().serviceId, QStringLiteral("free"));
        QCOMPARE(services.primary->requests.size(), 1);
        QCOMPARE(services.fallback->requests.size(), 1);
    }

    void changingFallbackClearsCache()
    {
        TranslationManager manager;
        auto *primary = new FakeService(QStringLiteral("primary"));
        auto *fallback = new FakeService(QStringLiteral("free"), true, true);
        auto *other = new FakeService(QStringLiteral("other"), true, true);
        manager.registerService(primary);
        manager.registerService(fallback);
        manager.registerService(other);
        manager.setFallbackService(QStringLiteral("  Free \n"));
        QCOMPARE(manager.fallbackService(), QStringLiteral("free"));
        manager.setFreeServicesAllowed(true);

        auto first = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        primary->replies.first()->completeError(TranslationError::Network,
                                                QStringLiteral("offline"));
        fallback->replies.first()->complete(QStringLiteral("Привет"));
        QVERIFY(first->result().ok());

        // The same value again keeps the cache.
        manager.setFallbackService(QStringLiteral("free"));
        auto cached = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        QSignalSpy cachedFinished(cached.get(), &TranslationReply::finished);
        QVERIFY(cachedFinished.wait(1000));
        QCOMPARE(cached->result().translatedText, QStringLiteral("Привет"));
        QCOMPARE(cached->result().serviceId, QStringLiteral("free"));
        QCOMPARE(primary->requests.size(), 1);

        // A new fallback clears the cache: the primary is asked again.
        manager.setFallbackService(other->id());
        auto second = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        QCOMPARE(primary->requests.size(), 2);
        primary->replies.at(1)->completeError(TranslationError::Network,
                                              QStringLiteral("offline"));
        QCOMPARE(other->requests.size(), 1);
        other->replies.first()->complete(QStringLiteral("Hello"));
        QVERIFY(second->result().ok());
        QCOMPARE(second->result().serviceId, QStringLiteral("other"));
    }

    void disablingTheGateClearsTheFallbackCache()
    {
        TranslationManager manager;
        const FallbackPair services = withFallback(manager);

        auto first = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        services.primary->replies.first()->completeError(TranslationError::Network,
                                                         QStringLiteral("offline"));
        services.fallback->replies.first()->complete(QStringLiteral("Привет"));
        QVERIFY(first->result().ok());

        manager.setFreeServicesAllowed(false);
        auto second = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        QCOMPARE(services.primary->requests.size(), 2);
        services.primary->replies.at(1)->completeError(TranslationError::Network,
                                                       QStringLiteral("offline"));
        QCOMPARE(services.fallback->requests.size(), 1);
        QCOMPARE(second->result().error, TranslationError::Network);
    }

    void noSecondHopOrSmartRetry()
    {
        TranslationManager manager;
        const FallbackPair services = withFallback(manager);

        // A failing fallback ends the chain: the primary error is returned.
        auto first = std::unique_ptr<TranslationReply>(manager.translate(
            request(QStringLiteral("Hello")), TranslationManager::Channel::MainWindow));
        services.primary->replies.first()->completeError(TranslationError::Network,
                                                         QStringLiteral("offline"));
        services.fallback->replies.first()->completeError(TranslationError::Network,
                                                          QStringLiteral("offline"));
        QCOMPARE(first->result().error, TranslationError::Network);
        QCOMPARE(first->result().serviceId, QStringLiteral("primary"));
        QCOMPARE(services.primary->requests.size(), 1);
        QCOMPARE(services.fallback->requests.size(), 1);

        // The fallback does not get the smart secondary-language retry: one
        // failure is one fallback request, not two.
        manager.setPrimaryLanguage(QStringLiteral("ru"));
        manager.setSecondaryLanguage(QStringLiteral("en"));
        auto second = std::unique_ptr<TranslationReply>(manager.translate(
            {QStringLiteral("Привет"), QStringLiteral("auto"), QStringLiteral("ru")},
            TranslationManager::Channel::MainWindow));
        services.primary->replies.at(1)->completeError(TranslationError::Network,
                                                       QStringLiteral("offline"));
        QCOMPARE(services.fallback->requests.size(), 2);
        services.fallback->replies.at(1)->complete(QStringLiteral("Привет"),
                                                   QStringLiteral("ru"));
        QVERIFY(second->result().ok());
        QCOMPARE(second->result().translatedText, QStringLiteral("Привет"));
        QCOMPARE(services.fallback->requests.size(), 2);
    }

    void logIsClean()
    {
        TranslationManager manager;
        const FallbackPair services = withFallback(manager);

        {
            LogCapture capture;
            auto reply = std::unique_ptr<TranslationReply>(manager.translate(
                request(QStringLiteral("TOPSECRET")), TranslationManager::Channel::MainWindow));
            services.primary->replies.first()->completeError(
                TranslationError::Network, QStringLiteral("offline TOPSECRET"));
            services.fallback->replies.first()->completeError(
                TranslationError::BadResponse,
                QStringLiteral("bad body TOPSECRET http://example.invalid/?q=TOPSECRET"));
            QCOMPARE(reply->result().error, TranslationError::Network);
        }

        QVERIFY(!g_logMessages.isEmpty());
        bool sawPrimaryFailure = false;
        bool sawFallbackFailure = false;
        for (const QString &message : std::as_const(g_logMessages)) {
            QVERIFY2(!message.contains(QStringLiteral("TOPSECRET")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("offline")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("bad body")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("|auto|")), qPrintable(message));
            if (message.contains(QStringLiteral("primary"))
                && message.contains(QStringLiteral("network")))
                sawPrimaryFailure = true;
            if (message.contains(QStringLiteral("free"))
                && message.contains(QStringLiteral("bad-response")))
                sawFallbackFailure = true;
        }
        QVERIFY(sawPrimaryFailure);
        QVERIFY(sawFallbackFailure);
    }
};

QTEST_GUILESS_MAIN(TestTranslationManager)
#include "tst_translationmanager.moc"
