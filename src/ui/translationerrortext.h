#pragma once

#include "core/translationtypes.h"

#include <QString>

// User-facing description of a failed translation, shared by the windows that
// show translations.
namespace TranslationErrorText {

// E.g. "Network error (Host translate.googleapis.com not found)". Empty for a
// successful result.
QString forResult(const TranslationResult &result);

} // namespace TranslationErrorText
