#pragma once

#include "core/proofreadingtypes.h"

#include <QString>
#include <QVector>

// Pure text operations on a check result: no UI, no document (see
// doc/proofreading.md, §4, "for text replacement").
namespace ProofreadingText {

// True when `issue` has a replacement and its [offset, offset+length) range
// lies inside `text`; offset/length count UTF-16 units.
bool isApplicable(const QString &text, const ProofreadingIssue &issue);

// Applies the first replacement of every applicable issue, from the end of
// the text to the beginning, and returns the corrected text. An issue whose
// range overlaps an already-applied edit is skipped, so no two edits ever
// fight over one character; an empty replacement list is skipped.
QString corrected(const QString &text, const QVector<ProofreadingIssue> &issues);

} // namespace ProofreadingText