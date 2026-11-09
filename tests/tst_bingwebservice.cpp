#include "core/appinfo.h"
#include "core/languages.h"
#include "core/settings.h"
#include "core/textchunker.h"
#include "net/networkmanager.h"
#include "services/bingwebservice.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
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

const QString kTokenPath = QStringLiteral("/translator");
const QString kPostPath = QStringLiteral("/ttranslatev3");
const QString kToken = QStringLiteral("fake0token0value0for0tests0");
const QString kSecondToken = QStringLiteral("refreshed0token0value0for0tests0");
const QString kKey = QStringLiteral("1750000000123");
const QString kIg = QStringLiteral("0123456789ABCDEF0123456789ABCDEF");
const QString kIid = QStringLiteral("translator.5023");
const QString kMuid = QStringLiteral("00112233445566778899AABBCCDDEEFF");

// A synthetic translator page with the markers the parser needs; no live page,
// token or cookie. The optional MUID is omitted when `muid` is empty.
QByteArray tokenPage(const QString &token = kToken, const QString &muid = kMuid,
                     int lifetimeMs = 3600000)
{
    QByteArray html = "<!DOCTYPE html><html><body>"
        "<div data-iid=\"" + kIid.toLatin1() + "\"></div>"
        "<script>var _G = {IG:\"" + kIg.toLatin1() + "\"};"
        "params_AbusePreventionHelper = [" + kKey.toLatin1() + ",\"" + token.toLatin1() + "\","
        + QByteArray::number(lifetimeMs) + "];";
    if (!muid.isEmpty())
        html += "var _H = {\"muid\":\"" + muid.toLatin1() + "\"};";
    html += "</script><p>token page body</p></body></html>";
    return html;
}

// The live JSON shape: an array whose first object carries the translations and
// the detected language.
QByteArray translationResponse(const QString &text, const QString &detected = QStringLiteral("en"))
{
    QJsonObject entry;
    entry.insert(QStringLiteral("text"), text);
    entry.insert(QStringLiteral("to"), QStringLiteral("de"));

    QJsonObject first;
    first.insert(QStringLiteral("translations"), QJsonArray{entry});
    first.insert(QStringLiteral("usedLLM"), true);
    if (!detected.isEmpty()) {
        QJsonObject language;
        language.insert(QStringLiteral("language"), detected);
        first.insert(QStringLiteral("detectedLanguage"), language);
    }
    return QJsonDocument(QJsonArray{first}).toJson(QJsonDocument::Compact);
}

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

// Answers every chunk with its own text, as a translation would.
QByteArray echoResponse(const QByteArray &body)
{
    return translationResponse(formValue(body, QStringLiteral("text")));
}

QString queryValue(const QUrl &url, const QString &key)
{
    return QUrlQuery(url).queryItemValue(key, QUrl::FullyDecoded);
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

// Error messages are fixed: neither the text, nor the page, nor the body nor
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
    QVERIFY2(!message.contains(kKey), qPrintable(message));
    QVERIFY2(!message.contains(kToken), qPrintable(message));
    QVERIFY2(!message.contains(kMuid), qPrintable(message));
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
            QStringLiteral("qtranslate.service.bing-web.debug=true"));
    }

    ~LogCapture()
    {
        QLoggingCategory::setFilterRules(QString());
        qInstallMessageHandler(previous);
    }
};

} // namespace

