#include "core/languages.h"
#include "core/settings.h"
#include "core/textchunker.h"
#include "core/translationmanager.h"
#include "net/networkmanager.h"
#include "services/libretranslateservice.h"

#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
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

const QString kTranslatePath = QStringLiteral("/translate");

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(QTRANSLATE_FIXTURES_DIR "/libretranslate/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open fixture %s", qPrintable(file.fileName()));
    return file.readAll();
}

// The explicit-source shape: no detectedLanguage at all.
QByteArray translationResponse(const QString &text)
{
    QJsonObject object;
    object.insert(QStringLiteral("translatedText"), text);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// The auto shape: the recorded confidence float plus the detected language.
QByteArray autoResponse(const QString &text, const QString &detected)
{
    QJsonObject language;
    language.insert(QStringLiteral("confidence"), 90.0);
    language.insert(QStringLiteral("language"), detected);
    QJsonObject object;
    object.insert(QStringLiteral("detectedLanguage"), language);
    object.insert(QStringLiteral("translatedText"), text);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject requestObject(const FakeHttpServer::Request &request)
{
    return QJsonDocument::fromJson(request.body).object();
}

// Answers every chunk with the chunk's own text, as a translation would.
QByteArray echoResponse(const FakeHttpServer::Request &request)
{
    return translationResponse(requestObject(request).value(QStringLiteral("q")).toString());
}

// The parts a reply sends: the service trims the edge whitespace of every chunk
// and never sends an all-whitespace one.
QStringList sentParts(const QString &text)
{
    QStringList result;
    for (const QString &part : TextChunker::split(text, 5000)) {
        if (!part.trimmed().isEmpty())
            result << part.trimmed();
    }
    return result;
}

// Cyrillic, paragraphs and emoji: more than 15000 UTF-16 units, i.e. at least
// four 5000-unit chunks with edge whitespace.
QString longText()
{
    QString text = QStringLiteral("  ");
    int sentence = 0;
    while (text.size() < 16000) {
        text += QStringLiteral("Предложение номер %1 рассказывает о погоде, природе и 😀.  ")
                    .arg(++sentence);
        if (sentence % 5 == 0)
            text += QStringLiteral("\n\n");
    }
    return text;
}

// Error messages are fixed: neither the text, the body, the key nor the URL may
// leak into them.
void verifyFixedMessage(const QString &message)
{
    QVERIFY(!message.isEmpty());
    QVERIFY2(!message.contains(QStringLiteral("TOPSECRET")), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("secret-key")), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('<')), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('{')), qPrintable(message));
    QVERIFY2(!message.contains(QLatin1Char('}')), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("127.0.0.1")), qPrintable(message));
    QVERIFY2(!message.contains(kTranslatePath), qPrintable(message));
}

// Captures the application log lines for the hygiene tests.
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
            QStringLiteral("qtranslate.service.libretranslate.debug=true"));
    }

    ~LogCapture()
    {
        QLoggingCategory::setFilterRules(QString());
        qInstallMessageHandler(previous);
    }
};

// A reply the test finishes by hand: the fallback case needs a primary service
// that fails before LibreTranslate is asked.
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

