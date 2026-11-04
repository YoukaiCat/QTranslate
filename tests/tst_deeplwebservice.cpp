#include "core/settings.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/deeplwebservice.h"

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

QByteArray translationResponse(const QStringList &texts,
                               const QString &detected = QStringLiteral("en"))
{
    QJsonArray translations;
    for (const QString &text : texts) {
        QJsonObject item;
        item.insert(QStringLiteral("text"), text);
        if (translations.isEmpty() && !detected.isEmpty())
            item.insert(QStringLiteral("detected_source_language"), detected);
        translations.append(item);
    }
    QJsonObject root;
    root.insert(QStringLiteral("translations"), translations);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray identityTranslation(const QByteArray &body)
{
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    QStringList texts;
    for (const QJsonValue &value : object.value(QStringLiteral("text")).toArray())
        texts << value.toString();
    return translationResponse(texts);
}

AppSettings settingsWithEnglishVariant(const QString &englishVariant = QStringLiteral("EN-US"))
{
    AppSettings settings;
    settings.deepl.englishVariant = englishVariant;
    return settings;
}

QString longText()
{
    QString text;
    int sentence = 0;
    while (text.size() < 12000) {
        text += QStringLiteral("Sentence number %1 is about the weather. ").arg(++sentence);
        if (sentence % 7 == 0)
            text += QStringLiteral("\n\n");
    }
    return text;
}

} // namespace

