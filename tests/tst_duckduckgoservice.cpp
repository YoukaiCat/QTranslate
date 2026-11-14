#include "core/appinfo.h"
#include "core/languages.h"
#include "core/settings.h"
#include "core/textchunker.h"
#include "core/translationmanager.h"
#include "net/networkmanager.h"
#include "services/duckduckgoservice.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

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
        QByteArray location;          // Location header; empty for a normal response
        QList<QByteArray> setCookies; // one Set-Cookie line per value

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
        for (const QByteArray &value : response.setCookies)
            head += "Set-Cookie: " + value + "\r\n";
        head += "Connection: close\r\n\r\n";
        socket->write(head + response.body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QElapsedTimer m_clock;
};

// The endpoint root carries the token page, /translation.js takes the text.
const QString kTokenPath = QStringLiteral("/");
const QString kPostPath = QStringLiteral("/translation.js");
const QString kToken = QStringLiteral("4-00000000000000000000000000000000000");
const QString kSecondToken = QStringLiteral("4-11111111111111111111111111111111111");

// A synthetic token page with the marker the parser needs; no live page and no
// real token. The unquoted `vqd=` mention comes first, as on the real page.
QByteArray tokenPage(const QString &token = kToken)
{
    return "<!DOCTYPE html><html><body>"
        "<a href=\"/?q=translate&amp;vqd=unquoted\">retry</a>"
        "<div class=\"result\" vqd=\"" + token.toLatin1() + "\"></div>"
        "<p>search page body</p></body></html>";
}

// The live JSON shape; an empty `detected` writes the explicit-source null.
QByteArray translationResponse(const QString &text,
                               const QString &detected = QStringLiteral("en"))
{
    QJsonObject object;
    object.insert(QStringLiteral("translated"), text);
    object.insert(QStringLiteral("detected_language"),
                  detected.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(detected));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// Answers every chunk with its own text, as a translation would.
QByteArray echoResponse(const QByteArray &body)
{
    return translationResponse(QString::fromUtf8(body));
}

QString queryValue(const QUrl &url, const QString &key)
{
    return QUrlQuery(url).queryItemValue(key, QUrl::FullyDecoded);
}

QStringList queryKeys(const QUrl &url)
{
    QStringList keys;
    for (const auto &item : QUrlQuery(url).queryItems(QUrl::FullyDecoded))
        keys << item.first;
    return keys;
}

// The parts a reply sends: the service trims the edge whitespace of every chunk
// and never sends an all-whitespace one.
QStringList sentParts(const QString &text)
{
    QStringList result;
    for (const QString &part : TextChunker::split(text, 900)) {
        if (!part.trimmed().isEmpty())
            result << part.trimmed();
    }
    return result;
}

// Cyrillic, paragraphs and emoji: several 900-unit chunks with edge whitespace.
QString longText()
{
    QString text = QStringLiteral("  ");
    int sentence = 0;
    while (text.size() < 4000) {
        text += QStringLiteral("Предложение номер %1 рассказывает о погоде, природе и 😀.  ")
                    .arg(++sentence);
        if (sentence % 5 == 0)
            text += QStringLiteral("\n\n");
    }
    return text;
}

// Exactly three 900-unit chunks: two paragraph breaks fall inside the second
// half of a window, so every chunk holds one paragraph.
QString threeChunkText()
{
    return QString(700, QLatin1Char('a')) + QStringLiteral("\n\n")
        + QString(700, QLatin1Char('b')) + QStringLiteral("\n\n")
        + QString(700, QLatin1Char('c'));
}

// Error messages are fixed: neither the text, the page, the body, the token nor
// the URL may leak into them.
void verifyFixedMessage(const QString &message)
{
    QVERIFY(!message.isEmpty());
    QVERIFY2(!message.contains(QStringLiteral("TOPSECRET")), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('<')), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('{')), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('}')), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("127.0.0.1")), qPrintable(message));
    QVERIFY2(!message.contains(kToken), qPrintable(message));
    QVERIFY2(!message.contains(kSecondToken), qPrintable(message));
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
            QStringLiteral("qtranslate.service.duckduckgo.debug=true"));
    }

    ~LogCapture()
    {
        QLoggingCategory::setFilterRules(QString());
        qInstallMessageHandler(previous);
    }
};

