#include "core/languages.h"
#include "core/proofreadingservice.h"
#include "core/settings.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/languagetoolservice.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QVector>

#include <functional>
#include <memory>
#include <utility>

Q_DECLARE_METATYPE(TranslationError)

namespace {

// A local HTTP server that records every request and lets a test delay or skip
// the response. The POST body is read by Content-Length.
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
        QByteArray setCookie; // Set-Cookie header; empty for a normal response
        int delayMs = 0;     // writes the response only after this pause

        Response() = default;
        Response(int responseStatus, QByteArray responseBody, bool responseRespond = true,
                 QByteArray responseLocation = {}, int responseDelayMs = 0)
            : status(responseStatus)
            , body(std::move(responseBody))
            , respond(responseRespond)
            , location(std::move(responseLocation))
            , delayMs(responseDelayMs)
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
        if (!response.setCookie.isEmpty())
            head += "Set-Cookie: " + response.setCookie + "\r\n";
        head += "Connection: close\r\n\r\n";
        if (response.delayMs > 0) {
            QTimer::singleShot(response.delayMs, socket, [socket, head, body = response.body] {
                socket->write(head + body);
                socket->disconnectFromHost();
            });
            return;
        }
        socket->write(head + response.body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QElapsedTimer m_clock;
};

// The body as the endpoint sees it: percent-escapes decoded back to text. A
// literal '+' stays '+'; the service never sends one as a space.
QString formValue(const QByteArray &body, const QString &key)
{
    for (const QByteArray &field : body.split('&')) {
        const qsizetype equals = field.indexOf('=');
        if (equals < 0)
            continue;
        if (QUrl::fromPercentEncoding(field.left(equals)) == key)
            return QUrl::fromPercentEncoding(field.mid(equals + 1));
    }
    return QString();
}

QStringList formKeys(const QByteArray &body)
{
    QStringList keys;
    for (const QByteArray &field : body.split('&')) {
        const qsizetype equals = field.indexOf('=');
        if (equals >= 0)
            keys << QUrl::fromPercentEncoding(field.left(equals));
    }
    return keys;
}

QJsonObject matchJson(int offset, int length, const QString &ruleId, const QString &categoryId,
                      const QString &message = QStringLiteral("issue"),
                      const QStringList &replacements = {})
{
    QJsonObject rule;
    rule.insert(QStringLiteral("id"), ruleId);
    QJsonObject category;
    category.insert(QStringLiteral("id"), categoryId);
    category.insert(QStringLiteral("name"), categoryId);
    rule.insert(QStringLiteral("category"), category);

    QJsonObject match;
    match.insert(QStringLiteral("message"), message);
    match.insert(QStringLiteral("offset"), offset);
    match.insert(QStringLiteral("length"), length);
    match.insert(QStringLiteral("rule"), rule);
    // The parser ignores context; the service must never copy it into a
    // message or a log line.
    QJsonObject context;
    context.insert(QStringLiteral("text"), message);
    context.insert(QStringLiteral("offset"), 0);
    context.insert(QStringLiteral("length"), length);
    match.insert(QStringLiteral("context"), context);
    QJsonArray values;
    for (const QString &value : replacements) {
        QJsonObject replacement;
        replacement.insert(QStringLiteral("value"), value);
        values.append(replacement);
    }
    match.insert(QStringLiteral("replacements"), values);
    return match;
}

// A minimal /v2/check response; `detected` mirrors language.detectedLanguage,
// which the service ignores because ProofreadingResult has no field for it.
QByteArray checkResponse(const QJsonArray &matches, const QString &detected = {})
{
    QJsonObject language;
    language.insert(QStringLiteral("name"), QStringLiteral("English"));
    language.insert(QStringLiteral("code"), QStringLiteral("en-US"));
    if (!detected.isEmpty())
        language.insert(QStringLiteral("detectedLanguage"), detected);

    QJsonObject root;
    root.insert(QStringLiteral("language"), language);
    root.insert(QStringLiteral("matches"), matches);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray emptyCheckResponse()
{
    return checkResponse(QJsonArray{});
}

// Three 20 000-unit chunks with sentence boundaries and an emoji that a naive
// cut could split.
QString longText()
{
    QString text = QStringLiteral("  ");
    int sentence = 0;
    while (text.size() < 45000) {
        text += QStringLiteral("Sentence %1 tells a story about the weather, nature and 😀. ")
                    .arg(++sentence);
        if (sentence % 7 == 0)
            text += QStringLiteral("\n\n");
    }
    return text;
}

// Error messages are fixed: neither the text, an issue message, a replacement,
// the URL nor the body may leak into them.
void verifyFixedMessage(const QString &message)
{
    QVERIFY(!message.isEmpty());
    QVERIFY2(!message.contains(QStringLiteral("TOPSECRET")), qPrintable(message));
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
            QStringLiteral("qtranslate.service.languagetool.debug=true"));
    }

