#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTest>
#include <QXmlStreamReader>

namespace {

struct Message
{
    QString context;
    QString source;
    QString translation;
    bool unfinished = false;
    bool vanished = false;
};

QVector<Message> readMessages(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QVector<Message> messages;
    QXmlStreamReader xml(&file);
    QString context;
    Message current;
    bool inMessage = false;

    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("context")) {
                context.clear();
            } else if (xml.name() == QLatin1String("name")) {
                context = xml.readElementText();
            } else if (xml.name() == QLatin1String("message")) {
                inMessage = true;
                current = Message{};
                current.context = context;
            } else if (inMessage && xml.name() == QLatin1String("source")) {
                current.source = xml.readElementText();
            } else if (inMessage && xml.name() == QLatin1String("translation")) {
                current.unfinished =
                    xml.attributes().value(QLatin1String("type")) == QLatin1String("unfinished");
                current.vanished =
                    xml.attributes().value(QLatin1String("type")) == QLatin1String("vanished");
                current.translation = xml.readElementText();
            }
        } else if (token == QXmlStreamReader::EndElement
                   && xml.name() == QLatin1String("message")) {
            messages.append(current);
            inMessage = false;
        }
    }
    return messages;
}

QStringList placeholders(const QString &text)
{
    QStringList result;
    static const QRegularExpression expression(QStringLiteral("%(?:\\d+|n)"));
    auto it = expression.globalMatch(text);
    while (it.hasNext())
        result.append(it.next().captured(0));
    result.sort();
    return result;
}

} // namespace

class TestTranslations : public QObject
{
    Q_OBJECT

private slots:
    void allLanguagesAreComplete();
    void placeholdersAndSymbolsArePreserved();
};

void TestTranslations::allLanguagesAreComplete()
{
    const QDir directory(QStringLiteral(QTRANSLATE_SOURCE_DIR "/i18n"));
    const QStringList expected = {
        QStringLiteral("qtranslate_ru_RU.ts"), QStringLiteral("qtranslate_es.ts"),
        QStringLiteral("qtranslate_de.ts"),    QStringLiteral("qtranslate_fr.ts"),
        QStringLiteral("qtranslate_pt_BR.ts"), QStringLiteral("qtranslate_it.ts"),
        QStringLiteral("qtranslate_pl.ts"),    QStringLiteral("qtranslate_uk.ts"),
        QStringLiteral("qtranslate_tr.ts"),    QStringLiteral("qtranslate_zh_CN.ts"),
        QStringLiteral("qtranslate_ja.ts"),    QStringLiteral("qtranslate_ko.ts"),
        QStringLiteral("qtranslate_ar.ts"),    QStringLiteral("qtranslate_hi.ts"),
    };

    for (const QString &name : expected) {
        const QString path = directory.filePath(name);
        QVERIFY2(QFileInfo::exists(path), qPrintable(path));
        const QVector<Message> messages = readMessages(path);
        QVERIFY2(messages.size() >= 250, qPrintable(name));

        for (const Message &message : messages) {
            QVERIFY2(!message.vanished,
                     qPrintable(name + QStringLiteral(": vanished ") + message.source));
            QVERIFY2(!message.unfinished,
                     qPrintable(name + QStringLiteral(": unfinished ") + message.source));
            QVERIFY2(!message.translation.trimmed().isEmpty(),
                     qPrintable(name + QStringLiteral(": empty ") + message.source));
        }
    }
}

void TestTranslations::placeholdersAndSymbolsArePreserved()
{
    const QDir directory(QStringLiteral(QTRANSLATE_SOURCE_DIR "/i18n"));
    const QStringList files =
        directory.entryList({QStringLiteral("qtranslate_*.ts")}, QDir::Files);
    QVERIFY(!files.isEmpty());

    const QStringList symbols = {QStringLiteral("📋"), QStringLiteral("↗"),
                                 QStringLiteral("✓"), QStringLiteral("→")};
    for (const QString &name : files) {
        const QVector<Message> messages = readMessages(directory.filePath(name));
        for (const Message &message : messages) {
            QCOMPARE(placeholders(message.translation), placeholders(message.source));

            for (const QString &symbol : symbols) {
                if (message.source.contains(symbol)) {
                    QVERIFY2(message.translation.contains(symbol),
                             qPrintable(name + QStringLiteral(": lost ") + symbol + QStringLiteral(" in ")
                                        + message.source));
                }
            }

            if (message.source.startsWith(QLatin1Char(' ')))
                QVERIFY2(message.translation.startsWith(QLatin1Char(' ')),
                         qPrintable(name + QStringLiteral(": lost leading space in ")
                                    + message.source));
            if (message.source.endsWith(QLatin1Char(' ')))
                QVERIFY2(message.translation.endsWith(QLatin1Char(' ')),
                         qPrintable(name + QStringLiteral(": lost trailing space in ")
                                    + message.source));
            QVERIFY2(!message.translation.contains(QChar(0xE000)),
                     qPrintable(name + QStringLiteral(": internal token in ") + message.source));
        }
    }
}

QTEST_GUILESS_MAIN(TestTranslations)
#include "tst_translations.moc"
