#include "core/languages.h"
#include "core/settings.h"
#include "net/networkmanager.h"
#include "services/openaicompatibleservice.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <functional>
#include <memory>

Q_DECLARE_METATYPE(TranslationError)

namespace {

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

QByteArray chatResponse(const QString &content, const QString &finishReason = QStringLiteral("stop"))
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

QString translationContent(const QString &text, const QString &detected = QStringLiteral("en"))
{
    QJsonObject object;
    object.insert(QStringLiteral("detected_language"), detected);
    object.insert(QStringLiteral("translation"), text);
    return QString::fromUtf8(jsonObject(object));
}

QByteArray modelList(const QStringList &models)
{
    QJsonArray data;
    for (const QString &model : models) {
        QJsonObject item;
        item.insert(QStringLiteral("id"), model);
        data.append(item);
    }
    QJsonObject root;
    root.insert(QStringLiteral("data"), data);
    return jsonObject(root);
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

} // namespace

class TestOpenAiCompatibleService : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
        m_network = std::make_unique<NetworkManager>();
        m_service = std::make_unique<OpenAiCompatibleService>(m_network.get());
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
        QCOMPARE(m_service->displayName(), QStringLiteral("Local LLM"));
        QVERIFY(m_service->isConfigured());
        QVERIFY(m_service->targetLanguages().contains(QStringLiteral("de")));
        QVERIFY(m_service->targetLanguages().contains(QStringLiteral("hy")));
        QVERIFY(!m_service->sourceLanguages().contains(Languages::autoCode()));

