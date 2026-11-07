// Guiless CLI test: parses the shared command-line options and drives the real
// single-instance socket with a child process. No display is needed.
#include "app/commandline.h"
#include "app/singleinstance.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

namespace {

constexpr int kTimeoutMs = 5000;
constexpr int kChildExitPrimary = 3;

QString uniqueKey()
{
    return QStringLiteral("qtranslate-test-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QStringList argumentsFor(const QStringList &options)
{
    QStringList arguments{QStringLiteral("qtranslate")};
    arguments += options;
    return arguments;
}

int runChild(int argc, char *argv[])
{
    // The parent adds --child in front of the application arguments; parse the
    // rest exactly like the application does.
    QStringList arguments;
    arguments.reserve(argc - 1);
    arguments.append(QString::fromLocal8Bit(argv[0]));
    for (int i = 2; i < argc; ++i)
        arguments.append(QString::fromLocal8Bit(argv[i]));

    QCommandLineParser parser;
    const CommandLineOptions options;
    options.addTo(parser);
    parser.process(arguments);

    QString error;
    const SingleInstance::Command scenario = options.scenario(parser, &error);
    if (!error.isEmpty()) {
        qWarning("%s: %s", qPrintable(QCoreApplication::applicationName()),
                 qPrintable(error));
        return 1;
    }

    const bool startHidden = options.isStartHidden(parser);
    const SingleInstance::Command command = scenario != SingleInstance::Command::None
        ? scenario
        : startHidden ? SingleInstance::Command::None : SingleInstance::Command::Activate;

    SingleInstance instance(QString::fromLocal8Bit(qgetenv("QTRANSLATE_TEST_KEY")));
    if (instance.tryBecomePrimary(command)) {
        // The parent is expected to hold the lock; becoming primary means the
        // forwarding did not happen.
        return kChildExitPrimary;
    }
    return 0;
}

} // namespace

class TestCli : public QObject
{
    Q_OBJECT

private slots:
    void defaults();
    void scenarioOptions_data();
    void scenarioOptions();
    void scenarioWithStartupFlags_data();
    void scenarioWithStartupFlags();
    void mutuallyExclusive();
    void unknownOption();
    void positionalArgumentsAreIgnored();
    void forwarding_data();
    void forwarding();
    void legacyWireFormat();
};

void TestCli::defaults()
{
    QCommandLineParser parser;
    const CommandLineOptions options;
    options.addTo(parser);
    QVERIFY(parser.parse(argumentsFor({})));

    QCOMPARE(static_cast<int>(options.scenario(parser, nullptr)),
             static_cast<int>(SingleInstance::Command::None));
    QVERIFY(!options.isStartHidden(parser));
    QVERIFY(!options.isVerbose(parser));
}

void TestCli::scenarioOptions_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<int>("expected");

    QTest::newRow("translate-popup") << QStringList{QStringLiteral("--translate-popup")}
                                     << static_cast<int>(SingleInstance::Command::TranslatePopup);
    QTest::newRow("translate-main") << QStringList{QStringLiteral("--translate-main")}
                                    << static_cast<int>(SingleInstance::Command::TranslateMain);
    QTest::newRow("translate-inplace")
        << QStringList{QStringLiteral("--translate-inplace")}
        << static_cast<int>(SingleInstance::Command::TranslateInplace);
}

void TestCli::scenarioOptions()
{
    QFETCH(QStringList, arguments);
    QFETCH(int, expected);

    QCommandLineParser parser;
    const CommandLineOptions options;
    options.addTo(parser);
    QVERIFY(parser.parse(argumentsFor(arguments)));

    QString error;
    QCOMPARE(static_cast<int>(options.scenario(parser, &error)), expected);
    QVERIFY(error.isEmpty());
}

void TestCli::scenarioWithStartupFlags_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<int>("expected");
    QTest::addColumn<bool>("hidden");
    QTest::addColumn<bool>("verbose");

