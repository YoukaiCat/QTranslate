#include "core/textchunker.h"

#include <QRandomGenerator>
#include <QTest>

namespace {

// Joining the parts gives the text back, and no part is longer than the limit.
void verifyInvariants(const QString &text, const QStringList &parts, int maxLength)
{
    QCOMPARE(parts.join(QString()), text);
    for (const QString &part : parts) {
        QVERIFY(!part.isEmpty());
        QVERIFY2(part.size() <= maxLength, qPrintable(QString::number(part.size())));
        QVERIFY2(!part.front().isLowSurrogate(), "surrogate pair split");
        QVERIFY2(!part.back().isHighSurrogate(), "surrogate pair split");
    }
}

} // namespace

class TestTextChunker : public QObject
{
    Q_OBJECT

private slots:
    void emptyText()
    {
        QVERIFY(TextChunker::split(QString(), 10).isEmpty());
    }

    void shortTextIsOnePart()
    {
        const QString text = QStringLiteral("Hello, world.\n\nSecond paragraph.");
        QCOMPARE(TextChunker::split(text, 5000), QStringList{text});
        QCOMPARE(TextChunker::split(text, int(text.size())), QStringList{text});
    }

    void cutsAfterBlankLine()
    {
        // The blank line wins over a later line break and sentence end.
        const QString text = QStringLiteral("aaaaaaaaaaaaaaaaaa\n\nbbbb.\nccc dddddddddddddddddddd");
        const QStringList parts = TextChunker::split(text, 30);
        verifyInvariants(text, parts, 30);
        QCOMPARE(parts.first(), QStringLiteral("aaaaaaaaaaaaaaaaaa\n\n"));
    }

    void cutsAfterBlankLineWithCrLf()
    {
        const QString text = QStringLiteral("aaaaaaaaaaaaaaaaaa\r\n\r\nbbbb.\r\nccc ddddddddddddddddd");
        const QStringList parts = TextChunker::split(text, 30);
        verifyInvariants(text, parts, 30);
        QCOMPARE(parts.first(), QStringLiteral("aaaaaaaaaaaaaaaaaa\r\n\r\n"));
    }

    void cutsAfterLineBreak()
    {
        const QString text = QStringLiteral("aaaaaaaaaaaaaaaaaa. bbbb\ncccc dddddddddddddddddddd");
        const QStringList parts = TextChunker::split(text, 30);
        verifyInvariants(text, parts, 30);
        QCOMPARE(parts.first(), QStringLiteral("aaaaaaaaaaaaaaaaaa. bbbb\n"));
    }

    void cutsAfterSentence()
    {
        const QString text = QStringLiteral("One two three four. Five six seven eight nine ten.");
        const QStringList parts = TextChunker::split(text, 30);
        verifyInvariants(text, parts, 30);
        QCOMPARE(parts.first(), QStringLiteral("One two three four. "));
    }

    void cutsAfterQuestionAndExclamation()
    {
        const QString text = QStringLiteral("Is it raining today? Yes! It is raining a lot.");
        const QStringList parts = TextChunker::split(text, 26);
        verifyInvariants(text, parts, 26);
        QCOMPARE(parts.first(), QStringLiteral("Is it raining today? Yes! "));
    }

    void cutsAfterFullWidthSentenceEnd()
    {
        const QString text = QStringLiteral("今日は晴れです。明日は雨が降るでしょう。週末は暖かくなります");
        const QStringList parts = TextChunker::split(text, 22);
        verifyInvariants(text, parts, 22);
        QCOMPARE(parts.first(), QStringLiteral("今日は晴れです。明日は雨が降るでしょう。"));
    }

    void sentenceEndTooEarlyIsIgnored()
    {
        // A boundary in the first half would make a needlessly short part;
        // the last space is used instead.
        const QString text = QStringLiteral("Hi. aaaa bbbb cccc dddd eeee ffff gggg hhhh");
        const QStringList parts = TextChunker::split(text, 30);
        verifyInvariants(text, parts, 30);
        QCOMPARE(parts.first(), QStringLiteral("Hi. aaaa bbbb cccc dddd eeee "));
    }

    void cutsAfterWhitespace()
    {
        const QString text = QStringLiteral("alpha beta gamma delta epsilon zeta eta theta");
        const QStringList parts = TextChunker::split(text, 20);
        verifyInvariants(text, parts, 20);
        QCOMPARE(parts.first(), QStringLiteral("alpha beta gamma "));
    }

    void wordLongerThanLimit()
    {
        const QString text = QString(25, u'x') + QStringLiteral(" end");
        const QStringList parts = TextChunker::split(text, 10);
        verifyInvariants(text, parts, 10);
        QCOMPARE(parts.at(0), QString(10, u'x'));
        QCOMPARE(parts.at(1), QString(10, u'x'));
    }

    void surrogatePairsAreNotSplit()
    {
        // U+1F600 is a surrogate pair; with an odd limit a naive cut would split one.
        QString text;
        for (int i = 0; i < 20; ++i)
            text += QStringLiteral("😀");
        for (int maxLength : {3, 5, 7}) {
            const QStringList parts = TextChunker::split(text, maxLength);
            verifyInvariants(text, parts, maxLength);
        }
    }

    void crLfIsNotSplit()
    {
        const QString text = QStringLiteral("abcdefghi\r\njklmnopqr");
        const QStringList parts = TextChunker::split(text, 10);
        verifyInvariants(text, parts, 10);
        QCOMPARE(parts.first(), QStringLiteral("abcdefghi"));
        QVERIFY(parts.at(1).startsWith(QStringLiteral("\r\n")));
    }

    void tinyLimit()
    {
        const QString text = QStringLiteral("abc 😀 def");
        verifyInvariants(text, TextChunker::split(text, 1), 2);
    }

    void largeTextInvariants()
    {
        // Mixed text with paragraphs, sentences, CJK and emoji.
        const QStringList pieces = {
            QStringLiteral("The cat sits on the mat. "), QStringLiteral("Is it late? "),
            QStringLiteral("Yes!\n"),                    QStringLiteral("\n"),
            QStringLiteral("今日は晴れです。"),          QStringLiteral("😀 "),
            QStringLiteral("Supercalifragilistic"),      QStringLiteral("\r\n"),
        };
        QRandomGenerator random(42);
        QString text;
        while (text.size() < 23000)
            text += pieces.at(random.bounded(int(pieces.size())));

        const QStringList parts = TextChunker::split(text, 5000);
        verifyInvariants(text, parts, 5000);
        QCOMPARE(parts.size(), 5);
        // Natural boundaries keep parts reasonably long.
        for (qsizetype i = 0; i + 1 < parts.size(); ++i)
            QVERIFY(parts.at(i).size() > 2500);
    }
};

QTEST_GUILESS_MAIN(TestTextChunker)
#include "tst_textchunker.moc"