        AppSettings settings = makeSettings(m_server->port());
        settings.llm.baseUrl = QStringLiteral("https://openrouter.ai/api/v1");
        m_service->applySettings(settings);
        QCOMPARE(m_service->displayName(), QStringLiteral("OpenRouter"));
    }

    void missingConfigurationIsReported()
    {
        AppSettings settings = makeSettings(m_server->port(), QStringLiteral("test-key"), {});
        m_service->applySettings(settings);
        QVERIFY(!m_service->isConfigured());
        QVERIFY(m_service->configurationError().contains(QStringLiteral("model")));
    }

    void sendsChatRequest()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200, chatResponse(translationContent(QStringLiteral("Hallo, Welt"))), true};
        };

        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello, world"), QStringLiteral("auto"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hallo, Welt"));
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("en"));
        QCOMPARE(reply->result().serviceId, QStringLiteral("llm"));

        QCOMPARE(m_server->requests.size(), 1);
        const FakeHttpServer::Request &request = m_server->requests.first();
        QCOMPARE(request.method, QByteArray("POST"));
        QCOMPARE(request.url.path(), QStringLiteral("/api/v1/chat/completions"));
        QCOMPARE(request.headers.value("authorization"), QByteArray("Bearer test-key"));
        QCOMPARE(request.headers.value("http-referer"),
                 QByteArray("https://github.com/youkaicat/qtranslate"));
        QCOMPARE(request.headers.value("x-title"), QByteArray("qtranslate"));
        QVERIFY(request.headers.value("content-type").startsWith("application/json"));

        const QJsonObject body = QJsonDocument::fromJson(request.body).object();
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("test/model"));
        QCOMPARE(body.value(QStringLiteral("temperature")).toDouble(), 0.2);
        const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 2);
        const QString system = messages.at(0).toObject().value(QStringLiteral("content")).toString();
        const QString user = messages.at(1).toObject().value(QStringLiteral("content")).toString();
        QVERIFY(system.contains(QStringLiteral("German (de)")));
        QVERIFY(system.contains(QStringLiteral("JSON")));
        QCOMPARE(user, QStringLiteral("Hello, world"));
    }

    void usesExplicitSourceLanguage()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200, chatResponse(translationContent(QStringLiteral("Hallo"), QStringLiteral("fr"))),
                true};
        };

        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("en"));
        const QString system = QJsonDocument::fromJson(m_server->requests.first().body)
                                   .object()
                                   .value(QStringLiteral("messages"))
                                   .toArray()
                                   .at(0)
                                   .toObject()
                                   .value(QStringLiteral("content"))
                                   .toString();
        QVERIFY(system.contains(QStringLiteral("English (en)")));
    }

    void acceptsPlainTextFallback()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, chatResponse(QStringLiteral("Hallo, Welt")), true};
        };

        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello, world"), QStringLiteral("auto"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hallo, Welt"));
        QVERIFY(reply->result().detectedSourceLang.isEmpty());
    }

    void acceptsJsonInMarkdownFence()
    {
        const QString content =
            QStringLiteral("```json\n%1\n```")
                .arg(translationContent(QStringLiteral("Hallo, Welt")));
        m_server->handler = [content](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, chatResponse(content), true};
        };

        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello, world"), QStringLiteral("auto"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hallo, Welt"));
    }

    void preservesOuterWhitespace()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, chatResponse(translationContent(QStringLiteral("Hallo"))),
                                            true};
        };

        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("  Hello\n"), QStringLiteral("en"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("  Hallo\n"));
    }

    void mapsHttpErrors_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<int>("expected");

        QTest::newRow("401") << 401 << int(TranslationError::Auth);
        QTest::newRow("402") << 402 << int(TranslationError::QuotaExceeded);
        QTest::newRow("429") << 429 << int(TranslationError::RateLimited);
        QTest::newRow("413") << 413 << int(TranslationError::TooLong);
        QTest::newRow("400") << 400 << int(TranslationError::BadResponse);
        QTest::newRow("500") << 500 << int(TranslationError::Network);
    }

    void mapsHttpErrors()
    {
        QFETCH(int, status);
        QFETCH(int, expected);

        m_server->handler = [status](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{status, "{}", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(int(reply->result().error), expected);
    }

    void timeout()
    {
        m_network->setTimeoutMs(300);
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Timeout);
    }

    void cancellation()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("de")}));
        reply->abort();
        QCOMPARE(reply->result().error, TranslationError::Cancelled);
    }

    void openRouterRequiresAKey()
    {
        AppSettings settings = makeSettings(m_server->port(), {}, QStringLiteral("test/model"));
        settings.llm.baseUrl = QStringLiteral("https://openrouter.ai/api/v1");
        m_service->applySettings(settings);

        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Auth);
        QVERIFY(reply->result().errorMessage.contains(QStringLiteral("API key")));
        QVERIFY(m_server->requests.isEmpty());
    }

    void loadsModels()
    {
        useOpenRouterEndpoints();
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == QStringLiteral("/api/v1/key"))
                return FakeHttpServer::Response{200, R"({"data":{}})", true};
            if (request.url.path() == QStringLiteral("/api/v1/models")) {
                return FakeHttpServer::Response{
                    200, modelList({QStringLiteral("openai/gpt-4o-mini"),
                                     QStringLiteral("deepseek/deepseek-chat")}),
                    true};
            }
            return FakeHttpServer::Response{404, "{}", true};
        };

        bool called = false;
        bool ok = false;
        QStringList models;
        m_service->loadModels({}, QStringLiteral("test-key"),
                              [&called, &ok, &models](bool success, const QString &,
                                                      const QStringList &result) {
                                  called = true;
                                  ok = success;
                                  models = result;
                              });
        QTRY_VERIFY_WITH_TIMEOUT(called, 5000);
        QVERIFY(ok);
        QCOMPARE(models,
                 (QStringList{QStringLiteral("deepseek/deepseek-chat"),
                              QStringLiteral("openai/gpt-4o-mini")}));
        QCOMPARE(m_server->requests.size(), 2);
        for (const FakeHttpServer::Request &request : m_server->requests)
            QCOMPARE(request.headers.value("authorization"), QByteArray("Bearer test-key"));
    }

    void rejectsInvalidKey()
    {
        useOpenRouterEndpoints();
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{401, "{}", true};
        };

        bool called = false;
        bool ok = true;
        m_service->loadModels({}, QStringLiteral("bad-key"),
                              [&called, &ok](bool success, const QString &, const QStringList &) {
                                  called = true;
                                  ok = success;
                              });
        QTRY_VERIFY_WITH_TIMEOUT(called, 5000);
        QVERIFY(!ok);
        QCOMPARE(m_server->requests.size(), 1);
    }

    void loadModelsUsesEditedBaseUrl()
    {
        AppSettings settings = makeSettings(m_server->port());
        settings.llm.baseUrl = QStringLiteral("https://openrouter.ai/api/v1");
        m_service->applySettings(settings);

        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == QStringLiteral("/api/v1/models"))
                return FakeHttpServer::Response{200, modelList({QStringLiteral("local-model")}),
                                                true};
            return FakeHttpServer::Response{404, "{}", true};
        };

        bool called = false;
        bool ok = false;
        QStringList models;
        m_service->loadModels(
            fakeBaseUrl(m_server->port()), {},
            [&called, &ok, &models](bool success, const QString &,
                                    const QStringList &result) {
                called = true;
                ok = success;
                models = result;
            });
        QTRY_VERIFY_WITH_TIMEOUT(called, 5000);
        QVERIFY(ok);
        QCOMPARE(models, QStringList{QStringLiteral("local-model")});
        QCOMPARE(m_server->requests.size(), 1);
        QCOMPARE(m_server->requests.first().url.path(), QStringLiteral("/api/v1/models"));
    }

    void localServerLoadsModelsWithoutKey()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == QStringLiteral("/api/v1/models"))
                return FakeHttpServer::Response{200, modelList({QStringLiteral("local-model")}),
                                                true};
            return FakeHttpServer::Response{404, "{}", true};
        };

        bool called = false;
        bool ok = false;
        QStringList models;
        m_service->loadModels({}, {}, [&called, &ok, &models](bool success, const QString &,
                                                              const QStringList &result) {
            called = true;
            ok = success;
            models = result;
        });
        QTRY_VERIFY_WITH_TIMEOUT(called, 5000);
        QVERIFY(ok);
        QCOMPARE(models, QStringList{QStringLiteral("local-model")});
        QCOMPARE(m_server->requests.size(), 1);
        QVERIFY(m_server->requests.first().headers.value("authorization").isEmpty());
    }

private:
    void useOpenRouterEndpoints()
    {
        AppSettings settings = makeSettings(m_server->port());
        settings.llm.baseUrl = QStringLiteral("https://openrouter.ai/api/v1");
        m_service->applySettings(settings);
        const QString base = fakeBaseUrl(m_server->port());
        m_service->setEndpoint(QUrl(base + QStringLiteral("/chat/completions")));
        m_service->setModelsEndpoint(QUrl(base + QStringLiteral("/models")));
        m_service->setKeyEndpoint(QUrl(base + QStringLiteral("/key")));
    }

    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<OpenAiCompatibleService> m_service;
};

QTEST_GUILESS_MAIN(TestOpenAiCompatibleService)
#include "tst_openaicompatibleservice.moc"
