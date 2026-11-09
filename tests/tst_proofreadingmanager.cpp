#include "core/languages.h"
#include "core/proofreadingmanager.h"
#include "core/proofreadingservice.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

#include <memory>
#include <utility>

namespace {

// A reply the test finishes by hand. It carries the emitting service's id, so
// a result can be told apart when several services are registered.
class FakeProofreadingReply : public ProofreadingReply
{
public:
    explicit FakeProofreadingReply(QString serviceId = QStringLiteral("fake"))
        : m_serviceId(std::move(serviceId))
    {
    }

    void complete(ProofreadingResult result)
    {
        if (result.serviceId.isEmpty())
            result.serviceId = m_serviceId;
        finish(result);
    }

    void completeError(TranslationError error, const QString &message)
    {
        ProofreadingResult result;
        result.serviceId = m_serviceId;
        result.error = error;
        result.errorMessage = message;
        finish(result);
    }

    int abortCount = 0;

protected:
    void abortImpl() override { ++abortCount; }

private:
    QString m_serviceId;
};

class FakeProofreadingService : public ProofreadingService
{
public:
    enum class ReplyMode {
        Manual,   // the test finishes the reply by hand
        Queued,   // the reply finishes through the event loop
        Finished, // a ready reply through ProofreadingReply::createFinished()
    };

    explicit FakeProofreadingService(QString id = QStringLiteral("fake"))
        : m_id(std::move(id))
    {
    }

    QString id() const override { return m_id; }
    QString displayName() const override { return m_id; }
    bool isConfigured() const override { return configured; }
    QString configurationError() const override { return configurationErrorText; }
    QStringList languages() const override { return languageList; }
    bool supportsAutoLanguage() const override { return autoLanguage; }
    bool isLocal() const override { return local; }
    void applySettings(const AppSettings &) override {}

    ProofreadingReply *check(const ProofreadingRequest &request) override
    {
        ++checkCalls;
        requests << request;
        switch (replyMode) {
        case ReplyMode::Finished:
            return ProofreadingReply::createFinished(withServiceId(nextResult));
        case ReplyMode::Queued: {
            auto *reply = new FakeProofreadingReply(m_id);
            replies << reply;
            QMetaObject::invokeMethod(
                reply, [reply, result = withServiceId(nextResult)] { reply->complete(result); },
                Qt::QueuedConnection);
            return reply;
        }
        case ReplyMode::Manual:
            break;
        }
        auto *reply = new FakeProofreadingReply(m_id);
        replies << reply;
        return reply;
    }

    int checkCalls = 0;
    QList<ProofreadingRequest> requests;
    QList<QPointer<FakeProofreadingReply>> replies;
    bool configured = true;
    QString configurationErrorText;
    QStringList languageList = {QStringLiteral("en"), QStringLiteral("ru")};
    bool autoLanguage = true;
    bool local = false;
    ReplyMode replyMode = ReplyMode::Manual;
    ProofreadingResult nextResult;

private:
    ProofreadingResult withServiceId(ProofreadingResult result) const
    {
        if (result.serviceId.isEmpty())
            result.serviceId = m_id;
        return result;
    }

    QString m_id;
};

ProofreadingRequest request(const QString &text,
                            const QString &language = QStringLiteral("auto"))
{
    return {text, language, {}};
}

// Captures the application log lines for the hygiene test.
QStringList g_logMessages;

void collectMessage(QtMsgType, const QMessageLogContext &, const QString &message)
{
    g_logMessages << message;
}

class LogCapture
{
public:
    LogCapture()
        : m_previous(qInstallMessageHandler(collectMessage))
    {
        g_logMessages.clear();
        // The success outcome is qCDebug; enable it for this test.
        QLoggingCategory::setFilterRules(QStringLiteral("qtranslate.proofreading.debug=true"));
    }

    ~LogCapture()
    {
        QLoggingCategory::setFilterRules(QString());
        qInstallMessageHandler(m_previous);
    }

private:
    QtMessageHandler m_previous;
};

} // namespace

