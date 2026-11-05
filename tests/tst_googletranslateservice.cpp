#include "core/settings.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/googletranslateservice.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

#include <functional>
#include <memory>

namespace {

// Minimal HTTP/1.1 server on localhost that answers each request with what
// `handler` returns. No network access is involved.
class FakeHttpServer : public QObject
{
public:
    struct Request
    {
        QByteArray method;
        QUrl url;
        QHash<QByteArray, QByteArray> headers; // lower-case names
        QByteArray body;
        QString text;                          // decoded "q" form field
    };
    struct Response
    {
        int status = 200;
        QByteArray body;
        bool respond = true; // false: keep the connection open and never answer
    };

    FakeHttpServer()
    {
        connect(&m_server, &QTcpServer::newConnection, this, &FakeHttpServer::onNewConnection);
        if (!m_server.listen(QHostAddress::LocalHost))
            qFatal("Cannot listen on localhost");
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/translate_a/single").arg(m_server.serverPort()));
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
        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
        request.method = requestLine.value(0);
        request.url = QUrl(QString::fromLatin1(requestLine.value(1)));
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const qsizetype colon = lines.at(i).indexOf(':');
            if (colon > 0)
                request.headers.insert(lines.at(i).left(colon).trimmed().toLower(),
                                       lines.at(i).mid(colon + 1).trimmed());
        }
        const qsizetype length = request.headers.value("content-length").toLongLong();
        if (buffer.size() < headerEnd + 4 + length)
            return;
        const QByteArray body = buffer.mid(headerEnd + 4, length);
        m_buffers.remove(socket);
        request.body = body;
        if (body.startsWith("q="))
            request.text = QUrl::fromPercentEncoding(body.mid(2));

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
};

// A gtx-style answer: one sentence, detected language "en".
QByteArray gtxResponse(const QString &translation, const QString &original)
{
    const QJsonArray sentence{translation, original, QJsonValue(), QJsonValue(), 10};
    QJsonArray sentences; // not QJsonArray{sentence}: that would be a copy of `sentence`
    sentences.append(sentence);
    const QJsonArray root{sentences, QJsonValue(), QStringLiteral("en")};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

FakeHttpServer::Response bracketed(const FakeHttpServer::Request &request)
{
    return {200, gtxResponse(QLatin1Char('[') + request.text + QLatin1Char(']'), request.text), true};
}

FakeHttpServer::Response identity(const FakeHttpServer::Request &request)
{
    return {200, gtxResponse(request.text, request.text), true};
}

QByteArray cloudResponse(const QString &translation, const QString &detected = QStringLiteral("en"))
{
    QJsonObject item;
    item.insert(QStringLiteral("translatedText"), translation);
    if (!detected.isEmpty())
        item.insert(QStringLiteral("detectedSourceLanguage"), detected);
    QJsonObject data;
    data.insert(QStringLiteral("translations"), QJsonArray{item});
    QJsonObject root;
    root.insert(QStringLiteral("data"), data);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

FakeHttpServer::Response cloudIdentity(const FakeHttpServer::Request &request)
{
    const QJsonObject object = QJsonDocument::fromJson(request.body).object();
    return {200, cloudResponse(QLatin1Char('[') + object.value(QStringLiteral("q")).toString()
                                   + QLatin1Char(']')),
            true};
}

// About 12 000 characters of paragraphs and sentences: three parts.
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

class TestGoogleTranslateService : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
        m_network = std::make_unique<NetworkManager>();
        m_service = std::make_unique<GoogleTranslateService>(m_network.get());
        m_service->setEndpoint(m_server->url());
    }

    void cleanup()
    {
        m_service.reset();
        m_network.reset();
        m_server.reset();
    }

    void properties()
    {
        QCOMPARE(m_service->id(), QStringLiteral("google"));
        QVERIFY(m_service->isConfigured());
        QVERIFY(m_service->targetLanguages().contains(QStringLiteral("zh-Hans")));
        QVERIFY(!m_service->sourceLanguages().contains(QStringLiteral("auto")));
    }

    void translatesOneRequest()
    {
        m_server->handler = bracketed;
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello, world & more"), QStringLiteral("auto"), QStringLiteral("zh-Hans")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));

        const TranslationResult &result = reply->result();
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.translatedText, QStringLiteral("[Hello, world & more]"));
        QCOMPARE(result.detectedSourceLang, QStringLiteral("en"));
        QCOMPARE(result.serviceId, QStringLiteral("google"));

        QCOMPARE(m_server->requests.size(), 1);
        const FakeHttpServer::Request &request = m_server->requests.first();
        QCOMPARE(request.method, QByteArray("POST"));
        QCOMPARE(request.url.path(), QStringLiteral("/translate_a/single"));
        const QUrlQuery query(request.url);
        QCOMPARE(query.queryItemValue(QStringLiteral("client")), QStringLiteral("gtx"));
        QCOMPARE(query.queryItemValue(QStringLiteral("sl")), QStringLiteral("auto"));
        QCOMPARE(query.queryItemValue(QStringLiteral("tl")), QStringLiteral("zh-CN"));
        QCOMPARE(query.queryItemValue(QStringLiteral("dt")), QStringLiteral("t"));
        QVERIFY(request.headers.value("content-type").startsWith("application/x-www-form-urlencoded"));
        QVERIFY(request.headers.value("user-agent").startsWith("QTranslate/"));
        QCOMPARE(request.text, QStringLiteral("Hello, world & more"));
    }

