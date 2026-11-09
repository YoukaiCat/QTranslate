#pragma once

#include "core/translationtypes.h"

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

// Value types shared by proofreading services, ProofreadingManager and the UI
// (see doc/proofreading.md, §4, and doc/architecture.md, §4.15).

struct ProofreadingIssue
{
    // offset/length count QChar (UTF-16) units, as in QString; a surrogate
    // pair is two units. A cut must never split a surrogate pair.
    int offset = 0;
    int length = 0;
    QString message;
    QStringList replacements;
    QString ruleId;
    QString category; // spelling | grammar | punctuation | style
    int severity = 0;
};

struct ProofreadingResult
{
    QVector<ProofreadingIssue> issues;
    // Filled only by a provider that returns ready text (the stage-6 LLM); a
    // provider that returns issues (LanguageTool) leaves it empty.
    QString correctedText;
    QString serviceId;
    TranslationError error = TranslationError::None;
    QString errorMessage; // technical details for the user and the log; never the text itself

    bool ok() const { return error == TranslationError::None; }
};

struct ProofreadingRequest
{
    QString text;
    // Internal language code or Languages::autoCode() ("auto").
    QString language;
    // Values from proofreadingCategoryIds(); an empty list means all four.
    QStringList categories;
};

Q_DECLARE_METATYPE(ProofreadingRequest)
Q_DECLARE_METATYPE(ProofreadingResult)
