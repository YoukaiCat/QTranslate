#include "core/appinfo.h"
#include "core/languages.h"
#include "core/settings.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/reversoservice.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <functional>
#include <memory>
#include <utility>

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

// The flat success object of doc/free-endpoints.md, §4.
QByteArray translationResponse(const QString &text, const QString &from = QString(),
                               const QString &to = QString())
{
    QJsonObject root;
    root.insert(QStringLiteral("translation"), text);
    root.insert(QStringLiteral("directionFrom"), from);
    root.insert(QStringLiteral("directionTo"), to);
    root.insert(QStringLiteral("success"), true);
    root.insert(QStringLiteral("message"), QString());
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray providerErrorBody(const QString &message)
{
    QJsonObject root;
    root.insert(QStringLiteral("success"), false);
    root.insert(QStringLiteral("message"), message);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// Answers a chunk with its own text, split back from the JSON body.
QByteArray echoResponse(const FakeHttpServer::Request &request)
{
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    const QString text = body.value(QStringLiteral("source")).toString();
    const QString direction = body.value(QStringLiteral("direction")).toString();
    const qsizetype dash = direction.indexOf(QLatin1Char('-'));
    return translationResponse(text, direction.left(dash), direction.mid(dash + 1));
}

QJsonObject requestBody(const FakeHttpServer::Request &request)
{
    return QJsonDocument::fromJson(request.body).object();
}

// Cyrillic, CRLF paragraph breaks and emoji: several 5000-unit chunks with
// edge whitespace.
QString longText()
{
    QString text = QStringLiteral("  ");
    int sentence = 0;
    while (text.size() < 12000) {
        text += QStringLiteral("Предложение номер %1 рассказывает о погоде, природе и 😀.  ")
                    .arg(++sentence);
        if (sentence % 5 == 0)
            text += QStringLiteral("\r\n\r\n");
    }
    return text;
}

// Error messages are fixed: neither the text, the body, nor the URL may leak
// into them.
void verifyFixedMessage(const QString &message)
{
    QVERIFY(!message.isEmpty());
    QVERIFY2(!message.contains(QStringLiteral("TOPSECRET")), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('<')), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('{')), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('}')), qPrintable(message));
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
            QStringLiteral("qtranslate.service.reverso.debug=true"));
    }

    ~LogCapture()
    {
        QLoggingCategory::setFilterRules(QString());
        qInstallMessageHandler(previous);
    }
};

} // namespace