class TestBingWebService : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
        m_network = std::make_unique<NetworkManager>();
        if (!m_network->nam()->cookieJar())
            m_network->nam()->setCookieJar(new QNetworkCookieJar(m_network->nam()));
        m_service = std::make_unique<BingWebService>(m_network.get());
        m_service->setEndpoints(
            QUrl(QStringLiteral("http://127.0.0.1:%1/translator").arg(m_server->port())),
            QUrl(QStringLiteral("http://127.0.0.1:%1/ttranslatev3").arg(m_server->port())));
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
        QCOMPARE(m_service->id(), QStringLiteral("bing-web"));
        QCOMPARE(m_service->displayName(), QStringLiteral("Bing"));
        QVERIFY(m_service->isConfigured());
        QVERIFY(m_service->requiresOptIn());

        const QStringList sources = m_service->sourceLanguages();
        const QStringList targets = m_service->targetLanguages();
        QVERIFY(!sources.isEmpty());
        QCOMPARE(sources, targets);
        QCOMPARE(sources, Languages::supported(QStringLiteral("bing-web"),
                                              Languages::Direction::Source));
        QVERIFY(!sources.contains(Languages::autoCode()));
        for (const QString &code : {QStringLiteral("zh-Hans"), QStringLiteral("zh-Hant"),
                                    QStringLiteral("pt-BR"), QStringLiteral("pt-PT"),
                                    QStringLiteral("nb"), QStringLiteral("sr"),
                                    QStringLiteral("he"), QStringLiteral("fil"),
                                    QStringLiteral("jv")}) {
            QVERIFY2(sources.contains(code), qPrintable(code));
        }
    }

    void defaultsAndEndpoints()
    {
        BingWebService service(m_network.get());
        QCOMPARE(service.rateLimitMs(), 1000);
        QCOMPARE(service.tokenEndpoint(), QUrl(QStringLiteral("https://www.bing.com/translator")));
        QCOMPARE(service.translateEndpoint(),
                 QUrl(QStringLiteral("https://www.bing.com/ttranslatev3")));
        QCOMPARE(BingWebService::defaultTokenEndpoint(),
                 QUrl(QStringLiteral("https://www.bing.com/translator")));
        QCOMPARE(BingWebService::defaultTranslateEndpoint(),
                 QUrl(QStringLiteral("https://www.bing.com/ttranslatev3")));

        const QUrl token(QStringLiteral("http://127.0.0.1:1/translator"));
        const QUrl translate(QStringLiteral("http://127.0.0.1:1/ttranslatev3"));
        const QUrl invalid(QStringLiteral("http://exa mple.com/translator"));
        QVERIFY(!invalid.isValid());

        // The two URLs fall back independently.
        service.setEndpoints(QUrl(), translate);
        QCOMPARE(service.tokenEndpoint(), BingWebService::defaultTokenEndpoint());
        QCOMPARE(service.translateEndpoint(), translate);

        service.setEndpoints(token, invalid);
        QCOMPARE(service.tokenEndpoint(), token);
        QCOMPARE(service.translateEndpoint(), BingWebService::defaultTranslateEndpoint());

        service.setEndpoints(invalid, QUrl());
        QCOMPARE(service.tokenEndpoint(), BingWebService::defaultTokenEndpoint());
        QCOMPARE(service.translateEndpoint(), BingWebService::defaultTranslateEndpoint());
    }

    void getAndPostRequest()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath) {
                FakeHttpServer::Response response;
                response.body = tokenPage();
                response.setCookies = {QByteArray("MUID=") + kMuid.toLatin1() + "; Path=/"};
                return response;
            }
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hallo"));
        QCOMPARE(reply->result().serviceId, QStringLiteral("bing-web"));
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("en"));

        QCOMPARE(m_server->requests.size(), 2);
        const FakeHttpServer::Request &get = m_server->requests.at(0);
        QCOMPARE(get.method, QByteArray("GET"));
        QCOMPARE(get.url.path(), kTokenPath);
        QCOMPARE(get.headers.value("accept"), QByteArray("text/html"));
        QCOMPARE(get.headers.value("user-agent"), BingWebService::browserUserAgent().toLatin1());
        QVERIFY(get.headers.value("user-agent") != AppInfo::userAgent().toLatin1());
        QVERIFY(!get.headers.contains("authorization"));
        QVERIFY(!get.headers.contains("cookie"));

        const FakeHttpServer::Request &post = m_server->requests.at(1);
        QCOMPARE(post.method, QByteArray("POST"));
        QCOMPARE(post.url.path(), kPostPath);
        QCOMPARE(queryValue(post.url, QStringLiteral("isVertical")), QStringLiteral("1"));
        QCOMPARE(queryValue(post.url, QStringLiteral("IG")), kIg);
        QCOMPARE(queryValue(post.url, QStringLiteral("IID")), kIid);
        QCOMPARE(post.headers.value("content-type"),
                 QByteArray("application/x-www-form-urlencoded"));
        QCOMPARE(post.headers.value("accept"), QByteArray("application/json"));
        QCOMPARE(post.headers.value("user-agent"), BingWebService::browserUserAgent().toLatin1());
        QCOMPARE(post.headers.value("cookie"), QByteArray("MUID=") + kMuid.toLatin1());
        QVERIFY(!post.headers.contains("authorization"));

        const QStringList expectedKeys = {
            QStringLiteral("text"),
            QStringLiteral("fromLang"),
            QStringLiteral("to"),
            QStringLiteral("token"),
            QStringLiteral("key"),
            QStringLiteral("isAuthv2"),
            QStringLiteral("tryFetchingGenderDebiasedTranslations"),
        };
        QCOMPARE(formKeys(post.body), expectedKeys);
        QCOMPARE(formValue(post.body, QStringLiteral("text")), QStringLiteral("Hello"));
        QCOMPARE(formValue(post.body, QStringLiteral("fromLang")), QStringLiteral("en"));
        QCOMPARE(formValue(post.body, QStringLiteral("to")), QStringLiteral("ru"));
        QCOMPARE(formValue(post.body, QStringLiteral("token")), kToken);
        QCOMPARE(formValue(post.body, QStringLiteral("key")), kKey);
        QCOMPARE(formValue(post.body, QStringLiteral("isAuthv2")), QStringLiteral("true"));
        QCOMPARE(formValue(post.body, QStringLiteral("tryFetchingGenderDebiasedTranslations")),
                 QStringLiteral("true"));
    }

    void setCookieMuidWins()
    {
        const QString pageMuid = QStringLiteral("PAGEMUID000000000000000000000000");
        const QString cookieMuid = QStringLiteral("COOKIEMUID000000000000000000000");
        m_server->handler = [pageMuid, cookieMuid](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath) {
                FakeHttpServer::Response response;
                response.body = tokenPage(kToken, pageMuid);
                response.setCookies = {QByteArray("MUID=") + cookieMuid.toLatin1() + "; Path=/"};
                return response;
            }
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        QCOMPARE(m_server->requests.size(), 2);
        QCOMPARE(m_server->requests.at(1).headers.value("cookie"),
                 QByteArray("MUID=") + cookieMuid.toLatin1());
    }

    void pageMuidIsTheFallback()
    {
        const QString pageMuid = QStringLiteral("PAGEMUID000000000000000000000000");
        m_server->handler = [pageMuid](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage(kToken, pageMuid)};
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        QCOMPARE(m_server->requests.size(), 2);
        QCOMPARE(m_server->requests.at(1).headers.value("cookie"),
                 QByteArray("MUID=") + pageMuid.toLatin1());
    }

    void missingMuidStopsBeforePost()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage(kToken, QString())};
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::ServiceUnavailable);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("the token page has no session cookie"));
        verifyFixedMessage(reply->result().errorMessage);
        QCOMPARE(countPath(kTokenPath), 1);
        QCOMPARE(countPath(kPostPath), 0);
    }

    // A control character in the scraped MUID means the page or a proxy is
    // mangled: the value is dropped exactly like a missing one.
    void muidWithControlCharacterIsRejected()
    {
        const QString muid = QStringLiteral("001122\t3344");
        m_server->handler = [muid](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage(kToken, muid)};
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::ServiceUnavailable);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("the token page has no session cookie"));
        verifyFixedMessage(reply->result().errorMessage);
        QVERIFY2(!reply->result().errorMessage.contains(muid),
                 qPrintable(reply->result().errorMessage));
        QVERIFY2(!reply->result().errorMessage.contains(QStringLiteral("3344")),
                 qPrintable(reply->result().errorMessage));
        QCOMPARE(countPath(kTokenPath), 1);
        QCOMPARE(countPath(kPostPath), 0);
    }

    void mapsLanguageCodes_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("to");

        QTest::newRow("English to Russian") << "en" << "ru" << "en" << "ru";
        QTest::newRow("Chinese simplified") << "en" << "zh-Hans" << "en" << "zh-Hans";
        QTest::newRow("Chinese traditional") << "en" << "zh-Hant" << "en" << "zh-Hant";
        QTest::newRow("Portuguese Brazil") << "en" << "pt-BR" << "en" << "pt";
        QTest::newRow("Portuguese Portugal") << "en" << "pt-PT" << "en" << "pt-PT";
        QTest::newRow("Norwegian") << "en" << "nb" << "en" << "nb";
        QTest::newRow("Serbian") << "en" << "sr" << "en" << "sr-Cyrl";
        QTest::newRow("Hebrew") << "he" << "en" << "he" << "en";
        QTest::newRow("Filipino") << "fil" << "en" << "fil" << "en";
        QTest::newRow("Javanese") << "jv" << "en" << "jv" << "en";
    }

    void mapsLanguageCodes()
    {
        QFETCH(QString, source);
        QFETCH(QString, target);
        QFETCH(QString, from);
        QFETCH(QString, to);

        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
        };

        std::unique_ptr<TranslationReply> reply =
            translate(QStringLiteral("Hello"), source, target);
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        const QByteArray body = m_server->requests.at(1).body;
        QCOMPARE(formValue(body, QStringLiteral("fromLang")), from);
        QCOMPARE(formValue(body, QStringLiteral("to")), to);
    }

    void autoSourceIsSentAsAutoDetect()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
        };

        std::unique_ptr<TranslationReply> reply =
            translate(QStringLiteral("Hello"), QStringLiteral("auto"), QStringLiteral("de"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("en"));
        QCOMPARE(formValue(m_server->requests.at(1).body, QStringLiteral("fromLang")),
                 QStringLiteral("auto-detect"));
    }

    void unmappedLanguagesAreRejected_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("code");

        QTest::newRow("ceb source") << "ceb" << "en" << "ceb";
        QTest::newRow("unknown source") << "xx" << "en" << "xx";
        QTest::newRow("ceb target") << "en" << "ceb" << "ceb";
        QTest::newRow("unknown target") << "en" << "xx" << "xx";
    }

    void unmappedLanguagesAreRejected()
    {
        QFETCH(QString, source);
        QFETCH(QString, target);
        QFETCH(QString, code);

        std::unique_ptr<TranslationReply> reply =
            translate(QStringLiteral("Hello"), source, target);
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

        std::unique_ptr<TranslationReply> reply = translate(text);
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(1000));
        QCOMPARE(reply->result().error, TranslationError::EmptyText);
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 0);
    }

    void edgeWhitespaceIsTrimmed()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, translationResponse(QStringLiteral("X"))};
        };

        const QString text = QStringLiteral(" \tHello\r\n ");
        std::unique_ptr<TranslationReply> reply = translate(text);
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(m_server->requests.size(), 2);
        QCOMPARE(formValue(m_server->requests.at(1).body, QStringLiteral("text")),
                 QStringLiteral("Hello"));
        QCOMPARE(reply->result().translatedText, QStringLiteral(" \tX\r\n "));
    }

    void encodesFormBody()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
        };

        const QString text = QStringLiteral("Hello + мир 😀 C++ 1+1 & = %");
        std::unique_ptr<TranslationReply> reply = translate(text);
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        const QByteArray body = m_server->requests.at(1).body;
        QCOMPARE(formValue(body, QStringLiteral("text")), text);
        QVERIFY2(!body.contains(' '), qPrintable(QString::fromLatin1(body)));
        // Seven fields, so exactly six separators.
        QCOMPARE(body.count('&'), 6);
        QVERIFY2(body.contains("%20"), qPrintable(QString::fromLatin1(body)));
        QVERIFY2(body.contains("%2B"), qPrintable(QString::fromLatin1(body)));
        QVERIFY2(body.contains("%26"), qPrintable(QString::fromLatin1(body)));
        QVERIFY2(body.contains("%3D"), qPrintable(QString::fromLatin1(body)));
        QVERIFY2(body.contains("%25"), qPrintable(QString::fromLatin1(body)));
        QVERIFY2(body.contains("%D0"), qPrintable(QString::fromLatin1(body))); // Cyrillic
        QVERIFY2(body.contains("%F0%9F"), qPrintable(QString::fromLatin1(body))); // emoji
    }

    void longTextIsSplitIntoChunks()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
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

        QStringList sentParts;
        for (const QString &part : expectedParts) {
            if (!part.trimmed().isEmpty())
                sentParts << part.trimmed();
        }

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("ru"), QStringLiteral("en"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, text);
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("en"));

        // One token GET and one POST per sent part; the token is reused.
        QCOMPARE(countPath(kTokenPath), 1);
        QCOMPARE(countPath(kPostPath), sentParts.size());
        qsizetype index = 0;
        for (const FakeHttpServer::Request &request : std::as_const(m_server->requests)) {
            if (request.url.path() != kPostPath)
                continue;
            const QString sent = formValue(request.body, QStringLiteral("text"));
            QCOMPARE(sent, sentParts.at(index++));
            QVERIFY2(sent.size() <= 5000,
                     qPrintable(QStringLiteral("%1 UTF-16 units").arg(sent.size())));
            QVERIFY(!sent.at(0).isLowSurrogate());
            QVERIFY(!sent.at(sent.size() - 1).isHighSurrogate());
        }
        QCOMPARE(index, sentParts.size());
    }

    // The middle chunk is all spaces: it is never POSTed, and its whitespace is
    // restored through the unsent-part branch.
    void whitespaceOnlyPartsAreNotSent()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
        };

        const QString text = QStringLiteral("Hello") + QString(12000, QLatin1Char(' '))
            + QStringLiteral("World");
        const QStringList expectedParts = TextChunker::split(text, 5000);
        QCOMPARE(expectedParts.size(), 3);
        QVERIFY(expectedParts.at(1).trimmed().isEmpty());

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("ru"), QStringLiteral("en"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, text);
        QCOMPARE(reply->result().detectedSourceLang, QStringLiteral("en"));

        // One token GET and two POSTs: the whitespace-only chunk produces none.
        QCOMPARE(countPath(kTokenPath), 1);
        QCOMPARE(countPath(kPostPath), 2);
        qsizetype posts = 0;
        for (const FakeHttpServer::Request &request : std::as_const(m_server->requests)) {
            if (request.url.path() != kPostPath)
                continue;
            ++posts;
            const QString sent = formValue(request.body, QStringLiteral("text"));
            QVERIFY2(!sent.isEmpty() && !sent.trimmed().isEmpty(), qPrintable(sent));
            QVERIFY2(sent.size() <= 5000,
                     qPrintable(QStringLiteral("%1 UTF-16 units").arg(sent.size())));
        }
        QCOMPARE(posts, 2);
    }

    void pacesChunks()
    {
        m_service->setRateLimitMs(100);
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
        };
        const QString text = longText();
        QVERIFY(TextChunker::split(text, 5000).size() > 2);

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("ru"), QStringLiteral("en"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(countPath(kPostPath), TextChunker::split(text, 5000).size());
        // Every request, including the first POST after the token GET, is paced.
        QVERIFY(m_server->requests.size() > 2);
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
            if (request.url.path() == kTokenPath)
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
        const qint64 delta = m_server->requests.at(2).elapsedMs
            - m_server->requests.at(1).elapsedMs;
        QVERIFY2(delta >= 180, qPrintable(QStringLiteral("delta %1 ms").arg(delta)));
    }

    void cancelsQueuedRequest()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200, {}, false};
        };

        std::unique_ptr<TranslationReply> first = translate(QStringLiteral("One"));
        QTRY_VERIFY_WITH_TIMEOUT(m_server->requests.size() == 1, 5000);

        std::unique_ptr<TranslationReply> second = translate(QStringLiteral("Two"));
        QSignalSpy secondFinished(second.get(), &TranslationReply::finished);
        second->abort();
        QCOMPARE(second->result().error, TranslationError::Cancelled);
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 1);
        QCOMPARE(secondFinished.size(), 1);
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
            if (request.url.path() == kTokenPath)
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
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            if (++*posts == 1)
                return FakeHttpServer::Response{200, echoResponse(request.body)};
            return FakeHttpServer::Response{500, QByteArray("Too many requests")};
        };
        const QString text = longText();
        QVERIFY(TextChunker::split(text, 5000).size() > 2);

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("ru"), QStringLiteral("en"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QCOMPARE(reply->result().error, TranslationError::RateLimited);
        QVERIFY(reply->result().translatedText.isEmpty());
        QCOMPARE(countPath(kTokenPath), 1);
        QCOMPARE(countPath(kPostPath), 2);
        QTest::qWait(50);
        QCOMPARE(countPath(kPostPath), 2);
        verifyFixedMessage(reply->result().errorMessage);
    }

    void reusesTokenWithinLifetime()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
        };

        std::unique_ptr<TranslationReply> first = translate(QStringLiteral("One"));
        QSignalSpy firstFinished(first.get(), &TranslationReply::finished);
        QVERIFY(firstFinished.wait(5000));
        QVERIFY2(first->result().ok(), qPrintable(first->result().errorMessage));

        std::unique_ptr<TranslationReply> second = translate(QStringLiteral("Two"));
        QSignalSpy secondFinished(second.get(), &TranslationReply::finished);
        QVERIFY(secondFinished.wait(5000));
        QVERIFY2(second->result().ok(), qPrintable(second->result().errorMessage));

        QCOMPARE(countPath(kTokenPath), 1);
        QCOMPARE(countPath(kPostPath), 2);
    }

    void zeroLifetimeRefetchesToken()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage(kToken, kMuid, 0)};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
        };

        std::unique_ptr<TranslationReply> first = translate(QStringLiteral("One"));
        QSignalSpy firstFinished(first.get(), &TranslationReply::finished);
        QVERIFY(firstFinished.wait(5000));
        QVERIFY2(first->result().ok(), qPrintable(first->result().errorMessage));

        std::unique_ptr<TranslationReply> second = translate(QStringLiteral("Two"));
        QSignalSpy secondFinished(second.get(), &TranslationReply::finished);
        QVERIFY(secondFinished.wait(5000));
        QVERIFY2(second->result().ok(), qPrintable(second->result().errorMessage));

        QCOMPARE(countPath(kTokenPath), 2);
        QCOMPARE(countPath(kPostPath), 2);
    }

    void applySettingsClearsToken()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
        };

        std::unique_ptr<TranslationReply> first = translate(QStringLiteral("One"));
        QSignalSpy firstFinished(first.get(), &TranslationReply::finished);
        QVERIFY(firstFinished.wait(5000));
        QVERIFY2(first->result().ok(), qPrintable(first->result().errorMessage));

        m_service->applySettings(AppSettings{});

        std::unique_ptr<TranslationReply> second = translate(QStringLiteral("Two"));
        QSignalSpy secondFinished(second.get(), &TranslationReply::finished);
        QVERIFY(secondFinished.wait(5000));
        QVERIFY2(second->result().ok(), qPrintable(second->result().errorMessage));

        QCOMPARE(countPath(kTokenPath), 2);
        QCOMPARE(countPath(kPostPath), 2);
    }

    void refreshesTokenOnBody401()
    {
        auto tokenFetches = std::make_shared<int>(0);
        auto posts = std::make_shared<int>(0);
        m_server->handler = [tokenFetches, posts](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath) {
                ++*tokenFetches;
                return FakeHttpServer::Response{
                    200, tokenPage(*tokenFetches == 1 ? kToken : kSecondToken)};
            }
            if (++*posts == 1)
                return FakeHttpServer::Response{
                    200, QByteArray(R"({"statusCode":401,"errorMessage":""})")};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(reply->result().translatedText, QStringLiteral("Hello"));
        QCOMPARE(countPath(kTokenPath), 2);
        QCOMPARE(countPath(kPostPath), 2);

        const FakeHttpServer::Request &retry = m_server->requests.at(3);
        QCOMPARE(formValue(retry.body, QStringLiteral("token")), kSecondToken);
        QCOMPARE(formValue(retry.body, QStringLiteral("text")), QStringLiteral("Hello"));

        // The refreshed token serves the next translation without a third GET.
        std::unique_ptr<TranslationReply> next = translate(QStringLiteral("Next"));
        QSignalSpy nextFinished(next.get(), &TranslationReply::finished);
        QVERIFY(nextFinished.wait(5000));
        QVERIFY2(next->result().ok(), qPrintable(next->result().errorMessage));
        QCOMPARE(countPath(kTokenPath), 2);
        QCOMPARE(countPath(kPostPath), 3);
    }

    void refreshesTokenOnHttp401()
    {
        auto tokenFetches = std::make_shared<int>(0);
        auto posts = std::make_shared<int>(0);
        m_server->handler = [tokenFetches, posts](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath) {
                ++*tokenFetches;
                return FakeHttpServer::Response{
                    200, tokenPage(*tokenFetches == 1 ? kToken : kSecondToken)};
            }
            if (++*posts == 1)
                return FakeHttpServer::Response{401, {}};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));
        QCOMPARE(countPath(kTokenPath), 2);
        QCOMPARE(countPath(kPostPath), 2);
        QCOMPARE(formValue(m_server->requests.at(3).body, QStringLiteral("token")), kSecondToken);
    }

    void second401IsRateLimited()
    {
        auto tokenFetches = std::make_shared<int>(0);
        auto posts = std::make_shared<int>(0);
        m_server->handler = [tokenFetches, posts](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath) {
                ++*tokenFetches;
                return FakeHttpServer::Response{
                    200, tokenPage(QStringLiteral("token-%1").arg(*tokenFetches))};
            }
            ++*posts;
            return FakeHttpServer::Response{
                200, QByteArray(R"({"statusCode":401,"errorMessage":""})")};
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
            if (request.url.path() == kTokenPath) {
                ++*tokenFetches;
                if (*tokenFetches == 1)
                    return FakeHttpServer::Response{200, tokenPage()};
                return FakeHttpServer::Response{500, {}};
            }
            return FakeHttpServer::Response{
                200, QByteArray(R"({"statusCode":401,"errorMessage":""})")};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Network);
        QCOMPARE(reply->result().errorMessage, QStringLiteral("HTTP 500"));
        verifyFixedMessage(reply->result().errorMessage);
        // One successful GET, one POST, one failed refresh GET; no retry POST.
        QCOMPARE(countPath(kTokenPath), 2);
        QCOMPARE(countPath(kPostPath), 1);
        QTest::qWait(50);
        QCOMPARE(countPath(kPostPath), 1);
    }

    void http403IsRateLimitedWithoutRefresh()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{403, {}};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::RateLimited);
        verifyFixedMessage(reply->result().errorMessage);
        QCOMPARE(countPath(kTokenPath), 1);
        QCOMPARE(countPath(kPostPath), 1);
    }

    void laterChunk401IsNotRefreshed()
    {
        auto tokenFetches = std::make_shared<int>(0);
        auto posts = std::make_shared<int>(0);
        m_server->handler = [tokenFetches, posts](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath) {
                ++*tokenFetches;
                return FakeHttpServer::Response{
                    200, tokenPage(*tokenFetches == 1 ? kToken : kSecondToken)};
            }
            ++*posts;
            if (*posts == 1 || *posts == 3)
                return FakeHttpServer::Response{
                    200, QByteArray(R"({"statusCode":401,"errorMessage":""})")};
            return FakeHttpServer::Response{200, echoResponse(request.body)};
        };
        const QString text = longText();
        QVERIFY(TextChunker::split(text, 5000).size() > 2);

        std::unique_ptr<TranslationReply> reply =
            translate(text, QStringLiteral("ru"), QStringLiteral("en"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(30000));
        QCOMPARE(reply->result().error, TranslationError::RateLimited);
        QVERIFY(reply->result().translatedText.isEmpty());
        QCOMPARE(countPath(kTokenPath), 2);
        QCOMPARE(countPath(kPostPath), 3);
    }

    void httpErrors_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("429 rate limit") << 429 << QByteArray() << TranslationError::RateLimited;
        QTest::newRow("403 forbidden") << 403 << QByteArray() << TranslationError::RateLimited;
        QTest::newRow("413 too long") << 413 << QByteArray() << TranslationError::TooLong;
        QTest::newRow("500 server error") << 500 << QByteArray() << TranslationError::Network;
        QTest::newRow("503 unavailable") << 503 << QByteArray("upstream failure")
                                         << TranslationError::Network;
        QTest::newRow("400 bad request") << 400 << QByteArray() << TranslationError::BadResponse;
    }

    void httpErrors()
    {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(TranslationError, error);

        const int responseStatus = status;
        const QByteArray responseBody = body;
        m_server->handler = [responseStatus, responseBody](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{responseStatus, responseBody};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, error);
        QVERIFY(reply->result().translatedText.isEmpty());
        verifyFixedMessage(reply->result().errorMessage);
        QCOMPARE(countPath(kTokenPath), 1);
        QCOMPARE(countPath(kPostPath), 1);
    }

    void htmlBody()
    {
        m_server->handler = [](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{
                200, QByteArray("<!DOCTYPE html><html><body>captcha</body></html>")};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::ServiceUnavailable);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("the endpoint returned an HTML page"));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void tokenPageWithoutMarkers()
    {
        m_server->handler = [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{
                200, QByteArray("<!DOCTYPE html><html><body>changed</body></html>")};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::ServiceUnavailable);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("the token page is missing required parameters"));
        verifyFixedMessage(reply->result().errorMessage);
        QCOMPARE(countPath(kTokenPath), 1);
        QCOMPARE(countPath(kPostPath), 0);
    }

    void providerErrors_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<TranslationError>("error");

        QTest::newRow("status 205") << QByteArray(R"({"statusCode":205,"errorMessage":""})")
                                    << TranslationError::ServiceUnavailable;
        QTest::newRow("status 403") << QByteArray(R"({"statusCode":403,"errorMessage":"blocked"})")
                                    << TranslationError::RateLimited;
        QTest::newRow("status 429") << QByteArray(R"({"statusCode":429,"errorMessage":""})")
                                    << TranslationError::RateLimited;
        QTest::newRow("status 413") << QByteArray(R"({"statusCode":413,"errorMessage":""})")
                                    << TranslationError::TooLong;
        QTest::newRow("status 500") << QByteArray(R"({"statusCode":500,"errorMessage":""})")
                                    << TranslationError::Network;
    }

    void providerErrors()
    {
        QFETCH(QByteArray, body);
        QFETCH(TranslationError, error);

        const QByteArray responseBody = body;
        m_server->handler = [responseBody](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, responseBody};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, error);
        QVERIFY(reply->result().translatedText.isEmpty());
        verifyFixedMessage(reply->result().errorMessage);
    }

    void malformedResponse_data()
    {
        QTest::addColumn<QByteArray>("body");

        QTest::newRow("malformed") << QByteArray(R"([{"translations":)");
        QTest::newRow("empty array") << QByteArray("[]");
        QTest::newRow("empty translations") << QByteArray(R"([{"translations":[]}])");
        QTest::newRow("empty text") << QByteArray(R"([{"translations":[{"text":""}]}])");
        QTest::newRow("missing text")
            << QByteArray(R"([{"detectedLanguage":{"language":"en"}}])");
        QTest::newRow("JSON object") << QByteArray(R"({"translations":[{"text":"x"}]})");
    }

    void malformedResponse()
    {
        QFETCH(QByteArray, body);

        const QByteArray responseBody = body;
        m_server->handler = [responseBody](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            return FakeHttpServer::Response{200, responseBody};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
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

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->result().error, TranslationError::Timeout);
        QCOMPARE(reply->result().errorMessage,
                 QStringLiteral("Bing did not respond within 0.3 s"));
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
                + "/translator";
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
            if (request.url.path() == kTokenPath)
                return FakeHttpServer::Response{200, tokenPage()};
            FakeHttpServer::Response response;
            response.status = 302;
            response.location = QByteArray("http://127.0.0.1:") + QByteArray::number(otherPort)
                + "/ttranslatev3";
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
        const QString cookieMuid = QStringLiteral("COOKIEMUID000000000000000000000");
        m_server->handler = [cookieMuid](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath) {
                FakeHttpServer::Response response;
                response.body = tokenPage();
                response.setCookies = {QByteArray("MUID=") + cookieMuid.toLatin1() + "; Path=/"};
                return response;
            }
            if (request.url.path() == kPostPath)
                return FakeHttpServer::Response{200, translationResponse(QStringLiteral("Hallo"))};
            return FakeHttpServer::Response{200, QByteArray("ok")};
        };

        const QUrl origin(QStringLiteral("http://127.0.0.1:%1/").arg(m_server->port()));
        QNetworkCookieJar *jar = m_network->nam()->cookieJar();
        QVERIFY(jar);
        QNetworkCookie preloaded(QByteArray("jar"), QByteArray("value"));
        preloaded.setDomain(QStringLiteral("127.0.0.1"));
        preloaded.setPath(QStringLiteral("/"));
        QVERIFY(jar->setCookiesFromUrl({preloaded}, origin));

        // The jar holds its cookie and nothing else before the exchange.
        bool jarCookieFound = false;
        for (const QNetworkCookie &cookie : jar->cookiesForUrl(origin)) {
            QVERIFY2(cookie.name() != "MUID", qPrintable(cookie.name()));
            QVERIFY2(cookie.name() != "MUIDB", qPrintable(cookie.name()));
            if (cookie.name() == "jar")
                jarCookieFound = true;
        }
        QVERIFY(jarCookieFound);

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("Hello"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->result().ok(), qPrintable(reply->result().errorMessage));

        QCOMPARE(m_server->requests.size(), 2);
        const FakeHttpServer::Request &get = m_server->requests.at(0);
        const FakeHttpServer::Request &post = m_server->requests.at(1);
        QVERIFY(get.headers.value("cookie").isEmpty());
        QVERIFY2(!post.headers.value("cookie").contains("jar=value"),
                 qPrintable(post.headers.value("cookie")));
        // Exactly the explicit MUID cookie, nothing from the shared jar.
        QCOMPARE(post.headers.value("cookie"), QByteArray("MUID=") + cookieMuid.toLatin1());

        // The Bing cookie did not enter the jar; the preloaded one is intact.
        jarCookieFound = false;
        for (const QNetworkCookie &cookie : jar->cookiesForUrl(origin)) {
            QVERIFY2(cookie.name() != "MUID", qPrintable(cookie.name()));
            QVERIFY2(cookie.name() != "MUIDB", qPrintable(cookie.name()));
            if (cookie.name() == "jar")
                jarCookieFound = true;
        }
        QVERIFY(jarCookieFound);

        // The jar still works for a request that is not the Bing exchange.
        const QNetworkRequest plainRequest =
            m_network->makeRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/ping")
                                            .arg(m_server->port())));
        QNetworkReply *plain = m_network->nam()->get(plainRequest);
        QSignalSpy plainFinished(plain, &QNetworkReply::finished);
        QVERIFY(plainFinished.wait(5000));
        plain->deleteLater();

        QCOMPARE(m_server->requests.size(), 3);
        const QByteArray plainCookie = m_server->requests.at(2).headers.value("cookie");
        QVERIFY2(plainCookie.contains("jar=value"), qPrintable(plainCookie));
        QVERIFY2(!plainCookie.contains("MUID"), qPrintable(plainCookie));
    }

    void errorMessagesOmitSecrets_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");

        QTest::newRow("503") << 503 << QByteArray("blocked");
        QTest::newRow("html") << 200
                              << QByteArray("<!DOCTYPE html><html><body>captcha</body></html>");
        QTest::newRow("rate limit") << 200 << QByteArray("Too many requests TOPSECRET");
        QTest::newRow("provider error")
            << 200 << QByteArray(R"({"statusCode":205,"errorMessage":"TOPSECRET"})");
    }

    void errorMessagesOmitSecrets()
    {
        QFETCH(int, status);
        QFETCH(QByteArray, body);

        const int responseStatus = status;
        const QByteArray responseBody = body;
        m_server->handler = [responseStatus, responseBody](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath) {
                FakeHttpServer::Response response;
                response.body = tokenPage(QStringLiteral("TOPSECRET"),
                                          QStringLiteral("TOPSECRETMUID"));
                return response;
            }
            return FakeHttpServer::Response{responseStatus, responseBody};
        };

        std::unique_ptr<TranslationReply> reply = translate(QStringLiteral("TOPSECRET"));
        QSignalSpy finished(reply.get(), &TranslationReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY(!reply->result().ok());
        QVERIFY(!reply->result().errorMessage.contains(QStringLiteral("TOPSECRET")));
        QVERIFY(!reply->result().errorMessage.contains(kKey));
        verifyFixedMessage(reply->result().errorMessage);
    }

    void logsAreClean()
    {
        auto posts = std::make_shared<int>(0);
        m_server->handler = [posts](const FakeHttpServer::Request &request) {
            if (request.url.path() == kTokenPath) {
                FakeHttpServer::Response response;
                response.body =
                    tokenPage(QStringLiteral("TOPSECRET"), QStringLiteral("TOPSECRETMUID"));
                response.setCookies = {QByteArray("MUID=TOPSECRETMUID; Path=/")};
                return response;
            }
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
            QVERIFY2(!message.contains(kKey), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("MUID")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("<html")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("params_AbusePreventionHelper")),
                     qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("Too many requests")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("translations")), qPrintable(message));
            QVERIFY2(!message.contains(QStringLiteral("detectedLanguage")), qPrintable(message));
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
    std::unique_ptr<BingWebService> m_service;
};

QTEST_GUILESS_MAIN(TestBingWebService)
#include "tst_bingwebservice.moc"