class TestProofreadingManager : public QObject
{
    Q_OBJECT

private slots:
    void registry()
    {
        ProofreadingManager manager;
        QVERIFY(!manager.activeService());

        auto *first = new FakeProofreadingService(QStringLiteral("first"));
        auto *second = new FakeProofreadingService(QStringLiteral("second"));
        manager.registerService(first);
        QSignalSpy changed(&manager, &ProofreadingManager::activeServiceChanged);
        manager.registerService(second);

        QCOMPARE(first->parent(), &manager);
        QCOMPARE(manager.services(), (QList<ProofreadingService *>{first, second}));
        QCOMPARE(manager.activeService(), first);
        QCOMPARE(manager.service(QStringLiteral("second")), second);
        QVERIFY(!manager.service(QStringLiteral("none")));

        manager.setActiveService(QStringLiteral("second"));
        QCOMPARE(manager.activeService(), second);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.at(0).at(0).toString(), QStringLiteral("second"));

        // The same id and an unknown id change nothing and emit nothing.
        manager.setActiveService(QStringLiteral("second"));
        manager.setActiveService(QStringLiteral("none"));
        QCOMPARE(manager.activeService(), second);
        QCOMPARE(changed.size(), 1);
    }

    void emptyTextIsRejectedBeforeTheProvider()
    {
        ProofreadingManager manager;
        auto *service = new FakeProofreadingService;
        manager.registerService(service);
        manager.setOnlineServicesAllowed(true);

        for (const QString &text : {QString(), QStringLiteral(" \n\t ")}) {
            auto reply = std::unique_ptr<ProofreadingReply>(
                manager.check(request(text), ProofreadingManager::Channel::MainWindow));
            QCOMPARE(service->checkCalls, 0);
            QVERIFY(!reply->isFinished());
            QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
            QCOMPARE(finished.size(), 0);
            QVERIFY(finished.wait(1000));
            QCOMPARE(finished.size(), 1);
            QCOMPARE(reply->result().error, TranslationError::EmptyText);
            QVERIFY(reply->result().errorMessage.isEmpty());
            QCOMPARE(reply->result().serviceId, QStringLiteral("fake"));
        }
        QCOMPARE(service->checkCalls, 0);
    }

    void missingProviderIsRejected()
    {
        ProofreadingManager manager;
        QVERIFY(!manager.activeService());

        auto reply = std::unique_ptr<ProofreadingReply>(
            manager.check(request(QStringLiteral("Hello")),
                          ProofreadingManager::Channel::MainWindow));
        QVERIFY(!reply->isFinished());
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QCOMPARE(finished.size(), 0);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::ServiceUnavailable);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("No text checking provider is available."));
        QVERIFY(reply->result().serviceId.isEmpty());
    }

    void unconfiguredProviderIsRejected()
    {
        ProofreadingManager manager;
        auto *service = new FakeProofreadingService;
        service->configured = false;
        service->configurationErrorText = QStringLiteral("Add a LanguageTool key.");
        manager.registerService(service);
        manager.setOnlineServicesAllowed(true);

        auto reply = std::unique_ptr<ProofreadingReply>(
            manager.check(request(QStringLiteral("Hello")),
                          ProofreadingManager::Channel::MainWindow));
        QVERIFY(!reply->isFinished());
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QCOMPARE(finished.size(), 0);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::Auth);
        QCOMPARE(reply->result().errorMessage, QStringLiteral("Add a LanguageTool key."));
        QCOMPARE(reply->result().serviceId, QStringLiteral("fake"));
        QCOMPARE(service->checkCalls, 0);

        // An empty hint falls back to the generic message.
        service->configurationErrorText.clear();
        auto second = std::unique_ptr<ProofreadingReply>(
            manager.check(request(QStringLiteral("Hello")),
                          ProofreadingManager::Channel::MainWindow));
        QVERIFY(!second->isFinished());
        QSignalSpy secondFinished(second.get(), &ProofreadingReply::finished);
        QCOMPARE(secondFinished.size(), 0);
        QVERIFY(secondFinished.wait(1000));
        QCOMPARE(second->result().error, TranslationError::Auth);
        QCOMPARE(second->result().errorMessage,
                 QStringLiteral("The text checking provider needs a key."));
        QCOMPARE(service->checkCalls, 0);
    }

    void onlineGateRefusesRemoteProviders()
    {
        ProofreadingManager manager;
        QVERIFY(!manager.onlineServicesAllowed());
        auto *remote = new FakeProofreadingService(QStringLiteral("remote"));
        manager.registerService(remote);

        auto reply = std::unique_ptr<ProofreadingReply>(
            manager.check(request(QStringLiteral("Hello")),
                          ProofreadingManager::Channel::MainWindow));
        QVERIFY(!reply->isFinished());
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QCOMPARE(finished.size(), 0);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::ServiceUnavailable);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("Enable online text checking in Settings → Text checking."));
        QCOMPARE(reply->result().serviceId, QStringLiteral("remote"));
        QCOMPARE(remote->checkCalls, 0);

        // The refusal is not cached: with the switch on the request goes through.
        manager.setOnlineServicesAllowed(true);
        auto allowed = std::unique_ptr<ProofreadingReply>(
            manager.check(request(QStringLiteral("Hello")),
                          ProofreadingManager::Channel::MainWindow));
        QCOMPARE(remote->checkCalls, 1);
        QCOMPARE(remote->requests.size(), 1);
        remote->replies.first()->complete({});
        QVERIFY(allowed->result().ok());
        QCOMPARE(allowed->result().serviceId, QStringLiteral("remote"));
    }

    void localProviderWorksWithTheGateOff()
    {
        ProofreadingManager manager;
        QVERIFY(!manager.onlineServicesAllowed());
        auto *local = new FakeProofreadingService(QStringLiteral("local"));
        local->local = true;
        manager.registerService(local);

        auto reply = std::unique_ptr<ProofreadingReply>(
            manager.check(request(QStringLiteral("Hello")),
                          ProofreadingManager::Channel::MainWindow));
        QCOMPARE(local->checkCalls, 1);
        QCOMPARE(local->requests.size(), 1);
        local->replies.first()->complete({});
        QVERIFY(reply->result().ok());
        QCOMPARE(reply->result().serviceId, QStringLiteral("local"));
    }

    void unsupportedLanguageIsRejected()
    {
        ProofreadingManager manager;
        auto *service = new FakeProofreadingService;
        service->autoLanguage = false;
        manager.registerService(service);
        manager.setOnlineServicesAllowed(true);

        auto autoReply = std::unique_ptr<ProofreadingReply>(
            manager.check(request(QStringLiteral("Hello"), Languages::autoCode()),
                          ProofreadingManager::Channel::MainWindow));
        QVERIFY(!autoReply->isFinished());
        QSignalSpy autoFinished(autoReply.get(), &ProofreadingReply::finished);
        QCOMPARE(autoFinished.size(), 0);
        QVERIFY(autoFinished.wait(1000));
        QCOMPARE(autoReply->result().error, TranslationError::UnsupportedLanguage);
        QVERIFY(autoReply->result().errorMessage.contains(QStringLiteral("\"auto\"")));

        // A code that is not in languages() is rejected as well.
        auto unknownReply = std::unique_ptr<ProofreadingReply>(
            manager.check(request(QStringLiteral("Hello"), QStringLiteral("xx")),
                          ProofreadingManager::Channel::MainWindow));
        QVERIFY(!unknownReply->isFinished());
        QSignalSpy unknownFinished(unknownReply.get(), &ProofreadingReply::finished);
        QCOMPARE(unknownFinished.size(), 0);
        QVERIFY(unknownFinished.wait(1000));
        QCOMPARE(unknownReply->result().error, TranslationError::UnsupportedLanguage);
        QVERIFY(unknownReply->result().errorMessage.contains(QStringLiteral("\"xx\"")));

        QCOMPARE(service->checkCalls, 0);
    }

    void resultIsPassedThrough()
    {
        ProofreadingManager manager;
        auto *service = new FakeProofreadingService;
        service->replyMode = FakeProofreadingService::ReplyMode::Queued;
        ProofreadingIssue issue;
        issue.offset = 2;
        issue.length = 3;
        issue.message = QStringLiteral("Possible typo");
        issue.replacements = {QStringLiteral("the"), QStringLiteral("them")};
        issue.ruleId = QStringLiteral("MORFOLOGIK_RULE_EN_US");
        issue.category = QStringLiteral("spelling");
        issue.severity = 1;
        service->nextResult.issues = {issue};
        service->nextResult.correctedText = QStringLiteral("corrected text");
        manager.registerService(service);
        manager.setOnlineServicesAllowed(true);

        const ProofreadingRequest asked{QStringLiteral("Hello"), QStringLiteral("ru"),
                                        {QStringLiteral("grammar"),
                                         QStringLiteral("style")}};
        auto reply = std::unique_ptr<ProofreadingReply>(
            manager.check(asked, ProofreadingManager::Channel::MainWindow));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        // The service finishes through the event loop, never inside check().
        QCOMPARE(finished.size(), 0);
        QVERIFY(finished.wait(1000));
        QCOMPARE(finished.size(), 1);

        QVERIFY(reply->result().ok());
        QCOMPARE(reply->result().serviceId, QStringLiteral("fake"));
        QCOMPARE(reply->result().correctedText, QStringLiteral("corrected text"));
        QCOMPARE(reply->result().issues.size(), 1);
        const ProofreadingIssue &actual = reply->result().issues.first();
        QCOMPARE(actual.offset, 2);
        QCOMPARE(actual.length, 3);
        QCOMPARE(actual.message, QStringLiteral("Possible typo"));
        QCOMPARE(actual.replacements,
                 QStringList({QStringLiteral("the"), QStringLiteral("them")}));
        QCOMPARE(actual.ruleId, QStringLiteral("MORFOLOGIK_RULE_EN_US"));
        QCOMPARE(actual.category, QStringLiteral("spelling"));
        QCOMPARE(actual.severity, 1);

        QCOMPARE(service->checkCalls, 1);
        QCOMPARE(service->requests.size(), 1);
        QCOMPARE(service->requests.first().text, QStringLiteral("Hello"));
        QCOMPARE(service->requests.first().language, QStringLiteral("ru"));
        QCOMPARE(service->requests.first().categories,
                 QStringList({QStringLiteral("grammar"), QStringLiteral("style")}));
    }

    void newCheckCancelsStaleOneInSameChannel()
    {
        ProofreadingManager manager;
        auto *service = new FakeProofreadingService;
        manager.registerService(service);
        manager.setOnlineServicesAllowed(true);

        auto first = std::unique_ptr<ProofreadingReply>(
            manager.check(request(QStringLiteral("one")),
                          ProofreadingManager::Channel::MainWindow));
        QSignalSpy firstFinished(first.get(), &ProofreadingReply::finished);

        auto inPlace = std::unique_ptr<ProofreadingReply>(
            manager.check(request(QStringLiteral("in place")),
                          ProofreadingManager::Channel::InPlace));

        auto second = std::unique_ptr<ProofreadingReply>(
            manager.check(request(QStringLiteral("two")),
                          ProofreadingManager::Channel::MainWindow));
        QSignalSpy secondFinished(second.get(), &ProofreadingReply::finished);

        QCOMPARE(firstFinished.size(), 1);
        QCOMPARE(first->result().error, TranslationError::Cancelled);
        QCOMPARE(service->replies.at(0)->abortCount, 1);
        // The other channel is left alone.
        QVERIFY(!inPlace->isFinished());
        QCOMPARE(service->replies.at(1)->abortCount, 0);

        // A late answer to the cancelled check changes nothing.
        service->replies.at(0)->complete({});
        QCOMPARE(firstFinished.size(), 1);
        QCOMPARE(first->result().error, TranslationError::Cancelled);

        service->replies.at(2)->complete({});
        QCOMPARE(secondFinished.size(), 1);
        QVERIFY(second->result().ok());
    }

    void cancelEmptyChannelIsNoOp()
    {
        ProofreadingManager manager;
        manager.cancel(ProofreadingManager::Channel::MainWindow);
        manager.cancel(ProofreadingManager::Channel::InPlace);
        manager.cancelAll();

        auto *service = new FakeProofreadingService;
        manager.registerService(service);
        manager.setOnlineServicesAllowed(true);
        auto reply = std::unique_ptr<ProofreadingReply>(
            manager.check(request(QStringLiteral("Hello")),
                          ProofreadingManager::Channel::MainWindow));
        QCOMPARE(service->checkCalls, 1);
        service->replies.first()->complete({});
        QVERIFY(reply->result().ok());
    }

    void cancelAllFinishesEveryOpenReply()
    {
        ProofreadingManager manager;
        auto *service = new FakeProofreadingService;
        manager.registerService(service);
        manager.setOnlineServicesAllowed(true);

        auto main = std::unique_ptr<ProofreadingReply>(
            manager.check(request(QStringLiteral("one")),
                          ProofreadingManager::Channel::MainWindow));
        auto inPlace = std::unique_ptr<ProofreadingReply>(
            manager.check(request(QStringLiteral("two")),
                          ProofreadingManager::Channel::InPlace));
        manager.cancelAll();

        QCOMPARE(main->result().error, TranslationError::Cancelled);
        QCOMPARE(inPlace->result().error, TranslationError::Cancelled);
        QCOMPARE(service->replies.at(0)->abortCount, 1);
        QCOMPARE(service->replies.at(1)->abortCount, 1);
    }

    void replyDeletedByCallerIsSafe()
    {
        ProofreadingManager manager;
        auto *service = new FakeProofreadingService;
        manager.registerService(service);
        manager.setOnlineServicesAllowed(true);

        delete manager.check(request(QStringLiteral("one")),
                             ProofreadingManager::Channel::MainWindow);
        QVERIFY(service->replies.at(0).isNull());

        auto second = std::unique_ptr<ProofreadingReply>(
            manager.check(request(QStringLiteral("two")),
                          ProofreadingManager::Channel::MainWindow));
        QVERIFY(!second->isFinished());
        manager.cancelAll();
        QCOMPARE(second->result().error, TranslationError::Cancelled);
    }

    void logIsClean()
    {
        ProofreadingManager manager;
        auto *service = new FakeProofreadingService;
        manager.registerService(service);
        manager.setOnlineServicesAllowed(true);

        {
            LogCapture capture;

            // A successful check carrying the private text, an issue message,
            // a replacement and a key-like value.
            ProofreadingIssue issue;
            issue.message = QStringLiteral("issue TOPSECRET");
            issue.replacements = {QStringLiteral("replacement TOPSECRET")};
            service->nextResult.issues = {issue};
            service->nextResult.errorMessage = QStringLiteral("key TOPSECRET");
            service->replyMode = FakeProofreadingService::ReplyMode::Queued;
            auto reply = std::unique_ptr<ProofreadingReply>(
                manager.check(request(QStringLiteral("TOPSECRET text")),
                              ProofreadingManager::Channel::MainWindow));
            QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
            QVERIFY(finished.wait(1000));
            QVERIFY(reply->result().ok());

            // A failing check with the same secrets in the result.
            service->nextResult = ProofreadingResult{};
            service->nextResult.error = TranslationError::Network;
            service->nextResult.errorMessage = QStringLiteral("offline TOPSECRET key=TOPSECRET");
            auto failed = std::unique_ptr<ProofreadingReply>(
                manager.check(request(QStringLiteral("TOPSECRET text")),
                              ProofreadingManager::Channel::MainWindow));
            QSignalSpy failedFinished(failed.get(), &ProofreadingReply::finished);
            QVERIFY(failedFinished.wait(1000));
            QCOMPARE(failed->result().error, TranslationError::Network);
        }

        QVERIFY(!g_logMessages.isEmpty());
        bool sawSuccess = false;
        bool sawFailure = false;
        for (const QString &message : std::as_const(g_logMessages)) {
            QVERIFY2(!message.contains(QStringLiteral("TOPSECRET")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("issue")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("replacement")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("offline")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("key")), qPrintable(message));
            if (message.contains(QStringLiteral("error=none")))
                sawSuccess = true;
            if (message.contains(QStringLiteral("error=network")))
                sawFailure = true;
        }
        QVERIFY(sawSuccess);
        QVERIFY(sawFailure);
    }
};

QTEST_GUILESS_MAIN(TestProofreadingManager)
#include "tst_proofreadingmanager.moc"
