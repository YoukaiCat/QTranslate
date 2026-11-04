#include "core/textchunker.h"

#include <QtGlobal>

namespace {

bool isSentenceEnd(QChar c)
{
    return c == u'.' || c == u'!' || c == u'?' || c == u'…';
}

// Full-width terminators (CJK) are not followed by a space.
bool isFullWidthSentenceEnd(QChar c)
{
    return c == u'。' || c == u'！' || c == u'？';
}

// Finds where to cut the part of `text` that starts at `from` and is longer
// than `maxLength`. Returns the length of the part, 1..maxLength.
qsizetype cutLength(const QString &text, qsizetype from, qsizetype maxLength)
{
    // For a cut after `n` characters: the last character of the part and the
    // first character of the rest.
    const auto last = [&](qsizetype n) { return text.at(from + n - 1); };
    const auto next = [&](qsizetype n) { return text.at(from + n); }; // exists: rest is non-empty
    const auto splitsCrLf = [&](qsizetype n) { return last(n) == u'\r' && next(n) == u'\n'; };

    const auto blankLine = [&](qsizetype n) {
        if (n < 2 || last(n) != u'\n')
            return false;
        if (last(n - 1) == u'\n')
            return true;
        return n >= 3 && last(n - 1) == u'\r' && last(n - 2) == u'\n';
    };
    const auto lineBreak = [&](qsizetype n) { return last(n) == u'\n'; };
    const auto sentenceEnd = [&](qsizetype n) {
        if (isFullWidthSentenceEnd(last(n)))
            return true;
        return n >= 2 && last(n).isSpace() && !splitsCrLf(n) && isSentenceEnd(last(n - 1));
    };
    const auto whitespace = [&](qsizetype n) { return last(n).isSpace() && !splitsCrLf(n); };

    // Natural boundaries only count in the second half, so that parts do not
    // get needlessly short.
    const auto findBackward = [maxLength](const auto &isBoundary, qsizetype lowerBound) {
        for (qsizetype n = maxLength; n > lowerBound; --n) {
            if (isBoundary(n))
                return n;
        }
        return qsizetype(0);
    };
    const qsizetype half = maxLength / 2;
    if (const qsizetype n = findBackward(blankLine, half))
        return n;
    if (const qsizetype n = findBackward(lineBreak, half))
        return n;
    if (const qsizetype n = findBackward(sentenceEnd, half))
        return n;
    if (const qsizetype n = findBackward(whitespace, 0))
        return n;

    // A single "word" longer than the window: cut it, but keep surrogate pairs
    // and "\r\n" together.
    qsizetype n = maxLength;
    if ((last(n).isHighSurrogate() && next(n).isLowSurrogate()) || splitsCrLf(n))
        --n;
    return n;
}

} // namespace

namespace TextChunker {

QStringList split(const QString &text, int maxLength)
{
    QStringList parts;
    if (text.isEmpty())
        return parts;

    // A surrogate pair must fit into a part.
    const qsizetype limit = qMax(maxLength, 2);
    qsizetype from = 0;
    while (text.size() - from > limit) {
        const qsizetype length = cutLength(text, from, limit);
        parts << text.mid(from, length);
        from += length;
    }
    parts << text.mid(from);
    return parts;
}

} // namespace TextChunker