    const QList<QPair<QString, SingleInstance::Command>> scenarios = {
        {QStringLiteral("--translate-popup"), SingleInstance::Command::TranslatePopup},
        {QStringLiteral("--translate-main"), SingleInstance::Command::TranslateMain},
        {QStringLiteral("--translate-inplace"), SingleInstance::Command::TranslateInplace},
    };
    for (const auto &scenario : scenarios) {
        const QByteArray prefix = scenario.first.toLatin1();
        QTest::newRow(prefix + "-minimized")
            << QStringList{scenario.first, QStringLiteral("--minimized")}
            << static_cast<int>(scenario.second) << true << false;
        QTest::newRow(prefix + "-verbose")
            << QStringList{scenario.first, QStringLiteral("--verbose")}
            << static_cast<int>(scenario.second) << false << true;
    }
}

void TestCli::scenarioWithStartupFlags()
{
    QFETCH(QStringList, arguments);
    QFETCH(int, expected);
    QFETCH(bool, hidden);
    QFETCH(bool, verbose);

    QCommandLineParser parser;
    const CommandLineOptions options;
    options.addTo(parser);
    QVERIFY(parser.parse(argumentsFor(arguments)));

    QString error;
    QCOMPARE(static_cast<int>(options.scenario(parser, &error)), expected);
    QVERIFY(error.isEmpty());
    QCOMPARE(options.isStartHidden(parser), hidden);
    QCOMPARE(options.isVerbose(parser), verbose);
}

void TestCli::mutuallyExclusive()
{
    QCommandLineParser parser;
    const CommandLineOptions options;
    options.addTo(parser);
    QVERIFY(parser.parse(argumentsFor(
        {QStringLiteral("--translate-popup"), QStringLiteral("--translate-main")})));

    QString error;
    QCOMPARE(static_cast<int>(options.scenario(parser, &error)),
             static_cast<int>(SingleInstance::Command::None));
    QVERIFY(!error.isEmpty());
}

void TestCli::unknownOption()
{
    QCommandLineParser parser;
    const CommandLineOptions options;
    options.addTo(parser);

    // Do not call process(): it prints the error and exits the test process.
    QVERIFY(!parser.parse(argumentsFor({QStringLiteral("--bogus")})));
    QVERIFY(parser.errorText().contains(QStringLiteral("Unknown option")));
}

void TestCli::positionalArgumentsAreIgnored()
{
    QCommandLineParser parser;
    const CommandLineOptions options;
    options.addTo(parser);
    QVERIFY(parser.parse(argumentsFor(
        {QStringLiteral("--translate-main"), QStringLiteral("file.txt")})));

    QCOMPARE(parser.positionalArguments(), QStringList{QStringLiteral("file.txt")});
    QCOMPARE(static_cast<int>(options.scenario(parser, nullptr)),
             static_cast<int>(SingleInstance::Command::TranslateMain));
}

void TestCli::forwarding_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<int>("expected");

    QTest::newRow("translate-popup") << QStringList{QStringLiteral("--translate-popup")}
                                     << static_cast<int>(SingleInstance::Command::TranslatePopup);
    QTest::newRow("translate-main") << QStringList{QStringLiteral("--translate-main")}
                                    << static_cast<int>(SingleInstance::Command::TranslateMain);
    QTest::newRow("translate-inplace")
        << QStringList{QStringLiteral("--translate-inplace")}
        << static_cast<int>(SingleInstance::Command::TranslateInplace);
    QTest::newRow("minimized-noop") << QStringList{QStringLiteral("--minimized")}
                                    << static_cast<int>(SingleInstance::Command::None);
}

