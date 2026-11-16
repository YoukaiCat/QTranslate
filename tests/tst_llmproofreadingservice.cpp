#include "core/languages.h"
#include "core/proofreadingservice.h"
#include "core/proofreadingtext.h"
#include "core/settings.h"
#include "net/networkmanager.h"
#include "services/llmproofreadingservice.h"
#include "services/openaicompatibleservice.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>
#include <QVector>

#include <functional>
#include <memory>
#include <utility>

Q_DECLARE_METATYPE(TranslationError)

namespace {

// A local HTTP server that records every request and can skip the response, so
// the timeout and the cancellation path are testable without a real provider.
class FakeHttpServer : public QObject
{
public:
    struct Request
    {
        QByteArray method;
        QUrl url;
        QHash<QByteArray, QByteArray> headers;
        QByteArray body;
        qint64 elapsedMs = 0;
    };
    struct Response
    {
        int status = 200;
        QByteArray body;
        bool respond = true;
    };

    FakeHttpServer()
    {
        m_clock.start();
        connect(&m_server, &QTcpServer::newConnection, this, &FakeHttpServer::onNewConnection);
        if (!m_server.listen(QHostAddress::LocalHost))
            qFatal("Cannot listen on localhost");
    }

    quint16 port() const { return m_server.serverPort(); }

    std::function<Response(const Request &)> handler;
    QList<Request> requests;

private:
    void onNewConnection()
    {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    void onReadyRead(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;

        Request request;
        request.elapsedMs = m_clock.elapsed();
        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
        request.method = requestLine.value(0);
        request.url = QUrl(QString::fromLatin1(requestLine.value(1)));
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const qsizetype colon = lines.at(i).indexOf(':');
            if (colon > 0) {
                request.headers.insert(lines.at(i).left(colon).trimmed().toLower(),
                                       lines.at(i).mid(colon + 1).trimmed());
            }
        }
        const qsizetype length = request.headers.value("content-length").toLongLong();
        if (buffer.size() < headerEnd + 4 + length)
            return;
        request.body = buffer.mid(headerEnd + 4, length);
        m_buffers.remove(socket);

        requests << request;
        const Response response = handler ? handler(request) : Response{};
        if (!response.respond)
            return;
        socket->write("HTTP/1.1 " + QByteArray::number(response.status) + " Status\r\n"
                      "Content-Type: application/json; charset=utf-8\r\n"
                      "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n"
                      "Connection: close\r\n\r\n" + response.body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QElapsedTimer m_clock;
};

QByteArray jsonObject(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray chatResponse(const QString &content,
                        const QString &finishReason = QStringLiteral("stop"))
{
    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    message.insert(QStringLiteral("content"), content);

    QJsonObject choice;
    choice.insert(QStringLiteral("index"), 0);
    choice.insert(QStringLiteral("message"), message);
    choice.insert(QStringLiteral("finish_reason"), finishReason);

    QJsonObject usage;
    usage.insert(QStringLiteral("prompt_tokens"), 10);
    usage.insert(QStringLiteral("completion_tokens"), 5);

    QJsonObject root;
    root.insert(QStringLiteral("model"), QStringLiteral("test/model"));
    root.insert(QStringLiteral("choices"), QJsonArray{choice});
    root.insert(QStringLiteral("usage"), usage);
    return jsonObject(root);
}

QString editContent(const QJsonArray &edits)
{
    QJsonObject root;
    root.insert(QStringLiteral("edits"), edits);
    return QString::fromUtf8(jsonObject(root));
}

QJsonObject edit(const QString &original, const QString &replacement,
                 const QString &message = QStringLiteral("fix"),
                 const QString &category = QStringLiteral("grammar"))
{
    QJsonObject object;
    object.insert(QStringLiteral("original"), original);
    object.insert(QStringLiteral("replacement"), replacement);
    object.insert(QStringLiteral("message"), message);
    object.insert(QStringLiteral("category"), category);
    return object;
}

QString fakeBaseUrl(quint16 port)
{
    return QStringLiteral("http://127.0.0.1:%1/api/v1").arg(port);
}

AppSettings makeSettings(quint16 port, const QString &key = QStringLiteral("test-key"),
                         const QString &model = QStringLiteral("test/model"))
{
    AppSettings settings;
    settings.llm.baseUrl = fakeBaseUrl(port);
    settings.llm.apiKey = key;
    settings.llm.model = model;
    return settings;
}

// Error messages are fixed: neither the text, the key, an issue message, a
// replacement, the URL nor the body may leak into them.
void verifyFixedMessage(const QString &message)
{
    QVERIFY(!message.isEmpty());
    QVERIFY2(!message.contains(QStringLiteral("TOPSECRET")), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("test-key")), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('<')), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("127.0.0.1")), qPrintable(message));
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
        QLoggingCategory::setFilterRules(
            QStringLiteral("qtranslate.service.llm-proofreading.debug=true"));
    }

