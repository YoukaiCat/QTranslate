#pragma once

#include "core/proofreadingtypes.h"

#include <QString>

// User-facing description of a failed text check. Separate from
// TranslationErrorText: the checking vocabulary never mentions translation
// or an API key (stage 5 sends no credentials).
namespace ProofreadingErrorText {

// E.g. "Network error (Host api.languagetool.org not found)". Empty for a
// successful result.
QString forResult(const ProofreadingResult &result);

} // namespace ProofreadingErrorText