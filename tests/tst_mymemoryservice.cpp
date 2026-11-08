#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/mymemoryservice.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

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

    void close() { m_server.close(); }

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

// The query as the endpoint sees it: percent-escapes decoded back to text.
QString queryValue(const QUrl &url, const QString &key)
{
    return QUrlQuery(url).queryItemValue(key, QUrl::FullyDecoded);
}

QByteArray translationResponse(const QString &text, const QString &detected = QString())
{
    QJsonObject data;
    data.insert(QStringLiteral("translatedText"), text);
    if (!detected.isEmpty())
        data.insert(QStringLiteral("detectedLanguage"), detected);
    QJsonObject root;
    root.insert(QStringLiteral("responseData"), data);
    root.insert(QStringLiteral("quotaFinished"), false);
    root.insert(QStringLiteral("responseDetails"), QString());
    root.insert(QStringLiteral("responseStatus"), 200);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// Answers every chunk with its own text, as a translation would.
QByteArray echoResponse(const QUrl &url)
{
    return translationResponse(queryValue(url, QStringLiteral("q")));
}

// Cyrillic and emoji: several chunks at 500 bytes, with edge whitespace.
QString longText()
{
    QString text = QStringLiteral("  ");
    int sentence = 0;
    while (text.size() < 2500) {
        text += QStringLiteral("Предложение номер %1 рассказывает о погоде и природе.  ")
                    .arg(++sentence);
        if (sentence % 5 == 0)
            text += QStringLiteral("\n\n");
    }
    return text;
}

// Error messages are fixed: neither the text, nor the query, nor the URL may
// leak into them.
void verifyFixedMessage(const QString &message)
{
    QVERIFY(!message.isEmpty());
    QVERIFY2(!message.contains(QStringLiteral("TOPSECRET")), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("127.0.0.1")), qPrintable(message));
}

} // namespace