class TestDeepLWebService : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
        m_network = std::make_unique<NetworkManager>();
        m_service = std::make_unique<DeepLWebService>(m_network.get());
        m_service->setEndpoint(
            QUrl(QStringLiteral("http://127.0.0.1:%1/v1/translate").arg(m_server->port())));
        m_service->setRateLimitMs(0);
        m_service->setRetryDelayMs(0);
        m_service->applySettings(settingsWithEnglishVariant());
    }

    void cleanup()
    {
        m_service.reset();
        m_network.reset();
        m_server.reset();
    }

    void properties()
    {
        QCOMPARE(m_service->id(), QStringLiteral("deepl-web"));
        QCOMPARE(m_service->displayName(), QStringLiteral("DeepL (free web)"));
        QVERIFY(m_service->isConfigured());
        QVERIFY(m_service->targetLanguages().contains(QStringLiteral("de")));
        QVERIFY(!m_service->sourceLanguages().contains(QStringLiteral("auto")));
        QVERIFY(!m_service->targetLanguages().contains(QStringLiteral("hy")));
    }

    void sendsWebRequest()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, identityTranslation(request.body), true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello, world"), QStringLiteral("auto"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hello, world"));
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("en"));
        QCOMPARE(reply->result().serviceId, QStringLiteral("deepl-web"));

        QCOMPARE(m_server->requests.size(), 1);
        const FakeHttpServer::Request &request = m_server->requests.first();
        QCOMPARE(request.method, QByteArray("POST"));
        QCOMPARE(request.url.path(), QStringLiteral("/v1/translate"));
        QCOMPARE(request.headers.value("authorization"), QByteArray("None"));
        QCOMPARE(request.headers.value("accept"), QByteArray("application/json"));
        QVERIFY(request.headers.value("content-type").startsWith("application/json"));
        const QJsonObject body = QJsonDocument::fromJson(request.body).object();
        QVERIFY(!body.contains(QStringLiteral("source_lang")));
        QCOMPARE(body.value(QStringLiteral("target_lang")).toString(), QStringLiteral("de"));
        const QJsonArray texts = body.value(QStringLiteral("text")).toArray();
        QCOMPARE(texts.size(), 1);
        QCOMPARE(texts.at(0).toString(), QStringLiteral("Hello, world"));
    }

    void mapsDeepLCodesForTheWebEndpoint()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, identityTranslation(request.body), true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("pt-BR")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        const QJsonObject body = QJsonDocument::fromJson(m_server->requests.first().body).object();
        QCOMPARE(body.value(QStringLiteral("source_lang")).toString(), QStringLiteral("en"));
        QCOMPARE(body.value(QStringLiteral("target_lang")).toString(), QStringLiteral("pt-BR"));
    }

    void usesEnglishVariant()
    {
        m_service->applySettings(settingsWithEnglishVariant(QStringLiteral("EN-GB")));
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, identityTranslation(request.body), true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hallo"), QStringLiteral("de"), QStringLiteral("en")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        const QJsonObject body = QJsonDocument::fromJson(m_server->requests.first().body).object();
        QCOMPARE(body.value(QStringLiteral("target_lang")).toString(), QStringLiteral("en-GB"));
    }

    void longTextIsOneRequestAndKeepsWhitespace()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, identityTranslation(request.body), true};
        };
        const QString text = longText();
        const QStringList parts = TextChunker::split(text, 5000);
        QCOMPARE(parts.size(), 3);

        std::unique_ptr<TranslationReply> reply(
            m_service->translate({text, QStringLiteral("en"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(10000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, text);
        QCOMPARE(m_server->requests.size(), 1);
        const QJsonObject body =
            QJsonDocument::fromJson(m_server->requests.first().body).object();
        QCOMPARE(body.value(QStringLiteral("text")).toArray().size(), parts.size());
    }

    void retriesOnceAfterTooManyRequests()
    {
        int calls = 0;
        m_server->handler = [&calls](const FakeHttpServer::Request &request) {
            if (++calls == 1)
                return FakeHttpServer::Response{
                    429, R"({"error":{"code":1042911,"message":"Too many requests"}})", true};
            return FakeHttpServer::Response{200, identityTranslation(request.body), true};
        };

        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(m_server->requests.size(), 2);
    }

    void givesUpAfterTwoTooManyRequests()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                429, R"({"error":{"code":1042911,"message":"Too many requests"}})", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::RateLimited);
        QCOMPARE(reply->result().errorMessage, QStringLiteral("HTTP 429"));
        QCOMPARE(m_server->requests.size(), 2);
    }

    void forbiddenIsTreatedAsRateLimited()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{403, "blocked", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::RateLimited);
        QCOMPARE(m_server->requests.size(), 1);
    }

    void serverErrorIsNetwork()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{503, "unavailable", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Network);
        QCOMPARE(reply->result().errorMessage, QStringLiteral("HTTP 503"));
    }

    void htmlResponseIsBadResponse()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, "<html>blocked</html>", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::BadResponse);
    }

    void rateLimitInBodyIsRateLimited()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200, R"({"error":{"code":1042911,"message":"Too many requests"}})", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::RateLimited);
        QCOMPARE(m_server->requests.size(), 1);
    }

    void timeout()
    {
        m_network->setTimeoutMs(300);
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Timeout);
    }

    void serializesRequestsWithRateLimit()
    {
        m_service->setRateLimitMs(200);
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, identityTranslation(request.body), true};
        };

        std::unique_ptr<TranslationReply> first(m_service->translate(
            {QStringLiteral("One"), QStringLiteral("auto"), QStringLiteral("de")}));
        std::unique_ptr<TranslationReply> second(m_service->translate(
            {QStringLiteral("Two"), QStringLiteral("auto"), QStringLiteral("de")}));
        QSignalSpy firstFinished(first.get(), &TranslationReply::finished);
        QSignalSpy secondFinished(second.get(), &TranslationReply::finished);
        QVERIFY(firstFinished.wait(5000));
        QVERIFY(secondFinished.wait(5000));
        QVERIFY2(first->result().ok(), qPrintable(first->result().errorMessage));
        QVERIFY2(second->result().ok(), qPrintable(second->result().errorMessage));
        QCOMPARE(m_server->requests.size(), 2);

        const qint64 delta = m_server->requests.at(1).elapsedMs
            - m_server->requests.at(0).elapsedMs;
        QVERIFY2(delta >= 180, qPrintable(QStringLiteral("delta %1 ms").arg(delta)));
    }

    void toWebCode_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");

        QTest::newRow("German") << "DE" << "de";
        QTest::newRow("Russian") << "RU" << "ru";
        QTest::newRow("English US") << "EN-US" << "en-US";
        QTest::newRow("English GB") << "EN-GB" << "en-GB";
        QTest::newRow("Portuguese Brazil") << "PT-BR" << "pt-BR";
        QTest::newRow("Chinese simplified") << "ZH-HANS" << "zh-Hans";
        QTest::newRow("Chinese traditional") << "ZH-HANT" << "zh-Hant";
        QTest::newRow("empty") << "" << "";
    }

    void toWebCode()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(DeepLWebService::toWebCode(input), expected);
    }

private:
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<DeepLWebService> m_service;
};

QTEST_GUILESS_MAIN(TestDeepLWebService)
#include "tst_deeplwebservice.moc"
