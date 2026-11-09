#include "core/proofreadingtext.h"

#include <algorithm>

namespace {

// The order in which the edits are applied: from the end of the text to the
// beginning, the longer range first at the same offset, and the rule id makes
// ties deterministic.
bool editOrder(const ProofreadingIssue &left, const ProofreadingIssue &right)
{
    if (left.offset != right.offset)
        return left.offset > right.offset;
    if (left.length != right.length)
        return left.length > right.length;
    return left.ruleId < right.ruleId;
}

bool splitsSurrogatePair(const QString &text, int offset)
{
    return offset > 0 && offset < text.size() && text.at(offset).isLowSurrogate()
        && text.at(offset - 1).isHighSurrogate();
}

} // namespace

namespace ProofreadingText {

bool isApplicable(const QString &text, const ProofreadingIssue &issue)
{
    if (issue.replacements.isEmpty() || issue.offset < 0 || issue.length < 0
        || issue.offset > text.size() || issue.length > text.size() - issue.offset) {
        return false;
    }
    // A replacement must not cut a surrogate pair in half, neither at the
    // start nor at the end of the range.
    if (splitsSurrogatePair(text, issue.offset))
        return false;
    return !splitsSurrogatePair(text, issue.offset + issue.length);
}

QString corrected(const QString &text, const QVector<ProofreadingIssue> &issues)
{
    QVector<ProofreadingIssue> applicable;
    applicable.reserve(issues.size());
    for (const ProofreadingIssue &issue : issues) {
        if (isApplicable(text, issue))
            applicable.append(issue);
    }

    std::sort(applicable.begin(), applicable.end(), editOrder);

    QString result = text;
    int appliedFrom = text.size();
    for (const ProofreadingIssue &issue : applicable) {
        if (issue.offset + issue.length > appliedFrom)
            continue;
        result.replace(issue.offset, issue.length, issue.replacements.first());
        appliedFrom = issue.offset;
    }
    return result;
}

} // namespace ProofreadingText