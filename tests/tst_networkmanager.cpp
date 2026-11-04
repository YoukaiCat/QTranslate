#include "net/networkmanager.h"

#include <QHash>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>

#include <memory>

namespace {

constexpr auto kAuthorization = "Basic dXNlcjpwYXNz"; // user:pass

class HttpProxy final : public QObject
{
public:
    explicit HttpProxy(bool requireAuth)
        : m_requireAuth(requireAuth)
    {
        connect(&m_server, &QTcpServer::newConnection, this, &HttpProxy::onNewConnection);
        if (!m_server.listen(QHostAddress::LocalHost))
            qFatal("Cannot listen on localhost");
    }

    quint16 port() const { return m_server.serverPort(); }
    bool sawConnect = false;
    bool sawAuthenticatedRequest = false;

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

        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        const QByteArray requestLine = lines.value(0).trimmed();
        QHash<QByteArray, QByteArray> headers;
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const qsizetype colon = lines.at(i).indexOf(':');
            if (colon > 0) {
                headers.insert(lines.at(i).left(colon).trimmed().toLower(),
                               lines.at(i).mid(colon + 1).trimmed());
            }
        }
        m_buffers.remove(socket);

        if (requestLine.startsWith("CONNECT ")) {
            sawConnect = true;
            socket->write("HTTP/1.1 200 Connection Established\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }

        if (m_requireAuth) {
            const QByteArray auth = headers.value("proxy-authorization");
            if (auth != kAuthorization) {
                socket->write("HTTP/1.1 407 Proxy Authentication Required\r\n"
                              "Proxy-Authenticate: Basic realm=\"test\"\r\n"
                              "Content-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
                return;
            }
            sawAuthenticatedRequest = true;
        }

        socket->write("HTTP/1.1 200 OK\r\nContent-Length: 7\r\nConnection: close\r\n\r\n"
                      "proxied");
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    bool m_requireAuth = false;
};

class Socks5Proxy final : public QObject
{
public:
    Socks5Proxy(bool requireAuth, QString user = QString(), QString password = QString())
        : m_requireAuth(requireAuth)
        , m_user(std::move(user))
        , m_password(std::move(password))
    {
        connect(&m_server, &QTcpServer::newConnection, this, &Socks5Proxy::onNewConnection);
        if (!m_server.listen(QHostAddress::LocalHost))
            qFatal("Cannot listen on localhost");
    }

    quint16 port() const { return m_server.serverPort(); }
    bool sawAuthentication = false;

private:
    struct State
    {
        QByteArray buffer;
        int phase = 0; // 0 greeting, 1 auth, 2 request, 3 tunnel
        bool replySent = false;
    };

    static void fail(QTcpSocket *socket)
    {
        socket->write(QByteArray::fromHex("05ff"));
        socket->disconnectFromHost();
    }

    void onNewConnection()
    {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
            connect(socket, &QTcpSocket::disconnected, socket,
                    [this, socket] { m_states.remove(socket); socket->deleteLater(); });
        }
    }

    void onReadyRead(QTcpSocket *socket)
    {
        State &state = m_states[socket];
        state.buffer += socket->readAll();

        for (;;) {
            if (state.phase == 0) {
                if (state.buffer.size() < 2)
                    return;
                const int methods = quint8(state.buffer.at(1));
                if (state.buffer.size() < 2 + methods)
                    return;
                const QByteArray supported = state.buffer.mid(2, methods);
                const quint8 method = m_requireAuth ? 0x02 : 0x00;
                if (!supported.contains(char(method))) {
                    fail(socket);
                    return;
                }
                socket->write(QByteArray::fromHex("05") + QByteArray(1, char(method)));
                state.buffer.remove(0, 2 + methods);
                state.phase = m_requireAuth ? 1 : 2;
                continue;
            }

            if (state.phase == 1) {
                if (state.buffer.size() < 2)
                    return;
                const int userLength = quint8(state.buffer.at(1));
                if (state.buffer.size() < 2 + userLength + 1)
                    return;
                const int passwordLength = quint8(state.buffer.at(2 + userLength));
                if (state.buffer.size() < 2 + userLength + 1 + passwordLength)
                    return;
                const QString user =
                    QString::fromUtf8(state.buffer.mid(2, userLength));
                const QString password = QString::fromUtf8(
                    state.buffer.mid(3 + userLength, passwordLength));
                state.buffer.remove(0, 2 + userLength + 1 + passwordLength);

                if (user != m_user || password != m_password) {
                    socket->write(QByteArray::fromHex("0101"));
                    socket->disconnectFromHost();
                    return;
                }
                sawAuthentication = true;
                socket->write(QByteArray::fromHex("0100"));
                state.phase = 2;
                continue;
            }

            if (state.phase == 2) {
                if (state.buffer.size() < 4)
                    return;
                const quint8 atyp = quint8(state.buffer.at(3));
                qsizetype requestLength = 0;
                if (atyp == 0x01)
                    requestLength = 10;
                else if (atyp == 0x03) {
                    if (state.buffer.size() < 5)
                        return;
                    requestLength = 4 + 1 + quint8(state.buffer.at(4)) + 2;
                } else {
                    fail(socket);
                    return;
                }
                if (state.buffer.size() < requestLength)
                    return;
                state.buffer.remove(0, requestLength);
                socket->write(QByteArray::fromHex("05000001000000000000"));
                state.phase = 3;
                continue;
            }

            if (state.phase == 3 && !state.replySent && !state.buffer.isEmpty()) {
                // Act as the origin server; the test only needs a valid HTTP reply.
                state.buffer.clear();
                state.replySent = true;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 6\r\nConnection: close\r\n\r\n"
                              "socks5");
                socket->disconnectFromHost();
                return;
            }
            return;
        }
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, State> m_states;
    bool m_requireAuth = false;
    QString m_user;
    QString m_password;
};

ProxySettings manualProxy(ProxyType type, quint16 port)
{
    ProxySettings settings;
    settings.mode = ProxyMode::Manual;
    settings.type = type;
    settings.host = QStringLiteral("127.0.0.1");
    settings.port = port;
    return settings;
}

} // namespace

class TestNetworkManager : public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        QNetworkProxyFactory::setUseSystemConfiguration(false);
    }

    void appliesProxySettings()
    {
        NetworkManager manager;
        manager.applyProxy(manualProxy(ProxyType::Http, 8080));
        QCOMPARE(manager.nam()->proxy().type(), QNetworkProxy::HttpProxy);
        QCOMPARE(manager.nam()->proxy().hostName(), QStringLiteral("127.0.0.1"));
        QCOMPARE(manager.nam()->proxy().port(), quint16(8080));

        ProxySettings socks = manualProxy(ProxyType::Socks5, 1080);
        socks.authEnabled = true;
        socks.user = QStringLiteral("user");
        socks.password = QStringLiteral("pass");
        manager.applyProxy(socks);
        QCOMPARE(manager.nam()->proxy().type(), QNetworkProxy::Socks5Proxy);
        QCOMPARE(manager.nam()->proxy().user(), QStringLiteral("user"));
        QCOMPARE(manager.nam()->proxy().password(), QStringLiteral("pass"));

        ProxySettings none;
        none.mode = ProxyMode::None;
        manager.applyProxy(none);
        QCOMPARE(manager.nam()->proxy().type(), QNetworkProxy::NoProxy);

        ProxySettings system;
        system.mode = ProxyMode::System;
        manager.applyProxy(system);
        QVERIFY(QNetworkProxyFactory::usesSystemConfiguration());
        manager.applyProxy(none);
    }

    void httpProxyRequest()
    {
        HttpProxy proxy(false);
        NetworkManager manager;
        manager.applyProxy(manualProxy(ProxyType::Http, proxy.port()));

        QNetworkReply *reply = manager.nam()->get(
            manager.makeRequest(QUrl(QStringLiteral("http://example.invalid/hello"))));
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->readAll(), QByteArray("proxied"));
        reply->deleteLater();
    }

    void httpProxyAuthentication()
    {
        HttpProxy proxy(true);
        ProxySettings settings = manualProxy(ProxyType::Http, proxy.port());
        settings.authEnabled = true;
        settings.user = QStringLiteral("user");
        settings.password = QStringLiteral("pass");

        NetworkManager manager;
        manager.applyProxy(settings);
        QNetworkReply *reply = manager.nam()->get(
            manager.makeRequest(QUrl(QStringLiteral("http://example.invalid/hello"))));
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->readAll(), QByteArray("proxied"));
        QVERIFY(proxy.sawAuthenticatedRequest);
        reply->deleteLater();
    }

    void httpProxyGetsConnectForHttps()
    {
        HttpProxy proxy(false);
        NetworkManager manager;
        manager.applyProxy(manualProxy(ProxyType::Http, proxy.port()));
        QNetworkReply *reply = manager.nam()->get(
            manager.makeRequest(QUrl(QStringLiteral("https://example.invalid/"))));
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY(proxy.sawConnect);
        reply->deleteLater();
    }

    void socks5ProxyWithoutAuthentication()
    {
        Socks5Proxy proxy(false);
        NetworkManager manager;
        manager.applyProxy(manualProxy(ProxyType::Socks5, proxy.port()));
        QNetworkReply *reply = manager.nam()->get(
            manager.makeRequest(QUrl(QStringLiteral("http://example.invalid/hello"))));
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->readAll(), QByteArray("socks5"));
        reply->deleteLater();
    }

    void socks5ProxyWithAuthentication()
    {
        Socks5Proxy proxy(true, QStringLiteral("user"), QStringLiteral("pass"));
        ProxySettings settings = manualProxy(ProxyType::Socks5, proxy.port());
        settings.authEnabled = true;
        settings.user = QStringLiteral("user");
        settings.password = QStringLiteral("pass");

        NetworkManager manager;
        manager.applyProxy(settings);
        QNetworkReply *reply = manager.nam()->get(
            manager.makeRequest(QUrl(QStringLiteral("http://example.invalid/hello"))));
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->readAll(), QByteArray("socks5"));
        QVERIFY(proxy.sawAuthentication);
        reply->deleteLater();
    }

    void testProxySuccess()
    {
        HttpProxy proxy(false);
        NetworkManager manager;
        bool called = false;
        bool ok = false;
        QString message;
        manager.testProxy(manualProxy(ProxyType::Http, proxy.port()),
                          QUrl(QStringLiteral("http://example.invalid/")),
                          [&](bool resultOk, const QString &text) {
                              called = true;
                              ok = resultOk;
                              message = text;
                          });
        QTRY_VERIFY_WITH_TIMEOUT(called, 5000);
        QVERIFY(ok);
        QVERIFY(message.contains(QStringLiteral("HTTP 200")));
    }

    void testProxyFailure()
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        const quint16 port = probe.serverPort();
        probe.close();

        NetworkManager manager;
        bool called = false;
        bool ok = true;
        QString message;
        manager.testProxy(manualProxy(ProxyType::Http, port),
                          QUrl(QStringLiteral("http://example.invalid/")),
                          [&](bool resultOk, const QString &text) {
                              called = true;
                              ok = resultOk;
                              message = text;
                          });
        QTRY_VERIFY_WITH_TIMEOUT(called, 5000);
        QVERIFY(!ok);
        QVERIFY(!message.isEmpty());
    }

    void wrongProxyPassword()
    {
        HttpProxy proxy(true);
        ProxySettings settings = manualProxy(ProxyType::Http, proxy.port());
        settings.authEnabled = true;
        settings.user = QStringLiteral("user");
        settings.password = QStringLiteral("wrong");

        NetworkManager manager;
        manager.applyProxy(settings);
        QNetworkReply *reply = manager.nam()->get(
            manager.makeRequest(QUrl(QStringLiteral("http://example.invalid/hello"))));
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY(reply->error() != QNetworkReply::NoError);
        QVERIFY(manager.describeNetworkError(reply).contains(QStringLiteral("Proxy")));
        reply->deleteLater();
    }
};

QTEST_GUILESS_MAIN(TestNetworkManager)
#include "tst_networkmanager.moc"
