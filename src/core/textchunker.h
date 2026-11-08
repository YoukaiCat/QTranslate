#pragma once

#include <QString>
#include <QStringList>

// Splits long text into parts that fit a service's request size limit.
namespace TextChunker {

// Splits `text` into parts of at most `maxLength` UTF-16 code units, so that
// joining the parts gives back `text` exactly. Cuts preferably after a blank
// line, a line break or the end of a sentence (looked for in the second half of
// the window), then after any whitespace, and as a last resort anywhere except
// inside a surrogate pair or a "\r\n". Returns an empty list for empty text.
QStringList split(const QString &text, int maxLength);

// Splits `text` into parts whose `toUtf8().size()` is at most `maxBytes`, so
// that joining the parts gives back `text` exactly. Cuts preferably after a
// blank line, a line break or the end of a sentence (looked for in the second
// half of the byte window), then after any whitespace; a UTF-8 sequence, a
// surrogate pair and a "\r\n" are never split. The effective limit is
// qMax(maxBytes, 4), one code point. Returns an empty list for empty text.
QStringList splitUtf8(const QString &text, int maxBytes);

} // namespace TextChunker
