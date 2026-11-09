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

// UTF-16 units of the code point at `pos`: two for a valid surrogate pair.
qsizetype codePointLength(const QString &text, qsizetype pos)
{
    return text.at(pos).isHighSurrogate() && pos + 1 < text.size()
                   && text.at(pos + 1).isLowSurrogate()
               ? 2
               : 1;
}

// UTF-8 size of the code point at `pos`: 1 for ASCII, 2 for U+0080..U+07FF,
// 3 for U+0800..U+FFFF and 4 for a surrogate pair. A lone surrogate is
// invalid; 3 bytes is a conservative upper bound for it, and the defensive
// loop in splitUtf8 checks the real encoded size.
qsizetype codePointSize(const QString &text, qsizetype pos)
{
    const QChar c = text.at(pos);
    if (c.isHighSurrogate() && pos + 1 < text.size() && text.at(pos + 1).isLowSurrogate())
        return 4;
    if (c.unicode() < 0x80)
        return 1;
    if (c.isSurrogate())
        return 3;
    return c.unicode() < 0x800 ? 2 : 3;
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

QVector<Part> splitWithOffsets(const QString &text, int maxLength)
{
    QVector<Part> parts;
    int offset = 0;
    for (const QString &part : split(text, maxLength)) {
        parts.append(Part{part, offset});
        offset += int(part.size());
    }
    return parts;
}

QStringList splitUtf8(const QString &text, int maxBytes)
{
    QStringList parts;
    if (text.isEmpty())
        return parts;

    // One code point must fit into a part.
    const qsizetype limit = qMax(maxBytes, 4);
    const qsizetype half = limit / 2;
    qsizetype from = 0;
    while (from < text.size()) {
        // The longest prefix of the rest that fits into the byte limit, and
        // the natural boundaries seen in the second half of the window.
        qsizetype end = from;
        qsizetype bytes = 0;
        qsizetype blankLine = 0;
        qsizetype lineBreak = 0;
        qsizetype sentenceEnd = 0;
        qsizetype whitespace = 0;
        while (end < text.size()) {
            const qsizetype size = codePointSize(text, end);
            if (bytes + size > limit)
                break;
            bytes += size;
            end += codePointLength(text, end);

            // Natural boundaries only count in the second half, so that parts
            // do not get needlessly short.
            if (bytes <= half)
                continue;
            const qsizetype n = end - from;
            const QChar last = text.at(end - 1);
            if (last == u'\n') {
                lineBreak = end;
                if ((n >= 2 && text.at(end - 2) == u'\n')
                    || (n >= 3 && text.at(end - 2) == u'\r' && text.at(end - 3) == u'\n')) {
                    blankLine = end;
                }
            } else if (isFullWidthSentenceEnd(last)) {
                sentenceEnd = end;
            } else if (last.isSpace()
                       && !(last == u'\r' && end < text.size() && text.at(end) == u'\n')) {
                whitespace = end;
                if (n >= 2 && isSentenceEnd(text.at(end - 2)))
                    sentenceEnd = end;
            }
        }

        // The strongest boundary wins; otherwise cut at the last code point
        // that fits. A rest that fits at all is one part.
        qsizetype cut = end;
        if (end < text.size()) {
            if (whitespace)
                cut = whitespace;
            if (sentenceEnd)
                cut = sentenceEnd;
            if (lineBreak)
                cut = lineBreak;
            if (blankLine)
                cut = blankLine;
        }

        qsizetype length = cut - from;
        // Never cut between "\r" and "\n".
        if (length > 1 && cut < text.size() && text.at(cut - 1) == u'\r'
            && text.at(cut) == u'\n') {
            --length;
        }

        // The sizes above must fit already; this only guards against a lone
        // surrogate, whose UTF-8 size differs from the estimate.
        const qsizetype minLength = codePointLength(text, from);
        while (length > minLength && text.mid(from, length).toUtf8().size() > limit)
            --length;

        parts << text.mid(from, length);
        from += length;
    }
    return parts;
}

} // namespace TextChunker
