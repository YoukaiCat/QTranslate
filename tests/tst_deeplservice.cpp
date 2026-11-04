#include "core/settings.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/deeplservice.h"

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
    };
    struct Response
    {
        int status = 200;
        QByteArray body;
        bool respond = true;
    };

    FakeHttpServer()
    {
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
};

QByteArray translationResponse(const QStringList &texts,
                               const QString &detected = QStringLiteral("EN"))
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

AppSettings configuredSettings(const QString &key = QStringLiteral("secret-key"))
{
    AppSettings settings;
    settings.deepl.apiKey = key;
    settings.deepl.englishVariant = QStringLiteral("EN-US");
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

class TestDeepLService : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
        m_network = std::make_unique<NetworkManager>();
        m_service = std::make_unique<DeepLService>(m_network.get());
        m_service->setEndpoint(
            QUrl(QStringLiteral("http://127.0.0.1:%1/v2/translate").arg(m_server->port())));
        m_service->applySettings(configuredSettings());
    }

    void cleanup()
    {
        m_service.reset();
        m_network.reset();
        m_server.reset();
    }

    void properties()
    {
        QCOMPARE(m_service->id(), QStringLiteral("deepl"));
        QCOMPARE(m_service->displayName(), QStringLiteral("DeepL"));
        QVERIFY(m_service->isConfigured());
        QVERIFY(m_service->targetLanguages().contains(QStringLiteral("de")));
        QVERIFY(!m_service->sourceLanguages().contains(QStringLiteral("auto")));
        QVERIFY(!m_service->targetLanguages().contains(QStringLiteral("fil")));
    }

    void freeAndProHosts()
    {
        QCOMPARE(DeepLService::defaultEndpointForKey(QStringLiteral("key:fx")).host(),
                 QStringLiteral("api-free.deepl.com"));
        QCOMPARE(DeepLService::defaultEndpointForKey(QStringLiteral("key")).host(),
                 QStringLiteral("api.deepl.com"));
    }

    void sendsAuthorizationAndArray()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, identityTranslation(request.body), true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello, world"), QStringLiteral("en"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hello, world"));
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("en"));
        QCOMPARE(reply->result().serviceId, QStringLiteral("deepl"));

        QCOMPARE(m_server->requests.size(), 1);
        const FakeHttpServer::Request &request = m_server->requests.first();
        QCOMPARE(request.method, QByteArray("POST"));
        QCOMPARE(request.url.path(), QStringLiteral("/v2/translate"));
        QCOMPARE(request.headers.value("authorization"),
                 QByteArray("DeepL-Auth-Key secret-key"));
        QVERIFY(request.headers.value("content-type").startsWith("application/json"));
        const QJsonObject body = QJsonDocument::fromJson(request.body).object();
        QCOMPARE(body.value(QStringLiteral("source_lang")).toString(), QStringLiteral("EN"));
        QCOMPARE(body.value(QStringLiteral("target_lang")).toString(), QStringLiteral("DE"));
        const QJsonArray texts = body.value(QStringLiteral("text")).toArray();
        QCOMPARE(texts.size(), 1);
        QCOMPARE(texts.at(0).toString(), QStringLiteral("Hello, world"));
    }

    void englishVariantIsUsedForTarget()
    {
        AppSettings settings = configuredSettings();
        settings.deepl.englishVariant = QStringLiteral("EN-GB");
        m_service->applySettings(settings);
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, identityTranslation(request.body), true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hallo"), QStringLiteral("de"), QStringLiteral("en")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        const QJsonObject body = QJsonDocument::fromJson(m_server->requests.first().body).object();
        QCOMPARE(body.value(QStringLiteral("target_lang")).toString(), QStringLiteral("EN-GB"));
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

    void missingKeyIsNotConfigured()
    {
        m_service->applySettings(configuredSettings(QString()));
        QVERIFY(!m_service->isConfigured());
        std::unique_ptr<TranslationReply> reply(
            m_service->translate({QStringLiteral("Hello"), QStringLiteral("auto"),
                                  QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::Auth);
        QVERIFY(m_server->requests.isEmpty());
    }

    void httpErrorStatuses_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("403") << 403 << TranslationError::Auth;
        QTest::newRow("413") << 413 << TranslationError::TooLong;
        QTest::newRow("429") << 429 << TranslationError::RateLimited;
        QTest::newRow("456") << 456 << TranslationError::QuotaExceeded;
        QTest::newRow("500") << 500 << TranslationError::Network;
        QTest::newRow("400") << 400 << TranslationError::BadResponse;
    }

    void httpErrorStatuses()
    {
        QFETCH(int, status);
        QFETCH(TranslationError, error);
        m_server->handler = [status](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{status, "error", true};
        };
        std::unique_ptr<TranslationReply> reply(
            m_service->translate({QStringLiteral("Hello"), QStringLiteral("auto"),
                                  QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, error);
        QCOMPARE(reply->result().errorMessage, QStringLiteral("HTTP %1").arg(status));
        QVERIFY(!reply->result().errorMessage.contains(QStringLiteral("secret-key")));
    }

    void unexpectedBody()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, "<html>error</html>", true};
        };
        std::unique_ptr<TranslationReply> reply(
            m_service->translate({QStringLiteral("Hello"), QStringLiteral("auto"),
                                  QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::BadResponse);
    }

    void connectionRefused()
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        const quint16 port = probe.serverPort();
        probe.close();
        m_service->setEndpoint(
            QUrl(QStringLiteral("http://127.0.0.1:%1/v2/translate").arg(port)));

        std::unique_ptr<TranslationReply> reply(
            m_service->translate({QStringLiteral("Hello"), QStringLiteral("auto"),
                                  QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(10000));
        QCOMPARE(reply->result().error, TranslationError::Network);
    }

    void timeout()
    {
        m_network->setTimeoutMs(300);
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };
        std::unique_ptr<TranslationReply> reply(
            m_service->translate({QStringLiteral("Hello"), QStringLiteral("auto"),
                                  QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Timeout);
    }

    void checkKeySuccess()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == QStringLiteral("/v2/usage"))
                return FakeHttpServer::Response{200,
                                                R"({"character_count":1234,"character_limit":500000})",
                                                true};
            return FakeHttpServer::Response{404, "no", true};
        };

        bool called = false;
        bool ok = false;
        QString message;
        m_service->checkKey(QStringLiteral("key:fx"), [&](bool resultOk, const QString &text) {
            called = true;
            ok = resultOk;
            message = text;
        });
        QTRY_VERIFY_WITH_TIMEOUT(called, 5000);
        QVERIFY(ok);
        QVERIFY(message.contains(QStringLiteral("1234")));
        QVERIFY(message.contains(QStringLiteral("500000")));
        QCOMPARE(m_server->requests.size(), 1);
        QCOMPARE(m_server->requests.first().method, QByteArray("GET"));
        QCOMPARE(m_server->requests.first().url.path(), QStringLiteral("/v2/usage"));
        QCOMPARE(m_server->requests.first().headers.value("authorization"),
                 QByteArray("DeepL-Auth-Key key:fx"));
    }

    void checkKeyError()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{403, "denied", true};
        };
        bool called = false;
        bool ok = true;
        QString message;
        m_service->checkKey(QStringLiteral("bad-key"), [&](bool resultOk, const QString &text) {
            called = true;
            ok = resultOk;
            message = text;
        });
        QTRY_VERIFY_WITH_TIMEOUT(called, 5000);
        QVERIFY(!ok);
        QVERIFY(message.contains(QStringLiteral("rejected")));
        QVERIFY(!message.contains(QStringLiteral("bad-key")));
    }

    void checkKeyEmpty()
    {
        bool called = false;
        bool ok = true;
        m_service->checkKey(QString(), [&](bool resultOk, const QString &) {
            called = true;
            ok = resultOk;
        });
        QTRY_VERIFY_WITH_TIMEOUT(called, 1000);
        QVERIFY(!ok);
        QVERIFY(m_server->requests.isEmpty());
    }

private:
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<DeepLService> m_service;
};

QTEST_GUILESS_MAIN(TestDeepLService)
#include "tst_deeplservice.moc"
