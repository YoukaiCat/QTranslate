#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/mozhiservice.h"

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
        QByteArray location; // Location header; empty for a normal response

        Response() = default;
        Response(int responseStatus, QByteArray responseBody, bool responseRespond = true,
                 QByteArray responseLocation = {})
            : status(responseStatus)
            , body(std::move(responseBody))
            , respond(responseRespond)
            , location(std::move(responseLocation))
        {
        }
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
        QByteArray head = "HTTP/1.1 " + QByteArray::number(response.status) + " Status\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
        if (!response.location.isEmpty())
            head += "Location: " + response.location + "\r\n";
        head += "Connection: close\r\n\r\n";
        socket->write(head + response.body);
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

QByteArray translationResponse(const QString &text, const QString &detected = QString(),
                               const QString &source = QStringLiteral("en"))
{
    QJsonObject root;
    root.insert(QStringLiteral("engine"), QStringLiteral("google"));
    root.insert(QStringLiteral("detected"), detected);
    root.insert(QStringLiteral("translated-text"), text);
    root.insert(QStringLiteral("source_language"), source);
    root.insert(QStringLiteral("target_language"), QStringLiteral("ru"));
    root.insert(QStringLiteral("target_transliteration"), QString());
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// Answers every chunk with its own text, as a translation would.
QByteArray echoResponse(const QUrl &url)
{
    return translationResponse(queryValue(url, QStringLiteral("text")));
}

// Cyrillic, paragraphs and emoji: several 5000-unit chunks with edge
// whitespace.
QString longText()
{
    QString text = QStringLiteral("  ");
    int sentence = 0;
    while (text.size() < 12000) {
        text += QStringLiteral("Предложение номер %1 рассказывает о погоде, природе и 😀.  ")
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

class TestMozhiService : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
        m_network = std::make_unique<NetworkManager>();
        m_service = std::make_unique<MozhiService>(m_network.get());
        m_service->setInstance(
            QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server->port())));
        m_service->setEngine(QStringLiteral("google"));
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
        QCOMPARE(m_service->id(), QStringLiteral("mozhi"));
        QCOMPARE(m_service->displayName(), QStringLiteral("Mozhi"));
        QVERIFY(m_service->isConfigured());
        QVERIFY(m_service->requiresOptIn());
        QVERIFY(m_service->sourceLanguages().contains(QStringLiteral("ru")));
        QVERIFY(m_service->targetLanguages().contains(QStringLiteral("zh-Hans")));
        QVERIFY(!m_service->sourceLanguages().contains(QStringLiteral("auto")));
        QVERIFY(!m_service->targetLanguages().contains(QStringLiteral("auto")));
        QCOMPARE(MozhiService::defaultInstance(),
                 QUrl(QStringLiteral("https://mozhi.adminforge.de")));
        QCOMPARE(MozhiService::defaultEngine(), QStringLiteral("google"));
    }

    void defaults()
    {
        MozhiService service(m_network.get());
        QCOMPARE(service.rateLimitMs(), 500);
        QCOMPARE(service.instance(), QUrl(QStringLiteral("https://mozhi.adminforge.de")));
        QCOMPARE(service.engine(), QStringLiteral("google"));
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
        QCOMPARE(reply->result().serviceId, QStringLiteral("mozhi"));

        QCOMPARE(m_server->requests.size(), 1);
        const FakeHttpServer::Request &request = m_server->requests.first();
        QCOMPARE(request.method, QByteArray("GET"));
        QCOMPARE(request.url.path(), QStringLiteral("/api/translate"));
        QCOMPARE(queryValue(request.url, QStringLiteral("engine")), QStringLiteral("google"));
        QCOMPARE(queryValue(request.url, QStringLiteral("from")), QStringLiteral("en"));
        QCOMPARE(queryValue(request.url, QStringLiteral("to")), QStringLiteral("ru"));
        QCOMPARE(queryValue(request.url, QStringLiteral("text")), QStringLiteral("Hello, world"));
        QCOMPARE(request.headers.value("accept"), QByteArray("application/json"));
        QVERIFY(!request.headers.contains("authorization"));
    }

    void usesInstancePathPrefix()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.url), true};
        };
        m_service->setInstance(
            QUrl(QStringLiteral("http://127.0.0.1:%1/mozhi/").arg(m_server->port())));
        std::unique_ptr<TranslationReply> prefixed(m_service->translate(
            {QStringLiteral("One"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy prefixedFinished(prefixed.get(), &TranslationReply::finished);
        QVERIFY(prefixedFinished.wait(5000));
        QVERIFY2(prefixed->result().ok(), qPrintable(prefixed->result().errorMessage));
        QCOMPARE(m_server->requests.at(0).url.path(), QStringLiteral("/mozhi/api/translate"));

        m_service->setInstance(
            QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server->port())));
        std::unique_ptr<TranslationReply> origin(m_service->translate(
            {QStringLiteral("Two"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy originFinished(origin.get(), &TranslationReply::finished);
        QVERIFY(originFinished.wait(5000));
        QVERIFY2(origin->result().ok(), qPrintable(origin->result().errorMessage));
        QCOMPARE(m_server->requests.at(1).url.path(), QStringLiteral("/api/translate"));
    }

    void encodesQuery()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.url), true};
        };
        const QString text = QStringLiteral("Hello + мир 😀 C++ 1+1");
        std::unique_ptr<TranslationReply> reply(
            m_service->translate({text, QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        const QUrl url = m_server->requests.first().url;
        QCOMPARE(queryValue(url, QStringLiteral("text")), text);
        const QString raw = url.query(QUrl::FullyEncoded);
        QVERIFY2(!raw.contains(QLatin1Char(' ')), qPrintable(raw));
        // A literal '+' would reach a form-decoding server as a space.
        QVERIFY2(!raw.contains(QLatin1Char('+')), qPrintable(raw));
        QVERIFY2(raw.contains(QStringLiteral("%20")), qPrintable(raw));
        QVERIFY2(raw.contains(QStringLiteral("%2B")), qPrintable(raw));
        QVERIFY2(raw.contains(QStringLiteral("%D0")), qPrintable(raw)); // Cyrillic
        QVERIFY2(raw.contains(QStringLiteral("%F0%9F")), qPrintable(raw)); // emoji
    }

    void changesEngine()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.url), true};
        };
        m_service->setEngine(QStringLiteral("deepl"));
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(queryValue(m_server->requests.first().url, QStringLiteral("engine")),
                 QStringLiteral("deepl"));
    }

    void mapsLanguageCodes_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("to");

        QTest::newRow("Portuguese Brazil") << "ru" << "pt-BR" << "ru" << "pt";
        QTest::newRow("Chinese simplified") << "en" << "zh-Hans" << "en" << "zh-CN";
        QTest::newRow("Chinese traditional") << "en" << "zh-Hant" << "en" << "zh-TW";
        QTest::newRow("Hebrew") << "he" << "en" << "iw" << "en";
        QTest::newRow("Javanese") << "jv" << "en" << "jw" << "en";
        QTest::newRow("Norwegian") << "nb" << "en" << "no" << "en";
        QTest::newRow("Filipino") << "fil" << "en" << "tl" << "en";
    }

    void mapsLanguageCodes()
    {
        QFETCH(QString, source);
        QFETCH(QString, target);
        QFETCH(QString, from);
        QFETCH(QString, to);

        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.url), true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), source, target}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(queryValue(m_server->requests.first().url, QStringLiteral("from")), from);
        QCOMPARE(queryValue(m_server->requests.first().url, QStringLiteral("to")), to);
    }

    void autoSourceIsSent()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200, translationResponse(QStringLiteral("Hello"), QStringLiteral("en"),
                                         QStringLiteral("auto")),
                true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(queryValue(m_server->requests.first().url, QStringLiteral("from")),
                 QStringLiteral("auto"));
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("en"));

        // detected is empty, but source_language tells the language.
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hello")),
                                            true};
        };
        std::unique_ptr<TranslationReply> fallback(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")}));
        QSignalSpy fallbackFinished(fallback.get(), &TranslationReply::finished);
        QVERIFY(fallbackFinished.wait(5000));
        QVERIFY2(fallback->result().ok(), qPrintable(fallback->result().errorMessage));
        QCOMPARE(fallback->result().detectedSourceLang, QStringLiteral("en"));
    }

    void autoRejectedByEndpoint()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{500, "Source language code invalid", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::UnsupportedLanguage);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("the language pair is not supported"));
        QCOMPARE(m_server->requests.size(), 1);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void unknownLanguageIsRejected()
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
        QCOMPARE(queryValue(m_server->requests.first().url, QStringLiteral("text")),
                 QStringLiteral("Hello"));
        QCOMPARE(reply->result().translatedText, QStringLiteral(" \tX\r\n "));
    }

    void longTextIsSplitIntoChunks()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.url), true};
        };
        const QString text = longText();
        const QStringList expectedParts = TextChunker::split(text, 5000);
        QVERIFY(expectedParts.size() > 2);
        // A chunk boundary must never split a "\r\n" or a surrogate pair.
        for (qsizetype i = 0; i + 1 < expectedParts.size(); ++i) {
            const QString &part = expectedParts.at(i);
            const QString &next = expectedParts.at(i + 1);
            QVERIFY2(!(part.endsWith(QLatin1Char('\r'))
                       && next.startsWith(QLatin1Char('\n'))),
                     "a chunk boundary split \\r\\n");
            QVERIFY2(!(part.at(part.size() - 1).isHighSurrogate()
                       && next.at(0).isLowSurrogate()),
                     "a chunk boundary split a surrogate pair");
        }

        std::unique_ptr<TranslationReply> reply(
            m_service->translate({text, QStringLiteral("ru"), QStringLiteral("en")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, text);

        QStringList sentParts;
        for (const QString &part : expectedParts) {
            if (!part.trimmed().isEmpty())
                sentParts << part.trimmed();
        }
        QCOMPARE(m_server->requests.size(), sentParts.size());
        for (qsizetype i = 0; i < m_server->requests.size(); ++i) {
            const QUrl url = m_server->requests.at(i).url;
            const QString sent = queryValue(url, QStringLiteral("text"));
            QCOMPARE(sent, sentParts.at(i));
            QVERIFY2(sent.size() <= 5000,
                     qPrintable(QStringLiteral("%1 UTF-16 units").arg(sent.size())));
            QVERIFY(!sent.at(0).isLowSurrogate());
            QVERIFY(!sent.at(sent.size() - 1).isHighSurrogate());
        }
    }

    void failedChunkFailsTheWholeReply()
    {
        auto calls = std::make_shared<int>(0);
        m_server->handler = [calls](const FakeHttpServer::Request &request) {
            if (++*calls == 1)
                return FakeHttpServer::Response{200, echoResponse(request.url), true};
            return FakeHttpServer::Response{500, "instance has been rate limited", true};
        };
        const QString text = longText();
        QVERIFY(TextChunker::split(text, 5000).size() > 2);

        std::unique_ptr<TranslationReply> reply(
            m_service->translate({text, QStringLiteral("ru"), QStringLiteral("en")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QCOMPARE(reply->result().error, TranslationError::RateLimited);
        QVERIFY(reply->result().translatedText.isEmpty());
        QCOMPARE(m_server->requests.size(), 2);
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 2);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void pacesChunks()
    {
        m_service->setRateLimitMs(100);
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.url), true};
        };
        const QString text = longText();
        QVERIFY(TextChunker::split(text, 5000).size() > 2);

        std::unique_ptr<TranslationReply> reply(
            m_service->translate({text, QStringLiteral("ru"), QStringLiteral("en")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(m_server->requests.size(), TextChunker::split(text, 5000).size());
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
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("429 rate limit") << 429 << QByteArray() << TranslationError::RateLimited;
        QTest::newRow("403 forbidden") << 403 << QByteArray() << TranslationError::RateLimited;
        QTest::newRow("503 unavailable") << 503 << QByteArray() << TranslationError::Network;
        QTest::newRow("414 uri too long") << 414 << QByteArray() << TranslationError::TooLong;
        QTest::newRow("400 required fields")
            << 400 << QByteArray("from, to, engine, text are required query strings.")
            << TranslationError::BadResponse;
        QTest::newRow("500 invalid language")
            << 500 << QByteArray("Target language code invalid")
            << TranslationError::UnsupportedLanguage;
        QTest::newRow("500 rate limit body")
            << 500 << QByteArray("instance has been rate limited")
            << TranslationError::RateLimited;
        QTest::newRow("500 status code 429")
            << 500 << QByteArray("request failed with status code: 429")
            << TranslationError::RateLimited;
        QTest::newRow("500 unknown engine")
            << 500 << QByteArray("Engine does not exist or has been disabled.")
            << TranslationError::ServiceUnavailable;
    }

    void httpErrors()
    {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(TranslationError, error);

        const int responseStatus = status;
        const QByteArray responseBody = body;
        m_server->handler = [responseStatus, responseBody](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{responseStatus, responseBody, true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, error);
        QVERIFY(reply->result().translatedText.isEmpty());
        verifyFixedMessage(reply->result().errorMessage);
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

    void jsonError()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200, R"({"error":"Engine does not exist or has been disabled."})", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::ServiceUnavailable);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("the selected engine is not available on this instance"));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void malformedJson()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, R"({"translated-text":)", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::BadResponse);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void missingTranslatedText_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("missing") << QByteArray(R"({"detected":"en","source_language":"en"})");
        QTest::newRow("empty") << QByteArray(R"({"translated-text":"  "})");
        QTest::newRow("not a string") << QByteArray(R"({"translated-text":42})");
    }

    void missingTranslatedText()
    {
        QFETCH(QByteArray, body);

        const QByteArray responseBody = body;
        m_server->handler = [responseBody](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, responseBody, true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::BadResponse);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("no translated text in the response"));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void rateLimitBody()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, "instance has been rate limited", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::RateLimited);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("the endpoint reported a rate limit"));
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
                 QStringLiteral("Mozhi did not respond within 0.3 s"));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void closedServerIsNetwork()
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

    void crossOriginRedirectIsNotFollowed()
    {
        FakeHttpServer other;
        const quint16 otherPort = other.port();
        m_server->handler = [otherPort](const FakeHttpServer::Request &) {
            FakeHttpServer::Response response;
            response.status = 302;
            response.location = QByteArray("http://127.0.0.1:")
                + QByteArray::number(otherPort) + "/api/translate";
            return response;
        };
        std::unique_ptr<TranslationReply> reply(
            m_service->translate({QStringLiteral("TOPSECRET"), QStringLiteral("en"),
                                  QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY(!reply->result().ok());
        QVERIFY(reply->result().translatedText.isEmpty());
        QTest::qWait(100);
        QCOMPARE(other.requests.size(), 0);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void errorMessageOmitsTextAndUrl_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");

        QTest::newRow("503") << 503 << QByteArray("blocked");
        QTest::newRow("html") << 200
                              << QByteArray("<!DOCTYPE html><html><body>blocked</body></html>");
        QTest::newRow("rate limit") << 200
                                    << QByteArray("instance has been rate limited TOPSECRET");
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
        QCOMPARE(queryValue(m_server->requests.first().url, QStringLiteral("text")),
                 QStringLiteral("TOPSECRET"));
        verifyFixedMessage(reply->result().errorMessage);
    }

private:
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<MozhiService> m_service;
};

QTEST_GUILESS_MAIN(TestMozhiService)
#include "tst_mozhiwebservice.moc"