    void explicitSourceLanguageIsMapped()
    {
        m_server->handler = bracketed;
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Shalom"), QStringLiteral("he"), QStringLiteral("en")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(QUrlQuery(m_server->requests.first().url).queryItemValue(QStringLiteral("sl")),
                 QStringLiteral("iw"));
    }

    void edgeWhitespaceIsKeptOutOfRequest()
    {
        m_server->handler = bracketed;
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("  Hello\n world \n"), QStringLiteral("auto"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(m_server->requests.first().text, QStringLiteral("Hello\n world"));
        QCOMPARE(reply->result().translatedText, QStringLiteral("  [Hello\n world] \n"));
    }

    void longTextIsSentInPartsAndJoined()
    {
        m_server->handler = identity;
        const QString text = longText();
        const QStringList parts = TextChunker::split(text, GoogleTranslateService::kMaxChunkLength);
        QCOMPARE(parts.size(), 3);

        std::unique_ptr<TranslationReply> reply(
            m_service->translate({text, QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(10000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        QCOMPARE(m_server->requests.size(), 3);
        for (qsizetype i = 0; i < parts.size(); ++i) {
            QCOMPARE(m_server->requests.at(i).text, parts.at(i).trimmed());
            QVERIFY(m_server->requests.at(i).text.size() <= GoogleTranslateService::kMaxChunkLength);
        }
        // Joined without losing or duplicating anything at the joints.
        QCOMPARE(reply->result().translatedText, text);
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("en"));
    }

    void errorInAnyPartFailsWholeRequest()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            static int count = 0;
            return ++count == 2 ? FakeHttpServer::Response{503, "busy", true} : identity(request);
        };
        std::unique_ptr<TranslationReply> reply(
            m_service->translate({longText(), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(10000));
        QCOMPARE(reply->result().error, TranslationError::Network);
        QCOMPARE(reply->result().errorMessage, QStringLiteral("HTTP 503"));
        QVERIFY(reply->result().translatedText.isEmpty());
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 2);
        QCOMPARE(finished.size(), 1);
    }

    void httpErrorStatus()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{429, "Too Many Requests", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::RateLimited);
        QCOMPARE(reply->result().serviceId, QStringLiteral("google"));
    }

    void unexpectedBody()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, "<html>sorry</html>", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::BadResponse);
    }

    void connectionRefused()
    {
        // A port nobody listens on any more.
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        const quint16 port = probe.serverPort();
        probe.close();
        m_service->setEndpoint(QUrl(QStringLiteral("http://127.0.0.1:%1/translate_a/single").arg(port)));

        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(10000));
        QCOMPARE(reply->result().error, TranslationError::Network);
        QVERIFY(!reply->result().errorMessage.isEmpty());
    }

    void timeout()
    {
        m_network->setTimeoutMs(300);
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Timeout);
        QVERIFY(reply->result().errorMessage.contains(QStringLiteral("0.3 s")));
    }

    void abortDuringRequest()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QTRY_COMPARE(m_server->requests.size(), 1);

        reply->abort();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(reply->result().error, TranslationError::Cancelled);
        QTest::qWait(100);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(reply->result().error, TranslationError::Cancelled);
    }

    void abortBeforeStart()
    {
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")}));
        reply->abort();
        QTest::qWait(100);
        QVERIFY(m_server->requests.isEmpty());
        QCOMPARE(reply->result().error, TranslationError::Cancelled);
    }

    void deleteDuringRequest()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };
        TranslationReply *reply = m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")});
        QTRY_COMPARE(m_server->requests.size(), 1);
        delete reply;
        QTest::qWait(100); // nothing touches the deleted reply
    }

    void unsupportedLanguage()
    {
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("xx")}));
        QVERIFY(!reply->isFinished());
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::UnsupportedLanguage);
        QVERIFY(reply->result().errorMessage.contains(QStringLiteral("xx")));
        QVERIFY(m_server->requests.isEmpty());
    }

    void whitespaceOnlyNeedsNoRequest()
    {
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral(" \n "), QStringLiteral("auto"), QStringLiteral("ru")}));
        QVERIFY(!reply->isFinished());
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QVERIFY(reply->result().ok());
        QCOMPARE(reply->result().translatedText, QStringLiteral(" \n "));
        QVERIFY(m_server->requests.isEmpty());
    }

    void cloudModeSendsJsonAndKey()
    {
        AppSettings settings;
        settings.google.mode = QStringLiteral("cloud");
        settings.google.apiKey = QStringLiteral("test-cloud-key");
        m_service->applySettings(settings);
        QVERIFY(m_service->isConfigured());
        m_service->setCloudEndpoint(
            QUrl(QStringLiteral("http://127.0.0.1:%1/language/translate/v2")
                     .arg(m_server->port())));
        m_server->handler = cloudIdentity;

        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello & goodbye"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("[Hello & goodbye]"));
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("en"));

        QCOMPARE(m_server->requests.size(), 1);
        const FakeHttpServer::Request &request = m_server->requests.first();
        QCOMPARE(request.method, QByteArray("POST"));
        QCOMPARE(request.url.path(), QStringLiteral("/language/translate/v2"));
        QCOMPARE(QUrlQuery(request.url).queryItemValue(QStringLiteral("key")),
                 QStringLiteral("test-cloud-key"));
        QVERIFY(request.headers.value("content-type").startsWith("application/json"));
        const QJsonObject body = QJsonDocument::fromJson(request.body).object();
        QCOMPARE(body.value(QStringLiteral("q")).toString(), QStringLiteral("Hello & goodbye"));
        QCOMPARE(body.value(QStringLiteral("target")).toString(), QStringLiteral("ru"));
        QCOMPARE(body.value(QStringLiteral("source")).toString(), QStringLiteral("en"));
        QCOMPARE(body.value(QStringLiteral("format")).toString(), QStringLiteral("text"));
    }

    void cloudModeWithoutKeyIsNotConfigured()
    {
        AppSettings settings;
        settings.google.mode = QStringLiteral("cloud");
        settings.google.apiKey.clear();
        m_service->applySettings(settings);
        QVERIFY(!m_service->isConfigured());

        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::Auth);
        QVERIFY(m_server->requests.isEmpty());
    }

    void cloudHttpError()
    {
        AppSettings settings;
        settings.google.mode = QStringLiteral("cloud");
        settings.google.apiKey = QStringLiteral("bad-key");
        m_service->applySettings(settings);
        m_service->setCloudEndpoint(
            QUrl(QStringLiteral("http://127.0.0.1:%1/language/translate/v2")
                     .arg(m_server->port())));
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                400, R"({"error":{"message":"API key not valid."}})", true};
        };

        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Auth);
        QCOMPARE(reply->result().errorMessage, QStringLiteral("HTTP 400"));
    }

    void cloudNetworkErrorHidesTheKey()
    {
        AppSettings settings;
        settings.google.mode = QStringLiteral("cloud");
        settings.google.apiKey = QStringLiteral("secret-cloud-key");
        m_service->applySettings(settings);
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        const quint16 port = probe.serverPort();
        probe.close();
        m_service->setCloudEndpoint(
            QUrl(QStringLiteral("http://127.0.0.1:%1/language/translate/v2").arg(port)));

        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(10000));
        QCOMPARE(reply->result().error, TranslationError::Network);
        QVERIFY(!reply->result().errorMessage.contains(QStringLiteral("secret-cloud-key")));
    }

private:
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<GoogleTranslateService> m_service;
};

QTEST_GUILESS_MAIN(TestGoogleTranslateService)
#include "tst_googletranslateservice.moc"
