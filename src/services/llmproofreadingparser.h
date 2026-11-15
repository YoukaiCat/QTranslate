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
    int droppedEdits = 0; // edits that produced no issue: not found in the source,
                          // an empty `original`, an unchanged `replacement`, or not an object
};

// `language` is ProofreadingRequest::language: an internal code, or "auto" to ask the
// model to detect the language and check the text in it.
QString systemPrompt(const QString &language, const QStringList &categories);
// `source` is exactly the text that was sent to the model; offsets index into it.
Parsed parseResponse(const QString &source, const QString &content, const QStringList &categories);
QVector<ProofreadingIssue> diffIssues(const QString &source, const QString &corrected);

} // namespace LlmProofreading