class TestMyMemoryService : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
        m_network = std::make_unique<NetworkManager>();
        m_service = std::make_unique<MyMemoryService>(m_network.get());
        m_service->setEndpoint(
            QUrl(QStringLiteral("http://127.0.0.1:%1/get").arg(m_server->port())));
        m_service->setRateLimitMs(0);
    }

    void cleanup()
    {
        m_service.reset();
        m_network.reset();
        m_server.reset();
    }

    void properties()
    {
        QCOMPARE(m_service->id(), QStringLiteral("mymemory"));
        QCOMPARE(m_service->displayName(), QStringLiteral("MyMemory"));
        QVERIFY(m_service->isConfigured());
        QVERIFY(m_service->requiresOptIn());
        QVERIFY(m_service->sourceLanguages().contains(QStringLiteral("ru")));
        QVERIFY(m_service->targetLanguages().contains(QStringLiteral("zh-Hans")));
        QVERIFY(!m_service->sourceLanguages().contains(QStringLiteral("auto")));
        QVERIFY(!m_service->targetLanguages().contains(QStringLiteral("auto")));
        QCOMPARE(MyMemoryService::defaultEndpoint(),
                 QUrl(QStringLiteral("https://api.mymemory.translated.net/get")));
    }

    void defaultPause()
    {
        MyMemoryService service(m_network.get());
        QCOMPARE(service.rateLimitMs(), 350);
    }

    void sendsGetRequest()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.url), true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello, world"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hello, world"));
        QCOMPARE(reply->result().serviceId, QStringLiteral("mymemory"));

        QCOMPARE(m_server->requests.size(), 1);
        const FakeHttpServer::Request &request = m_server->requests.first();
        QCOMPARE(request.method, QByteArray("GET"));
        QCOMPARE(request.url.path(), QStringLiteral("/get"));
        QCOMPARE(queryValue(request.url, QStringLiteral("q")), QStringLiteral("Hello, world"));
        QCOMPARE(queryValue(request.url, QStringLiteral("langpair")), QStringLiteral("en|ru"));
        QCOMPARE(queryValue(request.url, QStringLiteral("mt")), QStringLiteral("1"));
        QCOMPARE(request.headers.value("accept"), QByteArray("application/json"));
        QVERIFY(!request.headers.contains("authorization"));
    }

    void edgeWhitespaceIsTrimmed()
    {
        // The endpoint must receive the text without its edge whitespace, while
        // the result keeps those edges exactly as the user wrote them.
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("X")), true};
        };
        const QString text = QStringLiteral(" \tHello\r\n ");
        std::unique_ptr<TranslationReply> reply(
            m_service->translate({text, QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(m_server->requests.size(), 1);
        QCOMPARE(queryValue(m_server->requests.first().url, QStringLiteral("q")),
                 QStringLiteral("Hello"));
        QCOMPARE(reply->result().translatedText, QStringLiteral(" \tX\r\n "));
    }

    void encodesQuery()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.url), true};
        };
        const QString text = QStringLiteral("Hello + мир 😀");
        std::unique_ptr<TranslationReply> reply(
            m_service->translate({text, QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        const QUrl url = m_server->requests.first().url;
        QCOMPARE(queryValue(url, QStringLiteral("q")), text);
        const QString raw = url.query(QUrl::FullyEncoded);
        QVERIFY2(!raw.contains(QLatin1Char(' ')), qPrintable(raw));
        // A literal '+' would reach a form-decoding server as a space.
        QVERIFY2(!raw.contains(QLatin1Char('+')), qPrintable(raw));
        QVERIFY2(raw.contains(QStringLiteral("%20")), qPrintable(raw));
        QVERIFY2(raw.contains(QStringLiteral("%2B")), qPrintable(raw));
        QVERIFY2(raw.contains(QStringLiteral("%D0")), qPrintable(raw)); // Cyrillic
        QVERIFY2(raw.contains(QStringLiteral("%F0%9F")), qPrintable(raw)); // emoji
    }

    void mapsLanguageCodes_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("langpair");

        QTest::newRow("Portuguese Brazil") << "ru" << "pt-BR" << "ru|pt-BR";
        QTest::newRow("Chinese simplified") << "en" << "zh-Hans" << "en|zh-CN";
        QTest::newRow("Chinese traditional") << "en" << "zh-Hant" << "en|zh-TW";
        QTest::newRow("Norwegian") << "nb" << "en" << "no|en";
        QTest::newRow("Filipino") << "fil" << "en" << "tl|en";
        QTest::newRow("Hebrew") << "he" << "en" << "he|en";
    }

    void mapsLanguageCodes()
    {
        QFETCH(QString, source);
        QFETCH(QString, target);
        QFETCH(QString, langpair);

        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.url), true};
        };
        std::unique_ptr<TranslationReply> reply(
            m_service->translate({QStringLiteral("Hello"), source, target}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(queryValue(m_server->requests.first().url, QStringLiteral("langpair")),
                 langpair);
    }

    void detectedLanguage()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200, translationResponse(QStringLiteral("Hello"), QStringLiteral("zh-CN")), true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("你好"), QStringLiteral("zh-Hans"), QStringLiteral("en")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("zh-Hans"));
    }

    void autoSourceIsRejected()
    {
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::UnsupportedLanguage);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("MyMemory needs an explicit source language; choose one instead "
                                "of Auto-detect"));
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 0);
    }

    void unknownTargetIsRejected()
    {
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("xx")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::UnsupportedLanguage);
        QVERIFY(reply->result().errorMessage.contains(
            QStringLiteral("language \"xx\" is not supported")));
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 0);
    }

    void emptyTextIsEmptyError_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("empty") << QString();
        QTest::newRow("whitespace") << QStringLiteral(" \n\t ");
    }

    void emptyTextIsEmptyError()
    {
        QFETCH(QString, text);

        std::unique_ptr<TranslationReply> reply(
            m_service->translate({text, QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::EmptyText);
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 0);
    }

    void longTextIsSplitIntoByteChunks()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.url), true};
        };
        const QString text = longText();
        const QStringList expectedParts = TextChunker::splitUtf8(text, 500);
        QVERIFY(expectedParts.size() > 3);

        std::unique_ptr<TranslationReply> reply(
            m_service->translate({text, QStringLiteral("ru"), QStringLiteral("en")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, text);
        QCOMPARE(m_server->requests.size(), expectedParts.size());
        for (const FakeHttpServer::Request &request : std::as_const(m_server->requests)) {
            const QString q = queryValue(request.url, QStringLiteral("q"));
            QVERIFY2(!q.isEmpty(), "a whitespace-only part must not be sent");
            QVERIFY2(q.toUtf8().size() <= 500,
                     qPrintable(QStringLiteral("%1 bytes").arg(q.toUtf8().size())));
        }
    }

    void failedChunkFailsTheWholeReply()
    {
        auto calls = std::make_shared<int>(0);
        m_server->handler = [calls](const FakeHttpServer::Request &request) {
            if (++*calls == 1)
                return FakeHttpServer::Response{200, echoResponse(request.url), true};
            return FakeHttpServer::Response{503, "blocked", true};
        };
        const QString text = longText();
        QVERIFY(TextChunker::splitUtf8(text, 500).size() > 2);

        std::unique_ptr<TranslationReply> reply(
            m_service->translate({text, QStringLiteral("ru"), QStringLiteral("en")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QCOMPARE(reply->result().error, TranslationError::Network);
        QCOMPARE(reply->result().errorMessage, QStringLiteral("HTTP 503"));
        QVERIFY(reply->result().translatedText.isEmpty());
        QCOMPARE(m_server->requests.size(), 2);
    }

    void pacesChunks()
    {
        m_service->setRateLimitMs(100);
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.url), true};
        };
        const QString text = longText();
        QVERIFY(TextChunker::splitUtf8(text, 500).size() > 2);

        std::unique_ptr<TranslationReply> reply(
            m_service->translate({text, QStringLiteral("ru"), QStringLiteral("en")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(m_server->requests.size(), TextChunker::splitUtf8(text, 500).size());
        for (qsizetype i = 1; i < m_server->requests.size(); ++i) {
            const qint64 delta = m_server->requests.at(i).elapsedMs
                - m_server->requests.at(i - 1).elapsedMs;
            QVERIFY2(delta >= 90, qPrintable(QStringLiteral("delta %1 ms").arg(delta)));
        }
    }

    void serializesRequests()
    {
        m_service->setRateLimitMs(200);
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.url), true};
        };

        std::unique_ptr<TranslationReply> first(m_service->translate(
            {QStringLiteral("One"), QStringLiteral("en"), QStringLiteral("ru")}));
        std::unique_ptr<TranslationReply> second(m_service->translate(
            {QStringLiteral("Two"), QStringLiteral("en"), QStringLiteral("ru")}));
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

    void cancelsQueuedRequest()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };

        std::unique_ptr<TranslationReply> first(m_service->translate(
            {QStringLiteral("One"), QStringLiteral("en"), QStringLiteral("ru")}));
        QTRY_VERIFY_WITH_TIMEOUT(m_server->requests.size() == 1, 5000);

        std::unique_ptr<TranslationReply> second(m_service->translate(
            {QStringLiteral("Two"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy secondFinished(second.get(), &TranslationReply::finished);
        second->abort();
        QCOMPARE(second->result().error, TranslationError::Cancelled);
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 1);
        QCOMPARE(secondFinished.size(), 1);
    }

    void abortActive()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };

        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QTRY_VERIFY_WITH_TIMEOUT(m_server->requests.size() == 1, 5000);
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        reply->abort();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(reply->result().error, TranslationError::Cancelled);
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 1);
    }

    void httpErrors_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("429 rate limit") << 429 << TranslationError::RateLimited;
        QTest::newRow("403 forbidden") << 403 << TranslationError::RateLimited;
        QTest::newRow("503 unavailable") << 503 << TranslationError::Network;
        QTest::newRow("414 uri too long") << 414 << TranslationError::TooLong;
        QTest::newRow("400 bad request") << 400 << TranslationError::BadResponse;
    }

    void httpErrors()
    {
        QFETCH(int, status);
        QFETCH(TranslationError, error);

        const int responseStatus = status;
        m_server->handler = [responseStatus](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{responseStatus, "blocked", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, error);
        QCOMPARE(reply->result().errorMessage, QStringLiteral("HTTP %1").arg(status));
    }

    void htmlBody()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200, "<!DOCTYPE html><html><body>blocked</body></html>", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::ServiceUnavailable);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("the endpoint returned an HTML page"));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void quotaFinished()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200,
                R"({"responseData":{},"quotaFinished":true,"responseDetails":"","responseStatus":200})",
                true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::QuotaExceeded);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void rateLimitBody()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200,
                R"({"responseData":{},"quotaFinished":false,"responseDetails":"TOO MANY REQUESTS","responseStatus":200})",
                true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::RateLimited);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void timeout()
    {
        m_network->setTimeoutMs(300);
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Timeout);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("MyMemory did not respond within 0.3 s"));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void connectionRefusedIsNetwork()
    {
        m_server->close();
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Network);
        // describeNetworkError(request, false), never QNetworkReply::errorString():
        // that would contain the host, the port and the text.
        QCOMPARE(reply->result().errorMessage, QStringLiteral("Network error"));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void errorMessageOmitsTextAndUrl_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");

        QTest::newRow("503") << 503 << QByteArray("blocked");
        QTest::newRow("html") << 200
                              << QByteArray("<!DOCTYPE html><html><body>blocked</body></html>");
    }

    void errorMessageOmitsTextAndUrl()
    {
        QFETCH(int, status);
        QFETCH(QByteArray, body);

        const int responseStatus = status;
        const QByteArray responseBody = body;
        m_server->handler = [responseStatus, responseBody](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{responseStatus, responseBody, true};
        };
        std::unique_ptr<TranslationReply> reply(
            m_service->translate({QStringLiteral("TOPSECRET"), QStringLiteral("en"),
                                  QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY(!reply->result().ok());
        QCOMPARE(queryValue(m_server->requests.first().url, QStringLiteral("q")),
                 QStringLiteral("TOPSECRET"));
        verifyFixedMessage(reply->result().errorMessage);
    }

private:
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<MyMemoryService> m_service;
};

QTEST_GUILESS_MAIN(TestMyMemoryService)
#include "tst_mymemoryservice.moc"