class TestLibreTranslateService : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
        m_network = std::make_unique<NetworkManager>();
        m_service = std::make_unique<LibreTranslateService>(m_network.get());
        m_service->setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(m_server->port())));
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
        // The pause of a fresh instance; init() lowers it for the tests.
        LibreTranslateService defaults(m_network.get());
        QCOMPARE(defaults.rateLimitMs(), 500);

        QCOMPARE(m_service->id(), QStringLiteral("libretranslate"));
        QCOMPARE(m_service->displayName(), QStringLiteral("LibreTranslate"));
        QVERIFY(m_service->isConfigured());
        QVERIFY(m_service->apiKey().isEmpty());

        const QStringList sources = m_service->sourceLanguages();
        const QStringList targets = m_service->targetLanguages();
        QVERIFY(!sources.isEmpty());
        QCOMPARE(sources, targets);
        QCOMPARE(sources, Languages::supported(QStringLiteral("libretranslate"),
                                               Languages::Direction::Source));
        QCOMPARE(targets, Languages::supported(QStringLiteral("libretranslate"),
                                               Languages::Direction::Target));
        QCOMPARE(sources.size(), 51);
        QVERIFY(!sources.contains(Languages::autoCode()));
        for (const QString &code : {QStringLiteral("fil"), QStringLiteral("pt-PT"),
                                    QStringLiteral("zh-Hant"), QStringLiteral("sr"),
                                    QStringLiteral("nb")}) {
            QVERIFY2(sources.contains(code), qPrintable(code));
        }
        for (const QString &code :
             {QStringLiteral("auto"), QStringLiteral("af"), QStringLiteral("kk"),
              QStringLiteral("pa"), QStringLiteral("haw")}) {
            QVERIFY2(!sources.contains(code), qPrintable(code));
        }
    }

    void configuration()
    {
        // A fresh instance has no base URL: research §1.5 found no key-less
        // public instance, so there is no default to fall back to.
        LibreTranslateService fresh(m_network.get());
        QVERIFY(!fresh.isConfigured());
        QVERIFY(fresh.baseUrl().isEmpty());
        QCOMPARE(fresh.baseUrl(), LibreTranslateService::defaultBaseUrl());
        QVERIFY(LibreTranslateService::defaultBaseUrl().isEmpty());
        QVERIFY(fresh.configurationError().contains(QStringLiteral("base URL")));

        {
            std::unique_ptr<TranslationReply> reply(fresh.translate(
                {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
            QSignalSpy finished(reply.get(), &TranslationReply::finished);
            QVERIFY(finished.wait(1000));
            QCOMPARE(reply->result().error, TranslationError::Auth);
            QCOMPARE(reply->result().errorMessage, fresh.configurationError());
            QVERIFY(reply->result().translatedText.isEmpty());
        }
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 0);

        for (const QUrl &url : {QUrl(QStringLiteral("http://exa mple.com/")),
                                QUrl(QStringLiteral("ftp://127.0.0.1/")),
                                QUrl(QStringLiteral("http:///path"))}) {
            fresh.setBaseUrl(url);
            QVERIFY2(!fresh.isConfigured(), qPrintable(url.toString()));
        }

        // The key never changes isConfigured().
        fresh.setApiKey(QStringLiteral("key"));
        QVERIFY(!fresh.isConfigured());
        fresh.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(m_server->port())));
        QVERIFY(fresh.isConfigured());
        QVERIFY(fresh.configurationError().isEmpty());
        fresh.setBaseUrl(QUrl());
        QVERIFY(!fresh.isConfigured());
    }

    void translation()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Привет"))};
        };

        std::unique_ptr<TranslationReply> reply =
            translate(QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Привет"));
        QCOMPARE(reply->result().serviceId, QStringLiteral("libretranslate"));
        QCOMPARE(reply->result().detectedSourceLang, QString());

        QCOMPARE(m_server->requests.size(), 1);
        const FakeHttpServer::Request &post = m_server->requests.at(0);
        QCOMPARE(post.method, QByteArray("POST"));
        QCOMPARE(post.url.path(), kTranslatePath);
        QCOMPARE(post.headers.value("content-type"), QByteArray("application/json"));
        QCOMPARE(post.headers.value("accept"), QByteArray("application/json"));
        // Qt always adds Accept-Language from the system locale, so the service
        // pins it to English: the body classifier matches the recorded English
        // sentences, and the assertion also proves the system locale does not
        // leak to the server.
        QCOMPARE(post.headers.value("accept-language"), QByteArray("en"));

        const QJsonObject body = requestObject(post);
        QCOMPARE(body.value(QStringLiteral("q")).toString(), QStringLiteral("Hello"));
        QCOMPARE(body.value(QStringLiteral("source")).toString(), QStringLiteral("en"));
        QCOMPARE(body.value(QStringLiteral("target")).toString(), QStringLiteral("ru"));
        // The key is absent, not empty, when none is set.
        QVERIFY(!body.contains(QStringLiteral("api_key")));
        QCOMPARE(body.size(), 3);
    }

    void apiKeyIsOptional()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
        };

        {
            LogCapture capture;

            m_service->setApiKey(QStringLiteral("secret-key"));
            finish(translate(QStringLiteral("Hello")));
            QCOMPARE(requestObject(m_server->requests.last())
                         .value(QStringLiteral("api_key"))
                         .toString(),
                     QStringLiteral("secret-key"));

            // The key is trimmed when the body is built.
            m_service->setApiKey(QStringLiteral("  secret-key\n"));
            finish(translate(QStringLiteral("Hello")));
            QCOMPARE(requestObject(m_server->requests.last())
                         .value(QStringLiteral("api_key"))
                         .toString(),
                     QStringLiteral("secret-key"));

            m_service->setApiKey(QString());
            finish(translate(QStringLiteral("Hello")));
            QVERIFY(!requestObject(m_server->requests.last())
                         .contains(QStringLiteral("api_key")));
        }

        QVERIFY(!g_logMessages.isEmpty());
        for (const QString &message : std::as_const(g_logMessages))
            QVERIFY2(!message.contains(QStringLiteral("secret-key")), qPrintable(message));
    }

    void autoIsDetected()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200,
                                            autoResponse(QStringLiteral("Good morning"),
                                                         QStringLiteral("ru"))};
        };

        std::unique_ptr<TranslationReply> reply = translate(
            QStringLiteral("Доброе утро"), Languages::autoCode(), QStringLiteral("en"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Good morning"));
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("ru"));
        // `source` is mandatory on the server: auto-detection is the literal
        // "auto", not an omitted field.
        QCOMPARE(requestObject(m_server->requests.at(0)).value(QStringLiteral("source")).toString(),
                 QStringLiteral("auto"));
    }

    void pathPrefixIsKept()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
        };

        for (const QString &base : {QStringLiteral("http://127.0.0.1:%1/lt/"),
                                    QStringLiteral("http://127.0.0.1:%1/lt")}) {
            m_service->setBaseUrl(QUrl(base.arg(m_server->port())));
            finish(translate(QStringLiteral("Hello")));
            QCOMPARE(m_server->requests.last().url.path(), QStringLiteral("/lt/translate"));
        }
    }

    void specialCharacters()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{
                200, translationResponse(
                         requestObject(request).value(QStringLiteral("q")).toString()
                         + QStringLiteral("!"))};
        };

        // JSON escaping is why a quote, a backslash, a newline and braces are
        // safe here, unlike the percent-encoded query of MozhiService.
        const QString text = QStringLiteral("He said \"hi\" \\ and {left}\nnext line");
        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("en"), QStringLiteral("de"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, text + QStringLiteral("!"));
        QCOMPARE(requestObject(m_server->requests.at(0)).value(QStringLiteral("q")).toString(),
                 text);
    }

    void unsupportedLanguage()
    {
        {
            std::unique_ptr<TranslationReply> reply =
                translate(QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("af"));
            QSignalSpy finished(reply.get(), &TranslationReply::finished);
            QVERIFY(finished.wait(1000));
            QCOMPARE(reply->result().error, TranslationError::UnsupportedLanguage);
            QVERIFY2(reply->result().errorMessage.contains(
                         QStringLiteral("language \"af\" is not supported")),
                     qPrintable(reply->result().errorMessage));
        }
        {
            std::unique_ptr<TranslationReply> reply =
                translate(QStringLiteral("Hello"), QStringLiteral("kk"), QStringLiteral("en"));
            QSignalSpy finished(reply.get(), &TranslationReply::finished);
            QVERIFY(finished.wait(1000));
            QCOMPARE(reply->result().error, TranslationError::UnsupportedLanguage);
            QVERIFY2(reply->result().errorMessage.contains(
                         QStringLiteral("language \"kk\" is not supported")),
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

    void httpErrors_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<TranslationError>("error");
        QTest::addColumn<QString>("message");

        const QString rateLimit = QStringLiteral("the endpoint reported a rate limit");
        const QString tooLong = QStringLiteral("the request is too long for the endpoint");
        const QString language = QStringLiteral("the endpoint rejected the language");
        const QString key = QStringLiteral("the endpoint rejected the API key");
        const QString request = QStringLiteral("the endpoint rejected the request");
        const QString provider = QStringLiteral("the endpoint returned an error");
        const QString html = QStringLiteral("the endpoint returned an HTML page");
        const QString noText = QStringLiteral("no translated text in the response");

        QTest::newRow("429 slowdown")
            << 429 << fixture(QStringLiteral("error_slowdown.json")) << TranslationError::RateLimited
            << rateLimit;
        QTest::newRow("429 empty body") << 429 << QByteArray() << TranslationError::RateLimited
                                        << QStringLiteral("HTTP 429");
        QTest::newRow("413 text limit")
            << 413 << fixture(QStringLiteral("error_text_limit.json")) << TranslationError::TooLong
            << tooLong;
        QTest::newRow("400 not supported")
            << 400 << fixture(QStringLiteral("error_not_supported.json"))
            << TranslationError::UnsupportedLanguage << language;
        QTest::newRow("403 invalid key")
            << 403 << fixture(QStringLiteral("error_invalid_key.json")) << TranslationError::Auth
            << key;
        // The hosted-instance shape: a key-protected server answers 400, not 401.
        QTest::newRow("400 portal")
            << 400 << fixture(QStringLiteral("error_portal.json")) << TranslationError::Auth << key;
        QTest::newRow("400 missing q")
            << 400 << fixture(QStringLiteral("error_missing_q.json"))
            << TranslationError::BadResponse << request;
        QTest::newRow("401 empty body") << 401 << QByteArray() << TranslationError::Auth
                                        << QStringLiteral("HTTP 401");
        QTest::newRow("500 empty body") << 500 << QByteArray() << TranslationError::Network
                                        << QStringLiteral("HTTP 500");
        QTest::newRow("503 unknown error")
            << 503 << fixture(QStringLiteral("error_unknown.json"))
            << TranslationError::ServiceUnavailable << provider;
        QTest::newRow("404 empty body") << 404 << QByteArray() << TranslationError::BadResponse
                                        << QStringLiteral("HTTP 404");
        QTest::newRow("200 html page")
            << 200 << fixture(QStringLiteral("html_page.html"))
            << TranslationError::ServiceUnavailable << html;
        QTest::newRow("200 malformed")
            << 200 << fixture(QStringLiteral("response_malformed.json"))
            << TranslationError::BadResponse << noText;
        QTest::newRow("200 empty body")
            << 200 << fixture(QStringLiteral("response_empty.json")) << TranslationError::BadResponse
            << noText;
        QTest::newRow("200 empty object")
            << 200 << QByteArray("{}") << TranslationError::BadResponse << noText;
        // The body is classified before the status: a 200 is no proof of a
        // translation when the server reports an error.
        QTest::newRow("200 slowdown")
            << 200 << fixture(QStringLiteral("error_slowdown.json")) << TranslationError::RateLimited
            << rateLimit;
    }

    void httpErrors()
    {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(TranslationError, error);
        QFETCH(QString, message);

        const int responseStatus = status;
        const QByteArray responseBody = body;
        m_server->handler = [responseStatus, responseBody](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{responseStatus, responseBody};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("TOPSECRET"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, error);
        QCOMPARE(reply->result().errorMessage, message);
        QVERIFY(reply->result().translatedText.isEmpty());
        verifyFixedMessage(reply->result().errorMessage);
        QCOMPARE(m_server->requests.size(), 1);
        QTest::qWait(20);
        QCOMPARE(m_server->requests.size(), 1);
    }

    void chunks()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request)};
        };

        const QString text = longText();
        const QStringList expected = sentParts(text);
        QVERIFY(expected.size() >= 4);

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("ru"), QStringLiteral("en"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, text);
        QCOMPARE(m_server->requests.size(), expected.size());

        qsizetype index = 0;
        for (const FakeHttpServer::Request &request : std::as_const(m_server->requests)) {
            QCOMPARE(request.method, QByteArray("POST"));
            const QString sent = requestObject(request).value(QStringLiteral("q")).toString();
            QCOMPARE(sent, expected.at(index++));
            QVERIFY2(sent.size() <= 5000,
                     qPrintable(QStringLiteral("%1 UTF-16 units").arg(sent.size())));
            QVERIFY(!sent.at(0).isLowSurrogate());
            QVERIFY(!sent.at(sent.size() - 1).isHighSurrogate());
        }
        QCOMPARE(index, expected.size());
    }

    void edgeWhitespace()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("X"))};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral(" \tHello\r\n "));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral(" \tX\r\n "));
        QCOMPARE(m_server->requests.size(), 1);
        QCOMPARE(requestObject(m_server->requests.at(0)).value(QStringLiteral("q")).toString(),
                 QStringLiteral("Hello"));
    }

    void pacesChunks()
    {
        m_service->setRateLimitMs(100);
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request)};
        };
        const QString text = longText();
        QVERIFY(sentParts(text).size() >= 3);

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("ru"), QStringLiteral("en"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        QVERIFY(m_server->requests.size() >= 3);
        for (qsizetype i = 1; i < m_server->requests.size(); ++i) {
            const qint64 delta =
                m_server->requests.at(i).elapsedMs - m_server->requests.at(i - 1).elapsedMs;
            QVERIFY2(delta >= 90, qPrintable(QStringLiteral("delta %1 ms").arg(delta)));
        }
    }

    void serializesRequests()
    {
        m_service->setRateLimitMs(200);
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
        };

        std::unique_ptr<TranslationReply> first = translate(QStringLiteral("One"));
        std::unique_ptr<TranslationReply> second = translate(QStringLiteral("Two"));
        QSignalSpy firstFinished(first.get(), &TranslationReply::finished);
        QSignalSpy secondFinished(second.get(), &TranslationReply::finished);
        QVERIFY(firstFinished.wait(5000));
        QVERIFY(secondFinished.wait(5000));
        QVERIFY2(first->result().ok(), qPrintable(first->result().errorMessage));
        QVERIFY2(second->result().ok(), qPrintable(second->result().errorMessage));
        QCOMPARE(m_server->requests.size(), 2);

        // The second reply waits for the first one's request.
        const qint64 delta =
            m_server->requests.at(1).elapsedMs - m_server->requests.at(0).elapsedMs;
        QVERIFY2(delta >= 180, qPrintable(QStringLiteral("delta %1 ms").arg(delta)));
    }

    void abortActiveRequest()
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

    void timeout()
    {
        m_network->setTimeoutMs(300);
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Timeout);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("LibreTranslate did not respond within 0.3 s"));
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
        // that would contain the host and the port.
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
            response.location = QByteArray("http://127.0.0.1:") + QByteArray::number(otherPort)
                + "/translate";
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

    void failedMiddleChunkFailsTheWholeReply()
    {
        auto posts = std::make_shared<int>(0);
        m_server->handler = [posts](const FakeHttpServer::Request &request) {
            if (++*posts == 1)
                return FakeHttpServer::Response{200, echoResponse(request)};
            return FakeHttpServer::Response{500, {}};
        };
        const QString text = longText();

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("ru"), QStringLiteral("en"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QCOMPARE(reply->result().error, TranslationError::Network);
        // A failed chunk never leaves a partial translation behind.
        QVERIFY(reply->result().translatedText.isEmpty());
        QCOMPARE(m_server->requests.size(), 2);
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 2);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void logsAndMessagesAreClean()
    {
        auto posts = std::make_shared<int>(0);
        m_server->handler = [posts](const FakeHttpServer::Request &) {
            if (++*posts == 1) {
                return FakeHttpServer::Response{
                    200, translationResponse(QStringLiteral("TOPSECRET"))};
            }
            return FakeHttpServer::Response{
                500, QByteArray(R"({"error":"Invalid request: TOPSECRET is not supported"})")};
        };
        m_service->setApiKey(QStringLiteral("secret-key"));

        {
            LogCapture capture;

            std::unique_ptr<TranslationReply> success = translate(QStringLiteral("TOPSECRET"));
            QSignalSpy successFinished(success.get(), &TranslationReply::finished);
            QVERIFY(successFinished.wait(5000));
            QVERIFY2(success->result().ok(), qPrintable(success->result().errorMessage));

            std::unique_ptr<TranslationReply> failure = translate(QStringLiteral("TOPSECRET"));
            QSignalSpy failureFinished(failure.get(), &TranslationReply::finished);
            QVERIFY(failureFinished.wait(5000));
            QCOMPARE(failure->result().error, TranslationError::UnsupportedLanguage);
            verifyFixedMessage(failure->result().errorMessage);
        }

        QVERIFY(!g_logMessages.isEmpty());
        for (const QString &message : std::as_const(g_logMessages)) {
            QVERIFY2(!message.contains(QStringLiteral("TOPSECRET")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("secret-key")), qPrintable(message));
            QVERIFY2(!message.contains(QLatin1Char('<')), qPrintable(message));
            QVERIFY2(!message.contains(QLatin1Char('{')), qPrintable(message));
            QVERIFY2(!message.contains(QLatin1Char('}')), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("127.0.0.1")), qPrintable(message));
            QVERIFY2(!message.contains(kTranslatePath), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("libretranslate.com")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("is not supported")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("Invalid request")), qPrintable(message));
        }
    }

    void fallbackServiceWorks()
    {
        TranslationManager manager;
        auto *primary = new FakeService(QStringLiteral("primary"));
        auto *fallback = new LibreTranslateService(m_network.get());
        fallback->setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(m_server->port())));
        fallback->setRateLimitMs(0);
        manager.registerService(primary);
        manager.registerService(fallback);
        manager.setFallbackService(QStringLiteral("libretranslate"));
        QCOMPARE(manager.activeService(), static_cast<TranslationService *>(primary));

        m_server->handler = [](const FakeHttpServer::Request &) {
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
        QCOMPARE(reply->result().serviceId, QStringLiteral("libretranslate"));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hallo"));
    }

private:
    std::unique_ptr<TranslationReply> translate(
        const QString &text, const QString &source = QStringLiteral("en"),
        const QString &target = QStringLiteral("de"))
    {
        return std::unique_ptr<TranslationReply>(m_service->translate({text, source, target}));
    }

    // Waits for a reply the test expects to succeed.
    void finish(const std::unique_ptr<TranslationReply> &reply)
    {
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
    }

    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<LibreTranslateService> m_service;
};

QTEST_GUILESS_MAIN(TestLibreTranslateService)
#include "tst_libretranslateservice.moc"
