#pragma once

#include <span>
#include <vector>

#include "cursiv/services/Diagnostic.hpp"
#include "cursiv/services/JsonValue.hpp"
#include "cursiv/workspace/Document.hpp"

// The "did you mean" fixes of a linking error: names in the reference's scope
// within a bounded edit distance. Computed on request, never by validation.

namespace cursiv::detail {

// The rows of each diagnostic, in order: up to three replacements, nearest
// first. None for a diagnostic that is not a not-found reference, or past the
// first sites of its document. The references are walked once per request.
[[nodiscard]] std::vector<JsonValue::Array> spelling_actions(
    const Document& document, std::span<const Diagnostic* const> diagnostics);

}  // namespace cursiv::detail