class TestReversoService : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
        m_network = std::make_unique<NetworkManager>();
        m_service = std::make_unique<ReversoService>(m_network.get());
        m_service->setEndpoint(
            QUrl(QStringLiteral("http://127.0.0.1:%1/api2/TranslateText").arg(m_server->port())));
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
        QCOMPARE(m_service->id(), QStringLiteral("reverso"));
        QCOMPARE(m_service->displayName(), QStringLiteral("Reverso"));
        QVERIFY(m_service->isConfigured());
        QVERIFY(m_service->requiresOptIn());

        const QStringList sources = m_service->sourceLanguages();
        const QStringList targets = m_service->targetLanguages();
        QCOMPARE(sources, Languages::supported(QStringLiteral("reverso"),
                                               Languages::Direction::Source));
        QCOMPARE(targets, Languages::supported(QStringLiteral("reverso"),
                                               Languages::Direction::Target));
        QCOMPARE(int(sources.size()), 6);
        QCOMPARE(int(targets.size()), 6);
        QVERIFY(!sources.contains(Languages::autoCode()));
        QVERIFY(!targets.contains(Languages::autoCode()));
        for (const QString &code : {QStringLiteral("en"), QStringLiteral("fr"),
                                    QStringLiteral("de"), QStringLiteral("es"),
                                    QStringLiteral("it"), QStringLiteral("ru")}) {
            QVERIFY2(sources.contains(code), qPrintable(code));
            QVERIFY2(targets.contains(code), qPrintable(code));
        }
    }

    void defaults()
    {
        ReversoService service(m_network.get());
        QCOMPARE(service.rateLimitMs(), 600);
        QCOMPARE(service.endpoint(), ReversoService::defaultEndpoint());
        QCOMPARE(ReversoService::defaultEndpoint(),
                 QUrl(QStringLiteral("https://cps.reverso.net/api2/TranslateText")));

        service.setEndpoint(QUrl());
        QCOMPARE(service.endpoint(), ReversoService::defaultEndpoint());

        const QUrl invalid(QStringLiteral("http://exa mple.com/api2/TranslateText"));
        QVERIFY(!invalid.isValid());
        service.setEndpoint(invalid);
        QCOMPARE(service.endpoint(), ReversoService::defaultEndpoint());

        const QUrl local(QStringLiteral("http://127.0.0.1:1/api2/TranslateText"));
        service.setEndpoint(local);
        QCOMPARE(service.endpoint(), local);

        service.applySettings(AppSettings{});
        QCOMPARE(service.rateLimitMs(), 600);
        QCOMPARE(service.endpoint(), local);
    }

    void postsJson()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, QByteArray("{}"), true};
            return FakeHttpServer::Response{200, echoResponse(request), true};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hello"));
        QCOMPARE(reply->result().serviceId, QStringLiteral("reverso"));
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("en"));

        QCOMPARE(m_server->requests.size(), 1);
        const FakeHttpServer::Request &request = m_server->requests.first();
        QCOMPARE(request.method, QByteArray("POST"));
        QCOMPARE(request.url.path(), QStringLiteral("/api2/TranslateText"));
        QCOMPARE(request.headers.value("content-type"), QByteArray("application/json"));
        QCOMPARE(request.headers.value("accept"), QByteArray("application/json"));
        QCOMPARE(request.headers.value("user-agent"),
                 ReversoService::browserUserAgent().toUtf8());
        QVERIFY(ReversoService::browserUserAgent() != AppInfo::userAgent());

        const QJsonObject body = requestBody(request);
        QCOMPARE(int(body.size()), 7);
        const QJsonValue source = body.value(QStringLiteral("source"));
        QVERIFY(source.isString());
        QCOMPARE(source.toString(), QStringLiteral("Hello"));
        const QJsonValue direction = body.value(QStringLiteral("direction"));
        QVERIFY(direction.isString());
        QCOMPARE(direction.toString(), QStringLiteral("en-ru"));
        for (const QString &key : {QStringLiteral("deviceId"), QStringLiteral("uiLang"),
                                   QStringLiteral("origin"), QStringLiteral("accessToken"),
                                   QStringLiteral("appId")}) {
            QVERIFY2(body.value(key).isString(), qPrintable(key));
        }
        QCOMPARE(body.value(QStringLiteral("deviceId")).toString(), QStringLiteral("0"));
        QCOMPARE(body.value(QStringLiteral("uiLang")).toString(), QStringLiteral("en"));
        QCOMPARE(body.value(QStringLiteral("origin")).toString(),
                 QStringLiteral("chromeextension"));
        QCOMPARE(body.value(QStringLiteral("accessToken")).toString(), QString());
        QCOMPARE(body.value(QStringLiteral("appId")).toString(), QStringLiteral("0"));

        // A plain makeRequest() to the same server still sends the application
        // User-Agent: the browser override is on this service's request only.
        QNetworkReply *plain = m_network->nam()->get(
            m_network->makeRequest(
                QUrl(QStringLiteral("http://127.0.0.1:%1/plain").arg(m_server->port()))));
        QSignalSpy plainFinished(plain, &QNetworkReply::finished);
        QVERIFY(plainFinished.wait(5000));
        plain->deleteLater();
        QCOMPARE(m_server->requests.size(), 2);
        QCOMPARE(m_server->requests.at(1).method, QByteArray("GET"));
        QCOMPARE(m_server->requests.at(1).headers.value("user-agent"),
                 AppInfo::userAgent().toUtf8());
    }

    void utf8Body()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request), true};
        };

        const QString text = QStringLiteral("Café — Москва 😀");
        std::unique_ptr<TranslationReply> reply = translate(text);
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, text);

        QCOMPARE(m_server->requests.size(), 1);
        const FakeHttpServer::Request &request = m_server->requests.first();
        QCOMPARE(requestBody(request).value(QStringLiteral("source")).toString(), text);
        // Raw UTF-8 inside the JSON string: no \u escapes and ordinary spaces.
        QVERIFY2(request.body.contains(text.toUtf8()), qPrintable(request.body.left(500)));
        QVERIFY2(!request.body.contains("\\u"), qPrintable(request.body.left(500)));
        QVERIFY2(request.body.contains(QStringLiteral("Москва").toUtf8()),
                 qPrintable(request.body.left(500)));
    }

    void allDirections_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("target");

        const QStringList languages = {
            QStringLiteral("en"), QStringLiteral("fr"), QStringLiteral("de"),
            QStringLiteral("es"), QStringLiteral("it"), QStringLiteral("ru"),
        };
        for (const QString &source : languages) {
            for (const QString &target : languages) {
                const QByteArray name = (source + QLatin1Char('-') + target).toLatin1();
                QTest::newRow(name.constData()) << source << target;
            }
        }
    }

    void allDirections()
    {
        QFETCH(QString, source);
        QFETCH(QString, target);

        const QString mappedSource =
            Languages::toServiceCode(QStringLiteral("reverso"), source,
                                     Languages::Direction::Source);
        const QString mappedTarget =
            Languages::toServiceCode(QStringLiteral("reverso"), target,
                                     Languages::Direction::Target);
        QVERIFY(!mappedSource.isEmpty());
        QVERIFY(!mappedTarget.isEmpty());

        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request), true};
        };
        std::unique_ptr<TranslationReply> reply =
            translate(QStringLiteral("Hello"), source, target);
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hello"));
        QCOMPARE(m_server->requests.size(), 1);
        const QJsonObject body = requestBody(m_server->requests.first());
        QCOMPARE(body.value(QStringLiteral("direction")).toString(),
                 mappedSource + QLatin1Char('-') + mappedTarget);
        QCOMPARE(body.value(QStringLiteral("source")).toString(), QStringLiteral("Hello"));
    }

    void rejectsAuto()
    {
        std::unique_ptr<TranslationReply> reply =
            translate(QStringLiteral("Hello"), Languages::autoCode(), QStringLiteral("ru"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::UnsupportedLanguage);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("Reverso needs an explicit source language; choose one instead of "
                                "Auto-detect"));
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 0);
    }

    void rejectsUnmappedLanguage()
    {
        std::unique_ptr<TranslationReply> sourceReply =
            translate(QStringLiteral("Hello"), QStringLiteral("xx"), QStringLiteral("ru"));
        QSignalSpy sourceFinished(sourceReply.get(), &TranslationReply::finished);
        QVERIFY(sourceFinished.wait(1000));
        QCOMPARE(sourceReply->result().error, TranslationError::UnsupportedLanguage);
        QCOMPARE(sourceReply->result().errorMessage,
                 QStringLiteral("language \"xx\" is not supported"));

        std::unique_ptr<TranslationReply> targetReply =
            translate(QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("uk"));
        QSignalSpy targetFinished(targetReply.get(), &TranslationReply::finished);
        QVERIFY(targetFinished.wait(1000));
        QCOMPARE(targetReply->result().error, TranslationError::UnsupportedLanguage);
        QCOMPARE(targetReply->result().errorMessage,
                 QStringLiteral("language \"uk\" is not supported"));

        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 0);
    }

    void emptyText_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("empty") << QString();
        QTest::newRow("whitespace") << QStringLiteral(" \n\t ");
    }

    void emptyText()
    {
        QFETCH(QString, text);

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("en"), QStringLiteral("ru"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::EmptyText);
        QCOMPARE(reply->result().serviceId, QStringLiteral("reverso"));
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 0);
    }

    void edgeWhitespace()
    {
        // The endpoint receives the text without its edge whitespace while the
        // result keeps those edges exactly as the user wrote them.
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("X")), true};
        };
        std::unique_ptr<TranslationReply> reply =
            translate(QStringLiteral(" \tHello\r\n "));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(m_server->requests.size(), 1);
        QCOMPARE(requestBody(m_server->requests.first()).value(QStringLiteral("source")).toString(),
                 QStringLiteral("Hello"));
        QCOMPARE(reply->result().translatedText, QStringLiteral(" \tX\r\n "));
        m_server->requests.clear();

        // A whitespace-only chunk in the middle is never sent and is restored
        // when the parts are joined.
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request), true};
        };
        const QString text = QString(4901, QLatin1Char('A')) + QString(6000, QLatin1Char(' '))
            + QString(6000, QLatin1Char('B'));
        const QStringList expectedParts = TextChunker::split(text, 5000);
        qsizetype nonBlankParts = 0;
        for (const QString &part : expectedParts) {
            if (!part.trimmed().isEmpty())
                ++nonBlankParts;
        }
        QVERIFY(nonBlankParts < expectedParts.size());
        QVERIFY(nonBlankParts > 1);

        std::unique_ptr<TranslationReply> longReply =
            translate(text, QStringLiteral("en"), QStringLiteral("ru"));
        QSignalSpy longFinished(longReply.get(), &TranslationReply::finished);
        QVERIFY(longFinished.wait(30000));
        QVERIFY2(longReply->result().ok(), qPrintable(longReply->result().errorMessage));
        QCOMPARE(longReply->result().translatedText, text);
        QCOMPARE(m_server->requests.size(), nonBlankParts);
        for (const FakeHttpServer::Request &request : std::as_const(m_server->requests)) {
            QVERIFY(requestBody(request).value(QStringLiteral("source")).toString().trimmed()
                    == requestBody(request).value(QStringLiteral("source")).toString());
            QVERIFY(!requestBody(request).value(QStringLiteral("source")).toString().isEmpty());
        }
    }

    void chunks()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request), true};
        };
        const QString text = longText();
        const QStringList expectedParts = TextChunker::split(text, 5000);
        QVERIFY(expectedParts.size() > 2);
        // The fixture must really contain CRLF breaks, so the boundary check
        // below cannot silently become tautological.
        QVERIFY(text.contains(QStringLiteral("\r\n")));
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

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("ru"), QStringLiteral("en"));
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
            const QString sent =
                requestBody(m_server->requests.at(i)).value(QStringLiteral("source")).toString();
            QCOMPARE(sent, sentParts.at(i));
            QVERIFY2(sent.size() <= 5000,
                     qPrintable(QStringLiteral("%1 UTF-16 units").arg(sent.size())));
            QVERIFY(!sent.at(0).isLowSurrogate());
            QVERIFY(!sent.at(sent.size() - 1).isHighSurrogate());
        }
    }

    void pacesChunks()
    {
        m_service->setRateLimitMs(100);
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request), true};
        };
        const QString text = longText();
        QVERIFY(TextChunker::split(text, 5000).size() > 2);

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("ru"), QStringLiteral("en"));
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
            return FakeHttpServer::Response{200, echoResponse(request), true};
        };

        std::unique_ptr<TranslationReply> first =
            translate(QStringLiteral("One"), QStringLiteral("en"), QStringLiteral("ru"));
        std::unique_ptr<TranslationReply> second =
            translate(QStringLiteral("Two"), QStringLiteral("en"), QStringLiteral("ru"));
        QSignalSpy firstFinished(first.get(), &TranslationReply::finished);
        QSignalSpy secondFinished(second.get(), &TranslationReply::finished);
        QVERIFY(firstFinished.wait(5000));
        QVERIFY(secondFinished.wait(5000));
        QVERIFY2(first->result().ok(), qPrintable(first->result().errorMessage));
        QVERIFY2(second->result().ok(), qPrintable(second->result().errorMessage));
        QCOMPARE(m_server->requests.size(), 2);

        const qint64 delta =
            m_server->requests.at(1).elapsedMs - m_server->requests.at(0).elapsedMs;
        QVERIFY2(delta >= 180, qPrintable(QStringLiteral("delta %1 ms").arg(delta)));
    }

    void cancelsQueuedRequest()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };

        std::unique_ptr<TranslationReply> first =
            translate(QStringLiteral("One"), QStringLiteral("en"), QStringLiteral("ru"));
        QTRY_VERIFY_WITH_TIMEOUT(m_server->requests.size() == 1, 5000);

        std::unique_ptr<TranslationReply> second =
            translate(QStringLiteral("Two"), QStringLiteral("en"), QStringLiteral("ru"));
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

        std::unique_ptr<TranslationReply> reply =
            translate(QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru"));
        QTRY_VERIFY_WITH_TIMEOUT(m_server->requests.size() == 1, 5000);
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        reply->abort();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(reply->result().error, TranslationError::Cancelled);
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 1);
    }

    void failedMiddleChunk()
    {
        auto calls = std::make_shared<int>(0);
        m_server->handler = [calls](const FakeHttpServer::Request &request) {
            if (++*calls == 1)
                return FakeHttpServer::Response{200, echoResponse(request), true};
            return FakeHttpServer::Response{429, QByteArray(), true};
        };
        const QString text = longText();
        QVERIFY(TextChunker::split(text, 5000).size() > 2);

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("ru"), QStringLiteral("en"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QCOMPARE(reply->result().error, TranslationError::RateLimited);
        QVERIFY(reply->result().translatedText.isEmpty());
        QCOMPARE(m_server->requests.size(), 2);
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 2);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void httpErrors_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("429 rate limit") << 429 << QByteArray() << TranslationError::RateLimited;
        QTest::newRow("403 forbidden") << 403 << QByteArray() << TranslationError::RateLimited;
        QTest::newRow("503 unavailable") << 503 << QByteArray() << TranslationError::Network;
        QTest::newRow("413 too long") << 413 << QByteArray() << TranslationError::TooLong;
        QTest::newRow("400 bad request") << 400 << QByteArray() << TranslationError::BadResponse;
        QTest::newRow("200 provider error")
            << 200 << QByteArray(R"({"success":false})") << TranslationError::ServiceUnavailable;
        QTest::newRow("200 html page")
            << 200 << QByteArray("<!DOCTYPE html><html><body>blocked</body></html>")
            << TranslationError::ServiceUnavailable;
        QTest::newRow("200 rate limit text")
            << 200 << QByteArray("Too many requests") << TranslationError::RateLimited;
        QTest::newRow("200 malformed body")
            << 200 << QByteArray(R"({"translation":)") << TranslationError::BadResponse;
        QTest::newRow("200 empty body") << 200 << QByteArray() << TranslationError::BadResponse;
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
        std::unique_ptr<TranslationReply> reply =
            translate(QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, error);
        QVERIFY(reply->result().translatedText.isEmpty());
        verifyFixedMessage(reply->result().errorMessage);
    }

    void providerMessage()
    {
        const QString message = QStringLiteral("the service is temporarily busy");
        const QByteArray body = providerErrorBody(message);
        m_server->handler = [body](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, body, true};
        };
        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::ServiceUnavailable);
        QCOMPARE(reply->result().errorMessage, message);
        QVERIFY(reply->result().translatedText.isEmpty());

        // Anything that is not a short plain sentence falls back to the fixed
        // literal.
        const QStringList invalid = {
            QStringLiteral("<html>blocked</html>"),
            QStringLiteral("see https://example.invalid/blocked"),
            QStringLiteral("bad\x07message"),
            QString(201, QLatin1Char('a')),
        };
        for (const QString &invalidMessage : invalid) {
            const QByteArray invalidBody = providerErrorBody(invalidMessage);
            m_server->handler = [invalidBody](const FakeHttpServer::Request &) {
                return FakeHttpServer::Response{200, invalidBody, true};
            };
            std::unique_ptr<TranslationReply> invalidReply = translate(QStringLiteral("Hello"));
            QSignalSpy invalidFinished(invalidReply.get(), &TranslationReply::finished);
            QVERIFY(invalidFinished.wait(5000));
            QCOMPARE(invalidReply->result().error, TranslationError::ServiceUnavailable);
            QCOMPARE(invalidReply->result().errorMessage,
                     QStringLiteral("the endpoint reported an error"));
            verifyFixedMessage(invalidReply->result().errorMessage);
        }

        // An object that is an error and also claims success takes the error
        // path and never returns the translation.
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200,
                QByteArray(R"({"error":true,"success":true,"translation":"TOPSECRET"})"), true};
        };
        std::unique_ptr<TranslationReply> conflicting =
            translate(QStringLiteral("Hello"));
        QSignalSpy conflictingFinished(conflicting.get(), &TranslationReply::finished);
        QVERIFY(conflictingFinished.wait(5000));
        QCOMPARE(conflicting->result().error, TranslationError::ServiceUnavailable);
        QVERIFY(conflicting->result().translatedText.isEmpty());
        verifyFixedMessage(conflicting->result().errorMessage);
    }

    void timeout()
    {
        m_network->setTimeoutMs(300);
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };
        std::unique_ptr<TranslationReply> reply =
            translate(QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Timeout);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("Reverso did not respond within 0.3 s"));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void closedServerIsNetwork()
    {
        m_server->close();
        std::unique_ptr<TranslationReply> reply =
            translate(QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru"));
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
                + QByteArray::number(otherPort) + "/api2/TranslateText";
            return response;
        };
        std::unique_ptr<TranslationReply> reply =
            translate(QStringLiteral("TOPSECRET"), QStringLiteral("en"), QStringLiteral("ru"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY(!reply->result().ok());
        QVERIFY(reply->result().translatedText.isEmpty());
        QTest::qWait(100);
        QCOMPARE(other.requests.size(), 0);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void logsAreClean()
    {
        const QString providerMessage = QStringLiteral("TOPSECRET provider message");
        const QByteArray errorBody = providerErrorBody(providerMessage);
        auto calls = std::make_shared<int>(0);
        m_server->handler = [calls, errorBody](const FakeHttpServer::Request &request) {
            if (++*calls == 1)
                return FakeHttpServer::Response{200, echoResponse(request), true};
            return FakeHttpServer::Response{200, errorBody, true};
        };

        {
            LogCapture capture;

            std::unique_ptr<TranslationReply> success = translate(QStringLiteral("TOPSECRET"));
            QSignalSpy successFinished(success.get(), &TranslationReply::finished);
            QVERIFY(successFinished.wait(5000));
            QVERIFY2(success->result().ok(), qPrintable(success->result().errorMessage));

            std::unique_ptr<TranslationReply> failure = translate(QStringLiteral("TOPSECRET"));
            QSignalSpy failureFinished(failure.get(), &TranslationReply::finished);
            QVERIFY(failureFinished.wait(5000));
            QCOMPARE(failure->result().error, TranslationError::ServiceUnavailable);
            QCOMPARE(failure->result().errorMessage, providerMessage);
        }

        QVERIFY(!g_logMessages.isEmpty());
        for (const QString &message : std::as_const(g_logMessages)) {
            QVERIFY2(!message.contains(QStringLiteral("TOPSECRET")), qPrintable(message));
            QVERIFY2(!message.contains(providerMessage), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("<html")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("127.0.0.1")), qPrintable(message));
        }
    }

private:
    std::unique_ptr<TranslationReply> translate(
        const QString &text, const QString &source = QStringLiteral("en"),
        const QString &target = QStringLiteral("ru"))
    {
        return std::unique_ptr<TranslationReply>(
            m_service->translate({text, source, target}));
    }

    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<ReversoService> m_service;
};

QTEST_GUILESS_MAIN(TestReversoService)
#include "tst_reversoservice.moc"