void TestCli::forwarding()
{
    QFETCH(QStringList, arguments);
    QFETCH(int, expected);

    const QString key = uniqueKey();
    SingleInstance instance(key);
    QVERIFY(instance.tryBecomePrimary(SingleInstance::Command::None));

    QSignalSpy spy(&instance, &SingleInstance::commandRequested);
    QVERIFY(spy.isValid());

    QProcess child;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QTRANSLATE_TEST_KEY"), key);
    child.setProcessEnvironment(environment);
    child.start(QCoreApplication::applicationFilePath(),
                QStringList{QStringLiteral("--child")} + arguments);
    QVERIFY2(child.waitForStarted(kTimeoutMs), qPrintable(child.errorString()));

    if (expected == static_cast<int>(SingleInstance::Command::None)) {
        // A hidden start forwards "noop": the child exits 0 and the primary
        // must not run a scenario.
        QVERIFY2(child.waitForFinished(kTimeoutMs), "the child did not exit");
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
        QTest::qWait(200); // let a late signal arrive if the bug exists
        QCOMPARE(spy.count(), 0);
        return;
    }

    if (!spy.wait(kTimeoutMs)) {
        child.kill();
        child.waitForFinished(kTimeoutMs);
        const bool exited = child.state() == QProcess::NotRunning;
        QFAIL(qPrintable(
            QStringLiteral("no command received; child %1, exit code %2, stderr: %3")
                .arg(exited ? QStringLiteral("exited") : QStringLiteral("was killed"))
                .arg(exited ? child.exitCode() : -1)
                .arg(QString::fromLocal8Bit(child.readAllStandardError()))));
    }

    QVERIFY2(child.waitForFinished(kTimeoutMs), "the child did not exit after forwarding");
    QCOMPARE(child.exitStatus(), QProcess::NormalExit);
    QCOMPARE(child.exitCode(), 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<SingleInstance::Command>(),
             static_cast<SingleInstance::Command>(expected));
}

void TestCli::legacyWireFormat()
{
    const QString key = uniqueKey();
    SingleInstance instance(key);
    QVERIFY(instance.tryBecomePrimary(SingleInstance::Command::None));

    QSignalSpy spy(&instance, &SingleInstance::commandRequested);
    QVERIFY(spy.isValid());

    QLocalSocket socket;
    socket.connectToServer(key);
    QVERIFY2(socket.waitForConnected(kTimeoutMs), qPrintable(socket.errorString()));

    // Byte-exact lines of the wire contract: the previous "activate" plus the
    // three translate commands. Both ends share the mapping helpers, so this
    // raw-socket test is what catches a symmetric typo in them.
    const QList<QPair<QByteArray, SingleInstance::Command>> commands = {
        {QByteArrayLiteral("activate\n"), SingleInstance::Command::Activate},
        {QByteArrayLiteral("translate-popup\n"), SingleInstance::Command::TranslatePopup},
        {QByteArrayLiteral("translate-main\n"), SingleInstance::Command::TranslateMain},
        {QByteArrayLiteral("translate-inplace\n"), SingleInstance::Command::TranslateInplace},
    };
    for (const auto &command : commands) {
        QCOMPARE(socket.write(command.first), qint64(command.first.size()));
        QVERIFY(socket.waitForBytesWritten(kTimeoutMs));
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, kTimeoutMs);
        QCOMPARE(spy.at(0).at(0).value<SingleInstance::Command>(), command.second);
        spy.clear();
    }

    // "noop" and unknown lines from a future version must stay silent.
    const QList<QByteArray> ignored = {QByteArrayLiteral("noop\n"),
                                       QByteArrayLiteral("future-command\n")};
    for (const QByteArray &line : ignored) {
        QCOMPARE(socket.write(line), qint64(line.size()));
        QVERIFY(socket.waitForBytesWritten(kTimeoutMs));
        QTest::qWait(200);
        QCOMPARE(spy.count(), 0);
    }

    socket.disconnectFromServer();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc > 1 && qstrcmp(argv[1], "--child") == 0)
        return runChild(argc, argv);

    TestCli test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_cli.moc"