// A reply the test finishes by hand: the fallback case needs a primary service
// that fails before DuckDuckGo is asked.
class FakeReply : public TranslationReply
{
public:
    explicit FakeReply(QString serviceId)
        : m_serviceId(std::move(serviceId))
    {
    }

    void completeError(TranslationError error, const QString &message)
    {
        TranslationResult result;
        result.error = error;
        result.errorMessage = message;
        result.serviceId = m_serviceId;
        finish(result);
    }

private:
    QString m_serviceId;
};

class FakeService : public TranslationService
{
public:
    explicit FakeService(QString id)
        : m_id(std::move(id))
    {
    }

    QString id() const override { return m_id; }
    QString displayName() const override { return m_id; }
    bool isConfigured() const override { return true; }
    QStringList sourceLanguages() const override { return {QStringLiteral("en")}; }
    QStringList targetLanguages() const override { return {QStringLiteral("de")}; }
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
};

} // namespace

class TestDuckDuckGoService : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
        m_network = std::make_unique<NetworkManager>();
        if (!m_network->nam()->cookieJar())
            m_network->nam()->setCookieJar(new QNetworkCookieJar(m_network->nam()));
        m_service = std::make_unique<DuckDuckGoService>(m_network.get());
        m_service->setEndpoint(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(m_server->port())));
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
        QCOMPARE(m_service->id(), QStringLiteral("duckduckgo"));
        QCOMPARE(m_service->displayName(), QStringLiteral("DuckDuckGo"));
        QVERIFY(m_service->isConfigured());

        const QStringList sources = m_service->sourceLanguages();
        const QStringList targets = m_service->targetLanguages();
        QVERIFY(!sources.isEmpty());
        QCOMPARE(sources, targets);
        QCOMPARE(sources, Languages::supported(QStringLiteral("duckduckgo"),
                                               Languages::Direction::Source));
        QCOMPARE(targets, Languages::supported(QStringLiteral("duckduckgo"),
                                               Languages::Direction::Target));
        QVERIFY(!sources.contains(Languages::autoCode()));
        // The fifteen codes the follow-up probe confirmed as targets; `es` is
        // the one the inherited Bing table does not declare.
        for (const QString &code :
             {QStringLiteral("de"), QStringLiteral("fr"), QStringLiteral("es"),
              QStringLiteral("it"), QStringLiteral("pt-BR"), QStringLiteral("zh-Hans"),
              QStringLiteral("ja"), QStringLiteral("ko"), QStringLiteral("pl"),
              QStringLiteral("uk"), QStringLiteral("tr"), QStringLiteral("ar"),
              QStringLiteral("hi"), QStringLiteral("en"), QStringLiteral("ru")}) {
            QVERIFY2(sources.contains(code), qPrintable(code));
        }
    }

    void defaultsAndEndpoints()
    {
        DuckDuckGoService service(m_network.get());
        QCOMPARE(service.rateLimitMs(), 1000);
        QCOMPARE(service.endpoint(), QUrl(QStringLiteral("https://duckduckgo.com/")));
        QCOMPARE(DuckDuckGoService::defaultEndpoint(), service.endpoint());

        // No setting reaches the protocol, so applying them changes nothing.
        service.applySettings(AppSettings{});
        QCOMPARE(service.rateLimitMs(), 1000);
        QCOMPARE(service.endpoint(), DuckDuckGoService::defaultEndpoint());

        service.setEndpoint(QUrl());
        QCOMPARE(service.endpoint(), DuckDuckGoService::defaultEndpoint());
        const QUrl invalid(QStringLiteral("http://exa mple.com/"));
        QVERIFY(!invalid.isValid());
        service.setEndpoint(invalid);
        QCOMPARE(service.endpoint(), DuckDuckGoService::defaultEndpoint());

        // A URL without a trailing slash must not lose its last segment when
        // the POST URL is resolved against it.
        service.setEndpoint(QUrl(QStringLiteral("http://127.0.0.1:1/base")));
        QVERIFY(service.endpoint().path().endsWith(QLatin1Char('/')));
        QCOMPARE(service.endpoint().resolved(QUrl(QStringLiteral("translation.js"))),
                 QUrl(QStringLiteral("http://127.0.0.1:1/base/translation.js")));

        // A token belongs to the host that issued it: setting an endpoint
        // evicts the cache, so the next reply fetches a new token.
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
        };
        std::unique_ptr<TranslationReply> first = translate(QStringLiteral("Hello"));
        QSignalSpy firstFinished(first.get(), &TranslationReply::finished);
        QVERIFY(firstFinished.wait(5000));
        QVERIFY2(first->result().ok(), qPrintable(first->result().errorMessage));
        QCOMPARE(countPath(kTokenPath), 1);

        m_service->setEndpoint(m_service->endpoint());
        std::unique_ptr<TranslationReply> second = translate(QStringLiteral("Hello"));
        QSignalSpy secondFinished(second.get(), &TranslationReply::finished);
        QVERIFY(secondFinished.wait(5000));
        QVERIFY2(second->result().ok(), qPrintable(second->result().errorMessage));
        QCOMPARE(countPath(kTokenPath), 2);
    }

    void tokenThenPost()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
        };

        // The automatic source sends no `from`; the route detects the language
        // and reports it in the response.
        std::unique_ptr<TranslationReply> reply =
            translate(QStringLiteral("Hello"), Languages::autoCode(), QStringLiteral("de"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hallo"));
        QCOMPARE(reply->result().serviceId, QStringLiteral("duckduckgo"));
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("en"));

        QCOMPARE(m_server->requests.size(), 2);
        const FakeHttpServer::Request &get = m_server->requests.at(0);
        QCOMPARE(get.method, QByteArray("GET"));
        QCOMPARE(get.url.path(), kTokenPath);
        QCOMPARE(queryValue(get.url, QStringLiteral("q")), QStringLiteral("translate"));
        QCOMPARE(get.headers.value("accept"), QByteArray("text/html"));
        QCOMPARE(get.headers.value("user-agent"),
                 DuckDuckGoService::browserUserAgent().toLatin1());
        QVERIFY(get.headers.value("user-agent") != AppInfo::userAgent().toLatin1());
        QVERIFY(!get.headers.contains("cookie"));
        QVERIFY(!get.headers.contains("authorization"));

        const FakeHttpServer::Request &post = m_server->requests.at(1);
        QCOMPARE(post.method, QByteArray("POST"));
        QCOMPARE(post.url.path(), kPostPath);
        QCOMPARE(queryKeys(post.url),
                 QStringList({QStringLiteral("vqd"), QStringLiteral("query"),
                              QStringLiteral("to")}));
        QCOMPARE(queryValue(post.url, QStringLiteral("vqd")), kToken);
        QCOMPARE(queryValue(post.url, QStringLiteral("query")), QStringLiteral("translate"));
        QCOMPARE(queryValue(post.url, QStringLiteral("to")), QStringLiteral("de"));
        // The body is the raw chunk, byte for byte, not a JSON document.
        QCOMPARE(post.body, QByteArray("Hello"));
        QCOMPARE(post.headers.value("content-type"), QByteArray("application/json"));
        QCOMPARE(post.headers.value("accept"), QByteArray("application/json"));
        QCOMPARE(post.headers.value("user-agent"),
                 DuckDuckGoService::browserUserAgent().toLatin1());
        QVERIFY(!post.headers.contains("cookie"));
        QVERIFY(!post.headers.contains("authorization"));

        // The browser identity is request-scoped: a request built by the shared
        // manager still carries the application User-Agent.
        const QNetworkRequest plainRequest = m_network->makeRequest(
            QUrl(QStringLiteral("http://127.0.0.1:%1/ping").arg(m_server->port())));
        QNetworkReply *plain = m_network->nam()->get(plainRequest);
        QSignalSpy plainFinished(plain, &QNetworkReply::finished);
        QVERIFY(plainFinished.wait(5000));
        plain->deleteLater();
        QCOMPARE(m_server->requests.last().headers.value("user-agent"),
                 AppInfo::userAgent().toLatin1());
    }

    void explicitSourceSendsFrom()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{
                200, translationResponse(QStringLiteral("Good morning"), QString())};
        };

        std::unique_ptr<TranslationReply> reply =
            translate(QStringLiteral("Доброе утро"), QStringLiteral("ru"), QStringLiteral("en"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Good morning"));
        // An explicit source makes the endpoint report `null`: nothing detected.
        QVERIFY(reply->result().detectedSourceLang.isEmpty());

        QCOMPARE(m_server->requests.size(), 2);
        const FakeHttpServer::Request &post = m_server->requests.at(1);
        QCOMPARE(queryKeys(post.url),
                 QStringList({QStringLiteral("vqd"), QStringLiteral("query"),
                              QStringLiteral("to"), QStringLiteral("from")}));
        QCOMPARE(queryValue(post.url, QStringLiteral("from")), QStringLiteral("ru"));
        QCOMPARE(queryValue(post.url, QStringLiteral("to")), QStringLiteral("en"));
        QCOMPARE(post.body, QStringLiteral("Доброе утро").toUtf8());
    }

    void reusesCachedToken()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
        };

        for (const QString &text : {QStringLiteral("One"), QStringLiteral("Two")}) {
            std::unique_ptr<TranslationReply> reply = translate(text);
            QSignalSpy finished(reply.get(), &TranslationReply::finished);
            QVERIFY(finished.wait(5000));
            QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        }

        QCOMPARE(countPath(kTokenPath), 1);
        QCOMPARE(countPath(kPostPath), 2);
        QCOMPARE(queryValue(m_server->requests.at(1).url, QStringLiteral("vqd")), kToken);
        QCOMPARE(queryValue(m_server->requests.at(2).url, QStringLiteral("vqd")), kToken);
    }

    void autoIsSupported()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{
                200, translationResponse(QStringLiteral("Hallo"), QStringLiteral("fr"))};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Bonjour"),
                                                           Languages::autoCode(),
                                                           QStringLiteral("de"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("fr"));

        // There is no "auto" value to send: the parameter is simply absent.
        const FakeHttpServer::Request &post = m_server->requests.at(1);
        QVERIFY(!QUrlQuery(post.url).hasQueryItem(QStringLiteral("from")));
        QCOMPARE(queryKeys(post.url),
                 QStringList({QStringLiteral("vqd"), QStringLiteral("query"),
                              QStringLiteral("to")}));
    }

    void refreshesTokenOn403()
    {
        auto tokenFetches = std::make_shared<int>(0);
        auto posts = std::make_shared<int>(0);
        m_server->handler = [tokenFetches, posts](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET")) {
                ++*tokenFetches;
                return FakeHttpServer::Response{
                    200, tokenPage(*tokenFetches == 1 ? kToken : kSecondToken)};
            }
            if (++*posts == 1)
                return FakeHttpServer::Response{403, {}};
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hallo"));

        QCOMPARE(m_server->requests.size(), 4);
        QCOMPARE(m_server->requests.at(0).method, QByteArray("GET"));
        QCOMPARE(m_server->requests.at(1).method, QByteArray("POST"));
        QCOMPARE(m_server->requests.at(2).method, QByteArray("GET"));
        QCOMPARE(m_server->requests.at(3).method, QByteArray("POST"));
        QCOMPARE(queryValue(m_server->requests.at(1).url, QStringLiteral("vqd")), kToken);
        QCOMPARE(queryValue(m_server->requests.at(3).url, QStringLiteral("vqd")), kSecondToken);
        // The same chunk, not a skipped or a trimmed one.
        QCOMPARE(m_server->requests.at(3).body, m_server->requests.at(1).body);
        QCOMPARE(m_server->requests.at(3).body, QByteArray("Hello"));

        // No further token fetch follows the successful retry.
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 4);
    }

    void refreshesOnLaterChunkKeepsIndex()
    {
        auto tokenFetches = std::make_shared<int>(0);
        auto posts = std::make_shared<int>(0);
        m_server->handler = [tokenFetches, posts](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET")) {
                ++*tokenFetches;
                return FakeHttpServer::Response{
                    200, tokenPage(*tokenFetches == 1 ? kToken : kSecondToken)};
            }
            // The second chunk is rejected once; the retry and the third chunk
            // must still translate.
            if (++*posts == 2)
                return FakeHttpServer::Response{403, {}};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
        };

        const QString text = threeChunkText();
        const QStringList parts = sentParts(text);
        QCOMPARE(parts.size(), 3);

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("ru"), QStringLiteral("en"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, text);

        QList<QByteArray> bodies;
        for (const FakeHttpServer::Request &request : std::as_const(m_server->requests)) {
            if (request.method == QByteArray("POST"))
                bodies << request.body;
        }
        QCOMPARE(bodies.size(), 4);
        QCOMPARE(bodies.at(0), parts.at(0).toUtf8());
        QCOMPARE(bodies.at(1), parts.at(1).toUtf8());
        QCOMPARE(bodies.at(2), parts.at(1).toUtf8());
        QCOMPARE(bodies.at(3), parts.at(2).toUtf8());
        QCOMPARE(countPath(kTokenPath), 2);
    }

    void second403IsRateLimited()
    {
        auto tokenFetches = std::make_shared<int>(0);
        auto posts = std::make_shared<int>(0);
        m_server->handler = [tokenFetches, posts](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET")) {
                ++*tokenFetches;
                return FakeHttpServer::Response{
                    200, tokenPage(QStringLiteral("4-token-%1").arg(*tokenFetches))};
            }
            ++*posts;
            return FakeHttpServer::Response{403, {}};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::RateLimited);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("the endpoint rejected the token"));
        verifyFixedMessage(reply->result().errorMessage);
        QCOMPARE(countPath(kTokenPath), 2);
        QCOMPARE(countPath(kPostPath), 2);
        QTest::qWait(50);
        QCOMPARE(countPath(kTokenPath), 2);
        QCOMPARE(countPath(kPostPath), 2);
    }

    void failedRefreshSurfacesItsError()
    {
        auto tokenFetches = std::make_shared<int>(0);
        m_server->handler = [tokenFetches](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET")) {
                ++*tokenFetches;
                if (*tokenFetches == 1)
                    return FakeHttpServer::Response{200, tokenPage()};
                return FakeHttpServer::Response{503, {}};
            }
            return FakeHttpServer::Response{403, {}};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Network);
        QCOMPARE(reply->result().errorMessage, QStringLiteral("HTTP 503"));
        verifyFixedMessage(reply->result().errorMessage);
        // One successful GET, one POST, one failed refresh GET; no retry POST.
        QCOMPARE(countPath(kTokenPath), 2);
        QCOMPARE(countPath(kPostPath), 1);
        QTest::qWait(50);
        QCOMPARE(countPath(kPostPath), 1);
    }

    void httpErrors_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<TranslationError>("error");
        QTest::addColumn<QString>("message");
        QTest::addColumn<int>("posts");

        QTest::newRow("invalid_to") << 400 << QByteArray(R"({"error":"invalid_to"})")
                                    << TranslationError::UnsupportedLanguage
                                    << "the endpoint rejected the language" << 1;
        QTest::newRow("invalid_text") << 400 << QByteArray(R"({"error":"invalid_text"})")
                                      << TranslationError::TooLong
                                      << "the request is too long for the endpoint" << 1;
        QTest::newRow("429") << 429 << QByteArray() << TranslationError::RateLimited
                             << "HTTP 429" << 1;
        // A 403 on the POST is a rejected token first: one refresh, then the
        // second 403 is the block.
        QTest::newRow("403") << 403 << QByteArray(R"({"error":"Forbidden"})")
                             << TranslationError::RateLimited
                             << "the endpoint rejected the token" << 2;
        QTest::newRow("500") << 500 << QByteArray() << TranslationError::Network << "HTTP 500" << 1;
        QTest::newRow("404") << 404 << QByteArray() << TranslationError::BadResponse
                             << "HTTP 404" << 1;
        QTest::newRow("html body") << 200
            << QByteArray("<!DOCTYPE html><html><body>Before you continue</body></html>")
            << TranslationError::ServiceUnavailable << "the endpoint returned an HTML page" << 1;
        QTest::newRow("unknown error code") << 200
            << QByteArray(R"({"error":"something_new"})")
            << TranslationError::ServiceUnavailable << "the endpoint returned an error" << 1;
        QTest::newRow("malformed body") << 200 << QByteArray(R"({"translated":)")
                                        << TranslationError::BadResponse
                                        << "no translated text in the response" << 1;
        QTest::newRow("empty body") << 200 << QByteArray() << TranslationError::BadResponse
                                    << "no translated text in the response" << 1;
    }

    void httpErrors()
    {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(TranslationError, error);
        QFETCH(QString, message);
        QFETCH(int, posts);

        const int responseStatus = status;
        const QByteArray responseBody = body;
        m_server->handler = [responseStatus, responseBody](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{responseStatus, responseBody};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, error);
        QCOMPARE(reply->result().errorMessage, message);
        QVERIFY(reply->result().translatedText.isEmpty());
        verifyFixedMessage(reply->result().errorMessage);
        QCOMPARE(countPath(kPostPath), posts);
        QTest::qWait(20);
        QCOMPARE(countPath(kPostPath), posts);
    }

    void unsupportedLanguage()
    {
        {
            std::unique_ptr<TranslationReply> reply =
                translate(QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ceb"));
            QSignalSpy finished(reply.get(), &TranslationReply::finished);
            QVERIFY(finished.wait(1000));
            QCOMPARE(reply->result().error, TranslationError::UnsupportedLanguage);
            QVERIFY2(reply->result().errorMessage.contains(
                         QStringLiteral("language \"ceb\" is not supported")),
                     qPrintable(reply->result().errorMessage));
        }
        {
            std::unique_ptr<TranslationReply> reply =
                translate(QStringLiteral("Hello"), QStringLiteral("haw"), QStringLiteral("en"));
            QSignalSpy finished(reply.get(), &TranslationReply::finished);
            QVERIFY(finished.wait(1000));
            QCOMPARE(reply->result().error, TranslationError::UnsupportedLanguage);
            QVERIFY2(reply->result().errorMessage.contains(
                         QStringLiteral("language \"haw\" is not supported")),
                     qPrintable(reply->result().errorMessage));
        }

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

        std::unique_ptr<TranslationReply> reply = translate(text);
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::EmptyText);
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 0);
    }

    void edgeWhitespace()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("X"))};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral(" \tHello\r\n "));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral(" \tX\r\n "));
        QCOMPARE(m_server->requests.size(), 2);
        QCOMPARE(m_server->requests.at(1).body, QByteArray("Hello"));

        // The middle chunk is all spaces: it is never sent, and its whitespace
        // is restored through the unsent-part branch.
        const QString text =
            QStringLiteral("Hello") + QString(2000, QLatin1Char(' ')) + QStringLiteral("World");
        bool whitespaceOnlyPart = false;
        for (const QString &part : TextChunker::split(text, 900))
            whitespaceOnlyPart = whitespaceOnlyPart || part.trimmed().isEmpty();
        QVERIFY(whitespaceOnlyPart);
        const QStringList parts = sentParts(text);
        QCOMPARE(parts.size(), qsizetype(2));

        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
        };
        std::unique_ptr<TranslationReply> spaced =
            translate(text, QStringLiteral("en"), QStringLiteral("de"));
        QSignalSpy spacedFinished(spaced.get(), &TranslationReply::finished);
        QVERIFY(spacedFinished.wait(5000));
        QVERIFY2(spaced->result().ok(), qPrintable(spaced->result().errorMessage));
        QCOMPARE(spaced->result().translatedText, text);
        QCOMPARE(countPath(kPostPath), 3); // "Hello" and "World" plus the first case
        for (const FakeHttpServer::Request &request : std::as_const(m_server->requests)) {
            if (request.method == QByteArray("POST"))
                QVERIFY2(!request.body.isEmpty(), "a whitespace-only chunk was sent");
        }
    }

    void chunks()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
        };

        const QString text = longText();
        const QStringList expected = sentParts(text);
        QVERIFY(expected.size() > 2);

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("ru"), QStringLiteral("en"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, text);
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("en"));
        QCOMPARE(countPath(kTokenPath), 1);
        QCOMPARE(countPath(kPostPath), expected.size());

        qsizetype index = 0;
        for (const FakeHttpServer::Request &request : std::as_const(m_server->requests)) {
            if (request.method != QByteArray("POST"))
                continue;
            const QString sent = QString::fromUtf8(request.body);
            QCOMPARE(sent, expected.at(index++));
            QVERIFY2(sent.size() <= 900,
                     qPrintable(QStringLiteral("%1 UTF-16 units").arg(sent.size())));
            QVERIFY(!sent.at(0).isLowSurrogate());
            QVERIFY(!sent.at(sent.size() - 1).isHighSurrogate());
        }
        QCOMPARE(index, expected.size());
    }

    void pacesChunks()
    {
        m_service->setRateLimitMs(100);
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
        };
        const QString text = longText();
        QVERIFY(sentParts(text).size() > 2);

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("ru"), QStringLiteral("en"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        // Every request, including the first POST after the token GET, is paced.
        QVERIFY(m_server->requests.size() > 2);
        for (qsizetype i = 1; i < m_server->requests.size(); ++i) {
            const qint64 delta =
                m_server->requests.at(i).elapsedMs - m_server->requests.at(i - 1).elapsedMs;
            QVERIFY2(delta >= 90, qPrintable(QStringLiteral("delta %1 ms").arg(delta)));
        }
    }

    void serializesRequests()
    {
        m_service->setRateLimitMs(200);
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
        };

        std::unique_ptr<TranslationReply> first = translate(QStringLiteral("One"));
        std::unique_ptr<TranslationReply> second = translate(QStringLiteral("Two"));
        QSignalSpy firstFinished(first.get(), &TranslationReply::finished);
        QSignalSpy secondFinished(second.get(), &TranslationReply::finished);
        QVERIFY(firstFinished.wait(5000));
        QVERIFY(secondFinished.wait(5000));
        QVERIFY2(first->result().ok(), qPrintable(first->result().errorMessage));
        QVERIFY2(second->result().ok(), qPrintable(second->result().errorMessage));
        QCOMPARE(countPath(kTokenPath), 1);
        QCOMPARE(countPath(kPostPath), 2);

        // The second reply waits for the first one's last request.
        const qint64 delta =
            m_server->requests.at(2).elapsedMs - m_server->requests.at(1).elapsedMs;
        QVERIFY2(delta >= 180, qPrintable(QStringLiteral("delta %1 ms").arg(delta)));
    }

    void abortActiveTokenRequest()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QTRY_VERIFY_WITH_TIMEOUT(m_server->requests.size() == 1, 5000);
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        reply->abort();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(reply->result().error, TranslationError::Cancelled);
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 1);
    }

    void abortActivePostRequest()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, {}, false};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QTRY_COMPARE_WITH_TIMEOUT(countPath(kPostPath), 1, 5000);
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        reply->abort();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(reply->result().error, TranslationError::Cancelled);
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 2);
    }

    void failedMiddleChunkFailsTheWholeReply()
    {
        auto posts = std::make_shared<int>(0);
        m_server->handler = [posts](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            if (++*posts == 1)
                return FakeHttpServer::Response{200, echoResponse(request.body)};
            return FakeHttpServer::Response{500, {}};
        };
        const QString text = longText();

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("ru"), QStringLiteral("en"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QCOMPARE(reply->result().error, TranslationError::Network);
        QVERIFY(reply->result().translatedText.isEmpty());
        QCOMPARE(countPath(kTokenPath), 1);
        QCOMPARE(countPath(kPostPath), 2);
        QTest::qWait(50);
        QCOMPARE(countPath(kPostPath), 2);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void timeout()
    {
        m_network->setTimeoutMs(300);
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, {}, false};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Timeout);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("DuckDuckGo did not respond within 0.3 s"));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void closedServerIsNetwork()
    {
        m_server->close();

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Network);
        // describeNetworkError(request, false), never QNetworkReply::errorString():
        // that would contain the host, the port and the page.
        QCOMPARE(reply->result().errorMessage, QStringLiteral("Network error"));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void crossOriginRedirectOnGetIsNotFollowed()
    {
        FakeHttpServer other;
        const quint16 otherPort = other.port();
        m_server->handler = [otherPort](const FakeHttpServer::Request &) {
            FakeHttpServer::Response response;
            response.status = 302;
            response.location = QByteArray("http://127.0.0.1:") + QByteArray::number(otherPort)
                + "/";
            return response;
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("TOPSECRET"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY(!reply->result().ok());
        QVERIFY(reply->result().translatedText.isEmpty());
        QTest::qWait(100);
        QCOMPARE(other.requests.size(), 0);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void crossOriginRedirectOnPostIsNotFollowed()
    {
        FakeHttpServer other;
        const quint16 otherPort = other.port();
        m_server->handler = [otherPort](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            FakeHttpServer::Response response;
            response.status = 302;
            response.location = QByteArray("http://127.0.0.1:") + QByteArray::number(otherPort)
                + "/translation.js";
            return response;
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("TOPSECRET"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY(!reply->result().ok());
        QVERIFY(reply->result().translatedText.isEmpty());
        QTest::qWait(100);
        QCOMPARE(other.requests.size(), 0);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void cookieIsolation()
    {
        const QByteArray cookie = "ddg=COOKIEVALUE; Path=/";
        m_server->handler = [cookie](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET")) {
                FakeHttpServer::Response response;
                response.body = tokenPage();
                response.setCookies = {cookie};
                return response;
            }
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
        };

        const QUrl origin(QStringLiteral("http://127.0.0.1:%1/").arg(m_server->port()));
        QNetworkCookieJar *jar = m_network->nam()->cookieJar();
        QVERIFY(jar);
        QNetworkCookie preloaded(QByteArray("jar"), QByteArray("value"));
        preloaded.setDomain(QStringLiteral("127.0.0.1"));
        preloaded.setPath(QStringLiteral("/"));
        QVERIFY(jar->setCookiesFromUrl({preloaded}, origin));

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        // Neither request reads from or writes to the shared jar: the token page
        // needs no cookie and must not plant one.
        QCOMPARE(m_server->requests.size(), 2);
        QVERIFY(m_server->requests.at(0).headers.value("cookie").isEmpty());
        QVERIFY(m_server->requests.at(1).headers.value("cookie").isEmpty());
        for (const QNetworkCookie &stored : jar->cookiesForUrl(origin))
            QVERIFY2(stored.name() != "ddg", qPrintable(stored.name()));

        // The jar still works for a request that is not the DuckDuckGo exchange.
        const QNetworkRequest plainRequest = m_network->makeRequest(
            QUrl(QStringLiteral("http://127.0.0.1:%1/ping").arg(m_server->port())));
        QNetworkReply *plain = m_network->nam()->get(plainRequest);
        QSignalSpy plainFinished(plain, &QNetworkReply::finished);
        QVERIFY(plainFinished.wait(5000));
        plain->deleteLater();
        QVERIFY2(m_server->requests.last().headers.value("cookie").contains("jar=value"),
                 qPrintable(m_server->requests.last().headers.value("cookie")));
    }

    void logsAndMessagesAreClean()
    {
        auto posts = std::make_shared<int>(0);
        m_server->handler = [posts](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            if (++*posts == 1)
                return FakeHttpServer::Response{
                    200, translationResponse(QStringLiteral("TOPSECRET"))};
            return FakeHttpServer::Response{200, QByteArray("Too many requests TOPSECRET")};
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
            QCOMPARE(failure->result().error, TranslationError::RateLimited);
            verifyFixedMessage(failure->result().errorMessage);
        }

        QVERIFY(!g_logMessages.isEmpty());
        for (const QString &message : std::as_const(g_logMessages)) {
            QVERIFY2(!message.contains(QStringLiteral("TOPSECRET")), qPrintable(message));
            QVERIFY2(!message.contains(kToken), qPrintable(message));
            QVERIFY2(!message.contains(kSecondToken), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("vqd")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("<html")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("Too many requests")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("translated")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("detected_language")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("127.0.0.1")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("translation.js")), qPrintable(message));
        }
    }

    void fallbackServiceWorks()
    {
        TranslationManager manager;
        auto *primary = new FakeService(QStringLiteral("primary"));
        auto *fallback = new DuckDuckGoService(m_network.get());
        fallback->setEndpoint(m_service->endpoint());
        fallback->setRateLimitMs(0);
        manager.registerService(primary);
        manager.registerService(fallback);
        manager.setFallbackService(QStringLiteral("duckduckgo"));
        QCOMPARE(manager.activeService(), static_cast<TranslationService *>(primary));

        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.method == QByteArray("GET"))
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
        };

        std::unique_ptr<TranslationReply> reply(manager.translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("de")},
            TranslationManager::Channel::MainWindow));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QCOMPARE(primary->requests.size(), 1);
        primary->replies.last()->completeError(TranslationError::Network,
                                               QStringLiteral("network"));
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().serviceId, QStringLiteral("duckduckgo"));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hallo"));
    }

private:
    std::unique_ptr<TranslationReply> translate(
        const QString &text, const QString &source = QStringLiteral("en"),
        const QString &target = QStringLiteral("de"))
    {
        return std::unique_ptr<TranslationReply>(m_service->translate({text, source, target}));
    }

    qsizetype countPath(const QString &path) const
    {
        qsizetype count = 0;
        for (const FakeHttpServer::Request &request : m_server->requests) {
            if (request.url.path() == path)
                ++count;
        }
        return count;
    }

    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<DuckDuckGoService> m_service;
};

QTEST_GUILESS_MAIN(TestDuckDuckGoService)
#include "tst_duckduckgoservice.moc"
