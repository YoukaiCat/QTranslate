#pragma once

#include "core/proofreadingtypes.h"

#include <QString>
#include <QStringList>
#include <QVector>

// Pure functions that interpret the answers of the LLM proofreading provider
// (see doc/roadmap/qtranslate/12-llm-proofreading); tested on saved answers
// without network access. No network, no widgets and no logging: the checked
// text and the model answer never reach any output except the returned issues,
// and every message the parser adds itself is a fixed literal.
namespace LlmProofreading {

enum class Status {
    Edits,     // the answer is an edits document, possibly with no usable edit
    PlainText, // the answer is the corrected text, issues come from the diff
    Malformed, // the answer is empty or JSON that is not an edits document
};

struct Parsed
{
    QVector<ProofreadingIssue> issues;
    QString correctedText; // filled only for Status::PlainText
    Status status = Status::Malformed;
    // Edits that could not be placed in the text: an entry that is not an
    // object, an empty `original`, an `original` not found in the source, an
    // offset that splits a surrogate pair, or an edit overlapping an accepted one.
    int droppedEdits = 0;
    // Edits dropped by the filter, which is expected rather than a broken
    // answer: a known category the check did not ask for, or a `replacement`
    // equal to `original`.
    int filteredEdits = 0;
};

// `language` is ProofreadingRequest::language: an internal code, or "auto" to ask the
// model to detect the language and check the text in it.
QString systemPrompt(const QString &language, const QStringList &categories);
// `source` is exactly the text that was sent to the model; offsets index into it.
Parsed parseResponse(const QString &source, const QString &content, const QStringList &categories);
QVector<ProofreadingIssue> diffIssues(const QString &source, const QString &corrected);

} // namespace LlmProofreading