    ~LogCapture()
    {
        QLoggingCategory::setFilterRules(QString());
        qInstallMessageHandler(previous);
    }
};

} // namespace

class TestLanguageToolService : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
        m_network = std::make_unique<NetworkManager>();
        m_service = std::make_unique<LanguageToolService>(m_network.get());

        AppSettings settings;
        settings.proofreading.endpoint = localEndpoint();
        m_service->applySettings(settings);
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
        QCOMPARE(m_service->id(), QStringLiteral("languagetool"));
        QCOMPARE(m_service->displayName(), QStringLiteral("LanguageTool"));
        QVERIFY(m_service->isConfigured());
        QVERIFY(!m_service->isLocal());
        QVERIFY(m_service->supportsAutoLanguage());

        const QStringList languages = m_service->languages();
        QVERIFY(!languages.isEmpty());
        QVERIFY(!languages.contains(Languages::autoCode()));
        for (const QString &code : {QStringLiteral("ru"), QStringLiteral("en"),
                                    QStringLiteral("de"), QStringLiteral("zh-Hans"),
                                    QStringLiteral("fil"), QStringLiteral("pt-PT")}) {
            QVERIFY2(languages.contains(code), qPrintable(code));
        }
        QVERIFY(!languages.contains(QStringLiteral("zh-Hant")));
        QVERIFY(!languages.contains(QStringLiteral("hi")));

        QCOMPARE(LanguageToolService::defaultEndpoint(),
                 QUrl(QStringLiteral("https://api.languagetool.org/v2/check")));
    }

    void endpointFallback()
    {
        LanguageToolService service(m_network.get());
        QCOMPARE(service.endpoint(), LanguageToolService::defaultEndpoint());

        const QUrl local(QStringLiteral("http://127.0.0.1:%1/v2/check").arg(m_server->port()));
        service.setEndpoint(local);
        QCOMPARE(service.endpoint(), local);

        service.setEndpoint(QUrl());
        QCOMPARE(service.endpoint(), LanguageToolService::defaultEndpoint());

        service.setEndpoint(QUrl(QStringLiteral("ftp://example.invalid/v2/check")));
        QCOMPARE(service.endpoint(), LanguageToolService::defaultEndpoint());

        service.setEndpoint(QUrl(QStringLiteral("not a url")));
        QCOMPARE(service.endpoint(), LanguageToolService::defaultEndpoint());
    }

    void pauses()
    {
        LanguageToolService fresh(m_network.get());
        QCOMPARE(fresh.rateLimitMs(), 3000);

        AppSettings settings;
        settings.proofreading.endpoint = localEndpoint();
        fresh.applySettings(settings);
        QCOMPARE(fresh.rateLimitMs(), 500);
        QCOMPARE(fresh.endpoint(), QUrl(localEndpoint()));

        settings.proofreading.endpoint.clear();
        fresh.applySettings(settings);
        QCOMPARE(fresh.rateLimitMs(), 3000);
        QCOMPARE(fresh.endpoint(), LanguageToolService::defaultEndpoint());

        settings.proofreading.endpoint =
            QStringLiteral("https://api.languagetool.org/v2/check");
        fresh.applySettings(settings);
        QCOMPARE(fresh.rateLimitMs(), 3000);

        settings.proofreading.endpoint = QStringLiteral("ftp://example.invalid/v2/check");
        fresh.applySettings(settings);
        QCOMPARE(fresh.rateLimitMs(), 3000);
        QCOMPARE(fresh.endpoint(), LanguageToolService::defaultEndpoint());

        // setEndpoint() is the test seam only: it never changes the pause.
        settings.proofreading.endpoint = QStringLiteral("https://api.languagetool.org/v2/check");
        fresh.applySettings(settings);
        fresh.setEndpoint(localEndpoint());
        QCOMPARE(fresh.rateLimitMs(), 3000);
    }

    void applySettingsKeepsActiveRequest()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };

        auto reply = check(QStringLiteral("Hello"));
        QTRY_COMPARE(m_server->requests.size(), 1);
        QCOMPARE(m_server->requests.first().url.path(), QStringLiteral("/v2/check"));

        FakeHttpServer other;
        AppSettings settings;
        settings.proofreading.endpoint =
            QStringLiteral("http://127.0.0.1:%1/v2/check").arg(other.port());
        m_service->applySettings(settings);

        // The in-flight request keeps the endpoint it copied.
        QVERIFY(!reply->isFinished());
        QCOMPARE(m_service->endpoint(), QUrl(settings.proofreading.endpoint));
        QCOMPARE(m_server->requests.size(), 1);
        QCOMPARE(other.requests.size(), 0);

        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        reply->abort();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(reply->result().error, TranslationError::Cancelled);
    }

    void sendsPostForm()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, emptyCheckResponse(), true};
        };

        auto reply = check(QStringLiteral("Hello, world"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().serviceId, QStringLiteral("languagetool"));

        QCOMPARE(m_server->requests.size(), 1);
        const FakeHttpServer::Request &request = m_server->requests.first();
        QCOMPARE(request.method, QByteArray("POST"));
        QCOMPARE(request.url.path(), QStringLiteral("/v2/check"));
        QCOMPARE(request.headers.value("content-type"),
                 QByteArray("application/x-www-form-urlencoded"));
        QCOMPARE(request.headers.value("accept"), QByteArray("application/json"));
        QVERIFY(!request.headers.contains("authorization"));
        QVERIFY(!request.headers.contains("cookie"));

        const QStringList keys = formKeys(request.body);
        QCOMPARE(keys, (QStringList{QStringLiteral("text"), QStringLiteral("language"),
                                    QStringLiteral("enabledOnly")}));
        QCOMPARE(formValue(request.body, QStringLiteral("text")), QStringLiteral("Hello, world"));
        QCOMPARE(formValue(request.body, QStringLiteral("language")), QStringLiteral("en-US"));
        QCOMPARE(formValue(request.body, QStringLiteral("enabledOnly")), QStringLiteral("false"));
        QVERIFY(formValue(request.body, QStringLiteral("apiKey")).isEmpty());
        QVERIFY(formValue(request.body, QStringLiteral("username")).isEmpty());
    }

    void cookiesAreIsolated()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            FakeHttpServer::Response response{200, emptyCheckResponse(), true};
            response.setCookie = QByteArrayLiteral("lt=TOPSECRET; Path=/");
            return response;
        };

        QNetworkAccessManager *nam = m_network->nam();
        if (!nam->cookieJar())
            nam->setCookieJar(new QNetworkCookieJar(nam));
        const QUrl origin(QStringLiteral("http://127.0.0.1:%1/").arg(m_server->port()));
        QNetworkCookie preloaded(QByteArray("preloaded"), QByteArray("TOPSECRET"));
        preloaded.setDomain(QStringLiteral("127.0.0.1"));
        preloaded.setPath(QStringLiteral("/"));
        QVERIFY(nam->cookieJar()->setCookiesFromUrl({preloaded}, origin));

        auto reply = check(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        QCOMPARE(m_server->requests.size(), 1);
        const QByteArray cookie = m_server->requests.first().headers.value("cookie");
        QVERIFY2(cookie.isEmpty(), qPrintable(cookie));

        // The manual save attribute kept the server cookie out of the shared jar.
        const QList<QNetworkCookie> stored = nam->cookieJar()->cookiesForUrl(origin);
        for (const QNetworkCookie &entry : stored)
            QVERIFY2(entry.name() != "lt", qPrintable(entry.name()));
    }

    void encodesTheFormBody()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, emptyCheckResponse(), true};
        };

        const QString text = QStringLiteral("Hello + world & = % мир 😀 100%");
        auto reply = check(text);
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        QCOMPARE(m_server->requests.size(), 1);
        const QByteArray body = m_server->requests.first().body;
        QCOMPARE(formValue(body, QStringLiteral("text")), text);
        // A raw space or '+' would be read as a space by a form decoder.
        QVERIFY2(!body.contains(' '), body.constData());
        QVERIFY2(!body.contains('+'), body.constData());
        QVERIFY2(body.contains("%20"), body.constData());
        QVERIFY2(body.contains("%2B"), body.constData());
        QVERIFY2(body.contains("%26"), body.constData()); // &
        QVERIFY2(body.contains("%3D"), body.constData()); // =
        QVERIFY2(body.contains("%25"), body.constData()); // %
        QVERIFY2(body.contains("%D0"), body.constData()); // Cyrillic
        QVERIFY2(body.contains("%F0%9F"), body.constData()); // emoji
    }

    void mapsLanguageCodes_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("serviceCode");

        QTest::newRow("Russian") << "ru" << "ru-RU";
        QTest::newRow("English") << "en" << "en-US";
        QTest::newRow("German") << "de" << "de-DE";
        QTest::newRow("Chinese simplified") << "zh-Hans" << "zh-CN";
        QTest::newRow("Filipino") << "fil" << "tl-PH";
        QTest::newRow("Portuguese Portugal") << "pt-PT" << "pt-PT";
    }

    void mapsLanguageCodes()
    {
        QFETCH(QString, code);
        QFETCH(QString, serviceCode);

        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, emptyCheckResponse(), true};
        };

        auto reply = check(QStringLiteral("Hello"), code);
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(formValue(m_server->requests.first().body, QStringLiteral("language")),
                 serviceCode);
    }

    void rejectsUnsupportedLanguages_data()
    {
        QTest::addColumn<QString>("code");

        QTest::newRow("Chinese traditional") << "zh-Hant";
        QTest::newRow("Hindi") << "hi";
        QTest::newRow("Unknown") << "xx";
    }

    void rejectsUnsupportedLanguages()
    {
        QFETCH(QString, code);

        auto reply = check(QStringLiteral("Hello"), code);
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::UnsupportedLanguage);
        QCOMPARE(reply->result().serviceId, QStringLiteral("languagetool"));
        QVERIFY(reply->result().issues.isEmpty());
        QVERIFY2(reply->result().errorMessage.contains(code),
                 qPrintable(reply->result().errorMessage));
        verifyFixedMessage(reply->result().errorMessage);
        QCOMPARE(m_server->requests.size(), 0);
    }

    void emptyTextIsRefused_data()
    {
        QTest::addColumn<QString>("text");

        QTest::newRow("empty") << QString();
        QTest::newRow("spaces") << QStringLiteral("   ");
        QTest::newRow("whitespace lines") << QStringLiteral("\n\t \r\n");
    }

    void emptyTextIsRefused()
    {
        QFETCH(QString, text);

        auto reply = check(text);
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::EmptyText);
        QCOMPARE(reply->result().serviceId, QStringLiteral("languagetool"));
        QCOMPARE(m_server->requests.size(), 0);
    }

    void autoRequestIsSentAsIs()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200,
                checkResponse({matchJson(0, 5, QStringLiteral("R"), QStringLiteral("GRAMMAR"))},
                              QStringLiteral("de")),
                true};
        };

        auto reply = check(QStringLiteral("Hello"), Languages::autoCode());
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().serviceId, QStringLiteral("languagetool"));

        const QByteArray body = m_server->requests.first().body;
        QCOMPARE(formValue(body, QStringLiteral("language")), QStringLiteral("auto"));
        QCOMPARE(reply->result().issues.size(), 1);
        QCOMPARE(reply->result().issues.first().offset, 0);
        // language.detectedLanguage was "de"; the result has no field for it,
        // so nothing about the detection is exposed.
    }

    void autoDetectionFailure_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("message");

        QTest::newRow("HTTP 400")
            << 400 << QByteArray("Error: TOPSECRET detection failed") << QStringLiteral("HTTP 400");
        QTest::newRow("plain text")
            << 200 << QByteArray("TOPSECRET is not a check response")
            << QStringLiteral("response is not a LanguageTool response");
        QTest::newRow("JSON array")
            << 200 << QByteArray("[1,2,3]")
            << QStringLiteral("response is not a LanguageTool response");
        QTest::newRow("object without matches")
            << 200 << QByteArray(R"({"error":"TOPSECRET"})")
            << QStringLiteral("no matches array in the response");
    }

    void autoDetectionFailure()
    {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(QString, message);

        const int responseStatus = status;
        const QByteArray responseBody = body;
        m_server->handler = [responseStatus, responseBody](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{responseStatus, responseBody, true};
        };

        const QString text = QStringLiteral("TOPSECRET hello");
        auto reply = check(text, Languages::autoCode());
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::BadResponse);
        QCOMPARE(reply->result().errorMessage, message);
        QVERIFY(reply->result().issues.isEmpty());
        QVERIFY(!reply->result().errorMessage.contains(QStringLiteral("TOPSECRET")));
        QVERIFY(!reply->result().errorMessage.contains(QStringLiteral("hello")));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void chunksAreSentWithShiftedOffsets()
    {
        const QString text = longText();
        const QVector<TextChunker::Part> parts = TextChunker::splitWithOffsets(text, 20000);
        QCOMPARE(parts.size(), 3);

        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200,
                checkResponse({matchJson(0, 1, QStringLiteral("R"), QStringLiteral("GRAMMAR"))}),
                true};
        };

        auto reply = check(text);
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(10000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(m_server->requests.size(), parts.size());
        QCOMPARE(reply->result().issues.size(), parts.size());

        QString joined;
        for (qsizetype i = 0; i < parts.size(); ++i) {
            const QString chunk =
                formValue(m_server->requests.at(i).body, QStringLiteral("text"));
            QCOMPARE(chunk, parts.at(i).text);
            QVERIFY(!chunk.isEmpty());
            QVERIFY(chunk.size() <= 20000);
            QVERIFY(!chunk.front().isLowSurrogate());
            QVERIFY(!chunk.back().isHighSurrogate());
            joined += chunk;

            QCOMPARE(reply->result().issues.at(i).offset, parts.at(i).offset);
            QCOMPARE(reply->result().issues.at(i).length, 1);
        }
        QCOMPARE(joined, text);
        for (qsizetype i = 1; i < reply->result().issues.size(); ++i) {
            QVERIFY(reply->result().issues.at(i - 1).offset
                    < reply->result().issues.at(i).offset);
        }
        const ProofreadingIssue &last = reply->result().issues.last();
        QVERIFY(last.offset + last.length <= text.size());
    }

    void duplicateIssuesAreRemoved()
    {
        const QJsonObject issue =
            matchJson(2, 3, QStringLiteral("R"), QStringLiteral("GRAMMAR"),
                      QStringLiteral("same message"), {QStringLiteral("fix")});
        m_server->handler = [issue](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200,
                checkResponse({issue, issue,
                               matchJson(0, 1, QStringLiteral("R2"), QStringLiteral("TYPOS"))}),
                true};
        };

        auto reply = check(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().issues.size(), 2);
        // Sorted by offset: the TYPOS issue at 0 comes first.
        QCOMPARE(reply->result().issues.at(0).offset, 0);
        QCOMPARE(reply->result().issues.at(1).offset, 2);
        QCOMPARE(reply->result().issues.at(1).message, QStringLiteral("same message"));
    }

    void issueOutsideTheChunkIsBadResponse()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200,
                checkResponse({matchJson(10, 5, QStringLiteral("R"),
                                         QStringLiteral("GRAMMAR"))}),
                true};
        };

        auto reply = check(QStringLiteral("abc"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::BadResponse);
        QCOMPARE(reply->result().errorMessage, QStringLiteral("a match has an invalid range"));
        QVERIFY(reply->result().issues.isEmpty());
        verifyFixedMessage(reply->result().errorMessage);
    }

    void failedChunkFailsTheWholeReply()
    {
        const QString text = longText();
        QCOMPARE(TextChunker::splitWithOffsets(text, 20000).size(), 3);

        auto calls = std::make_shared<int>(0);
        m_server->handler = [calls](const FakeHttpServer::Request &) {
            if (++*calls == 1) {
                return FakeHttpServer::Response{
                    200,
                    checkResponse({matchJson(0, 1, QStringLiteral("R"),
                                             QStringLiteral("GRAMMAR"))}),
                    true};
            }
            return FakeHttpServer::Response{500, QByteArray("server error"), true};
        };

        auto reply = check(text);
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(10000));
        QCOMPARE(reply->result().error, TranslationError::Network);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("the LanguageTool server failed"));
        QVERIFY(reply->result().issues.isEmpty());
        QCOMPARE(m_server->requests.size(), 2);
        QTest::qWait(100);
        QCOMPARE(m_server->requests.size(), 2); // no third chunk
        verifyFixedMessage(reply->result().errorMessage);
    }

    void chunksArePaced()
    {
        m_service->setRateLimitMs(100);
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, emptyCheckResponse(), true};
        };

        const QString text = longText();
        auto reply = check(text);
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(10000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(m_server->requests.size(), 3);
        for (qsizetype i = 1; i < m_server->requests.size(); ++i) {
            const qint64 delta =
                m_server->requests.at(i).elapsedMs - m_server->requests.at(i - 1).elapsedMs;
            QVERIFY2(delta >= 90, qPrintable(QStringLiteral("delta %1 ms").arg(delta)));
        }
    }

    void finishesWithoutTrailingPause()
    {
        // A single chunk is answered immediately; the reply must finish without
        // waiting rateLimitMs() after the last response.
        m_service->setRateLimitMs(1500);
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, emptyCheckResponse(), true};
        };

        auto reply = check(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(1000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
    }

    void categoryFilter()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200,
                checkResponse({
                    matchJson(0, 1, QStringLiteral("SPELL"), QStringLiteral("TYPOS")),
                    matchJson(1, 1, QStringLiteral("GRAM"), QStringLiteral("GRAMMAR")),
                    matchJson(2, 1, QStringLiteral("PUNCT"), QStringLiteral("PUNCTUATION")),
                    matchJson(3, 1, QStringLiteral("STYLE"), QStringLiteral("CASING")),
                }),
                true};
        };

        auto filtered = check(QStringLiteral("abcd"), QStringLiteral("en"),
                              {QStringLiteral("spelling"), QStringLiteral("punctuation")});
        QSignalSpy filteredFinished(filtered.get(), &ProofreadingReply::finished);
        QVERIFY(filteredFinished.wait(5000));
        QVERIFY2(filtered->result().ok(), qPrintable(filtered->result().errorMessage));
        QCOMPARE(filtered->result().issues.size(), 2);
        QCOMPARE(filtered->result().issues.at(0).category, QStringLiteral("spelling"));
        QCOMPARE(filtered->result().issues.at(0).offset, 0);
        QCOMPARE(filtered->result().issues.at(1).category, QStringLiteral("punctuation"));
        QCOMPARE(filtered->result().issues.at(1).offset, 2);
        QVERIFY2(!m_server->requests.last().body.contains("enabledCategories"),
                 m_server->requests.last().body.constData());

        auto all = check(QStringLiteral("abcd"));
        QSignalSpy allFinished(all.get(), &ProofreadingReply::finished);
        QVERIFY(allFinished.wait(5000));
        QVERIFY2(all->result().ok(), qPrintable(all->result().errorMessage));
        QCOMPARE(all->result().issues.size(), 4);
        QStringList categories;
        for (const ProofreadingIssue &issue : all->result().issues) {
            QCOMPARE(issue.offset, categories.size()); // the filter never shifts offsets
            categories << issue.category;
        }
        QCOMPARE(categories,
                 (QStringList{QStringLiteral("spelling"), QStringLiteral("grammar"),
                              QStringLiteral("punctuation"), QStringLiteral("style")}));
        QVERIFY2(!m_server->requests.last().body.contains("enabledCategories"),
                 m_server->requests.last().body.constData());
    }

    void httpErrors_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<TranslationError>("error");
        QTest::addColumn<QString>("message");

        QTest::newRow("400")
            << 400 << TranslationError::BadResponse << QStringLiteral("HTTP 400");
        QTest::newRow("401")
            << 401 << TranslationError::Auth
            << QStringLiteral("the endpoint rejected the request");
        QTest::newRow("403")
            << 403 << TranslationError::Auth
            << QStringLiteral("the endpoint rejected the request");
        QTest::newRow("413")
            << 413 << TranslationError::TooLong
            << QStringLiteral("the text is too long for LanguageTool");
        QTest::newRow("429")
            << 429 << TranslationError::RateLimited
            << QStringLiteral("the endpoint reported a rate limit");
        QTest::newRow("500")
            << 500 << TranslationError::Network
            << QStringLiteral("the LanguageTool server failed");
        QTest::newRow("503")
            << 503 << TranslationError::Network
            << QStringLiteral("the LanguageTool server failed");
    }

    void httpErrors()
    {
        QFETCH(int, status);
        QFETCH(TranslationError, error);
        QFETCH(QString, message);

        const int responseStatus = status;
        m_server->handler = [responseStatus](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{responseStatus,
                                            QByteArray("Error: TOPSECRET body"), true};
        };

        auto reply = check(QStringLiteral("TOPSECRET text"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, error);
        QCOMPARE(reply->result().errorMessage, message);
        QVERIFY(reply->result().issues.isEmpty());
        verifyFixedMessage(reply->result().errorMessage);
    }

    void htmlBodyIsServiceUnavailable()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200, QByteArray("<!DOCTYPE html><html><body>TOPSECRET</body></html>"), true};
        };

        auto reply = check(QStringLiteral("TOPSECRET text"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::ServiceUnavailable);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("the endpoint returned an HTML page"));
        QVERIFY(reply->result().issues.isEmpty());
        verifyFixedMessage(reply->result().errorMessage);
    }

    void malformedBody_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("message");

        QTest::newRow("plain text")
            << QByteArray("TOPSECRET") << QStringLiteral("response is not a LanguageTool response");
        QTest::newRow("JSON array")
            << QByteArray("[1]") << QStringLiteral("response is not a LanguageTool response");
        QTest::newRow("object without matches")
            << QByteArray(R"({"error":"TOPSECRET"})")
            << QStringLiteral("no matches array in the response");
        QTest::newRow("non-object match")
            << QByteArray(R"({"matches":[1]})")
            << QStringLiteral("a match is not an object");
        QTest::newRow("match without message")
            << QByteArray(R"({"matches":[{"offset":0,"length":1,"rule":{}}]})")
            << QStringLiteral("a match has no message");
    }

    void malformedBody()
    {
        QFETCH(QByteArray, body);
        QFETCH(QString, message);

        const QByteArray responseBody = body;
        m_server->handler = [responseBody](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, responseBody, true};
        };

        auto reply = check(QStringLiteral("TOPSECRET text"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::BadResponse);
        QCOMPARE(reply->result().errorMessage, message);
        QVERIFY(reply->result().issues.isEmpty());
        verifyFixedMessage(reply->result().errorMessage);
    }

    void timeout()
    {
        m_network->setTimeoutMs(300);
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };

        auto reply = check(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Timeout);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("LanguageTool did not respond within 0.3 s"));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void closedServerIsNetwork()
    {
        m_server->close();

        auto reply = check(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
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
                + QByteArray::number(otherPort) + "/v2/check";
            return response;
        };

        auto reply = check(QStringLiteral("TOPSECRET text"));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY(!reply->result().ok());
        QVERIFY(reply->result().issues.isEmpty());
        QTest::qWait(100);
        QCOMPARE(other.requests.size(), 0);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void queuedCheckStartsAfterTheActiveOne()
    {
        m_service->setRateLimitMs(100);
        auto calls = std::make_shared<int>(0);
        m_server->handler = [calls](const FakeHttpServer::Request &) {
            FakeHttpServer::Response response{200, emptyCheckResponse(), true};
            if (++*calls == 1)
                response.delayMs = 300;
            return response;
        };

        auto first = check(QStringLiteral("First"));
        auto second = check(QStringLiteral("Second"));
        QSignalSpy firstFinished(first.get(), &ProofreadingReply::finished);
        QSignalSpy secondFinished(second.get(), &ProofreadingReply::finished);
        QVERIFY(firstFinished.wait(5000));
        QVERIFY(secondFinished.wait(5000));
        QVERIFY2(first->result().ok(), qPrintable(first->result().errorMessage));
        QVERIFY2(second->result().ok(), qPrintable(second->result().errorMessage));
        QCOMPARE(m_server->requests.size(), 2);

        const qint64 delta =
            m_server->requests.at(1).elapsedMs - m_server->requests.at(0).elapsedMs;
        QVERIFY2(delta >= 250, qPrintable(QStringLiteral("delta %1 ms").arg(delta)));
    }

    void queuedCheckAbortedBeforeStartSendsNothing()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            FakeHttpServer::Response response{200, emptyCheckResponse(), true};
            response.delayMs = 300;
            return response;
        };

        auto first = check(QStringLiteral("First"));
        auto second = check(QStringLiteral("Second"));
        second->abort();
        QCOMPARE(second->result().error, TranslationError::Cancelled);

        QSignalSpy firstFinished(first.get(), &ProofreadingReply::finished);
        QVERIFY(firstFinished.wait(5000));
        QVERIFY2(first->result().ok(), qPrintable(first->result().errorMessage));
        QTest::qWait(100);
        QCOMPARE(m_server->requests.size(), 1);
    }

    void abortedBeforeQueuedSendSendsNothing()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, emptyCheckResponse(), true};
        };

        auto reply = check(QStringLiteral("Hello"));
        // start() ran synchronously in the queue, but send() is queued and must
        // see the finished reply and return without a request.
        reply->abort();
        QCOMPARE(reply->result().error, TranslationError::Cancelled);
        QTest::qWait(100);
        QCOMPARE(m_server->requests.size(), 0);
    }

    void activeCheckAbortStopsTheRequest()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };

        auto reply = check(QStringLiteral("Hello"));
        QTRY_COMPARE(m_server->requests.size(), 1);
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        reply->abort();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(reply->result().error, TranslationError::Cancelled);
        QTest::qWait(100);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(m_server->requests.size(), 1);
    }

    void deletedQueuedCheckDoesNotSend()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            FakeHttpServer::Response response{200, emptyCheckResponse(), true};
            response.delayMs = 300;
            return response;
        };

        auto first = check(QStringLiteral("First"));
        ProofreadingReply *second = m_service->check(
            {QStringLiteral("Second"), QStringLiteral("en"), {}});
        // Deleted while still queued: the raw pointer captured by isDone() and
        // start() must never be dereferenced.
        delete second;

        QSignalSpy firstFinished(first.get(), &ProofreadingReply::finished);
        QVERIFY(firstFinished.wait(5000));
        QVERIFY2(first->result().ok(), qPrintable(first->result().errorMessage));
        QTest::qWait(100);
        QCOMPARE(m_server->requests.size(), 1);
    }

    void serviceDestroyedMidRequest()
    {
        auto service = std::make_unique<LanguageToolService>(m_network.get());
        AppSettings settings;
        settings.proofreading.endpoint = localEndpoint();
        service->applySettings(settings);
        service->setRateLimitMs(0);

        m_server->handler = [](const FakeHttpServer::Request &) {
            FakeHttpServer::Response response{200, emptyCheckResponse(), true};
            response.delayMs = 300;
            return response;
        };

        auto reply = std::unique_ptr<ProofreadingReply>(
            service->check({QStringLiteral("Hello"), QStringLiteral("en"), {}}));
        QSignalSpy finished(reply.get(), &ProofreadingReply::finished);
        QTRY_COMPARE(m_server->requests.size(), 1);
        service.reset();

        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Network);
        QCOMPARE(reply->result().errorMessage, QStringLiteral("the service is not available"));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void messagesAndLogsAreClean()
    {
        const QByteArray successBody = checkResponse(
            {matchJson(0, 5, QStringLiteral("TOPSECRET_RULE"), QStringLiteral("TYPOS"),
                       QStringLiteral("TOPSECRET issue message"),
                       {QStringLiteral("TOPSECRET replacement")})});
        auto calls = std::make_shared<int>(0);
        m_server->handler = [calls, successBody](const FakeHttpServer::Request &) {
            if (++*calls == 1)
                return FakeHttpServer::Response{200, successBody, true};
            return FakeHttpServer::Response{
                400, QByteArray("Error: TOPSECRET echoed back in the body"), true};
        };

        {
            LogCapture capture;

            const QString text = QStringLiteral("TOPSECRET text");
            auto success = check(text);
            QSignalSpy successFinished(success.get(), &ProofreadingReply::finished);
            QVERIFY(successFinished.wait(5000));
            QVERIFY2(success->result().ok(), qPrintable(success->result().errorMessage));
            QCOMPARE(success->result().issues.size(), 1);

            auto failure = check(text);
            QSignalSpy failureFinished(failure.get(), &ProofreadingReply::finished);
            QVERIFY(failureFinished.wait(5000));
            QCOMPARE(failure->result().error, TranslationError::BadResponse);
            QCOMPARE(failure->result().errorMessage, QStringLiteral("HTTP 400"));
            verifyFixedMessage(failure->result().errorMessage);
        }

        QVERIFY(!g_logMessages.isEmpty());
        for (const QString &message : std::as_const(g_logMessages)) {
            QVERIFY2(!message.contains(QStringLiteral("TOPSECRET")), qPrintable(message));
            QVERIFY2(!message.contains(QLatin1Char('<')), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("://")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("127.0.0.1")), qPrintable(message));
        }

        // No credential of any kind is sent.
        for (const FakeHttpServer::Request &request : std::as_const(m_server->requests)) {
            const QStringList keys = formKeys(request.body);
            QVERIFY(!keys.contains(QStringLiteral("apiKey")));
            QVERIFY(!keys.contains(QStringLiteral("username")));
            QVERIFY(!request.headers.contains("authorization"));
            QVERIFY(!request.headers.contains("cookie"));
        }
    }

private:
    QString localEndpoint() const
    {
        return QStringLiteral("http://127.0.0.1:%1/v2/check").arg(m_server->port());
    }

    std::unique_ptr<ProofreadingReply> check(const QString &text,
                                             const QString &language = QStringLiteral("en"),
                                             const QStringList &categories = {})
    {
        return std::unique_ptr<ProofreadingReply>(
            m_service->check({text, language, categories}));
    }

    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<LanguageToolService> m_service;
};

QTEST_GUILESS_MAIN(TestLanguageToolService)
#include "tst_languagetoolservice.moc"
