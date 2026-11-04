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

} // namespace TextChunker
