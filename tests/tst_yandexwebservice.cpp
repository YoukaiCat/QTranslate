#include "core/appinfo.h"
#include "core/languages.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/yandexwebservice.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
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

// The POST body as the endpoint sees it: keys and values percent-decoded.
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

QByteArray translationResponse(const QString &text, const QString &from = QString())
{
    QJsonObject first;
    first.insert(QStringLiteral("text"), text);
    if (!from.isEmpty())
        first.insert(QStringLiteral("from"), from);
    first.insert(QStringLiteral("to"), QStringLiteral("de"));
    return QJsonDocument(QJsonArray{first}).toJson(QJsonDocument::Compact);
}

// Answers every chunk with its own text, as a translation would.
QByteArray echoResponse(const QByteArray &body)
{
    return translationResponse(formValue(body, QStringLiteral("text")));
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

// Error messages are fixed: neither the text, nor the body, nor the URL may
// leak into them.
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

} // namespace

class TestYandexWebService : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
        m_network = std::make_unique<NetworkManager>();
        m_service = std::make_unique<YandexWebService>(m_network.get());
        m_service->setEndpoint(
            QUrl(QStringLiteral("http://127.0.0.1:%1/instaserp/translate").arg(m_server->port())));
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
        QCOMPARE(m_service->id(), QStringLiteral("yandex-web"));
        QCOMPARE(m_service->displayName(), QStringLiteral("Yandex"));
        QVERIFY(m_service->isConfigured());
        QVERIFY(m_service->requiresOptIn());

        const QStringList sources = m_service->sourceLanguages();
        const QStringList targets = m_service->targetLanguages();
        QVERIFY(!sources.isEmpty());
        QCOMPARE(sources, targets);
        QVERIFY(!sources.contains(Languages::autoCode()));
        for (const QString &code : {QStringLiteral("ru"), QStringLiteral("uk"),
                                    QStringLiteral("tr"), QStringLiteral("zh-Hans"),
                                    QStringLiteral("he"), QStringLiteral("nb"),
                                    QStringLiteral("pt-BR")}) {
            QVERIFY2(sources.contains(code), qPrintable(code));
        }
        for (const QString &code : {QStringLiteral("fil"), QStringLiteral("jv"),
                                    QStringLiteral("pt-PT")}) {
            QVERIFY2(!sources.contains(code), qPrintable(code));
        }

        QCOMPARE(YandexWebService::defaultEndpoint(),
                 QUrl(QStringLiteral("https://api.browser.yandex.com/instaserp/translate")));
    }

    void defaultPause()
    {
        YandexWebService service(m_network.get());
        QCOMPARE(service.rateLimitMs(), 750);
        QCOMPARE(service.endpoint(), YandexWebService::defaultEndpoint());
    }

    void sendsPostRequest()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.body), true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hello"));
        QCOMPARE(reply->result().serviceId, QStringLiteral("yandex-web"));

        QCOMPARE(m_server->requests.size(), 1);
        const FakeHttpServer::Request &request = m_server->requests.first();
        QCOMPARE(request.method, QByteArray("POST"));
        QCOMPARE(request.url.path(), QStringLiteral("/instaserp/translate"));
        QCOMPARE(request.headers.value("content-type"),
                 QByteArray("application/x-www-form-urlencoded"));
        QCOMPARE(request.headers.value("accept"), QByteArray("application/json"));
        QVERIFY(!request.headers.contains("authorization"));

        const QStringList expectedKeys = {
            QStringLiteral("text"),      QStringLiteral("brandID"),
            QStringLiteral("statLang"),  QStringLiteral("targetLang"),
            QStringLiteral("locale"),    QStringLiteral("clid"),
            QStringLiteral("disable"),   QStringLiteral("use_llm_srv"),
        };
        QCOMPARE(formKeys(request.body), expectedKeys);
        QCOMPARE(formValue(request.body, QStringLiteral("text")), QStringLiteral("Hello"));
        QCOMPARE(formValue(request.body, QStringLiteral("brandID")), QStringLiteral("int"));
        QCOMPARE(formValue(request.body, QStringLiteral("statLang")), QStringLiteral("de"));
        QCOMPARE(formValue(request.body, QStringLiteral("targetLang")), QStringLiteral("auto"));
        QCOMPARE(formValue(request.body, QStringLiteral("locale")), QStringLiteral("de"));
        QCOMPARE(formValue(request.body, QStringLiteral("clid")), QStringLiteral("2270494"));
        QCOMPARE(formValue(request.body, QStringLiteral("disable")), QStringLiteral("serp"));
        QCOMPARE(formValue(request.body, QStringLiteral("use_llm_srv")), QStringLiteral("0"));
    }

    void usesBrowserUserAgentOnThisRequestOnly()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.body), true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        QCOMPARE(m_server->requests.size(), 1);
        const QByteArray yandexAgent = m_server->requests.first().headers.value("user-agent");
        QCOMPARE(yandexAgent, YandexWebService::browserUserAgent().toLatin1());
        QVERIFY(yandexAgent != AppInfo::userAgent().toLatin1());

        // The common path every other service uses keeps the app User-Agent.
        const QNetworkRequest request = m_network->makeRequest(
            QUrl(QStringLiteral("http://127.0.0.1:%1/ping").arg(m_server->port())));
        QNetworkReply *plain = m_network->nam()->get(request);
        QSignalSpy plainFinished(plain, &QNetworkReply::finished);
        QVERIFY(plainFinished.wait(5000));
        plain->deleteLater();

        QCOMPARE(m_server->requests.size(), 2);
        QCOMPARE(m_server->requests.at(1).headers.value("user-agent"),
                 AppInfo::userAgent().toLatin1());
    }

    void encodesFormBody()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.body), true};
        };
        const QString text = QStringLiteral("Hello + мир 😀 C++ 1+1 & = %");
        std::unique_ptr<TranslationReply> reply(
            m_service->translate({text, QStringLiteral("en"), QStringLiteral("de")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        const QByteArray body = m_server->requests.first().body;
        QCOMPARE(formValue(body, QStringLiteral("text")), text);
        QVERIFY2(!body.contains(' '), qPrintable(QString::fromLatin1(body)));
        // Eight fields, so exactly seven separators.
        QCOMPARE(body.count('&'), 7);
        QVERIFY2(body.contains("%20"), qPrintable(QString::fromLatin1(body)));
        QVERIFY2(body.contains("%2B"), qPrintable(QString::fromLatin1(body)));
        QVERIFY2(body.contains("%26"), qPrintable(QString::fromLatin1(body)));
        QVERIFY2(body.contains("%3D"), qPrintable(QString::fromLatin1(body)));
        QVERIFY2(body.contains("%25"), qPrintable(QString::fromLatin1(body)));
        QVERIFY2(body.contains("%D0"), qPrintable(QString::fromLatin1(body))); // Cyrillic
        QVERIFY2(body.contains("%F0%9F"), qPrintable(QString::fromLatin1(body))); // emoji
    }

    void mapsLanguageCodes_data()
    {
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("code");

        QTest::newRow("English") << "en" << "en";
        QTest::newRow("Russian") << "ru" << "ru";
        QTest::newRow("Ukrainian") << "uk" << "uk";
        QTest::newRow("Turkish") << "tr" << "tr";
        QTest::newRow("German") << "de" << "de";
        QTest::newRow("Chinese simplified") << "zh-Hans" << "zh";
        QTest::newRow("Chinese traditional") << "zh-Hant" << "zh";
        QTest::newRow("Norwegian") << "nb" << "no";
        QTest::newRow("Portuguese Brazil") << "pt-BR" << "pt";
        QTest::newRow("Hebrew") << "he" << "he";
    }

    void mapsLanguageCodes()
    {
        QFETCH(QString, target);
        QFETCH(QString, code);

        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.body), true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), target}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        const QByteArray body = m_server->requests.first().body;
        QCOMPARE(formValue(body, QStringLiteral("statLang")), code);
        QCOMPARE(formValue(body, QStringLiteral("locale")), code);
        QCOMPARE(formValue(body, QStringLiteral("targetLang")), QStringLiteral("auto"));
    }

    void autoSourceIsAccepted()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hello"),
                                                                    QStringLiteral("en")),
                                            true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("en"));

        // The protocol has no source field: the source only gates the request.
        const QByteArray body = m_server->requests.first().body;
        QVERIFY(!formKeys(body).contains(QStringLiteral("sourceLang")));
        QVERIFY(!formKeys(body).contains(QStringLiteral("from")));
        QCOMPARE(formValue(body, QStringLiteral("targetLang")), QStringLiteral("auto"));

        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hello"),
                                                                    QStringLiteral("zh")),
                                            true};
        };
        std::unique_ptr<TranslationReply> chinese(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("ru")}));
        QSignalSpy chineseFinished(chinese.get(), &TranslationReply::finished);
        QVERIFY(chineseFinished.wait(5000));
        QVERIFY2(chinese->result().ok(), qPrintable(chinese->result().errorMessage));
        QCOMPARE(chinese->result().detectedSourceLang, QStringLiteral("zh-Hans"));
    }

    void unmappedLanguagesAreRejected_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("code");

        QTest::newRow("fil source") << "fil" << "en" << "fil";
        QTest::newRow("jv source") << "jv" << "en" << "jv";
        QTest::newRow("ceb source") << "ceb" << "en" << "ceb";
        QTest::newRow("zu source") << "zu" << "en" << "zu";
        QTest::newRow("fil target") << "en" << "fil" << "fil";
        QTest::newRow("jv target") << "en" << "jv" << "jv";
        QTest::newRow("zu target") << "en" << "zu" << "zu";
    }

    void unmappedLanguagesAreRejected()
    {
        QFETCH(QString, source);
        QFETCH(QString, target);
        QFETCH(QString, code);

        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), source, target}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::UnsupportedLanguage);
        QVERIFY2(reply->result().errorMessage.contains(
                     QStringLiteral("language \"%1\" is not supported").arg(code)),
                 qPrintable(reply->result().errorMessage));
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
        QCOMPARE(formValue(m_server->requests.first().body, QStringLiteral("text")),
                 QStringLiteral("Hello"));
        QCOMPARE(reply->result().translatedText, QStringLiteral(" \tX\r\n "));
    }

    void longTextIsSplitIntoChunks()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response{200, echoResponse(request.body), true};
        };
        const QString text = longText();
        const QStringList expectedParts = TextChunker::split(text, 5000);
        QVERIFY(expectedParts.size() > 2);
        // A chunk boundary must never split a "\r\n" or a surrogate pair.
        for (qsizetype i = 0; i + 1 < expectedParts.size(); ++i) {
            const QString &part = expectedParts.at(i);
            const QString &next = expectedParts.at(i + 1);
            QVERIFY2(!(part.endsWith(QLatin1Char('\r')) && next.startsWith(QLatin1Char('\n'))),
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
            const QString sent =
                formValue(m_server->requests.at(i).body, QStringLiteral("text"));
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
            return FakeHttpServer::Response{200, echoResponse(request.body), true};
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
            return FakeHttpServer::Response{200, echoResponse(request.body), true};
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

    void failedChunkFailsTheWholeReply()
    {
        auto calls = std::make_shared<int>(0);
        m_server->handler = [calls](const FakeHttpServer::Request &request) {
            if (++*calls == 1)
                return FakeHttpServer::Response{200, echoResponse(request.body), true};
            return FakeHttpServer::Response{500, "Too many requests", true};
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

    void httpErrors_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("429 rate limit") << 429 << QByteArray() << TranslationError::RateLimited;
        QTest::newRow("403 forbidden") << 403 << QByteArray() << TranslationError::RateLimited;
        QTest::newRow("500 server error") << 500 << QByteArray() << TranslationError::Network;
        QTest::newRow("503 unavailable") << 503 << QByteArray() << TranslationError::Network;
        QTest::newRow("413 too large") << 413 << QByteArray() << TranslationError::TooLong;
        QTest::newRow("400 bad request") << 400 << QByteArray() << TranslationError::BadResponse;
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
                200, "<!DOCTYPE html><html><body>captcha</body></html>", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::ServiceUnavailable);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("the endpoint returned an HTML page"));
        QVERIFY2(!reply->result().errorMessage.contains(QStringLiteral("DOCTYPE")),
                 qPrintable(reply->result().errorMessage));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void rateLimitBody()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, R"({"message":"Too many requests"})", true};
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

    void blockedBody_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("message");

        QTest::newRow("html") << QByteArray("<!DOCTYPE html><html><body>blocked</body></html>")
                              << "the endpoint returned an HTML page";
        QTest::newRow("json") << QByteArray(R"({"error":"Request blocked"})")
                              << "the endpoint is blocked or unavailable";
    }

    void blockedBody()
    {
        QFETCH(QByteArray, body);
        QFETCH(QString, message);

        const QByteArray responseBody = body;
        m_server->handler = [responseBody](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, responseBody, true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::ServiceUnavailable);
        QCOMPARE(reply->result().errorMessage, message);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void jsonError()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, R"({"error":"Something went wrong"})", true};
        };
        std::unique_ptr<TranslationReply> reply(m_service->translate(
            {QStringLiteral("Hello"), QStringLiteral("en"), QStringLiteral("ru")}));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::ServiceUnavailable);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("the endpoint returned an error"));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void brokenResponse_data()
    {
        QTest::addColumn<QByteArray>("body");

        QTest::newRow("malformed") << QByteArray(R"([{"text":)");
        QTest::newRow("empty array") << QByteArray("[]");
        QTest::newRow("missing text") << QByteArray(R"([{"from":"en"}])");
        QTest::newRow("JSON object") << QByteArray(R"({"text":"Hallo Welt!"})");
    }

    void brokenResponse()
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
                 QStringLiteral("Yandex did not respond within 0.3 s"));
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
                + QByteArray::number(otherPort) + "/instaserp/translate";
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

        QTest::newRow("503") << 503 << QByteArray("upstream failure");
        QTest::newRow("html") << 200
                              << QByteArray("<!DOCTYPE html><html><body>blocked</body></html>");
        QTest::newRow("rate limit") << 200 << QByteArray("Too many requests TOPSECRET");
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
        QCOMPARE(formValue(m_server->requests.first().body, QStringLiteral("text")),
                 QStringLiteral("TOPSECRET"));
        verifyFixedMessage(reply->result().errorMessage);
    }

private:
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<YandexWebService> m_service;
};

QTEST_GUILESS_MAIN(TestYandexWebService)
#include "tst_yandexwebservice.moc"