    ~LogCapture()
    {
        QLoggingCategory::setFilterRules(QString());
        qInstallMessageHandler(previous);
    }
};

} // namespace

class TestLlmProofreadingService : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
        m_network = std::make_unique<NetworkManager>();
        m_service = std::make_unique<LlmProofreadingService>(m_network.get());
        m_service->applySettings(makeSettings(m_server->port()));
    }

    void cleanup()
    {
        m_service.reset();
        m_network.reset();
        m_server.reset();
    }

    void properties()
    {
        QCOMPARE(m_service->id(), QStringLiteral("llm"));
        QCOMPARE(m_service->displayName(), QStringLiteral("AI"));
        QVERIFY(m_service->isConfigured());
        QVERIFY(m_service->supportsAutoLanguage());

        const QStringList languages = m_service->languages();
        QVERIFY(!languages.isEmpty());
        QVERIFY(languages.contains(QStringLiteral("de")));
        QVERIFY(languages.contains(QStringLiteral("ru")));
        QVERIFY(!languages.contains(Languages::autoCode()));

        // A missing model is a configuration failure; a missing key is not.
        m_service->applySettings(makeSettings(m_server->port(), QStringLiteral("test-key"), {}));
        QVERIFY(!m_service->isConfigured());
        QCOMPARE(m_service->configurationError(),
                 QStringLiteral("the AI provider is configured on the settings AI tab"));

        m_service->applySettings(makeSettings(m_server->port(), {}));
        QVERIFY(m_service->isConfigured());
    }

    void appliesSettings()
    {
        AppSettings settings = makeSettings(m_server->port());
        m_service->applySettings(settings);
        QCOMPARE(m_service->endpoint().path(), QStringLiteral("/api/v1/chat/completions"));
        QCOMPARE(m_service->endpoint().host(), QStringLiteral("127.0.0.1"));

        settings.llm.baseUrl.clear();
        m_service->applySettings(settings);
        QCOMPARE(m_service->endpoint(), OpenAiCompatibleService::defaultEndpoint());
        QCOMPARE(m_service->endpoint().host(), QStringLiteral("openrouter.ai"));

        settings.llm.baseUrl =
            QStringLiteral("http://127.0.0.1:%1/custom/chat/completions").arg(m_server->port());
        m_service->applySettings(settings);
        QCOMPARE(m_service->endpoint().path(), QStringLiteral("/custom/chat/completions"));

        // An unusable base URL falls back to the default endpoint.
        settings.llm.baseUrl = QStringLiteral("not a url");
        m_service->applySettings(settings);
        QCOMPARE(m_service->endpoint(), OpenAiCompatibleService::defaultEndpoint());
    }

    void sendsRequest()
    {
        respondWith(QStringLiteral("{\"edits\":[]}"));

        auto reply = check(QStringLiteral("  Hallo Welt \n"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        QCOMPARE(m_server->requests.size(), 1);
        const FakeHttpServer::Request &request = m_server->requests.first();
        QCOMPARE(request.method, QByteArray("POST"));
        QCOMPARE(request.url.path(), QStringLiteral("/api/v1/chat/completions"));
        QCOMPARE(request.headers.value("authorization"), QByteArray("Bearer test-key"));
        QCOMPARE(request.headers.value("http-referer"),
                 QByteArray("https://github.com/youkaicat/QTranslate"));
        QCOMPARE(request.headers.value("x-title"), QByteArray("QTranslate"));
        QCOMPARE(request.headers.value("accept"), QByteArray("application/json"));
        QVERIFY(request.headers.value("content-type").startsWith("application/json"));

        const QJsonObject body = QJsonDocument::fromJson(request.body).object();
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("test/model"));
        QCOMPARE(body.value(QStringLiteral("temperature")).toDouble(), 0.0);
        QVERIFY(!body.contains(QStringLiteral("max_tokens")));

        const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.at(0).toObject().value(QStringLiteral("role")).toString(),
                 QStringLiteral("system"));
        QCOMPARE(messages.at(1).toObject().value(QStringLiteral("role")).toString(),
                 QStringLiteral("user"));
        const QString system =
            messages.at(0).toObject().value(QStringLiteral("content")).toString();
        QVERIFY(system.contains(QStringLiteral("edits")));
        QVERIFY(system.contains(QStringLiteral("German")));
        // The user message is the trimmed text, not the original.
        QCOMPARE(messages.at(1).toObject().value(QStringLiteral("content")).toString(),
                 QStringLiteral("Hallo Welt"));

        // Without a key no Authorization header is sent.
        m_service->applySettings(makeSettings(m_server->port(), {}));
        respondWith(QStringLiteral("{\"edits\":[]}"));
        auto anonymous = check(QStringLiteral("Hallo"));
        QSignalSpy anonymousFinished(anonymous.get(), &ProofreadingReply::finished);
        QVERIFY(anonymousFinished.wait(5000));
        QVERIFY2(anonymous->result().ok(), qPrintable(anonymous->result().errorMessage));
        QCOMPARE(m_server->requests.size(), 2);
        QVERIFY(!m_server->requests.at(1).headers.contains("authorization"));
    }

    void structuredEdits()
    {
        const QString text = QStringLiteral("  Zeile eins\nZeile zwei");
        respondWith(editContent({edit(QStringLiteral("eins"), QStringLiteral("Eins"),
                                      QStringLiteral("capitalise"),
                                      QStringLiteral("spelling")),
                                 edit(QStringLiteral("zwei"), QStringLiteral("Zwei"),
                                      QStringLiteral("capitalise"),
                                      QStringLiteral("spelling"))}));

        auto reply = check(text);
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        const ProofreadingResult &result = reply->result();
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.serviceId, QStringLiteral("llm"));
        QVERIFY(result.correctedText.isEmpty());

        QCOMPARE(result.issues.size(), 2);
        // The parser indexed the trimmed message; the offsets point into the
        // untrimmed text, so the two leading spaces are included.
        QCOMPARE(result.issues.at(0).offset, 8);
        QCOMPARE(result.issues.at(0).length, 4);
        QCOMPARE(result.issues.at(0).ruleId, QStringLiteral("llm"));
        QCOMPARE(result.issues.at(0).replacements, QStringList({QStringLiteral("Eins")}));
        QCOMPARE(result.issues.at(1).offset, 19);
        QCOMPARE(result.issues.at(1).length, 4);
        QCOMPARE(result.issues.at(1).ruleId, QStringLiteral("llm"));
        QCOMPARE(result.issues.at(1).replacements, QStringList({QStringLiteral("Zwei")}));

        // The invariant the checking window and the controller rely on.
        QCOMPARE(ProofreadingText::corrected(text, result.issues),
                 QStringLiteral("  Zeile Eins\nZeile Zwei"));
    }

    void cleanAnswerIsNotAnError()
    {
        // An `edits` document with no usable edit and no dropped edit is the
        // normal "nothing to fix" answer, not a bad response.
        respondWith(QStringLiteral("{\"edits\":[]}"));
        auto emptyEdits = check(QStringLiteral("Zeile eins"));
        QSignalSpy emptyFinished(emptyEdits.get(), &ProofreadingReply::finished);
        QVERIFY(emptyFinished.wait(5000));
        QVERIFY2(emptyEdits->result().ok(), qPrintable(emptyEdits->result().errorMessage));
        QVERIFY(emptyEdits->result().issues.isEmpty());

        // A fenced document is the same answer in a different shape.
        respondWith(QStringLiteral("```json\n{\"edits\":[]}\n```"));
        auto fenced = check(QStringLiteral("Zeile eins"));
        QSignalSpy fencedFinished(fenced.get(), &ProofreadingReply::finished);
        QVERIFY(fencedFinished.wait(5000));
        QVERIFY2(fenced->result().ok(), qPrintable(fenced->result().errorMessage));
        QVERIFY(fenced->result().issues.isEmpty());

        // An answer identical to the source is a clean check, not a diff.
        respondWith(QStringLiteral("Zeile eins"));
        auto identical = check(QStringLiteral("Zeile eins"));
        QSignalSpy identicalFinished(identical.get(), &ProofreadingReply::finished);
        QVERIFY(identicalFinished.wait(5000));
        QVERIFY2(identical->result().ok(), qPrintable(identical->result().errorMessage));
        QVERIFY(identical->result().issues.isEmpty());
    }

    void plainTextFallback()
    {
        respondWith(QStringLiteral("Zeile Eins"));

        auto reply = check(QStringLiteral("  Zeile eins\n"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        const ProofreadingResult &result = reply->result();
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.issues.size(), 1);
        QCOMPARE(result.issues.first().ruleId, QStringLiteral("llm-diff"));
        QCOMPARE(result.issues.first().offset, 8);
        QCOMPARE(result.issues.first().length, 1);
        // The corrected text is the whole text, the answer re-wrapped in the
        // source's surrounding whitespace.
        QCOMPARE(result.correctedText, QStringLiteral("  Zeile Eins\n"));
    }

    void partialAnswerIsASuccess()
    {
        // One edit cannot be placed in the text, another one can: the usable
        // issue survives and the check succeeds.
        respondWith(editContent({edit(QStringLiteral("nicht im Text"), QStringLiteral("x")),
                                 edit(QStringLiteral("zwei"), QStringLiteral("Zwei"))}));

        auto reply = check(QStringLiteral("Zeile eins\nZeile zwei"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        const ProofreadingResult &result = reply->result();
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.issues.size(), 1);
        QCOMPARE(result.issues.first().offset, 17);
        QCOMPARE(result.issues.first().length, 4);
        QCOMPARE(result.issues.first().replacements, QStringList({QStringLiteral("Zwei")}));
    }

    void droppedEditsIsBadResponse()
    {
        respondWith(editContent({edit(QStringLiteral("nicht im Text"), QStringLiteral("x"))}));

        auto reply = check(QStringLiteral("Zeile eins"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::BadResponse);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("the model returned changes that do not match the text"));
        QVERIFY(reply->result().issues.isEmpty());
    }

    void filteredEditsIsACleanCheck()
    {
        // The only edit belongs to a category the check did not ask for: the
        // filter did its job, so the check succeeds instead of reporting a bad
        // response.
        respondWith(editContent({edit(QStringLiteral("teh"), QStringLiteral("the"),
                                      QStringLiteral("fix"), QStringLiteral("grammar"))}));

        auto reply = check(QStringLiteral("teh cat"), QStringLiteral("de"),
                           {QStringLiteral("spelling")});
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QVERIFY(reply->result().issues.isEmpty());

        // An edit whose replacement equals the original changes nothing: also a
        // clean check.
        respondWith(editContent({edit(QStringLiteral("Zeile"), QStringLiteral("Zeile"),
                                      QStringLiteral("fix"), QStringLiteral("spelling"))}));

        auto unchanged = check(QStringLiteral("Zeile eins"), QStringLiteral("de"),
                               {QStringLiteral("spelling")});
        QSignalSpy unchangedFinished(unchanged.get(), &ProofreadingReply::finished);
        QVERIFY(unchangedFinished.wait(5000));
        QVERIFY2(unchanged->result().ok(), qPrintable(unchanged->result().errorMessage));
        QVERIFY(unchanged->result().issues.isEmpty());
    }

    void truncatedAnswerIsTooLong()
    {
        // The JSON is cut off and would read as plain text without the
        // finish_reason gate.
        respondWith(QStringLiteral("{\"edits\":[{\"orig"), QStringLiteral("length"));
        auto truncated = check(QStringLiteral("Zeile eins"));
        QSignalSpy truncatedFinished(truncated.get(), &ProofreadingReply::finished);
        QVERIFY(truncatedFinished.wait(5000));
        QCOMPARE(truncated->result().error, TranslationError::TooLong);

        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{413, QByteArray("{}"), true};
        };
        auto tooLarge = check(QStringLiteral("Zeile eins"));
        QSignalSpy tooLargeFinished(tooLarge.get(), &ProofreadingReply::finished);
        QVERIFY(tooLargeFinished.wait(5000));
        QCOMPARE(tooLarge->result().error, TranslationError::TooLong);
    }

    void malformedAnswer()
    {
        const QStringList answers = {
            QStringLiteral("{\"foo\":1}"),
            QString(),
            QStringLiteral("{\"edits\":\"nope\"}"),
        };
        for (const QString &answer : answers) {
            respondWith(answer);
            auto reply = check(QStringLiteral("Zeile eins"));
            QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
            QVERIFY(finished.wait(5000));
            QCOMPARE(reply->result().error, TranslationError::BadResponse);
            verifyFixedMessage(reply->result().errorMessage);
        }
    }

    void httpErrors_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<int>("expected");

        QTest::newRow("400") << 400 << int(TranslationError::BadResponse);
        QTest::newRow("401") << 401 << int(TranslationError::Auth);
        QTest::newRow("402") << 402 << int(TranslationError::QuotaExceeded);
        QTest::newRow("403") << 403 << int(TranslationError::Auth);
        QTest::newRow("404") << 404 << int(TranslationError::BadResponse);
        QTest::newRow("413") << 413 << int(TranslationError::TooLong);
        QTest::newRow("429") << 429 << int(TranslationError::RateLimited);
        QTest::newRow("500") << 500 << int(TranslationError::Network);
        QTest::newRow("502") << 502 << int(TranslationError::Network);
    }

    void httpErrors()
    {
        QFETCH(int, status);
        QFETCH(int, expected);

        m_server->handler = [status](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                status, QByteArray("Error: TOPSECRET echoed back in the body"), true};
        };

        auto reply = check(QStringLiteral("TOPSECRET text"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(int(reply->result().error), expected);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void errorObjectInResponse()
    {
        struct Case
        {
            QByteArray body;
            TranslationError expected;
        };
        const QVector<Case> cases = {
            {QByteArray("{\"error\":{\"code\":429,\"message\":\"TOPSECRET rate limited\"}}"),
             TranslationError::RateLimited},
            {QByteArray("{\"error\":{\"message\":\"TOPSECRET failure\"}}"),
             TranslationError::BadResponse},
            {QByteArray("{\"object\":\"chat.completion\"}"), TranslationError::BadResponse},
        };

        for (const Case &testCase : cases) {
            const QByteArray body = testCase.body;
            m_server->handler = [body](const FakeHttpServer::Request &) {
                return FakeHttpServer::Response{200, body, true};
            };
            auto reply = check(QStringLiteral("TOPSECRET text"));
            QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
            QVERIFY(finished.wait(5000));
            QCOMPARE(reply->result().error, testCase.expected);
            verifyFixedMessage(reply->result().errorMessage);
        }
    }

    void unsupportedLanguage()
    {
        auto reply = check(QStringLiteral("Zeile eins"), QStringLiteral("xx"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::UnsupportedLanguage);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("language \"xx\" is not supported"));
        QVERIFY(m_server->requests.isEmpty());
    }

    void emptyText()
    {
        auto reply = check(QStringLiteral("   \n\t "));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::EmptyText);
        QVERIFY(m_server->requests.isEmpty());
    }

    void timeout()
    {
        m_network->setTimeoutMs(300);
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };

        auto reply = check(QStringLiteral("Zeile eins"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Timeout);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void abort()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };

        auto reply = check(QStringLiteral("Zeile eins"));
        QTRY_COMPARE(m_server->requests.size(), 1);
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        reply->abort();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(reply->result().error, TranslationError::Cancelled);

        // A late response is ignored.
        QTest::qWait(100);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(m_server->requests.size(), 1);
    }

    void logHygiene()
    {
        auto calls = std::make_shared<int>(0);
        const QString successBody =
            editContent({edit(QStringLiteral("TOPSECRET"), QStringLiteral("TOPSECRETREPLACEMENT"),
                              QStringLiteral("TOPSECRET MESSAGE"))});
        m_server->handler = [calls, successBody](const FakeHttpServer::Request &) {
            if (++*calls == 1)
                return FakeHttpServer::Response{200, chatResponse(successBody), true};
            return FakeHttpServer::Response{
                400, QByteArray("Error: TOPSECRET echoed back in the body"), true};
        };

        {
            LogCapture capture;

            auto success = check(QStringLiteral("TOPSECRET text"));
            QSignalSpy successFinished(success.get(), &ProofreadingReply::finished);
            QVERIFY(successFinished.wait(5000));
            QVERIFY2(success->result().ok(), qPrintable(success->result().errorMessage));
            QCOMPARE(success->result().issues.size(), 1);

            auto failure = check(QStringLiteral("TOPSECRET text"));
            QSignalSpy failureFinished(failure.get(), &ProofreadingReply::finished);
            QVERIFY(failureFinished.wait(5000));
            QCOMPARE(failure->result().error, TranslationError::BadResponse);
        }

        QVERIFY(!g_logMessages.isEmpty());
        bool reportedIssueCount = false;
        for (const QString &message : std::as_const(g_logMessages)) {
            QVERIFY2(!message.contains(QStringLiteral("TOPSECRET")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("test-key")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("127.0.0.1")), qPrintable(message));
            if (message.contains(QStringLiteral("Checked"))
                && message.contains(QStringLiteral("issue"))) {
                reportedIssueCount = true;
            }
        }
        QVERIFY(reportedIssueCount);
    }

private:
    std::unique_ptr<ProofreadingReply> check(const QString &text,
                                             const QString &language = QStringLiteral("de"),
                                             const QStringList &categories = {})
    {
        return std::unique_ptr<ProofreadingReply>(
            m_service->check({text, language, categories}));
    }

    void respondWith(const QString &content,
                     const QString &finishReason = QStringLiteral("stop"))
    {
        m_server->handler = [content, finishReason](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, chatResponse(content, finishReason), true};
        };
    }

    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<LlmProofreadingService> m_service;
};

QTEST_GUILESS_MAIN(TestLlmProofreadingService)
#include "tst_llmproofreadingservice.moc"
