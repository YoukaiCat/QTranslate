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

// The same for a UTF-8 byte limit: no part is longer than `maxBytes` bytes and
// no "\r\n" is split either.
void verifyUtf8Invariants(const QString &text, const QStringList &parts, int maxBytes)
{
    QCOMPARE(parts.join(QString()), text);
    for (const QString &part : parts) {
        QVERIFY(!part.isEmpty());
        QVERIFY2(part.toUtf8().size() <= maxBytes,
                 qPrintable(QString::number(part.toUtf8().size())));
        QVERIFY2(!part.front().isLowSurrogate(), "surrogate pair split");
        QVERIFY2(!part.back().isHighSurrogate(), "surrogate pair split");
    }
    for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
        QVERIFY2(!(parts.at(i).back() == u'\r' && parts.at(i + 1).front() == u'\n'),
                 "CRLF split");
    }
}

// The same for the offset-preserving split: joining the parts gives the text
// back, every offset is exact and no part is longer than the limit.
void verifyOffsetInvariants(const QString &text, const QVector<TextChunker::Part> &parts,
                            int maxLength)
{
    QString joined;
    int expectedOffset = 0;
    for (const TextChunker::Part &part : parts) {
        QVERIFY(!part.text.isEmpty());
        QCOMPARE(part.offset, expectedOffset);
        QVERIFY2(part.text.size() <= maxLength, qPrintable(QString::number(part.text.size())));
        QVERIFY2(!part.text.front().isLowSurrogate(), "surrogate pair split");
        QVERIFY2(!part.text.back().isHighSurrogate(), "surrogate pair split");
        joined += part.text;
        expectedOffset += int(part.text.size());
    }
    QCOMPARE(joined, text);
    if (!parts.isEmpty()) {
        QCOMPARE(parts.last().offset + int(parts.last().text.size()),
                 int(text.size()));
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

    void splitWithOffsetsEmptyText()
    {
        QVERIFY(TextChunker::splitWithOffsets(QString(), 10).isEmpty());
    }

    void splitWithOffsetsExactLimit()
    {
        const QString text = QString(10, u'x');
        const QVector<TextChunker::Part> parts = TextChunker::splitWithOffsets(text, 10);
        QCOMPARE(parts.size(), 1);
        QCOMPARE(parts.first().text, text);
        QCOMPARE(parts.first().offset, 0);
    }

    void splitWithOffsetsThreeParts()
    {
        const QString text = QStringLiteral("alpha beta gamma delta epsilon zeta eta theta");
        const QVector<TextChunker::Part> parts = TextChunker::splitWithOffsets(text, 20);
        verifyOffsetInvariants(text, parts, 20);
        QCOMPARE(parts.size(), 3);
        for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
            QCOMPARE(parts.at(i).offset + int(parts.at(i).text.size()),
                     parts.at(i + 1).offset);
        }
        QCOMPARE(parts.last().offset + int(parts.last().text.size()), int(text.size()));
    }

    void splitWithOffsetsSurrogatePairs()
    {
        // U+1F600 is a surrogate pair; with an odd limit a naive cut would split one.
        QString text;
        for (int i = 0; i < 20; ++i)
            text += QStringLiteral("😀");
        for (int maxLength : {3, 5, 7})
            verifyOffsetInvariants(text, TextChunker::splitWithOffsets(text, maxLength),
                                   maxLength);
    }

    void splitWithOffsetsCrLf()
    {
        const QString text = QStringLiteral("abcdefghi\r\njklmnopqr");
        const QVector<TextChunker::Part> parts = TextChunker::splitWithOffsets(text, 10);
        verifyOffsetInvariants(text, parts, 10);
        QCOMPARE(parts.first().text, QStringLiteral("abcdefghi"));
        QCOMPARE(parts.at(1).offset, 9);
        QVERIFY(parts.at(1).text.startsWith(QStringLiteral("\r\n")));
    }

    void splitWithOffsetsTinyLimit()
    {
        // A limit below one code point is raised to two UTF-16 units.
        const QString text = QStringLiteral("abc 😀 def");
        verifyOffsetInvariants(text, TextChunker::splitWithOffsets(text, 1), 2);
    }

    void utf8EmptyText()
    {
        QVERIFY(TextChunker::splitUtf8(QString(), 10).isEmpty());
    }

    void utf8ShortTextIsOnePart()
    {
        const QString text = QStringLiteral("Hello, world.\n\nSecond paragraph.");
        QCOMPARE(TextChunker::splitUtf8(text, 5000), QStringList{text});
        QCOMPARE(TextChunker::splitUtf8(text, int(text.toUtf8().size())), QStringList{text});
    }

    void utf8CutsAfterSentence()
    {
        // ASCII: a byte and a UTF-16 code unit are the same.
        const QString text = QStringLiteral("One two three four. Five six seven eight nine ten.");
        const QStringList parts = TextChunker::splitUtf8(text, 30);
        verifyUtf8Invariants(text, parts, 30);
        QCOMPARE(parts.first(), QStringLiteral("One two three four. "));
    }

    void utf8CutsAfterSentenceInCyrillic()
    {
        // Two bytes per character: 48 bytes are 24 characters.
        const QString text = QStringLiteral("Это первое предложение. Это второе предложение.");
        QCOMPARE(TextChunker::splitUtf8(text, int(text.toUtf8().size())), QStringList{text});
        const QStringList parts = TextChunker::splitUtf8(text, 48);
        verifyUtf8Invariants(text, parts, 48);
        QCOMPARE(parts, (QStringList{QStringLiteral("Это первое предложение. "),
                                     QStringLiteral("Это второе предложение.")}));
    }

    void utf8CutsAfterFullWidthSentenceEnd()
    {
        // Three bytes per character: 36 bytes are 12 characters.
        const QString text = QStringLiteral("今天天气很好。明天会下雨吗？周末会很暖和");
        const QStringList parts = TextChunker::splitUtf8(text, 36);
        verifyUtf8Invariants(text, parts, 36);
        QCOMPARE(parts, (QStringList{QStringLiteral("今天天气很好。"),
                                     QStringLiteral("明天会下雨吗？"),
                                     QStringLiteral("周末会很暖和")}));
    }

    void utf8SurrogatePairsAreNotSplit()
    {
        // U+1F600 takes 4 UTF-8 bytes and two UTF-16 code units.
        QString text;
        for (int i = 0; i < 20; ++i)
            text += QStringLiteral("😀");
        for (int maxBytes : {1, 4, 5, 6, 7}) {
            const QStringList parts = TextChunker::splitUtf8(text, maxBytes);
            verifyUtf8Invariants(text, parts, qMax(maxBytes, 4));
            QCOMPARE(parts.size(), 20); // one code point per part
        }
        // Two 4-byte code points fit into 9 bytes.
        const QStringList pairs = TextChunker::splitUtf8(text, 9);
        verifyUtf8Invariants(text, pairs, 9);
        QCOMPARE(pairs.size(), 10);
        QCOMPARE(pairs.first(), QStringLiteral("😀😀"));
    }

    void utf8CrLfIsNotSplit()
    {
        // The limit would cut exactly between "\r" and "\n".
        const QString text = QStringLiteral("abcdefghi\r\njklmnopqr");
        const QStringList parts = TextChunker::splitUtf8(text, 10);
        verifyUtf8Invariants(text, parts, 10);
        QCOMPARE(parts.first(), QStringLiteral("abcdefghi"));
        QVERIFY(parts.at(1).startsWith(QStringLiteral("\r\n")));
    }

    void utf8CutsAfterLineBreakWithCrLf()
    {
        const QString text = QStringLiteral("aaaa bbbb\r\ncccc dddd");
        const QStringList parts = TextChunker::splitUtf8(text, 12);
        verifyUtf8Invariants(text, parts, 12);
        QCOMPARE(parts, (QStringList{QStringLiteral("aaaa bbbb\r\n"),
                                     QStringLiteral("cccc dddd")}));
    }

    void utf8WordLongerThanLimit()
    {
        // No boundary in the second half: a hard cut at a code-point boundary.
        const QString text = QString(10, u'ж') + QStringLiteral(" end");
        const QStringList parts = TextChunker::splitUtf8(text, 8);
        verifyUtf8Invariants(text, parts, 8);
        QCOMPARE(parts.first(), QString(4, u'ж'));
        QCOMPARE(parts.at(1), QString(4, u'ж'));
    }

    void utf8TinyLimit()
    {
        // A limit below 4 bytes is raised to one 4-byte code point.
        const QString text = QStringLiteral("abc 😀 def");
        const QStringList parts = TextChunker::splitUtf8(text, 1);
        verifyUtf8Invariants(text, parts, 4);
        QCOMPARE(parts, (QStringList{QStringLiteral("abc "), QStringLiteral("😀"),
                                     QStringLiteral(" def")}));
    }

    void utf8LargeTextInvariants()
    {
        // Mixed text with paragraphs, sentences, Cyrillic, CJK and emoji.
        const QStringList pieces = {
            QStringLiteral("The cat sits on the mat. "),
            QStringLiteral("Кот сидит на коврике. "),
            QStringLiteral("今日は晴れです。"),
            QStringLiteral("😀🙂 "),
            QStringLiteral("Is it late? Yes!\r\n"),
            QStringLiteral("\r\n"),
        };
        QRandomGenerator random(42);
        QString text;
        int textBytes = 0;
        while (textBytes < 30000) {
            const QString piece = pieces.at(random.bounded(int(pieces.size())));
            text += piece;
            textBytes += int(piece.toUtf8().size());
        }

        const QStringList parts = TextChunker::splitUtf8(text, 500);
        verifyUtf8Invariants(text, parts, 500);
        // Natural boundaries keep parts reasonably long.
        for (qsizetype i = 0; i + 1 < parts.size(); ++i)
            QVERIFY2(parts.at(i).toUtf8().size() > 250,
                     qPrintable(QString::number(parts.at(i).toUtf8().size())));
    }
};

QTEST_GUILESS_MAIN(TestTextChunker)
#include "tst_textchunker.moc"
